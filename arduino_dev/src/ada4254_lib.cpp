#include "ada4254_lib.h"
#include "utility.h"

#define INAMP_DEBUG

// Public methods
bool ADA4254::begin(SPIClass *spi, int8_t cs){
  // Set private variables
  _SPI = spi;
  _pin_cs = cs;

  // Setup csb high
  pinMode(_pin_cs, OUTPUT);
  digitalWrite(_pin_cs, HIGH);

  // Read ID register - should read 0x30
  if (read(0x2F) != 0x30) return false;

  // GAIN_MUX (0x00): Gain = 1x
  //if (set_gain(ADA4254_IX0P25, ADA4254_OX1P375) < 0) return false;

  // GPIO_DIR (0x08): Set GPIO3 as output for error detection
  write(0x08, (1 << 3));

  // SF_CFG (0x0C): Enable Fault Interrupt Output on GPIO3
  write(0x0C, (1 << 3));

  // TODO scheduled calibration? (reg 0x0E)

  // TODO Enable error interrupt

  return true;
}

float ADA4254::set_gain(ada4254_gainin_t in, ada4254_gainout_t out) {
  // Save previous values
  ada4254_gainin_t tmp_in   = _config_gainin;
  ada4254_gainout_t tmp_out = _config_gainout;
  float tmp_gain            = _gain;

  switch(in) {
    case ADA4254_IX0P0625:
      _gain = 0.0625F; break;
    case ADA4254_IX0P125:
      _gain = 0.125F; break;
    case ADA4254_IX0P25:
      _gain = 0.25F; break;
    case ADA4254_IX0P5:
      _gain = 0.5F; break;
    case ADA4254_IX1:
      _gain = 1.0F; break;
    case ADA4254_IX2:
      _gain = 2.0F; break;
    case ADA4254_IX4:
      _gain = 4.0F; break;
    case ADA4254_IX8:
      _gain = 8.0F; break;
    case ADA4254_IX16:
      _gain = 16.0F; break;
    case ADA4254_IX32:
      _gain = 32.0F; break;
    case ADA4254_IX64:
      _gain = 64.0F; break;
    case ADA4254_IX128:
      _gain = 128.0F; break;
    default:
      return -1;
  }

  switch (out) {
    case ADA4254_OX1:
      _gain = _gain; break;
    case ADA4254_OX1P25:
      _gain = 1.25F * _gain; break;
    case ADA4254_OX1P375:
      _gain = 1.375F * _gain; break;
    default:
      return -1;
  }

  // Set new values and update
  _config_gainin  = in;
  _config_gainout = out;

  if (!update_config(ADA4254_GAIN)){
    // Reset to previous values and return
    _config_gainin  = tmp_in;
    _config_gainout = tmp_out;
    _gain = tmp_gain;

    return -1;
  }

  return _gain;
}

bool ADA4254::set_switch(ada4254_switch_t pos, ada4254_switch_t neg) {
  // Save previous values
  ada4254_switch_t tmp_pos = _config_swp;
  ada4254_switch_t tmp_neg = _config_swn;

  switch (pos) {
    case ADA4254_IN1:
    case ADA4254_IN2:
    case ADA4254_INT:
      break;
    case ADA4254_SHORT:
      if (neg != ADA4254_SHORT) return false;
      break;
    default:
      return false;
  }

  switch (neg) {
    case ADA4254_IN1:
    case ADA4254_IN2:
    case ADA4254_INT:
    case ADA4254_SHORT:
      break;
    default:
      return false;
  }

  _config_swp = pos;
  _config_swn = neg;

  if (!update_config(ADA4254_SW)){
    // Reset to previous values and return
    _config_swp  = tmp_pos;
    _config_swn = tmp_neg;

    return false;
  }

  return true;
}

