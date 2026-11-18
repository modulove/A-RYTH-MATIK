#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM) - must be the first include
#include "src/libmodulove/arythmatik.h"
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <FastGPIO.h>
#include "OledRle.h"
#include "frames.h"  // generated from frames_src.h by ../tools/oled_rle.py

/*
 * Kissing Couple - Love Animation
 * Two figures meet for a kiss and part again, one animation frame per clock.
 *
 * Clock and reset are handled in the pin-change interrupt: every clock edge advances the frame
 * and switches the outputs right there. The display only shows the latest frame (it skips frames
 * when the clock is faster than the display).
 *
 * Outputs (frames 0..12, gates one frame long unless stated):
 *   OUT2 / OUT5  both high on frames 0 and 1 (one gate two frames long)
 *   OUT4         frames 4, 6, 8   (the kiss: OUT4 and OUT1 take turns)
 *   OUT1         frames 5, 7, 9
 *   OUT3 / OUT6  flip-flop, switched on frame 9 and held: OUT6 high in the 1st, 3rd, 5th ... loop,
 *                OUT3 high in the 2nd, 4th ... loop. Both low after power-up and reset until
 *                the next frame 9.
 *
 * CLK in: one frame per rising edge (or per 2/4/8 edges with the divider). No clock edge for 2 s =
 *         internal clock. Tempo in BPM = 4 frames per beat (the clock counts as 16ths).
 * RST in: frame 0 and the frame-0 outputs at once, flip-flop off. A clock edge up to 5 ms after
 *         the reset is that same downbeat, so reset + clock together = frame 0, the next clock =
 *         frame 1.
 *
 * Encoder:
 *   turn         no clock: internal tempo 30..600 BPM (turn fast for steps of 5)
 *                with clock: clock divider /1 /2 /4 /8
 *   click        info for 1.5 s: clock source, tempo or incoming BPM, divider, look
 *   hold 0.7 s   next look: classic / mirror / night / night mirror
 * Settings are saved 3 s after the last change.
 */

// Configuration flags
//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

// The CI builds the reversed .hex with -DENCODER_REVERSED (REVERSE_ENCODER is accepted too).
#if defined(REVERSE_ENCODER) && !defined(ENCODER_REVERSED)
#define ENCODER_REVERSED
#endif

#ifdef ROTATE_PANEL
// When panel is rotated
#define PANEL_ROTATED true
#define CLK_BIT _BV(3)  // CLK in D11 = PB3
#define RST_BIT _BV(5)  // RST in D13 = PB5
typedef FastGPIO::Pin<8> PinOut1;
typedef FastGPIO::Pin<9> PinOut2;
typedef FastGPIO::Pin<10> PinOut3;
typedef FastGPIO::Pin<5> PinOut4;
typedef FastGPIO::Pin<6> PinOut5;
typedef FastGPIO::Pin<7> PinOut6;
typedef FastGPIO::Pin<0> PinLed1;
typedef FastGPIO::Pin<1> PinLed2;
typedef FastGPIO::Pin<17> PinLed3;
typedef FastGPIO::Pin<14> PinLed4;
typedef FastGPIO::Pin<15> PinLed5;
typedef FastGPIO::Pin<16> PinLed6;
#else
// When panel is not rotated
#define PANEL_ROTATED false
#define CLK_BIT _BV(5)  // CLK in D13 = PB5
#define RST_BIT _BV(3)  // RST in D11 = PB3
typedef FastGPIO::Pin<5> PinOut1;
typedef FastGPIO::Pin<6> PinOut2;
typedef FastGPIO::Pin<7> PinOut3;
typedef FastGPIO::Pin<8> PinOut4;
typedef FastGPIO::Pin<9> PinOut5;
typedef FastGPIO::Pin<10> PinOut6;
typedef FastGPIO::Pin<14> PinLed1;
typedef FastGPIO::Pin<15> PinLed2;
typedef FastGPIO::Pin<16> PinLed3;
typedef FastGPIO::Pin<0> PinLed4;  // LED4/LED5 share D0/D1 with the serial port
typedef FastGPIO::Pin<1> PinLed5;
typedef FastGPIO::Pin<17> PinLed6;
#endif
typedef FastGPIO::Pin<4> PinClkLed;

using namespace modulove;
using namespace arythmatik;

#define FRAME_COUNT KISS_FRAMES  // 13
#define KISS_X 40                // the couple's left edge on the display
#define KISS_PAGE 0              // its top on display page 0 (y = 0)
#define FLIP_FRAME 9             // the OUT3/OUT6 flip-flop switches here (end of the kiss)
#define FLIP_OUT3 0x04
#define FLIP_OUT6 0x20

