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
  MENU_COUNT  // Total number of menu items
};

// Declare A-RYTH-MATIK hardware variable.
Arythmatik hw;

// The encoder is libmodulove's hw.eb (pins in the ENCODER_REVERSED order of this sketch):
// a second EncoderButton on the same pins cost ~80 bytes of RAM and handled every pulse twice.
#define encoder hw.eb

// channel configuration Struct (what the UI edits; the interrupts work on a copy in sides[])
struct ChannelConfig {
  float probability = 0.5;
  bool state = false;       // toggle state / last coin flip, copied from the interrupts for the display
  float swingAmount = 0.5;  // 0.5 means no swing, range 0.0 to 0.7
  Menu currentMenu = PROB;  // current menu mode
  int factor = 1;  // clock divider factor
};

// create configuration for both channels
ChannelConfig channelA;
ChannelConfig channelB;

bool isChannelASelected = true;
bool isEncoderIdle = false;

unsigned long lastUpdateTime = 0;

// ---------- trigger engine ----------
// Side A = RST input, side B = CLK input. Both inputs are read by the pin-change interrupt and
// each side owns one Timer1 compare channel (side A: OCR1A, side B: OCR1B) for its 12 ms pulse
// end and its swung step. So the two sides run at any tempo, independent of each other and of
// the display (before, a display refresh blocked both sides for 25-47 ms).
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
#define PEND_END 1
#define PEND_FIRE 2

struct Side {
  uint8_t mode, prob, swing, factor;  // copied from the UI: prob 0..100, swing 0..70 (%)
  uint8_t count;                      // factor counter
  uint8_t phase;                      // swing: 0 = next clock is on the beat, 1 = off-beat
  uint8_t odd;                        // swing: the off-beat step is timed by Timer1
  uint8_t pend;                       // PEND_END / PEND_FIRE
  bool latch;                         // toggle state / last coin flip
  uint8_t level;                      // last input level
  uint16_t endAt, fireAt;             // Timer1 ticks
  uint32_t lastEdgeUs, periodUs;
  uint8_t outMain, outInv, outClk;
};
volatile Side sides[2];
volatile bool redrawA;
uint16_t rng16 = 1;

// settings in EEPROM (saved 3 s after the last change)
struct SavedSettings {
  char magic[4];
  uint8_t menu[2], prob[2], swing[2], factor[2];
};
bool settingsDirty = false;
unsigned long settingsChangedAt = 0;

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
  sides[0].outMain = 1;
  sides[0].outInv = 0;
  sides[0].outClk = 2;
  sides[1].outMain = 4;
  sides[1].outInv = 3;
  sides[1].outClk = 5;
  applySettings();
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
  encoder.setLongClickHandler(onEncoderLongClicked);  // long click to switch Mode
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);
}

