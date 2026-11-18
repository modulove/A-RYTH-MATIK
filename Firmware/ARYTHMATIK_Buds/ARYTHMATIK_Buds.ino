#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM) - must be the first include
#include "src/libmodulove/arythmatik.h"
#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include "EncoderButton.h"
#include <util/atomic.h>

//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

using namespace modulove;
using namespace arythmatik;

const unsigned long OLED_UPDATE_INTERVAL_NORMAL = 150;  // Normal OLED update interval in milliseconds
const unsigned long OLED_UPDATE_INTERVAL_IDLE = 300;    // OLED update interval when idle
const unsigned long TRIGGER_PULSE_DURATION = 12;  // Trigger pulse duration in milliseconds

// Enum for menu items
enum Menu {
  PROB,
  TOGGLE,
  SWING,
  FACTOR,
  BOUNCE,
  MENU_COUNT  // Total number of menu items
};

// Bounce parameters. START is kept in 2 ms units (its turn step), so every value fits a byte.
#define BOUNCE_START_LO 5    // 10 ms
#define BOUNCE_START_HI 250  // 500 ms
#define BOUNCE_START_DEF 75  // 150 ms
#define BOUNCE_MIN_LO 5      // ms
#define BOUNCE_MIN_HI 80
#define BOUNCE_MIN_DEF 18
#define BOUNCE_REST_LO 30  // %
#define BOUNCE_REST_HI 95
#define BOUNCE_REST_DEF 62

// Declare A-RYTH-MATIK hardware variable.
Arythmatik hw;

// The encoder is libmodulove's hw.eb (pins in the ENCODER_REVERSED order of this sketch):
// a second EncoderButton on the same pins cost ~80 bytes of RAM and handled every pulse twice.
#define encoder hw.eb

bool isChannelASelected = true;
bool isEncoderIdle = false;
uint8_t bounceField[2];  // Bounce field that turning changes: 0 = START, 1 = MIN, 2 = REST

uint16_t lastUpdateTime = 0;  // low 16 bits of millis() are enough for the intervals here

// ---------- trigger engine ----------
// Side A = RST input, side B = CLK input. Both inputs are read by the pin-change interrupt and
// each side owns one Timer1 compare channel (side A: OCR1A, side B: OCR1B) for its 12 ms pulse
// end, its swung step and its Bounce hits. So the two sides run at any tempo, independent of
// each other and of the display (before, a display refresh blocked both sides for 25-47 ms).
#if F_CPU > 20000000UL
#define T1_CS ((1 << CS12) | (1 << CS10))  // /1024: 32 us per tick at 32 MHz
#define T1_PRESCALE 1024UL
#else
#define T1_CS (1 << CS12)  // /256: 16 us per tick at 16 MHz
#define T1_PRESCALE 256UL
#endif
#define T1_US_PER_TICK (T1_PRESCALE * 1000000UL / F_CPU)
#define PULSE_TICKS ((uint16_t)(TRIGGER_PULSE_DURATION * 1000UL / T1_US_PER_TICK))
#define MAX_DELAY_TICKS 30000UL  // keeps the signed 16-bit time compare valid
#define PEND_END 1      // clock output trigger ends (main and inverted too, except Toggle/Bounce)
#define PEND_FIRE 2     // swung step / next Bounce hit
#define PEND_HIT_END 4  // Bounce: main output trigger ends
// Bounce flight times in 1/256 Timer1 ticks, exact for START and MIN: 2 ms = 125 ticks of 16 us
#if T1_US_PER_TICK == 16
#define T1_SHIFT 0
#elif T1_US_PER_TICK == 32
#define T1_SHIFT 1
#else
#error "Bounce timing expects 16 or 32 us per Timer1 tick"
#endif
#define START_Q8(v) ((uint32_t)mul8(v, 125) << (8 - T1_SHIFT))  // START v (2 ms units)
// flight q (1/256 ticks) shorter than MIN v (ms)? MIN = v x 125 half ticks (16 us ticks)
#define BELOW_MIN(q, v) ((uint16_t)((q) >> (7 - T1_SHIFT)) < mul8(v, 125))
// Outputs of side s (numbers as in setOut)
#define OUT_INV(s) ((s) * 3)
#define OUT_MAIN(s) ((s) * 3 + 1)
#define OUT_CLK(s) ((s) * 3 + 2)

