/*
  ARYTHMATIK — Dual Trigger Utility (Fast PCINT + Direct Port I/O)
  Modes per channel: PROB, TOGGLE, SWING, FACTOR, BOUNCE (bouncing-ball)

  Timing model:
    - RST (D11/PB3/PCINT3) clocks Channel A
    - CLK (D13/PB5/PCINT5) clocks Channel B
    - ISR (PCINT0) only snapshots PINB and queues rising edges (pendingA/B)
    - Main loop drains queues, runs per-channel scheduler, and renders OLED (throttled)

  Outputs (fast, direct-port):
    - Index 0..5 map to D5..D10 (PD5, PD6, PD7, PB0, PB1, PB2)
    - Channel A : main=1, inv=0, clk=2
    - Channel B : main=4, inv=3, clk=5

  LED mirroring (direct-port):
    - LED1..LED3 → A0..A2 (PC0..PC2), LED6 → A3 (PC3), LED_CLK → D4 (PD4)
    - LED4/LED5 on D0/D1 are optional (disabled by default)
*/

#include "src/libmodulove/arythmatik.h"
#include <avr/pgmspace.h>
#include <EEPROM.h>
#include "EncoderButton.h"
#include <avr/interrupt.h>
#include <math.h> // for the small UI knob rendering

// -------------------- Pin map (your hardware) --------------------
#define ENCODER_PIN1 2
#define ENCODER_PIN2 3
#define ENCODER_SW_PIN 12
#define CLK_PIN 13      // PB5 / PCINT5  -> Channel B clock
#define RST_PIN 11      // PB3 / PCINT3  -> Channel A clock

// Outputs (index 0..5 → D5..D10)
#define OUT_CH1 5       // idx 0 → D5 / PD5
#define OUT_CH2 6       // idx 1 → D6 / PD6
#define OUT_CH3 7       // idx 2 → D7 / PD7
#define OUT_CH4 8       // idx 3 → D8 / PB0
#define OUT_CH5 9       // idx 4 → D9 / PB1
#define OUT_CH6 10      // idx 5 → D10 / PB2

// LEDs (digital aliases): A0..A3 = 14..17 on PORTC, CLK LED on D4=PD4
#define LED_CH1 14      // PC0 (A0)
#define LED_CH2 15      // PC1 (A1)
#define LED_CH3 16      // PC2 (A2)
#define LED_CH4 0       // PD0 (D0 / RX)
#define LED_CH5 1       // PD1 (D1 / TX)
#define LED_CH6 17      // PC3 (A3)
#define LED_CLK 4       // PD4 (D4)

// -------------------- Build options --------------------
#define MIRROR_SERIAL_LEDS 0     // 1 = also drive D0/D1 (unsafe if Serial used)
constexpr bool LED_ACTIVE_LOW = false;

// OLED throttling
const unsigned long OLED_UPDATE_INTERVAL_NORMAL = 150;
const unsigned long OLED_UPDATE_INTERVAL_IDLE   = 300;

// Pulse width
constexpr uint8_t  TRIG_MS      = 12;    // ms
constexpr int8_t   SWING_MS_MAX = 50;    // ± range for swing
constexpr int16_t  SCALE_1000   = 1000;  // fixed-point scale

// -------------------- Library hardware wrapper --------------------
using namespace modulove;
using namespace arythmatik;
Arythmatik hw;

// -------------------- Encoder --------------------
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
void onEncoderIdle();
void onEncoderClicked();
void onEncoderDoubleClicked();
void onEncoderLongClicked();
void onEncoderPressedRotation();
void onEncoderRotation(EncoderButton& eb);

// -------------------- Modes & channel structs --------------------
enum Menu : uint8_t { PROB, TOGGLE, SWING, FACTOR, BOUNCE, MENU_COUNT };

struct ChannelConfig {
  // UI parameters (fixed-point)
  int16_t probability_1000 = 500;  // 0..1000 (== 0..100%)
  int16_t swing_1000       = 500;  // 0..1000 (500 == centered / no swing)
  uint8_t factor           = 1;    // 1..8
  bool    mute             = false;

  // Mode internals
  uint8_t  triggerCount    = 0;    // divider counter
  bool     latchedState    = false; // for TOGGLE

  // Swing scheduler
  bool     swingPolarity   = false; // alternates early/late
  bool     scheduled       = false;
  uint32_t scheduledAtMs   = 0;

  // Pulse tail
  bool     pulseActive     = false;
  uint32_t pulseStartMs    = 0;

