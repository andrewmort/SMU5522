#include <Arduino.h>
#include <time.h>
#include "quad_smu.h"
#include "utility.h"
#include "ada4254_lib.h"
#include <cmath>
#include <SPI.h>

SPIClass SPI_CTRL(HSPI); // Create an instance for the HSPI bus

//const smu_rate_t smu_rate_fast = {
//  .adc_code = 0x00,
//  .disp_update = 500,
//  .web_update = 500
//};

//#define NUM_CH 4
#define NUM_CH 1
#define MILLIS_PROCESS 500

int8_t pin_inamp_cs[] = {PIN_INAMP0_CS, -1, -1, -1};

typedef enum {
  FIELD_FV    = 0,
  FIELD_FI    = 1,
  FIELD_MV    = 2,
  FIELD_MI    = 3,
  FIELD_CLLI  = 4,
  FIELD_CLHI  = 5,
  FIELD_CLLV  = 6,
  FIELD_CLHV  = 7,
  FIELD_RANGE = 8,
  FIELD_STATE = 9,
  FIELD_MODE  = 10,
  FIELD_SENSE = 11
} smu_control_bitfield_t;

typedef struct {
  float fv;
  float fi;
  float mv;
  float mi;
  float clli;
  float clhi;
  float cllv;
  float clhv;
  smu_range_t range;
  smu_state_t state;
  smu_mode_t  mode;
  smu_sense_t sense;
  float mv_gain;
  float mi_mult;
} smu_control_t;

volatile smu_control_t smu_control[NUM_CH];
volatile uint16_t smu_control_updated[NUM_CH];
volatile unsigned long smu_millis_process;

ADA4254 inamp_array[4];

/**********************************************************
 *
 * Helper Functions
 *
 **********************************************************/

smu_ch_t smu_int2ch(int ch) {
  switch(ch) {
    case 0:
      return CH0;
    case 1:
      return CH1;
    case 2:
      return CH2;
    case 3:
      return CH3;
  }
  return CH0;
}

// Calibrate DAC
// Limit val to valid range
// Return digital DAC code
// Range not required for FV, CLLV, CLHV
void smu_dac_v2d(smu_ch_t ch, smu_dac_t dac, smu_range_t range, float *val, uint16_t *code){
  bool is_idac;
  float val_cal;
  float val_max, val_min;
  float rsense;
  float val_over;
  float codef;

  if (dac == DAC_FI || dac == DAC_CLLI || dac == DAC_CLHI) {
    is_idac = true;

    switch (range) {
      case RANGE_5UA:
        val_max =  5e-6;
        val_min = -5e-6;
        rsense  = 200e3;
        break;
      case RANGE_20UA:
        val_max =  20e-6;
        val_min = -20e-6;
        rsense = 50e3;
        break;
      case RANGE_200UA:
        val_max =  200e-6;
        val_min = -200e-6;
        rsense = 5e3;
        break;
      case RANGE_2MA:
        val_max =  2.0e-3;
        val_min = -2.0e-3;
        rsense = 500;
        break;
      case RANGE_20MA:
        val_max =  20e-3;
        val_min = -20e-3;
        rsense = 50;
        break;
      case RANGE_200MA:
        val_max =  200e-3;
        val_min = -200e-3;
        rsense = 5;
        break;
    }
  } else {
    is_idac = false;
    val_max =  10;
    val_min = -10;
  }

  // Prevent I-clamps from being programmed <250mV around 0V
  if ((dac == DAC_CLLI) && ((*val)*rsense + 0.25F > 0)) {
    *val = -0.25F/rsense;
  } else if ((dac == DAC_CLHI) && ((*val)*rsense - 0.25F < 0)) {
    *val = 0.25F/rsense;
  }
  // Prevent V-clamps from being programmed <500mV apart
  if ((dac == DAC_CLLV) && (smu_control[ch].clhv - (*val) < 0.5F)) {
    *val = smu_control[ch].clhv - 0.5F;
  } else if ((dac == DAC_CLHV) && ((*val) - smu_control[ch].cllv < 0.5F)) {
    *val = smu_control[ch].cllv + 0.5F;
  }

  // Allow 12.5% over on clamps
  if (dac == DAC_CLLI || dac == DAC_CLHI || dac == DAC_CLLV || dac == DAC_CLHV) {
    val_over = (val_max - val_min) * 1.125F/2;
  }
  // Allow 5% over on other DACs
  else {
    val_over = (val_max - val_min) * 1.05F/2;
  }
  val_max = val_max + val_over;
  val_min = val_min - val_over;

  // Ensure user val is within range
  if ((*val) > val_max) {
    *val = val_max;
  } else if ((*val) < val_min) {
    *val = val_min;
  }

  //TODO calibrate DAC value
  val_cal = *val;


  if (is_idac) {
    codef = (val_cal * rsense * 10)/(4.5F * 5) * pow(2.0F,16) + 32768;
  } else {
    uint16_t pmu_offset = 42130;
    codef = (val_cal + (3.5F * 5 * pmu_offset/pow(2.0F,16)))/(4.5F * 5) * pow(2.0F,16);
  }

  // Round code and limit to 0 and 0xFFFF
  *code = (uint16_t) std::min(std::max(roundf(codef), 0.0f), 65535.0f);
}

