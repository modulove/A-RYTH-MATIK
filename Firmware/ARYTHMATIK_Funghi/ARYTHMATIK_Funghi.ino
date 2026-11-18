#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM) - must be the first include
#include "src/libmodulove/arythmatik.h"
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <FastGPIO.h>
#include "OledRle.h"
#include "frames.h"  // the mushrooms, generated from frames_src.h by ../tools/oled_rle.py

/*
 * Funghi - slime mold under dancing mushrooms
 * A Game of Life colony on a 42 x 7 cell grid at the bottom of the display grows, dies back and
 * is fed, one generation per clock step, while three mushrooms dance above it (one animation
 * frame per generation). The outputs fire on what happens in the colony.
 *
 * Timing: the main loop computes up to 4 generations ahead (their trigger bits go into a queue).
 * The clock edge (pin-change interrupt) or the internal clock (Timer1) plays the next one: its
 * triggers go high right on the edge and Timer2 ends them after 10 ms. The display shows the
 * generation that is playing (it skips generations when the clock is faster than the display).
 * A step that finds no generation ready (clock faster than the loop) fires nothing.
 *
 * Outputs (10 ms triggers on the step of their generation):
 *   OUT1  7 or more cells died          OUT4  growth: more births than deaths, below the cap
 *   OUT2  9 or more cells were born     OUT5  decline: more deaths than births
 *   OUT3  stable: as many births as deaths, cells alive
 *   OUT6  more cells than half the cap
 * A colony that has been empty for 16 generations starts again from its start pattern (that
 * step fires nothing, as after RST); its random numbers run on, so it doesn't just repeat.
 *
 * CLK in: one generation per rising edge (or per 2/4/8 edges with the divider). No clock edge
 *         for 2 s = internal clock. Tempo in BPM = 4 generations per beat.
 * RST in: back to the start colony; the same seed gives the same evolution again as long as the
 *         parameters stay the same. A clock edge up to 5 ms after the reset is that same
 *         downbeat, so reset + clock together = the start colony, the next clock = generation 1.
 *
 * Encoder:
 *   turn         change the selected parameter (TEMPO: turn fast for steps of 5)
 *   click        show the selected parameter for 2 s; click again while it is shown = next one:
 *                TEMPO (no clock) / DIVIDER (with a clock), ATTRACTION, REPULSION, RANDOMNESS
 *   hold 0.7 s   new colony (new seed)
 * Settings and the seed are saved 3 s after the last change.
 */

// Configuration flags
//#define ENCODER_REVERSED
//#define ROTATE_PANEL

// The CI builds the reversed .hex with -DENCODER_REVERSED (REVERSE_ENCODER is accepted too).
#if defined(REVERSE_ENCODER) && !defined(ENCODER_REVERSED)
#define ENCODER_REVERSED
#endif

#ifdef ROTATE_PANEL
// When panel is rotated
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
#define PANEL_ROTATED true
#else
// When panel is not rotated
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
#define PANEL_ROTATED false
#endif
typedef FastGPIO::Pin<4> PinClkLed;

using namespace modulove;
using namespace arythmatik;

#define QUEUE_DEPTH 4  // generations computed ahead (a power of 2)
#define PILZ_X 10      // the mushrooms: x = 10, y = 5 (frames.h starts them on page 0)
#define LINE_Y 50      // the line above the cells; cell (x, y) = 3x3 square at (3x, 2y + 50)

#define MIN_BPM 30
#define MAX_BPM 600
#define DEFAULT_BPM 75                   // 5 generations per second, the old internal clock
#define DEFAULT_SEED 0x2A17              // start colony at the first start
#define DIV_COUNT 4                      // external clock divider /1 /2 /4 /8 = 1 << divIdx
#define EXT_TIMEOUT_US 2000000UL         // external clock = a CLK rising edge within the last 2 s
#define RESET_WINDOW_US 5000UL           // a CLK edge this soon after RST is the reset's downbeat
#define OVERLAY_MS 2000
#define COLONY_MSG_MS 1000
#define SAVE_DELAY_MS 3000               // an EEPROM write stalls an LGT8F ~46 ms: save once, later
#define LONG_PRESS_MS 700