  // Bouncing-ball state
  bool     bb_active       = false; // a burst is in progress
  uint8_t  bb_index        = 0;     // which bounce we’re on
  uint32_t bb_nextDueMs    = 0;     // next event time
  uint16_t bb_currInterval = 0;     // current inter-hit interval

  // Bouncing-ball parameters
  uint16_t bb_start_ms     = 150;   // initial gap (perceived "height")
  uint16_t bb_min_ms       = 18;    // floor interval — stop when reached again
  uint8_t  bb_restitution  = 62;    // 0..100% (energy retention per bounce)

  // UI state for BOUNCE editing
  uint8_t  bb_editField    = 0;     // 0=start_ms, 1=min_ms, 2=restitution
  Menu     currentMenu     = PROB;
};

ChannelConfig channelA;  // driven by RST (D11)
ChannelConfig channelB;  // driven by CLK (D13)

bool isChannelASelected = true;
bool isEncoderIdle = false;
unsigned long lastUpdateTime = 0;

// -------------------- PCINT (PORTB) input engine --------------------
volatile uint8_t prevPINB = 0;
volatile uint8_t pendingA = 0;   // queued rising edges for Channel A (D11)
volatile uint8_t pendingB = 0;   // queued rising edges for Channel B (D13)
static inline void isr_inc_255(volatile uint8_t &x) { if (x != 255) x++; }

ISR(PCINT0_vect) {
  uint8_t pinb = PINB;
  uint8_t changes = pinb ^ prevPINB;

  // PB3 (D11 → Channel A / RST) — rising only
  if ((changes & _BV(3)) && (pinb & _BV(3))) {
    isr_inc_255(pendingA);
  }
  // PB5 (D13 → Channel B / CLK) — rising only
  if ((changes & _BV(5)) && (pinb & _BV(5))) {
    isr_inc_255(pendingB);
  }
  prevPINB = pinb;
}

// -------------------- Fast outputs + LED mirror --------------------
static bool outState[6] = {0,0,0,0,0,0};

static inline void fastOutputInit() {
  // D5..D7 -> DDRD bits 5..7
  DDRD |= _BV(5) | _BV(6) | _BV(7);
  // D8..D10 -> DDRB bits 0..2
  DDRB |= _BV(0) | _BV(1) | _BV(2);
  // Initialize low
  PORTD &= ~(_BV(5) | _BV(6) | _BV(7));
  PORTB &= ~(_BV(0) | _BV(1) | _BV(2));
}
static inline void fastLedInit() {
  // A0..A3 as digital outputs (PC0..PC3)
  DDRC |= _BV(0) | _BV(1) | _BV(2) | _BV(3);
  // D4 as output (PD4)
  DDRD |= _BV(4);
#if MIRROR_SERIAL_LEDS
  DDRD |= _BV(0) | _BV(1); // RX/TX as outputs (unsafe if Serial used)
#endif
  // all off
  PORTC &= ~(_BV(0) | _BV(1) | _BV(2) | _BV(3));
  PORTD &= ~_BV(4);
#if MIRROR_SERIAL_LEDS
  PORTD &= ~(_BV(0) | _BV(1));
#endif
}
inline void writeLedIdx(uint8_t idx, bool on) {
  bool level = LED_ACTIVE_LOW ? !on : on;
  switch (idx) {
    case 0: if (level) PORTC |=  _BV(0); else PORTC &= ~_BV(0); break; // A0
    case 1: if (level) PORTC |=  _BV(1); else PORTC &= ~_BV(1); break; // A1
    case 2: if (level) PORTC |=  _BV(2); else PORTC &= ~_BV(2); break; // A2
    case 3:
#if MIRROR_SERIAL_LEDS
      if (level) PORTD |=  _BV(0); else PORTD &= ~_BV(0);              // D0
#endif
      break;
    case 4:
#if MIRROR_SERIAL_LEDS
      if (level) PORTD |=  _BV(1); else PORTD &= ~_BV(1);              // D1
#endif
      break;
    case 5: if (level) PORTC |=  _BV(3); else PORTC &= ~_BV(3); break; // A3
  }
}
inline void writeClkLed(bool on) {
  bool level = LED_ACTIVE_LOW ? !on : on;
  if (level) PORTD |=  _BV(4);
  else       PORTD &= ~_BV(4);
}
inline void fastWriteOut(uint8_t idx, bool high) {
  if (idx <= 2) {
    uint8_t bit = 5 + idx;              // PD5..PD7
    if (high) PORTD |=  _BV(bit); else PORTD &= ~_BV(bit);
  } else {
    uint8_t bit = idx - 3;              // PB0..PB2
    if (high) PORTB |=  _BV(bit); else PORTB &= ~_BV(bit);
  }
  outState[idx] = high;
  writeLedIdx(idx, high);
  if (idx == 2 || idx == 5) writeClkLed(outState[2] || outState[5]);
}