#define MIN_BPM 30
#define MAX_BPM 600
#define DEFAULT_BPM 225                  // 15 frames/s, about the old internal speed
#define EXT_TIMEOUT_US 2000000UL         // external clock = a CLK rising edge within the last 2 s
#define RESET_WINDOW_US 5000UL           // a CLK edge this soon after RST is the reset's downbeat
#define OVERLAY_MS 1500
#define SAVE_DELAY_MS 3000               // an EEPROM write stalls an LGT8F ~46 ms: save once, later
#define LONG_PRESS_MS 700

// Internal clock = Timer1 in CTC mode, one compare match per frame (15 s / BPM, 4 frames per beat).
// The prescaler is chosen from F_CPU so the slowest tempo fits 16 bits (16 MHz Nano, 32 MHz LGT).
#if (F_CPU / 256UL * 15UL / MIN_BPM) <= 65536UL
#define T1_DIV 256UL
#define T1_CS _BV(CS12)
#else
#define T1_DIV 1024UL
#define T1_CS (_BV(CS12) | _BV(CS10))
#endif
#define T1_FRAME_TICKS_1BPM (F_CPU / T1_DIV * 15UL)  // Timer1 counts per frame at 1 BPM

#define LOOK_MIRROR 0x01  // couple flipped left-right
#define LOOK_NIGHT 0x02   // inverted display

// Types first: the Arduino IDE puts its generated prototypes above the first function.
// Settings, saved in EEPROM as one block (EEPROM.put = one page swap on the LGT8F).
struct KissSettings {
  char magic[4];   // "KIS1"
  uint16_t bpm;    // internal tempo, MIN_BPM..MAX_BPM
  uint8_t divIdx;  // external clock divider, index into DIVIDERS
  uint8_t look;    // LOOK_MIRROR | LOOK_NIGHT
};

Arythmatik hw;

KissSettings settings = { { 'K', 'I', 'S', '1' }, DEFAULT_BPM, 0, 0 };
const uint8_t DIVIDERS[4] = { 1, 2, 4, 8 };
bool settingsDirty = false;
uint32_t settingsChangedAt;

// Shared between the interrupts and the main loop.
volatile uint8_t frame = 0;         // animation frame 0..12
volatile uint8_t clockDivider = 1;  // CLK edges per frame
volatile bool extClock = false;     // a CLK rising edge came within the last 2 s
volatile uint32_t extPeriodUs = 0;  // time between the last two CLK edges, 0 = not known yet

// Interrupt-only state (the PCINT and Timer1 interrupts never interrupt each other).
uint8_t pinsPrev;    // PINB at the last pin change
uint8_t divCount;    // position inside the divider
uint8_t flipGates;   // OUT3/OUT6 flip-flop: 0 (off), FLIP_OUT6 or FLIP_OUT3
uint8_t beatPos;     // frames into the beat, 0..3 (13 frames are no multiple of 4)
uint32_t lastClkUs;  // last CLK rising edge
uint32_t resetUs;    // last RST rising edge
bool resetWindow;    // the next CLK edge may still belong to the last reset

// Display / UI (main loop only).
bool overlayOn = false;
bool redraw = true;
uint32_t overlayUntil;
uint32_t lastTurnMs;

// Output gates for each frame, bit 0 = OUT1 .. bit 5 = OUT6, without the OUT3/OUT6 flip-flop:
// OUT2 + OUT5 = frames 0 and 1, OUT4 = frames 4, 6, 8, OUT1 = frames 5, 7, 9.
const uint8_t OUT_GATES[FRAME_COUNT] PROGMEM = {
  0x12, 0x12, 0x00, 0x00, 0x08, 0x01, 0x08, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00
};

// smaller 'Modulove_Logo', 28x32px to save resources
const unsigned char Modulove_Logo[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x03, 0xf0, 0x00, 0x00, 0x0c, 0x18, 0x7e, 0x00, 0x18, 0x05, 0x81, 0x80,
  0x10, 0x07, 0x00, 0xc0, 0x20, 0x06, 0x00, 0x40, 0x20, 0x05, 0x00, 0x20, 0x42, 0x09, 0x00, 0x20,
  0x42, 0x09, 0x00, 0x20, 0x41, 0x09, 0x01, 0x20, 0x41, 0x89, 0x02, 0x20, 0x40, 0x87, 0x0c, 0x20,
  0x20, 0xc7, 0x0c, 0x20, 0x20, 0x7d, 0xf0, 0x40, 0x10, 0x00, 0x00, 0x40, 0x10, 0x00, 0x00, 0x80,
  0x08, 0x00, 0x01, 0x80, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01, 0x00, 0x04, 0x00,
  0x00, 0xc0, 0x0c, 0x00, 0x00, 0x60, 0x10, 0x00, 0x00, 0x30, 0x20, 0x00, 0x00, 0x08, 0x40, 0x00,
  0x00, 0x04, 0x80, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x04, 0x80, 0x00,
  0x00, 0x04, 0x40, 0x00, 0x00, 0x04, 0x40, 0x00, 0x00, 0x06, 0xc0, 0x00, 0x00, 0x03, 0x80, 0x00
};


