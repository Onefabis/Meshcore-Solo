#include "ST7789LCDDisplay.h"

#ifdef OLED_MISC_FIXED_FONT
  #include "MiscFixedFont.h"
  #include "LemonIcons.h"
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X 2.5f // 320 / 128
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y 3.75f // 240 / 64
#endif

#ifndef DISPLAY_WIDTH
  #define DISPLAY_WIDTH 240
#endif
#ifndef DISPLAY_HEIGHT
  #define DISPLAY_HEIGHT 320
#endif

#ifndef DISPLAY_INVERT_COLORS
  #define DISPLAY_INVERT_COLORS 1
#endif

#ifndef DISPLAY_SPI_SPEED
  #define DISPLAY_SPI_SPEED 40000000UL
#endif

#ifndef PIN_TFT_LEDA_CTL_ACTIVE
  #define PIN_TFT_LEDA_CTL_ACTIVE HIGH
#endif

#ifndef DISPLAY_BRIGHTNESS_MIN
  #define DISPLAY_BRIGHTNESS_MIN 1
#endif

#ifdef OLED_MISC_FIXED_FONT
static bool repackGlyphToXbm(const uint8_t* bitmapProgmem, uint16_t byteOffset,
                             uint8_t w, uint8_t h, uint8_t outW, uint8_t outH,
                             uint8_t* out, uint8_t outCap) {
  uint8_t widthInBytes = (outW + 7) / 8;
  uint16_t needed = (uint16_t)widthInBytes * outH;
  if (needed == 0 || needed > outCap) return false;
  memset(out, 0, needed);
  for (uint8_t row = 0; row < outH; row++) {
    uint8_t srcRow = (uint16_t)row * h / outH;
    for (uint8_t col = 0; col < outW; col++) {
      uint8_t srcCol = (uint16_t)col * w / outW;
      uint16_t srcBit = (uint16_t)srcRow * w + srcCol;
      uint16_t byteIdx = byteOffset + (srcBit >> 3);
      uint8_t bitMask = 0x80 >> (srcBit & 7);
      if (pgm_read_byte(bitmapProgmem + byteIdx) & bitMask)
        out[row * widthInBytes + (col >> 3)] |= 0x80 >> (col & 7);
    }
  }
  return true;
}

uint8_t ST7789LCDDisplay::glyphXAdvance(uint32_t cp) {
  for (uint8_t i = 0; i < lemonIconCount; i++)
    if (pgm_read_dword(&lemonIconCPs[i]) == cp)
      return pgm_read_byte(&lemonIconGlyphs[i].xAdvance);
  if (cp < MiscFixed.first || cp > MiscFixed.last) return 6;
  return pgm_read_byte(&MiscFixedGlyphs[cp - MiscFixed.first].xAdvance);
}
#endif

// Convert logical UI coordinates to physical panel coordinates.  Rectangle
// sizes must be calculated from the two scaled edges, rather than by scaling
// the width independently: with fractional scales (280/128 and 240/64),
// truncating every 1px span separately leaves occasional physical gaps.
static int scaleX(int x) {
  return (int)(x * DISPLAY_SCALE_X);
}

static int scaleY(int y) {
  return (int)(y * DISPLAY_SCALE_Y);
}

static int scaleWidth(int x, int w) {
  if (w <= 0) return 0;
  int scaled = scaleX(x + w) - scaleX(x);
  return scaled > 0 ? scaled : 1;
}

static int scaleHeight(int y, int h) {
  if (h <= 0) return 0;
  int scaled = scaleY(y + h) - scaleY(y);
  return scaled > 0 ? scaled : 1;
}

bool ST7789LCDDisplay::i2c_probe(TwoWire& wire, uint8_t addr) {
  return true;
}

bool ST7789LCDDisplay::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();

#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL != -1
      pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
      digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
#endif

    // Im not sure if this is just a t-deck problem or not, if your display is slow try this.
    #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT)
      displaySPI.begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);
    #endif

#if defined(PROMICRO)
    #if defined(PIN_TFT_MISO) && PIN_TFT_MISO >= 0
      const uint8_t tft_miso = PIN_TFT_MISO;
    #else
      const uint8_t tft_miso = PIN_SPI1_MISO;
    #endif
    SPI1.setPins(tft_miso, PIN_TFT_SCL, PIN_TFT_SDA);
    SPI1.begin();
#endif
    display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.setRotation(DISPLAY_ROTATION);
    display.invertDisplay(DISPLAY_INVERT_COLORS != 0);
    display.setSPISpeed(DISPLAY_SPI_SPEED);
    // The rounded panel corners are outside the UI canvas. Clear the complete
    // GRAM once after initialization so those physical pixels cannot show
    // stale or undefined contents between frame updates.
    display.fillScreen(ST77XX_BLACK);
#if defined(PROMICRO)
  canvas.fillScreen(0);
#endif
    display.setTextColor(ST77XX_WHITE);
