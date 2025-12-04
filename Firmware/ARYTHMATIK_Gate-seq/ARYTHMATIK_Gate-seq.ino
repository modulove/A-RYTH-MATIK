/**
 * ARYTHMATIK_Gate-seq — U8x8 text-only, lean + double-length presets (ATmega328P)
 *
 * Delta in this build:
 * - Adds USER style (genre=7) with EEPROM-backed USER BASE + USER FILL patterns.
 * - First menu item cycles MANU (manual) or style names: TECHN0/DUB/HOUSE/HALF/GEN/AMBIENT/DNB/USER.
 * - Serial: S0..S7 (S7=USER), U?, U=..., UF=...
 * - USER behaves like a fixed bank: BASE on normal cycles, FILL one-shot when F-EV hits, then back to BASE.
 *
 * Previous features kept:
 * - Removed RESET from bottom menu; safe D0/D1 LED toggle (J0/1); gate duty Y##; progress bar with F/S markers; deferred EEPROM writes; OLED flip maps I/O + LEDs.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <U8x8lib.h>
#include <avr/pgmspace.h>
#include <EncoderButton.h>

// ---------------- Display (U8x8) ----------------
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/*reset=*/U8X8_PIN_NONE);
volatile bool uiDirty = true;
unsigned long nextOledMs = 0;
const uint16_t OLED_INTERVAL_MS = 40;  // 25 Hz

// --------------- Encoder+Button ---------------
EncoderButton encBtn(3, 2, 12);  // A=D3, B=D2, SW=D12
unsigned long lastEncHandledMs = 0;
const uint8_t ENC_RATE_MS = 8;

// --------------- Timing Constants ---------------
const uint16_t SECRET_MENU_BOOT_HOLD_MS = 3000;
const uint16_t SECRET_MENU_RUNTIME_HOLD_MS = 5000;
const uint16_t SECRET_MENU_SAVE_HOLD_MS = 1500;
const uint16_t SECRET_MENU_FACTORY_HOLD_MS = 4000;

// --------------- EEPROM layout ----------------
#define EE_SIG_ADDR 0
#define EE_SIG_VAL  0xA7
// 1..12 -> current pattern words (ch1..ch6 hi/lo)
#define EE_OLED_ROT 13
#define EE_ENC_DIR  14
// 15 free
#define EE_CLK_SRC  16
#define EE_TEMPO    17
// 18..19 free
#define EE_GENRE    20
#define EE_MODE     21
#define EE_REPEAT   22
#define EE_FILLIN   23
#define EE_SW       24
#define EE_RST      25
#define EE_FPER     26
#define EE_OMODE    27
#define EE_TIMEOUT  28
#define EE_DUTY     29
#define EE_D01LED   30   // 0=safe (Serial), 1=use D0/D1 as LEDs
// USER preset storage (BASE + FILL): 6 words (12 bytes) each
#define EE_UBASE    31   // 31..42
#define EE_UFILL    43   // 43..54

// --------------- Pins / ports ------------------
#define CH1_PD_BIT 5  // D5
#define CH2_PD_BIT 6  // D6
#define CH3_PD_BIT 7  // D7
#define CH4_PB_BIT 0  // D8
#define CH5_PB_BIT 1  // D9
#define CH6_PB_BIT 2  // D10

#define LED1_PC_BIT 0  // A0
#define LED2_PC_BIT 1  // A1
#define LED3_PC_BIT 2  // A2
#define LED4_PD_BIT 0  // D0 (RX)  — only if D01 LED = ON
#define LED5_PD_BIT 1  // D1 (TX)  — only if D01 LED = ON
#define LED6_PC_BIT 3  // A3

#define CLK_PB_BIT 5  // D13, PCINT5
#define RST_PB_BIT 3  // D11, PCINT3

// --- Globals ---
uint8_t oledFlip = 0;

// Pin mapping helpers (for OLED rotation support)
static inline uint8_t mapClkPin() { return oledFlip ? RST_PB_BIT : CLK_PB_BIT; }
static inline uint8_t mapRstPin() { return oledFlip ? CLK_PB_BIT : RST_PB_BIT; }
static inline uint8_t mapOutputChannel(uint8_t ch) {
  if (!oledFlip) return ch;
  switch(ch) {
    case 0: return 3; // CH1->CH4
    case 1: return 4; // CH2->CH5
    case 2: return 5; // CH3->CH6
    case 3: return 0; // CH4->CH1
    case 4: return 1; // CH5->CH2
    case 5: return 2; // CH6->CH3
    default: return ch;
  }
}

// ---------- debug + timeout UI ----------
#define DEBUG 1
#if DEBUG
bool dbg = false;
volatile uint8_t dbg_rst_evt = 255;
#endif

// ---------- idle timeout bar ----------
uint8_t menuTimeoutSec = 5;
unsigned long lastUIActivityMs = 0;
bool hideUI = false;

// ---------- deferred EEPROM writes ----------
bool eepromDirty = false;
unsigned long lastEepromWriteMs = 0;
const uint16_t EEPROM_WRITE_DELAY_MS = 2000;

// ---------------- Clock/Timing -----------------
#define TRIG_US 10000UL
enum ClockSrc { CLK_EXT = 0, CLK_INT = 1 };
volatile uint8_t  clkSource = CLK_EXT;
volatile uint16_t bpm = 120;

// Output mode
enum OutMode : uint8_t { OUT_TRG = 0, OUT_GAT = 1, OUT_FF = 2 };
volatile uint8_t outMode = OUT_TRG;

// GATE duty + step period estimation
volatile uint32_t stepPeriodUsEst = 125000; // µs per step (≈120 BPM)
volatile uint32_t lastExtClkUs    = 0;
volatile unsigned long lastStepEdgeMs = 0;
uint8_t gateDuty = 50; // % (1..99)

// D0/D1 LED usage (safe manual toggle)
uint8_t useD01AsLEDs = 0;

// TRIG/GATE timing bookkeeping
volatile uint8_t step_count = 1;  // 1..16
volatile uint8_t wrappedEdge = 0;
volatile uint8_t stepEdgeFlag = 0;
volatile uint8_t resetEdgeFlag = 0;
volatile uint8_t outBits = 0;  // CH1..CH6 mask
unsigned long trigOffAtUs[6] = { 0, 0, 0, 0, 0, 0 };
volatile uint8_t trigActiveMask = 0;

// ----------------- UI / Menu -------------------
int8_t  encDir = +1;  // +1 normal / -1 reverse
uint8_t mode   = 0;   // 0=MANUAL, 1=AUTO
uint8_t enc    = 96;  // selection index

const uint8_t STEP_MIN = 1;
const uint8_t STEP_MAX = 96;

// Bottom menu IDs
const uint8_t ENC_STYLE  = 97;   // "MANU" or style name
const uint8_t ENC_FILLIN = 98;   // FILL:Y/N
const uint8_t ENC_CLK    = 99;   // CLK:E/I
const uint8_t ENC_BPM    = 100;  // BPM:xxx
const uint8_t ENC_REP    = 101;  // REP:xx/ET
const uint8_t ENC_SW     = 102;  // SW:xx/ET
const uint8_t ENC_RIN    = 103;  // IN:X
const uint8_t ENC_GEN    = 104;  // GEN
const uint8_t ENC_MUT    = 105;  // MUT
const uint8_t ENC_FPER   = 106;  // F-EV:n
const uint8_t ENC_OMODE  = 107;  // OUT:TRG/GAT/FF
const uint8_t ENC_DUTY   = 108;  // DUTY:xx
const uint8_t ENC_LAST   = ENC_DUTY;

int16_t menuScrollCol = 0;

// -------------- Secret menu state --------------
bool  secretMenuActive = false;
uint8_t secretIndex    = 0;  // 0 ROT, 1 ENC, 2 CLK, 3 RIN, 4 TIMEOUT, 5 SAVE, 6 FACT, 7 D01 LED
long secretEncOld      = 0;
bool secWasPressed     = false;
unsigned long secPressStart = 0;

// --------------- Patterns/state ---------------
uint16_t ch1_step = 0x8888, ch2_step = 0x0808, ch3_step = 0xCCCC;
uint16_t ch4_step = 0x2222, ch5_step = 0xFFFF, ch6_step = 0x0000;

// 0=TECHNO 1=DUB 2=HOUSE 3=HALF 4=GEN 5=AMBIENT 6=DNB 7=USER
uint8_t genre = 0;
uint8_t fillin = 1;
uint8_t sw = 0;
int repeat = 2, repeat_max = 16, repeat_done = 0;
int sw_max = 1, sw_done = 0;

uint8_t change_bnk1 = 0, change_bnk2 = 0, change_bnk3 = 0, change_bnk4 = 0;

enum RstAction : uint8_t { RIN_RESET = 0, RIN_FILL = 1, RIN_GEN = 2, RIN_MUT = 3 };
uint8_t rstAction = RIN_RESET;
volatile uint8_t rstActionFlag = 0;

uint8_t fillPeriodIdx = 2;  // 0..4 -> 1/2/4/8/16
bool    fillActive = false;

