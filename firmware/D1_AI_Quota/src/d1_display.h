#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <D1_GFX_Driver.h>
#include <PCA95x5.h>

#define TOUCH_MODULES_FT5x06
#define TOUCH_MODULE_ADDR (0x48)
#include <TouchLib.h>
#include <math.h>

// SenseCAP Indicator D1 display driver reused from the local working demo
// Display layer now follows the proven SenseCAP Indicator driver approach:
// PCA9555-controlled LCD CS/reset + 9-bit SPI + SenseCAP-specific ST7701 init.
// Recommended environment from Seeed wiki:
//   ESP32 board package 3.1.2
//   GFX Library for Arduino 1.5.3
//   PCA95x5 0.1.3
//   TouchLib 0.0.2
// Board: ESP32S3 Dev Module, PSRAM: OPI PSRAM

static constexpr int SCREEN_W = 480;
static constexpr int SCREEN_H = 480;
static constexpr int SPI_MOSI_PIN = 48;
static constexpr int SPI_SCLK_PIN = 41;
static constexpr int I2C_SDA_PIN = 39;
static constexpr int I2C_SCL_PIN = 40;
static constexpr int BACKLIGHT_PIN = 45;

static constexpr PCA95x5::Port::Port LCD_CS_PORT = PCA95x5::Port::P04;
static constexpr PCA95x5::Port::Port LCD_RESET_PORT = PCA95x5::Port::P05;
static constexpr PCA95x5::Port::Port LCD_INT_PORT = PCA95x5::Port::P06;
static constexpr PCA95x5::Port::Port TOUCH_RESET_PORT = PCA95x5::Port::P07;

PCA9555 ioex;
static bool hwInitDone = false;

static void indicator_hw_init() {
  if (hwInitDone) return;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  ioex.attach(Wire);
  ioex.polarity(PCA95x5::Polarity::ORIGINAL_ALL);

  ioex.write(LCD_CS_PORT, PCA95x5::Level::H);
  ioex.direction(LCD_CS_PORT, PCA95x5::Direction::OUT);

  ioex.write(LCD_RESET_PORT, PCA95x5::Level::L);
  ioex.direction(LCD_RESET_PORT, PCA95x5::Direction::OUT);

  ioex.direction(LCD_INT_PORT, PCA95x5::Direction::IN);

  ioex.write(TOUCH_RESET_PORT, PCA95x5::Level::L);
  ioex.direction(TOUCH_RESET_PORT, PCA95x5::Direction::OUT);

  delay(5);
  ioex.write(LCD_RESET_PORT, PCA95x5::Level::H);
  ioex.write(TOUCH_RESET_PORT, PCA95x5::Level::H);
  delay(5);

  hwInitDone = true;
}

// Same bus strategy used by the working SenseCAP Indicator Arduino/LVGL project.
class Indicator_SWSPI : public Arduino_DataBus {
public:
  Indicator_SWSPI(int8_t dc, int8_t cs, int8_t sck, int8_t mosi, int8_t miso = GFX_NOT_DEFINED)
      : _dc(dc), _cs(cs), _sck(sck), _mosi(mosi), _miso(miso) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)speed;
    (void)dataMode;
    indicator_hw_init();

    if (_cs != GFX_NOT_DEFINED) {
      ioex.write(static_cast<PCA95x5::Port::Port>(_cs), PCA95x5::Level::L);
      ioex.direction(static_cast<PCA95x5::Port::Port>(_cs), PCA95x5::Direction::OUT);
      ioex.write(static_cast<PCA95x5::Port::Port>(_cs), PCA95x5::Level::H);
    }

    if (_dc != GFX_NOT_DEFINED) {
      pinMode(_dc, OUTPUT);
      digitalWrite(_dc, HIGH);
    }

    pinMode(_sck, OUTPUT);
    digitalWrite(_sck, LOW);
    pinMode(_mosi, OUTPUT);
    digitalWrite(_mosi, LOW);
    if (_miso != GFX_NOT_DEFINED) pinMode(_miso, INPUT);
    return true;
  }

  void beginWrite() override {
    if (_dc != GFX_NOT_DEFINED) digitalWrite(_dc, HIGH);
    csLow();
  }

  void endWrite() override { csHigh(); }

  void writeCommand(uint8_t c) override {
    if (_dc == GFX_NOT_DEFINED) {
      write9Command(c);
    } else {
      digitalWrite(_dc, LOW);
      write8(c);
      digitalWrite(_dc, HIGH);
    }
  }

  void writeCommand16(uint16_t c) override {
    if (_dc == GFX_NOT_DEFINED) {
      write9Command((uint8_t)(c >> 8));
      write9Command((uint8_t)c);
    } else {
      digitalWrite(_dc, LOW);
      write16raw(c);
      digitalWrite(_dc, HIGH);
    }
  }

  void writeCommandBytes(uint8_t *data, uint32_t len) override {
    if (_dc == GFX_NOT_DEFINED) {
      while (len--) write9Command(*data++);
    } else {
      digitalWrite(_dc, LOW);
      while (len--) write8(*data++);
      digitalWrite(_dc, HIGH);
    }
  }

  void write(uint8_t d) override {
    if (_dc == GFX_NOT_DEFINED) write9Data(d);
    else write8(d);
  }

  void write16(uint16_t d) override {
    if (_dc == GFX_NOT_DEFINED) {
      write9Data((uint8_t)(d >> 8));
      write9Data((uint8_t)d);
    } else {
      write16raw(d);
    }
  }

  void writeRepeat(uint16_t p, uint32_t len) override {
    if (_dc == GFX_NOT_DEFINED) {
      while (len--) {
        write9Data((uint8_t)(p >> 8));
        write9Data((uint8_t)p);
      }
    } else {
      while (len--) write16raw(p);
    }
  }

  void writePixels(uint16_t *data, uint32_t len) override {
    while (len--) write16raw(*data++);
  }

