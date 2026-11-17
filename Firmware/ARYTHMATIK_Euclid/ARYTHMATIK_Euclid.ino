/**
 * @file ARYTHMATIK_Euclid.ino
 * @author Modulove & friends
 * @brief 6CH Eurorack (HAGIWO) euclidean Rhythm Generator
 * @version 0.2
 * @date 2024-04-23
 *
 * @copyright Copyright (c) 2025
 *
 * Usage:
 *
 *  Connect clock source to the CLK input and each output will output triggers according to settings set in the UI
 * 
 * Features:
 * - Interactive OLED menu for control and visualization.
 * - Rotary encoder for parameter adjustments and easy menu navigation.
 * - Reset through dedicated input and button press (Also possible individually per channel)
 * - Random auto advance, Save / Load State
 *
 * Hardware:
 * - Clock input (CLK) for timing triggers.
 * - Input: Clock (CLK), Reset (RST) and Rotary Encoder (with Button).
 * - Output: 6 Trigger Channels with LED indicators + CLK LED
 *
 * Encoder:
 * - Short press: Toggle between menue options (right side) & left menue options (channel options)
 * - rotate CW: dial in individual channel values for Hits, Offset, Limit, Mute. Reset channel, randomize channel
 * - rotate CCW: quick access menue for sequence Reset & Mute
 *
 * Boards:
 * - One .hex for the Arduino Nano (ATmega328P) and LGT8F328P Nano boards:
 *   build for "Arduino Nano", the chip is detected at power-up (LGT8F_Compat.h).
 * - Building with the lgt8fx core (Board "LGT8F328") also works (32 MHz).
 *
 */

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EncoderButton.h>
#include <util/atomic.h>
#include "LGT8F_Compat.h"  // instead of <EEPROM.h>: Nano + LGT8F328P from one .hex
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Configuration flags
//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

#define NUM_MEMORY_SLOTS 20
#define NUM_PRESETS 20

#ifdef ROTATE_PANEL
// When panel is rotated
#define RESET FastGPIO::Pin<13>
#define CLK FastGPIO::Pin<11>
#define OUTPUT1 FastGPIO::Pin<8>
#define OUTPUT2 FastGPIO::Pin<9>
#define OUTPUT3 FastGPIO::Pin<10>
#define OUTPUT4 FastGPIO::Pin<5>
#define OUTPUT5 FastGPIO::Pin<6>
#define OUTPUT6 FastGPIO::Pin<7>
#define LED1 FastGPIO::Pin<0>
#define LED2 FastGPIO::Pin<1>
#define LED3 FastGPIO::Pin<17>
#define LED4 FastGPIO::Pin<14>
#define LED5 FastGPIO::Pin<15>
#define LED6 FastGPIO::Pin<16>
#else
// When panel is not rotated
#define RESET FastGPIO::Pin<11>
#define CLK FastGPIO::Pin<13>
#define OUTPUT1 FastGPIO::Pin<5>
#define OUTPUT2 FastGPIO::Pin<6>
#define OUTPUT3 FastGPIO::Pin<7>
#define OUTPUT4 FastGPIO::Pin<8>
#define OUTPUT5 FastGPIO::Pin<9>
#define OUTPUT6 FastGPIO::Pin<10>
#define LED1 FastGPIO::Pin<14>
#define LED2 FastGPIO::Pin<15>
#define LED3 FastGPIO::Pin<16>
#define LED4 FastGPIO::Pin<0>
#define LED5 FastGPIO::Pin<1>
#define LED6 FastGPIO::Pin<17>
#endif
#define CLK_LED FastGPIO::Pin<4>

// Additional Pins
const byte ENCODER_PIN1 = 2;
const byte ENCODER_PIN2 = 3;
const byte ENCODER_SW_PIN = 12;

//#define DEBUG  // Uncomment for enabling debug print to serial monitoring output. Note: this affects performance and locks LED 4 & 5 on HIGH.
int debug = 0;  // ToDo: rework the debug feature (enable in menue?)

// Top level menu for selecting a channel or global settings.
enum TopMenu {
  MENU_CH_1,
  MENU_CH_2,
  MENU_CH_3,
  MENU_CH_4,
  MENU_CH_5,
  MENU_CH_6,
  MENU_RANDOM_ADVANCE,
  MENU_RAND,
  MENU_SAVE,
  MENU_LOAD,
  MENU_PRESET,
  MENU_TEMPO,
  MENU_ALL_RESET,
  MENU_ALL_MUTE,
  MENU_LAST
};

// Enum for individual channel settings.
enum Setting {
  SETTING_TOP_MENU,
  SETTING_HITS,
  SETTING_OFFSET,
  SETTING_LIMIT,
  SETTING_MUTE,
  SETTING_RESET,
  SETTING_RANDOM,
  SETTING_PROB,
  SETTING_LAST
};

// For debug / UI
#define FIRMWARE_MAGIC "EUCLID10"
#define FIRMWARE_MAGIC_ADDRESS 0  // Store firmware magic (read with debug fw!)

// OLED
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// EEPROM
#define EEPROM_START_ADDRESS 20
#define CONFIG_SIZE (sizeof(SlotConfiguration))
#define LAST_USED_SLOT_ADDRESS (EEPROM_START_ADDRESS + NUM_MEMORY_SLOTS * CONFIG_SIZE)

// Timing
bool trg_in = false, old_trg_in = false, rst_in = false, old_rst_in = false;

volatile byte playing_step[6] = { 0 };  // advanced by the clock interrupts

// display Menu and UI
// Select settings menu and channel menu
TopMenu selected_menu = MENU_CH_1;
Setting selected_setting = SETTING_TOP_MENU;
byte selected_preset = 0;
byte selected_slot = 0;
bool disp_refresh = true;
bool force_refresh = true;
unsigned long last_refresh = 0;
bool allMutedFlag = false;
bool internalClock = false;
bool showOverlay = false;

int tempo = 120;                 // beats per minute.
int period = 60000 / tempo / 4;  // one minute in ms divided by tempo divided by 4 for 16th note period.
// BPM derived from the external clock pulses
int externalBPM = 0;

// drawing tables in flash (RAM is the tightest resource of this firmware)
const byte pgm_graph_x[6] PROGMEM = { 0, 40, 80, 15, 55, 95 }, pgm_graph_y[6] PROGMEM = { 0, 0, 0, 32, 32, 32 };

byte line_xbuf[17];
byte line_ybuf[17];

const byte pgm_x16[16] PROGMEM = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, pgm_y16[16] PROGMEM = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };
#define GX(k) ((int)pgm_read_byte(&pgm_graph_x[k]))
#define GY(k) ((int)pgm_read_byte(&pgm_graph_y[k]))
#define X16(i) ((int)pgm_read_byte(&pgm_x16[i]))
#define Y16(i) ((int)pgm_read_byte(&pgm_y16[i]))

//Sequence variables
constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;  // 250;  // 125;  // Used by fast inputs like encoder rotation to throttle the display refresh. (3 steps)
volatile unsigned long last_clock_input = 0;

// Gate timing: the clock interrupts play the steps (clockStep()), so gates are
// on time even while the main loop is busy sending a frame to the OLED.
// Timer1 runs free at F_CPU / 256 (16 us per tick at 16 MHz):
// compare A = internal clock, compare B = gates off 10 ms after a step.
const uint16_t GATE_TICKS = F_CPU / 256 / 100;              // 10 ms gate
volatile uint16_t intClockTicks = F_CPU / 256 * 15 / 120;   // 16th note at 120 BPM
volatile uint8_t beatCount = 0;        // steps played; the main loop follows it
volatile unsigned long ledTimer = 0;   // channel LEDs go off 30 ms after a step
uint16_t isrRandom = 1;                // xorshift state for the probability (interrupt only)
uint16_t mainRandom = 1;               // xorshift state for the random mode (main loop only)