// One side: its settings (written by the UI; single bytes, so the interrupts can read them at
// any time; bRestQ is written in an ATOMIC_BLOCK) and the state of its interrupts.
struct Side {
  uint8_t mode, prob, swing, factor;  // Menu; prob 0..100, swing 0..70 (%); factor 1..8
  uint8_t bStart, bMin, bRest;        // Bounce: START (2 ms units), MIN (ms), REST (%)
  union {                             // Bounce: REST / 100 in 1/2^24 units, for the interrupt
    uint32_t bRestQ;
    uint8_t bRestB[4];
  };
  union {
    uint8_t count;                    // factor counter
    uint8_t bDueFrac;                 // Bounce: fireAt's fraction, in 1/256 ticks
  };
  uint8_t phase;                      // swing: 0 = next clock is on the beat, 1 = off-beat
  uint8_t odd;                        // swing: the off-beat step is timed by Timer1
  uint8_t pend;                       // PEND_END / PEND_FIRE / PEND_HIT_END
  bool latch;                         // toggle state / last coin flip
  uint8_t level;                      // last input level
  uint16_t endAt, fireAt;             // Timer1 ticks; Bounce: fireAt = next hit
  uint16_t hitEndAt;                  // Bounce: end of the main output trigger
  union {                             // Bounce: flight time to the next hit, in 1/256 ticks
    uint32_t bFlight;
    uint8_t bFlightB[4];
  };
  uint32_t lastEdgeUs;
};

// One 8 x 8 bit product = one MUL instruction. (The empty asm keeps avr-gcc from merging the
// products of bounceRest() into 16 x 16 -> 32 bit library multiplies.)
static inline uint16_t mul8(uint8_t a, uint8_t b) {
  uint16_t p = (uint16_t)a * b;
  __asm__("" : "+r"(p));
  return p;
}
volatile Side sides[2];
volatile bool redrawA;
uint16_t rng16 = 1;

// settings in EEPROM (saved 3 s after the last change); "BUD1" (v1.1.0, no Bounce) is read too
struct SavedSettings {
  char magic[4];
  uint8_t menu[2], prob[2], swing[2], factor[2];
  uint8_t bStart[2], bMin[2], bRest[2];  // "BUD2" only
};
bool settingsDirty = false;
uint16_t settingsChangedAt = 0;  // low 16 bits of millis()

void setup() {

#ifdef ROTATE_PANEL
  hw.config.RotatePanel = true;
#endif

#ifdef REVERSE_ENCODER
  hw.config.ReverseEncoder = true;
#endif

  // Initialize the A-RYTH-MATIK peripherals.
  i2cGuardBegin();  // a stuck display bus can no longer freeze the module
  hw.Init();
  rng16 = (uint16_t)chipRandomSeed() | 1;  // coin flips differ after every power-up

  loadSettings();
  for (uint8_t o = 0; o < 6; o++) setOut(o, false);
  DDRD |= (1 << 0) | (1 << 1) | (1 << 5) | (1 << 6) | (1 << 7);  // D0 D1 D5 D6 D7
  DDRB |= (1 << 0) | (1 << 1) | (1 << 2);                       // D8 D9 D10
  DDRC |= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);            // A0..A3
  TCCR1A = 0;  // Timer1 free-running (Arduino set it up for PWM, Buds doesn't use that)
  TCCR1B = T1_CS;
  TIMSK1 = 0;
  TIFR1 = (1 << OCF1A) | (1 << OCF1B) | (1 << TOV1);
  sides[0].level = hw.rst.Read();
  sides[1].level = hw.clk.Read();
  hw.AttachClockHandler(onInputChange);  // libmodulove calls it on every CLK/RST pin change

  // Set up encoder parameters
  encoder.setDebounceInterval(5);
  encoder.setMultiClickInterval(70);
  encoder.setLongClickDuration(400);
  encoder.setRateLimit(10);
  encoder.setIdleTimeout(1000);
  encoder.setIdleHandler(onEncoderIdle);
  encoder.setClickHandler(onEncoderClicked); // short press to switch sideA-sideB
  encoder.setDoubleClickHandler(onEncoderDoubleClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);  // long click: toggle state / Bounce field
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);
}

