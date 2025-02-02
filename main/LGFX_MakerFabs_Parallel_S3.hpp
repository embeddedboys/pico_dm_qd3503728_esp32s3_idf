// based on https://github.com/lovyan03/LovyanGFX/blob/experimental/src/lgfx_user/LGFX_ESP32S2_MakerabsParallelTFTwithTouch.hpp
// LGFX for Makerfabs ESP32-S2-Parallel-TFT-with-Touch
// https://github.com/Makerfabs/Makerfabs-ESP32-S2-Parallel-TFT-with-Touch/

// modified for the S3 version's pins as with https://github.com/Makerfabs/Makerfabs-ESP32-S3-Parallel-TFT-with-Touch/blob/main/firmware/SD16_3.5/SD16_3.5.ino
#pragma once

#define LGFX_USE_V1

#include <LovyanGFX.hpp>
#include <driver/i2c.h>

#define NOLOGO_ESP32S3_PICO   1
#define WALNUTPI_PICOW        2
#define UNKNOWN_ESP32S3_PICO  3

#ifndef DEFAULT_CORE_BOARD_MODEL
  #define DEFAULT_CORE_BOARD_MODEL NOLOGO_ESP32S3_PICO
#endif

#if DEFAULT_CORE_BOARD_MODEL == NOLOGO_ESP32S3_PICO
  #define TFT_PIN_BLK   9
  #define TFT_PIN_WR    2
  #define TFT_PIN_RD    41
  #define TFT_PIN_RS    4
  #define TFT_PIN_RST   6

  #define TFT_PIN_D0    11
  #define TFT_PIN_D1    12
  #define TFT_PIN_D2    13
  #define TFT_PIN_D3    14
  #define TFT_PIN_D4    15
  #define TFT_PIN_D5    16
  #define TFT_PIN_D6    17
  #define TFT_PIN_D7    18
  #define TFT_PIN_D8    33
  #define TFT_PIN_D9    34
  #define TFT_PIN_D10   35
  #define TFT_PIN_D11   36
  #define TFT_PIN_D12   37
  #define TFT_PIN_D13   38
  #define TFT_PIN_D14   39
  #define TFT_PIN_D15   40

  #define TP_PIN_SDA    7
  #define TP_PIN_SCL    8
  #define TP_PIN_INT    5
#elif DEFAULT_CORE_BOARD_MODEL == WALNUTPI_PICOW
  #define TFT_PIN_BLK   11
  #define TFT_PIN_WR    5
  #define TFT_PIN_RD    2
  #define TFT_PIN_RS    6
  #define TFT_PIN_RST   8

  #define TFT_PIN_D0    13
  #define TFT_PIN_D1    14
  #define TFT_PIN_D2    15
  #define TFT_PIN_D3    16
  #define TFT_PIN_D4    17
  #define TFT_PIN_D5    18
  #define TFT_PIN_D6    21
  #define TFT_PIN_D7    34
  #define TFT_PIN_D8    35
  #define TFT_PIN_D9    36
  #define TFT_PIN_D10   37
  #define TFT_PIN_D11   38
  #define TFT_PIN_D12   39
  #define TFT_PIN_D13   40
  #define TFT_PIN_D14   41
  #define TFT_PIN_D15   42

  #define TP_PIN_SDA    9
  #define TP_PIN_SCL    10
  #define TP_PIN_INT    7
  #error "Walnutpi PicoW not supported yet!"
#elif DEFAULT_CORE_BOARD_MODEL == UNKNOWN_ESP32S3_PICO
  #define TFT_PIN_BLK   9
  #define TFT_PIN_WR    3
  #define TFT_PIN_RD    41
  #define TFT_PIN_RS    4
  #define TFT_PIN_RST   6

  #define TFT_PIN_D0    43
  #define TFT_PIN_D1    44
  #define TFT_PIN_D2    38
  #define TFT_PIN_D3    39
  #define TFT_PIN_D4    40
  #define TFT_PIN_D5    41
  #define TFT_PIN_D6    42
  #define TFT_PIN_D7    21
  #define TFT_PIN_D8    20
  #define TFT_PIN_D9    19
  #define TFT_PIN_D10   18
  #define TFT_PIN_D11   17
  #define TFT_PIN_D12   14
  #define TFT_PIN_D13   13
  #define TFT_PIN_D14   12
  #define TFT_PIN_D15   11

  #define TP_PIN_SDA    7
  #define TP_PIN_SCL    8
  #define TP_PIN_INT    5
