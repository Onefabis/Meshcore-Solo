#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif
#ifndef DISPLAY_WIDTH
  #define DISPLAY_WIDTH 240
#endif
#ifndef DISPLAY_HEIGHT
  #define DISPLAY_HEIGHT 320
#endif

#ifndef DISPLAY_LOGICAL_WIDTH
  #define DISPLAY_LOGICAL_WIDTH 128
#endif
#ifndef DISPLAY_LOGICAL_HEIGHT
  #define DISPLAY_LOGICAL_HEIGHT 64
#endif

#ifndef DISPLAY_MARGIN_X
  #ifdef DISPLAY_MARGIN
    #define DISPLAY_MARGIN_X DISPLAY_MARGIN
  #else
    #define DISPLAY_MARGIN_X 0
  #endif
#endif
#ifndef DISPLAY_MARGIN_Y
  #ifdef DISPLAY_MARGIN
    #define DISPLAY_MARGIN_Y DISPLAY_MARGIN
  #else
    #define DISPLAY_MARGIN_Y 0
  #endif
#endif

#if DISPLAY_ROTATION == 1 || DISPLAY_ROTATION == 3
  #define ST7789_PANEL_WIDTH DISPLAY_HEIGHT
  #define ST7789_PANEL_HEIGHT DISPLAY_WIDTH
#else
  #define ST7789_PANEL_WIDTH DISPLAY_WIDTH
  #define ST7789_PANEL_HEIGHT DISPLAY_HEIGHT
#endif

#define ST7789_CANVAS_WIDTH (ST7789_PANEL_WIDTH - 2 * DISPLAY_MARGIN_X)
#define ST7789_CANVAS_HEIGHT (ST7789_PANEL_HEIGHT - 2 * DISPLAY_MARGIN_Y)

class ST7789LCDDisplay : public DisplayDriver {
  #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
    SPIClass displaySPI;
  #endif
  Adafruit_ST7789 display;
#if defined(PROMICRO)
  GFXcanvas1 canvas;
  struct ColorRegion {
    int16_t left, top, right, bottom;
    uint16_t color;
  };
  ColorRegion _color_regions[16];
  uint8_t _color_region_count = 0;
#endif
  bool _isOn;
  uint8_t _brightness_level = 4;
  uint16_t _color;
  RefCountedDigitalPin* _peripher_power;
#ifdef OLED_MISC_FIXED_FONT
  int _lx = 0, _ly = 0;
#endif

  bool i2c_probe(TwoWire& wire, uint8_t addr);
#ifdef OLED_MISC_FIXED_FONT
  uint8_t glyphXAdvance(uint32_t cp);
#endif
public:
#ifdef USE_PIN_TFT
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_SDA, PIN_TFT_SCL, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#elif defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64),
      displaySPI(HSPI),
      display(&displaySPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#elif defined(PROMICRO)
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(DISPLAY_LOGICAL_WIDTH, DISPLAY_LOGICAL_HEIGHT),
      display(&SPI1, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      canvas(ST7789_CANVAS_WIDTH, ST7789_CANVAS_HEIGHT),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#else
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#endif
  bool begin();

#ifdef OLED_MISC_FIXED_FONT
  int getCharWidth() const override { return 6; }
  int getLineHeight() const override { return 9; }
  int textWidthTrailingGap() const override { return 0; }
  bool isSingleFont() const override { return true; }
  void setSingleFont(bool) override { }
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
  uint16_t getCodepointWidth(uint32_t cp) override { return glyphXAdvance(cp); }
#endif

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
#if defined(PROMICRO)
  void setRegionColor(int x, int y, int w, int h, uint16_t rgb565) override;
#endif
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void setBrightness(uint8_t level) override;
  void endFrame() override;
};