float smu_adc_d2v(smu_ch_t ch, smu_adc_t adc, smu_range_t range, uint32_t code) {
  float val;
  float rsense;

  val = 2 * ADC_REF * (((float) code / ADC_RES) - 0.5F);

  if (adc == ADC_MI){
    switch (range) {
      case RANGE_5UA:
        rsense  = 200e3;
        break;
      case RANGE_20UA:
        rsense = 50e3;
        break;
      case RANGE_200UA:
        rsense = 5e3;
        break;
      case RANGE_2MA:
        rsense = 500;
        break;
      case RANGE_20MA:
        rsense = 50;
        break;
      case RANGE_200MA:
        rsense = 5;
        break;
    }
    val = (val - (0.45 * 5))/(0.2*10*rsense);
  }

  return val;
}

// TODO Setup ADC callback
//  - get all active ADC values at once
//  - update mv/mi in smu_control
//  - log temperature?
//  - initiate next update/step during sweep
void adc_callback(uint32_t *results, uint16_t valid) {
  for (int k = 0; k < NUM_CH*2; k++){
    if ((valid >> k) & 1) {
      if (k % 2 == 0) {
        smu_control[k/2].mv = smu_adc_d2v(smu_int2ch(k/2), ADC_MV, smu_control[k/2].range, results[k])/smu_control[k/2].mv_gain;
        smu_control_updated[k/2] |= (1 << FIELD_MV);
      } else {
        smu_control[k/2].mi = smu_adc_d2v(smu_int2ch(k/2), ADC_MI, smu_control[k/2].range, results[k]);
        //smu_control[k/2].mi = smu_adc_d2v(smu_int2ch(k/2), ADC_MV, smu_control[k/2].range, results[k]);
        smu_control_updated[k/2] |= (1 << FIELD_MI);
      }
    }
  }
  //TODO initiate next update during sweep
}

ad5522_ch_t smu2ad5522_ch(smu_ch_t ch) {
  switch (ch) {
    case CH0:
      return AD5522_CH0;
      break;
    case CH1:
      return AD5522_CH1;
      break;
    case CH2:
      return AD5522_CH2;
      break;
    case CH3:
      return AD5522_CH3;
      break;
  }
  return AD5522_CH0;
}


// TODO Setup timeout for ADC meas after smu setting change
//  - gate ADC samples until X time after smu setting change
//  - different default times based on range
//  - allow user setting?




// TODO Setup sweep function
//  - set start, step, stop, lin/log
//  - allow logging only certain channels to speed up sweep
//  - allow sweeping all ch together
//  - allow sweeping one one ch while holding others constant
//  - adc callback initiates next sweep point
//

/**********************************************************
 *
 * Global Functions
 *
 **********************************************************/

void smu_init(){
  for(int i = 0; i < NUM_CH; i++) {
    smu_control[i].range = RANGE_2MA;
    smu_control[i].state = DISABLE;
    smu_control[i].mode  = FV;
    smu_control[i].sense  = LOCAL;
    smu_control[i].fv = 0;
    smu_control[i].fi = 0;
    smu_control[i].mv = 0;
    smu_control[i].mi = 0;
    smu_control[i].clli = -2.25e-3;
    smu_control[i].clhi =  2.25e-3;
    smu_control[i].cllv = -11.25;
    smu_control[i].clhv =  11.25;
    smu_control[i].mv_gain =  1;
    smu_control[i].mi_mult =  1e3;

    smu_control_updated[i] = 0xFFFF;
  }

  // Init last UI updates
  smu_millis_process = millis();

  // Initialize ADC
  ad7177_init(SPIBUS_ADC, PIN_ADC_SCLK, PIN_ADC_MISO, PIN_ADC_MOSI, PIN_ADC_CS, PIN_ADC_INT);
  ad7177_callback(adc_callback);
  ad7177_config_ch(AD7177_CH0, AD7177_AIN0, AD7177_AIN1, true); // inamp MV
  ad7177_config_ch(AD7177_CH1, AD7177_AIN2, AD7177_AIN3, true); // pmu MI

  // Setup SPI for control
  SPI_CTRL.begin(PIN_HSPI_SCLK, PIN_HSPI_MISO, PIN_HSPI_MOSI);

  // Initialize PMU
  ad5522_init(&SPI_CTRL, PIN_PMU_CS, PIN_PMU_RST, PIN_PMU_BUSY);

  //TODO Add mutex for SPI_CTRL

  // Initialize INamp
  for (int i = 0; i < NUM_CH; i++) {
    float gain;
    inamp_array[i].begin(&SPI_CTRL, pin_inamp_cs[i]);
    gain = inamp_array[i].set_gain(ADA4254_IX0P5, ADA4254_OX1);
    if (gain < 0) gain = (1/2.0F); //TODO
    smu_control[i].mv_gain = gain;
  }
  /*

  // Init timer to update webpage
  // Init timer to update display

  */
  // Begin taking samples on ADC
  ad7177_start();
}