// abstract tree UI
void drawTree(int x, int y, uint8_t prob) {
  // main branch
  hw.display.drawLine(x, y, x, y - 10, WHITE);

  // primary twigs
  hw.display.drawLine(x, y - 5, x - 5, y - 15, WHITE);
  hw.display.drawLine(x, y - 5, x + 5, y - 15, WHITE);

  // two dots based on probability
  if (prob <= 50) {
    hw.display.fillCircle(x - 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x - 5, y - 15, 4, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 2, WHITE);
  } else {
    hw.display.drawCircle(x - 5, y - 15, 2, WHITE);
    hw.display.fillCircle(x + 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 4, WHITE);
  }
}

// draw vertical line
void drawDottedLine(int x, int y, int length, int spacing) {
  for (int i = 0; i < length; i += spacing) {
    hw.display.drawPixel(x, y + i, WHITE);
  }
}

// bottom bar indicator
void drawBottomBar() {
  hw.display.setCursor(0, 56);

  uint8_t modeA = sides[0].mode, modeB = sides[1].mode;
  if (modeA == PROB) {
    // taller center line for 50% in prob mode
    drawDottedLine(30, 54, 10, 2);  // center line
    // bar for Channel A (centered at 50% and fill left or right)
    int barLengthA = ((int)sides[0].prob - 50) * 2 / 5;  // -20..20
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    if (barLengthA > 0) {
      hw.display.fillRoundRect(30, 58, barLengthA, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(30 + barLengthA, 58, -barLengthA, 4, 2, WHITE);
    }
  } else if (modeA == TOGGLE) {
    // Toggle mode indicator for Channel A
    bool stateA = sides[0].latch;
    hw.display.fillCircle(30, 25, 3, stateA ? WHITE : BLACK);
    hw.display.drawCircle(30, 25, 5, WHITE);
    hw.display.fillCircle(30, 45, 3, stateA ? BLACK : WHITE);
    hw.display.drawCircle(30, 45, 5, WHITE);
    hw.display.setCursor(10, 52);
    //hw.display.print(stateA ? "ON " : "OFF");
  } else if (modeA == SWING) {
    // Swing mode indicator for Channel A
    int barLengthA = ((int)sides[0].swing - 50) * 2;
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(10, 58, barLengthA, 4, 2, WHITE);
  } else if (modeA == FACTOR) {
    // Factor mode indicator for Channel A
    hw.display.setCursor(24, 30);
    hw.display.setTextSize(2);
    hw.display.print(sides[0].factor);
    hw.display.setTextSize(1);
  }

  if (modeB == PROB) {
    drawDottedLine(94, 54, 10, 4);  // center line
    // bar for Channel B (centered at 50% and fill left or right)
    int barLengthB = ((int)sides[1].prob - 50) * 2 / 5;  // -20..20
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    if (barLengthB > 0) {
      hw.display.fillRoundRect(94, 58, barLengthB, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(94 + barLengthB, 58, -barLengthB, 4, 2, WHITE);
    }
  } else if (modeB == TOGGLE) {
    // Toggle mode indicator for Channel B
    bool stateB = sides[1].latch;
    hw.display.fillCircle(96, 25, 3, stateB ? WHITE : BLACK);
    hw.display.drawCircle(96, 25, 5, WHITE);
    hw.display.fillCircle(96, 45, 3, stateB ? BLACK : WHITE);
    hw.display.drawCircle(96, 45, 5, WHITE);
    hw.display.setCursor(74, 52);
    //hw.display.print(stateB ? "ON " : "OFF");
  } else if (modeB == SWING) {
    // Swing mode indicator for Channel B
    int barLengthB = ((int)sides[1].swing - 50) * 2;
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(74, 58, barLengthB, 4, 2, WHITE);
  } else if (modeB == FACTOR) {
    // Factor mode indicator for Channel B
    hw.display.setCursor(94, 30);
    hw.display.setTextSize(2);
    hw.display.print(sides[1].factor);
    hw.display.setTextSize(1);
  }
}

// mode names in flash (string literals, and a switch's lookup table, would sit in RAM)
const char MODE_NAMES[MENU_COUNT][7] PROGMEM = { "Buds", "Toggle", "Swing", "Factor", "Bounce" };
const __FlashStringHelper *modeName(uint8_t mode) {
  return (const __FlashStringHelper *)MODE_NAMES[mode];
}

// top bar menu
void drawTopBar() {
  hw.display.setCursor(0, 0);

  const __FlashStringHelper *modeStrA = modeName(sides[0].mode);
  const __FlashStringHelper *modeStrB = modeName(sides[1].mode);

  if (isChannelASelected) {
    hw.display.setCursor(16, 1);
    hw.display.print(modeStrA);
    hw.display.fillRoundRect(10, 0, 44, 12, 3, INVERSE);  // Invert the mode description for Channel A
    hw.display.setCursor(80, 1);
    hw.display.print(modeStrB);
  } else {
    hw.display.setCursor(80, 1);
    hw.display.print(modeStrB);
    hw.display.fillRoundRect(74, 0, 44, 12, 3, INVERSE);  // Invert the mode description for Channel B
    hw.display.setCursor(16, 1);
    hw.display.print(modeStrA);
  }
}

// swing mode UI
void drawSwingUI(uint8_t s, int x) {
  uint8_t swing = sides[s].swing;
  hw.display.setCursor(x - 10, 16);
  hw.display.print(swing);
  hw.display.print(F("%"));

  // knob for swing amount
  drawKnob(x, 38, (swing - 50) / 20.0);  // Normalized to 0-1 range
}

void drawKnob(int x, int y, float value) {
  hw.display.drawCircle(x, y, 5, WHITE);
  hw.display.drawCircle(x, y, 8, WHITE);  // fancy knob

  // Calculate angle based on value (0 to 1)
  float angle = value * 2 * PI - PI / 2;
  int x1 = x + 10 * cos(angle);  // Calculate x position of the line
  int y1 = y + 10 * sin(angle);  // Calculate y position of the line

  hw.display.drawLine(x, y, x1, y1, WHITE);  // knob position
}

// Bounce mode UI: START (ms), MIN (ms), REST (%); the field that turning changes is underlined
void drawBounceUI(uint8_t s, int x) {
  volatile Side &d = sides[s];
  hw.display.setCursor(x - 28, 16);
  hw.display.print(F("Start"));
  hw.display.setCursor(x + 8, 16);
  hw.display.print(d.bStart * 2);
  hw.display.setCursor(x - 28, 31);
  hw.display.print(F("Min"));
  hw.display.setCursor(x + 8, 31);
  hw.display.print(d.bMin);
  hw.display.setCursor(x - 28, 46);
  hw.display.print(F("Rest"));
  hw.display.setCursor(x + 8, 46);
  hw.display.print(d.bRest);
  hw.display.print(F("%"));
  hw.display.drawFastHLine(x - 28, 25 + bounceField[s] * 15, 54, WHITE);
}

void updateOLED() {
  hw.display.clearDisplay();
  drawTopBar();
  drawDottedLine(64, 0, 64, 2);  // Centerline

  for (uint8_t s = 0; s < 2; s++) {  // side A left (centre 32), side B right (centre 96)
    int x = s ? 96 : 32;
    uint8_t mode = sides[s].mode;
    if (mode == PROB) {
      hw.display.setCursor(x - 8, 16);
      hw.display.print(sides[s].prob);
      hw.display.print(F("% "));
      drawTree(x, 48, sides[s].prob);
    } else if (mode == SWING) {
      drawSwingUI(s, x);
    } else if (mode == BOUNCE) {
      drawBounceUI(s, x);
    }
  }

  drawBottomBar();
  i2cGuardShow(hw.display);
}

// ---------- settings ----------
// Bounce REST % for side s, with REST / 100 in 1/2^24 units (rounded) for the interrupt, which
// then needs no division
void setRest(uint8_t s, uint8_t rest) {
  uint32_t q = (((uint32_t)rest << 24) + 50) / 100;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    sides[s].bRest = rest;
    sides[s].bRestQ = q;
  }
}

void settingsChanged() {
  settingsDirty = true;
  settingsChangedAt = millis();
}

void saveSettings() {
  SavedSettings st;
  st.magic[0] = 'B';
  st.magic[1] = 'U';
  st.magic[2] = 'D';
  st.magic[3] = '2';
  for (uint8_t i = 0; i < 2; i++) {
    volatile Side &d = sides[i];
    st.menu[i] = d.mode;
    st.prob[i] = d.prob;
    st.swing[i] = d.swing;
    st.factor[i] = d.factor;
    st.bStart[i] = d.bStart;
    st.bMin[i] = d.bMin;
    st.bRest[i] = d.bRest;
  }
  EEPROM.put(0, st);  // one block (an LGT8F stalls ~46 ms per EEPROM write)
  settingsDirty = false;
}

// Runs in setup() before the interrupts are set up.
void loadSettings() {
  for (uint8_t i = 0; i < 2; i++) {  // defaults
    volatile Side &d = sides[i];
    d.mode = PROB;
    d.prob = 50;
    d.swing = 50;
    d.factor = 1;
    d.bStart = BOUNCE_START_DEF;
    d.bMin = BOUNCE_MIN_DEF;
    setRest(i, BOUNCE_REST_DEF);
  }
  SavedSettings st;
  EEPROM.get(0, st);
  char ver = st.magic[3];  // '1' = v1.1.0 settings (no Bounce values yet), '2' = with Bounce
  if (st.magic[0] == 'B' && st.magic[1] == 'U' && st.magic[2] == 'D' && (ver == '1' || ver == '2')) {
    for (uint8_t i = 0; i < 2; i++) {
      volatile Side &d = sides[i];
      d.mode = st.menu[i] % MENU_COUNT;
      d.prob = constrain(st.prob[i], 0, 100);
      d.swing = constrain(st.swing[i], 0, 70);
      d.factor = constrain(st.factor[i], 1, 8);
      if (ver == '2') {
        d.bStart = constrain(st.bStart[i], BOUNCE_START_LO, BOUNCE_START_HI);
        d.bMin = constrain(st.bMin[i], BOUNCE_MIN_LO, BOUNCE_MIN_HI);
        setRest(i, constrain(st.bRest[i], BOUNCE_REST_LO, BOUNCE_REST_HI));
      }
    }
  }
}

// Toggle state flipped by hand (long click, or turning in Toggle mode).
void flipLatch(uint8_t s) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    volatile Side &d = sides[s];
    d.latch = !d.latch;
    if (d.mode == TOGGLE) {
      setOut(OUT_MAIN(s), d.latch);
      setOut(OUT_INV(s), !d.latch);
    }
  }
}