static inline uint8_t fillPeriodFromIdx(uint8_t i) { return (uint8_t)(1u << i); }

// ========== PRESETS (double length: BASE[0..5], FILL[6..11]) ==========
// Bank 1 (TECHNO-ish)
const static uint16_t bnk1_ptn[8][12] PROGMEM = {
 { 0x8888, 0x0808, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xDDDD, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xFFFF, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0808, 0x4545, 0x2222, 0x1000, 0x0022,
   0x88AF, 0x0808, 0x4545, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022,
   0x88AF, 0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0809, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x0000, 0x0809, 0xDDDD, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x0000, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022 },
 { 0x8888, 0x0802, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x8896, 0x0869, 0xDDDD, 0x2222, 0x1000, 0x0022 }
};
// Bank 2 (DUB-ish)
const static uint16_t bnk2_ptn[8][12] PROGMEM = {
 { 0x8888, 0x0808, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0809, 0xDDDD, 0x2222, 0x1240, 0x0022 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022 },
 { 0x8889, 0x0808, 0xCCCC, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0000, 0xCCCC, 0x2222, 0x1240, 0x0022 },
 { 0x8889, 0x0808, 0x4545, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0809, 0x4545, 0x2222, 0x1240, 0x0022 },
 { 0x888C, 0x0808, 0xFFFF, 0x2222, 0x1240, 0x0022,
   0x8888, 0x8888, 0xFFFF, 0x2222, 0x1240, 0x0022 },
 { 0x888C, 0x0809, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x999F, 0x0000, 0xDDDD, 0x2222, 0x1240, 0x0022 },
 { 0x0000, 0x0849, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022 },
 { 0x0000, 0x0802, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0802, 0xDDDD, 0x2222, 0x1000, 0x0022 }
};
// Bank 3 (HOUSE + DNB on row 7)
const static uint16_t bnk3_ptn[8][12] PROGMEM = {
 { 0x8888, 0x0808, 0x2222, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2222, 0x0000, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0x2323, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2323, 0x0000, 0x0040, 0x0101 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112 },
 // DnB/Breakbeat (row 7)
 { 0x9008, 0xF0F0, 0x0808, 0x2222, 0x0040, 0x4112,
   0xB008, 0xF8F8, 0x0C08, 0x2222, 0x1040, 0x4112 }
};
// Bank 4 (HALF + AMBIENT on row 7)
const static uint16_t bnk4_ptn[8][12] PROGMEM = {
 { 0x8888, 0x0808, 0x0000, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2222, 0x0040, 0x0000, 0x0101 },
 { 0x888A, 0x0808, 0x2323, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2323, 0x0000, 0x0040, 0x0101 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112 },
 // Ambient (row 7)
 { 0x8080, 0x1010, 0x1000, 0x0001, 0x0040, 0x0100,
   0xC0C0, 0x3030, 0x1200, 0x0005, 0x0040, 0x0101 }
};

// ---------- fwd decls ----------
static void applyOLEDflip(uint8_t flip) { u8x8.setFlipMode(flip ? 1 : 0); }
static void recalcTimer1();
static void internalClockStart();
static void internalClockStop();
static void writeOutputsFast(uint8_t mask);
static void writeLEDsFast(uint8_t mask);
static inline void handleStepEdgeISR();
static inline void handleResetEdgeISR();
static inline void markDirty();

void OLED_display();
void save_data();
void change_step();
void loadPresetBase(uint8_t bank, uint8_t idx);
void loadPresetFill(uint8_t bank, uint8_t idx);
void fillin_step();

// USER slot helpers
void loadUserBase();
void loadUserFill();
void saveUserBase(uint16_t v[6]);
void saveUserFill(uint16_t v[6]);

// UI helpers
void drawGrid();
void drawCaret();
void drawBottomMenu();
uint8_t bottomItemCount();
uint8_t bottomItemIdAt(uint8_t idx);
uint8_t bottomItemWidthCh(uint8_t id);
uint8_t buildItemLabel(uint8_t id);

// Secret menu
void secretMenuLoop();
void drawSecretMenu(uint8_t progress);

// EncoderButton handlers
void onRotate(EncoderButton& eb);
void onRotatePressed(EncoderButton& eb);
void onClick(EncoderButton& eb);

// -------- Fast LFSR random --------
static uint16_t lfsr = 0xACE1;
static inline uint8_t fastRandom8() {
  lfsr = (lfsr >> 1) ^ (-(int16_t)(lfsr & 1) & 0xB400);
  return (uint8_t)lfsr;
}
static inline uint16_t fastRandom16() { fastRandom8(); return lfsr; }
static inline bool fastRandomChance(uint8_t percent) { return (fastRandom8() % 100) < percent; }

// -------- GEN + MUT --------
static void genRandomPattern(uint16_t& a, uint16_t& b, uint16_t& c, uint16_t& d, uint16_t& e, uint16_t& f) {
  a = (1u << 15) | (1u << 11) | (1u << 7) | (1u << 3);
  if (fastRandomChance(40)) a |= (1u << 13);
  if (fastRandomChance(25)) a |= (1u << 1);
  b = 0; for (uint8_t i=0;i<16;i++) if (fastRandomChance(50)) b |= (1u<<(15-i));
  c = 0; if (fastRandomChance(80)) c |= (1u<<11); if (fastRandomChance(65)) c |= (1u<<3);
  d = 0; for (uint8_t i=0;i<5;i++)  if (fastRandomChance(30)) d |= (1u<<(fastRandom8()&15));
  e = 0; for (uint8_t i=0;i<3;i++)  if (fastRandomChance(25)) e |= (1u<<(fastRandom8()&15));
  f = fastRandomChance(30) ? 0xF000 : 0x0000;
}

// --- MUTATE helpers (compact + musical) ---
static inline void mutateMask(uint16_t& m, uint16_t anchorMask) {
  // Flip/add/remove a few random bits, but never touch "anchor" bits.
  uint8_t changes = 1 + (fastRandom8() % 3);  // 1..3 edits
  for (uint8_t i = 0; i < changes; i++) {
    uint8_t pos = fastRandom8() & 15;
    uint16_t bit = (uint16_t)(1u << pos);
    if (anchorMask & bit) continue;  // keep anchors intact
    uint8_t r = fastRandom8() % 100;
    if      (r < 60) m ^=  bit;   // toggle (most likely)
    else if (r < 80) m |=  bit;   // force on
    else             m &= ~bit;   // force off
  }
}

static inline void mutatePatternAll() {
  // Keep 4-on-the-floor anchors on channel 1, mutate everything else freely.
  const uint16_t anchors = (1u<<15) | (1u<<11) | (1u<<7) | (1u<<3);
  mutateMask(ch1_step, anchors);
  mutateMask(ch2_step, 0);
  mutateMask(ch3_step, 0);
  mutateMask(ch4_step, 0);
  mutateMask(ch5_step, 0);
  mutateMask(ch6_step, 0);
}


// ---------- helpers ----------
static void recalcTimer1() {
  uint32_t ocr = 3750000UL / (uint32_t)bpm; // 16MHz, /64, 4 steps/beat
  if (ocr > 0) ocr--;
  if (ocr < 1000UL)  ocr = 1000UL;
  if (ocr > 65535UL) ocr = 65535UL;
  noInterrupts();
  OCR1A = (uint16_t)ocr;
  stepPeriodUsEst = ((uint32_t)OCR1A + 1U) * 4U; // 4 µs per tick
  interrupts();
}
static void internalClockStart() {
  noInterrupts();
  TCCR1A = 0; TCCR1B = 0;
  recalcTimer1();
  TCCR1B |= _BV(WGM12);
  TCCR1B |= _BV(CS11) | _BV(CS10);
  TIMSK1 |= _BV(OCIE1A);
  interrupts();
}
static void internalClockStop() {
  noInterrupts();
  TIMSK1 &= ~_BV(OCIE1A);
  TCCR1B = 0;
  interrupts();
}
static void writeOutputsFast(uint8_t mask) {
  uint8_t mapped = 0;
  for (uint8_t i=0; i<6; i++) if (mask & (1<<i)) mapped |= (1 << mapOutputChannel(i));
  if (mapped & (1<<0)) PORTD |= _BV(CH1_PD_BIT); else PORTD &= ~_BV(CH1_PD_BIT);
  if (mapped & (1<<1)) PORTD |= _BV(CH2_PD_BIT); else PORTD &= ~_BV(CH2_PD_BIT);
  if (mapped & (1<<2)) PORTD |= _BV(CH3_PD_BIT); else PORTD &= ~_BV(CH3_PD_BIT);
  if (mapped & (1<<3)) PORTB |= _BV(CH4_PB_BIT); else PORTB &= ~_BV(CH4_PB_BIT);
  if (mapped & (1<<4)) PORTB |= _BV(CH5_PB_BIT); else PORTB &= ~_BV(CH5_PB_BIT);
  if (mapped & (1<<5)) PORTB |= _BV(CH6_PB_BIT); else PORTB &= ~_BV(CH6_PB_BIT);
}
static void writeLEDsFast(uint8_t mask) {
  uint8_t mapped = 0;
  for (uint8_t i=0; i<6; i++) if (mask & (1<<i)) mapped |= (1 << mapOutputChannel(i));

  // A0..A2 = LED1..LED3
  if (mapped & (1<<0)) PORTC |= _BV(LED1_PC_BIT); else PORTC &= ~_BV(LED1_PC_BIT);
  if (mapped & (1<<1)) PORTC |= _BV(LED2_PC_BIT); else PORTC &= ~_BV(LED2_PC_BIT);
  if (mapped & (1<<2)) PORTC |= _BV(LED3_PC_BIT); else PORTC &= ~_BV(LED3_PC_BIT);

  // D0..D1 = LED4..LED5 (ONLY if explicitly enabled)
  if (useD01AsLEDs) {
    DDRD |= _BV(LED4_PD_BIT) | _BV(LED5_PD_BIT);
    if (mapped & (1<<3)) PORTD |= _BV(LED4_PD_BIT); else PORTD &= ~_BV(LED4_PD_BIT);
    if (mapped & (1<<4)) PORTD |= _BV(LED5_PD_BIT); else PORTD &= ~_BV(LED5_PD_BIT);
  }

  // A3 = LED6
  if (mapped & (1<<5)) PORTC |= _BV(LED6_PC_BIT); else PORTC &= ~_BV(LED6_PC_BIT);
}

