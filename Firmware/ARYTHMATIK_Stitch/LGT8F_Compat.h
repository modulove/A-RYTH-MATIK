/**
 * @file LGT8F_Compat.h
 * @brief One firmware for Arduino Nano (ATmega328P) and LGT8F328P Nano boards.
 *
 * Include this instead of <EEPROM.h>, as the FIRST include of the sketch (before
 * any library header that includes <EEPROM.h>, e.g. libmodulove's arythmatik.h).
 *
 * Built for "Arduino Nano", the chip is detected before main() runs:
 * the LGT8F328P runs ATmega328P code, but its bootloader starts the sketch
 * with reset defaults (clock prescaler not set, emulated EEPROM off).
 * On an LGT8F chipInit() sets the clock to the 16 MHz the build expects and
 * switches the EEPROM on. On an ATmega328P nothing is changed.
 * Built with the lgt8fx core (Board "LGT8F328"), the core sets the clock
 * (32 MHz by default) and only the EEPROM is switched on here.
 *
 * EEPROM is a drop-in replacement for the Arduino EEPROM object
 * (read, write, update, get, put, length). 1020 bytes are usable on the
 * LGT8F (the last 4 hold flags). Its EEPROM lives in flash: every write
 * operation is a page swap that FREEZES the whole chip (CPU, timers,
 * interrupts) for ~46 ms (measured on an LGT8F328P, 2026-11-17).
 * - write()/update() write one byte = one page swap, only if the byte changed.
 * - put() writes the whole object as one block (continuous 32-bit "SWM"
 *   mode, normally one page swap). Save several bytes with put(),
 *   never with a loop of update() calls.
 *
 * Keep the program below 29,696 bytes: the LGT8F bootloader starts at 0x7400.
 */
#pragma once

// This header provides the EEPROM object. Arduino's EEPROM.h (AVR core and lgt8fx
// core both use the include guard EEPROM_h) must not be included as well.
#ifdef EEPROM_h
#error "Include LGT8F_Compat.h before <EEPROM.h> and before headers that include it"
#endif
#define EEPROM_h

#include <Arduino.h>
#include <avr/boot.h>
#include <avr/eeprom.h>

// LGT8F registers (reserved addresses on the ATmega328P, only touched on an LGT8F)
#define LGT_ECCR _SFR_IO8(0x36)   // E2PROM control
#define LGT_E2PD1 _SFR_IO8(0x3A)  // E2PROM data bytes 1..3 for 32-bit writes (byte 0 = EEDR)
#define LGT_E2PD2 _SFR_IO8(0x37)
#define LGT_E2PD3 _SFR_IO8(0x3C)
#define LGT_GUID _SFR_MEM8(0xF3)  // 32-bit unique ID, 0xF3..0xF6
#define LGT_EEPROM_SIZE 1020

#if defined(__LGT8F__)
#define IS_LGT true  // built with the lgt8fx core
#else
// Set in .init3, before .bss is cleared, so it must live in .noinit.
static uint8_t chipIsLGT __attribute__((section(".noinit")));
#define IS_LGT (chipIsLGT != 0)
#endif

static void __attribute__((noinline)) chipInit() {
#if !defined(__LGT8F__)
  // ATmega328/P/PB return their signature (0x1E 0x95 ..) for this read.
  // The LGT8F has no signature row: the same LPM reads flash bytes 0 and 2,
  // which hold the bootloader's jmp instruction.
  chipIsLGT = !(boot_signature_byte_get(0x0000) == 0x1E && boot_signature_byte_get(0x0002) == 0x95);
  if (!chipIsLGT) return;
  // Internal 32 MHz RC divided by 2 = the 16 MHz this build was compiled for.
  CLKPR = 0x80;
  CLKPR = 0x01;
#endif
  // 1 KB emulated EEPROM, the same setting the LGT bootloader and core use.
  LGT_ECCR = 0x80;
  LGT_ECCR = 0x4C;
}

// Runs from .init3 (stack set up, interrupts off), before global constructors and init().
static void __attribute__((naked, used, section(".init3"))) chipInitHook() {
  chipInit();
}

