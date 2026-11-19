#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM) - must be the first include
/**
 * ARYTHMATIK Ping - physics rhythm generator for the A-RYTH-MATIK (libModulove).
 * Based on https://github.com/shveytank/Arduino_Pong_Game
 *
 * A playable ping-pong game with gravity and bounce: 1-3 balls with different speeds fly
 * through the court, every hit fires a 10 ms trigger.
 *   OUT1 left wall (behind the CPU paddle)   OUT4 CPU paddle
 *   OUT2 right wall (behind your paddle)     OUT5 your paddle
 *   OUT3 top or bottom wall                  OUT6 any hit
 *
 * Controls
 *   Turn               move your paddle (right side)
 *   Click              show the selected parameter; click again for the next one
 *   Press + turn       change the selected parameter
 *   Long press (0.8 s) new serve (new random pattern)
 *   CLK in             with a clock (a pulse within the last 2 s) the game runs
 *                      STEPS/CLK physics steps per clock pulse, so the rhythms lock to
 *                      the tempo; without a clock it runs 128 steps per second.
 *                      The court's centre line is solid while a clock is followed.
 *   RST in             restart the current serve (same pattern again; with a clock it
 *                      starts on the next clock pulse, or on the pulse that came up to
 *                      5 ms before the reset)
 *
 * Parameters (saved 3 s after the last change)
 *   GRAVITY   0-9      0 = straight lines (the classic game), more = arcs
 *   BOUNCE    50-100%  height kept per top/bottom hit; below 100% the balls die down
 *                      and are launched again when they come to rest
 *   BALLS     1-3      ball 2 flies 1.5x, ball 3 0.75x the speed of ball 1
 *   SPEED     25-300%  horizontal ball speed, 100% = classic
 *   STEPS/CLK 1-96     physics steps per clock pulse (free running: the internal
 *                      clock for QUANTIZE ticks every STEPS/CLK steps)
 *   QUANTIZE  OFF/ON   ON = hits wait for the next clock pulse (or internal clock)
 * The classic game: GRAVITY 0, BOUNCE 100%, BALLS 1, SPEED 100%.
 *
 * Timing: physics steps run in the Timer1 compare A interrupt (or on the clock edge in
 * the pin change interrupt), trigger ends in the compare B interrupt, so the game speed
 * and the 10 ms pulses do not depend on the display. The main loop only draws.
 *
 * EEPROM: address 0, 7 bytes, one EEPROM.put(): magic 0xB5, then the menu positions of
 * GRAVITY (0-9), BOUNCE (0-10 = 50-100%), BALLS (0-2 = 1-3), SPEED (0-11 = 25-300%),
 * STEPS/CLK (0-12, index into STEPS_TABLE), QUANTIZE (0-1).
 */
#include <util/atomic.h>
#include <EncoderButton.h>
#include "src/libmodulove/arythmatik.h"

using namespace modulove;
using namespace arythmatik;

#if (F_CPU % 1000000UL) != 0
#error "Timer values are derived from F_CPU in whole MHz"
#endif

// ---------- types (before the first function: the Arduino IDE puts prototypes there) ----------
enum Param : uint8_t { P_GRAVITY, P_BOUNCE, P_BALLS, P_SPEED, P_STEPS, P_QUANT, P_COUNT };
const uint8_t OVERLAY_SERVE = P_COUNT;  // overlay item for the "NEW SERVE" message

struct Settings {
  uint8_t magic;
  uint8_t p[P_COUNT];  // menu position of each parameter, see paramText()
};

struct Ball {
  int16_t x, y;    // top left pixel of the 2x2 ball in 1/256 px; x relative to the court centre
  int16_t vx, vy;  // 1/256 px per physics step
};

// ---------- parameters ----------
const uint8_t SETTINGS_MAGIC = 0xB5;
const int SETTINGS_ADDR = 0;
const uint16_t SAVE_DELAY_MS = 3000;
const uint16_t OVERLAY_MS = 2000;