#if !defined(LITTLE_FOOT_PRINT)
  void writeBytes(uint8_t *data, uint32_t len) override {
    while (len--) write8(*data++);
  }
#endif

private:
  int8_t _dc, _cs, _sck, _mosi, _miso;

  inline void csHigh() {
    if (_cs != GFX_NOT_DEFINED)
      ioex.write(static_cast<PCA95x5::Port::Port>(_cs), PCA95x5::Level::H);
  }

  inline void csLow() {
    if (_cs != GFX_NOT_DEFINED)
      ioex.write(static_cast<PCA95x5::Port::Port>(_cs), PCA95x5::Level::L);
  }

  inline void clk() {
    digitalWrite(_sck, HIGH);
    digitalWrite(_sck, LOW);
  }

  inline void bitOut(bool high) {
    digitalWrite(_mosi, high ? HIGH : LOW);
    clk();
  }

  inline void write9Command(uint8_t c) {
    bitOut(false);
    for (uint8_t mask = 0x80; mask; mask >>= 1) bitOut(c & mask);
  }

  inline void write9Data(uint8_t d) {
    bitOut(true);
    for (uint8_t mask = 0x80; mask; mask >>= 1) bitOut(d & mask);
  }

  inline void write8(uint8_t d) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) bitOut(d & mask);
  }

  inline void write16raw(uint16_t d) {
    for (uint16_t mask = 0x8000; mask; mask >>= 1) bitOut(d & mask);
  }
};

// SenseCAP Indicator-specific ST7701 initialization sequence from the
// community driver referenced by Seeed's Arduino tutorial.
static const uint8_t st7701_indicator_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x08,

    WRITE_COMMAND_8, 0xB0,
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1,
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60,
    WRITE_C8_D8, 0xB1, 0x32,
    WRITE_C8_D8, 0xB2, 0x07,
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_C8_D8, 0xC7, 0x04,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    WRITE_C8_D8, 0x36, 0x10,

    WRITE_COMMAND_8, 0x21,
    WRITE_C8_D8, 0x3A, 0x60,

    WRITE_COMMAND_8, 0x11,
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE};

Indicator_SWSPI *bus = new Indicator_SWSPI(
    GFX_NOT_DEFINED, (int8_t)LCD_CS_PORT,
    SPI_SCLK_PIN, SPI_MOSI_PIN, GFX_NOT_DEFINED);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21,
    4, 3, 2, 1, 0,
    10, 9, 8, 7, 6, 5,
    15, 14, 13, 12, 11,
    1, 10, 8, 50,
    1, 10, 8, 20,
    0, GFX_NOT_DEFINED, false, 0, 0, 480*20);

// Internal SRAM bounce buffers protect LCD scanout from short PSRAM traffic spikes.
// The application presents only changed spans and never flushes the whole canvas.
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbpanel, 0, true,
    bus, GFX_NOT_DEFINED,
    st7701_indicator_init_operations,
    sizeof(st7701_indicator_init_operations));

TouchLib touch(Wire, I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_MODULE_ADDR);