const static byte euc16[MAX_PATTERNS][MAX_STEPS] PROGMEM = {  //euclidian rythm
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
  { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0 },
  { 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0 },
  { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0 },
  { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1 },
  { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
  { 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1 },
  { 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
  { 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }
};
// What the clock interrupt plays. updateRythm() (main loop) prepares it and copies it in one
// atomic block, so the interrupt never sees a half-changed pattern and needs no table lookups.
volatile uint16_t stepMask[MAX_CHANNELS];  // bit n = step n has a hit (pattern with offset)
volatile uint8_t stepLimit[MAX_CHANNELS];  // pattern length 1..16
volatile uint8_t stepProb[MAX_CHANNELS];   // probability 0..100
volatile uint8_t muteMask;                 // bit i = channel i muted
#define STEP_ON(ch, step) ((stepMask[ch] >> (step)) & 1)
volatile uint16_t clkPeriodTicks;          // Timer1 ticks between the last two clock edges
uint16_t lastClkTick;

// random assign
const PROGMEM uint8_t hit_occ[MAX_CHANNELS] = { 5, 1, 20, 20, 40, 80 };
const PROGMEM uint8_t off_occ[MAX_CHANNELS] = { 1, 3, 20, 30, 40, 20 };
const PROGMEM uint8_t mute_occ[MAX_CHANNELS] = { 0, 2, 20, 20, 20, 20 };
const PROGMEM uint8_t hit_rng_max[MAX_CHANNELS] = { 6, 5, 8, 4, 4, 6 };
const PROGMEM uint8_t hit_rng_min[MAX_CHANNELS] = { 3, 2, 2, 1, 1, 1 };
const int bar_max[MAX_CHANNELS] PROGMEM = { 2, 4, 6, 8, 12, 16 };  // control

byte bar_now = 1;
byte bar_select = 1;  // ToDo: selected bar needs to be saved as well!
byte step_cnt = 0;

// Display Setup
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// reverse encoder
#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#endif

// Euclidean Sequencer Configuration with Rhythm Patterns
// This configuration defines parameters for different rhythm presets typical of various music genres.
// Each preset includes settings for hits, offsets, mute, step limit, probability, and genre names.

struct SlotConfiguration {
  byte hits[MAX_CHANNELS];         // Number of hits per pattern in each channel
  byte offset[MAX_CHANNELS];       // Step offset for each channel
  bool mute[MAX_CHANNELS];         // Mute status for each channel
  byte limit[MAX_CHANNELS];        // Step limit (length) of the pattern for each channel
  byte probability[MAX_CHANNELS];  // Probability of triggering each hit (0-100%)
  char name[10];                   // Name of the preset with a fixed size
  int tempo;                       // Tempo for the preset
  bool internalClock;              // Clock source state
  byte lastUsedSlot;               // Last used slot
  byte selectedPreset;             // Last used preset
  bool lastLoadedFromPreset;       // Flag to indicate last load source
};

// Default presets with names and rhythm patterns. All presets start unmuted with
// 100 % probability, so only hits/offset/limit/name/tempo are stored (saves ~400 bytes
// of flash); loadDefaultConfig() expands them to a SlotConfiguration.
struct PresetPattern {
  byte hits[MAX_CHANNELS];
  byte offset[MAX_CHANNELS];
  byte limit[MAX_CHANNELS];
  char name[7];
  byte tempo;
};

const PresetPattern defaultSlots[NUM_PRESETS] PROGMEM = {
  { { 4, 4, 7, 4, 4, 4 }, { 0, 2, 3, 2, 1, 0 }, { 16, 16, 12, 8, 16, 16 }, "Techno", 130 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 1, 0, 1 }, { 16, 16, 16, 16, 16, 16 }, "Funk", 110 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 3, 1, 0, 2 }, { 12, 12, 12, 12, 12, 12 }, "Swing", 140 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 2, 3, 2, 0, 2 }, { 8, 8, 8, 8, 8, 8 }, "Samba", 100 },
  { { 4, 3, 4, 3, 4, 3 }, { 0, 2, 0, 2, 0, 2 }, { 12, 12, 12, 12, 12, 12 }, "Waltz", 90 },
  { { 2, 2, 3, 3, 3, 3 }, { 0, 2, 0, 2, 0, 2 }, { 16, 16, 16, 16, 16, 16 }, "BossaN", 120 },
  { { 7, 7, 7, 7, 7, 7 }, { 0, 2, 4, 6, 8, 10 }, { 12, 12, 12, 12, 12, 12 }, "Bell", 100 },
  { { 2, 2, 3, 1, 2, 1 }, { 0, 1, 0, 2, 1, 0 }, { 24, 18, 24, 21, 16, 30 }, "Gen", 80 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 2, 0, 3 }, { 16, 16, 16, 16, 16, 16 }, "Gahu", 120 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 1, 0, 1, 0, 2 }, { 12, 12, 12, 12, 12, 12 }, "Threes", 120 },
  { { 9, 9, 9, 9, 9, 9 }, { 0, 1, 2, 3, 4, 5 }, { 8, 8, 8, 8, 8, 8 }, "Aksak", 120 },
  { { 5, 5, 5, 5, 5, 5 }, { 0, 2, 4, 6, 8, 10 }, { 8, 8, 8, 8, 8, 8 }, "Rumba", 120 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 2, 1, 3, 2, 4 }, { 16, 16, 16, 16, 16, 16 }, "Clave", 120 },
  { { 3, 3, 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { 16, 16, 16, 16, 16, 16 }, "Zarbi", 120 },
  { { 5, 5, 5, 5, 5, 5 }, { 0, 1, 2, 3, 4, 5 }, { 12, 12, 12, 12, 12, 12 }, "Nawkht", 120 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 1, 0, 1 }, { 16, 16, 16, 16, 16, 16 }, "FumeF", 120 },
  { { 5, 3, 5, 3, 5, 3 }, { 0, 2, 0, 2, 0, 2 }, { 12, 12, 12, 12, 12, 12 }, "Djembe", 120 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 2, 3, 4, 5 }, { 16, 16, 16, 16, 16, 16 }, "Gahu44", 120 },
  { { 3, 3, 3, 3, 3, 3 }, { 0, 2, 4, 1, 3, 5 }, { 16, 16, 16, 16, 16, 16 }, "Takita", 120 },
  { { 16, 8, 4, 2, 1, 1 }, { 0, 0, 0, 0, 0, 0 }, { 16, 16, 16, 16, 16, 16 }, "ClkDiv", 120 },
};

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS], currentConfig;
byte lastUsedSlot = 0;