// ---------- outputs and frame stepping (interrupt side) ----------

// Six outputs and their LEDs, bit 0 = OUT1. FastGPIO writes are single sbi/cbi instructions:
// fast, and safe next to digitalWrite() calls in the main loop.
static inline void writeOutputs(uint8_t g) {
  PinOut1::setOutputValue(g & 0x01);
  PinLed1::setOutputValue(g & 0x01);
  PinOut2::setOutputValue(g & 0x02);
  PinLed2::setOutputValue(g & 0x02);
  PinOut3::setOutputValue(g & 0x04);
  PinLed3::setOutputValue(g & 0x04);
  PinOut4::setOutputValue(g & 0x08);
  PinLed4::setOutputValue(g & 0x08);
  PinOut5::setOutputValue(g & 0x10);
  PinLed5::setOutputValue(g & 0x10);
  PinOut6::setOutputValue(g & 0x20);
  PinLed6::setOutputValue(g & 0x20);
}

static void frameOutputs(uint8_t f) {
  writeOutputs(pgm_read_byte(&OUT_GATES[f]) | flipGates);
}

static void stepFrame() {
  uint8_t f = frame + 1;
  if (f >= FRAME_COUNT) f = 0;
  frame = f;
  if (f == FLIP_FRAME) flipGates = (flipGates == FLIP_OUT6) ? FLIP_OUT3 : FLIP_OUT6;  // off -> OUT6
  beatPos = (beatPos + 1) & 3;
  frameOutputs(f);
}

static void onReset() {
  frame = 0;
  flipGates = 0;  // flip-flop off until the next frame 9
  beatPos = 0;
  frameOutputs(0);
  divCount = 0;
  resetUs = micros();
  resetWindow = true;
  TCNT1 = 0;  // the internal clock starts over: its next frame comes one full period after the reset
  TIFR1 = _BV(OCF1A);
  if (!extClock) PinClkLed::setOutputValueHigh();  // beat LED: frame 0
}

static void onClock() {
  uint32_t now = micros();
  if (extClock) extPeriodUs = now - lastClkUs;
  lastClkUs = now;
  extClock = true;
  if (resetWindow) {
    resetWindow = false;
    if (now - resetUs < RESET_WINDOW_US) {  // reset + clock together: this edge plays frame 0
      divCount = (clockDivider > 1) ? 1 : 0;
      return;
    }
  }
  if (divCount == 0) stepFrame();
  if (++divCount >= clockDivider) divCount = 0;
}

// libmodulove's ISR(PCINT0_vect) calls this on every pin change of CLK or RST.
void onInputChange() {
  uint8_t pins = PINB;
  uint8_t rose = pins & ~pinsPrev;
  uint8_t changed = pins ^ pinsPrev;
  pinsPrev = pins;
  if (changed & CLK_BIT) PinClkLed::setOutputValue(pins & CLK_BIT);  // CLK LED mirrors CLK in
  if (rose & RST_BIT) onReset();  // reset first: a clock edge in the same interrupt is its downbeat
  if (rose & CLK_BIT) onClock();
}

// Internal clock: one compare match per frame. Timer1 always runs; it only steps the frame
// while there is no external clock.
ISR(TIMER1_COMPA_vect) {
  uint32_t now = micros();
  if (resetWindow && now - resetUs >= RESET_WINDOW_US) resetWindow = false;
  if (extClock) {
    if (now - lastClkUs < EXT_TIMEOUT_US) return;
    extClock = false;  // no CLK edge for 2 s: the internal clock takes over
    extPeriodUs = 0;
    divCount = 0;
  }
  stepFrame();
  PinClkLed::setOutputValue(beatPos == 0);  // CLK LED shows the beat (every 4 frames)
}

static uint16_t tempoTop(uint16_t bpm) {
  return (uint16_t)(T1_FRAME_TICKS_1BPM / bpm - 1);
}