void onEncoderIdle(EncoderButton &eb) {
  isEncoderIdle = true;
}

void onEncoderClicked(EncoderButton &eb) {
  isEncoderIdle = false;
  isChannelASelected = !isChannelASelected;
}

void onEncoderDoubleClicked(EncoderButton &eb) {
  isEncoderIdle = false;
  isChannelASelected = !isChannelASelected;  // Switch sides
}

void onEncoderLongClicked(EncoderButton &eb) {
  isEncoderIdle = false;
  uint8_t s = isChannelASelected ? 0 : 1;
  if (sides[s].mode == BOUNCE) {
    if (++bounceField[s] > 2) bounceField[s] = 0;  // next field: START, MIN, REST
  } else {
    flipLatch(s);  // flip the toggle state of the selected side
  }
}

void onEncoderPressedRotation(EncoderButton &eb) {
  isEncoderIdle = false;
  uint8_t s = isChannelASelected ? 0 : 1;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    volatile Side &d = sides[s];
    uint8_t mode = d.mode + 1;
    if (mode >= MENU_COUNT) mode = 0;
    if (d.mode == BOUNCE || mode == BOUNCE) {
      // Bounce times the main output by itself and keeps the inverted output low: end both here
      d.pend &= ~PEND_HIT_END;
      setOut(OUT_MAIN(s), false);
      setOut(OUT_INV(s), false);
    }
    d.mode = mode;  // new mode: start its pattern fresh
    d.phase = 0;
    d.count = 0;
    d.odd = 0;
    d.pend &= ~PEND_FIRE;
  }
  settingsChanged();
}