// 'Modulove_Logo', 128x30px // Optimized Boot Logo
const unsigned char Modulove_Logo [] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0xe0, 0x3c, 0x00, 0x07, 0x00, 0x02, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x01, 0xa0, 0x6c, 0x00, 0x0d, 0x80, 0x06, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x01, 0x20, 0xc8, 0x00, 0x0d, 0x00, 0x04, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x03, 0x60, 0x88, 0x00, 0x1b, 0x00, 0x0c, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x02, 0x41, 0x98, 0x00, 0x13, 0x00, 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x06, 0xc3, 0x30, 0x00, 0x36, 0x00, 0x19, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x07, 0x83, 0x60, 0x00, 0x24, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x07, 0x06, 0x40, 0x00, 0x6c, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0e, 0x04, 0x80, 0x00, 0x78, 0x00, 0x32, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0e, 0x0d, 0x80, 0x00, 0x70, 0x00, 0x36, 0x00, 0x00, 0x03, 0xe0, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0c, 0x0f, 0x00, 0x00, 0xf0, 0x00, 0x2c, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 
  0x00, 0x00, 0x7f, 0x18, 0x1e, 0x0e, 0x02, 0xe4, 0x08, 0x2c, 0x38, 0x41, 0xcc, 0x60, 0x00, 0x00, 
  0x00, 0x03, 0xfc, 0x18, 0x18, 0x1f, 0x0e, 0xc4, 0x18, 0x78, 0x7c, 0xc3, 0xc8, 0xc0, 0x00, 0x00, 
  0x00, 0x0e, 0x00, 0x38, 0x30, 0x19, 0xfd, 0xc4, 0x10, 0x70, 0xc7, 0xc2, 0x7f, 0x80, 0x00, 0x00, 
  0x00, 0x18, 0x00, 0x6d, 0xe0, 0x1f, 0xf9, 0x8c, 0x30, 0x60, 0x46, 0x86, 0x3e, 0x00, 0x00, 0x00, 
  0x00, 0x30, 0x00, 0xc7, 0xe0, 0x01, 0x61, 0x88, 0x30, 0xe0, 0x7c, 0x84, 0x20, 0x00, 0x38, 0x00, 
  0x00, 0x60, 0x01, 0x80, 0x40, 0x61, 0x43, 0x18, 0x60, 0xc3, 0x3d, 0x8c, 0x20, 0x01, 0xff, 0x00, 
  0x00, 0x40, 0x03, 0x00, 0x40, 0xc3, 0xc3, 0x18, 0x60, 0x82, 0x05, 0x88, 0x60, 0x03, 0x08, 0x00, 
  0x00, 0xc0, 0x02, 0x00, 0xc0, 0x83, 0x83, 0x30, 0xc1, 0x86, 0x0d, 0x88, 0x40, 0x06, 0x00, 0x00, 
  0x00, 0x80, 0x06, 0x00, 0xc1, 0x83, 0x86, 0x30, 0xc3, 0x86, 0x0d, 0x98, 0x40, 0x04, 0x00, 0x00, 
  0x00, 0xc0, 0x0c, 0x00, 0xc1, 0x07, 0x86, 0x71, 0xc3, 0x0e, 0x09, 0x90, 0x40, 0x04, 0x00, 0x00, 
  0x00, 0xc0, 0x38, 0x00, 0xc3, 0x07, 0x8e, 0x71, 0x87, 0x0e, 0x19, 0xb0, 0x40, 0x04, 0x00, 0x00, 
  0x00, 0xc0, 0x60, 0x00, 0xc7, 0x0d, 0x0e, 0xf3, 0x8d, 0x1e, 0x11, 0xb0, 0x60, 0x0c, 0x00, 0x00, 
  0x00, 0x60, 0xc0, 0x00, 0xcd, 0x99, 0x1e, 0xf2, 0x99, 0x32, 0x30, 0xa0, 0x30, 0x18, 0x00, 0x00, 
  0x00, 0x3f, 0x80, 0x00, 0x79, 0xf1, 0xf7, 0xbe, 0xf1, 0xe3, 0xe0, 0xe0, 0x3f, 0xf0, 0x00, 0x00, 
  0x00, 0x0e, 0x00, 0x00, 0x30, 0xe0, 0xc3, 0x18, 0x60, 0xc0, 0xc0, 0x40, 0x0f, 0xe0, 0x00, 0x00
};

// Screen dimensions
const int screenWidth = 128;
const int screenHeight = 64;

// Logo dimensions
const int logoWidth = 128;  // Keep full width
const int logoHeight = 30;  // Reduced height (only the rows with content)
const int yOffset = 14;     // Position in the original bitmap where content starts

void drawAnimation() {  // Boot Logo
    for (int x = 0; x <= logoWidth; x += 10) {
        display.clearDisplay();
        
        // Position the bitmap at the same vertical offset as in the original
        display.drawBitmap(0, yOffset, Modulove_Logo, logoWidth, logoHeight, WHITE);
        
        // Apply the wipe effect
        display.fillRect(x, yOffset, logoWidth - x, logoHeight, BLACK);
        
        i2cGuardShow(display);
    }
}

// Reset flag - set in the interrupt, the main loop refreshes the display
volatile bool resetTriggerFlag = false;

// Previous pin states for edge detection in the interrupt
volatile bool prev_clk_state = false;
volatile bool prev_rst_state = false;

static void clockStep();

// CLK / RST pin change interrupt. Kept small on purpose: RAM is so tight in this firmware that a
// deep interrupt (function pointer, divisions, millis()) let the stack run into the display
// buffer and crash the module under fast clocks. BPM and timers are handled in the main loop.
ISR(PCINT0_vect) {
  uint8_t pins = PINB;
#ifdef ROTATE_PANEL
  bool clk = pins & (1 << PINB3), rst = pins & (1 << PINB5);  // CLK = pin 11, RST = pin 13
#else
  bool clk = pins & (1 << PINB5), rst = pins & (1 << PINB3);  // CLK = pin 13, RST = pin 11
#endif
  if (rst && !prev_rst_state) {  // reset first: a clock arriving with it plays step 0
    for (uint8_t k = 0; k < MAX_CHANNELS; k++) playing_step[k] = 15;
    resetTriggerFlag = true;
  }
  if (clk && !prev_clk_state) {
    uint16_t t = TCNT1;
    clkPeriodTicks = t - lastClkTick;
    lastClkTick = t;
    if (!internalClock) clockStep();
  }
  prev_clk_state = clk;
  prev_rst_state = rst;
}

// Function to setup pin change interrupts
void setupPinChangeInterrupts() {
  // Disable interrupts during setup
  cli();
  
  // Read initial pin states
#ifdef ROTATE_PANEL
  prev_clk_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
  prev_rst_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
#else
  prev_clk_state = (PINB & (1 << PINB5)) != 0;   // Pin 13
  prev_rst_state = (PINB & (1 << PINB3)) != 0;   // Pin 11
#endif

  // Enable Pin Change Interrupt Control Register for Port B
  PCICR |= (1 << PCIE0);  // Enable PCIE0 (Port B)
  
#ifdef ROTATE_PANEL
  // Enable PCINT3 (Pin 11 - CLK) & PCINT5 (Pin 13 - RST)
  PCMSK0 |= (1 << PCINT3) | (1 << PCINT5);
#else
  // Enable PCINT5 (Pin 13 - CLK) & PCINT3 (Pin 11 - RST)
  PCMSK0 |= (1 << PCINT5) | (1 << PCINT3);
#endif

  // Re-enable interrupts
  sei();
}

void printDebugMessage(const char *message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(message);
  display.display();
  delay(100);
}

// 0 .. n-1 (replaces Arduino random(), which costs ~300 bytes of flash)
static uint16_t rnd(uint16_t n) {
  mainRandom ^= mainRandom << 7;
  mainRandom ^= mainRandom >> 9;
  mainRandom ^= mainRandom << 8;
  return mainRandom % n;
}

void Random_change(bool includeMute, bool allChannels, byte select_ch = 0) {
  static constexpr uint8_t MAX_RANDOM = 100;
  for (uint8_t k = 0; k < MAX_CHANNELS; k++) {
    if (!allChannels && k != select_ch) continue;

    uint8_t random_value = 1 + rnd(MAX_RANDOM - 1);
    if (pgm_read_byte(&hit_occ[k]) >= random_value) {
      uint8_t lo = pgm_read_byte(&hit_rng_min[k]);
      currentConfig.hits[k] = lo + rnd(pgm_read_byte(&hit_rng_max[k]) + 1 - lo);
    }
    if (pgm_read_byte(&off_occ[k]) >= random_value) {
      currentConfig.offset[k] = rnd(MAX_STEPS);
    }
    if (includeMute && k > 0) {
      currentConfig.mute[k] = pgm_read_byte(&mute_occ[k]) >= random_value;
    } else {
      currentConfig.mute[k] = false;
    }
  }
}