static inline const __FlashStringHelper *chipName() {
  return IS_LGT ? F("LGT8F328P") : F("ATmega328P");
}

// LGT8F unique ID, 0 on the ATmega328P.
static inline uint32_t chipUniqueId() {
  uint32_t id = 0;
  if (IS_LGT) {
    for (uint8_t i = 0; i < 4; i++) id |= (uint32_t)(&LGT_GUID)[i] << (8 * i);
  }
  return id;
}

// Seed for randomSeed(): noise from A6/A7 (not used on the A-RYTH-MATIK),
// the start-up time and, on the LGT8F, the unique ID.
static inline uint32_t chipRandomSeed() {
  return analogRead(A6) ^ ((uint32_t)analogRead(A7) << 10) ^ micros() ^ chipUniqueId();
}

struct ChipEEPROM {
  uint16_t length() {
    return IS_LGT ? LGT_EEPROM_SIZE : E2END + 1;
  }

  __attribute__((noinline)) uint8_t read(int addr) {
    if (!IS_LGT) return eeprom_read_byte((const uint8_t *)addr);
    if ((uint16_t)addr >= LGT_EEPROM_SIZE) return 0xFF;
    EEARL = addr & 0xFF;
    EEARH = addr >> 8;
    EECR |= (1 << EERE);
    __asm__ __volatile__("nop\n\tnop");  // LGT8F needs two cycles before EEDR is valid
    return EEDR;
  }

  void update(int addr, uint8_t value) {
    if (read(addr) == value) return;
    if (!IS_LGT) {
      eeprom_write_byte((uint8_t *)addr, value);
      return;
    }
    if ((uint16_t)addr >= LGT_EEPROM_SIZE) return;  // would overwrite the page flags
    EEARL = addr & 0xFF;
    EEARH = addr >> 8;
    EEDR = value;
    uint8_t sreg = SREG;
    cli();
    EECR = 0x04;  // EEMPE
    EECR = 0x02;  // EEPE
    SREG = sreg;
  }

  void write(int addr, uint8_t value) {
    update(addr, value);
  }

  template<typename T> T &get(int addr, T &value) {
    uint8_t *p = (uint8_t *)&value;
    for (uint16_t i = 0; i < sizeof(T); i++) p[i] = read(addr + i);
    return value;
  }

  template<typename T> const T &put(int addr, const T &value) {
    const uint8_t *p = (const uint8_t *)&value;
    if (IS_LGT) {
      lgtWriteBlock(addr, p, sizeof(T));
    } else {
      for (uint16_t i = 0; i < sizeof(T); i++) update(addr + i, p[i]);
    }
    return value;
  }

  // LGT8F: write [addr, addr + len) as bursts of 32-bit words in continuous write
  // mode (SWM), one page swap per burst. The first and last word keep their bytes
  // outside the range. Same register sequence as lgt_eeprom_writeSWM() (lgt8fx core).
  // Measured 2026-11-17: a burst of 2+ words that STARTS with an all-0xFF word hangs
  // the chip (0xFF words alone, in the middle or at the end are fine), so such a
  // word is written on its own first.
  void lgtWriteBlock(uint16_t addr, const uint8_t *src, uint16_t len) {
    static const uint8_t dataReg[4] PROGMEM = { 0x40, 0x5A, 0x57, 0x5C };  // EEDR, E2PD1..3
    uint16_t end = addr + len;
    if (end > LGT_EEPROM_SIZE) return;
    uint16_t i = 0;
    while (i < len && read(addr + i) == src[i]) i++;
    if (i == len) return;  // nothing changed
    uint16_t a0 = addr & ~3u;
    uint16_t a1 = (end + 3) & ~3u;
    uint8_t edge[8];  // current bytes of the first and the last word
    for (i = 0; i < 4; i++) {
      edge[i] = read(a0 + i);
      edge[4 + i] = read(a1 - 4 + i);
    }
    auto byteAt = [&](uint16_t a) -> uint8_t {
      return (a < addr) ? edge[a - a0] : (a >= end) ? edge[a - a1 + 8] : src[a - addr];
    };
    uint16_t a = a0;
    while (a < a1) {
      uint16_t stop = a1;
      if ((byteAt(a) & byteAt(a + 1) & byteAt(a + 2) & byteAt(a + 3)) == 0xFF) stop = a + 4;
      LGT_ECCR |= 0x20;  // reset the E2PROM controller
      LGT_ECCR = 0x80;   // continuous write mode (SWM) on
      LGT_ECCR |= 0x10;
      EEARH = a >> 8;
      EEARL = a & 0xFF;
      for (; a < stop; a++) {
        _MMIO_BYTE(pgm_read_byte(&dataReg[a & 3])) = byteAt(a);
        if ((a & 3) == 3) {
          if (a == stop - 1) {  // SWM off before the last word
            LGT_ECCR = 0x80;
            LGT_ECCR &= 0xEF;
          }
          uint8_t sreg = SREG;
          cli();
          EECR = 0x44;
          EECR = 0x42;
          SREG = sreg;
        }
      }
    }
  }
};

