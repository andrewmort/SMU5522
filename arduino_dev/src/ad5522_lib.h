#ifndef AD5522_LIB_H
#define AD5522_LIB_H

#include <Arduino.h>
#include <SPI.h>

typedef enum {
  AD5522_CH0 = 0,
  AD5522_CH1 = 1,
  AD5522_CH2 = 2,
  AD5522_CH3 = 3
} ad5522_ch_t;

typedef enum {
  AD5522_FV = 0,
  AD5522_FI = 1
} ad5522_mode_t;

typedef enum {
  AD5522_ENABLE = 0,
  AD5522_HIZ = 1
} ad5522_state_t;

typedef enum {
  AD5522_RNG_5UA            = 0,
  AD5522_RNG_20UA           = 1,
  AD5522_RNG_200UA          = 2,
  AD5522_RNG_2MA            = 3,
  AD5522_RNG_EXT            = 4,
  AD5522_RNG_EXT_ALWAYS_OFF = 5,
  AD5522_RNG_EXT_ALWAYS_ON  = 6
} ad5522_range_t;

typedef enum {
  AD5522_DAC_FI_5UA   = 0x08,
  AD5522_DAC_FI_20UA  = 0x09,
  AD5522_DAC_FI_200UA = 0x0A,
  AD5522_DAC_FI_2MA   = 0x0B,
  AD5522_DAC_FI_EXT   = 0x0C,
  AD5522_DAC_FV       = 0x0D,
  AD5522_DAC_CLLV     = 0x15,
  AD5522_DAC_CLHV     = 0x1D,
  AD5522_DAC_CLLI     = 0x14,
  AD5522_DAC_CLHI     = 0x1C
} ad5522_dac_t;

typedef enum {
  AD5522_MEASGAIN_FULL  = 0,
  AD5522_MEASGAIN_ATTEN = 2
} ad5522_measgain_t;

typedef enum {
  AD5522_MEAS_ISENSE = 0,
  AD5522_MEAS_VSENSE = 1,
  AD5522_MEAS_THERM  = 2,
  AD5522_MEAS_HIZ    = 3
} ad5522_meas_t;

bool ad5522_init(SPIClass *spi, int8_t cs, int8_t rst, int8_t busy);
bool ad5522_set_state(ad5522_ch_t ch, ad5522_state_t state);
bool ad5522_set_mode(ad5522_ch_t ch, ad5522_mode_t mode);
bool ad5522_set_range(ad5522_ch_t ch, ad5522_range_t range);
bool ad5522_set_dac(ad5522_ch_t ch, ad5522_dac_t dac, uint16_t code);

#endif