void onOverlayTimeout(EncoderButton &eb) {
  showOverlay = false;
  disp_refresh = true;
}

void setup() {

  setupEncoder();
  initIO();
  // Setup pin change interrupts AFTER initIO
  setupPinChangeInterrupts();
  initDisplay();

#if !defined(DISABLE_BOOT_LOGO)
  drawAnimation();
  // Show which chip was detected, so a board swap can be checked at a glance.
  display.setCursor(IS_LGT ? 37 : 34, 54);
  display.print(chipName());
  i2cGuardShow(display);
  delay(1200);
#endif

  checkAndInitializeSettings();

  // Load the last used slot from EEPROM
  EEPROM.get(LAST_USED_SLOT_ADDRESS, lastUsedSlot);
  if (lastUsedSlot >= NUM_MEMORY_SLOTS) {
    lastUsedSlot = 0;
  }

  // Ensure the current configuration is initialized correctly
  loadFromEEPROM(lastUsedSlot);
  if (currentConfig.lastLoadedFromPreset) {
    loadFromPreset(currentConfig.selectedPreset);
  }

  updateRythm();
  OLED_display();

  // analogRead(A0) gave the same seed on every power-up: A0 drives LED1 low.
  uint32_t seed = chipRandomSeed();
  mainRandom = (uint16_t)seed | 1;  // xorshift states must never be 0

  // Initialize the last external clock time to the current time
  last_clock_input = millis();
  isrRandom = (uint16_t)(seed >> 16) | 1;
  setupGateTimer();
}

void loop() {
  encoder.update();  // Process Encoder & button updates

  updateRythm();

  // Internal clock: 16th-note period for the Timer1 interrupt.
  static int tickTempo = 0;
  if (tempo != tickTempo) {
    tickTempo = tempo;
    uint16_t ticks = (uint32_t)(F_CPU / 256) * 15 / tempo;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      intClockTicks = ticks;
    }
  }

  // Reset is done in the interrupt, only the display follows here.
  if (resetTriggerFlag) {
    resetTriggerFlag = false;
    disp_refresh = true;
  }

  // Follow the steps the clock interrupts have played.
  static uint8_t beatsSeen = 0;
  if (beatsSeen != beatCount) {
    unsigned long now = millis();
    static unsigned long lastBeatMs = 0;
    if (!internalClock) {  // BPM of the external clock (16th notes), from Timer1 ticks
      uint16_t ticks;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        ticks = clkPeriodTicks;
      }
      // under 1 s between pulses: exact from Timer1 (wraps after 1.05 s); slower shows as 14
      uint32_t bpm = (now - lastBeatMs < 1000 && ticks) ? (uint32_t)(F_CPU / 256) * 15 / ticks : 14;
      if (bpm > 360) bpm = 360;
      externalBPM = bpm;
      period = 60000 / externalBPM / 4;
    }
    lastBeatMs = now;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      ledTimer = now;
      last_clock_input = now;
    }
  }
  while (beatsSeen != beatCount) {
    beatsSeen++;
    force_refresh = true;
    disp_refresh = true;  // Updates the display where the trigger was entered.
    debug = 0;

    // Random advance mode (mode 6)
    if (selected_menu == MENU_RANDOM_ADVANCE) {  // random mode setting
      step_cnt++;
      if (step_cnt >= 16) {
        bar_now++;
        step_cnt = 0;
        if (bar_now > pgm_read_word(&bar_max[bar_select])) {
          bar_now = 1;
          Random_change(true, true);
        }
      }
    }
  }

  // Channel LEDs stay on for 30 ms (the gates end in the Timer1 interrupt after 10 ms).
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (millis() - ledTimer >= 30) {
      LED1::setOutput(0);
      LED2::setOutput(0);
      LED3::setOutput(0);
      LED4::setOutput(0);
      LED5::setOutput(0);
      LED6::setOutput(0);
    }
  }

  OLED_display();  // refresh display

}

void setupEncoder() {
  encoder.setDebounceInterval(2);  // Increase debounce interval
  encoder.setMultiClickInterval(20);
  encoder.setRateLimit(100);
  encoder.setIdleTimeout(5000);
  encoder.setIdleHandler(onOverlayTimeout);
  encoder.setLongClickDuration(375);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);  // Add long click handler
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);  // Added handler again for pressed rotation
}

void initIO() {
  RESET::setInput();        // RST
  CLK::setInput();          // CLK
  OUTPUT1::setOutputLow();  // CH1
  OUTPUT2::setOutputLow();  // CH2
  OUTPUT3::setOutputLow();  // CH3
  OUTPUT4::setOutputLow();  // CH4
  OUTPUT5::setOutputLow();  // CH5
  OUTPUT6::setOutputLow();  // CH6
  // LED outputs
  LED1::setOutputLow();     // CH1 LED
  LED2::setOutputLow();     // CH2 LED
  LED3::setOutputLow();     // CH3 LED
  LED4::setOutputLow();     // CH6 LED
  LED5::setOutputLow();     // CH4 LED
  LED6::setOutputLow();     // CH5 LED
  CLK_LED::setOutputLow();  // CLK LED
}

void initDisplay() {
  delay(1000);  // Screen needs a sec to initialize ? How to fix ?
  i2cGuardBegin();  // free a stuck bus; every I2C call gives up after 25 ms instead of hanging
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

// Check if the panel should be rotated
#ifdef ROTATE_PANEL
  display.setRotation(2);  // Adjust rotation for upside down mounting
#endif

#ifdef DEBUG
  // Display the firmware version and basic instructions when debugging
  display.println(F("ARYTHMATIK Euclid"));
  display.println(F("Firmware: v0.1"));
  display.println(F("6CH Euclidean Rhythm"));
  display.display();
  delay(1000);
#endif
}

// Timer1 free-running at F_CPU / 256; compare A = internal clock, compare B = gate end.
void setupGateTimer() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1A = 0;
    TCCR1B = (1 << CS12);
    OCR1A = TCNT1 + intClockTicks;
    TIFR1 = (1 << OCF1A) | (1 << OCF1B);
    TIMSK1 = (1 << OCIE1A);
  }
}

// 0..99 for the probability check (xorshift16, interrupt only).
static inline __attribute__((always_inline)) uint8_t isrRandom100() {
  isrRandom ^= isrRandom << 7;
  isrRandom ^= isrRandom >> 9;
  isrRandom ^= isrRandom << 8;
  return ((uint16_t)(isrRandom >> 8) * 100) >> 8;
}

// Play one step on all channels. Called from the clock interrupts only; one shared copy, free of
// further calls and lookups, so the interrupt needs little stack.
static void __attribute__((noinline)) clockStep() {
  uint8_t on = 0;
  uint8_t mutes = muteMask;
  for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
    uint8_t st = playing_step[i] + 1;
    if (st >= stepLimit[i]) st = 0;  // step limit reached
    playing_step[i] = st;
    if (STEP_ON(i, st) && !(mutes & (1 << i)) && isrRandom100() < stepProb[i]) on |= 1 << i;
  }
  CLK_LED::setOutput(1);
  if (on & 0x01) { OUTPUT1::setOutput(1); LED1::setOutput(1); }
  if (on & 0x02) { OUTPUT2::setOutput(1); LED2::setOutput(1); }
  if (on & 0x04) { OUTPUT3::setOutput(1); LED3::setOutput(1); }
  if (on & 0x08) { OUTPUT4::setOutput(1); LED4::setOutput(1); }
  if (on & 0x10) { OUTPUT5::setOutput(1); LED5::setOutput(1); }
  if (on & 0x20) { OUTPUT6::setOutput(1); LED6::setOutput(1); }
  OCR1B = TCNT1 + GATE_TICKS;  // gates off in 10 ms
  TIFR1 = (1 << OCF1B);
  TIMSK1 |= (1 << OCIE1B);
  beatCount++;
}