// -------------------- Helpers: probability & swing --------------------
inline bool randByProb_1000(int16_t p1000) {
  if (p1000 < 0) p1000 = 0;
  if (p1000 > 1000) p1000 = 1000;
  return random(1000) < (uint16_t)p1000; // 0..999 < 0..1000
}
inline int16_t swingOffsetMs(int16_t swing_1000, bool polarity) {
  int16_t centered = (int16_t)(swing_1000 - 500);    // −500..+500
  if (centered < -500) centered = -500;
  if (centered >  +500) centered = +500;
  int16_t base = (int16_t)(( (int32_t)centered * SWING_MS_MAX ) / 500); // −50..+50
  return polarity ? base : (int16_t)(-base);
}

// -------------------- Mode executor (no scheduling here) --------------------
inline void fireImmediate(ChannelConfig& ch, uint8_t mainOut, uint8_t invOut, uint8_t clkOut) {
  switch (ch.currentMenu) {
    case TOGGLE: {
      ch.latchedState = !ch.latchedState;
      fastWriteOut(mainOut, ch.latchedState);
      fastWriteOut(invOut, !ch.latchedState);
    } break;
    case PROB: {
      bool s = randByProb_1000(ch.probability_1000);
      fastWriteOut(mainOut, s);
      fastWriteOut(invOut, !s);
    } break;
    case FACTOR: {
      if (++ch.triggerCount >= ch.factor) {
        ch.triggerCount = 0;
        fastWriteOut(mainOut, true);
        fastWriteOut(invOut, false);
      }
    } break;
    case SWING: {
      // timing handled by scheduler; when invoked here → just fire
      fastWriteOut(mainOut, true);
      fastWriteOut(invOut, false);
    } break;
    case BOUNCE: {
      // Start new burst only if not active; change to "restart" if you prefer
      if (!ch.bb_active) {
        ch.bb_active      = true;
        ch.bb_index       = 0;
        ch.bb_currInterval= ch.bb_start_ms;
        fastWriteOut(mainOut, true);
        fastWriteOut(invOut, false);
        ch.bb_nextDueMs   = millis() + ch.bb_currInterval;
      }
    } break;
    default: break;
  }
  ch.pulseActive  = true;
  ch.pulseStartMs = millis();
  fastWriteOut(clkOut, true);
}

// -------------------- Per-channel scheduler/driver --------------------
inline void serviceChannel(ChannelConfig& ch, bool newEdge,
                           uint8_t mainOut, uint8_t invOut, uint8_t clkOut) {
  const uint32_t now = millis();

  // 1) Late swing note due?
  if (ch.scheduled && (int32_t)(now - ch.scheduledAtMs) >= 0) {
    ch.scheduled = false;
    fireImmediate(ch, mainOut, invOut, clkOut);
  }

  // 2) Bouncing-ball progression
  if (ch.currentMenu == BOUNCE && ch.bb_active) {
    if ((int32_t)(now - ch.bb_nextDueMs) >= 0) {
      // hit
      fastWriteOut(mainOut, true);
      fastWriteOut(invOut, false);
      ch.pulseActive  = true;
      ch.pulseStartMs = now;
      fastWriteOut(clkOut, true);

      // next interval (geometric decay): next = max(min, prev * restitution / 100)
      uint32_t next = (uint32_t)ch.bb_currInterval * (uint32_t)ch.bb_restitution / 100U;
      if (next < ch.bb_min_ms) next = ch.bb_min_ms;
      ch.bb_currInterval = (uint16_t)next;
      ch.bb_index++;

      // stop after we hit the floor twice (optional: tweak feel)
      if (ch.bb_currInterval == ch.bb_min_ms && ch.bb_index > 1) {
        ch.bb_active = false;
      } else {
        ch.bb_nextDueMs = now + ch.bb_currInterval;
      }
    }
  }

  // 3) New input edge
  if (newEdge && !ch.mute) {
    if (ch.currentMenu == SWING) {
      const int16_t offset = swingOffsetMs(ch.swing_1000, ch.swingPolarity);
      ch.swingPolarity = !ch.swingPolarity;
      if (offset <= 0) {
        fireImmediate(ch, mainOut, invOut, clkOut); // early/straight
      } else {
        ch.scheduled     = true;
        ch.scheduledAtMs = now + (uint16_t)offset;  // late
      }
    } else {
      fireImmediate(ch, mainOut, invOut, clkOut);
    }
  }

  // 4) Auto-off pulse tail
  if (ch.pulseActive && (now - ch.pulseStartMs) >= TRIG_MS) {
    fastWriteOut(clkOut, false);
    if (ch.currentMenu != TOGGLE) {
      fastWriteOut(mainOut, false);
      fastWriteOut(invOut, false);
    }
    ch.pulseActive = false;
  }
}

