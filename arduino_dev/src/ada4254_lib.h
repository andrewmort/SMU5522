#ifndef ADA4254_LIB_H
#define ADA4254_LIB_H

#include <Arduino.h>
#include <SPI.h>

// Input Mux Switch Settings
typedef enum {
  ADA4254_IN1   = 5, // In1
  ADA4254_IN2   = 3, // In2
  ADA4254_INT   = 1, // Testmux
  ADA4254_SHORT = 0  // Short inputs of internal PGIA
} ada4254_switch_t;

// Inamp Input Gain
typedef enum {
  ADA4254_IX0P0625   = 0x0,
  ADA4254_IX0P125    = 0x1,
  ADA4254_IX0P25     = 0x2,
  ADA4254_IX0P5      = 0x3,
  ADA4254_IX1        = 0x4,
  ADA4254_IX2        = 0x5,
  ADA4254_IX4        = 0x6,
  ADA4254_IX8        = 0x7,
  ADA4254_IX16       = 0x8,
  ADA4254_IX32       = 0x9,
  ADA4254_IX64       = 0xA,
  ADA4254_IX128      = 0xB
} ada4254_gainin_t;

// Inamp Output Gain
typedef enum {
  ADA4254_OX1     = 0x0,
  ADA4254_OX1P25  = 0x2,
  ADA4254_OX1P375 = 0x3
} ada4254_gainout_t;

// TMUX Setting
typedef enum {
  ADA4254_AVSS  = 0x0,
  ADA4254_DVSS  = 0x1,
  ADA4254_P20M  = 0x2, // +20mV
  ADA4254_N20M  = 0x3  // -20mV
} ada4254_tmux_t;

typedef enum {
  ADA4254_GAIN,
  ADA4254_SW,
  ADA4254_TMUX
} ada4254_update_t;


class ADA4254 {
public:
    // Public methods
    bool begin(SPIClass *spi, int8_t cs);
    float set_gain(ada4254_gainin_t in, ada4254_gainout_t out);
    bool set_switch(ada4254_switch_t pos, ada4254_switch_t neg);
    bool set_tmux(ada4254_tmux_t pos, ada4254_tmux_t neg);
    float get_gain();

private:
    // Private member variables
    int8_t _pin_cs;
    float _gain;
    SPIClass *_SPI;
    ada4254_gainin_t  _config_gainin;
    ada4254_gainout_t _config_gainout;
    ada4254_switch_t  _config_swp;
    ada4254_switch_t  _config_swn;
    ada4254_tmux_t    _config_tmuxp;
    ada4254_tmux_t    _config_tmuxn;

    // Private helper methods (if any)
    uint8_t transaction(uint8_t rw, uint8_t cmd, uint8_t data);
    void write(uint8_t addr, uint8_t data);
    uint8_t read(uint8_t addr);
    bool update_config(ada4254_update_t config);
};

#endif // ADA4254_LIB_H