// v moved by delta, kept in lo..hi
uint8_t stepValue(uint8_t v, int delta, uint8_t lo, uint8_t hi) {
  int n = v + delta;
  return n < lo ? lo : (n > hi ? hi : n);
}

void onEncoderRotation(EncoderButton& eb) {
  isEncoderIdle = false;
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
#ifndef ENCODER_REVERSED
  increment = -increment;  // hw.eb counts the other way round than this sketch's normal build
#endif
  if (increment == 0) return;

  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment < 0) {
    acceleratedIncrement = -acceleratedIncrement;  // Ensure that the direction of increment is preserved
  }

  uint8_t s = isChannelASelected ? 0 : 1;
  volatile Side &d = sides[s];
  switch (d.mode) {
    case PROB:
      d.prob = stepValue(d.prob, acceleratedIncrement * 2, 0, 100);  // 2 % steps
      break;
    case TOGGLE:
      flipLatch(s);
      break;
    case SWING:
      d.swing = stepValue(d.swing, acceleratedIncrement, 0, 70);
      break;
    case FACTOR:
      d.factor = stepValue(d.factor, acceleratedIncrement, 1, 8);
      break;
    case BOUNCE:
      if (bounceField[s] == 0) {
        d.bStart = stepValue(d.bStart, acceleratedIncrement, BOUNCE_START_LO, BOUNCE_START_HI);  // 2 ms steps
      } else if (bounceField[s] == 1) {
        d.bMin = stepValue(d.bMin, acceleratedIncrement, BOUNCE_MIN_LO, BOUNCE_MIN_HI);
      } else {
        setRest(s, stepValue(d.bRest, acceleratedIncrement, BOUNCE_REST_LO, BOUNCE_REST_HI));
      }
      break;
    default:
      break;
  }
  settingsChanged();
}