// -------------------- UI helpers (OLED) --------------------
static inline void drawDottedLine(int x, int y, int length, int spacing) {
  for (int i = 0; i < length; i += spacing) hw.display.drawPixel(x, y + i, WHITE);
}
static inline void drawTree(int x, int y, float probability) {
  hw.display.drawLine(x, y, x, y - 10, WHITE);
  hw.display.drawLine(x, y - 5, x - 5, y - 15, WHITE);
  hw.display.drawLine(x, y - 5, x + 5, y - 15, WHITE);
  if (probability <= 0.5f) {
    hw.display.fillCircle(x - 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x - 5, y - 15, 4, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 2, WHITE);
  } else {
    hw.display.drawCircle(x - 5, y - 15, 2, WHITE);
    hw.display.fillCircle(x + 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 4, WHITE);
  }
}
static inline void drawKnob(int x, int y, float norm01) {
  if (norm01 < 0) norm01 = 0; if (norm01 > 1) norm01 = 1;
  hw.display.drawCircle(x, y, 5, WHITE);
  hw.display.drawCircle(x, y, 8, WHITE);
  float angle = norm01 * 2.0f * PI - PI / 2.0f;
  int x1 = x + (int)(10.0f * cosf(angle));
  int y1 = y + (int)(10.0f * sinf(angle));
  hw.display.drawLine(x, y, x1, y1, WHITE);
}
static inline const char* modeName(Menu m) {
  switch (m) {
    case PROB:   return "Buds";
    case TOGGLE: return "Toggle";
    case SWING:  return "Swing";
    case FACTOR: return "Factor";
    case BOUNCE: return "Bounce";
    default:     return "?";
  }
}

void drawTopBar() {
  if (isChannelASelected) {
    hw.display.setCursor(16, 1); hw.display.print(modeName(channelA.currentMenu));
    hw.display.fillRoundRect(10, 0, 44, 12, 3, INVERSE);
    hw.display.setCursor(80, 1); hw.display.print(modeName(channelB.currentMenu));
  } else {
    hw.display.setCursor(80, 1); hw.display.print(modeName(channelB.currentMenu));
    hw.display.fillRoundRect(74, 0, 44, 12, 3, INVERSE);
    hw.display.setCursor(16, 1); hw.display.print(modeName(channelA.currentMenu));
  }
  drawDottedLine(64, 0, 64, 2);
}

void drawBottomBar() {
  // A-side
  if (channelA.currentMenu == PROB) {
    drawDottedLine(30, 54, 10, 2);
    int barLengthA = (int)((channelA.probability_1000 - 500) * 0.04f); // ±20 px
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    if (barLengthA > 0) hw.display.fillRoundRect(30, 58, barLengthA, 4, 2, WHITE);
    else                hw.display.fillRoundRect(30 + barLengthA, 58, -barLengthA, 4, 2, WHITE);
  } else if (channelA.currentMenu == SWING) {
    int barLengthA = (int)((channelA.swing_1000 - 500) * 0.08f); // ±40 px
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(30 + (barLengthA<0?barLengthA:0), 58, abs(barLengthA), 4, 2, WHITE);
  } else if (channelA.currentMenu == FACTOR) {
    hw.display.setCursor(24, 30); hw.display.setTextSize(2);
    hw.display.print(channelA.factor); hw.display.setTextSize(1);
  } else if (channelA.currentMenu == BOUNCE) {
    hw.display.setCursor(10, 54);
    hw.display.print("S:");
    hw.display.print(channelA.bb_start_ms);
    hw.display.print("  M:");
    hw.display.print(channelA.bb_min_ms);
    hw.display.print("  R:");
    hw.display.print(channelA.bb_restitution);
    hw.display.print("%");
  }

  // B-side
  if (channelB.currentMenu == PROB) {
    drawDottedLine(94, 54, 10, 4);
    int barLengthB = (int)((channelB.probability_1000 - 500) * 0.04f);
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    if (barLengthB > 0) hw.display.fillRoundRect(94, 58, barLengthB, 4, 2, WHITE);
    else                hw.display.fillRoundRect(94 + barLengthB, 58, -barLengthB, 4, 2, WHITE);
  } else if (channelB.currentMenu == SWING) {
    int barLengthB = (int)((channelB.swing_1000 - 500) * 0.08f);
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(94 + (barLengthB<0?barLengthB:0), 58, abs(barLengthB), 4, 2, WHITE);
  } else if (channelB.currentMenu == FACTOR) {
    hw.display.setCursor(94, 30); hw.display.setTextSize(2);
    hw.display.print(channelB.factor); hw.display.setTextSize(1);
  } else if (channelB.currentMenu == BOUNCE) {
    hw.display.setCursor(74, 54);
    hw.display.print("S:");
    hw.display.print(channelB.bb_start_ms);
    hw.display.print("  M:");
    hw.display.print(channelB.bb_min_ms);
    hw.display.print("  R:");
    hw.display.print(channelB.bb_restitution);
    hw.display.print("%");
  }
}

