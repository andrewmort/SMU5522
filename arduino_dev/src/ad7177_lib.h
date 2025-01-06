#ifndef AD7177_LIB_H
#define AD7177_LIB_H

/****************************************
 *  AD7177 Defines
 ***************************************/

//typedef void (*adc_cb_t)(uint32_t);
typedef void (*adc_cb_t)(uint32_t *results, uint16_t valid);

typedef enum {
  AD7177_AIN0      = 0x00,
  AD7177_AIN1      = 0x01,
  AD7177_AIN2      = 0x02,
  AD7177_AIN3      = 0x03,
  AD7177_AIN4      = 0x04,
  AD7177_TEMP_POS  = 0x11,
  AD7177_TEMP_NEG  = 0x12,
  AD7177_REF_POS   = 0x15,
  AD7177_REF_NEG   = 0x16
} ad7177_input_t;

typedef enum {
  AD7177_10000SP  = 0x0507,
  AD7177_5000SPS  = 0x0508,
  AD7177_2500SPS  = 0x0509,
  AD7177_1000SPS  = 0x050A,
  AD7177_500SPS   = 0x050B,
  AD7177_397SPS   = 0x050C,
  AD7177_200SPS   = 0x050D,
  AD7177_100SPS   = 0x050E,
  AD7177_60SPS    = 0x050F,
  AD7177_50SPS    = 0x0510,
  AD7177_20SPS    = 0x0511, // or 0x0D11
  AD7177_17SPS    = 0x0512, // or 0x0E12
  AD7177_10SPS    = 0x0513,
  AD7177_5SPS     = 0x0514
} ad7177_sample_rate_t;

typedef enum {
  AD7177_CH0    = 0x01,
  AD7177_CH1    = 0x02,
  AD7177_CH2    = 0x04,
  AD7177_CH3    = 0x08,
  AD7177_ALLCH  = 0x0F
} ad7177_ch_t;


void ad7177_init(uint8_t spi_intf, int8_t sck, int8_t miso, int8_t mosi, int8_t ss, int8_t isr);
void ad7177_callback(adc_cb_t cb);

void ad7177_write(uint8_t addr, uint64_t data, uint32_t num_bits);
int64_t ad7177_read(uint8_t addr, uint32_t num_bits);

// Add?
void ad7177_set_rate(ad7177_sample_rate_t rate);
//void ad7177_set_average(uint8_t type, size_t size);
void ad7177_active_ch(uint16_t ch);
void ad7177_config_ch(ad7177_ch_t ch, ad7177_input_t ainpos, ad7177_input_t ainneg, bool enable);
void ad7177_stop();
void ad7177_start();

#endif