// Internal clock = Timer1 in CTC mode, one compare match per step (15 s / BPM, 4 steps per beat).
// The prescaler is chosen from F_CPU so the slowest tempo fits 16 bits (16 MHz Nano, 32 MHz LGT).
#if (F_CPU / 256UL * 15UL / MIN_BPM) <= 65536UL
#define T1_DIV 256UL
#define T1_CS _BV(CS12)
#else
#define T1_DIV 1024UL
#define T1_CS (_BV(CS12) | _BV(CS10))
#endif
#define T1_STEP_TICKS_1BPM (F_CPU / T1_DIV * 15UL)  // Timer1 counts per step at 1 BPM

// Timer2 = trigger length: a CTC tick of 0.5 ms that runs while a trigger is high (as in Stitch).
// Each output counts TRIG_TICKS ticks; the tick starts in phase with the step when no trigger was
// high, so a trigger lasts 10 ms (9.5-10 ms when another trigger was still high).
#define T2_TICK_HZ 2000UL
#if F_CPU / T2_TICK_HZ <= 256
#define T2_PRESCALER 1UL
#define T2_CS_BITS (_BV(CS20))
#elif F_CPU / T2_TICK_HZ / 8 <= 256
#define T2_PRESCALER 8UL
#define T2_CS_BITS (_BV(CS21))
#elif F_CPU / T2_TICK_HZ / 32 <= 256
#define T2_PRESCALER 32UL
#define T2_CS_BITS (_BV(CS21) | _BV(CS20))
#elif F_CPU / T2_TICK_HZ / 64 <= 256
#define T2_PRESCALER 64UL
#define T2_CS_BITS (_BV(CS22))
#elif F_CPU / T2_TICK_HZ / 128 <= 256
#define T2_PRESCALER 128UL
#define T2_CS_BITS (_BV(CS22) | _BV(CS20))
#else
#error "F_CPU too high for the Timer2 trigger tick"
#endif
#define T2_OCR (F_CPU / T2_TICK_HZ / T2_PRESCALER - 1)  // 249 at 16 and 32 MHz
#define TRIG_TICKS 20                                   // 10 ms

// ---------- colony: constants and state (the PC test compiles this part too) ----------
#define GRID_W 42       // 42 columns x 7 rows of cells
#define GRID_H 7
#define START_TRIES 45  // start colony: 45 random cells within 2 cells of the centre
#define SPAWN_BELOW 45  // RANDOMNESS adds cells while fewer cells live
#define FEED_EVERY 8    // ATTRACTION feeds the centre every 8 generations (a power of 2)
#define DEATHS_TRIG 7   // OUT1
#define BIRTHS_TRIG 9   // OUT2
#define EMPTY_RESTART 16  // generations without a live cell before the start pattern comes back
#define P_ATTRACTION 0  // index into settings.param[] and colonyParam[]
#define P_REPULSION 1
#define P_RANDOMNESS 2
#define PARAM_MAX 10

// Types first: the Arduino IDE puts its generated prototypes above the first function.
struct Colony {
  uint8_t col[GRID_W];  // one byte per column: bit y = the cell in row y (row 0 at the top)
  uint32_t rng;         // xorshift32 state: every random decision of the colony comes from here
  uint16_t seed;        // the seed of the start pattern
  uint8_t step;         // steps since RST / new colony (low 8 bits): keeps it in step with the queue
  uint8_t gen;          // generations since the start pattern (low 8 bits); mushroom frame = gen & 7
  uint8_t empty;        // generations in a row the Life rules left no live cell
};
// ---------- end of colony constants ----------

// Menu items (what turning changes) and the info box contents.
enum : uint8_t { ITEM_CLOCK, ITEM_ATTRACTION, ITEM_REPULSION, ITEM_RANDOMNESS, ITEM_COUNT };
#define OV_COLONY ITEM_COUNT  // info box "NEW COLONY"

// Settings, saved in EEPROM as one block (EEPROM.put = one page swap on the LGT8F).
struct FunghiSettings {
  char magic[4];     // "FUN1"
  uint16_t bpm;      // internal tempo, MIN_BPM..MAX_BPM
  uint8_t divIdx;    // external clock divider 1 << divIdx, 0..DIV_COUNT - 1
  uint8_t param[3];  // ATTRACTION, REPULSION, RANDOMNESS, 0..PARAM_MAX
  uint16_t seed;     // start colony and its random numbers
};

Arythmatik hw;

FunghiSettings settings = { { 'F', 'U', 'N', '1' }, DEFAULT_BPM, 0, { 7, 3, 6 }, DEFAULT_SEED };
bool settingsDirty = false;
uint32_t settingsChangedAt;