const uint8_t PARAM_MAX[P_COUNT] PROGMEM = { 9, 10, 2, 11, 12, 1 };
const uint8_t PARAM_DEFAULT[P_COUNT] PROGMEM = { 3, 8, 1, 3, 7, 0 };  // gravity 3, 90%, 2 balls, 100%, 16 steps, off
const char NAME_GRAVITY[] PROGMEM = "GRAVITY";
const char NAME_BOUNCE[] PROGMEM = "BOUNCE";
const char NAME_BALLS[] PROGMEM = "BALLS";
const char NAME_SPEED[] PROGMEM = "SPEED";
const char NAME_STEPS[] PROGMEM = "STEPS/CLK";
const char NAME_QUANT[] PROGMEM = "QUANTIZE";
const char *const PARAM_NAMES[P_COUNT] PROGMEM = { NAME_GRAVITY, NAME_BOUNCE, NAME_BALLS, NAME_SPEED, NAME_STEPS, NAME_QUANT };

// Gravity in 1/256 px per step^2 (a fall over the court height takes 1.4 s .. 0.3 s at 128 steps/s)
const uint8_t GRAVITY_TABLE[10] PROGMEM = { 0, 1, 2, 3, 4, 6, 8, 11, 15, 20 };
// Kick for a ball at rest: sqrt(2 * g * 56 px * 256), it rises about 56 px
const uint16_t LAUNCH_TABLE[10] PROGMEM = { 0, 169, 239, 293, 339, 415, 479, 562, 656, 757 };
const uint8_t STEPS_TABLE[13] PROGMEM = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96 };

// ---------- timing (Timer1 free-running at F_CPU / 256: 16 us at 16 MHz, 8 us at 32 MHz) ----------
#define T1_TICKS(us) ((uint32_t)(us) * (F_CPU / 1000000UL) / 256)
const uint16_t FREE_STEP_TICKS = T1_TICKS(7813);  // 128 physics steps per second without a clock
const uint16_t PULSE_TICKS = T1_TICKS(10000);     // 10 ms triggers
const uint16_t MIN_STEP_TICKS = T1_TICKS(1000);   // at most 1000 steps per second
const uint32_t CLOCK_TIMEOUT_US = 2000000;        // no clock pulse for 2 s = free running
const uint16_t CLOCK_MIN_US = 1000;               // clock edges closer than 1 ms are ignored
const uint16_t RESET_WINDOW_US = 5000;            // a reset up to 5 ms after a clock belongs to that clock
const uint8_t MAX_CATCH_UP = 2;                   // missed steps run on the next clock edge

// ---------- court (pixels; border at x 0/127 and y 0/63) ----------
#define FX(px) ((int16_t)((px)*256))  // pixels -> 1/256 px
#define XR(px) FX((px)-64)            // screen x -> ball x (relative to the centre, keeps int16 in range)
const uint8_t MAX_BALLS = 3;
const uint8_t PADDLE_HEIGHT = 24;
const uint8_t CPU_X = 12;     // paddle columns
const uint8_t PLAYER_X = 115;
const uint8_t PADDLE_MIN = 1;
const uint8_t PADDLE_MAX = 63 - PADDLE_HEIGHT;
const int16_t BALL_X_MIN = XR(1);  // top left of the 2x2 ball inside the border
const int16_t BALL_X_MAX = XR(125);
const int16_t BALL_Y_MIN = FX(1);
const int16_t BALL_Y_MAX = FX(61);
const int16_t CPU_FACE = XR(CPU_X + 1);        // ball x where it touches a paddle
const int16_t PLAYER_FACE = XR(PLAYER_X - 2);
const int16_t V_MAX = FX(4);  // speed limit 4 px per step
const int8_t ENGLISH = 6;     // vertical speed added per pixel the ball hits off the paddle centre

const uint8_t CLK_BIT = 1 << 5;  // D13 = PB5
const uint8_t RST_BIT = 1 << 3;  // D11 = PB3

Arythmatik hw;  // hw.display = OLED, hw.eb = encoder (pins 2, 3, switch 12)

