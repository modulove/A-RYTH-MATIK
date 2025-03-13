/**
 * @file ARYTHMATIK_Euclid.ino
 * @author Modulove & friends
 * @brief 6CH Eurorack (HAGIWO) euclidean Rhythm Generator
 * @version 0.1
 * @date 2024-04-23
 *
 * @copyright Copyright (c) 2024
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
 * - rotate CCW: quick access menue for sequence Reset & Mute (WIP)
 *
 */

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EncoderButton.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Configuration flags
//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

#if defined(__LGT8FX8P__)
#define LGT8FX_BOARD
#endif

#ifdef LGT8FX_BOARD
#define NUM_MEMORY_SLOTS 10
#define NUM_PRESETS 10  // 10 for LGT8FX8P
#else
#define NUM_MEMORY_SLOTS 20
#define NUM_PRESETS 20  // All presets for Nano
#endif

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

byte playing_step[6] = { 0 };

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

//const byte graph_x[6] PROGMEM = { 0, 40, 80, 15, 55, 95 }, graph_y[6] PROGMEM = { 0, 0, 0, 32, 32, 32 };
const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 }, graph_y[6] = { 0, 0, 0, 32, 32, 32 };

byte line_xbuf[17];
byte line_ybuf[17];

//const byte x16[16] PROGMEM = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, y16[16] PROGMEM = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };
const byte x16[16] = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 }, y16[16] = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//Sequence variables
constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;  // 250;  // 125;  // Used by fast inputs like encoder rotation to throttle the display refresh. (3 steps)
unsigned long gate_timer = 0;
unsigned long last_clock_input = 0;
unsigned long internalClockTimer = 0;

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
bool offset_buf[MAX_CHANNELS][MAX_STEPS];  //offset buffer , Stores the offset result

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