// ====== tiny serial ======
static char sBuf[64];
static inline void serialInit() { Serial.begin(115200); }
static inline void printHex4(uint16_t v) {
  if (v < 0x1000) Serial.print('0');
  if (v < 0x100)  Serial.print('0');
  if (v < 0x10)   Serial.print('0');
  Serial.print(v, HEX);
}
static bool parse6hex(const char* p, uint16_t out[6]) {
  for (uint8_t i=0;i<6;i++){
    char* endp;
    unsigned long v = strtoul(p, &endp, 16);
    if (endp == p) return false;
    out[i] = (uint16_t)v;
    if (i<5){ if (*endp != ',') return false; p = endp+1; }
  }
  return true;
}
static void dump6hex(const char* prefix, uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f){
  Serial.print(prefix);
  printHex4(a); Serial.print(',');
  printHex4(b); Serial.print(',');
  printHex4(c); Serial.print(',');
  printHex4(d); Serial.print(',');
  printHex4(e); Serial.print(',');
  printHex4(f); Serial.print('\n');
}

static void serialPoll() {
  if (!Serial.available()) return;
  size_t n = Serial.readBytesUntil('\n', sBuf, sizeof(sBuf)-1);
  if (!n) return;
  if (sBuf[n-1] == '\r') n--;
  sBuf[n] = 0;

  if (sBuf[0]=='D' && (sBuf[1]=='0' || sBuf[1]=='1')){
    #if DEBUG
      dbg = (sBuf[1]=='1');
    #endif
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='T') { // Output mode
    int v = atoi(&sBuf[1]); v = constrain(v,0,2);
    outMode = (uint8_t)v; markDirty();
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='J') { // D0/D1 LEDs
    int v = atoi(&sBuf[1]); v = constrain(v,0,1);
    useD01AsLEDs = (uint8_t)v; EEPROM.update(EE_D01LED, useD01AsLEDs);
    if (!useD01AsLEDs) { DDRD &= ~(_BV(LED4_PD_BIT)|_BV(LED5_PD_BIT)); }
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='Y'){ // Gate duty
    int v = atoi(&sBuf[1]); v = constrain(v,1,99);
    gateDuty = (uint8_t)v; markDirty();
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='X' && sBuf[1]==0){
    switch (rstAction){
      case RIN_RESET: handleResetEdgeISR(); break;
      case RIN_FILL:  fillin_step(); uiDirty=true; break;
      case RIN_GEN:   genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step); uiDirty=true; break;
      case RIN_MUT:   mutatePatternAll(); uiDirty=true; break;
    }
    #if DEBUG
      if (dbg) { Serial.print(F("D:RST action=")); Serial.println((int)rstAction); }
    #endif
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='G' && sBuf[1]==0){ genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step); uiDirty=true; Serial.println(F("OK")); return; }
  if (sBuf[0]=='U' && sBuf[1]=='?'){ // dump USER base + fill
    uint16_t vb[6], vf[6];
    for (uint8_t i=0;i<6;i++){
      vb[i] = ((uint16_t)EEPROM.read(EE_UBASE + i*2) << 8) | EEPROM.read(EE_UBASE + i*2 + 1);
      vf[i] = ((uint16_t)EEPROM.read(EE_UFILL + i*2) << 8) | EEPROM.read(EE_UFILL + i*2 + 1);
    }
    dump6hex("U=", vb[0],vb[1],vb[2],vb[3],vb[4],vb[5]);
    dump6hex("UF=",vf[0],vf[1],vf[2],vf[3],vf[4],vf[5]);
    return;
  }
  if (sBuf[0]=='U' && sBuf[1]=='='){ // write USER BASE
    uint16_t v[6];
    if (parse6hex(&sBuf[2], v)){ saveUserBase(v); Serial.println(F("OK")); } else Serial.println(F("ERR"));
    return;
  }
  if (sBuf[0]=='U' && sBuf[1]=='F' && sBuf[2]=='='){ // write USER FILL
    uint16_t v[6];
    if (parse6hex(&sBuf[3], v)){ saveUserFill(v); Serial.println(F("OK")); } else Serial.println(F("ERR"));
    return;
  }
  if (sBuf[0]=='U' && sBuf[1]==0){ // convenience: set USER BASE = current pattern
    uint16_t v[6]={ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step};
    saveUserBase(v); Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='V' && sBuf[1]==0){ // convenience: set USER FILL = current pattern
    uint16_t v[6]={ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step};
    saveUserFill(v); Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='U' && sBuf[1]=='F' && sBuf[2]==0){ // UF (no '='): set FILL = current
    uint16_t v[6]={ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step};
    saveUserFill(v); Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='U' && sBuf[1]=='B' && sBuf[2]==0){ // UB: set BASE = current
    uint16_t v[6]={ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step};
    saveUserBase(v); Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='B'){
    int v = atoi(&sBuf[1]); v = constrain(v,60,240);
    if (v != bpm){ bpm=v; markDirty(); recalcTimer1(); }
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='K'){
    clkSource = (sBuf[1]=='1') ? 1 : 0; markDirty();
    if (clkSource==CLK_INT) internalClockStart(); else internalClockStop();
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='M'){ mode = (sBuf[1]=='1') ? 1 : 0; markDirty(); Serial.println(F("OK")); return; }
  if (sBuf[0]=='S'){ // Style 0..7 (S7=USER)
    int v = atoi(&sBuf[1]);
    genre = constrain(v,0,7);
    markDirty();
    Serial.println(F("OK")); return;
  }
  if (sBuf[0]=='F'){ fillin = (sBuf[1]=='1'); markDirty(); Serial.println(F("OK")); return; }
  if (sBuf[0]=='R'){ int v = atoi(&sBuf[1]); repeat = constrain(v,0,4); markDirty(); Serial.println(F("OK")); return; }
  if (sBuf[0]=='W'){ int v = atoi(&sBuf[1]); sw = constrain(v,0,4); markDirty(); Serial.println(F("OK")); return; }
  if (sBuf[0]=='O'){ oledFlip = (sBuf[1]=='1')?1:0; EEPROM.update(EE_OLED_ROT, oledFlip); applyOLEDflip(oledFlip); uiDirty=true; Serial.println(F("OK")); return; }
  if (sBuf[0]=='E'){ bool ccw = (sBuf[1]=='1'); encDir = ccw?-1:+1; EEPROM.update(EE_ENC_DIR, ccw?1:0); Serial.println(F("OK")); return; }
  if (sBuf[0]=='N'){ int v = atoi(&sBuf[1]); v = constrain(v,0,3); EEPROM.update(EE_RST, (uint8_t)v); Serial.println(F("OK")); return; }
  if (sBuf[0]=='P' && sBuf[1]=='?'){
    dump6hex("P=", ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step);
    return;
  }
  if (sBuf[0]=='P' && sBuf[1]=='='){
    uint16_t v[6];
    if (parse6hex(&sBuf[2], v)){
      ch1_step=v[0]; ch2_step=v[1]; ch3_step=v[2];
      ch4_step=v[3]; ch5_step=v[4]; ch6_step=v[5];
      save_data(); uiDirty=true; Serial.print(F("OK\n"));
    } else Serial.print(F("ERR\n"));
    return;
  }
  if (sBuf[0]=='C' && sBuf[1]=='?'){
    Serial.print(F("C="));
    Serial.print((int)bpm);       Serial.print(',');
    Serial.print((int)clkSource); Serial.print(',');
    Serial.print((int)mode);      Serial.print(',');
    Serial.print((int)genre);     Serial.print(',');
    Serial.print((int)fillin);    Serial.print(',');
    Serial.print((int)repeat);    Serial.print(',');
    Serial.print((int)sw);        Serial.print(',');
    Serial.print((encDir==-1)?1:0); Serial.print(',');
    Serial.print((int)oledFlip);  Serial.print(',');
    Serial.print((int)EEPROM.read(EE_RST)); Serial.print(',');
    Serial.print((int)fillPeriodIdx); Serial.print(',');
    Serial.print((int)outMode);   Serial.print(',');
    Serial.print((int)gateDuty);  Serial.print(',');
    Serial.println((int)useD01AsLEDs);
    return;
  }
  if (sBuf[0]=='Q' && sBuf[1]==0){
    EEPROM.update(EE_OLED_ROT,0);
    EEPROM.update(EE_ENC_DIR,0);
    EEPROM.update(EE_CLK_SRC,CLK_EXT);
    EEPROM.update(EE_TEMPO,120);
    EEPROM.update(EE_GENRE,0);
    EEPROM.update(EE_MODE,0);
    EEPROM.update(EE_REPEAT,2);
    EEPROM.update(EE_FILLIN,1);
    EEPROM.update(EE_SW,0);
    EEPROM.update(EE_RST,RIN_RESET);
    EEPROM.update(EE_FPER,2);
    EEPROM.update(EE_OMODE,OUT_TRG);
    EEPROM.update(EE_TIMEOUT,5);
    EEPROM.update(EE_DUTY,50);
    EEPROM.update(EE_D01LED,0);

    // Reset USER slots to defaults (TECHNO row0 base/fill)
    const uint16_t* p = &bnk1_ptn[0][0];
    for (uint8_t i=0;i<6;i++){
      uint16_t vb = pgm_read_word(&p[i]);
      EEPROM.update(EE_UBASE + i*2, highByte(vb));
      EEPROM.update(EE_UBASE + i*2 + 1, lowByte(vb));
      uint16_t vf = pgm_read_word(&p[6+i]);
      EEPROM.update(EE_UFILL + i*2, highByte(vf));
      EEPROM.update(EE_UFILL + i*2 + 1, lowByte(vf));
    }

    Serial.println(F("OK")); return;
  }
}

