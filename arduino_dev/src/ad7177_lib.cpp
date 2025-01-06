#include <Arduino.h>
#include "ad7177_lib.h"
#include <SPI.h>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>


// TODO
//  - cycle through ADC channels
//  - CRC checking
//  - average data
//  - m & c output cal
//

// Reserve memory for SPIClass instance (enough space for the object)
alignas(SPIClass) uint8_t spi_adc_buf[sizeof(SPIClass)];
SPIClass* SPI_ADC = nullptr;

#define ADC_CH 4

// Mark as volatile since these values can be updated during interrupt
volatile bool ad7177_enable_isr;
volatile bool ad7177_active;
volatile bool ad7177_discard_next_sample;
volatile uint16_t ad7177_ch_active;    // ADC Active Chs
volatile uint16_t ad7177_ch_valid;
uint32_t ad7177_array[ADC_CH];  // ADC Readback Value

uint32_t ad7177_array_cb[ADC_CH];
uint16_t ad7177_ch_valid_cb;

int8_t pin_ad7177_sclk;
int8_t pin_ad7177_miso;
int8_t pin_ad7177_mosi;
int8_t pin_ad7177_cs;
int8_t pin_ad7177_int;

TaskHandle_t ad7177_task_handle;
TaskHandle_t adc_cb_task_handle;

volatile adc_cb_t adc_cb = NULL;

void ad7177_callback(adc_cb_t cb){
  adc_cb = cb;
}

// Force inline since called in ISR
inline __attribute__((always_inline)) void ad7177_int_pause() {
  if (ad7177_enable_isr){
    detachInterrupt(digitalPinToInterrupt(pin_ad7177_int));
    digitalWrite(pin_ad7177_cs, HIGH);
  }
}