#if defined(PROMICRO)
    canvas.setTextColor(1);
#endif
    display.setTextSize(2 * DISPLAY_SCALE_X);
    display.cp437(true); // Use full 256 char 'Code Page 437' font

    _isOn = true;
    setBrightness(_brightness_level);

  }

  return true;
}

void ST7789LCDDisplay::turnOn() {
  ST7789LCDDisplay::begin();
}

void ST7789LCDDisplay::turnOff() {
  if (_isOn) {
#if defined(PIN_TFT_RST) && PIN_TFT_RST != -1
      digitalWrite(PIN_TFT_RST, LOW);
#endif
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL != -1
#if PIN_TFT_LEDA_CTL_ACTIVE == HIGH
      analogWrite(PIN_TFT_LEDA_CTL, 0);
#else
      analogWrite(PIN_TFT_LEDA_CTL, 255);
#endif
#endif
    _isOn = false;

    if (_peripher_power) _peripher_power->release();
  }
}

void ST7789LCDDisplay::clear() {
#if defined(PROMICRO)
  canvas.fillScreen(0);
#else
  display.fillScreen(ST77XX_BLACK);
#endif
}

void ST7789LCDDisplay::startFrame(Color bkg) {
#if defined(PROMICRO)
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(1 * DISPLAY_SCALE_X);
  canvas.cp437(true);
#else
  display.fillScreen(ST77XX_BLACK);
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(1 * DISPLAY_SCALE_X); // This one affects size of Please wait... message
  display.cp437(true); // Use full 256 char 'Code Page 437' font
#endif
}

void ST7789LCDDisplay::setTextSize(int sz) {
#if defined(PROMICRO)
  canvas.setTextSize(sz * DISPLAY_SCALE_X);
#else
  display.setTextSize(sz * DISPLAY_SCALE_X);
#endif
}

void ST7789LCDDisplay::setColor(Color c) {
  switch (c) {
    case DisplayDriver::DARK :
      _color = ST77XX_BLACK;
      break;
    case DisplayDriver::LIGHT :
      _color = ST77XX_WHITE;
      break;
    case DisplayDriver::RED :
      _color = ST77XX_RED;
      break;
    case DisplayDriver::GREEN :
      _color = ST77XX_GREEN;
      break;
    case DisplayDriver::BLUE :
      _color = ST77XX_BLUE;
      break;
    case DisplayDriver::YELLOW :
      _color = ST77XX_YELLOW;
      break;
    case DisplayDriver::ORANGE :
      _color = ST77XX_ORANGE;
      break;
    default:
      _color = ST77XX_WHITE;
      break;
  }
#if defined(PROMICRO)
  canvas.setTextColor(_color == ST77XX_BLACK ? 0 : 1);
#else
  display.setTextColor(_color);
#endif
}

void ST7789LCDDisplay::setCursor(int x, int y) {
#if defined(PROMICRO)
  canvas.setCursor(scaleX(x), scaleY(y));
#else
  display.setCursor(scaleX(x), scaleY(y));
#endif
#ifdef OLED_MISC_FIXED_FONT
  _lx = x;
  _ly = y;
#endif
}

void ST7789LCDDisplay::print(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  int lx = _lx;
  int ly = _ly;
  uint8_t glyphBuf[64];
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp = DisplayDriver::decodeCodepoint(p);
    if (cp == '\n') { ly += 9; lx = _lx; continue; }

    bool drawn = false;
    for (uint8_t i = 0; i < lemonIconCount; i++) {
      if (pgm_read_dword(&lemonIconCPs[i]) != cp) continue;
      const GFXglyph* g = &lemonIconGlyphs[i];
      uint8_t gw = pgm_read_byte(&g->width), gh = pgm_read_byte(&g->height);
      int8_t xo = (int8_t)pgm_read_byte(&g->xOffset);
      int8_t yo = (int8_t)pgm_read_byte(&g->yOffset);
      uint16_t bo = pgm_read_word(&g->bitmapOffset);
      uint8_t drawW = gw;
      uint8_t drawH = gh;
      if (repackGlyphToXbm(lemonIconBitmaps, bo, gw, gh, drawW, drawH,
                           glyphBuf, sizeof(glyphBuf)))
        drawXbm(lx + xo, ly + 6 + yo, glyphBuf, drawW, drawH);
      lx += pgm_read_byte(&g->xAdvance);
      drawn = true;
      break;
    }
    if (drawn) continue;

    if (cp < MiscFixed.first || cp > MiscFixed.last) {
      if (cp >= 0x20) fillRect(lx + 1, ly, 4, 6);
      lx += 6;
      continue;
    }

    const GFXglyph* g = &MiscFixedGlyphs[cp - MiscFixed.first];
    uint8_t gw = pgm_read_byte(&g->width), gh = pgm_read_byte(&g->height);
    int8_t xo = (int8_t)pgm_read_byte(&g->xOffset);
    int8_t yo = (int8_t)pgm_read_byte(&g->yOffset);
    uint16_t bo = pgm_read_word(&g->bitmapOffset);
    uint8_t drawW = gw;
    uint8_t drawH = gh;
    if (repackGlyphToXbm(MiscFixedBitmaps, bo, gw, gh, drawW, drawH,
                         glyphBuf, sizeof(glyphBuf)))
      drawXbm(lx + xo, ly + 7 + yo, glyphBuf, drawW, drawH);
    lx += pgm_read_byte(&g->xAdvance);
  }