#else
  #error "invaild core board model"
#endif


class LGFX : public lgfx::LGFX_Device
{
  static constexpr int I2C_PORT_NUM = I2C_NUM_0;
  static constexpr int I2C_PIN_SDA = TP_PIN_SDA;
  static constexpr int I2C_PIN_SCL = TP_PIN_SCL;
  static constexpr int I2C_PIN_INT = TP_PIN_INT;

  lgfx::Bus_Parallel16 _bus_instance;
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::ITouch*  _touch_instance_ptr = nullptr;

  /// Detects and configures the touch panel during initialization;
  bool init_impl(bool use_reset, bool use_clear) override
  {
    if (_touch_instance_ptr == nullptr)
    {
      lgfx::ITouch::config_t cfg;
      lgfx::i2c::init(I2C_PORT_NUM, I2C_PIN_SDA, I2C_PIN_SCL);
      if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, 0x38, 400000, false).has_value()
       && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value())
      {
        _touch_instance_ptr = new lgfx::Touch_FT5x06();
        cfg = _touch_instance_ptr->config();
        cfg.i2c_addr = 0x38;
        cfg.x_max = 320;
        cfg.y_max = 480;
      }
      else
      if (lgfx::i2c::beginTransaction(I2C_PORT_NUM, 0x48, 400000, false).has_value()
       && lgfx::i2c::endTransaction(I2C_PORT_NUM).has_value())
      {
        _touch_instance_ptr = new lgfx::Touch_NS2009();
        cfg = _touch_instance_ptr->config();
        cfg.i2c_addr = 0x48;
        cfg.x_min = 368;
        cfg.y_min = 212;
        cfg.x_max = 3800;
        cfg.y_max = 3800;
      }
      if (_touch_instance_ptr != nullptr)
      {
        cfg.i2c_port = I2C_PORT_NUM;
        cfg.pin_sda  = I2C_PIN_SDA;
        cfg.pin_scl  = I2C_PIN_SCL;
        cfg.pin_int  = I2C_PIN_INT;
        cfg.freq = 400000;
        cfg.bus_shared = false;
        _touch_instance_ptr->config(cfg);
        _panel_instance.touch(_touch_instance_ptr);
      }
    }
    return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
  }

public:

  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();

      cfg.freq_write = 50000000;
      cfg.pin_wr = TFT_PIN_WR;
      cfg.pin_rd = TFT_PIN_RD;
      cfg.pin_rs = TFT_PIN_RS;

      cfg.pin_d0 = TFT_PIN_D0;
      cfg.pin_d1 = TFT_PIN_D1;
      cfg.pin_d2 = TFT_PIN_D2;
      cfg.pin_d3 = TFT_PIN_D3;
      cfg.pin_d4 = TFT_PIN_D4;
      cfg.pin_d5 = TFT_PIN_D5;
      cfg.pin_d6 = TFT_PIN_D6;
      cfg.pin_d7 = TFT_PIN_D7;
      cfg.pin_d8 = TFT_PIN_D8;
      cfg.pin_d9 = TFT_PIN_D9;
      cfg.pin_d10 = TFT_PIN_D10;
      cfg.pin_d11 = TFT_PIN_D11;
      cfg.pin_d12 = TFT_PIN_D12;
      cfg.pin_d13 = TFT_PIN_D13;
      cfg.pin_d14 = TFT_PIN_D14;
      cfg.pin_d15 = TFT_PIN_D15;
      _bus_instance.config(cfg);
      _panel_instance.bus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs          =    -1;
      cfg.pin_rst         =    TFT_PIN_RST;
      cfg.pin_busy        =    -1;
      cfg.offset_rotation =     0;
      cfg.readable        =  true;
      cfg.invert          = false;
      cfg.rgb_order       = false;
      cfg.dlen_16bit      =  true;
      cfg.bus_shared      = false;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();

      cfg.pin_bl = TFT_PIN_BLK;
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;

      _light_instance.config(cfg);
      _panel_instance.light(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};