void smu_set_state(smu_ch_t ch, smu_state_t state) {
  smu_control[ch].state = state;
  smu_control_updated[ch] |= (1 << FIELD_STATE);

  switch (state) {
    case DISABLE:
    case STANDBY:
      ad5522_set_state(smu2ad5522_ch(ch), AD5522_HIZ);
    case ENABLE:
      ad5522_set_state(smu2ad5522_ch(ch), AD5522_ENABLE);
      break;
  }
}

void smu_set_mode(smu_ch_t ch, smu_mode_t mode){
  if (smu_control[ch].mode == mode) return;

  smu_control[ch].mode = mode;
  smu_control_updated[ch] |= (1 << FIELD_MODE);

  switch(mode) {
    case FV:
      smu_set_dac(ch, DAC_FV,   smu_control[ch].mv);
      smu_set_dac(ch, DAC_CLLI, smu_control[ch].clli);
      smu_set_dac(ch, DAC_CLHI, smu_control[ch].clhi);
      ad5522_set_mode(smu2ad5522_ch(ch), AD5522_FV);
    case FI:
      smu_set_dac(ch, DAC_FI,   smu_control[ch].mi);
      smu_set_dac(ch, DAC_CLLV, smu_control[ch].cllv);
      smu_set_dac(ch, DAC_CLHV, smu_control[ch].clhv);
      ad5522_set_mode(smu2ad5522_ch(ch), AD5522_FI);
      break;
  }
}