// ---------- physics state: changed in the interrupts, the main loop uses it only inside ATOMIC_BLOCK ----------
Ball balls[MAX_BALLS];
uint8_t ballCount;
int16_t cpuY = FX(20);  // top of the CPU paddle, 1/256 px
int16_t gravity, launchSpeed, restSpeed, vxBase, cpuSpeed;
uint16_t bounce256;     // 256 = 100 %
uint8_t stepsPerClock = 16;
bool quantize;
uint16_t rng = 1, serveSeed;     // xorshift16; serveSeed restarts the same pattern on RST
volatile uint8_t playerY = 20;   // top of your paddle (main loop writes, interrupts read)

// timing (interrupt context)
volatile bool clocked;           // following the CLK input
uint32_t lastClockUs;
uint32_t waitLeft;               // ticks still to wait after the next compare A (long step intervals)
uint8_t stepsLeft;               // clocked: steps still due in this clock period
uint8_t tickCount;               // free running: steps since the last internal clock pulse
uint32_t stepQ;                  // clocked: step interval = stepQ + stepR / spreadK ticks
uint8_t stepR, stepAcc, spreadK;
uint8_t pendingHits;             // QUANTIZE: hits waiting for the next clock pulse
volatile uint8_t pulseMask;      // outputs high right now
uint16_t pulseEnd[OUTPUT_COUNT]; // Timer1 time when each output goes low
uint8_t inputPins;               // last PINB, for the edge detection

// ---------- user interface (main loop) ----------
Settings settings;
uint8_t menuItem;      // parameter changed by press + turn
uint8_t overlayItem;   // what the overlay shows
uint32_t overlayUntil;
bool saveDue;
uint32_t lastEditMs;

// ---------- random numbers (interrupt context or inside ATOMIC_BLOCK) ----------
static uint16_t rnd16() {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  rng ^= rng << 8;
  return rng;
}

static uint16_t rndBelow(uint16_t n) {  // 0 .. n-1
  return ((uint32_t)rnd16() * n) >> 16;
}

// ---------- physics (interrupt context or inside ATOMIC_BLOCK) ----------
static int16_t ballSpeed(uint8_t i) {  // horizontal speed: ball 1 x1, ball 2 x1.5, ball 3 x0.75
  if (i == 1) return vxBase + (vxBase >> 1);
  if (i == 2) return vxBase - (vxBase >> 2);
  return vxBase;
}

static uint16_t kickSpeed() {  // vertical speed for a serve or a ball that came to rest
  if (gravity) return ((uint32_t)launchSpeed * (200 + rndBelow(56))) >> 8;  // rises 60-100 % of the court
  return (vxBase >> 1) + rndBelow((vxBase >> 1) + 1);                         // 0.5-1x the base speed
}

static void resetBall(uint8_t i) {
  Ball &b = balls[i];
  b.x = XR(63);
  b.y = FX(8) + rndBelow(FX(24));
  int16_t s = ballSpeed(i);
  b.vx = (rnd16() & 1) ? s : -s;
  if (gravity) {
    b.vy = -(int16_t)rndBelow(launchSpeed / 3 + 1);  // small toss, then it falls
  } else {
    int16_t v = kickSpeed();
    b.vy = (rnd16() & 1) ? v : -v;
  }
}

static void serve(bool newPattern) {
  if (newPattern) serveSeed = rnd16();
  rng = serveSeed;  // the whole game only depends on this seed (and your paddle)
  for (uint8_t i = 0; i < MAX_BALLS; i++) resetBall(i);
  cpuY = FX(20);
}

// Top or bottom wall: keep BOUNCE of the speed, launch again when the ball came to rest.
static void wallBounce(Ball &b, bool top) {
  uint16_t v = b.vy < 0 ? -b.vy : b.vy;
  v = ((uint32_t)v * bounce256) >> 8;
  if (v < (uint16_t)restSpeed && !(top && gravity)) v = kickSpeed();
  b.vy = top ? v : -(int16_t)v;
}

static bool onPaddle(int16_t y, uint8_t top) {
  int8_t by = y >> 8;
  return by + 1 >= top && by <= top + PADDLE_HEIGHT - 1;
}

static void english(Ball &b, uint8_t top) {  // hits off the paddle centre return steeper
  int8_t off = (int8_t)((b.y >> 8) + 1 - top - PADDLE_HEIGHT / 2);
  int16_t vy = b.vy + off * ENGLISH;
  b.vy = constrain(vy, -V_MAX, V_MAX);
}