// Start Timer1 (internal clock) and the CLK/RST handler.
static void startClock() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1A = 0;  // Arduino set Timer1 up for PWM on D9/D10; Kiss doesn't use analogWrite()
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = tempoTop(settings.bpm);
    TIFR1 = _BV(OCF1A);
    TIMSK1 = _BV(OCIE1A);
    TCCR1B = _BV(WGM12) | T1_CS;  // CTC, TOP = OCR1A
    pinsPrev = PINB;
    hw.AttachClockHandler(onInputChange);  // libmodulove calls it on every CLK/RST pin change
  }
}

static void applyTempo() {
  uint16_t top = tempoTop(settings.bpm);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    OCR1A = top;
    if (TCNT1 > top) TCNT1 = 0;  // past the new TOP it would count up to 0xFFFF first
  }
}

// ---------- settings ----------
void loadSettings() {
  KissSettings st;
  EEPROM.get(0, st);
  if (st.magic[0] != 'K' || st.magic[1] != 'I' || st.magic[2] != 'S' || st.magic[3] != '1') return;
  settings.bpm = constrain(st.bpm, MIN_BPM, MAX_BPM);
  settings.divIdx = (st.divIdx < sizeof(DIVIDERS)) ? st.divIdx : 0;
  settings.look = st.look & (LOOK_MIRROR | LOOK_NIGHT);
}

void saveSettings() {
  EEPROM.put(0, settings);  // one block (an LGT8F stalls ~46 ms per EEPROM write)
  settingsDirty = false;
}

void settingsChanged() {
  settingsDirty = true;
  settingsChangedAt = millis();
}

// ---------- encoder ----------
void showOverlay() {
  overlayOn = true;
  overlayUntil = millis() + OVERLAY_MS;
  redraw = true;
}

// Turn: internal tempo without a clock, clock divider with a clock.
void onTurn(EncoderButton &eb) {
  int16_t inc = eb.increment();
#ifndef ENCODER_REVERSED
  inc = -inc;  // hw.eb is EncoderButton(2, 3); the other A-RYTH-MATIK firmware use (3, 2) as normal
#endif
  if (inc == 0) return;
  uint32_t now = millis();
  if (extClock) {
    int8_t d = (int8_t)settings.divIdx + (inc > 0 ? 1 : -1);
    settings.divIdx = constrain(d, 0, (int8_t)sizeof(DIVIDERS) - 1);
    clockDivider = DIVIDERS[settings.divIdx];
  } else {
    int16_t step = (now - lastTurnMs < 60) ? 5 : 1;  // fast turn = bigger steps
    int16_t bpm = (int16_t)settings.bpm + inc * step;
    settings.bpm = constrain(bpm, MIN_BPM, MAX_BPM);
    applyTempo();
  }
  lastTurnMs = now;
  settingsChanged();
  showOverlay();
}

void onClick(EncoderButton &) {
  showOverlay();
}

// Hold: next look (classic, mirror, night, night mirror).
void onLongPress(EncoderButton &) {
  settings.look = (settings.look + 1) & (LOOK_MIRROR | LOOK_NIGHT);
  settingsChanged();
  showOverlay();
}

// ---------- display ----------

static const __FlashStringHelper *lookName(uint8_t look) {
  switch (look) {
    case LOOK_MIRROR: return F("mirror");
    case LOOK_NIGHT: return F("night");
    case LOOK_MIRROR | LOOK_NIGHT: return F("night mirror");
    default: return F("classic");
  }
}

static void printTenths(uint32_t v) {
  hw.display.print(v / 10);
  hw.display.print('.');
  hw.display.print(v % 10);
}

// Info box: clock source and divider, tempo, look, what turning does.
static void drawOverlay() {
  Adafruit_SSD1306 &d = hw.display;
  bool ext = extClock;
  d.fillRect(4, 10, 120, 44, BLACK);
  d.drawRect(4, 10, 120, 44, WHITE);
  d.setTextSize(1);
  d.setTextColor(WHITE);
  d.setTextWrap(false);
  d.setCursor(9, 14);
  if (ext) {
    uint32_t p;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      p = extPeriodUs;
    }
    d.print(F("EXT CLOCK  /"));
    d.print(clockDivider);
    d.setCursor(9, 24);
    if (p) {
      d.print((15000000UL + p / 2) / p);  // BPM with the clock as 16ths (4 PPQN)
      d.print(F(" BPM  "));
      printTenths((10000000UL + p / 2) / p);
      d.print(F(" Hz"));
    } else {
      d.print(F("--- BPM"));
    }
  } else {
    d.print(F("INT CLOCK"));
    d.setCursor(9, 24);
    d.print(settings.bpm);
    d.print(F(" BPM  "));
    printTenths((settings.bpm * 2UL + 1) / 3);  // frames per second, 4 frames per beat
    d.print(F(" fps"));
  }
  d.setCursor(9, 34);
  d.print(F("look: "));
  d.print(lookName(settings.look));
  d.setCursor(9, 44);
  d.print(ext ? F("turn: divider") : F("turn: tempo"));
}