// FV
//  Set I-clamp level to min value in both ranges
//  Change range
//  Update I-clamp level
// FI
//  Set new FI DAC level
//  Chnage range
void smu_set_range(smu_ch_t ch, smu_range_t range) {
  float val, val_clamp, mi_mult;
  uint16_t code;
  ad5522_range_t ad5522_range;
  ad5522_dac_t   ad5522_dac;

  // Going to larger range, same DAC val is more current, program
  //  clamps to final DAC code (lower current before to change)
  if (smu_control[ch].mode == FV && range > smu_control[ch].range) {
    val_clamp = smu_control[ch].clli;
    smu_dac_v2d(ch, DAC_CLLI, range, &val_clamp, &code);
    ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLLI, code);
    smu_control[ch].clli = val_clamp;
    smu_control_updated[ch] |= (1 << FIELD_CLLI);

    val_clamp = smu_control[ch].clhi;
    smu_dac_v2d(ch, DAC_CLHI, range, &val_clamp, &code);
    ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLHI, code);
    smu_control[ch].clhi = val_clamp;
    smu_control_updated[ch] |= (1 << FIELD_CLHI);
  }

  // Get range key for DAC write
  switch (range) {
    case RANGE_5UA:
      ad5522_range = AD5522_RNG_5UA;
      ad5522_dac   = AD5522_DAC_FI_5UA;
      mi_mult = 1e6;
      break;
    case RANGE_20UA:
      ad5522_range = AD5522_RNG_20UA;
      ad5522_dac   = AD5522_DAC_FI_20UA;
      mi_mult = 1e6;
      break;
    case RANGE_200UA:
      ad5522_range = AD5522_RNG_200UA;
      ad5522_dac   = AD5522_DAC_FI_200UA;
      mi_mult = 1e6;
      break;
    case RANGE_2MA:
      ad5522_range = AD5522_RNG_2MA;
      ad5522_dac   = AD5522_DAC_FI_2MA;
      mi_mult = 1e3;
      break;
    case RANGE_20MA:
    case RANGE_200MA:
      ad5522_range = AD5522_RNG_EXT;
      ad5522_dac   = AD5522_DAC_FI_EXT;
      mi_mult = 1e3;
      break;
  }

  // Set new current range DAC
  if (smu_control[ch].mode == FI) {
    val = smu_control[ch].fi;
    smu_dac_v2d(ch, DAC_FI, range, &val, &code);
    smu_control[ch].fi = val;
    smu_control_updated[ch] |= (1 << FIELD_FI);

    // In external range and going to external range
    //  set DAC to smaller value before range change
    if (range >= RANGE_20MA && smu_control[ch].range >= RANGE_20MA){
      float tmp_val = val;
      uint16_t tmp_code = code;
      smu_dac_v2d(ch, DAC_FI, RANGE_200MA, &tmp_val, &tmp_code);
      ad5522_set_dac(smu2ad5522_ch(ch), ad5522_dac, tmp_code);
    }

    // Not in external range or going to external range
    //  update DAC for new range prior to range change
    else {
      ad5522_set_dac(smu2ad5522_ch(ch), ad5522_dac, code);
    }
  }

  // Set range
  ad5522_set_range(smu2ad5522_ch(ch), ad5522_range);
  if (range >= RANGE_20MA) {
    ;
    //TODO set switch
  }

  // Going to smaller range, same DAC val is less current, program
  //  clamps to final DAC code
  if (smu_control[ch].mode == FV && range < smu_control[ch].range) {
    val_clamp = smu_control[ch].clli;
    smu_dac_v2d(ch, DAC_CLLI, range, &val_clamp, &code);
    ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLLI, code);
    smu_control[ch].clli = val_clamp;
    smu_control_updated[ch] |= (1 << FIELD_CLLI);

    val_clamp = smu_control[ch].clhi;
    smu_dac_v2d(ch, DAC_CLHI, range, &val_clamp, &code);
    ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLHI, code);
    smu_control[ch].clhi = val_clamp;
    smu_control_updated[ch] |= (1 << FIELD_CLHI);
  }

  // In external range and going to external range
  //  set DAC to final value
  if (smu_control[ch].mode == FI && range >= RANGE_20MA
      && smu_control[ch].range >= RANGE_20MA){
    ad5522_set_dac(smu2ad5522_ch(ch), ad5522_dac, code);
  }

  smu_control[ch].mi_mult = mi_mult;

}


void smu_set_dac(smu_ch_t ch, smu_dac_t dac, float val){
  uint16_t code;
  ad5522_dac_t   ad5522_dac;

  // Calibrate and get DAC code
  smu_dac_v2d(ch, dac, smu_control[ch].range, &val, &code);

  // Get correct FI DAC for range
  switch (smu_control[ch].range) {
    case RANGE_5UA:
      ad5522_dac   = AD5522_DAC_FI_5UA;
      break;
    case RANGE_20UA:
      ad5522_dac   = AD5522_DAC_FI_20UA;
      break;
    case RANGE_200UA:
      ad5522_dac   = AD5522_DAC_FI_200UA;
      break;
    case RANGE_2MA:
      ad5522_dac   = AD5522_DAC_FI_2MA;
      break;
    case RANGE_20MA:
    case RANGE_200MA:
      ad5522_dac   = AD5522_DAC_FI_EXT;
      break;
  }

  // Set DAC
  switch(dac) {
    case DAC_FI:
      smu_control[ch].fi = val;
      smu_control_updated[ch] |= (1 << FIELD_FI);
      ad5522_set_dac(smu2ad5522_ch(ch), ad5522_dac, code);
      break;
    case DAC_FV:
      smu_control[ch].fv = val;
      smu_control_updated[ch] |= (1 << FIELD_FV);
      ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_FV, code);
      break;
    case DAC_CLLV:
      smu_control[ch].cllv = val;
      smu_control_updated[ch] |= (1 << FIELD_CLLV);
      ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLLV, code);
      break;
    case DAC_CLHV:
      smu_control[ch].clhv = val;
      smu_control_updated[ch] |= (1 << FIELD_CLHV);
      ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLHV, code);
      break;
    case DAC_CLLI:
      smu_control[ch].clli = val;
      smu_control_updated[ch] |= (1 << FIELD_CLLI);
      ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLLI, code);
      break;
    case DAC_CLHI:
      smu_control[ch].clhi = val;
      smu_control_updated[ch] |= (1 << FIELD_CLHI);
      ad5522_set_dac(smu2ad5522_ch(ch), AD5522_DAC_CLHI, code);
      break;
  }
}