// Internal clock
ISR(TIMER1_COMPA_vect) {
  OCR1A += intClockTicks;
  if (internalClock) clockStep();
}

// End of the 10 ms gate
ISR(TIMER1_COMPB_vect) {
  OUTPUT1::setOutput(0);
  OUTPUT2::setOutput(0);
  OUTPUT3::setOutput(0);
  OUTPUT4::setOutput(0);
  OUTPUT5::setOutput(0);
  OUTPUT6::setOutput(0);
  CLK_LED::setOutput(0);  // Turn off the clock LED, so only on briefly after a clock impulse is received
  TIMSK1 &= ~(1 << OCIE1B);
}

bool isClockRunning() {
  unsigned long last;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    last = last_clock_input;
  }
  return internalClock || (millis() - last < period);
}

void onEncoderClicked(EncoderButton &eb) {
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true;  // just in case

  if (showOverlay) {
    onOverlayTimeout(eb);  // Handle click to exit overlay
  } else {
    switch (selected_menu) {
      // Channel-specific actions
      case MENU_CH_1:
      case MENU_CH_2:
      case MENU_CH_3:
      case MENU_CH_4:
      case MENU_CH_5:
      case MENU_CH_6:
        // Click should only advance selected setting when a channel top menu is selected.
        selected_setting = static_cast<Setting>((selected_setting + 1) % SETTING_LAST);
        break;
      // Mode-specific actions
      case MENU_ALL_RESET:
        resetSeq();
        break;
      case MENU_ALL_MUTE:
        toggleAllMutes();
        break;
      case MENU_RAND:
        Random_change(false, true);
        break;
      case MENU_RANDOM_ADVANCE:
        Random_change(false, false);
        break;
      case MENU_SAVE:
      case MENU_LOAD:
      case MENU_PRESET:
      case MENU_TEMPO:
        showOverlay = !showOverlay;
        break;
    }
  }
}

// now toggles the mute status of the selected channel when in SETTING_TOP_MENU
void onEncoderLongClicked(EncoderButton &eb) {
  if (!isClockRunning()) {
    force_refresh = true;
  }
  disp_refresh = true; 

  // If we're in the parameter settings (Hits, Offset, Limit, etc.) for one of the channels
  if (selected_menu <= MENU_CH_6 && selected_setting != SETTING_TOP_MENU) {
    // Decrement the selected menu to allow back navigation
    selected_setting = static_cast<Setting>((selected_setting - 1 + SETTING_LAST) % SETTING_LAST);
  } else if (showOverlay) {
    // Handle specific overlay actions
    switch (selected_menu) {
      case MENU_PRESET:
        loadDefaultConfig(&currentConfig, selected_preset);
        currentConfig.lastLoadedFromPreset = true;  // Indicates that the current config was loaded from a preset
        tempo = currentConfig.tempo;                // Use the preset's tempo
        period = 60000 / tempo / 4;                 // Update period with loaded tempo
        showOverlay = false;                        // Turn off the overlay
        break;
      case MENU_SAVE:
        saveToEEPROM(selected_slot); // Save to selected slot
        showOverlay = false;  // Turn off the overlay
        break;
      case MENU_LOAD:
        loadFromEEPROM(selected_slot);
        currentConfig.lastLoadedFromPreset = false;  // Indicates that the current config was loaded from a save slot
        showOverlay = false;                         // Turn off the overlay
        break;
    }
  } else if (selected_menu == MENU_TEMPO) {
    internalClock = !internalClock;  // Toggle the internal clock state
    showOverlay = true;              // Show overlay to indicate clock state change
    if (internalClock) period = 60000 / tempo / 4;
  } else if (selected_setting == SETTING_TOP_MENU && selected_menu <= MENU_CH_6) {
    // Mute the selected channel
    int channelIndex = selected_menu - MENU_CH_1;
    currentConfig.mute[channelIndex] = !currentConfig.mute[channelIndex];
  } 
}


void onEncoderRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;
  disp_refresh = true;

  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  // Only handle setting navigation if not all muted and the overlay is not shown.
  if (!allMutedFlag && !showOverlay) {
    handleSettingNavigation(acceleratedIncrement);
    return;
  }

  // Overlay shown menu adjustments.
  if (selected_setting == SETTING_TOP_MENU && showOverlay) {
    switch (selected_menu) {
      case MENU_PRESET:
        // Handle preset selection
        selected_preset = (selected_preset + increment + NUM_PRESETS) % NUM_PRESETS;
        break;

      case MENU_TEMPO:
        // Increment the tempo with the accelerated increment
        tempo += acceleratedIncrement;

        // Ensure the tempo stays within the range of 40 to 240 BPM
        if (tempo < 40) {
          tempo = 40;
        } else if (tempo > 240) {
          tempo = 240;
        }

        // Calculate the period based on the tempo
        period = 60000 / (tempo * 4);
        break;

      case MENU_SAVE:
      case MENU_LOAD:
        // EEPROM slot selection for saving or loading
        selected_slot = (selected_slot + increment + NUM_MEMORY_SLOTS) % NUM_MEMORY_SLOTS;
        break;

      default:
        // If no valid menu is selected, do nothing.
        break;
    }
  }
}

void onEncoderPressedRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;
  disp_refresh = true;

  int acceleratedIncrement = increment * increment;  // Squaring the increment for quicker adjustments
  if (increment < 0) acceleratedIncrement = -acceleratedIncrement;

  if (selected_menu == MENU_ALL_MUTE) {
    // Handle channel muting/unmuting in MENU_ALL_MUTE mode
    static int current_channel = 0;
    current_channel = (current_channel + increment + MAX_CHANNELS) % MAX_CHANNELS;

    // Mute or unmute channels sequentially
    if (increment > 0) {  // CW rotation
      currentConfig.mute[current_channel] = true;
    } else {  // CCW rotation
      currentConfig.mute[MAX_CHANNELS - 1 - current_channel] = false;
    }

    // Check if all channels are muted or unmuted and reset if needed
    bool all_muted = true, all_unmuted = true;
    for (int i = 0; i < MAX_CHANNELS; i++) {
      if (!currentConfig.mute[i]) all_muted = false;
      if (currentConfig.mute[i]) all_unmuted = false;
    }
    if (all_muted) {
      current_channel = 0;
      for (int i = 0; i < MAX_CHANNELS; i++) {
        currentConfig.mute[i] = false;
      }
    }
    if (all_unmuted) {
      current_channel = 0;
      for (int i = 0; i < MAX_CHANNELS; i++) {
        currentConfig.mute[i] = true;
      }
    }

    return;  // Exit early
  }

  // Handle specific modes when not in MENU_ALL_MUTE
  switch (selected_menu) {
    case MENU_CH_1:
    case MENU_CH_2:
    case MENU_CH_3:
    case MENU_CH_4:
    case MENU_CH_5:
    case MENU_CH_6:
      if (selected_setting == SETTING_TOP_MENU) {
        // Adjust the Hits value for the selected channel to quickly edit the beat/rhythm
        currentConfig.hits[selected_menu] = (currentConfig.hits[selected_menu] + increment + 17) % 17;
      } else {
        // Handle channel switching when in specific modes
        selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
        // Ensure the selected_menu is within the range of channels
        if (selected_menu > MENU_CH_6) {
          selected_menu = MENU_CH_1;
        }
      }
      break;

    case MENU_RAND:
      // Check rotation direction to call appropriate random change function
      if (increment > 0) {
        Random_change(false, true);  // Rotate CW: Random change without mute
      } else {
        Random_change(true, true);  // Rotate CCW: Random change with mute
      }
      break;

    case MENU_RANDOM_ADVANCE:
      // Ensure bar_select stays within the range of 1 to 5
      bar_select += increment;
      if (bar_select < 1) bar_select = 6;
      if (bar_select > 6) bar_select = 1;
      break;

    default:
      // Handle other settings or ignore
      if (selected_setting != SETTING_TOP_MENU) {
        selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
        // Ensure the selected_menu is within the range of channels
        if (selected_menu > MENU_CH_6) {
          selected_menu = MENU_CH_1;
        }
      }
      break;
  }
}