void drawProbUI(ChannelConfig& ch, int x) {
  hw.display.setCursor(x - 8, 16);
  hw.display.print((ch.probability_1000 + 5) / 10); // %
  hw.display.print("% ");
  drawTree(x, 48, ch.probability_1000 / 1000.0f);
}
void drawSwingUI(ChannelConfig& ch, int x) {
  hw.display.setCursor(x - 10, 16);
  hw.display.print((ch.swing_1000 + 5) / 10);
  hw.display.print("%");
  float knob = (ch.swing_1000 - 500) / 400.0f; // approximately 0..1 for visual
  if (knob < 0) knob = 0; if (knob > 1) knob = 1;
  drawKnob(x, 38, knob);
}
void drawBounceUI(ChannelConfig& ch, int x) {
  // Three small fields with selection underline: Start, Min, Restitution
  hw.display.setCursor(x - 22, 14); hw.display.print("Start");
  hw.display.setCursor(x - 22, 24); hw.display.print("Min");
  hw.display.setCursor(x - 22, 34); hw.display.print("Rest");

  // values
  hw.display.setCursor(x + 8, 14); hw.display.print(ch.bb_start_ms);
  hw.display.setCursor(x + 8, 24); hw.display.print(ch.bb_min_ms);
  hw.display.setCursor(x + 8, 34); hw.display.print(ch.bb_restitution); hw.display.print("%");

  // underline current field
  int uy = (ch.bb_editField == 0) ? 22 : (ch.bb_editField == 1) ? 32 : 42;
  hw.display.drawLine(x - 22, uy, x + 36, uy, WHITE);
}

void updateOLED() {
  hw.display.clearDisplay();
  drawTopBar();

  // A-side center UI
  if (channelA.currentMenu == PROB)      drawProbUI(channelA, 32);
  else if (channelA.currentMenu == SWING)drawSwingUI(channelA, 32);
  else if (channelA.currentMenu == BOUNCE) drawBounceUI(channelA, 32);

  // B-side center UI
  if (channelB.currentMenu == PROB)      drawProbUI(channelB, 96);
  else if (channelB.currentMenu == SWING)drawSwingUI(channelB, 96);
  else if (channelB.currentMenu == BOUNCE) drawBounceUI(channelB, 96);

  drawBottomBar();
  hw.display.display();
}

// -------------------- EEPROM (optional – small footprint) --------------------
struct Persist {
  uint8_t version = 1;
  ChannelConfig A;
  ChannelConfig B;
};
void saveState() {
  Persist p; p.A = channelA; p.B = channelB;
  EEPROM.put(0, p);
}
void loadState() {
  Persist p; EEPROM.get(0, p);
  if (p.version == 1) { channelA = p.A; channelB = p.B; }
}