void smu_set_rate(smu_rate_t rate) {
}

void smu_queue_update() {
  for (int i = 0; i < NUM_CH; i++) {
    smu_control_updated[i] = 0xFFFF;
  }
  smu_millis_process -= MILLIS_PROCESS;
}

void smu_process() {
  if (millis() - smu_millis_process > MILLIS_PROCESS) {
    smu_millis_process = millis();

    for (int i = 0; i < NUM_CH; i++) {
      if (smu_control_updated[i] > 0) {
        char str[1024];
        snprintf(str, sizeof(str),"{\"type\":\"smu\",\"ch\":\"%d\"", i);

        if (smu_control_updated[i] & (1 << FIELD_FV)) {
          snprintf(str, sizeof(str), "%s,\"fv\":\"%f\"", str, smu_control[i].fv);
        }
        if (smu_control_updated[i] & (1 << FIELD_FI)) {
          snprintf(str, sizeof(str), "%s,\"fi\":\"%f\"", str, smu_control[i].fi);
        }
        if (smu_control_updated[i] & (1 << FIELD_MV)) {
          snprintf(str, sizeof(str), "%s,\"mv\":\"%f\"", str, smu_control[i].mv);
        }
        if (smu_control_updated[i] & (1 << FIELD_MI)) {
          snprintf(str, sizeof(str), "%s,\"mi\":\"%f\"", str, smu_control[i].mi * smu_control[i].mi_mult);
        }
        if (smu_control_updated[i] & (1 << FIELD_CLLI)) {
          snprintf(str, sizeof(str), "%s,\"clli\":\"%f\"", str, smu_control[i].clli);
        }
        if (smu_control_updated[i] & (1 << FIELD_CLHI)) {
          snprintf(str, sizeof(str), "%s,\"clhi\":\"%f\"", str, smu_control[i].clhi);
        }
        if (smu_control_updated[i] & (1 << FIELD_CLLV)) {
          snprintf(str, sizeof(str), "%s,\"cllv\":\"%f\"", str, smu_control[i].cllv);
        }
        if (smu_control_updated[i] & (1 << FIELD_CLHV)) {
          snprintf(str, sizeof(str), "%s,\"clhv\":\"%f\"", str, smu_control[i].clhv);
        }
        if (smu_control_updated[i] & (1 << FIELD_RANGE)) {
          const char *tmp;
          const char *tmp_unit;
          switch(smu_control[i].range) {
            case RANGE_5UA:
              tmp = "5UA";
              tmp_unit = "uA";
              break;
            case RANGE_20UA:
              tmp = "20UA";
              tmp_unit = "uA";
              break;
            case RANGE_200UA:
              tmp = "200UA";
              tmp_unit = "uA";
              break;
            case RANGE_2MA:
              tmp = "2MA";
              tmp_unit = "mA";
              break;
            case RANGE_20MA:
              tmp = "20MA";
              tmp_unit = "mA";
              break;
            case RANGE_200MA:
              tmp = "200MA";
              tmp_unit = "mA";
              break;
          }
          snprintf(str, sizeof(str), "%s,\"range\":\"%s\"", str, tmp);
          snprintf(str, sizeof(str), "%s,\"unit\":\"%s\"", str, tmp_unit);
        }
        if (smu_control_updated[i] & (1 << FIELD_STATE)) {
          const char *tmp;
          switch(smu_control[i].state) {
            case DISABLE:
              tmp = "DISABLE";
              break;
            case STANDBY:
              tmp = "STANDBY";
              break;
            case ENABLE:
              tmp = "ENABLE";
              break;
          }
          snprintf(str, sizeof(str), "%s,\"state\":\"%s\"", str, tmp);
        }
        if (smu_control_updated[i] & (1 << FIELD_MODE)) {
          const char *tmp;
          switch(smu_control[i].mode) {
            case FV:
              tmp = "FV";
              break;
            case FI:
              tmp = "FI";
              break;
          }
          snprintf(str, sizeof(str), "%s,\"mode\":\"%s\"", str, tmp);
        }
        if (smu_control_updated[i] & (1 << FIELD_SENSE)) {
          const char *tmp;
          switch(smu_control[i].sense) {
            case LOCAL:
              tmp = "LOCAL";
              break;
            case REMOTE:
              tmp = "REMOTE";
              break;
          }
          snprintf(str, sizeof(str), "%s,\"sense\":\"%s\"", str, tmp);
        }

        snprintf(str, sizeof(str), "%s}", str);
        websocket_send(str);
        smu_control_updated[i] = 0;
      }
    }
  }
}
