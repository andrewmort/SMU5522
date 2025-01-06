#include <Arduino.h>
#include "ad5522_lib.h"
#include <SPI.h>
#include "utility.h"
#include <string>

#define PMU_BUSY_MAX 5        // Max delay waiting for busy (in ms)

//#define PMU_DEBUG


/*
 * How to handle register data?
 *  - store local copy of reigster
 * How to ensure local copy mathces part?
 *  - readback after every write and update local copy?
 * How to ensure UI matches?
 *  - send current status back to UI?
 *  - send back error if write/readback mismatch?
 * 
 */

SPIClass *SPI_PMU;
int8_t pin_ad5522_busy;
int8_t pin_ad5522_cs;
int8_t pin_ad5522_reset;

typedef struct {
                        // 21:18 (def 0) - Enable clamps (set in ch)
                        // 17:14 (def 0) - Enbale comparator outputs (set in ch)
  bool cmp_en;          //    13 (def 0) - Enable comparators (enable ch's cmp_out function)
  bool ch_dutgnd_en;    //    12 (def 0) - Enable per ch dutgnd (otherwise guard in)
  bool guard_alarm_en;  //    11 (def 0) - Enable Guard Alarm
  bool clamp_alarm_en;  //    10 (def 0) - Enable Clamp Alarm
  bool int_sense_en;    //     9 (def 0) - Enable internal sense sort
  bool guard_en;        //     8 (def 0) - Enable guard amp (enable ch's guard_out function)
  uint8_t meas_gain;    //   7:6 (def 0) - Set measout gain
  bool therm_en;        //     5 (def 1) - Enable therm shutdown
  uint8_t therm_thresh; //   4:3 (def 0) - Set thremal shutdown threshold
  bool alarm_latch_en;  //     2 (def 0) - Enable alarm pin as latched
                        //   1:0 (def 0) - Unused (default = 0)
} ad5522_sysctrl_reg_t;

typedef struct {
  bool ch_en;           //    21 (def 0) - Enable channel
  bool hiz_en;          //    20 (def 0) - Enable HiZ
  bool mode;            //    19 (def 0) - Enable FI (otherwise FV)
                        //    18 (def 0) - Unused (default = 0)
  uint8_t range;        // 17:15 (def 3) - Range select
  uint8_t meas_sel;     // 14:13 (def 3) - Measout select
  bool dac_en;          //    12 (def 0) - Enable FI DAC
  bool sys_force_en;    //    11 (def 0) - Enable system force
  bool sys_sense_en;    //    10 (def 0) - Enable system sense
  bool clamp_en;        //     9 (def 0) - Enable clamp
  bool cmp_en;          //     8 (def 0) - Enable comparator output
  bool cmp_fv_en;       //     7 (def 0) - Enable compare voltage (other compare current)
                        // Write:
                        //     6 - Clear latched alarm
                        // Read:
                        //     6 (def 1) - Latch alarm bar
                        //     5 (def 1) - Unlatched alarm bar
} ad5522_pmuctrl_reg_t;

ad5522_sysctrl_reg_t sysctrl_reg;
ad5522_pmuctrl_reg_t pmuctrl_reg[4];

/**************************************************
 *
 * Internal Helper Functions
 *
 **************************************************/

bool ad5522_busy() {
  uint8_t count = 0;
  while ((digitalRead(pin_ad5522_busy) == LOW) && (count++ < PMU_BUSY_MAX)) {
    delay(1);
  }

  if (count >= PMU_BUSY_MAX) {
    return false;
  }

  return true;
}

ad5522_ch_t ad5522_int2ch(int ch) {
  switch(ch) {
    case 0:
      return AD5522_CH0;
    case 1:
      return AD5522_CH1;
    case 2:
      return AD5522_CH2;
    case 3:
      return AD5522_CH3;
  }
  return AD5522_CH0;
}