// Shared between the interrupts and the main loop.
volatile uint8_t qTrig[QUEUE_DEPTH];  // trigger bits of the generations computed ahead
volatile uint8_t qHead;               // the next one to play
volatile uint8_t qCount;              // computed, not played yet
volatile uint8_t playedSteps;         // steps played since RST / new colony (low 8 bits)
volatile uint8_t colonyEpoch;         // +1 = back to the start colony (RST, new colony)
volatile uint16_t missedSteps;        // steps that found no generation ready (for debugging)
volatile uint8_t trigMask;            // outputs high right now, bit 0 = OUT1
volatile uint8_t clockDivider = 1;    // CLK edges per step
volatile bool extClock = false;       // a CLK rising edge came within the last 2 s
volatile uint32_t extPeriodUs = 0;    // time between the last two CLK edges, 0 = not known yet

// Interrupt-only state (the PCINT, Timer1 and Timer2 interrupts never interrupt each other).
uint8_t trigTicks[6];  // Timer2 ticks until each output's trigger ends
uint8_t pinsPrev;      // PINB at the last pin change
uint8_t divCount;      // position inside the divider
uint8_t beatStep;      // internal clock: step inside the beat, for the CLK LED
uint32_t lastClkUs;    // last CLK rising edge
uint32_t resetUs;      // last RST rising edge
bool resetWindow;      // the next CLK edge may still belong to the last reset

// Colony (main loop only). `ahead` is the newest generation computed (its step = playedSteps +
// qCount), `shown` follows the generation playing and is what the display draws. Both are computed
// with the same function and the same parameters, so they go through the same generations.
Colony ahead;
Colony shown;
uint8_t colonyParam[3];   // the parameters the colony is computed with (see syncParams())
uint8_t seenEpoch = 0xFF; // colonyEpoch the colony was last started for

// Display / UI (main loop only).
uint8_t menuItem = ITEM_CLOCK;
uint8_t overlayItem;
bool overlayOn = false;
bool redraw = true;
uint32_t overlayUntil;
uint32_t lastTurnMs;

// ---------- outputs and steps (interrupt side) ----------

// Six outputs and their LEDs, bit 0 = OUT1. FastGPIO writes are single sbi/cbi instructions:
// fast, and safe next to the main loop.
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

// The triggers of one step: high now, each one ends 10 ms later (Timer2). A trigger that fires
// again while it is still high stays high and ends 10 ms after the last step.
static void fireTriggers(uint8_t bits) {
  if (!bits) return;
  uint8_t was = trigMask;
  uint8_t bit = 1;
  for (uint8_t ch = 0; ch < 6; ch++, bit <<= 1) {
    if (bits & bit) trigTicks[ch] = TRIG_TICKS;
  }
  uint8_t m = was | bits;
  trigMask = m;
  writeOutputs(m);
  if (!was) {  // the tick starts in phase with this step
    TCNT2 = 0;
    TIFR2 = _BV(OCF2A);
    TIMSK2 = _BV(OCIE2A);
  }
}

// One clock step: play the next generation computed ahead.
static void playStep() {
  uint8_t n = qCount;
  if (!n) {
    missedSteps++;  // nothing ready (clock faster than the loop): this step fires nothing
    return;
  }
  uint8_t h = qHead;
  fireTriggers(qTrig[h]);
  qHead = (h + 1) & (QUEUE_DEPTH - 1);
  qCount = n - 1;
  playedSteps++;
}

static void onReset() {
  qCount = 0;       // the generations computed ahead are dropped,
  playedSteps = 0;  // the start colony plays (it has no triggers)
  colonyEpoch++;    // and the main loop starts it again from the seed
  divCount = 0;
  beatStep = 0;
  resetUs = micros();
  resetWindow = true;
  TCNT1 = 0;  // the internal clock starts over: its next step comes one full period after the reset
  TIFR1 = _BV(OCF1A);
  if (!extClock) PinClkLed::setOutputValueHigh();  // beat LED: step 0
}