// Queue read of data register on MISO falling transition
void IRAM_ATTR ad7177_data_isr() {
  if (ad7177_active && ad7177_enable_isr) {
    // Disable ISR until ADC sample is handled
    ad7177_int_pause();
    ad7177_enable_isr = false;

    // Notify ISR task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(ad7177_task_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

inline void ad7177_int_resume() {
  if (ad7177_enable_isr) {
    attachInterrupt(digitalPinToInterrupt(pin_ad7177_int), ad7177_data_isr, FALLING);
    digitalWrite(pin_ad7177_cs, LOW);
  }
}


void adc_cb_task(void *pvParameters) {
  while (true) {
    // Wait for notification from the ad7177_task
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Call the callback function with the latest data
    adc_cb(ad7177_array_cb, ad7177_ch_valid_cb);
  }
}

void ad7177_task(void *pvParameters) {
  while (true) {
    // Wait for notification from ISR
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Read ADC and save value
    int64_t ret   = ad7177_read(0x04, 32);
    uint32_t data = (((uint32_t) ret) & 0xFFFFFFFF) >> 8;
    uint32_t ch   = (((uint32_t) ret) & 0x3);

    // Keep sample
    if (!ad7177_discard_next_sample) {
      ad7177_array[ch] = data;
      ad7177_ch_valid |= (1 << ch);

      // All channels are valid, call callback function
      if (adc_cb && ad7177_active && !((ad7177_ch_active & ad7177_ch_valid) ^ ad7177_ch_active)) {
        // Copy the ADC data and channel validity
        memcpy(ad7177_array_cb, (const uint32_t *)ad7177_array, sizeof(ad7177_array));
        ad7177_ch_valid_cb = ad7177_ch_valid;

        xTaskNotifyGive(adc_cb_task_handle);

        // Reset ch_valid to begin taking next samples
        ad7177_ch_valid = 0;
      }
    }
    // Discard sample
    else {
      ad7177_discard_next_sample = false;
    }

    // Re-enable ISR after handling ADC data
    if (ad7177_active) {
      ad7177_enable_isr = true;
      ad7177_int_resume();
    }
  }
}

// Force inline

void ad7177_start() {
  // Discard next sample and reset valid data so new data can be collected
  ad7177_discard_next_sample = true;
  ad7177_ch_valid = 0;

  // Enable ISR
  ad7177_active = true;
  ad7177_enable_isr = true;
  ad7177_int_resume();
}

void ad7177_stop() {
  // Detach ISR
  ad7177_int_pause();
  ad7177_active = false;
  ad7177_enable_isr = false;
}

void ad7177_active_ch(uint16_t ch) {
  ad7177_ch_active = ((1 << ADC_CH) - 1) & ch;
}

void ad7177_set_rate(ad7177_sample_rate_t rate) {
  ad7177_write(0x28, rate, 16);
}

void ad7177_config_ch(ad7177_ch_t ch, ad7177_input_t ainpos, ad7177_input_t ainneg, bool enable) {
  uint8_t addr;
  uint8_t cur_ch;
  uint64_t data = 0;

  switch(ch) {
    case AD7177_CH0:
      addr   = 0x10;
      cur_ch = 0;
      break;
    case AD7177_CH1:
      addr   = 0x11;
      cur_ch = 1;
      break;
    case AD7177_CH2:
      addr   = 0x12;
      cur_ch = 2;
      break;
    case AD7177_CH3:
      addr   = 0x13;
      cur_ch = 3;
      break;
    default:
      return;
  }

  // Setup channel
  ad7177_ch_active |= (enable << cur_ch);
  data = (enable << 15) | (ainpos << 5) | (ainneg << 0);
  ad7177_write(addr, data, 16);
}

// rw = 0 for write, 1 for read
int64_t ad7177_transfer(uint8_t rw, uint8_t cmd, uint64_t data, uint32_t num_bits) {
  size_t rx_bytes, tx_bytes, data_bytes;
  uint64_t read = 0;

  // Check that len is multiple of 8
  if (num_bits % 8 != 0){
    //debugE("ADC transaction len must be multiple of 8.\n"
    //  "\t adc_transaction(%d, 0x%x, 0x%x, %d)", rw, cmd, data, num_bits);
    return -1;
  }

  // Remove interrupt since MISO will toggle
  ad7177_int_pause();

  // Prepare for SPI transaction
  SPI_ADC->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(pin_ad7177_cs, LOW);

  // Send cmd
  SPI_ADC->transfer(cmd | ((rw & 0x1) << 6));

  // Read/write ADC data
  data_bytes = num_bits/8;
  for (uint32_t i = 0; i < data_bytes; i++){
    uint8_t write_byte, read_byte;

    write_byte = (data >> (8*(data_bytes-1-i))) & 0xFF;
    read_byte = SPI_ADC->transfer(write_byte);

    read = read | ((uint64_t) (read_byte & 0xFF) << (8*(data_bytes-1-i)));
  }

  // Finish transaction
  digitalWrite(pin_ad7177_cs, HIGH);
  SPI_ADC->endTransaction();
  ad7177_int_resume();

  return (int64_t) read;
}

void ad7177_write(uint8_t addr, uint64_t data, uint32_t num_bits) {
  ad7177_transfer(0, addr, data, num_bits);
}

int64_t ad7177_read(uint8_t addr, uint32_t num_bits) {
  return ad7177_transfer(1, addr, 0x00, num_bits);
}

void ad7177_reset() {
  ad7177_transfer(1, 0xFF, 0xFFFFFFFFFFFFFFFFULL, 64);
}

void ad7177_init(uint8_t spi_intf, int8_t sck, int8_t miso, int8_t mosi, int8_t ss, int8_t isr) {
  SPI_ADC = new (spi_adc_buf) SPIClass(spi_intf);

  // Init global control vars
  ad7177_active = false;
  ad7177_enable_isr = false;
  ad7177_discard_next_sample = true;
  ad7177_ch_active = 0x1;
  ad7177_ch_valid  = 0x0;

  // Save pins
  pin_ad7177_sclk = sck;
  pin_ad7177_miso = miso;
  pin_ad7177_mosi = mosi;
  pin_ad7177_cs   = ss;
  pin_ad7177_int  = isr;

  // Configure CS pin
  pinMode(pin_ad7177_cs, OUTPUT);
  digitalWrite(pin_ad7177_cs, HIGH);

  // Configure interrupt pin
  pinMode(pin_ad7177_int, INPUT_PULLUP);

  // Begin SPI config
  SPI_ADC->begin(pin_ad7177_sclk, pin_ad7177_miso, pin_ad7177_mosi);

  // Set DOUT_RESET (csb must go high before DOUT is used for RDY)
  //  & Append status to data read
  ad7177_write(0x02, 0x0140, 16);

  // Disable SYNC_EN
  ad7177_write(0x06, 0x0000, 16);

  // Use Ext Ref (setup 0)
  ad7177_write(0x20, 0x1300, 16);

  // Set 5 SPS (setup 0)
  ad7177_write(0x28, 0x0514, 16);

  // Configure ch0 - 3
  ad7177_config_ch(AD7177_CH0, AD7177_AIN0, AD7177_AIN1, true);
  ad7177_config_ch(AD7177_CH1, AD7177_AIN0, AD7177_AIN1, false);
  ad7177_config_ch(AD7177_CH2, AD7177_AIN0, AD7177_AIN1, false);
  ad7177_config_ch(AD7177_CH3, AD7177_AIN0, AD7177_AIN1, false);

  // Create adc task and save its handle (higher priority than loop())
  xTaskCreatePinnedToCore(ad7177_task, "ad7177_task", 4096, NULL, 2, &ad7177_task_handle, 1);
  xTaskCreatePinnedToCore(adc_cb_task, "adc_cb_task", 4096, NULL, 1, &adc_cb_task_handle, 1);
}