// ---------- interrupt side ----------
static inline uint8_t rand100() {  // xorshift16, 0..99
  rng16 ^= rng16 << 7;
  rng16 ^= rng16 >> 9;
  rng16 ^= rng16 << 8;
  return ((uint16_t)(rng16 >> 8) * 100) >> 8;
}

// Output o (0..5) and its LED with single-instruction port writes: short and safe in interrupts
// (digitalWrite through libmodulove took a deep call chain in every clock interrupt).
#define PAIR(out, led) \
  do { \
    if (on) { \
      FastGPIO::Pin<out>::setOutputValueHigh(); \
      FastGPIO::Pin<led>::setOutputValueHigh(); \
    } else { \
      FastGPIO::Pin<out>::setOutputValueLow(); \
      FastGPIO::Pin<led>::setOutputValueLow(); \
    } \
  } while (0)
static void setOut(uint8_t o, bool on) {
  switch (o) {
#ifdef ROTATE_PANEL
    case 0: PAIR(8, 0); break;
    case 1: PAIR(9, 1); break;
    case 2: PAIR(10, 17); break;
    case 3: PAIR(5, 14); break;
    case 4: PAIR(6, 15); break;
    case 5: PAIR(7, 16); break;
#else
    case 0: PAIR(5, 14); break;
    case 1: PAIR(6, 15); break;
    case 2: PAIR(7, 16); break;
    case 3: PAIR(8, 0); break;
    case 4: PAIR(9, 1); break;
    case 5: PAIR(10, 17); break;
#endif
  }
}

// Bounce: one hit on the main output. Its trigger is 12 ms, but at most half the gap to the next
// hit, so fast bounces stay separate triggers.
static inline __attribute__((always_inline)) void bounceHit(uint8_t s, uint16_t gapToNext) {
  volatile Side &d = sides[s];
  uint16_t len = gapToNext >> 1;
  if (len > PULSE_TICKS) len = PULSE_TICKS;
  setOut(OUT_MAIN(s), true);
  d.hitEndAt = TCNT1 + len;
  d.pend |= PEND_HIT_END;
}