static void onClock() {
  uint32_t now = micros();
  if (extClock) extPeriodUs = now - lastClkUs;
  lastClkUs = now;
  extClock = true;
  if (resetWindow) {
    resetWindow = false;
    if (now - resetUs < RESET_WINDOW_US) {  // reset + clock together: this edge is the start colony
      divCount = (clockDivider > 1) ? 1 : 0;
      return;
    }
  }
  if (divCount == 0) playStep();
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

// Internal clock: one compare match per step. Timer1 always runs; it only plays steps while
// there is no external clock.
ISR(TIMER1_COMPA_vect) {
  uint32_t now = micros();
  if (resetWindow && now - resetUs >= RESET_WINDOW_US) resetWindow = false;
  if (extClock) {
    if (now - lastClkUs < EXT_TIMEOUT_US) return;
    extClock = false;  // no CLK edge for 2 s: the internal clock takes over
    extPeriodUs = 0;
    divCount = 0;
  }
  playStep();
  beatStep = (beatStep + 1) & 3;
  PinClkLed::setOutputValue(beatStep == 0);  // CLK LED shows the beat (every 4 steps)
}

// End of the 10 ms triggers. The tick stops when no trigger is left.
ISR(TIMER2_COMPA_vect) {
  uint8_t m = trigMask, ended = 0;
  uint8_t bit = 1;
  for (uint8_t ch = 0; ch < 6; ch++, bit <<= 1) {
    if (!(m & bit)) continue;
    if (trigTicks[ch] && --trigTicks[ch]) continue;
    ended |= bit;
  }
  if (ended) {
    m &= ~ended;
    trigMask = m;
    writeOutputs(m);
  }
  if (!m) TIMSK2 = 0;
}

static uint16_t tempoTop(uint16_t bpm) {
  return (uint16_t)(T1_STEP_TICKS_1BPM / bpm - 1);
}

// Start Timer1 (internal clock), Timer2 (trigger ends) and the CLK/RST handler.
static void startClock() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1A = 0;  // Arduino set Timer1 up for PWM on D9/D10; Funghi doesn't use analogWrite()
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = tempoTop(settings.bpm);
    TIFR1 = _BV(OCF1A);
    TIMSK1 = _BV(OCIE1A);
    TCCR1B = _BV(WGM12) | T1_CS;  // CTC, TOP = OCR1A
    TIMSK2 = 0;  // Timer2: CTC tick, its interrupt is on only while a trigger is high
    TCCR2A = _BV(WGM21);
    TCCR2B = T2_CS_BITS;
    OCR2A = T2_OCR;
    TCNT2 = 0;
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

// ---------- colony: simulation (main loop only; the PC test compiles this part too) ----------

// Random number 0 .. n-1 (n <= 256) from the colony's xorshift32.
static uint8_t rnd(Colony &c, uint8_t n) {
  uint32_t x = c.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  c.rng = x;
  return ((uint16_t)(uint8_t)(x >> 24) * n) >> 8;
}

static uint8_t bitCount(uint8_t b) {
  b = b - ((b >> 1) & 0x55);
  b = (b & 0x33) + ((b >> 2) & 0x33);
  return (b + (b >> 4)) & 0x0F;
}

static uint16_t population(const Colony &c) {
  uint16_t n = 0;
  for (uint8_t x = 0; x < GRID_W; x++) n += bitCount(c.col[x]);
  return n;
}

// REPULSION -> population cap: 120 - 15 per step up to 3 (75 = the old fixed cap), then 8 per
// step, so it stays above 0 (19 at 10).
static uint8_t capacity(uint8_t r) {
  return (r <= 3) ? (uint8_t)(120 - 15 * r) : (uint8_t)(99 - 8 * r);
}

// The start colony of a seed: 45 random cells within 2 cells of the centre (as the old firmware).
static void startColony(Colony &c, uint16_t seed) {
  c.rng = ((uint32_t)seed + 1) * 2654435761UL;  // never 0
  for (uint8_t i = 0; i < 4; i++) rnd(c, 1);
  for (uint8_t x = 0; x < GRID_W; x++) c.col[x] = 0;
  c.seed = seed;
  c.step = 0;
  c.gen = 0;
  c.empty = 0;
  for (uint8_t i = 0; i < START_TRIES; i++) {
    uint8_t x = GRID_W / 2 - 2 + rnd(c, 5);
    uint8_t y = GRID_H / 2 - 2 + rnd(c, 5);
    c.col[x] |= 1 << y;
  }
}

// Vertical sums of a column: v1:v0 = cell above + cell + cell below, for all 7 rows at once.
static inline void verticalSum(uint8_t c, uint8_t &v0, uint8_t &v1) {
  uint8_t above = c << 1;  // bit y = the cell in row y - 1
  uint8_t below = c >> 1;  // bit y = the cell in row y + 1
  v0 = above ^ c ^ below;
  v1 = (above & c) | (below & (above ^ c));
}

// One generation. Life rules: 3 neighbours = birth, 2 or 3 = survival, no wrap at the edges; no
// births while the population is at or above the REPULSION cap. Then RANDOMNESS and ATTRACTION
// add cells. Returns the trigger bits of the generation (bit 0 = OUT1).
static uint8_t advance(Colony &c) {
  if (c.empty >= EMPTY_RESTART) {
    // The colony died out: its start pattern again, as after RST (that step fires nothing). The
    // random numbers run on: started over with the seed's numbers, a colony that died once
    // would die the same way again and again.
    uint32_t r = c.rng;
    uint8_t s = c.step + 1;
    startColony(c, c.seed);
    c.rng = r;
    c.step = s;
    return 0;
  }
  uint8_t cap = capacity(colonyParam[P_REPULSION]);
  uint16_t before = population(c);
  bool births = before < cap;
  uint16_t live = 0, born = 0;
  uint8_t l0 = 0, l1 = 0;  // vertical sums of the column on the left (none left of column 0)
  uint8_t cur = c.col[0];
  uint8_t m0, m1;          // ... of this column
  verticalSum(cur, m0, m1);
  for (uint8_t x = 0; x < GRID_W; x++) {
    uint8_t right = (x + 1 < GRID_W) ? c.col[x + 1] : 0;  // still the old column
    uint8_t r0, r1;
    verticalSum(right, r0, r1);
    // Sum of the 3x3 block (the cell and its 8 neighbours) = s0 + 2 u0 + 4 u1 + 8 u2, 0..9.
    uint8_t s0 = l0 ^ m0 ^ r0;
    uint8_t k = (l0 & m0) | (r0 & (l0 ^ m0));  // carry into the 2s
    uint8_t p = l1 ^ m1 ^ r1;
    uint8_t q = (l1 & m1) | (r1 & (l1 ^ m1));  // 2s carry into the 4s
    uint8_t u0 = p ^ k;
    uint8_t kk = p & k;
    uint8_t u1 = q ^ kk;
    uint8_t u2 = q & kk;
    // Block sum 3: born, or alive with 2 neighbours. Block sum 4: alive with 3 neighbours.
    uint8_t next = ~u2 & ((s0 & u0 & ~u1) | (cur & ~s0 & ~u0 & u1)) & 0x7F;
    if (!births) next &= cur;
    live += bitCount(next);
    born += bitCount(next & ~cur);
    c.col[x] = next;
    l0 = m0;
    l1 = m1;
    m0 = r0;
    m1 = r1;
    cur = right;
  }
  uint16_t survived = live - born;
  uint16_t died = before - survived;
  uint8_t trig = 0;
  if (died >= DEATHS_TRIG) trig |= 0x01;
  if (born >= BIRTHS_TRIG) trig |= 0x02;
  if (live && born == died) trig |= 0x04;  // stable: the population held (still lifes too)
  if (born > died && live < cap) trig |= 0x08;
  if (died > born) trig |= 0x10;
  if (live > cap / 2) trig |= 0x20;
  if (live) {
    c.empty = 0;
  } else {
    c.empty++;  // (at most EMPTY_RESTART: then the start pattern comes back)
  }
  c.step++;
  c.gen++;

  // RANDOMNESS: every second generation while the colony is small, a cell may appear anywhere.
  // Chance = the old map(live, 0, 45, 100, 10) % at 6, scaled by RANDOMNESS / 6.
  uint8_t rr = colonyParam[P_RANDOMNESS];
  if (rr && !(c.gen & 1) && live < SPAWN_BELOW && live < cap) {
    uint16_t chance = (uint16_t)(100 - 2 * live) * rr / 6;
    if (rnd(c, 100) < chance) {
      uint8_t x = rnd(c, GRID_W);
      uint8_t bit = 1 << rnd(c, GRID_H);
      if (!(c.col[x] & bit)) {
        c.col[x] |= bit;
        live++;
      }
    }
  }

  // ATTRACTION: every 8 generations, cells are fed into the centre (row 3, columns 16..25, as the
  // old firmware). Count = the old map(live, 0, cap, 6, 1) at 7, scaled by ATTRACTION / 7.
  uint8_t a = colonyParam[P_ATTRACTION];
  if (a && !(c.gen & (FEED_EVERY - 1)) && live < cap) {
    uint8_t n = 6 - (uint8_t)(5 * live / cap);
    n = (n * a + 3) / 7;
    while (n--) c.col[GRID_W / 2 - 5 + rnd(c, 10)] |= 1 << (GRID_H / 2);
  }
  return trig;
}
// ---------- end of colony simulation ----------

// ---------- colony bookkeeping (main loop) ----------

// Back to the start colony of the seed (after RST, a new colony or at power-up). The interrupt
// side already dropped the queue and set playedSteps to 0.
static void restartColony() {
  uint8_t e = colonyEpoch;
  startColony(shown, settings.seed);
  ahead = shown;
  seenEpoch = e;
  redraw = true;
}

static void requestRestart() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    qCount = 0;
    playedSteps = 0;
    colonyEpoch++;
  }
}