#else
#if defined(PROMICRO)
  canvas.print(str);
#else
  display.print(str);
#endif
#endif
}

void ST7789LCDDisplay::fillRect(int x, int y, int w, int h) {
#if defined(PROMICRO)
  canvas.fillRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h),
                  _color == ST77XX_BLACK ? 0 : 1);
#else
  display.fillRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h), _color);
#endif
}

void ST7789LCDDisplay::drawRect(int x, int y, int w, int h) {
#if defined(PROMICRO)
  canvas.drawRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h),
                  _color == ST77XX_BLACK ? 0 : 1);
#else
  display.drawRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h), _color);
#endif
}

void ST7789LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  uint8_t byteWidth = (w + 7) / 8;

#if defined(PROMICRO)
  for (int j = 0; j < h; j++) {
    int runStart = -1;
    for (int i = 0; i <= w; i++) {
      bool pixelOn = false;
      if (i < w) {
        uint8_t byte = bits[j * byteWidth + i / 8];
        pixelOn = (byte & (0x80 >> (i & 7))) != 0;
      }

      if (pixelOn && runStart < 0) {
        runStart = i;
      } else if (!pixelOn && runStart >= 0) {
        int x0 = scaleX(x + runStart);
        int x1 = scaleX(x + i);
        int y0 = scaleY(y + j);
        int y1 = scaleY(y + j + 1);
        canvas.fillRect(x0, y0, x1 - x0, y1 - y0,
                        _color == ST77XX_BLACK ? 0 : 1);
        runStart = -1;
      }
    }
  }
#else
  display.startWrite();
  for (int j = 0; j < h; j++) {
    int runStart = -1;
    for (int i = 0; i <= w; i++) {
      bool pixelOn = false;
      if (i < w) {
        uint8_t byte = bits[j * byteWidth + i / 8];
        pixelOn = (byte & (0x80 >> (i & 7))) != 0;
      }

      if (pixelOn && runStart < 0) {
        runStart = i;
      } else if (!pixelOn && runStart >= 0) {
        int x0 = scaleX(x + runStart);
        int x1 = scaleX(x + i);
        int y0 = scaleY(y + j);
        int y1 = scaleY(y + j + 1);
        display.writeFillRect(x0, y0, x1 - x0, y1 - y0, _color);
        runStart = -1;
      }
    }
  }
  display.endWrite();
#endif
}

uint16_t ST7789LCDDisplay::getTextWidth(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  uint16_t width = 0;
  const uint8_t* p = (const uint8_t*)str;
  while (*p) width += glyphXAdvance(DisplayDriver::decodeCodepoint(p));
  return width;
#else
  int16_t x1, y1;
  uint16_t w, h;
#if defined(PROMICRO)
  canvas.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
#else
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
#endif

  return w / DISPLAY_SCALE_X;
#endif
}

void ST7789LCDDisplay::setBrightness(uint8_t level) {
  if (level > 4) level = 4;
  _brightness_level = level;
#if defined(PIN_TFT_LEDA_CTL) && PIN_TFT_LEDA_CTL != -1
  if (!_isOn) return;
  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  uint8_t min_duty = DISPLAY_BRIGHTNESS_MIN > 255 ? 255 : DISPLAY_BRIGHTNESS_MIN;
  uint8_t duty = min_duty + (uint16_t)(255 - min_duty) * level / 4;
#if PIN_TFT_LEDA_CTL_ACTIVE == HIGH
  analogWrite(PIN_TFT_LEDA_CTL, duty);
#else
  analogWrite(PIN_TFT_LEDA_CTL, 255 - duty);
#endif
#else
  (void)level;
#endif
}

void ST7789LCDDisplay::endFrame() {
#if defined(PROMICRO)
  uint16_t line[ST7789_CANVAS_WIDTH];
  display.startWrite();
  display.setAddrWindow(DISPLAY_MARGIN_X, DISPLAY_MARGIN_Y,
                        ST7789_CANVAS_WIDTH, ST7789_CANVAS_HEIGHT);
  for (int y = 0; y < ST7789_CANVAS_HEIGHT; y++) {
    for (int x = 0; x < ST7789_CANVAS_WIDTH; x++) {
      line[x] = canvas.getPixel(x, y) ? ST77XX_WHITE : ST77XX_BLACK;
    }
    display.writePixels(line, ST7789_CANVAS_WIDTH);
  }
  display.endWrite();
#endif
}