// Bounce: the ball hits the ground now and flies for `flight` (1/256 ticks) to the next hit,
// which is due one flight after this hit was due (so interrupt delays do not add up; the due
// time keeps its fraction, so long tails do not drift). If the flight would be shorter than
// MIN, the ball has come to rest: this is the last hit. (Not inlined: keeps the callers lean.)
static void __attribute__((noinline)) bounceLand(uint8_t s, uint32_t flight) {
  volatile Side &d = sides[s];
  uint16_t gapToNext = 0xFFFF;  // last hit: full 12 ms trigger
  if (BELOW_MIN(flight, d.bMin)) {
    d.pend &= ~PEND_FIRE;
  } else {
    d.bFlight = flight;
    gapToNext = flight >> 8;
    uint16_t frac = d.bDueFrac + (uint8_t)flight;
    d.fireAt += gapToNext + (frac >> 8);
    d.bDueFrac = frac;
    d.pend |= PEND_FIRE;
  }
  bounceHit(s, gapToNext);
}

// Bounce: the next flight = this flight x REST / 100 = round(bFlight x bRestQ / 2^24), from
// nine 8 x 8 bit products (both fit 24 bits): no division, no 32-bit multiply.
static inline uint32_t bounceRest(uint8_t s) {
  volatile uint8_t *f = sides[s].bFlightB, *r = sides[s].bRestB;
  uint32_t c = mul8(f[0], r[0]) >> 8;  // column sums of the product, lowest byte first
  c += mul8(f[1], r[0]);
  c += mul8(f[0], r[1]);
  c >>= 8;
  c += mul8(f[2], r[0]);
  c += mul8(f[1], r[1]);
  c += mul8(f[0], r[2]);
  c += 0x80;  // rounding: half of the 2^24 dropped below
  c >>= 8;
  c += mul8(f[2], r[1]);
  c += mul8(f[1], r[2]);
  c += (uint32_t)mul8(f[2], r[2]) << 8;
  return c;
}

// Bounce: the hit Timer1 timed. (Not inlined: keeps service() lean.)
static void __attribute__((noinline)) bounceNext(uint8_t s) {
  bounceLand(s, bounceRest(s));
}

// One step: set the outputs and start the 12 ms pulse.
static void fire(uint8_t s) {
  volatile Side &d = sides[s];
  switch (d.mode) {
    case PROB:
      d.latch = rand100() < d.prob;
      setOut(OUT_MAIN(s), d.latch);
      setOut(OUT_INV(s), !d.latch);
      break;
    case TOGGLE:
      d.latch = !d.latch;
      setOut(OUT_MAIN(s), d.latch);
      setOut(OUT_INV(s), !d.latch);
      break;
    case SWING:
      setOut(OUT_MAIN(s), true);
      setOut(OUT_INV(s), false);
      break;
    case FACTOR:
      if (++d.count >= d.factor) {
        d.count = 0;
        setOut(OUT_MAIN(s), true);
        setOut(OUT_INV(s), false);
      }
      break;
    default:  // (Bounce: below)
      break;
  }
  setOut(OUT_CLK(s), true);
  d.endAt = TCNT1 + PULSE_TICKS;
  d.pend |= PEND_END;
  if (s == 0) redrawA = true;
  if (d.mode == BOUNCE) {  // the clock throws the ball (again, if it still bounces): a hit now
    d.fireAt = TCNT1;
    d.bDueFrac = 128;  // + half a tick: every hit lands on the nearest tick
    bounceLand(s, START_Q8(d.bStart));  // the first flight is START (last call: a tail jump)
  }
}

static void pulseEnd(uint8_t s) {
  volatile Side &d = sides[s];
  setOut(OUT_CLK(s), false);
  if (d.mode != TOGGLE && d.mode != BOUNCE) {  // toggle outputs stay latched, Bounce ends its own
    setOut(OUT_MAIN(s), false);
    setOut(OUT_INV(s), false);
  }
}