// Brings `shown` to the generation playing. It is never more than 2 x QUEUE_DEPTH behind (the
// loop catches up on every pass that sees a new generation); if it is, something went wrong and
// the colony starts over.
static void catchUp(uint8_t target) {
  uint8_t d = target - shown.step;
  if (d > 2 * QUEUE_DEPTH) {
    requestRestart();
    return;
  }
  while (d--) advance(shown);
}

// Computes generations ahead until the queue is full, at most QUEUE_DEPTH per loop pass: with a
// clock faster than the computation it would never end, and `shown` could fall behind further.
static void fillQueue() {
  for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
    bool ok;
    uint8_t tail;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      ok = colonyEpoch == seenEpoch && qCount < QUEUE_DEPTH;
      tail = (qHead + qCount) & (QUEUE_DEPTH - 1);  // pops don't move it
    }
    if (!ok) return;
    uint8_t trig = advance(ahead);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      ok = colonyEpoch == seenEpoch;  // no RST meanwhile
      if (ok && (uint8_t)(playedSteps + qCount + 1) == ahead.step) {
        qTrig[tail] = trig;
        qCount++;
      } else if (ok) {
        ok = false;  // out of step (never expected): start over
        qCount = 0;
        playedSteps = 0;
        colonyEpoch++;
      }
    }
    if (!ok) return;
  }
}