// ---------------- USER slot I/O -----------------
void loadUserBase(){
  ch1_step = ((uint16_t)EEPROM.read(EE_UBASE+0) << 8) | EEPROM.read(EE_UBASE+1);
  ch2_step = ((uint16_t)EEPROM.read(EE_UBASE+2) << 8) | EEPROM.read(EE_UBASE+3);
  ch3_step = ((uint16_t)EEPROM.read(EE_UBASE+4) << 8) | EEPROM.read(EE_UBASE+5);
  ch4_step = ((uint16_t)EEPROM.read(EE_UBASE+6) << 8) | EEPROM.read(EE_UBASE+7);
  ch5_step = ((uint16_t)EEPROM.read(EE_UBASE+8) << 8) | EEPROM.read(EE_UBASE+9);
  ch6_step = ((uint16_t)EEPROM.read(EE_UBASE+10)<< 8) | EEPROM.read(EE_UBASE+11);
}
void loadUserFill(){
  ch1_step = ((uint16_t)EEPROM.read(EE_UFILL+0) << 8) | EEPROM.read(EE_UFILL+1);
  ch2_step = ((uint16_t)EEPROM.read(EE_UFILL+2) << 8) | EEPROM.read(EE_UFILL+3);
  ch3_step = ((uint16_t)EEPROM.read(EE_UFILL+4) << 8) | EEPROM.read(EE_UFILL+5);
  ch4_step = ((uint16_t)EEPROM.read(EE_UFILL+6) << 8) | EEPROM.read(EE_UFILL+7);
  ch5_step = ((uint16_t)EEPROM.read(EE_UFILL+8) << 8) | EEPROM.read(EE_UFILL+9);
  ch6_step = ((uint16_t)EEPROM.read(EE_UFILL+10)<< 8) | EEPROM.read(EE_UFILL+11);
}
void saveUserBase(uint16_t v[6]){
  for (uint8_t i=0;i<6;i++){
    EEPROM.update(EE_UBASE + i*2,   highByte(v[i]));
    EEPROM.update(EE_UBASE + i*2+1, lowByte (v[i]));
  }
}
void saveUserFill(uint16_t v[6]){
  for (uint8_t i=0;i<6;i++){
    EEPROM.update(EE_UFILL + i*2,   highByte(v[i]));
    EEPROM.update(EE_UFILL + i*2+1, lowByte (v[i]));
  }
}

// ---------------- setup -----------------
void setup() {
  serialInit();

  // pins
  pinMode(13, INPUT);
  pinMode(11, INPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);  // A3 LED6

  // D0/D1 remain inputs unless enabled
  DDRD &= ~(_BV(LED4_PD_BIT)|_BV(LED5_PD_BIT));

  Wire.begin();
  Wire.setClock(400000L);
  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.clear();

  // load saved current pattern
  ch1_step = EEPROM.read(1)*256 + EEPROM.read(2);
  ch2_step = EEPROM.read(3)*256 + EEPROM.read(4);
  ch3_step = EEPROM.read(5)*256 + EEPROM.read(6);
  ch4_step = EEPROM.read(7)*256 + EEPROM.read(8);
  ch5_step = EEPROM.read(9)*256 + EEPROM.read(10);
  ch6_step = EEPROM.read(11)*256 + EEPROM.read(12);

  if (EEPROM.read(EE_SIG_ADDR) != EE_SIG_VAL) {
    EEPROM.update(EE_SIG_ADDR, EE_SIG_VAL);
    EEPROM.update(EE_OLED_ROT, 0);
    EEPROM.update(EE_ENC_DIR, 0);
    EEPROM.update(EE_CLK_SRC, CLK_EXT);
    EEPROM.update(EE_TEMPO, 120);
    EEPROM.update(EE_GENRE, 0);
    EEPROM.update(EE_MODE, 0);
    EEPROM.update(EE_REPEAT, 2);
    EEPROM.update(EE_FILLIN, 1);
    EEPROM.update(EE_SW, 0);
    EEPROM.update(EE_RST, RIN_RESET);
    EEPROM.update(EE_FPER, 2);
    EEPROM.update(EE_OMODE, OUT_TRG);
    EEPROM.update(EE_TIMEOUT, 5);
    EEPROM.update(EE_DUTY, 50);
    EEPROM.update(EE_D01LED, 0);

    // initialize current pattern to first entry of bank1 base
    const uint16_t* p = &bnk1_ptn[0][0];
    for (uint8_t i=0;i<6;i++){
      uint16_t v = pgm_read_word(&p[i]);
      EEPROM.update(1 + i*2, highByte(v));
      EEPROM.update(2 + i*2, lowByte(v));
    }
    ch1_step = pgm_read_word(&p[0]);
    ch2_step = pgm_read_word(&p[1]);
    ch3_step = pgm_read_word(&p[2]);
    ch4_step = pgm_read_word(&p[3]);
    ch5_step = pgm_read_word(&p[4]);
    ch6_step = pgm_read_word(&p[5]);

    // Initialize USER BASE/FILL to TECHNO row0 base/fill
    for (uint8_t i=0;i<6;i++){
      uint16_t vb = pgm_read_word(&p[i]);
      EEPROM.update(EE_UBASE + i*2,   highByte(vb));
      EEPROM.update(EE_UBASE + i*2+1, lowByte (vb));
      uint16_t vf = pgm_read_word(&p[6+i]);
      EEPROM.update(EE_UFILL + i*2,   highByte(vf));
      EEPROM.update(EE_UFILL + i*2+1, lowByte (vf));
    }
  }

  // load config/state
  oledFlip      = EEPROM.read(EE_OLED_ROT) ? 1 : 0; applyOLEDflip(oledFlip);
  encDir        = EEPROM.read(EE_ENC_DIR) ? -1 : +1;
  clkSource     = EEPROM.read(EE_CLK_SRC);
  bpm           = constrain(EEPROM.read(EE_TEMPO),60,240);
  genre         = constrain(EEPROM.read(EE_GENRE),0,7);
  mode          = constrain(EEPROM.read(EE_MODE),0,1);
  repeat        = constrain(EEPROM.read(EE_REPEAT),0,4);
  fillin        = EEPROM.read(EE_FILLIN)?1:0;
  sw            = constrain(EEPROM.read(EE_SW),0,4);
  rstAction     = constrain(EEPROM.read(EE_RST),0,3);
  fillPeriodIdx = constrain(EEPROM.read(EE_FPER),0,4);
  outMode       = constrain(EEPROM.read(EE_OMODE),0,2);
  menuTimeoutSec= constrain(EEPROM.read(EE_TIMEOUT),1,30);
  gateDuty      = EEPROM.read(EE_DUTY); gateDuty = constrain(gateDuty?gateDuty:50,1,99);
  useD01AsLEDs  = EEPROM.read(EE_D01LED)?1:0;
  if (!useD01AsLEDs) DDRD &= ~(_BV(LED4_PD_BIT)|_BV(LED5_PD_BIT));

  // PCINT for CLK/RESET (PINB)
  PCICR |= _BV(PCIE0);
  PCMSK0 |= _BV(CLK_PB_BIT) | _BV(RST_PB_BIT);

  if (clkSource == CLK_INT) internalClockStart(); else internalClockStop();

  encBtn.useQuadPrecision(false);
  encBtn.setDebounceInterval(10);
  encBtn.setEncoderHandler(onRotate);
  encBtn.setEncoderPressedHandler(onRotatePressed);
  encBtn.setClickHandler(onClick);

  // Secret menu on boot-hold
  pinMode(12, INPUT_PULLUP);
  unsigned long t0 = millis();
  while (digitalRead(12) == LOW) {
    if (millis() - t0 >= SECRET_MENU_BOOT_HOLD_MS) {
      secretMenuActive = true;
      secretIndex = 0;
      secretEncOld = encBtn.position();
      drawSecretMenu(0);
      break;
    }
  }

  // Seed LFSR
  lfsr = analogRead(A0) ^ (analogRead(A1) << 8);
  if (lfsr == 0) lfsr = 0xACE1;

  uiDirty = true;
  lastUIActivityMs = millis();
}