// Updated default (preset) configuration with names and rhythm patterns
// presets with conditional size
const SlotConfiguration defaultSlots[NUM_PRESETS] PROGMEM = {
  // Define only the first 10 presets for LGT8FX_BOARD
  { { 4, 4, 7, 4, 4, 4 }, { 0, 2, 3, 2, 1, 0 }, { false, false, false, false, false, false }, { 16, 16, 12, 8, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Techno", 130 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 1, 0, 1 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Funk", 110 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 3, 1, 0, 2 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Swing", 140 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 2, 3, 2, 0, 2 }, { false, false, false, false, false, false }, { 8, 8, 8, 8, 8, 8 }, { 100, 100, 100, 100, 100, 100 }, "Samba", 100 },
  { { 4, 3, 4, 3, 4, 3 }, { 0, 2, 0, 2, 0, 2 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Waltz", 90 },
  { { 2, 2, 3, 3, 3, 3 }, { 0, 2, 0, 2, 0, 2 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "BossaN", 120 },
  { { 7, 7, 7, 7, 7, 7 }, { 0, 2, 4, 6, 8, 10 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Bell", 100 },
  { { 2, 2, 3, 1, 2, 1 }, { 0, 1, 0, 2, 1, 0 }, { false, false, false, false, false, false }, { 24, 18, 24, 21, 16, 30 }, { 100, 100, 100, 100, 100, 100 }, "Gen", 80 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 2, 0, 3 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Gahu", 120 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 1, 0, 1, 0, 2 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Threes", 120 },
// Additional presets for Arduino Nano
#ifndef LGT8FX_BOARD
  { { 9, 9, 9, 9, 9, 9 }, { 0, 1, 2, 3, 4, 5 }, { false, false, false, false, false, false }, { 8, 8, 8, 8, 8, 8 }, { 100, 100, 100, 100, 100, 100 }, "Aksak", 120 },
  { { 5, 5, 5, 5, 5, 5 }, { 0, 2, 4, 6, 8, 10 }, { false, false, false, false, false, false }, { 8, 8, 8, 8, 8, 8 }, { 100, 100, 100, 100, 100, 100 }, "Rumba", 120 },
  { { 3, 4, 3, 4, 3, 4 }, { 0, 2, 1, 3, 2, 4 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Clave", 120 },
    { { 3, 3, 3, 3, 3, 3 }, { 0, 1, 2, 3, 4, 5 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Zarbi", 120 },
  { { 5, 5, 5, 5, 5, 5 }, { 0, 1, 2, 3, 4, 5 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Nawkht", 120 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 0, 1, 0, 1 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "FumeF", 120 },
  { { 5, 3, 5, 3, 5, 3 }, { 0, 2, 0, 2, 0, 2 }, { false, false, false, false, false, false }, { 12, 12, 12, 12, 12, 12 }, { 100, 100, 100, 100, 100, 100 }, "Djembe", 120 },
  { { 4, 4, 4, 4, 4, 4 }, { 0, 1, 2, 3, 4, 5 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Gahu44", 120 },
  { { 3, 3, 3, 3, 3, 3 }, { 0, 2, 4, 1, 3, 5 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "Takita", 120 },
  { { 16, 8, 4, 2, 1, 1 }, { 0, 0, 0, 0, 0, 0 }, { false, false, false, false, false, false }, { 16, 16, 16, 16, 16, 16 }, { 100, 100, 100, 100, 100, 100 }, "ClkDiv", 120 },
#endif
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
        
        display.display();
    }
}

void printDebugMessage(const char *message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(message);
  display.display();
  delay(100);
}

void Random_change(bool includeMute, bool allChannels, byte select_ch = 0) {
  static constexpr uint8_t MAX_RANDOM = 100;
  for (uint8_t k = 0; k < MAX_CHANNELS; k++) {
    if (!allChannels && k != select_ch) continue;

    uint8_t random_value = random(1, MAX_RANDOM);
    if (pgm_read_byte(&hit_occ[k]) >= random_value) {
      currentConfig.hits[k] = random(pgm_read_byte(&hit_rng_min[k]), pgm_read_byte(&hit_rng_max[k]) + 1);
    }
    if (pgm_read_byte(&off_occ[k]) >= random_value) {
      currentConfig.offset[k] = random(0, MAX_STEPS);
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
  initDisplay();

// boot logo animation only on nano for now
#if !defined(LGT8FX_BOARD) && !defined(DISABLE_BOOT_LOGO)
  drawAnimation();
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

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  // Initialize the last external clock time to the current time
  last_clock_input = millis();
  internalClockTimer = millis();
}

void loop() {
  encoder.update();  // Process Encoder & button updates
  //display.fillScreen(BLACK);

  updateRythm();

  //-----------------trigger detect, reset & output----------------------
  bool rst_in = RESET::isInputHigh(), trg_in = CLK::isInputHigh();
  bool beat_start = false;

  // Handle reset input
  if (old_rst_in == 0 && rst_in == 1) {
    resetSeq();
    //force_refresh = true;
    disp_refresh = true;
  }

  // External clock detection and response
  if (!internalClock && old_trg_in == 0 && trg_in == 1) {
    beat_start = true;
    last_clock_input = millis();
    static unsigned long lastPulseTime = 0;
    unsigned long currentTime = millis();
    unsigned long pulseInterval = currentTime - lastPulseTime;
    if (pulseInterval > 0) {
      externalBPM = 60000 / (pulseInterval * 4);  // Convert 16th note pulse interval to BPM
      if (externalBPM > 360) {                    // Limit BPM to avoid high speed issues
        externalBPM = 360;
      }
      period = 60000 / externalBPM / 4;
    }

    lastPulseTime = currentTime;
  }

  // Switch to internal clock if no clock input received for set duration.
  if (internalClock && (millis() - internalClockTimer >= period)) {
    beat_start = true;
    internalClockTimer = millis();
  }

  if (beat_start) {
    gate_timer = millis();
    force_refresh = true;
    CLK_LED::setOutput(1);
    debug = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
      playing_step[i]++;
      if (playing_step[i] >= currentConfig.limit[i]) {
        playing_step[i] = 0;  // Step limit is reached
      }
    }

    // Output gate signal
    outputGateSignals();

    disp_refresh = true;  // Updates the display where the trigger was entered.

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


  if (gate_timer + 10 <= millis()) {  //off all gate , gate time is 10msec
    OUTPUT1::setOutput(0);
    OUTPUT2::setOutput(0);
    OUTPUT3::setOutput(0);
    OUTPUT4::setOutput(0);
    OUTPUT5::setOutput(0);
    OUTPUT6::setOutput(0);
    CLK_LED::setOutput(0);  // Turn off the clock LED, so only on briefly after a clock impulse is received
  }
  if (gate_timer + 30 <= millis()) {  //off all gate , gate time is 10msec, reduced from 100 ms to 30 ms
    LED1::setOutput(0);
    LED2::setOutput(0);
    LED3::setOutput(0);
    LED4::setOutput(0);
    LED5::setOutput(0);
    LED6::setOutput(0);
  }

  OLED_display();  // refresh display

  old_trg_in = trg_in;
  old_rst_in = rst_in;
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
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  Wire.setClock(400000L);  // Set I2C clock to 400kHz to see if this improves perfomance ToDo: measure this!
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

void outputGateSignals() {
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (offset_buf[i][playing_step[i]] == 1 && currentConfig.mute[i] == 0 && random(100) < currentConfig.probability[i]) {
      switch (i) {
        case 0:
          OUTPUT1::setOutput(1);
          LED1::setOutput(1);
          break;
        case 1:
          OUTPUT2::setOutput(1);
          LED2::setOutput(1);
          break;
        case 2:
          OUTPUT3::setOutput(1);
          LED3::setOutput(1);
          break;
        case 3:
          OUTPUT4::setOutput(1);
          LED4::setOutput(1);
          break;
        case 4:
          OUTPUT5::setOutput(1);
          LED5::setOutput(1);
          break;
        case 5:
          OUTPUT6::setOutput(1);
          LED6::setOutput(1);
          break;
      }
    }
  }
}

bool isClockRunning() {
  return internalClock || (millis() - last_clock_input < period);
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
        selected_preset = (selected_preset + increment + sizeof(defaultSlots) / sizeof(SlotConfiguration)) % (sizeof(defaultSlots) / sizeof(SlotConfiguration));
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
    memcpy_P(&currentConfig, &defaultSlots[0], sizeof(SlotConfiguration));  // Load the first default slot as the initial configuration
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
  memcpy_P(config, &defaultSlots[index], sizeof(SlotConfiguration));
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
  } else {
    // Handle error
    printDebugMessage("EEPROM Save Error");
    return;
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
  } else {
    // Handle the error
    printDebugMessage("EEPROM Load Error");
  }
}

void loadFromPreset(int preset) {
  if (preset >= sizeof(defaultSlots) / sizeof(SlotConfiguration)) preset = 0;
  loadDefaultConfig(&currentConfig, preset);
  tempo = currentConfig.tempo;
  internalClock = currentConfig.internalClock;
  selected_preset = preset;
  period = 60000 / tempo / 4;
}

void initializeDefaultRhythms() {
  for (int i = 0; i < NUM_PRESETS; i++) {
    SlotConfiguration config;
    memcpy_P(&config, &defaultSlots[i % (sizeof(defaultSlots) / sizeof(SlotConfiguration))], sizeof(SlotConfiguration));
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

void drawTopMenuRight(TopMenu select_ch) {
  if (showOverlay) return;  // Exit
  switch (select_ch) {
    case MENU_CH_1: rightMenu('1', 'H', 'O', ' '); break;
    case MENU_CH_2: rightMenu('2', 'H', 'O', ' '); break;
    case MENU_CH_3: rightMenu('3', 'H', 'O', ' '); break;
    case MENU_CH_4: rightMenu('4', 'H', 'O', ' '); break;
    case MENU_CH_5: rightMenu('5', 'H', 'O', ' '); break;
    case MENU_CH_6: rightMenu('6', 'H', 'O', ' '); break;
    case MENU_RANDOM_ADVANCE: rightMenu('R', 'N', 'D', ' '); break;
    case MENU_SAVE: rightMenu('S', ' ', ' ', ' '); break;
    case MENU_LOAD: rightMenu('L', ' ', ' ', ' '); break;
    case MENU_PRESET: rightMenu('P', ' ', ' ', ' '); break;
    case MENU_ALL_RESET: rightMenu('R', ' ', ' ', ' '); break;
    case MENU_ALL_MUTE: rightMenu('M', ' ', ' ', ' '); break;
    case MENU_RAND: rightMenu('X', ' ', ' ', ' '); break;
    case MENU_TEMPO:
      if (internalClock) {
        rightMenu('I', 'N', 'T', ' ');
      } else {
        rightMenu('E', 'X', 'T', ' ');
      }
      break;
    default: break;
  }
}

// left side menue - Channel Settings
void drawChannelEditMenu(TopMenu select_ch, Setting select_menu) {
  if (showOverlay) return;  // Exit
  switch (select_menu) {
    case SETTING_HITS: leftMenu('H', 'I', 'T', 'S'); break;
    case SETTING_OFFSET: leftMenu('O', 'F', 'F', 'S'); break;
    case SETTING_LIMIT: leftMenu('L', 'I', 'M', 'I'); break;
    case SETTING_MUTE: leftMenu('M', 'U', 'T', 'E'); break;
    case SETTING_RESET: leftMenu('R', 'S', 'E', 'T'); break;
    case SETTING_RANDOM: leftMenu('R', 'A', 'N', 'D'); break;
    case SETTING_PROB: leftMenu('P', 'R', 'O', 'B'); break;
    default: break;
  }
}

// left side menue - Menu Options
void drawModeMenu(TopMenu select_ch) {
  if (showOverlay) return;  // Exit
  switch (select_ch) {
    case MENU_SAVE: leftMenu('S', 'A', 'V', 'E'); break;
    case MENU_LOAD: leftMenu('L', 'O', 'A', 'D'); break;
    case MENU_ALL_RESET: leftMenu('R', 'S', 'E', 'T'); break;
    case MENU_ALL_MUTE: leftMenu('M', 'U', 'T', 'E'); break;
    case MENU_PRESET: leftMenu('P', 'R', 'S', 'T'); break;
    case MENU_TEMPO: leftMenu('*', 'C', 'L', 'K'); break;
    case MENU_RAND: leftMenu('R', 'A', 'N', 'D'); break;
    default: break;
  }
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
  if (select_menu == SETTING_TOP_MENU) display.drawTriangle(113, 0, 113, 6, 118, 3, WHITE);
  else if (select_menu == SETTING_HITS) display.drawTriangle(113, 9, 113, 15, 118, 12, WHITE);
  else if (select_menu == SETTING_OFFSET) display.drawTriangle(113, 18, 113, 24, 118, 21, WHITE);

  if (select_menu == SETTING_LIMIT) display.drawTriangle(12, 34, 12, 41, 7, 37, WHITE);
  else if (select_menu == SETTING_MUTE) display.drawTriangle(12, 42, 12, 49, 7, 45, WHITE);
  else if (select_menu == SETTING_RESET) display.drawTriangle(12, 50, 12, 57, 7, 53, WHITE);
  else if (select_menu == SETTING_RANDOM) display.drawTriangle(12, 58, 12, 65, 7, 61, WHITE);
  else if (select_menu == SETTING_PROB) display.drawTriangle(12, 66, 12, 73, 7, 69, WHITE);
}

void drawStepDots(const SlotConfiguration &currentConfig) {
  if (showOverlay) return;
  for (int k = 0; k < MAX_CHANNELS; k++) {
    for (int j = 0; j < currentConfig.limit[k]; j++) {
      int x_pos = x16[j % 16] + graph_x[k];
      int y_pos = y16[j % 16] + graph_y[k];
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

  display.display();
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
      if (currentConfig.mute[k] == 0 && offset_buf[k][m] == 1) {
        int x_pos = x16[m] + graph_x[k];
        int y_pos = y16[m] + graph_y[k];
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
        int x1 = 15 + graph_x[k];
        int y1 = 15 + graph_y[k];
        int x2 = x16[(currentConfig.offset[k] + 8) % 16] + graph_x[k];  // Adjust the offset to draw in the correct direction
        int y2 = y16[currentConfig.offset[k]] + graph_y[k];
        if (x1 < 128 && y1 < 64 && x2 < 128 && y2 < 64) {
          display.drawLine(x1, y1, x2, y2, WHITE);
        }
      }
    }
  }

  //draw play step circle
  for (int k = 0; k < MAX_CHANNELS; k++) {                                 //ch count
    if (currentConfig.mute[k] == 0 && selected_setting != SETTING_PROB) {  //mute on = no display circle
      if (offset_buf[k][playing_step[k]] == 0) {
        display.drawCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 2, WHITE);
      }
      if (offset_buf[k][playing_step[k]] == 1) {
        display.fillCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 3, WHITE);
      }
    }
  }

  // Draw 'M' for muted channels
  for (int k = 0; k < MAX_CHANNELS; k++) {
    if (currentConfig.mute[k]) {
      int centerX = graph_x[k] + 15;  // Center of the channel's area
      int centerY = graph_y[k] + 15;
      display.setCursor(centerX - 3, centerY - 4);  // Adjust cursor to center the 'M'
      display.print('M');
    }
  }

  // draw channel info in edit mode, should be helpful while editing.
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;


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
            int x1 = 15 + graph_x[ch];
            int y1 = 15 + graph_y[ch];
            int x2 = x16[currentConfig.limit[ch] % 16] + graph_x[ch];
            int y2 = y16[currentConfig.limit[ch] % 16] + graph_y[ch];
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

    int bar_x = graph_x[ch] + 12;
    int bar_y = graph_y[ch] + 30;

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
  char presetName[10];
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
    float tempoRatio = (float)(tempo - 40) / (240 - 40);
    int filledWidth = tempoRatio * (w - 22);  // margin on each side

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
  for (uint8_t i = 0; i < MAX_CHANNELS; ++i) {
    uint8_t hits = currentConfig.hits[i];
    uint8_t offset = currentConfig.offset[i];
    for (uint8_t j = 0; j < MAX_STEPS; ++j) {
      offset_buf[i][j] = pgm_read_byte(&euc16[hits][(j + offset) % MAX_STEPS]);
    }
  }
}