// abstract tree UI
void drawTree(int x, int y, float probability) {
  // main branch
  hw.display.drawLine(x, y, x, y - 10, WHITE);

  // primary twigs
  hw.display.drawLine(x, y - 5, x - 5, y - 15, WHITE);
  hw.display.drawLine(x, y - 5, x + 5, y - 15, WHITE);

  // two dots based on probability
  if (probability <= 0.5) {
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

  if (channelA.currentMenu == PROB) {
    // taller center line for 50% in prob mode
    drawDottedLine(30, 54, 10, 2);  // center line 
    // bar for Channel A (centered at 50% and fill left or right)
    int barLengthA = (channelA.probability - 0.5) * 40;
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    if (barLengthA > 0) {
      hw.display.fillRoundRect(30, 58, barLengthA, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(30 + barLengthA, 58, -barLengthA, 4, 2, WHITE);
    }
  } else if (channelA.currentMenu == TOGGLE) {
    // Toggle mode indicator for Channel A
    hw.display.fillCircle(30, 25, 3, channelA.state ? WHITE : BLACK);
    hw.display.drawCircle(30, 25, 5, WHITE);
    hw.display.fillCircle(30, 45, 3, channelA.state ? BLACK : WHITE);
    hw.display.drawCircle(30, 45, 5, WHITE);
    hw.display.setCursor(10, 52);
    //hw.display.print(channelA.state ? "ON " : "OFF");
  } else if (channelA.currentMenu == SWING) {
    // Swing mode indicator for Channel A
    int barLengthA = (channelA.swingAmount - 0.5) * 200;
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(10, 58, barLengthA, 4, 2, WHITE);
  } else if (channelA.currentMenu == FACTOR) {
    // Factor mode indicator for Channel A
    hw.display.setCursor(24, 30);
    hw.display.setTextSize(2);
    hw.display.print(channelA.factor);
    hw.display.setTextSize(1);
  }

  if (channelB.currentMenu == PROB) {
    drawDottedLine(94, 54, 10, 4);  // center line
    // bar for Channel B (centered at 50% and fill left or right)
    int barLengthB = (channelB.probability - 0.5) * 40;
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    if (barLengthB > 0) {
      hw.display.fillRoundRect(94, 58, barLengthB, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(94 + barLengthB, 58, -barLengthB, 4, 2, WHITE);
    }
  } else if (channelB.currentMenu == TOGGLE) {
    // Toggle mode indicator for Channel B
    hw.display.fillCircle(96, 25, 3, channelB.state ? WHITE : BLACK);
    hw.display.drawCircle(96, 25, 5, WHITE);
    hw.display.fillCircle(96, 45, 3, channelB.state ? BLACK : WHITE);
    hw.display.drawCircle(96, 45, 5, WHITE);
    hw.display.setCursor(74, 52);
    //hw.display.print(channelB.state ? "ON " : "OFF");
  } else if (channelB.currentMenu == SWING) {
    // Swing mode indicator for Channel B
    int barLengthB = (channelB.swingAmount - 0.5) * 200;
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(74, 58, barLengthB, 4, 2, WHITE);
  } else if (channelB.currentMenu == FACTOR) {
    // Factor mode indicator for Channel B
    hw.display.setCursor(94, 30);
    hw.display.setTextSize(2);
    hw.display.print(channelB.factor);
    hw.display.setTextSize(1);
  }
}

// top bar menu
void drawTopBar() {
  hw.display.setCursor(0, 0);

  const char* modeStrA = channelA.currentMenu == PROB ? "Buds" : (channelA.currentMenu == TOGGLE ? "Toggle" : (channelA.currentMenu == SWING ? "Swing" : "Factor"));
  const char* modeStrB = channelB.currentMenu == PROB ? "Buds" : (channelB.currentMenu == TOGGLE ? "Toggle" : (channelB.currentMenu == SWING ? "Swing" : "Factor"));

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
void drawSwingUI(ChannelConfig& channel, int x) {
  hw.display.setCursor(x - 10, 16);
  hw.display.print(channel.swingAmount * 100, 0);
  hw.display.print("%");

  // knob for swing amount
  drawKnob(x, 38, (channel.swingAmount - 0.5) / 0.2);  // Normalized to 0-1 range
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

void updateOLED() {
  hw.display.clearDisplay();
  drawTopBar();
  drawDottedLine(64, 0, 64, 2);  // Centerline

  if (channelA.currentMenu == PROB) {
    hw.display.setCursor(24, 16);
    hw.display.print(channelA.probability * 100, 0);
    hw.display.print("% ");
    drawTree(32, 48, channelA.probability);
  } else if (channelA.currentMenu == SWING) {
    drawSwingUI(channelA, 32);
  } 

  if (channelB.currentMenu == PROB) {
    hw.display.setCursor(88, 16);
    hw.display.print(channelB.probability * 100, 0);
    hw.display.print("% ");
    drawTree(96, 48, channelB.probability);
  } else if (channelB.currentMenu == SWING) {
    drawSwingUI(channelB, 96);
  } 

  drawBottomBar();
  i2cGuardShow(hw.display);
}

// ---------- settings ----------
void settingsChanged() {
  applySettings();
  settingsDirty = true;
  settingsChangedAt = millis();
}

void saveSettings() {
  SavedSettings st = { { 'B', 'U', 'D', '1' } };
  ChannelConfig *c[2] = { &channelA, &channelB };
  for (uint8_t i = 0; i < 2; i++) {
    st.menu[i] = c[i]->currentMenu;
    st.prob[i] = (uint8_t)(c[i]->probability * 100 + 0.5);
    st.swing[i] = (uint8_t)(c[i]->swingAmount * 100 + 0.5);
    st.factor[i] = c[i]->factor;
  }
  EEPROM.put(0, st);  // one block (an LGT8F stalls ~46 ms per EEPROM write)
  settingsDirty = false;
}

void loadSettings() {
  SavedSettings st;
  EEPROM.get(0, st);
  if (st.magic[0] != 'B' || st.magic[1] != 'U' || st.magic[2] != 'D' || st.magic[3] != '1') return;
  ChannelConfig *c[2] = { &channelA, &channelB };
  for (uint8_t i = 0; i < 2; i++) {
    c[i]->currentMenu = static_cast<Menu>(st.menu[i] % MENU_COUNT);
    c[i]->probability = constrain(st.prob[i], 0, 100) / 100.0;
    c[i]->swingAmount = constrain(st.swing[i], 0, 70) / 100.0;
    c[i]->factor = constrain(st.factor[i], 1, 8);
  }
}

// Copy the UI settings to the interrupt side.
void applySettings() {
  ChannelConfig *c[2] = { &channelA, &channelB };
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t mode = c[i]->currentMenu;
    uint8_t prob = (uint8_t)(c[i]->probability * 100 + 0.5);
    uint8_t swing = (uint8_t)(c[i]->swingAmount * 100 + 0.5);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      volatile Side &d = sides[i];
      if (d.mode != mode) {  // new mode: start its pattern fresh
        d.phase = 0;
        d.count = 0;
        d.odd = 0;
        d.pend &= ~PEND_FIRE;
      }
      d.mode = mode;
      d.prob = prob;
      d.swing = swing;
      d.factor = c[i]->factor;
    }
  }
}

// Toggle state flipped by hand (long click, or turning in Toggle mode).
void flipLatch(uint8_t s) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    volatile Side &d = sides[s];
    d.latch = !d.latch;
    if (d.mode == TOGGLE) {
      setOut(d.outMain, d.latch);
      setOut(d.outInv, !d.latch);
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
  flipLatch(isChannelASelected ? 0 : 1);  // flip the toggle state of the selected side
}

void onEncoderPressedRotation(EncoderButton &eb) {
  isEncoderIdle = false;
  if (isChannelASelected) {
    channelA.currentMenu = static_cast<Menu>((channelA.currentMenu + 1) % MENU_COUNT);
  } else {
    channelB.currentMenu = static_cast<Menu>((channelB.currentMenu + 1) % MENU_COUNT);
  }
  settingsChanged();
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

  if (isChannelASelected) {
    switch (channelA.currentMenu) {
      case PROB:
        channelA.probability = constrain(channelA.probability + acceleratedIncrement * 0.02, 0.0, 1.0);
        break;
      case TOGGLE:
        flipLatch(0);
        break;
      case SWING:
        channelA.swingAmount = constrain(channelA.swingAmount + acceleratedIncrement * 0.01, 0.0, 0.7);
        break;
      case FACTOR:
        channelA.factor = constrain(channelA.factor + acceleratedIncrement, 1, 8);
        break;
      default:
        break;
    }
  } else {
    switch (channelB.currentMenu) {
      case PROB:
        channelB.probability = constrain(channelB.probability + acceleratedIncrement * 0.02, 0.0, 1.0);
        break;
      case TOGGLE:
        flipLatch(1);
        break;
      case SWING:
        channelB.swingAmount = constrain(channelB.swingAmount + acceleratedIncrement * 0.01, 0.0, 0.7);
        break;
      case FACTOR:
        channelB.factor = constrain(channelB.factor + acceleratedIncrement, 1, 8);
        break;
      default:
        break;
    }
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

// One step: set the outputs and start the 12 ms pulse.
static void fire(uint8_t s) {
  volatile Side &d = sides[s];
  switch (d.mode) {
    case PROB:
      d.latch = rand100() < d.prob;
      setOut(d.outMain, d.latch);
      setOut(d.outInv, !d.latch);
      break;
    case TOGGLE:
      d.latch = !d.latch;
      setOut(d.outMain, d.latch);
      setOut(d.outInv, !d.latch);
      break;
    case SWING:
      setOut(d.outMain, true);
      setOut(d.outInv, false);
      break;
    case FACTOR:
      if (++d.count >= d.factor) {
        d.count = 0;
        setOut(d.outMain, true);
        setOut(d.outInv, false);
      }
      break;
    default:
      break;
  }
  setOut(d.outClk, true);
  d.endAt = TCNT1 + PULSE_TICKS;
  d.pend |= PEND_END;
  if (s == 0) redrawA = true;
}

static void pulseEnd(uint8_t s) {
  volatile Side &d = sides[s];
  setOut(d.outClk, false);
  if (d.mode != TOGGLE) {  // toggle outputs stay latched
    setOut(d.outMain, false);
    setOut(d.outInv, false);
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
    if ((d.pend & PEND_FIRE) && (int16_t)(now - d.fireAt) >= 0) {
      d.pend &= ~PEND_FIRE;
      fire(s);
      continue;
    }
    if (!d.pend) {
      TIMSK1 &= ~(s ? (1 << OCIE1B) : (1 << OCIE1A));
      return;
    }
    uint16_t at;
    if ((d.pend & (PEND_END | PEND_FIRE)) == (PEND_END | PEND_FIRE))
      at = (uint16_t)(d.fireAt - now) < (uint16_t)(d.endAt - now) ? d.fireAt : d.endAt;
    else
      at = (d.pend & PEND_FIRE) ? d.fireAt : d.endAt;
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
  if (period < 2000000UL) d.periodUs = period;  // a pause of 2 s or more starts over
  else d.periodUs = 0;
  if (d.mode != SWING) {
    playNow(s);
  } else if (d.phase == 0) {
    // Swing: the step on the beat plays on the clock; the off-beat step is placed at
    // 2 x swing x period after it (50 % = straight, more = later, less = earlier).
    if (d.pend & PEND_FIRE) playNow(s);  // the last off-beat step is still due: play it first
    playNow(s);
    d.phase = 1;
    d.odd = 0;
    uint32_t ticks = d.periodUs / T1_US_PER_TICK * d.swing / 50;
    if (d.periodUs && d.swing != 50 && ticks > 2 && ticks < MAX_DELAY_TICKS) {
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

  channelA.state = sides[0].latch;  // for the display
  channelB.state = sides[1].latch;

  // Throttle OLED update rate; side A steps still refresh it at once (timing no longer
  // depends on the display, the outputs run in the interrupts)
  unsigned long currentTime = millis();
  unsigned long updateInterval = isEncoderIdle ? OLED_UPDATE_INTERVAL_IDLE : OLED_UPDATE_INTERVAL_NORMAL;
  if (currentTime - lastUpdateTime >= updateInterval || redrawA) {
    redrawA = false;
    lastUpdateTime = currentTime;
    updateOLED();
  }

  if (settingsDirty && currentTime - settingsChangedAt >= 3000) saveSettings();
}