// rw = 1 for read
int32_t ad5522_transaction(uint8_t rw, uint8_t ch, uint8_t mode, uint32_t data) {
  uint32_t ret = 0;
  uint32_t spi_word;

  // Start SPI transaction (1MHz)
  SPI_PMU->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE1));

  // Select the PMU
  digitalWrite(pin_ad5522_cs, LOW);

  // Create spi word
  spi_word = ((rw & 1) << 28) | ((ch & 0xF) << 24) | ((mode & 3) << 22) | (data & 0x3FFFFF);

  // Write data
  for (uint32_t i = 0; i <= 3; i++){
    uint8_t write_byte;

    write_byte = (spi_word >> (8*(3-i))) & 0xFF;
    SPI_PMU->transfer(write_byte);
  }

  // SYNC toggle between write/read
  digitalWrite(pin_ad5522_cs, HIGH);
  delayMicroseconds(1);
  digitalWrite(pin_ad5522_cs, LOW);

  // Read data
  if (rw) {
    for (uint32_t i = 0; i <= 2; i++){
      uint8_t read_byte;

      read_byte = SPI_PMU->transfer(0xFF);
      ret = ret | (read_byte << (8*(2-i)));

//#ifdef PMU_DEBUG
//      char str_buffer[128];
//      snprintf(str_buffer, sizeof(str_buffer), "read_byte = 0x%X, ret = 0x%X", read_byte, ret);
//      std::string log_msg(str_buffer);
//      log_add(log_msg);
//#endif
    }
  }

  // Wait for busy to go high
  if (!ad5522_busy()) return -1;

  // Deselect the PMU while ending SPI control
  digitalWrite(pin_ad5522_cs, HIGH);

  // End SPI transaction
  SPI_PMU->endTransaction();

  // Print the result (for debugging purposes)
  //debugD("PMU transaction: rw = %d, ch = 0x%X, mode = 0x%X, data = 0x%X, read = 0x%X", rw, ch, mode, data, ret);
#ifdef PMU_DEBUG
  char str_buffer[128];
  snprintf(str_buffer, sizeof(str_buffer), "PMU transaction: rw = %d, ch = 0x%X, mode = 0x%X, data = 0x%X, read = 0x%X", rw, ch, mode, data, ret);
  std::string log_msg(str_buffer);
  log_add(log_msg);
#endif

  return (int32_t) ret;
}

bool ad5522_write(uint8_t ch, uint8_t mode, uint8_t addr, uint32_t data) {
  // Writing to DAC (addr is valid)
  if (mode > 0) {
    data = ((addr & 0x3F) << 16) | (data & 0xFFFF);
  }
  if (ad5522_transaction(0, ch, mode, data) < 0) return false;

  return true;
}

int32_t ad5522_read(uint8_t ch, uint8_t mode, uint8_t addr) {
  uint32_t data = 0;

  // Reading from DAC (addr is valid)
  if (mode > 0) {
    data = ((addr & 0x3F) << 16);
  }
  return ad5522_transaction(1, ch, mode, data);
}

bool ad5522_write_sysctrl() {
  uint32_t write_data = 0;
  int32_t  read_data;

  write_data |= (pmuctrl_reg[AD5522_CH3].clamp_en & 1) << 21;
  write_data |= (pmuctrl_reg[AD5522_CH2].clamp_en & 1) << 20;
  write_data |= (pmuctrl_reg[AD5522_CH1].clamp_en & 1) << 19;
  write_data |= (pmuctrl_reg[AD5522_CH0].clamp_en & 1) << 18;
  write_data |= (pmuctrl_reg[AD5522_CH3].cmp_en   & 1) << 17;
  write_data |= (pmuctrl_reg[AD5522_CH2].cmp_en   & 1) << 16;
  write_data |= (pmuctrl_reg[AD5522_CH1].cmp_en   & 1) << 15;
  write_data |= (pmuctrl_reg[AD5522_CH0].cmp_en   & 1) << 14;
  write_data |= (sysctrl_reg.cmp_en               & 1) << 13;
  write_data |= (sysctrl_reg.ch_dutgnd_en         & 1) << 12;
  write_data |= (sysctrl_reg.guard_alarm_en       & 1) << 11;
  write_data |= (sysctrl_reg.clamp_alarm_en       & 1) << 10;
  write_data |= (sysctrl_reg.int_sense_en         & 1) <<  9;
  write_data |= (sysctrl_reg.guard_en             & 1) <<  8;
  write_data |= (sysctrl_reg.meas_gain            & 3) <<  6;
  write_data |= (sysctrl_reg.therm_en             & 1) <<  5;
  write_data |= (sysctrl_reg.therm_thresh         & 3) <<  3;
  write_data |= (sysctrl_reg.alarm_latch_en       & 1) <<  2;

  // Write to sysctrl register (ch = 00, mode = 00, addr = NA, data = write_data)
  if (!ad5522_write(0, 0, 0, write_data)) return false;

  // Read sysctrl & validate register
  read_data = ad5522_read(0, 0, 0);
  if (read_data < 0 || (((uint32_t) read_data) & 0xFFFFFF) != write_data) return false;
//#ifdef PMU_DEBUG
//  char str_buffer[128];
//  snprintf(str_buffer, sizeof(str_buffer), "PMU sysctrl write: write = 0x%X, read_int = 0x%X, read_uint = 0x%X", write_data, read_data, (uint32_t) read_data);
//  std::string log_msg(str_buffer);
//  log_add(log_msg);
//#endif

  return true;
}