// A parameter was changed: the generations computed ahead are dropped and computed again from the
// one playing, so the change counts from the next step on (and RST replays it the same way).
static void syncParams() {
  if (colonyParam[0] == settings.param[0] && colonyParam[1] == settings.param[1]
      && colonyParam[2] == settings.param[2]) return;
  uint8_t target, e;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    qCount = 0;
    target = playedSteps;
    e = colonyEpoch;
  }
  if (e == seenEpoch) {  // (after an RST the restart takes care of it)
    catchUp(target);     // still with the old parameters
    ahead = shown;
  }
  for (uint8_t i = 0; i < 3; i++) colonyParam[i] = settings.param[i];
}

// New colony: new seed, start colony from it.
static void newColony() {
  uint16_t s = (uint16_t)micros() ^ (uint16_t)ahead.rng ^ (uint16_t)(ahead.rng >> 16);
  if (s == settings.seed) s ^= 0x5A5A;
  settings.seed = s;
  requestRestart();
}

// ---------- settings ----------
void loadSettings() {
  FunghiSettings st;
  EEPROM.get(0, st);
  if (st.magic[0] != 'F' || st.magic[1] != 'U' || st.magic[2] != 'N' || st.magic[3] != '1') return;
  settings.bpm = constrain(st.bpm, MIN_BPM, MAX_BPM);
  settings.divIdx = (st.divIdx < DIV_COUNT) ? st.divIdx : 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (st.param[i] <= PARAM_MAX) settings.param[i] = st.param[i];
  }
  settings.seed = st.seed;
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
void showOverlay(uint8_t item) {
  overlayItem = item;
  overlayOn = true;
  overlayUntil = millis() + (item == OV_COLONY ? COLONY_MSG_MS : OVERLAY_MS);
  redraw = true;
}