// -------------------- Encoder handlers --------------------
void onEncoderIdle() { isEncoderIdle = true; }
void onEncoderClicked() {
  isEncoderIdle = false;
  isChannelASelected = !isChannelASelected; // swap focus
}
void onEncoderDoubleClicked() {
  isEncoderIdle = false;
  // Cycle mode on focused channel
  ChannelConfig& C = isChannelASelected ? channelA : channelB;
  C.currentMenu = static_cast<Menu>((C.currentMenu + 1) % MENU_COUNT);
}
void onEncoderLongClicked() {
  isEncoderIdle = false;
  // Mute toggle on focused channel
  ChannelConfig& C = isChannelASelected ? channelA : channelB;
  C.mute = !C.mute;
}
void onEncoderPressedRotation() {
  isEncoderIdle = false;
  // In Bounce mode, press+rotate flips field; elsewhere cycles mode
  ChannelConfig& C = isChannelASelected ? channelA : channelB;
  if (C.currentMenu == BOUNCE) {
    C.bb_editField = (C.bb_editField + 1) % 3;
  } else {
    C.currentMenu = static_cast<Menu>((C.currentMenu + 1) % MENU_COUNT);
  }
}
void onEncoderRotation(EncoderButton& eb) {
  isEncoderIdle = false;
  int inc = encoder.increment();
  if (!inc) return;
  int accel = inc * inc; if (inc < 0) accel = -accel;

  ChannelConfig& C = isChannelASelected ? channelA : channelB;
  switch (C.currentMenu) {
    case PROB: {
      int v = C.probability_1000 + accel * 20; // ~2% per detent^2
      if (v < 0) v = 0; if (v > 1000) v = 1000;
      C.probability_1000 = (int16_t)v;
    } break;
    case TOGGLE: {
      C.latchedState = !C.latchedState; // visual immediate toggle
    } break;
    case SWING: {
      int v = C.swing_1000 + accel * 10; // finer steps
      if (v < 0) v = 0; if (v > 1000) v = 1000;
      C.swing_1000 = (int16_t)v;
    } break;
    case FACTOR: {
      int v = C.factor + accel;
      if (v < 1) v = 1; if (v > 8) v = 8;
      C.factor = (uint8_t)v;
    } break;
    case BOUNCE: {
      if (C.bb_editField == 0) {
        int v = (int)C.bb_start_ms + accel * 2;
        if (v < 10) v = 10; if (v > 500) v = 500;
        C.bb_start_ms = (uint16_t)v;
      } else if (C.bb_editField == 1) {
        int v = (int)C.bb_min_ms + accel;
        if (v < 5) v = 5; if (v > 80) v = 80;
        C.bb_min_ms = (uint16_t)v;
      } else {
        int v = (int)C.bb_restitution + accel;
        if (v < 30) v = 30; if (v > 95) v = 95;
        C.bb_restitution = (uint8_t)v;
      }
    } break;
    default: break;
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  hw.Init(); // OLED, etc.

  // Fast outputs + LEDs
  fastOutputInit();
  fastLedInit();

  // Encoder
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  encoder.setDebounceInterval(5);
  encoder.setMultiClickInterval(70);
  encoder.setLongClickDuration(400);
  encoder.setRateLimit(10);
  encoder.setIdleTimeout(1000);
  encoder.setIdleHandler(onEncoderIdle);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setDoubleClickHandler(onEncoderDoubleClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);

  // Inputs for PCINT (enable pullups if your front-end can float)
  pinMode(RST_PIN, INPUT);
  pinMode(CLK_PIN, INPUT);

  // PCINT on PB3(D11) & PB5(D13)
  prevPINB = PINB;
  PCICR  |= _BV(PCIE0);                       // enable PCINT PORTB
  PCMSK0 |= _BV(PCINT3) | _BV(PCINT5);        // PB3, PB5
  sei();

  randomSeed(analogRead(0));
  loadState(); // enable if you want persisted params
}

void loop() {
  // 1) Drain ISR queues
  uint8_t nA = 0, nB = 0;
  cli(); nA = pendingA; pendingA = 0; nB = pendingB; pendingB = 0; sei();

  // 2) Process edges + advance schedulers
  for (uint8_t i = 0; i < nA; ++i) serviceChannel(channelA, true, /*main*/1, /*inv*/0, /*clk*/2);
  for (uint8_t i = 0; i < nB; ++i) serviceChannel(channelB, true, /*main*/4, /*inv*/3, /*clk*/5);

  serviceChannel(channelA, false, 1, 0, 2);
  serviceChannel(channelB, false, 4, 3, 5);

  // 3) Encoder / UI
  encoder.update();

  // 4) OLED throttling — interval or when edges happened
  unsigned long now = millis();
  bool hadEdge = (nA | nB) != 0;
  unsigned long interval = isChannelASelected && isEncoderIdle ? OLED_UPDATE_INTERVAL_IDLE : OLED_UPDATE_INTERVAL_NORMAL;
  if (hadEdge || (now - lastUpdateTime >= interval)) {
    lastUpdateTime = now;
    updateOLED();
  }
}