bool ad5522_write_pmuctrl(ad5522_ch_t ch) {
  uint32_t write_data = 0;
  int32_t  read_data;

  write_data |= (pmuctrl_reg[ch].ch_en        & 1) << 21;
  write_data |= (pmuctrl_reg[ch].hiz_en       & 1) << 20;
  write_data |= (pmuctrl_reg[ch].mode         & 1) << 19;
  write_data |= (pmuctrl_reg[ch].range        & 7) << 15;
  write_data |= (pmuctrl_reg[ch].meas_sel     & 3) << 13;
  write_data |= (pmuctrl_reg[ch].dac_en       & 1) << 12;
  write_data |= (pmuctrl_reg[ch].sys_force_en & 1) << 11;
  write_data |= (pmuctrl_reg[ch].sys_sense_en & 1) << 10;
  write_data |= (pmuctrl_reg[ch].clamp_en     & 1) <<  9;
  write_data |= (pmuctrl_reg[ch].cmp_en       & 1) <<  8;
  write_data |= (pmuctrl_reg[ch].cmp_fv_en    & 1) <<  7;

  // Write to pmuctrl register (ch = xxxx, mode = 00, addr = NA, data = write_data)
  if (!ad5522_write((1 << ch), 0, 0, write_data)) return false;

  // Read and validate write
  read_data = ad5522_read((1 << ch), 0, 0);
  if (read_data < 0 || (((uint32_t) read_data) & 0xFFFF80) != write_data) return false;

  return true;
}


/**************************************************
 *
 * External Functions
 *
 **************************************************/