void initializeCurrentConfig(bool loadDefaults = false) {
  if (loadDefaults) {
    // Load default configuration from PROGMEM
    loadDefaultConfig(&currentConfig, 0);  // Load the first default slot as the initial configuration
  } else {
    // Load configuration from EEPROM
    int baseAddress = EEPROM_START_ADDRESS;  // Start address for the first slot
    EEPROM.get(baseAddress, currentConfig);
    tempo = currentConfig.tempo;                     // Load tempo
    internalClock = currentConfig.internalClock;     // Load clock state
    lastUsedSlot = currentConfig.lastUsedSlot;       // Load last used slot
    selected_preset = currentConfig.selectedPreset;  // Load last used preset
    if (currentConfig.lastLoadedFromPreset) {
      loadFromPreset(selected_preset);
    } else {
      loadFromEEPROM(lastUsedSlot);
    }
  }
}

void handleSettingNavigation(int changeDirection) {
  switch (selected_setting) {
    case SETTING_TOP_MENU:                                                                              // Select channel
      selected_menu = static_cast<TopMenu>((selected_menu + changeDirection + MENU_LAST) % MENU_LAST);  // Wrap-around for channel selection
      break;
    case SETTING_HITS:                                                                                                          // Hits
      currentConfig.hits[selected_menu] = (currentConfig.hits[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;  // Ensure hits wrap properly
      break;
    case SETTING_OFFSET:
      currentConfig.offset[selected_menu] = (currentConfig.offset[selected_menu] - changeDirection + MAX_STEPS) % MAX_STEPS;  // Wrap-around for offset (reversed the logic of offset so it rotates in the right direction)
      break;
    case SETTING_LIMIT:                                                                                                           // Limit
      currentConfig.limit[selected_menu] = (currentConfig.limit[selected_menu] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS;  // Wrap-around for limit
      break;
    case SETTING_MUTE:                                                         // Mute
      currentConfig.mute[selected_menu] = !currentConfig.mute[selected_menu];  // Toggle mute state
      break;
    case SETTING_RESET:  // Reset channel step
      playing_step[selected_menu] = 0;
      break;
    case SETTING_RANDOM:  // Randomize channel
      Random_change(false, false, selected_menu);
      break;
    case SETTING_PROB:  // Set probability
      currentConfig.probability[selected_menu] = (currentConfig.probability[selected_menu] + changeDirection + 101) % 101;
      break;
  }
}

// Loading SlotConfiguration from PROGMEM
void loadDefaultConfig(SlotConfiguration *config, int index) {
  if (index >= NUM_PRESETS) index = 0;
  PresetPattern preset;
  memcpy_P(&preset, &defaultSlots[index], sizeof(preset));
  memset(config, 0, sizeof(*config));  // unmuted, internal clock off, name zero-padded
  for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
    config->hits[i] = preset.hits[i];
    config->offset[i] = preset.offset[i];
    config->limit[i] = preset.limit[i];
    config->probability[i] = 100;
  }
  memcpy(config->name, preset.name, sizeof(preset.name));
  config->tempo = preset.tempo;
  disp_refresh = true;
}

void saveToEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);
  if (baseAddress + CONFIG_SIZE <= EEPROM.length()) {
    currentConfig.tempo = tempo;
    currentConfig.internalClock = internalClock;
    currentConfig.lastUsedSlot = slot;
    currentConfig.selectedPreset = selected_preset;
    currentConfig.lastLoadedFromPreset = false;  // Indicates that the current config was loaded from a save slot
    EEPROM.put(baseAddress, currentConfig);
    // Save the last used slot
    EEPROM.put(LAST_USED_SLOT_ADDRESS, slot);
  }
}

void loadFromEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);
  if (baseAddress + CONFIG_SIZE <= EEPROM.length()) {
    EEPROM.get(baseAddress, currentConfig);
    tempo = currentConfig.tempo;
    internalClock = currentConfig.internalClock;
    lastUsedSlot = slot;
    selected_preset = currentConfig.selectedPreset;
    period = 60000 / tempo / 4;  // Update period with loaded tempo
    updateRythm();               // Refresh
  }  // an invalid slot (garbage EEPROM data) is ignored
}

void loadFromPreset(int preset) {
  if (preset >= NUM_PRESETS) preset = 0;
  loadDefaultConfig(&currentConfig, preset);
  tempo = currentConfig.tempo;
  internalClock = currentConfig.internalClock;
  selected_preset = preset;
  period = 60000 / tempo / 4;
}

void initializeDefaultRhythms() {
  for (int i = 0; i < NUM_PRESETS; i++) {
    SlotConfiguration config;
    loadDefaultConfig(&config, i);
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, config);
  }
}

void saveDefaultsToEEPROM(int slot, SlotConfiguration config) {
  int address = EEPROM_START_ADDRESS + (slot * sizeof(SlotConfiguration));
  EEPROM.put(address, config);
}

void toggleAllMutes() {
  // Toggle mute for all channels
  bool allMuted = true;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (currentConfig.mute[i] == 0) {
      allMuted = false;
      break;
    }
  }
  for (int i = 0; i < MAX_CHANNELS; i++) {
    currentConfig.mute[i] = !allMuted;
  }
  allMutedFlag = !allMuted;
}

void resetSeq() {
  for (int k = 0; k < MAX_CHANNELS; k++) {
    playing_step[k] = 15;
  }
}

void leftMenu(char c1, char c2, char c3, char c4) {
  int y_base = 30;  // Starting y position
  int y_step = 9;   // Step for each character position in y

  display.setCursor(0, y_base);
  display.print(c1);
  display.setCursor(0, y_base + y_step * 1);
  display.print(c2);
  display.setCursor(0, y_base + y_step * 2);
  display.print(c3);
  display.setCursor(0, y_base + y_step * 3);
  display.print(c4);
}

void rightMenu(char c1, char c2, char c3, char c4) {
  int x_base = 120;  // Starting x position
  int y_step = 9;    // Step for each character position in y

  display.setCursor(x_base, 0);
  display.print(c1);
  display.setCursor(x_base, y_step * 1);
  display.print(c2);
  display.setCursor(x_base, y_step * 2);
  display.print(c3);
  display.setCursor(x_base, y_step * 3);
  display.print(c4);
}

// Menu labels as 4 letters per entry (saves flash compared to a switch per menu)
const char topMenuLabels[] PROGMEM = "RND X   S   L   P   EXT R   M   ";  // MENU_RANDOM_ADVANCE .. MENU_ALL_MUTE
const char modeMenuLabels[] PROGMEM = "RANDSAVELOADPRST*CLKRSETMUTE";    // MENU_RAND .. MENU_ALL_MUTE
const char settingLabels[] PROGMEM = "HITSOFFSLIMIMUTERSETRANDPROB";     // SETTING_HITS .. SETTING_PROB