// One physics step. Returns the hits: bit 0..4 = OUT1..OUT5.
static uint8_t physicsStep() {
  // CPU paddle follows the nearest ball flying towards it
  int16_t target = FX(20), nearest = INT16_MAX;
  for (uint8_t i = 0; i < ballCount; i++) {
    Ball &b = balls[i];
    if (b.vx < 0 && b.x >= CPU_FACE && b.x < nearest) {
      nearest = b.x;
      target = b.y - FX(PADDLE_HEIGHT / 2 - 1);
    }
  }
  int16_t d = target - cpuY;
  d = constrain(d, -cpuSpeed, cpuSpeed);
  int16_t y = cpuY + d;
  cpuY = constrain(y, FX(PADDLE_MIN), FX(PADDLE_MAX));

  uint8_t hits = 0;
  uint8_t cpuTop = cpuY >> 8;
  uint8_t playerTop = playerY;
  for (uint8_t i = 0; i < ballCount; i++) {
    Ball &b = balls[i];
    int16_t vy = b.vy + gravity;
    b.vy = constrain(vy, -V_MAX, V_MAX);
    int16_t oldX = b.x;
    b.x += b.vx;
    b.y += b.vy;
    // paddles only catch balls flying towards their wall, a ball behind a paddle flies back freely
    if (b.vx < 0 && oldX >= CPU_FACE && b.x < CPU_FACE && onPaddle(b.y, cpuTop)) {
      b.x = 2 * CPU_FACE - b.x;
      b.vx = -b.vx;
      english(b, cpuTop);
      hits |= 1 << 3;
    } else if (b.vx > 0 && oldX <= PLAYER_FACE && b.x > PLAYER_FACE && onPaddle(b.y, playerTop)) {
      b.x = 2 * PLAYER_FACE - b.x;
      b.vx = -b.vx;
      english(b, playerTop);
      hits |= 1 << 4;
    }
    if (b.x < BALL_X_MIN) {
      b.x = 2 * BALL_X_MIN - b.x;
      b.vx = -b.vx;
      hits |= 1 << 0;
    } else if (b.x > BALL_X_MAX) {
      b.x = 2 * BALL_X_MAX - b.x;
      b.vx = -b.vx;
      hits |= 1 << 1;
    }
    if (b.y < BALL_Y_MIN) {
      b.y = 2 * BALL_Y_MIN - b.y;
      wallBounce(b, true);
      hits |= 1 << 2;
    } else if (b.y > BALL_Y_MAX) {
      b.y = 2 * BALL_Y_MAX - b.y;
      wallBounce(b, false);
      hits |= 1 << 2;
    }
  }
  return hits;
}

// ---------- triggers (interrupt context) ----------
static void fire(uint8_t hits) {  // 10 ms trigger on each hit output, OUT6 = any hit
  if (!hits) return;
  hits |= 1 << 5;
  uint16_t end = TCNT1 + PULSE_TICKS;
  for (uint8_t i = 0; i < OUTPUT_COUNT; i++) {
    if (hits & (1 << i)) {
      hw.outputs[i].High();  // digitalWrite (interrupt-safe) on the output and its LED
      pulseEnd[i] = end;     // a hit while the output is high makes the pulse longer
    }
  }
  if (!pulseMask) {  // otherwise compare B already waits for an earlier end
    OCR1B = end;
    TIFR1 = _BV(OCF1B);
    TIMSK1 |= _BV(OCIE1B);
  }
  pulseMask |= hits;
}

ISR(TIMER1_COMPB_vect) {  // end of the 10 ms triggers
  for (;;) {
    uint16_t now = TCNT1, next = 0, nextLeft = 0xFFFF;
    for (uint8_t i = 0; i < OUTPUT_COUNT; i++) {
      uint8_t bit = 1 << i;
      if (!(pulseMask & bit)) continue;
      int16_t left = pulseEnd[i] - now;
      if (left <= 0) {
        hw.outputs[i].Low();
        pulseMask &= ~bit;
      } else if ((uint16_t)left < nextLeft) {
        nextLeft = left;
        next = pulseEnd[i];
      }
    }
    if (!pulseMask) {
      TIMSK1 &= ~_BV(OCIE1B);
      return;
    }
    OCR1B = next;
    if ((int16_t)(next - TCNT1) > 2) return;  // still ahead, else handle it right now
  }
}