bool ad5522_init(SPIClass *spi, int8_t cs, int8_t rst, int8_t busy) {
  SPI_PMU = spi;
  pin_ad5522_busy  = busy;
  pin_ad5522_cs    = cs;
  pin_ad5522_reset = rst;

  // Set PMU rstb high
  pinMode(pin_ad5522_reset, OUTPUT);
  digitalWrite(pin_ad5522_reset, LOW);
  delayMicroseconds(10);
  digitalWrite(pin_ad5522_reset, HIGH);

  // Set PMU csb high
  pinMode(pin_ad5522_cs, OUTPUT);
  digitalWrite(pin_ad5522_cs, HIGH);

  // Set PMU busy as input (pullup on board?)
  pinMode(pin_ad5522_busy, INPUT);

  // Wait for busy to go high
  if (!ad5522_busy()) return false;

  // Initialize sysctrl register struct
  sysctrl_reg.cmp_en          = 0; // (default = 0)
  //sysctrl_reg.ch_dutgnd_en    = 1; // (default = 0) TODO leave at default for eval board
  sysctrl_reg.ch_dutgnd_en    = 0; // (default = 0)
  sysctrl_reg.guard_alarm_en  = 0; // (default = 0)
  sysctrl_reg.clamp_alarm_en  = 0; // (default = 0)
  sysctrl_reg.int_sense_en    = 0; // (default = 0)
  sysctrl_reg.guard_en        = 0; // (default = 0)
  sysctrl_reg.meas_gain       = AD5522_MEASGAIN_ATTEN; // (default = 0)
  sysctrl_reg.therm_en        = 1; // (default = 1)
  sysctrl_reg.therm_thresh     = 0; // (default = 0) - 130C
  sysctrl_reg.alarm_latch_en  = 0; // (default = 0)
                                   //
  if (!ad5522_write_sysctrl()) return false;

  // Initialize pmuctrl register struct
  for (int i = 0; i < 4; i++) {
    pmuctrl_reg[i].ch_en         = 0; // (default = 0)
    pmuctrl_reg[i].hiz_en        = AD5522_HIZ; // (default = 0)
    pmuctrl_reg[i].mode          = AD5522_FV; // (default = 0)
    pmuctrl_reg[i].range         = AD5522_RNG_2MA; // (default = 3)
    pmuctrl_reg[i].meas_sel      = AD5522_MEAS_ISENSE; // (default = 3)
    pmuctrl_reg[i].dac_en        = 1; // (default = 0)
    pmuctrl_reg[i].sys_force_en  = 0; // (default = 0)
    pmuctrl_reg[i].sys_sense_en  = 0; // (default = 0)
    pmuctrl_reg[i].clamp_en      = 1; // (default = 0)
    pmuctrl_reg[i].cmp_en        = 0; // (default = 0)
    pmuctrl_reg[i].cmp_fv_en     = 0; // (default = 0)
                                      //
    if (!ad5522_write_pmuctrl(ad5522_int2ch(i))) return false;
  }

#ifdef PMU_DEBUG
  char str_buffer[128];
  snprintf(str_buffer, sizeof(str_buffer), "PMU finished init");
  std::string log_msg(str_buffer);
  log_add(log_msg);
#endif

  return true;
}

bool ad5522_extrange_always_on() {
  // TODO external range always enable sequence - see page 49 footnote 2
  // Maybe this should be a function called by smu code?
  return false;
}

bool ad5522_set_state(ad5522_ch_t ch, ad5522_state_t state) {
  switch (state) {
    case AD5522_HIZ:
    case AD5522_ENABLE:
      break;
    default:
      return false;
  }
  pmuctrl_reg[ch].hiz_en = state;

  if (state == AD5522_ENABLE) {
    pmuctrl_reg[ch].ch_en         = 1;
  }

  return ad5522_write_pmuctrl(ch);
}

bool ad5522_set_mode(ad5522_ch_t ch, ad5522_mode_t mode) {
  switch (mode) {
    case AD5522_FV:
    case AD5522_FI:
      break;
    default:
      return false;
  }

  pmuctrl_reg[ch].mode = mode;
  return ad5522_write_pmuctrl(ch);
}

bool ad5522_set_range(ad5522_ch_t ch, ad5522_range_t range) {
  switch (range) {
    case AD5522_RNG_5UA:
    case AD5522_RNG_20UA:
    case AD5522_RNG_200UA:
    case AD5522_RNG_2MA:
    case AD5522_RNG_EXT:
      break;
    default:
      return false;
  }

  pmuctrl_reg[ch].range = range;
  return ad5522_write_pmuctrl(ch);
}

bool ad5522_set_dac(ad5522_ch_t ch, ad5522_dac_t dac, uint16_t code){
  int32_t  read_data;

  switch (dac) {
    case AD5522_DAC_FI_5UA:
    case AD5522_DAC_FI_20UA:
    case AD5522_DAC_FI_200UA:
    case AD5522_DAC_FI_2MA:
    case AD5522_DAC_FI_EXT:
    case AD5522_DAC_FV:
    case AD5522_DAC_CLLV:
    case AD5522_DAC_CLHV:
    case AD5522_DAC_CLLI:
    case AD5522_DAC_CLHI:
      break;
    default:
      return false;
  }

  // ch = ch, mode = 3 (X1), addr = dac, data = code
  if (!ad5522_write(1 << ch, 3, dac, code)) return false;

  // Read & validate
  read_data = ad5522_read(1 << ch, 3, dac);
  if (read_data < 0 || (((uint32_t) read_data) & 0xFFFF) != code) return false;

  return true;
}