// ---------- encoder helpers ----------
static inline bool isMenuItem(uint8_t e) {
  if (e < ENC_STYLE || e > ENC_LAST) return false;
  return true;
}
static inline void wrapToValid() {
  if (enc >= STEP_MIN && enc <= STEP_MAX) return;
  if (enc < ENC_STYLE) enc = ENC_STYLE;
  if (enc > ENC_LAST) enc = ENC_STYLE;
  if (!isMenuItem(enc)) {
    uint8_t tries = 0;
    while (!isMenuItem(enc) && tries++ < 13) {
      enc++;
      if (enc > ENC_LAST) enc = ENC_STYLE;
    }
  }
}
static inline void stepEnc(int incRaw) {
  unsigned long now = millis();
  if ((now - lastEncHandledMs) < ENC_RATE_MS) return;
  lastEncHandledMs = now;
  int inc = incRaw * (encDir > 0 ? +1 : -1);
  if (enc >= STEP_MIN && enc <= STEP_MAX) {
    int v = (int)enc + inc;
    if (v < STEP_MIN) v = ENC_LAST;
    if (v > STEP_MAX) v = ENC_STYLE;
    enc = (uint8_t)v;
  } else {
    int v = (int)enc + inc;
    if (v < ENC_STYLE) v = STEP_MAX;
    if (v > ENC_LAST) v = STEP_MIN;
    enc = (uint8_t)v;
    if (enc >= ENC_STYLE && enc <= ENC_LAST) wrapToValid();
  }
  uiDirty = true;
}

// ---------------- loop -----------------
void loop() {
  #if DEBUG
  if (dbg) {
    if (dbg_rst_evt != 255) {
      Serial.print(F("D:RST action="));
      Serial.println((int)dbg_rst_evt);
      dbg_rst_evt = 255;
    }
  }
  #endif

  encBtn.update();

  // enter secret menu by long-hold during runtime
  static bool rtWasPressed = false;
  static unsigned long rtStart = 0;
  bool p = encBtn.isPressed();
  if (p && !rtWasPressed) { rtStart = millis(); }
  if (p && !secretMenuActive && (millis() - rtStart) >= SECRET_MENU_RUNTIME_HOLD_MS) {
    secretMenuActive = true;
    secretIndex = 0;
    secretEncOld = encBtn.position();
    drawSecretMenu(0);
  }
  rtWasPressed = p;

  if (secretMenuActive) { secretMenuLoop(); return; }

  serialPoll();

  // idle toggle for timeout bar
  hideUI = (millis() - lastUIActivityMs) >= (unsigned long)menuTimeoutSec * 1000UL;

  // deferred RST actions
  if (rstActionFlag) {
    uint8_t a = rstActionFlag; rstActionFlag = 0;
    if (a == RIN_FILL)      fillin_step();
    else if (a == RIN_GEN)  genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step);
    else if (a == RIN_MUT)  mutatePatternAll();
    uiDirty = true;
  }

  if (resetEdgeFlag) {
    noInterrupts(); resetEdgeFlag = 0; interrupts();
    step_count = 0; uiDirty = true;
  }

  if (stepEdgeFlag) {
    uint8_t wrapped;
    noInterrupts(); stepEdgeFlag = 0; wrapped = wrappedEdge; wrappedEdge = 0; interrupts();
    uiDirty = true;

    if (wrapped) {
      switch (repeat){ case 0: repeat_max=4; break; case 1: repeat_max=8; break; case 2: repeat_max=16; break; case 3: repeat_max=32; break; default: repeat_max=10000; break; }
      switch (sw)    { case 0: sw_max=2;   break; case 1: sw_max=4;  break; case 2: sw_max=8;  break; case 3: sw_max=16; break; default: sw_max=255; break; }

      // revert preset FILL -> BASE
      if (fillActive) {
        if (genre <= 3){
          uint8_t idx = (genre==0)?change_bnk1:(genre==1)?change_bnk2:(genre==2)?change_bnk3:change_bnk4;
          loadPresetBase(genre, idx);
        } else if (genre == 5) { // AMBIENT
          loadPresetBase(3, 7);
        } else if (genre == 6) { // DNB
          loadPresetBase(2, 7);
        } else if (genre == 7) { // USER
          loadUserBase();
        }
        fillActive = false;
      }

      if (mode == 1) {
        repeat_done++;

        if (repeat_done >= repeat_max) {
          sw_done++;
          repeat_done = 0;
          change_step();
          if (sw_done >= sw_max) sw_done = 0;
        } else {
          if (fillin){
            uint8_t per = fillPeriodFromIdx(fillPeriodIdx);
            if (per>0 && (repeat_done % per)==0){
              if (genre <= 3){
                uint8_t idx = (genre==0)?change_bnk1:(genre==1)?change_bnk2:(genre==2)?change_bnk3:change_bnk4;
                loadPresetFill(genre, idx);
                fillActive = true;
              } else if (genre == 5) {
                loadPresetFill(3, 7);  // AMBIENT
                fillActive = true;
              } else if (genre == 6) {
                loadPresetFill(2, 7);  // DNB
                fillActive = true;
              } else if (genre == 7) {
                loadUserFill();        // USER
                fillActive = true;
              } else {
                // GEN: algorithmic fill
                fillin_step();
              }
            }
          }
        }
      }
    }
  }

  // TRIG/GATE off scheduling
  if ((outMode == OUT_TRG || outMode == OUT_GAT) && trigActiveMask) {
    unsigned long us = micros();
    uint8_t mask = trigActiveMask;
    for (uint8_t i=0;i<6;i++){
      if (mask & (1<<i)) {
        if ((long)(us - trigOffAtUs[i]) >= 0) {
          uint8_t newMask;
          noInterrupts();
          outBits &= ~(1<<i);
          newMask = outBits;
          trigActiveMask &= ~(1<<i);
          interrupts();
          writeOutputsFast(newMask);
          writeLEDsFast(newMask);
        }
      }
    }
  }

  if (uiDirty && (long)(millis() - nextOledMs) >= 0) {
    OLED_display();
    uiDirty = false;
    nextOledMs = millis() + OLED_INTERVAL_MS;
  }

  // Deferred EEPROM writes — only when idle (no steps for >1s)
  if (eepromDirty && (millis() - lastEepromWriteMs) >= EEPROM_WRITE_DELAY_MS) {
    if ((millis() - lastStepEdgeMs) > 1000UL) {
      save_data();
    } else {
      lastEepromWriteMs = millis();
    }
  }
}

// --------- bottom menu build/draw ----------
static inline const __FlashStringHelper* genreFullF() {
  switch (genre) {
    case 0: return F("TECHNO");
    case 1: return F("DUB");
    case 2: return F("HOUSE");
    case 3: return F("HALF");
    case 4: return F("GEN");
    case 5: return F("AMBIENT");
    case 6: return F("DNB");
    case 7: return F("USER");
    default: return F("GEN");
  }
}
static inline const __FlashStringHelper* repTokenF() {
  switch (repeat) { case 0: return F("4"); case 1: return F("8"); case 2: return F("16"); case 3: return F("32"); default: return F("ET"); }
}
static inline const __FlashStringHelper* swTokenF() {
  switch (sw) { case 0: return F("2"); case 1: return F("4"); case 2: return F("8"); case 3: return F("16"); default: return F("ET"); }
}
static inline char rinChar(uint8_t a) { return (a==RIN_RESET)?'R':(a==RIN_FILL)?'F':(a==RIN_GEN)?'G':'M'; }
static inline const char* outModeStr(uint8_t m){
  switch (m){ case OUT_GAT: return "GAT"; case OUT_FF: return "FF"; default: return "TRG"; }
}