// ---------- physics step timing (interrupt context or inside ATOMIC_BLOCK) ----------
static void scheduleStep(uint16_t from, uint32_t delay) {  // next compare A, long waits in parts
  uint16_t now = delay > 0x8000 ? 0x8000 : (uint16_t)delay;
  waitLeft = delay - now;
  OCR1A = from + now;
}

static uint32_t nextSpread() {  // clocked: the steps of one clock period, evenly spaced
  uint32_t d = stepQ;
  stepAcc += stepR;
  if (stepAcc >= spreadK) {
    stepAcc -= spreadK;
    d++;
  }
  return d;
}

ISR(TIMER1_COMPA_vect) {  // physics steps
  if (waitLeft) {
    scheduleStep(OCR1A, waitLeft);
    return;
  }
  uint8_t hits;
  if (clocked) {
    if (!stepsLeft) return;  // all steps of this clock done, wait for the next pulse
    if (--stepsLeft) scheduleStep(OCR1A, nextSpread());
    hits = physicsStep();
    if (quantize) {
      pendingHits |= hits;
      hits = 0;
    }
  } else {
    scheduleStep(OCR1A, FREE_STEP_TICKS);
    hits = physicsStep();
    if (tickCount == 0) {  // internal clock pulse
      hits |= pendingHits;
      pendingHits = 0;
    } else if (quantize) {
      pendingHits |= hits;
      hits = 0;
    }
    if (++tickCount >= stepsPerClock) tickCount = 0;
  }
  fire(hits);
}

static void clockPulse() {  // CLK rising edge
  uint32_t now = micros();
  uint32_t period = now - lastClockUs;
  if (period < CLOCK_MIN_US) return;
  lastClockUs = now;
  uint16_t t = TCNT1;
  uint8_t k = stepsPerClock;
  uint8_t hits = pendingHits;
  pendingHits = 0;
  uint32_t ticks;
  if (clocked) {
    ticks = (period * (F_CPU / 1000000UL)) >> 8;
    for (uint8_t n = 0; stepsLeft && n < MAX_CATCH_UP; n++) {  // the clock got faster
      stepsLeft--;
      hits |= physicsStep();
    }
  } else {
    clocked = true;
    ticks = (uint32_t)FREE_STEP_TICKS * k;  // first pulse: free running speed until the period is known
  }
  hits |= physicsStep();  // the first step of every clock period is exactly on the edge
  fire(hits);
  stepsLeft = k - 1;
  waitLeft = 0;
  if (stepsLeft) {
    spreadK = k;
    stepQ = ticks / k;
    stepR = ticks % k;
    if (stepQ < MIN_STEP_TICKS) {
      stepQ = MIN_STEP_TICKS;
      stepR = 0;
    }
    stepAcc = 0;
    scheduleStep(t, nextSpread());
    TIFR1 = _BV(OCF1A);
  }
}

// RST input or long press: back to the serve. newPattern = new random serve.
static void restart(bool newPattern) {
  serve(newPattern);
  pendingHits = 0;
  if (clocked) {
    if (micros() - lastClockUs < RESET_WINDOW_US) {  // reset came just after the clock: start on that pulse
      fire(physicsStep());
      stepsLeft = stepsPerClock - 1;
    } else {
      stepsLeft = 0;  // start on the next clock pulse
    }
  } else {
    tickCount = 0;
    scheduleStep(TCNT1, FREE_STEP_TICKS);
    TIFR1 = _BV(OCF1A);
  }
}

// Called by libmodulove's ISR(PCINT0_vect) on every change of CLK (D13) or RST (D11).
static void onInputChange() {
  uint8_t pins = PINB;
  uint8_t changed = pins ^ inputPins;
  inputPins = pins;
  if (changed & CLK_BIT) digitalWrite(CLOCK_LED, (pins & CLK_BIT) ? HIGH : LOW);
  uint8_t rose = changed & pins;
  if (rose & RST_BIT) restart(false);  // before the clock, so a reset on a clock edge starts on it
  if (rose & CLK_BIT) clockPulse();
}