// Run what is due and arm the side's compare channel for the next event.
static void service(uint8_t s) {
  volatile Side &d = sides[s];
  for (;;) {
    uint16_t now = TCNT1;
    if ((d.pend & PEND_END) && (int16_t)(now - d.endAt) >= 0) {
      d.pend &= ~PEND_END;
      pulseEnd(s);
      continue;
    }
    if ((d.pend & PEND_HIT_END) && (int16_t)(now - d.hitEndAt) >= 0) {  // before the next hit
      d.pend &= ~PEND_HIT_END;
      setOut(OUT_MAIN(s), false);
      continue;
    }
    if ((d.pend & PEND_FIRE) && (int16_t)(now - d.fireAt) >= 0) {
      d.pend &= ~PEND_FIRE;
      if (d.mode == BOUNCE) bounceNext(s);
      else fire(s);
      continue;
    }
    if (!d.pend) {
      TIMSK1 &= ~(s ? (1 << OCIE1B) : (1 << OCIE1A));
      return;
    }
    uint16_t wait = 0xFFFF;  // ticks to the earliest pending event
    if (d.pend & PEND_END) wait = d.endAt - now;
    if ((d.pend & PEND_FIRE) && (uint16_t)(d.fireAt - now) < wait) wait = d.fireAt - now;
    if ((d.pend & PEND_HIT_END) && (uint16_t)(d.hitEndAt - now) < wait) wait = d.hitEndAt - now;
    uint16_t at = now + wait;
    if (s) {
      OCR1B = at;
      TIFR1 = 1 << OCF1B;
      TIMSK1 |= 1 << OCIE1B;
    } else {
      OCR1A = at;
      TIFR1 = 1 << OCF1A;
      TIMSK1 |= 1 << OCIE1A;
    }
    if ((int16_t)(TCNT1 - at) < 0) return;  // still ahead: the compare match will come
  }
}

ISR(TIMER1_COMPA_vect) { service(0); }
ISR(TIMER1_COMPB_vect) { service(1); }

static void playNow(uint8_t s) {
  sides[s].pend &= ~PEND_FIRE;
  fire(s);
}

// Rising edge on a side's input (interrupt context).
static void edge(uint8_t s) {
  volatile Side &d = sides[s];
  uint32_t now = micros();
  uint32_t period = now - d.lastEdgeUs;
  d.lastEdgeUs = now;
  uint32_t periodUs = 0;  // (only needed here, so it is not kept in sides[])
  if (period < 2000000UL) periodUs = period;  // a pause of 2 s or more starts over
  if (d.mode != SWING) {
    fire(s);  // (PEND_FIRE is only set in Swing and Bounce, where fire() handles it)
  } else if (d.phase == 0) {
    // Swing: the step on the beat plays on the clock; the off-beat step is placed at
    // 2 x swing x period after it (50 % = straight, more = later, less = earlier).
    if (d.pend & PEND_FIRE) playNow(s);  // the last off-beat step is still due: play it first
    playNow(s);
    d.phase = 1;
    d.odd = 0;
    uint32_t ticks = periodUs / T1_US_PER_TICK * d.swing / 50;
    if (periodUs && d.swing != 50 && ticks > 2 && ticks < MAX_DELAY_TICKS) {
      d.fireAt = TCNT1 + (uint16_t)ticks;
      d.pend |= PEND_FIRE;
      d.odd = 1;
    }
  } else {
    d.phase = 0;
    // straight, unknown tempo, or an early off-beat step that is now late: play it now;
    // otherwise Timer1 plays it later (late swing) or already played it (early swing)
    if (!d.odd || ((d.pend & PEND_FIRE) && d.swing < 50)) playNow(s);
  }
  service(s);
}

// Called by libmodulove's pin-change interrupt on every CLK/RST change.
static void onInputChange() {
  uint8_t a = hw.rst.Read(), b = hw.clk.Read();
  if (a && !sides[0].level) edge(0);
  if (b && !sides[1].level) edge(1);
  sides[0].level = a;
  sides[1].level = b;
}

void loop() {
  encoder.update();

  // Throttle OLED update rate; side A steps still refresh it at once (timing no longer
  // depends on the display, the outputs run in the interrupts)
  uint16_t currentTime = millis();
  unsigned long updateInterval = isEncoderIdle ? OLED_UPDATE_INTERVAL_IDLE : OLED_UPDATE_INTERVAL_NORMAL;
  if ((uint16_t)(currentTime - lastUpdateTime) >= updateInterval || redrawA) {
    redrawA = false;
    lastUpdateTime = currentTime;
    updateOLED();
  }

  if (settingsDirty && (uint16_t)(currentTime - settingsChangedAt) >= 3000) saveSettings();
}