bool ADA4254::set_tmux(ada4254_tmux_t pos, ada4254_tmux_t neg) {
  // Save previous values
  ada4254_tmux_t tmp_pos = _config_tmuxp;
  ada4254_tmux_t tmp_neg = _config_tmuxn;

  switch (pos) {
    case ADA4254_AVSS:
    case ADA4254_DVSS:
    case ADA4254_P20M:
    case ADA4254_N20M:
      break;
    default:
      return false;
  }

  switch (neg) {
    case ADA4254_AVSS:
    case ADA4254_DVSS:
    case ADA4254_P20M:
    case ADA4254_N20M:
      break;
    default:
      return false;
  }

  _config_tmuxp = pos;
  _config_tmuxn = neg;

  if (!update_config(ADA4254_TMUX)){
    // Reset to previous values and return
    _config_tmuxp = tmp_pos;
    _config_tmuxn = tmp_neg;

    return false;
  }

  return true;
}

// Private methods
uint8_t ADA4254::transaction(uint8_t rw, uint8_t cmd, uint8_t data) {
  uint8_t ret;

  // Start SPI transaction (0.5MHz)
  _SPI->beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));

  // Select the AMP
  digitalWrite(_pin_cs, LOW);

  // Send ADC SPI command
  _SPI->transfer(cmd | (rw & 0x1) << 7);

  // Write/read
  ret = _SPI->transfer(data);

  // Deselect the AMP while ending SPI control
  digitalWrite(_pin_cs, HIGH);

  // End SPI transaction
  _SPI->endTransaction();

  // Print the result (for debugging purposes)
  //debugD("INAMP transaction: rw = %d, cmd = 0x%X, data = 0x%X, read = 0x%X", rw, cmd, data, ret);
#ifdef INAMP_DEBUG
  char str_buffer[128];
  snprintf(str_buffer, sizeof(str_buffer), "INAMP transaction: rw = %d, cmd = 0x%X, data = 0x%X, read = 0x%X", rw, cmd, data, ret);
  std::string log_msg(str_buffer);
  log_add(log_msg);
#endif

  return ret;
}

void ADA4254::write(uint8_t addr, uint8_t data) {
  transaction(0, addr, data);
}

uint8_t ADA4254::read(uint8_t addr) {
  return transaction(1, addr, 0x00);
}

bool ADA4254::update_config(ada4254_update_t config){
  uint8_t update_reg0x00 = 0; // GAIN_MUX
  uint8_t update_reg0x06 = 0; // INPUT_MUX
  uint8_t update_reg0x0E = 0; // TEST_MUX

  switch(config) {
    case ADA4254_GAIN:
      update_reg0x00 = 1;
      update_reg0x0E = 1;
      break;
    case ADA4254_SW:
      update_reg0x06 = 1;
      break;
    case ADA4254_TMUX:
      update_reg0x0E = 1;
      break;
    default:
      return false;
  }

  // GAIN_MUX
  if (update_reg0x00) {
    uint8_t addr = 0x00;
    uint8_t data = 0x00;

    data |= ((((uint8_t) _config_gainin) & 0xF) << 3);
    data |= ((((uint8_t) _config_gainout) & 0x1) << 7);

    write(addr,data);
    //if (read(addr) != data) return false;
  }

  // INPUT_MUX
  if (update_reg0x06) {
    uint8_t addr = 0x06;
    uint8_t data = 0x00;

    if(_config_swp == ADA4254_SHORT) {
      data = 0x1;
    } else {
      data |= (1 << ((uint8_t) _config_swp + 1));
      data |= (1 << ((uint8_t) _config_swn + 0));
    }

    write(addr,data);
    //if (read(addr) != data) return false;
  }

  // TEST_MUX
  if (update_reg0x0E) {
    uint8_t addr = 0x0E;
    uint8_t data = 0x00;

    data |= ((((uint8_t) _config_tmuxp) & 0x3)   << 0);
    data |= ((((uint8_t) _config_tmuxn) & 0x3)   << 2);
    data |= ((((uint8_t) _config_gainout) & 0x2) << 6);

    write(addr,data);
    //if (read(addr) != data) return false;
  }

  return true;
}

