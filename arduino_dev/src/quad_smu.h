#ifndef QUAD_SMU_H
#define QUAD_SMU_H

#include "ad7177_lib.h"
#include "ad5522_lib.h"

#define HOST_NAME "quad_smu"

/****************************************
 *  SPI
 ***************************************/

// Default VSPI(SPI3) Pins - use for ADC data
#define SPIBUS_ADC    VSPI
#define PIN_ADC_CS    5 // PIN_VSPI_CS
#define PIN_ADC_SCLK 18 // PIN_VSPI_SCLK
#define PIN_ADC_MOSI 23 // PIN_VSPI_MOSI
#define PIN_ADC_MISO 19 // PIN_VSPI_MISO
#define PIN_ADC_INT  17

// Default HSPI(SPI2) Pins - use for control (normal SPI)
#define PIN_HSPI_MISO 12
#define PIN_HSPI_MOSI 13
#define PIN_HSPI_SCLK 14

// PMU interface pins
#define PIN_PMU_CS    32
#define PIN_PMU_RST   4
#define PIN_PMU_BUSY  25

// Inamp interface pins
#define PIN_INAMP0_CS  15

/****************************************
 *  System Parameters
 ***************************************/

#define ADC_REF 5
#define ADC_RES ((1 << 24) - 1)

/****************************************
 *  SMU Defines
 ***************************************/

typedef enum {
  CH0 = 0,
  CH1 = 1,
  CH2 = 2,
  CH3 = 3
} smu_ch_t;

typedef enum {
  FV = 0,
  FI = 1
} smu_mode_t;

typedef enum {
  DISABLE = 0,
  STANDBY = 1,
  ENABLE  = 2
} smu_state_t;

typedef enum {
  LOCAL  = 0,
  REMOTE = 1
} smu_sense_t;

typedef enum {
  ADC_MV = 0,
  ADC_MI = 1,
} smu_adc_t;

typedef enum {
  RANGE_5UA   = 0,
  RANGE_20UA  = 1,
  RANGE_200UA = 2,
  RANGE_2MA   = 3,
  RANGE_20MA  = 4,
  RANGE_200MA = 5
} smu_range_t;

typedef enum {
  DAC_FI,
  DAC_FV,
  DAC_CLLV,
  DAC_CLHV,
  DAC_CLLI,
  DAC_CLHI
} smu_dac_t;

typedef enum {
  RATE_FAST,
  RATE_MED,
  RATE_LINE,
  RATE_SLOW
} smu_rate_t;

/****************************************
 *  SMU Functions
 ***************************************/

void adc_callback(int64_t *results, uint16_t valid);
void smu_init();
void smu_set_state(smu_ch_t ch, smu_state_t state);
void smu_set_mode(smu_ch_t ch, smu_mode_t mode);
void smu_set_range(smu_ch_t ch, smu_range_t range);
void smu_set_dac(smu_ch_t ch, smu_dac_t dac, float val);
void smu_set_rate(smu_rate_t rate);
void smu_dac_v2d(smu_ch_t ch, smu_dac_t dac, smu_range_t range, float *val, uint16_t *code);
float smu_adc_d2v(smu_ch_t ch, smu_adc_t adc, smu_range_t range, uint32_t code);
void smu_queue_update();
void smu_process();

#endif