uint8_t bottomItemCount() { return (mode==1) ? 12 : 10; }
uint8_t bottomItemIdAt(uint8_t idx) {
  if (mode==1){
    static const uint8_t a[12] = {
      ENC_STYLE,ENC_FILLIN,ENC_FPER,ENC_CLK,ENC_BPM,ENC_REP,
      ENC_SW,ENC_RIN,ENC_GEN,ENC_MUT,ENC_OMODE,ENC_DUTY
    };
    return a[idx];
  }
  static const uint8_t b[10] = {
    ENC_STYLE,ENC_CLK,ENC_BPM,ENC_REP,ENC_SW,ENC_RIN,
    ENC_GEN,ENC_MUT,ENC_FILLIN,ENC_OMODE
  };
  return b[idx];
}
uint8_t bottomItemWidthCh(uint8_t id) {
  switch (id) {
    case ENC_STYLE: return 8; // enough for "AMBIENT"
    case ENC_FILLIN:return 6;
    case ENC_FPER:  return 7;
    case ENC_CLK:   return 5;
    case ENC_BPM:   return (bpm < 100) ? 7 : 8;
    case ENC_REP:   return 6;
    case ENC_SW:    return 5;
    case ENC_RIN:   return 5;
    case ENC_GEN:   return 3;
    case ENC_MUT:   return 3;
    case ENC_OMODE: return 7;
    case ENC_DUTY:  return 8;
    default:        return 3;
  }
}
uint8_t buildItemLabel(uint8_t id, char* out) {
  switch (id) {
    case ENC_STYLE: {
      if (mode==0) { strcpy(out,"MANU"); return 4; }
      const __FlashStringHelper* p = genreFullF();
      strcpy_P(out,(PGM_P)p);
      return (uint8_t)strlen(out);
    }
    case ENC_FILLIN: { sprintf(out,"FILL:%c", fillin?'Y':'N'); return 6; }
    case ENC_FPER:   { uint8_t per=fillPeriodFromIdx(fillPeriodIdx); sprintf(out,"F-EV:%u", per); return (uint8_t)strlen(out); }
    case ENC_CLK:    { sprintf(out,"CLK:%c",(clkSource==CLK_EXT)?'E':'I'); return 5; }
    case ENC_BPM:    { sprintf(out,"BPM:%u",(unsigned)bpm); return (uint8_t)strlen(out); }
    case ENC_REP:    { const __FlashStringHelper* p=repTokenF(); char b[3]; strcpy_P(b,(PGM_P)p);
                       if (b[1]) sprintf(out,"REP:%c%c",b[0],b[1]); else sprintf(out,"REP:%c",b[0]); return (uint8_t)strlen(out); }
    case ENC_SW:     { const __FlashStringHelper* p=swTokenF(); char b[3]; strcpy_P(b,(PGM_P)p);
                       if (b[1]) sprintf(out,"SW:%c%c",b[0],b[1]); else sprintf(out,"SW:%c",b[0]); return (uint8_t)strlen(out); }
    case ENC_RIN:    { sprintf(out,"IN:%c", rinChar(rstAction)); return 5; }
    case ENC_GEN:    strcpy(out,"GEN"); return 3;
    case ENC_MUT:    strcpy(out,"MUT"); return 3;
    case ENC_OMODE:  { sprintf(out,"OUT:%s", outModeStr(outMode)); return (uint8_t)strlen(out); }
    case ENC_DUTY:   { sprintf(out,"DUTY:%u",(unsigned)gateDuty); return (uint8_t)strlen(out); }
  }
  out[0]=0; return 0;
}
void drawBottomMenu() {
  uint8_t N = bottomItemCount();
  int total = 0, selL = 0, selR = 0;
  char buf[16];
  for (uint8_t i=0;i<N;i++){
    uint8_t id = bottomItemIdAt(i);
    uint8_t w  = buildItemLabel(id, buf);
    if (id == enc) { selL = total; selR = total + w; }
    total += w; if (i != N-1) total += 1;
  }
  if (selL < menuScrollCol) menuScrollCol = selL;
  if (selR > (menuScrollCol + 16)) menuScrollCol = selR - 16;
  if (menuScrollCol < 0) menuScrollCol = 0;
  int maxScroll = (total > 16) ? (total - 16) : 0;
  if (menuScrollCol > maxScroll) menuScrollCol = maxScroll;

  u8x8.setCursor(0, 7);
  int pos = 0;
  for (uint8_t i=0;i<N;i++){
    uint8_t id = bottomItemIdAt(i);
    uint8_t w  = buildItemLabel(id, buf);
    for (uint8_t k=0;k<w;k++,pos++){
      if (pos >= menuScrollCol && pos < menuScrollCol + 16) {
        u8x8.setInverseFont((id == enc) ? 1 : 0);
        u8x8.write(buf[k]);
      }
    }
    if (i != N-1) {
      if (pos >= menuScrollCol && pos < menuScrollCol + 16) {
        u8x8.setInverseFont(0); u8x8.write(' ');
      }
      pos++;
    }
  }
  u8x8.setInverseFont(0);
  for (int c=(total - menuScrollCol); c<16; c++) u8x8.write(' ');
}

// ---------------- OLED/UI ----------------
void drawGrid() {
  for (uint8_t r=0;r<6;r++){
    u8x8.setCursor(0, r);
    for (uint8_t c=0;c<16;c++){
      uint8_t idx = r*16 + c + 1;
      uint8_t bitIndex = 15 - c;
      uint8_t v = 0;
      switch (r) {
        case 0: v = bitRead(ch1_step, bitIndex); break;
        case 1: v = bitRead(ch2_step, bitIndex); break;
        case 2: v = bitRead(ch3_step, bitIndex); break;
        case 3: v = bitRead(ch4_step, bitIndex); break;
        case 4: v = bitRead(ch5_step, bitIndex); break;
        default: v = bitRead(ch6_step, bitIndex); break;
      }
      if (idx == enc) u8x8.setInverseFont(1); else u8x8.setInverseFont(0);
      u8x8.write(v ? '*' : '.');
    }
  }
  u8x8.setInverseFont(0);
}
void drawCaret() {
  u8x8.setCursor(0, 6);
  for (uint8_t c=0;c<16;c++) u8x8.write((c == (step_count - 1)) ? '^' : ' ');
}

// Progress/status bar (AUTO) or status (MANU)
void drawIdleBar(){
  u8x8.setInverseFont(0);
  u8x8.setCursor(0,7);

  if (mode==0){ // MANUAL
    char s[17];
    sprintf(s,"BPM:%03u CLK:%c",(unsigned)bpm,(clkSource==CLK_EXT)?'E':'I');
    for (uint8_t i=strlen(s); i<16; ++i) s[i]=' ';
    s[16]=0; u8x8.print(s); return;
  }

  const uint16_t rmax = (repeat<=3)? (4u<<repeat) : 0u;
  const uint8_t  segs = (sw<=3)? (uint8_t)(2u<<sw) : 0u;
  const uint8_t  per  = fillPeriodFromIdx(fillPeriodIdx);

  if (rmax==0 || segs==0){
    char s[17]; sprintf(s,"SW:%s FILL:%c", (sw<=3)? "??":"ET", fillin?'Y':'N');
    for (uint8_t i=strlen(s); i<16; ++i) s[i]=' ';
    s[16]=0; u8x8.print(s); return;
  }

  uint8_t filled = (uint8_t)((repeat_done * 16UL) / rmax);

  int8_t fpos = -1;
  if (fillin && per>0) {
    uint16_t nextFill = ((repeat_done / per) + 1U) * per; // 1-based
    if (nextFill <= rmax) {
      uint16_t tn = (uint16_t)((nextFill * 16UL) / rmax);
      fpos = (int8_t)((tn>0)? (tn-1) : 0);
      if (fpos>15) fpos=15;
    }
  }

  uint8_t segW = (segs>0) ? (16/segs) : 16;

  for (uint8_t c=0;c<16;c++){
    char ch = (c < filled) ? '#' : '.';
    if (segW && ((c % segW) == segW-1) && c!=15) ch='|'; // segment boundary
    if (c == (uint8_t)fpos) ch = 'F';                    // next fill cycle
    if (c==15) ch='S';                                   // next switch boundary
    u8x8.write(ch);
  }
}

void OLED_display() {
  drawGrid();
  drawCaret();
  if (hideUI) drawIdleBar(); else drawBottomMenu();
}

// ------------- preset loaders -------------
void loadPreset(uint8_t bank, uint8_t idx, bool isFill){
  const uint16_t* p = nullptr;
  uint8_t offset = isFill ? 6 : 0;
  switch (bank){
    case 0: p = &bnk1_ptn[idx][offset]; break;
    case 1: p = &bnk2_ptn[idx][offset]; break;
    case 2: p = &bnk3_ptn[idx][offset]; break;
    default: p = &bnk4_ptn[idx][offset]; break;
  }
  ch1_step=pgm_read_word(&p[0]); ch2_step=pgm_read_word(&p[1]); ch3_step=pgm_read_word(&p[2]);
  ch4_step=pgm_read_word(&p[3]); ch5_step=pgm_read_word(&p[4]); ch6_step=pgm_read_word(&p[5]);
}
static inline void loadPresetBase(uint8_t bank, uint8_t idx) { loadPreset(bank, idx, false); }
static inline void loadPresetFill(uint8_t bank, uint8_t idx) { loadPreset(bank, idx, true); }