// Draw frame f and send it. False = not shown.
static bool drawFrame(uint8_t f) {
  uint8_t *buf = hw.display.getBuffer();
  if (!buf) return false;
  hw.display.clearDisplay();
  oledRleDraw(buf, KISS_RLE, KISS_OFS, f, KISS_W, KISS_PAGES, KISS_X, KISS_PAGE,
              settings.look & LOOK_MIRROR, PANEL_ROTATED);
  if (overlayOn) drawOverlay();
  if (settings.look & LOOK_NIGHT) {
    for (uint16_t i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT / 8; i++) buf[i] = ~buf[i];
  }
  return i2cGuardShow(hw.display);
}

void drawLogo() {
  hw.display.fillRect(0, 0, 128, 64, BLACK);
  hw.display.drawBitmap(50, 16, Modulove_Logo, 28, 32, WHITE);
  i2cGuardShow(hw.display);
}

void setup() {
#ifdef ROTATE_PANEL
  hw.config.RotatePanel = true;
#endif
#ifdef ENCODER_REVERSED
  hw.config.ReverseEncoder = true;
#endif

  loadSettings();
  clockDivider = DIVIDERS[settings.divIdx];

  i2cGuardBegin();  // a stuck display bus can no longer freeze the module
  hw.Init();

  // Outputs and LEDs as outputs. Kiss sets them itself (FastGPIO): libmodulove's rotated pin
  // table has OUT4 on D1 instead of D5.
  PinOut1::setOutputLow();
  PinOut2::setOutputLow();
  PinOut3::setOutputLow();
  PinOut4::setOutputLow();
  PinOut5::setOutputLow();
  PinOut6::setOutputLow();
  PinLed1::setOutputLow();
  PinLed2::setOutputLow();
  PinLed3::setOutputLow();
  PinLed4::setOutputLow();
  PinLed5::setOutputLow();
  PinLed6::setOutputLow();
  PinClkLed::setOutputLow();
  frameOutputs(0);

  // libmodulove's encoder (D2/D3, switch D12); Kiss creates no second one on the same pins.
  hw.eb.setDebounceInterval(5);
  hw.eb.setMultiClickInterval(30);  // no double clicks: a click fires right after the release
  hw.eb.setLongClickDuration(LONG_PRESS_MS);
  hw.eb.setEncoderHandler(onTurn);
  hw.eb.setEncoderPressedHandler(onTurn);
  hw.eb.setClickHandler(onClick);
  hw.eb.setLongPressHandler(onLongPress);

  startClock();  // clock, reset and the internal clock run from here on (also during the boot screens)

#if !defined(DISABLE_BOOT_LOGO)
  drawLogo();
  delay(1200);
#endif

  hw.display.clearDisplay();
  hw.display.setTextColor(WHITE);
  hw.display.setCursor(18, 20);
  hw.display.print(F("Kissing Couple"));
  hw.display.setCursor(16, 35);
  hw.display.setTextSize(1);
  hw.display.print(F("Love Animation"));
  i2cGuardShow(hw.display);

  delay(2000);
}

void loop() {
  static uint8_t shownFrame = 0xFF;
  static bool shownExt = false;
  static uint32_t lastDrawMs;

  hw.eb.update();

  uint32_t now = millis();
  bool ext = extClock;
  if (ext != shownExt) {  // clock patched in or gone: show the info for a moment
    shownExt = ext;
    showOverlay();
  }
  if (overlayOn) {
    if ((int32_t)(now - overlayUntil) >= 0) {
      overlayOn = false;
      redraw = true;
    } else if (now - lastDrawMs >= 250) {
      redraw = true;  // keep the incoming BPM fresh while the clock is slow
    }
  }
  if (settingsDirty && now - settingsChangedAt >= SAVE_DELAY_MS) saveSettings();

  // Only the latest frame is drawn: frames the display could not keep up with are skipped.
  uint8_t f = frame;
  if (f != shownFrame || redraw) {
    shownFrame = f;
    redraw = false;
    lastDrawMs = now;
    if (!drawFrame(f)) redraw = true;  // display bus guard skipped it: try again
  }
}