static ChipEEPROM EEPROM __attribute__((unused));

// ---------- I2C bus guard (all firmware, Nano and LGT8F) ----------
// The Arduino Wire library waits forever when the display bus is stuck (a display that browns
// out, or a reset in the middle of a transfer leaves the display holding SDA low). That froze
// the whole module. i2cGuardBegin() frees a stuck bus and makes every I2C call give up after
// 25 ms; call it before the first display call (before hw.Init() / display.begin()).
// i2cGuardShow(display) sends a frame; after a timeout it frees the bus, re-initialises the
// display and pauses display updates for 1 s, so the rest of the firmware keeps running.
#include <Wire.h>
// SDA = A4 = PC4, SCL = A5 = PC5 on both chips. Single-bit port writes only (sbi/cbi), so LED
// pins on the same port changed by interrupts are never disturbed.
static void __attribute__((unused)) i2cGuardBegin() {
  TWCR = 0;
  DDRC &= ~(1 << 4);  // inputs with pull-ups
  DDRC &= ~(1 << 5);
  PORTC |= (1 << 4);
  PORTC |= (1 << 5);
  delayMicroseconds(10);
  for (uint8_t i = 0; i < 18 && !(PINC & (1 << 4)); i++) {  // clock out a half-sent byte
    PORTC &= ~(1 << 5);
    DDRC |= (1 << 5);  // SCL low
    delayMicroseconds(5);
    DDRC &= ~(1 << 5);
    PORTC |= (1 << 5);  // SCL released
    delayMicroseconds(5);
  }
  PORTC &= ~(1 << 4);
  DDRC |= (1 << 4);  // SDA low ...
  delayMicroseconds(5);
  DDRC &= ~(1 << 4);
  PORTC |= (1 << 4);  // ... and released while SCL is high: STOP
  Wire.setWireTimeout(25000, true);
}
static inline bool __attribute__((unused)) i2cGuardTimedOut() {
  if (!Wire.getWireTimeoutFlag()) return false;
  Wire.clearWireTimeoutFlag();
  return true;
}
// 1 s display pause after a timeout. Unsigned time difference: safe across the millis() wrap
// (49.7 days) and at any uptime.
static bool i2cGuardHold __attribute__((unused));
static uint32_t i2cGuardHoldStart __attribute__((unused));
static inline void __attribute__((unused)) i2cGuardPause() {
  i2cGuardHold = true;
  i2cGuardHoldStart = millis();
}
static inline bool __attribute__((unused)) i2cGuardPaused() {
  if (i2cGuardHold && millis() - i2cGuardHoldStart >= 1000) i2cGuardHold = false;
  return i2cGuardHold;
}
// D = Adafruit_SSD1306 (anything with display() and begin(vcc, address)), 0x3C. Returns false
// when the frame was skipped or failed. Re-init keeps the frame buffer (begin() doesn't
// allocate it again).
template<class D> static bool __attribute__((unused)) i2cGuardShow(D &d) {
  if (i2cGuardPaused()) return false;
  d.display();
  if (!i2cGuardTimedOut()) return true;
  i2cGuardBegin();
  d.begin(0x02 /* SSD1306_SWITCHCAPVCC */, 0x3C);
  i2cGuardTimedOut();
  i2cGuardPause();
  return false;
}