void leftMenuP(const char *label) {
  leftMenu(pgm_read_byte(label), pgm_read_byte(label + 1), pgm_read_byte(label + 2), pgm_read_byte(label + 3));
}

void drawTopMenuRight(TopMenu select_ch) {
  if (showOverlay) return;  // Exit
  if (select_ch <= MENU_CH_6) {
    rightMenu('1' + select_ch, 'H', 'O', ' ');
    return;
  }
  const char *label = topMenuLabels + 4 * (select_ch - MENU_RANDOM_ADVANCE);
  if (select_ch == MENU_TEMPO && internalClock) label = PSTR("INT ");
  rightMenu(pgm_read_byte(label), pgm_read_byte(label + 1), pgm_read_byte(label + 2), pgm_read_byte(label + 3));
}

// left side menue - Channel Settings
void drawChannelEditMenu(TopMenu select_ch, Setting select_menu) {
  if (showOverlay || select_menu < SETTING_HITS || select_menu > SETTING_PROB) return;  // Exit
  leftMenuP(settingLabels + 4 * (select_menu - SETTING_HITS));
}

// left side menue - Menu Options
void drawModeMenu(TopMenu select_ch) {
  if (showOverlay || select_ch < MENU_RAND || select_ch > MENU_ALL_MUTE) return;  // Exit
  leftMenuP(modeMenuLabels + 4 * (select_ch - MENU_RAND));
}

void checkAndInitializeSettings() {
  char magic[sizeof(FIRMWARE_MAGIC)];
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);

  if (strncmp(magic, FIRMWARE_MAGIC, sizeof(FIRMWARE_MAGIC)) != 0) {
    // Magic number not found, initialize EEPROM with default values
    initializeDefaultRhythms();
    EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
    initializeCurrentConfig(true);  // Load defaults into currentConfig
  } else {
    // Load the configuration from EEPROM
    initializeCurrentConfig(false);
  }
}

// Drawing random advance indicator
void drawRandomModeAdvanceSquare(int bar_select, int bar_now, const int *bar_max) {  // Change to const int*
  if (62 - pgm_read_word(&bar_max[bar_select]) * 2 >= 0 && 64 - bar_now * 2 >= 0) {
    display.drawRoundRect(1, 62 - pgm_read_word(&bar_max[bar_select]) * 2, 6, pgm_read_word(&bar_max[bar_select]) * 2 + 2, 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_now * 2, WHITE);
  }
}

void drawSelectionIndicator(Setting select_menu) {
  if (showOverlay) return;
  // Right side (top menu, hits, offset): 9 px rows; left side (limit .. prob): 8 px rows.
  if (select_menu <= SETTING_OFFSET) {
    int y = 9 * select_menu;
    display.drawTriangle(113, y, 113, y + 6, 118, y + 3, WHITE);
  } else {
    int y = 34 + 8 * (select_menu - SETTING_LIMIT);
    display.drawTriangle(12, y, 12, y + 7, 7, y + 3, WHITE);
  }
}

void drawStepDots(const SlotConfiguration &currentConfig) {
  if (showOverlay) return;
  for (int k = 0; k < MAX_CHANNELS; k++) {
    for (int j = 0; j < currentConfig.limit[k]; j++) {
      int x_pos = X16(j % 16) + GX(k);
      int y_pos = Y16(j % 16) + GY(k);
      if (x_pos < 128 && y_pos < 64 && currentConfig.mute[k] == 0) {
        display.drawPixel(x_pos, y_pos, WHITE);
      }
    }
  }
}

void OLED_display() {
  static uint32_t last_refresh = 0;
  uint32_t current_time = millis();

  if (!force_refresh && (!disp_refresh || (current_time - last_refresh < MIN_REFRESH_DURATION))) {
    return;
  }

  disp_refresh = false;
  force_refresh = false;
  last_refresh = current_time;

  display.clearDisplay();

  if (allMutedFlag) {
    drawMuteScreen();
  } else {
    drawMainScreen();
  }

  if (showOverlay) {
    drawOverlay();
  }

  i2cGuardShow(display);  // a stuck display bus no longer freezes the module
}


void drawMuteScreen() {
  display.setTextSize(2);
  display.setCursor((SCREEN_WIDTH - 4 * 12) / 2, (SCREEN_HEIGHT - 2 * 8) / 2);
  display.println(F("MUTE"));
  display.drawRoundRect((SCREEN_WIDTH - 4 * 12) / 2 - 4, (SCREEN_HEIGHT - 2 * 8) / 2 - 4, 4 * 12 + 8, 2 * 8 + 8, 2, WHITE);
  display.setTextSize(1);
}

void drawMainScreen() {
  drawTopMenuRight(selected_menu);
  drawChannelEditMenu(selected_menu, selected_setting);
  drawModeMenu(selected_menu);

  if (selected_menu == MENU_RANDOM_ADVANCE) {
    drawRandomModeAdvanceSquare(bar_select, bar_now, bar_max);
  }

  drawSelectionIndicator(selected_setting);

  if (selected_setting == SETTING_PROB) {
    drawProbabilityConfig();
  } else {
    drawStepDots(currentConfig);
    drawEuclideanRhythms();
  }
}

void drawOverlay() {
  if (selected_setting == SETTING_TOP_MENU) {
    switch (selected_menu) {
      case MENU_PRESET:
        drawPresetSelection();
        break;
      case MENU_SAVE:
      case MENU_LOAD:
        drawSaveLoadSelection();
        break;
      case MENU_TEMPO:
        drawTempo();
        break;
    }
  }
}