static void startTimer() {  // inside ATOMIC_BLOCK
  TCCR1A = 0;
  TCCR1B = _BV(CS12);  // normal mode, F_CPU / 256
  waitLeft = 0;
  OCR1A = TCNT1 + FREE_STEP_TICKS;
  TIFR1 = _BV(OCF1A) | _BV(OCF1B);
  TIMSK1 = _BV(OCIE1A);
}

// ---------- settings ----------
static void applySettings() {  // menu values -> physics
  const uint8_t *p = settings.p;
  uint8_t gi = p[P_GRAVITY];
  int16_t g = pgm_read_byte(&GRAVITY_TABLE[gi]);
  int16_t launch = pgm_read_word(&LAUNCH_TABLE[gi]);
  uint16_t b256 = (50 + 5 * p[P_BOUNCE]) * 256 / 100;
  int16_t vxb = 32 * (p[P_SPEED] + 1);  // 100 % = 0.5 px per step = 64 px/s
  uint8_t n = p[P_BALLS] + 1;
  uint8_t k = pgm_read_byte(&STEPS_TABLE[p[P_STEPS]]);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    gravity = g;
    launchSpeed = launch;
    restSpeed = 3 * g + 16;
    bounce256 = b256;
    stepsPerClock = k;
    quantize = p[P_QUANT];
    if (vxb != vxBase) {
      vxBase = vxb;
      cpuSpeed = vxb >> 1;
      for (uint8_t i = 0; i < MAX_BALLS; i++) {
        int16_t s = ballSpeed(i);
        balls[i].vx = balls[i].vx < 0 ? -s : s;
      }
    }
    while (ballCount < n) resetBall(ballCount++);
    ballCount = n;
  }
}

static void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, settings);
  bool ok = settings.magic == SETTINGS_MAGIC;
  for (uint8_t i = 0; i < P_COUNT; i++) {
    if (settings.p[i] > pgm_read_byte(&PARAM_MAX[i])) ok = false;
  }
  if (ok) return;
  settings.magic = SETTINGS_MAGIC;
  for (uint8_t i = 0; i < P_COUNT; i++) settings.p[i] = pgm_read_byte(&PARAM_DEFAULT[i]);
}

static void saveSettingsWhenDue() {
  // One EEPROM.put() a few seconds after the last change; on the LGT8F it stalls the chip
  // ~46 ms, so wait until no trigger is high (it would get 46 ms longer).
  if (saveDue && millis() - lastEditMs >= SAVE_DELAY_MS && !pulseMask) {
    saveDue = false;
    EEPROM.put(SETTINGS_ADDR, settings);
  }
}

static void checkClockTimeout() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (clocked && (int32_t)(micros() - lastClockUs) > (int32_t)CLOCK_TIMEOUT_US) {
      clocked = false;  // back to free running
      stepsLeft = 0;
      tickCount = 0;
      pendingHits = 0;
      scheduleStep(TCNT1, FREE_STEP_TICKS);
      TIFR1 = _BV(OCF1A);
    }
  }
}

// ---------- encoder ----------
static int16_t turnAmount(EncoderButton &eb) {
#ifdef ENCODER_REVERSED
  return eb.increment();
#else
  return -eb.increment();  // hw.eb has the pins in the ENCODER_REVERSED order
#endif
}

static void showOverlay(uint8_t item) {
  overlayItem = item;
  overlayUntil = millis() + OVERLAY_MS;
}

static bool overlayVisible() {
  return (int32_t)(millis() - overlayUntil) < 0;
}

static void onTurn(EncoderButton &eb) {
  int16_t y = playerY + 2 * turnAmount(eb);
  playerY = constrain(y, PADDLE_MIN, PADDLE_MAX);
}

static void onPressedTurn(EncoderButton &eb) {
  int16_t v = settings.p[menuItem] + turnAmount(eb);
  int16_t vmax = pgm_read_byte(&PARAM_MAX[menuItem]);
  settings.p[menuItem] = constrain(v, 0, vmax);
  applySettings();
  showOverlay(menuItem);
  saveDue = true;
  lastEditMs = millis();
}