// ------------- save/pattern ops -------------
static inline void markDirty() { eepromDirty = true; lastEepromWriteMs = millis(); }
void save_data() {
  EEPROM.update(1, highByte(ch1_step));  EEPROM.update(2, lowByte(ch1_step));
  EEPROM.update(3, highByte(ch2_step));  EEPROM.update(4, lowByte(ch2_step));
  EEPROM.update(5, highByte(ch3_step));  EEPROM.update(6, lowByte(ch3_step));
  EEPROM.update(7, highByte(ch4_step));  EEPROM.update(8, lowByte(ch4_step));
  EEPROM.update(9, highByte(ch5_step));  EEPROM.update(10, lowByte(ch5_step));
  EEPROM.update(11, highByte(ch6_step)); EEPROM.update(12, lowByte(ch6_step));
  EEPROM.update(EE_GENRE,  genre);
  EEPROM.update(EE_MODE,   mode);
  EEPROM.update(EE_REPEAT, repeat);
  EEPROM.update(EE_FILLIN, fillin?1:0);
  EEPROM.update(EE_SW,     sw);
  EEPROM.update(EE_TEMPO,  bpm);
  EEPROM.update(EE_CLK_SRC,clkSource);
  EEPROM.update(EE_FPER,   fillPeriodIdx);
  EEPROM.update(EE_OMODE,  outMode);
  EEPROM.update(EE_DUTY,   gateDuty);
  EEPROM.update(EE_D01LED, useD01AsLEDs);
  eepromDirty = false;
}

// --------- AUTO change logic -------------
void change_step() {
  if (genre == 4) { // GEN
    genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step);
    return;
  }
  uint8_t idx;
  if (genre==0){ if (sw_done >= sw_max) change_bnk1 = fastRandom8() & 7; idx = change_bnk1; loadPresetBase(0, idx); return; }
  if (genre==1){ if (sw_done >= sw_max) change_bnk2 = fastRandom8() & 7; idx = change_bnk2; loadPresetBase(1, idx); return; }
  if (genre==2){ if (sw_done >= sw_max) change_bnk3 = fastRandom8() & 7; idx = change_bnk3; loadPresetBase(2, idx); return; }
  if (genre==3){ if (sw_done >= sw_max) change_bnk4 = fastRandom8() & 7; idx = change_bnk4; loadPresetBase(3, idx); return; }

  // Fixed selections:
  if (genre==5) { loadPresetBase(3, 7); return; } // AMBIENT = bank4 row 7
  if (genre==6) { loadPresetBase(2, 7); return; } // DNB     = bank3 row 7
  if (genre==7) { loadUserBase();      return; }  // USER    = EEPROM slot
}

// Algorithmic fill (GEN)
static inline void makeFillFromBase() {
  ch2_step = (uint16_t)(ch2_step | (ch2_step >> 1) | (ch2_step << 15));
  ch3_step ^= (uint16_t)((1u<<11)|(1u<<3));
  ch4_step ^= (uint16_t)(ch4_step >> 2);
  if (fastRandomChance(30)) ch5_step |= (uint16_t)((1u<<15)|(1u<<7));
}
static inline void makeFillFromBase2() {
  ch2_step ^= (uint16_t)((ch2_step<<1) | (ch2_step>>15));
  ch3_step |= (uint16_t)((1u<<12)|(1u<<2));
  ch4_step ^= (uint16_t)((ch4_step>>1) & 0x3333u);
  if (fastRandomChance(25)) ch5_step ^= (uint16_t)(0x00F0u);
}
void fillin_step() {
  if (genre == 4) {
    static bool alt=false;
    if (!alt) makeFillFromBase();
    else      makeFillFromBase2();
    alt = !alt;
  } else {
    // banks 0..3 + fixed 5/6 handled via preset rows; USER handled via loadUserFill at wrap
  }
}

// --------- EncoderButton handlers ---------
static void hardResetSequencerLikeISR() {
  noInterrupts();
  step_count = 0;
  outBits = 0;
  trigActiveMask = 0;
  interrupts();
  writeOutputsFast(0);
  writeLEDsFast(0);
  repeat_done = 0;
  sw_done = 0;
  uiDirty = true;
}
void onRotate(EncoderButton& eb) { lastUIActivityMs = millis(); hideUI = false; stepEnc(eb.increment()); }
void onRotatePressed(EncoderButton& eb) {
  lastUIActivityMs = millis(); hideUI = false;
  int inc = eb.increment();
  unsigned long now = millis();
  if ((now - lastEncHandledMs) < ENC_RATE_MS) return;
  lastEncHandledMs = now;

  if (enc == ENC_BPM) {
    int newBpm = (int)bpm + inc * (encDir > 0 ? +1 : -1);
    newBpm = constrain(newBpm, 60, 240);
    if (newBpm != (int)bpm) { bpm = (uint16_t)newBpm; markDirty(); recalcTimer1(); }
    uiDirty = true; return;
  }

  // Press+rotate tweaks
  if (enc == ENC_REP || enc == ENC_SW || enc == ENC_FPER || enc == ENC_DUTY || (enc >= STEP_MIN && enc <= STEP_MAX)) {
    if (enc == ENC_REP) {
      int v = repeat + (inc * (encDir>0?+1:-1)); v = (v<0)?4:((v>4)?0:v);
      if (v != repeat){ repeat = v; markDirty(); }
    } else if (enc == ENC_SW) {
      int v = sw + (inc * (encDir>0?+1:-1)); v = (v<0)?4:((v>4)?0:v);
      if (v != sw){ sw = v; markDirty(); }
    } else if (enc == ENC_FPER) {
      int v = fillPeriodIdx + (inc * (encDir>0?+1:-1)); v = (v<0)?4:((v>4)?0:v);
      if (v != fillPeriodIdx){ fillPeriodIdx = v; markDirty(); }
    } else if (enc == ENC_DUTY) {
      int v = (int)gateDuty + (inc * (encDir>0?+1:-1)); v = constrain(v, 1, 99);
      if (v != (int)gateDuty){ gateDuty = (uint8_t)v; markDirty(); }
    } else {
      // steps: fall-through
    }
    uiDirty = true; return;
  }

  stepEnc(inc);
}
void onClick(EncoderButton& eb) {
  lastUIActivityMs = millis(); hideUI = false;

  if (enc >= STEP_MIN && enc <= STEP_MAX) {
    uint8_t col = (enc - 1) % 16;
    uint8_t bitIndex = 15 - col;
    uint16_t bit = (1U << bitIndex);
    if (enc <= 16)      ch1_step ^= bit;
    else if (enc <= 32) ch2_step ^= bit;
    else if (enc <= 48) ch3_step ^= bit;
    else if (enc <= 64) ch4_step ^= bit;
    else if (enc <= 80) ch5_step ^= bit;
    else                ch6_step ^= bit;
    markDirty();
  } else {
    if (enc == ENC_STYLE) {
      if (mode==0) { mode = 1; markDirty(); change_step(); wrapToValid(); }
      else {
        genre = (genre + 1);
        if (genre > 7) genre = 0;
        markDirty();
        if (genre==4) {
          genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step);
        } else if (genre==5) {
          loadPresetBase(3, 7);     // AMBIENT base
        } else if (genre==6) {
          loadPresetBase(2, 7);     // DNB base
        } else if (genre==7) {
          loadUserBase();           // USER base
        }
      }
    } else if (enc == ENC_FILLIN) {
      fillin = !fillin; markDirty();
    } else if (enc == ENC_FPER) {
      fillPeriodIdx = (fillPeriodIdx + 1) % 5; markDirty();
    } else if (enc == ENC_CLK) {
      clkSource = (clkSource==CLK_EXT)?CLK_INT:CLK_EXT; markDirty();
      if (clkSource==CLK_INT) internalClockStart(); else internalClockStop();
    } else if (enc == ENC_BPM) {
      bpm = 120; markDirty(); recalcTimer1();
    } else if (enc == ENC_REP) {
      repeat++; if (repeat>4) repeat=0; markDirty();
    } else if (enc == ENC_SW) {
      sw++; if (sw>4) sw=0; markDirty();
    } else if (enc == ENC_RIN) {
      rstAction = (rstAction + 1) & 0x03;
    } else if (enc == ENC_GEN) {
      genRandomPattern(ch1_step,ch2_step,ch3_step,ch4_step,ch5_step,ch6_step);
      markDirty();
    } else if (enc == ENC_MUT) {
      mutatePatternAll();
      markDirty();
    } else if (enc == ENC_OMODE) {
      outMode = (outMode + 1) % 3; markDirty();
    } else if (enc == ENC_DUTY) {
      int v = (int)gateDuty + (encDir>0?+1:-1); gateDuty = (uint8_t)constrain(v,1,99); markDirty();
    }
  }
  uiDirty = true;
}