void drawEuclideanRhythms() {
  if (showOverlay) return;  // Exit
  // draw hits line : 2~16hits if not muted
  int buf_count = 0;
  for (int k = 0; k < MAX_CHANNELS; k++) {  // Iterate over each channel
    buf_count = 0;
    // Collect the hit points
    for (int m = 0; m < MAX_STEPS; m++) {
      if (currentConfig.mute[k] == 0 && STEP_ON(k, m)) {
        int x_pos = X16(m) + GX(k);
        int y_pos = Y16(m) + GY(k);
        if (x_pos < 128 && y_pos < 64) {
          line_xbuf[buf_count] = x_pos;
          line_ybuf[buf_count] = y_pos;
          buf_count++;
        }
      }
    }

    // Draw the shape
    for (int j = 0; j < buf_count - 1; j++) {
      display.drawLine(line_xbuf[j], line_ybuf[j], line_xbuf[j + 1], line_ybuf[j + 1], WHITE);
    }
    if (buf_count > 0) {
      display.drawLine(line_xbuf[0], line_ybuf[0], line_xbuf[buf_count - 1], line_ybuf[buf_count - 1], WHITE);
    }
  }

  for (int j = 0; j < MAX_STEPS; j++) {  //line_buf reset
    line_xbuf[j] = 0;
    line_ybuf[j] = 0;
  }

  // draw hits line : 1hits if not muted
  for (int k = 0; k < MAX_CHANNELS; k++) {  // Channel count
    if (currentConfig.mute[k] == 0) {       // don't draw when muted
      if (currentConfig.hits[k] == 1) {
        int x1 = 15 + GX(k);
        int y1 = 15 + GY(k);
        int x2 = X16((currentConfig.offset[k] + 8) % 16) + GX(k);  // Adjust the offset to draw in the correct direction
        int y2 = Y16(currentConfig.offset[k]) + GY(k);
        if (x1 < 128 && y1 < 64 && x2 < 128 && y2 < 64) {
          display.drawLine(x1, y1, x2, y2, WHITE);
        }
      }
    }
  }

  //draw play step circle
  for (int k = 0; k < MAX_CHANNELS; k++) {                                 //ch count
    if (currentConfig.mute[k] == 0 && selected_setting != SETTING_PROB) {  //mute on = no display circle
      if (!STEP_ON(k, playing_step[k])) {
        display.drawCircle(X16(playing_step[k]) + GX(k), Y16(playing_step[k]) + GY(k), 2, WHITE);
      }
      if (STEP_ON(k, playing_step[k])) {
        display.fillCircle(X16(playing_step[k]) + GX(k), Y16(playing_step[k]) + GY(k), 3, WHITE);
      }
    }
  }

  // Draw 'M' for muted channels
  for (int k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k]) {
      int centerX = GX(k) + 15;  // Center of the channel's area
      int centerY = GY(k) + 15;
      display.setCursor(centerX - 3, centerY - 4);  // Adjust cursor to center the 'M'
      display.print('M');
    }
  }

  // draw channel info in edit mode, should be helpful while editing.
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = GX(ch);
    int y_base = GY(ch) + 8;


    // Draw hits info if not muted only
    if (currentConfig.mute[ch] == 0 && currentConfig.hits[ch] > 9 && selected_setting != SETTING_LIMIT && selected_setting != SETTING_MUTE) {  // Display only if there is space in the UI
      if (x_base + 10 < 120 && y_base < 56) {
        display.setCursor(x_base + 10, y_base);  // Adjust position
        display.print(currentConfig.hits[ch]);
        display.setCursor(x_base + 13, y_base + 8);
        display.println('H');
      }
    }

    // draw selected parameter UI for currently active channel when editing
    if (selected_setting != SETTING_TOP_MENU) {
      switch (selected_setting) {
        case SETTING_HITS: break;
        case SETTING_OFFSET: break;
        case SETTING_LIMIT:
          if (currentConfig.limit[ch] > 0 && currentConfig.hits[ch] > 6) {
            // draw line indicator from center to limit point
            int x1 = 15 + GX(ch);
            int y1 = 15 + GY(ch);
            int x2 = X16(currentConfig.limit[ch] % 16) + GX(ch);
            int y2 = Y16(currentConfig.limit[ch] % 16) + GY(ch);
            if (x1 < 128 && y1 < 64 && x2 < 128 && y2 < 64) {
              display.drawLine(x1, y1, x2, y2, WHITE);
            }
          }
          break;
      }
    }
  }
}

void drawProbabilityConfig() {
  if (selected_setting != SETTING_PROB) return;  // Exit early
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int barWidth = 4;
    int maxHeight = 15;
    int margin = 2;

    int bar_x = GX(ch) + 12;
    int bar_y = GY(ch) + 30;

    int barHeight = map(currentConfig.probability[ch], 0, 100, 0, maxHeight);
    int startY = bar_y - barHeight;

    int outerWidth = barWidth + 2 * margin;
    int outerHeight = maxHeight + 2 * margin;
    int outerX = bar_x - margin;
    int outerY = bar_y - maxHeight - margin;

    int text_x = outerX + (outerWidth / 2) - 6;
    int text_y = outerY - 10;

    text_x = constrain(text_x, 0, 128);
    text_y = constrain(text_y, 0, 64);

    display.setCursor(text_x, text_y);
    display.print(currentConfig.probability[ch]);

    display.drawRoundRect(outerX, outerY, outerWidth, outerHeight, 2, WHITE);
    display.fillRoundRect(bar_x, startY, barWidth, barHeight, 2, WHITE);
  }
}

//  Display selected slot
void drawSaveLoadSelection() {
  display.setCursor(24, 2);
  display.print(selected_menu == MENU_SAVE ? F("Save to Slot") : F("Load from Slot"));
  display.setCursor(58, 24);
  display.setTextSize(2);
  display.print(selected_slot + 1, DEC);
  display.setTextSize(1);
  display.setCursor(20, SCREEN_HEIGHT - 10);
  display.print(selected_menu == MENU_SAVE ? F("long PRESS Save") : F("long PRESS Load"));
}

void drawPresetSelection() {
  char presetName[sizeof(PresetPattern::name)];
  memcpy_P(&presetName, &defaultSlots[selected_preset].name, sizeof(presetName));
  display.setCursor(24, 2);
  display.println(F("Select Preset"));
  display.setCursor(24, 24);
  display.setTextSize(2);
  display.print(presetName);
  display.setTextSize(1);
  display.setCursor(10, SCREEN_HEIGHT - 10);
  display.print(F("long PRESS to LOAD"));
}

void drawTempo() {
  int16_t x1 = 10, y1 = 10;
  uint16_t w = 108, h = 44;  // Increased width and height
  uint16_t b = 4;
  uint16_t b2 = 8;

  // Clear screen underneath and draw the rectangle
  //display.fillRect(x1 - b, y1 - b, w + b2, h + b2, BLACK);
  //display.drawRoundRect(x1, y1, w, h, 2, WHITE);

  if (internalClock) {
    display.setCursor(x1 + 12, 8);
    display.print(F("Dial in Tempo"));

    // Calculate the width of the filled portion based on the tempo
    // Integer math: this was the only float use and pulled ~1 KB of float code in.
    int filledWidth = (long)(tempo - 40) * (w - 22) / (240 - 40);  // margin on each side

    // Draw the rounded rectangle
    display.drawRoundRect(x1 + 10, y1 + h / 2 - 5, w - 20, 10, 2, WHITE);

    // Draw the filled portion
    display.fillRoundRect(x1 + 11, y1 + h / 2 - 3, filledWidth, 6, 2, WHITE);

    display.setCursor(12, 45);
    display.print(F("40"));

    display.setCursor(50, 45);
    display.print(tempo);

    display.setCursor(95, 45);
    display.print(F("240"));

  } else {
    if (externalBPM == 0) {
      display.setCursor((SCREEN_WIDTH - 12 * 6) / 2, 10);
      display.setTextSize(1);
      display.print(F("Patch CLK or"));
      display.setCursor((SCREEN_WIDTH - 14 * 6) / 2, y1 + h / 2 - 6);
      display.print(F("long press for"));
      display.setCursor((SCREEN_WIDTH - 8 * 6) / 2, y1 + h / 2 + 8);
      display.print(F("INT CLK"));
    } else {
      display.setCursor(40, 10);
      display.print(F("Ext BPM"));
      display.setCursor(55, 35);
      //display.setTextSize(2);
      display.print(externalBPM, DEC);
      display.setTextSize(1);
    }
  }
}

void updateRythm() {
  uint16_t m[MAX_CHANNELS];
  uint8_t lim[MAX_CHANNELS], pr[MAX_CHANNELS], mutes = 0;
  for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
    uint8_t hits = currentConfig.hits[i] < MAX_PATTERNS ? currentConfig.hits[i] : MAX_PATTERNS - 1;
    uint8_t offset = currentConfig.offset[i] % MAX_STEPS;
    uint16_t mask = 0;
    for (uint8_t j = 0; j < MAX_STEPS; ++j) {
      if (pgm_read_byte(&euc16[hits][(j + offset) % MAX_STEPS])) mask |= 1u << j;
    }
    m[i] = mask;
    lim[i] = constrain(currentConfig.limit[i], 1, MAX_STEPS);
    pr[i] = currentConfig.probability[i];
    if (currentConfig.mute[i]) mutes |= 1 << i;
  }
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
      stepMask[i] = m[i];
      stepLimit[i] = lim[i];
      stepProb[i] = pr[i];
    }
    muteMask = mutes;
  }
}