// Turn: change the selected parameter (TEMPO without a clock, DIVIDER with a clock).
void onTurn(EncoderButton &eb) {
  int16_t inc = eb.increment();
#ifndef ENCODER_REVERSED
  inc = -inc;  // hw.eb is EncoderButton(2, 3); the other A-RYTH-MATIK firmware use (3, 2) as normal
#endif
  if (inc == 0) return;
  uint32_t now = millis();
  if (menuItem == ITEM_CLOCK) {
    if (extClock) {
      int8_t d = (int8_t)settings.divIdx + (inc > 0 ? 1 : -1);
      settings.divIdx = constrain(d, 0, DIV_COUNT - 1);
      clockDivider = 1 << settings.divIdx;
    } else {
      int16_t step = (now - lastTurnMs < 60) ? 5 : 1;  // fast turn = bigger steps
      int16_t bpm = (int16_t)settings.bpm + inc * step;
      settings.bpm = constrain(bpm, MIN_BPM, MAX_BPM);
      applyTempo();
    }
  } else {
    uint8_t &p = settings.param[menuItem - ITEM_ATTRACTION];
    int16_t v = (int16_t)p + inc;
    p = constrain(v, 0, PARAM_MAX);  // the loop applies it (syncParams)
  }
  lastTurnMs = now;
  settingsChanged();
  showOverlay(menuItem);
}

// Click: show the selected parameter; while it is shown, the next one.
void onClick(EncoderButton &) {
  if (overlayOn && overlayItem < ITEM_COUNT) menuItem = (menuItem + 1) % ITEM_COUNT;
  showOverlay(menuItem);
}

// Hold: new colony.
void onLongPress(EncoderButton &) {
  newColony();
  settingsChanged();
  showOverlay(OV_COLONY);
}

// ---------- display ----------

// One column of cells (bit 0 = row 48 of the display) into display pages 6 and 7.
static inline void putCellColumn(uint8_t *buf, uint8_t px, uint16_t bits) {
#ifdef ROTATE_PANEL
  buf[128 + 127 - px] |= oledRleFlip(bits & 0xFF);  // 180 degrees: rows 48..55 -> page 1
  buf[127 - px] |= oledRleFlip(bits >> 8);          // rows 56..63 -> page 0
#else
  buf[6 * 128 + px] |= bits & 0xFF;
  buf[7 * 128 + px] |= bits >> 8;
#endif
}

// The cells as 3x3 outlines at (3x, 2y + 50) (the bottom row is cut off at the display edge),
// straight into the display buffer: what drawRect() drew in the old firmware.
static void drawCells(uint8_t *buf, const Colony &c) {
  for (uint8_t x = 0; x < GRID_W; x++) {
    uint8_t cells = c.col[x];
    if (!cells) continue;
    uint16_t side = 0, middle = 0;
    uint16_t s = 0x07 << (LINE_Y - 48), m = 0x05 << (LINE_Y - 48);  // a cell in row 0
    for (uint8_t y = 0; y < GRID_H; y++, s <<= 2, m <<= 2) {
      if (cells & (1 << y)) {
        side |= s;
        middle |= m;
      }
    }
    uint8_t px = 3 * x;
    putCellColumn(buf, px, side);
    putCellColumn(buf, px + 1, middle);
    putCellColumn(buf, px + 2, side);
  }
}

static const __FlashStringHelper *itemName(uint8_t item, bool ext) {
  switch (item) {
    case ITEM_ATTRACTION: return F("ATTRACTION");
    case ITEM_REPULSION: return F("REPULSION");
    case ITEM_RANDOMNESS: return F("RANDOMNESS");
    default: return ext ? F("DIVIDER") : F("TEMPO");
  }
}

// Info box: the parameter and its value, the clock or the colony below it.
static void drawInfo() {
  Adafruit_SSD1306 &d = hw.display;
  d.setTextSize(1);
  d.setTextColor(WHITE);
  d.setTextWrap(false);
  if (overlayItem == OV_COLONY) {
    d.fillRect(28, 4, 72, 15, BLACK);
    d.drawRect(28, 4, 72, 15, WHITE);
    d.setCursor(34, 8);
    d.print(F("NEW COLONY"));
    return;
  }
  bool ext = extClock;
  char value[8];
  if (overlayItem == ITEM_CLOCK) {
    if (ext) {
      value[0] = '/';
      utoa(clockDivider, value + 1, 10);
    } else {
      utoa(settings.bpm, value, 10);
      strcat_P(value, PSTR(" BPM"));
    }
  } else {
    utoa(settings.param[overlayItem - ITEM_ATTRACTION], value, 10);
  }
  d.fillRect(4, 4, 120, 26, BLACK);
  d.drawRect(4, 4, 120, 26, WHITE);
  d.setCursor(8, 8);
  d.print(itemName(overlayItem, ext));
  d.setCursor(122 - 6 * strlen(value), 8);
  d.print(value);
  d.setCursor(8, 19);
  if (overlayItem != ITEM_CLOCK) {
    d.print(F("cells "));
    d.print(population(shown));
    d.print(F("  cap "));
    d.print(capacity(colonyParam[P_REPULSION]));
  } else if (ext) {
    uint32_t p;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      p = extPeriodUs;
    }
    d.print(F("EXT CLOCK "));
    if (p) {
      d.print((15000000UL + p / 2) / p);  // BPM with the clock as 16ths (4 PPQN)
    } else {
      d.print(F("---"));
    }
    d.print(F(" BPM"));
  } else {
    d.print(F("INT CLOCK"));
  }
}