static void onClick(EncoderButton &) {
  if (overlayVisible() && overlayItem < P_COUNT) menuItem = (menuItem + 1) % P_COUNT;
  showOverlay(menuItem);
}

static void onLongPress(EncoderButton &) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    restart(true);
  }
  showOverlay(OVERLAY_SERVE);
}

// ---------- display ----------
static void paramText(uint8_t item, char *buf) {
  uint8_t v = settings.p[item];
  uint16_t n = v;
  bool percent = false;
  switch (item) {
    case P_BOUNCE:
      n = 50 + 5 * v;
      percent = true;
      break;
    case P_BALLS:
      n = v + 1;
      break;
    case P_SPEED:
      n = 25 * (v + 1);
      percent = true;
      break;
    case P_STEPS:
      n = pgm_read_byte(&STEPS_TABLE[v]);
      break;
    case P_QUANT:
      strcpy_P(buf, v ? PSTR("ON") : PSTR("OFF"));
      return;
  }
  utoa(n, buf, 10);
  if (percent) {
    uint8_t len = strlen(buf);
    buf[len] = '%';
    buf[len + 1] = 0;
  }
}

static void drawOverlay() {
  Adafruit_SSD1306 &d = hw.display;
  char value[6] = "";
  const __FlashStringHelper *name;
  if (overlayItem == OVERLAY_SERVE) {
    name = F("NEW SERVE");
  } else {
    name = (const __FlashStringHelper *)pgm_read_word(&PARAM_NAMES[overlayItem]);
    paramText(overlayItem, value);
  }
  d.fillRect(14, 3, 100, 13, BLACK);
  d.drawRect(14, 3, 100, 13, WHITE);
  d.setTextColor(WHITE);
  d.setCursor(18, 6);
  d.print(name);
  d.setCursor(110 - 6 * strlen(value), 6);
  d.print(value);
}

static void drawFrame() {
  Ball b[MAX_BALLS];
  uint8_t n, cpuTop;
  bool sync;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {  // copy of the latest physics state
    n = ballCount;
    for (uint8_t i = 0; i < n; i++) b[i] = balls[i];
    cpuTop = cpuY >> 8;
    sync = clocked;
  }
  Adafruit_SSD1306 &d = hw.display;
  d.clearDisplay();
  d.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  if (sync) {
    d.drawFastVLine(63, 1, 62, WHITE);  // solid centre line: following the CLK input
  } else {
    for (uint8_t y = 3; y < 61; y += 4) d.drawFastVLine(63, y, 2, WHITE);
  }
  d.drawFastVLine(CPU_X, cpuTop, PADDLE_HEIGHT, WHITE);
  d.drawFastVLine(PLAYER_X, playerY, PADDLE_HEIGHT, WHITE);
  for (uint8_t i = 0; i < n; i++) d.fillRect((b[i].x >> 8) + 64, b[i].y >> 8, 2, 2, WHITE);
  if (overlayVisible()) drawOverlay();
  i2cGuardShow(d);  // never hangs on a stuck display bus
}

void setup() {
  i2cGuardBegin();  // before the first display call
  hw.Init();        // outputs, inputs, display (includes a 1 s pause)

  loadSettings();
  uint16_t seed = chipRandomSeed();
  rng = seed ? seed : 1;
  applySettings();

  hw.eb.setDebounceInterval(5);
  hw.eb.setMultiClickInterval(70);
  hw.eb.setLongClickDuration(800);
  hw.eb.setRateLimit(10);
  hw.eb.setEncoderHandler(onTurn);
  hw.eb.setEncoderPressedHandler(onPressedTurn);
  hw.eb.setClickHandler(onClick);
  hw.eb.setLongPressHandler(onLongPress);

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    serve(true);
    inputPins = PINB;
    hw.AttachClockHandler(onInputChange);  // libmodulove calls it for CLK and RST changes
    startTimer();
  }
}

void loop() {
  hw.eb.update();
  checkClockTimeout();
  saveSettingsWhenDue();
  drawFrame();
}