// ---------------- Secret menu ----------------
void secretMenuLoop() {
  long now = encBtn.position();
  long d = now - secretEncOld;
  if (d != 0) {
    int step = (d > 0) ? +1 : -1;
    secretEncOld = now;
    switch (secretIndex) {
      case 0: { uint8_t v = EEPROM.read(EE_OLED_ROT); v = (step>0)?1:0; EEPROM.update(EE_OLED_ROT,v); oledFlip=v; applyOLEDflip(v);} break;
      case 1: { encDir = (step>0)?+1:-1; EEPROM.update(EE_ENC_DIR,(encDir==-1)?1:0);} break;
      case 2: { clkSource = (step>0)?CLK_INT:CLK_EXT; EEPROM.update(EE_CLK_SRC,clkSource);
                if (clkSource==CLK_INT) internalClockStart(); else internalClockStop(); } break;
      case 3: { int v = (int)EEPROM.read(EE_RST) + step; if (v<0) v=3; if (v>3) v=0; EEPROM.update(EE_RST,(uint8_t)v); } break;
      case 4: { int v = (int)menuTimeoutSec + step; v = constrain(v,1,30); menuTimeoutSec = (uint8_t)v; EEPROM.update(EE_TIMEOUT,menuTimeoutSec); } break;
      case 5: /* SAVE(Hold) */ break;
      case 6: /* FACT(Hold) */ break;
      case 7: { useD01AsLEDs = (step>0)?1:0; EEPROM.update(EE_D01LED,useD01AsLEDs);
                if (!useD01AsLEDs) DDRD &= ~(_BV(LED4_PD_BIT)|_BV(LED5_PD_BIT)); } break;
    }
    drawSecretMenu(0);
  }

  bool p = encBtn.isPressed();
  if (p && !secWasPressed) { secPressStart = millis(); }
  if (!p && secWasPressed) {  // short press -> next item
    secretIndex = (secretIndex + 1) % 8;
    drawSecretMenu(0);
  }
  secWasPressed = p;

  if (p) {
    unsigned long held = millis() - secPressStart;
    if (secretIndex == 6 && held >= SECRET_MENU_FACTORY_HOLD_MS) {
      EEPROM.update(EE_OLED_ROT,0);
      EEPROM.update(EE_ENC_DIR,0);
      EEPROM.update(EE_CLK_SRC,CLK_EXT);
      EEPROM.update(EE_TEMPO,120);
      EEPROM.update(EE_GENRE,0);
      EEPROM.update(EE_MODE,0);
      EEPROM.update(EE_REPEAT,2);
      EEPROM.update(EE_FILLIN,1);
      EEPROM.update(EE_SW,0);
      EEPROM.update(EE_RST,RIN_RESET);
      EEPROM.update(EE_FPER,2);
      EEPROM.update(EE_OMODE,OUT_TRG);
      EEPROM.update(EE_TIMEOUT,5);
      EEPROM.update(EE_DUTY,50);
      EEPROM.update(EE_D01LED,0);

      // Reset USER slots
      const uint16_t* p = &bnk1_ptn[0][0];
      for (uint8_t i=0;i<6;i++){
        uint16_t vb = pgm_read_word(&p[i]);
        EEPROM.update(EE_UBASE + i*2,   highByte(vb));
        EEPROM.update(EE_UBASE + i*2+1, lowByte (vb));
        uint16_t vf = pgm_read_word(&p[6+i]);
        EEPROM.update(EE_UFILL + i*2,   highByte(vf));
        EEPROM.update(EE_UFILL + i*2+1, lowByte (vf));
      }

      u8x8.clear(); u8x8.setCursor(5,3); u8x8.print(F("OK")); delay(500);
      secretMenuActive=false; uiDirty=true; return;
    }
    if (secretIndex == 5 && held >= SECRET_MENU_SAVE_HOLD_MS) {
      EEPROM.update(EE_FPER, fillPeriodIdx);
      EEPROM.update(EE_OMODE, outMode);
      EEPROM.update(EE_TIMEOUT, menuTimeoutSec);
      EEPROM.update(EE_DUTY,   gateDuty);
      EEPROM.update(EE_D01LED, useD01AsLEDs);
      u8x8.clear(); u8x8.setCursor(5,3); u8x8.print(F("OK")); delay(400);
      secretMenuActive=false; uiDirty=true; return;
    }
  }
}
void drawSecretMenu(uint8_t) {
  u8x8.clear();
  u8x8.setInverseFont(secretIndex == 0);
  u8x8.setCursor(0, 0); u8x8.print(F("ROT ")); u8x8.print(EEPROM.read(EE_OLED_ROT)?F("180"):F("0"));
  u8x8.setInverseFont(secretIndex == 1);
  u8x8.setCursor(0, 1); u8x8.print(F("ENC ")); u8x8.print(encDir==-1?F("CC"):F("CW"));
  u8x8.setInverseFont(secretIndex == 2);
  u8x8.setCursor(0, 2); u8x8.print(F("CLK ")); u8x8.print(clkSource==CLK_EXT?F("EX"):F("IN"));
  u8x8.setInverseFont(secretIndex == 3);
  u8x8.setCursor(0, 3); u8x8.print(F("RIN ")); u8x8.print(rinChar(EEPROM.read(EE_RST)));
  u8x8.setInverseFont(secretIndex == 4);
  u8x8.setCursor(0, 4); u8x8.print(F("TMO ")); u8x8.print((unsigned)menuTimeoutSec); u8x8.print(F("s"));
  u8x8.setInverseFont(secretIndex == 5);
  u8x8.setCursor(0, 5); u8x8.print(F("SAVE(Hold)"));
  u8x8.setInverseFont(secretIndex == 6);
  u8x8.setCursor(0, 6); u8x8.print(F("FACT(Hold)"));
  u8x8.setInverseFont(secretIndex == 7);
  u8x8.setCursor(0, 7); u8x8.print(F("D01 LED:")); u8x8.print(useD01AsLEDs?F("ON"):F("OFF"));
  u8x8.setInverseFont(0);
}

// --------------- ISRs -----------------
static inline bool getStepHit(uint8_t ch, uint8_t sc) {
  uint8_t bi = 16 - sc;
  switch (ch) {
    case 0: return bitRead(ch1_step, bi);
    case 1: return bitRead(ch2_step, bi);
    case 2: return bitRead(ch3_step, bi);
    case 3: return bitRead(ch4_step, bi);
    case 4: return bitRead(ch5_step, bi);
    default: return bitRead(ch6_step, bi);
  }
}
static inline void handleStepEdgeISR() {
  lastStepEdgeMs = millis();
  step_count++;
  if (step_count > 16) { step_count = 1; wrappedEdge = 1; }

  if (outMode == OUT_TRG) {
    uint8_t newMask = outBits;
    for (uint8_t ch=0; ch<6; ch++){
      if (getStepHit(ch, step_count)) {
        newMask |= (1 << ch);
        trigActiveMask |= (1 << ch);
        trigOffAtUs[ch] = micros() + TRIG_US;  // 10 ms pulse
      }
    }
    outBits = newMask;
    writeOutputsFast(newMask);
    writeLEDsFast(newMask);
  } else if (outMode == OUT_GAT) {
    uint32_t gateUs = (stepPeriodUsEst * (uint32_t)gateDuty) / 100UL;
    if (gateUs < 500U) gateUs = 500U;

    uint8_t newMask = outBits;
    for (uint8_t ch=0; ch<6; ch++){
      if (getStepHit(ch, step_count)) {
        newMask |= (1 << ch);
        trigActiveMask |= (1 << ch);
        trigOffAtUs[ch] = micros() + gateUs;
      }
    }
    outBits = newMask;
    writeOutputsFast(newMask);
    writeLEDsFast(newMask);
  } else { // OUT_FF
    uint8_t newMask = outBits;
    for (uint8_t ch=0; ch<6; ch++){
      if (getStepHit(ch, step_count)) newMask ^= (1 << ch);
    }
    outBits = newMask;
    trigActiveMask = 0;
    writeOutputsFast(newMask);
    writeLEDsFast(newMask);
  }

  stepEdgeFlag = 1;
}
static inline void handleResetEdgeISR() {
  step_count = 0;
  outBits = 0; trigActiveMask = 0;
  writeOutputsFast(0); writeLEDsFast(0);
  resetEdgeFlag = 1;
}
ISR(PCINT0_vect) {
  static uint8_t lastB = PINB;
  uint8_t b       = PINB;
  uint8_t changed = b ^ lastB;
  uint8_t rising  = (~lastB) & b;

  if (changed) {
    if (rising & _BV(mapClkPin())) {
      uint32_t now = micros();
      uint32_t per = now - lastExtClkUs;
      lastExtClkUs = now;
      stepPeriodUsEst = (stepPeriodUsEst * 3U + per) >> 2; // smooth
      handleStepEdgeISR();
    }
    if (rising & _BV(mapRstPin())) {
      if (rstAction == RIN_RESET) {
        handleResetEdgeISR();
      } else {
        rstActionFlag = rstAction;
      }
      #if DEBUG
        dbg_rst_evt = rstAction;
      #endif
    }
  }
  lastB = b;
}
ISR(TIMER1_COMPA_vect) { if (clkSource == CLK_INT) handleStepEdgeISR(); }