// Draw the generation playing and send it. False = not shown (try again).
static bool drawScreen() {
  uint8_t target, e;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    target = playedSteps;
    e = colonyEpoch;
  }
  if (e != seenEpoch) return false;  // RST came: the loop restarts the colony first
  catchUp(target);
  if (shown.step != target || colonyEpoch != seenEpoch) return false;  // (RST during the catch-up)
  uint8_t *buf = hw.display.getBuffer();
  if (!buf) return false;
  hw.display.clearDisplay();
  oledRleDraw(buf, PILZ_RLE, PILZ_OFS, shown.gen & 7, PILZ_W, PILZ_PAGES, PILZ_X, 0, false, PANEL_ROTATED);
  hw.display.drawFastHLine(0, LINE_Y, SCREEN_WIDTH, WHITE);
  drawCells(buf, shown);
  if (overlayOn) drawInfo();
  return i2cGuardShow(hw.display);
}

void setup() {
#ifdef ROTATE_PANEL
  hw.config.RotatePanel = true;
#endif
#ifdef ENCODER_REVERSED
  hw.config.ReverseEncoder = true;
#endif

  loadSettings();
  clockDivider = 1 << settings.divIdx;
  for (uint8_t i = 0; i < 3; i++) colonyParam[i] = settings.param[i];

  i2cGuardBegin();  // a stuck display bus can no longer freeze the module
  hw.Init();

  // Outputs and LEDs as outputs. Funghi sets them itself (FastGPIO): libmodulove's rotated pin
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

  // libmodulove's encoder (D2/D3, switch D12); Funghi creates no second one on the same pins.
  hw.eb.setDebounceInterval(5);
  hw.eb.setMultiClickInterval(30);  // no double clicks: a click fires right after the release
  hw.eb.setLongClickDuration(LONG_PRESS_MS);
  hw.eb.setEncoderHandler(onTurn);
  hw.eb.setEncoderPressedHandler(onTurn);
  hw.eb.setClickHandler(onClick);
  hw.eb.setLongPressHandler(onLongPress);

  colonyEpoch = 0;
  restartColony();  // the start colony and the first generations are ready before the clock runs
  fillQueue();
  startClock();
}

void loop() {
  static uint8_t drawnStep;
  static bool shownExt = false;
  static uint32_t lastDrawMs;

  hw.eb.update();

  if (colonyEpoch != seenEpoch) restartColony();  // RST or new colony
  syncParams();
  fillQueue();

  uint32_t now = millis();
  bool ext = extClock;
  if (ext != shownExt) {  // clock patched in or gone: TEMPO <-> DIVIDER, show it for a moment
    shownExt = ext;
    menuItem = ITEM_CLOCK;
    showOverlay(ITEM_CLOCK);
  }
  if (overlayOn) {
    if ((int32_t)(now - overlayUntil) >= 0) {
      overlayOn = false;
      redraw = true;
    } else if (now - lastDrawMs >= 250) {
      redraw = true;  // keep the incoming BPM fresh while the clock is slow
    }
  }
  // Save when no trigger is high (the LGT8F's ~46 ms stall would make it longer), at the latest
  // 2 s later.
  if (settingsDirty && now - settingsChangedAt >= SAVE_DELAY_MS
      && (!trigMask || now - settingsChangedAt >= SAVE_DELAY_MS + 2000)) {
    saveSettings();
  }

  // Only the generation playing is drawn: generations the display could not keep up with are skipped.
  uint8_t s = playedSteps;
  if (s != drawnStep || redraw) {
    redraw = false;
    lastDrawMs = now;
    if (drawScreen()) {
      drawnStep = shown.step;
    } else {
      redraw = true;  // display bus guard skipped it, or a restart comes first: try again
    }
  }
}
