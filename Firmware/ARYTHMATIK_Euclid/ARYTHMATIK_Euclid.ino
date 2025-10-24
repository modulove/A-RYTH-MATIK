/**
  * @file ARYTHMATIK_Euclid.ino
  * @brief 6CH Eurorack Euclidean Rhythm Generator - AGGRESSIVELY OPTIMIZED
  * @version 0.9.3
  * @date 2025-10-09
  * 
  * OPTIMIZATIONS:
  * - Simplified boot config (saves ~250 bytes)
  * - Reduced overlay complexity (saves ~200 bytes)
  * - Optimized display functions (saves ~300 bytes)
  * - Shortened strings (saves ~150 bytes)
  * Total savings: ~900 bytes
  */

#include <avr/pgmspace.h>
#include <EncoderButton.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#if defined(__LGT8FX8P__)
  #define LGT8FX_BOARD
#endif

#ifdef LGT8FX_BOARD
  #define NUM_MEMORY_SLOTS 10
  #define NUM_PRESETS 10
#else
  #define NUM_MEMORY_SLOTS 20
  #define NUM_PRESETS 20
#endif

#ifdef ROTATE_PANEL
  #define RESET_BIT 5
  #define RESET_PORT PORTB
  #define RESET_DDR DDRB
  #define RESET_PIN PINB
  #define CLK_BIT 3
  #define CLK_PORT PORTB
  #define CLK_DDR DDRB
  #define CLK_PIN PINB
  #define OUTPUT1_BIT 0
  #define OUTPUT1_PORT PORTB
  #define OUTPUT1_DDR DDRB
  #define OUTPUT2_BIT 1
  #define OUTPUT2_PORT PORTB
  #define OUTPUT2_DDR DDRB
  #define OUTPUT3_BIT 2
  #define OUTPUT3_PORT PORTB
  #define OUTPUT3_DDR DDRB
  #define OUTPUT4_BIT 5
  #define OUTPUT4_PORT PORTD
  #define OUTPUT4_DDR DDRD
  #define OUTPUT5_BIT 6
  #define OUTPUT5_PORT PORTD
  #define OUTPUT5_DDR DDRD
  #define OUTPUT6_BIT 7
  #define OUTPUT6_PORT PORTD
  #define OUTPUT6_DDR DDRD
  #define LED1_BIT 0
  #define LED1_PORT PORTD
  #define LED1_DDR DDRD
  #define LED2_BIT 1
  #define LED2_PORT PORTD
  #define LED2_DDR DDRD
  #define LED3_BIT 3
  #define LED3_PORT PORTC
  #define LED3_DDR DDRC
  #define LED4_BIT 0
  #define LED4_PORT PORTC
  #define LED4_DDR DDRC
  #define LED5_BIT 1
  #define LED5_PORT PORTC
  #define LED5_DDR DDRC
  #define LED6_BIT 2
  #define LED6_PORT PORTC
  #define LED6_DDR DDRC
#else
  #define RESET_BIT 3
  #define RESET_PORT PORTB
  #define RESET_DDR DDRB
  #define RESET_PIN PINB
  #define CLK_BIT 5
  #define CLK_PORT PORTB
  #define CLK_DDR DDRB
  #define CLK_PIN PINB
  #define OUTPUT1_BIT 5
  #define OUTPUT1_PORT PORTD
  #define OUTPUT1_DDR DDRD
  #define OUTPUT2_BIT 6
  #define OUTPUT2_PORT PORTD
  #define OUTPUT2_DDR DDRD
  #define OUTPUT3_BIT 7
  #define OUTPUT3_PORT PORTD
  #define OUTPUT3_DDR DDRD
  #define OUTPUT4_BIT 0
  #define OUTPUT4_PORT PORTB
  #define OUTPUT4_DDR DDRB
  #define OUTPUT5_BIT 1
  #define OUTPUT5_PORT PORTB
  #define OUTPUT5_DDR DDRB
  #define OUTPUT6_BIT 2
  #define OUTPUT6_PORT PORTB
  #define OUTPUT6_DDR DDRB
  #define LED1_BIT 0
  #define LED1_PORT PORTC
  #define LED1_DDR DDRC
  #define LED2_BIT 1
  #define LED2_PORT PORTC
  #define LED2_DDR DDRC
  #define LED3_BIT 2
  #define LED3_PORT PORTC
  #define LED3_DDR DDRC
  #define LED4_BIT 0
  #define LED4_PORT PORTD
  #define LED4_DDR DDRD
  #define LED5_BIT 1
  #define LED5_PORT PORTD
  #define LED5_DDR DDRD
  #define LED6_BIT 3
  #define LED6_PORT PORTC
  #define LED6_DDR DDRC
#endif

#define CLK_LED_BIT 4
#define CLK_LED_PORT PORTD
#define CLK_LED_DDR DDRD

#define SET_INPUT(ddr, bit) ((ddr) &= ~(1 << (bit)))
#define SET_OUTPUT(ddr, bit) ((ddr) |= (1 << (bit)))
#define WRITE_HIGH(port, bit) ((port) |= (1 << (bit)))
#define WRITE_LOW(port, bit) ((port) &= ~(1 << (bit)))
#define READ_PIN(pin_reg, bit) (((pin_reg) >> (bit)) & 1)

const byte ENCODER_PIN1 = 2;
const byte ENCODER_PIN2 = 3;
const byte ENCODER_SW_PIN = 12;

enum TopMenu {
  MENU_CH_1, MENU_CH_2, MENU_CH_3, MENU_CH_4, MENU_CH_5, MENU_CH_6,
  MENU_RANDOM_ADVANCE, MENU_RAND, MENU_SAVE, MENU_LOAD, MENU_PRESET,
  MENU_TEMPO, MENU_ALL_RESET, MENU_ALL_MUTE, MENU_LAST
};

enum Setting {
  SETTING_TOP_MENU, SETTING_HITS, SETTING_OFFSET, SETTING_LIMIT,
  SETTING_MUTE, SETTING_RESET, SETTING_RANDOM, SETTING_PROB, SETTING_LAST
};

#define FIRMWARE_MAGIC "EUCLID11"
#define FIRMWARE_MAGIC_ADDRESS 0
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define EEPROM_START_ADDRESS 20

struct SlotConfiguration {
  byte hits[6];
  byte offset[6];
  bool mute[6];
  byte limit[6];
  byte probability[6];
  char name[10];
  int tempo;
  bool internalClock;
  byte lastUsedSlot;
  byte selectedPreset;
  bool lastLoadedFromPreset;
};

#define CONFIG_SIZE (sizeof(SlotConfiguration))
#define LAST_USED_SLOT_ADDRESS (EEPROM_START_ADDRESS + NUM_MEMORY_SLOTS * CONFIG_SIZE)
#define CONFIG_SETTINGS_ADDRESS (LAST_USED_SLOT_ADDRESS + sizeof(byte))

struct RandomProfile {
  uint8_t hit_occ[6];
  uint8_t off_occ[6];
  uint8_t mute_occ[6];
  uint8_t hit_rng_max[6];
  uint8_t hit_rng_min[6];
};

struct ConfigSettings {
  bool encoderReversed;
  byte displayRotation;
  char magic[8];
};

ConfigSettings configSettings = { false, 2, "CONFIG2" };

bool trg_in = false, old_trg_in = false, rst_in = false, old_rst_in = false;
byte playing_step[6] = {0};
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
bool isEncoderReversed = false;
byte displayRotation = 2;

int tempo = 120;
int period = 60000 / 120 / 4;
int externalBPM = 0;

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 };
const byte graph_y[6] = { 0, 0, 0, 32, 32, 32 };
byte line_xbuf[17], line_ybuf[17];
const byte x16[16] = { 15,21,26,29,30,29,26,21,15,9,4,1,0,1,4,9 };
const byte y16[16] = { 0,1,4,9,15,21,26,29,30,29,26,21,15,9,4,1 };

constexpr uint8_t MAX_CHANNELS = 6;
constexpr uint8_t MAX_STEPS = 16;
constexpr uint8_t MAX_PATTERNS = 17;
const int MIN_REFRESH_DURATION = 375;
unsigned long gate_timer = 0;
unsigned long last_clock_input = 0;
unsigned long internalClockTimer = 0;

const static byte euc16[MAX_PATTERNS][MAX_STEPS] PROGMEM = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0},
  {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0},
  {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0},
  {1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0},
  {1,0,0,1,0,1,0,0,1,0,0,1,0,1,0,0},
  {1,0,0,1,0,1,0,1,0,0,1,0,1,0,1,0},
  {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
  {1,0,1,1,0,1,0,1,0,1,1,0,1,0,1,0},
  {1,0,1,1,0,1,0,1,1,0,1,1,0,1,0,1},
  {1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1},
  {1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1},
  {1,0,1,1,1,1,0,1,1,1,1,0,1,1,1,1},
  {1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

bool offset_buf[MAX_CHANNELS][MAX_STEPS];
const int bar_max[MAX_CHANNELS] PROGMEM = { 2, 4, 6, 8, 12, 16 };
byte bar_now = 1;
byte bar_select = 1;
byte step_cnt = 0;

EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);

const SlotConfiguration defaultSlots[NUM_PRESETS] PROGMEM = {
  {{4,8,2,16,3,5},{0,2,8,0,5,3},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,100,90,85},"Techno",128},
  {{4,8,2,4,7,3},{0,2,8,4,1,6},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,100,85,75},"House",125},
  {{3,7,2,5,11,4},{0,3,8,2,0,5},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,85,100,70,80,65},"DnB",174},
  {{2,3,7,5,5,4},{0,8,2,6,3,0},{0,0,0,0,0,0},{16,16,16,12,16,12},{100,100,95,90,85,90},"Samba",160},
  {{2,3,4,3,5,3},{0,2,0,8,4,6},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,95,95,90,85},"Bossa",120},
  {{3,2,5,4,7,3},{0,8,1,4,2,10},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,100,85,80},"Clave32",110},
  {{3,2,5,4,6,5},{0,9,2,5,1,7},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,95,85,85},"Rumba",120},
  {{4,7,5,3,11,6},{0,3,6,8,1,4},{0,0,0,0,0,0},{16,16,12,12,16,16},{100,95,100,90,85,85},"Afrobt",115},
  {{2,4,8,4,3,5},{0,8,2,4,10,6},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,85,90,95,80},"Reggae",85},
  {{3,7,2,5,9,4},{0,4,8,2,1,6},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,80,100,65,75,70},"Funk",105},
#ifndef LGT8FX_BOARD
  {{3,4,5,3,7,5},{0,3,6,9,1,4},{0,0,0,0,0,0},{12,12,12,12,12,12},{100,100,100,95,90,90},"Gahu",130},
  {{3,3,4,4,7,5},{0,4,2,6,1,8},{0,0,0,0,0,0},{12,12,12,12,12,12},{100,100,95,95,85,85},"Bembe",140},
  {{4,6,3,5,8,4},{0,2,8,4,1,6},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,95,100,90,85,85},"Soukous",140},
  {{3,3,5,4,8,6},{0,8,3,5,1,2},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,100,90,85},"Tresil",120},
  {{7,5,2,4,9,3},{0,2,8,4,1,10},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,90,100,95,80,75},"Cascar",95},
  {{5,7,3,4,5,6},{0,3,8,1,5,2},{0,0,0,0,0,0},{16,16,16,12,16,16},{100,95,100,95,85,85},"Baiao",130},
  {{3,3,3,6,9,4},{0,4,8,1,0,2},{0,0,0,0,0,0},{12,12,12,12,12,12},{100,100,100,95,85,80},"Triplet",120},
  {{4,5,1,3,7,2},{0,4,12,8,2,10},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,95,100,75,70,60},"Minimal",126},
  {{4,6,2,5,9,3},{0,4,8,2,1,7},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,90,100,65,80,75},"Breaks",95},
  {{16,8,4,2,1,1},{0,0,0,0,0,0},{0,0,0,0,0,0},{16,16,16,16,16,16},{100,100,100,100,100,100},"ClkDiv",120},
#endif
};

const RandomProfile randomProfiles[NUM_PRESETS] PROGMEM = {
  {{0, 70, 0, 60, 50, 40},{0, 50, 0, 30, 40, 35},{0, 10, 0, 20, 20, 30},{4, 16, 2, 16, 8, 8},{4, 4, 2, 1, 2, 2}},
  {{0, 80, 5, 65, 60, 50},{0, 60, 5, 40, 50, 40},{0, 15, 0, 25, 25, 35},{4, 16, 3, 8, 10, 6},{4, 6, 2, 2, 3, 2}},
  {{5, 70, 0, 50, 80, 60},{5, 60, 0, 40, 70, 50},{0, 20, 0, 30, 35, 40},{4, 12, 2, 8, 16, 8},{2, 4, 2, 2, 6, 2}},
  {{40, 60, 70, 65, 70, 60},{30, 50, 60, 55, 60, 50},{5, 15, 25, 30, 30, 35},{3, 5, 10, 8, 8, 6},{2, 2, 4, 3, 3, 2}},
  {{10, 20, 30, 25, 35, 30},{5, 15, 25, 20, 30, 25},{0, 5, 10, 15, 20, 25},{3, 4, 5, 4, 6, 4},{2, 2, 3, 2, 3, 2}},
  {{0, 0, 40, 35, 60, 55},{0, 0, 30, 25, 50, 45},{0, 0, 20, 25, 30, 35},{3, 2, 7, 6, 10, 5},{3, 2, 3, 2, 4, 2}},
  {{0, 0, 45, 40, 65, 60},{0, 0, 35, 30, 55, 50},{0, 0, 25, 30, 35, 40},{3, 2, 8, 6, 9, 7},{3, 2, 3, 2, 4, 3}},
  {{30, 60, 55, 45, 75, 65},{25, 50, 45, 40, 65, 55},{5, 15, 20, 25, 30, 30},{6, 10, 8, 5, 14, 9},{3, 4, 3, 2, 6, 3}},
  {{0, 50, 75, 45, 40, 60},{0, 40, 65, 35, 30, 50},{0, 20, 30, 25, 20, 40},{2, 6, 12, 6, 5, 8},{2, 2, 4, 2, 2, 3}},
  {{0, 65, 0, 60, 70, 55},{0, 55, 0, 50, 60, 45},{0, 20, 0, 25, 30, 35},{4, 12, 2, 8, 12, 6},{3, 5, 2, 3, 6, 2}},
#ifndef LGT8FX_BOARD
  {{20, 35, 45, 30, 55, 50},{15, 25, 35, 20, 45, 40},{0, 10, 15, 20, 25, 25},{4, 6, 7, 5, 10, 8},{2, 3, 3, 2, 5, 3}},
  {{20, 30, 40, 35, 60, 55},{15, 20, 30, 25, 50, 45},{0, 10, 15, 20, 25, 30},{4, 5, 6, 6, 10, 8},{2, 2, 3, 3, 5, 3}},
  {{25, 55, 35, 50, 70, 45},{20, 45, 25, 40, 60, 35},{5, 15, 10, 25, 30, 20},{6, 9, 5, 8, 12, 6},{3, 4, 2, 3, 6, 2}},
  {{5, 10, 35, 30, 60, 55},{0, 5, 25, 20, 50, 45},{0, 5, 15, 20, 30, 35},{3, 4, 7, 6, 11, 8},{3, 3, 3, 2, 5, 4}},
  {{15, 45, 0, 40, 75, 50},{10, 35, 0, 30, 65, 40},{0, 15, 0, 20, 35, 30},{9, 8, 2, 6, 13, 5},{5, 3, 2, 2, 7, 2}},
  {{35, 55, 25, 40, 50, 60},{25, 45, 15, 30, 40, 50},{5, 15, 10, 20, 25, 30},{7, 10, 5, 6, 8, 9},{3, 5, 2, 2, 3, 4}},
  {{15, 20, 25, 40, 60, 35},{10, 15, 20, 30, 50, 25},{0, 5, 10, 20, 30, 20},{4, 4, 4, 8, 12, 6},{2, 2, 2, 4, 6, 2}},
  {{20, 70, 50, 80, 85, 75},{15, 60, 40, 70, 80, 65},{10, 30, 25, 40, 45, 50},{6, 8, 3, 6, 12, 4},{3, 3, 1, 2, 4, 1}},
  {{10, 65, 0, 55, 75, 60},{5, 55, 0, 45, 65, 50},{0, 20, 0, 30, 35, 40},{6, 10, 2, 8, 13, 5},{3, 4, 2, 3, 7, 2}},
  {{0, 0, 0, 0, 0, 0},{0, 0, 0, 0, 0, 0},{0, 0, 0, 0, 0, 0},{16, 8, 4, 2, 1, 1},{16, 8, 4, 2, 1, 1}},
#endif
};

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS], currentConfig;
byte lastUsedSlot = 0;

void (*ClockHandler)() = NULL;
void (*ResetHandler)() = NULL;
volatile bool clockTriggerFlag = false;
volatile bool resetTriggerFlag = false;
volatile bool prev_clk_state = false;
volatile bool prev_rst_state = false;

void handleClockInterrupt() {
  clockTriggerFlag = true;
  last_clock_input = millis();
  static unsigned long lastPulseTime = 0;
  unsigned long now = millis();
  unsigned long pulseInterval = now - lastPulseTime;
  if (pulseInterval > 0 && lastPulseTime > 0) {
    externalBPM = 60000 / (pulseInterval * 4);
    if (externalBPM > 360) externalBPM = 360;
    period = 60000 / externalBPM / 4;
  }
  lastPulseTime = now;
}

void handleResetInterrupt() { resetTriggerFlag = true; }

ISR(PCINT0_vect) {
  bool current_clk_state = READ_PIN(CLK_PIN, CLK_BIT);
  bool current_rst_state = READ_PIN(RESET_PIN, RESET_BIT);
  if (!prev_clk_state && current_clk_state && ClockHandler) ClockHandler();
  if (!prev_rst_state && current_rst_state && ResetHandler) ResetHandler();
  prev_clk_state = current_clk_state;
  prev_rst_state = current_rst_state;
}

void setupPinChangeInterrupts() {
  if (ClockHandler == NULL) ClockHandler = handleClockInterrupt;
  if (ResetHandler == NULL) ResetHandler = handleResetInterrupt;
  cli();
  prev_clk_state = READ_PIN(CLK_PIN, CLK_BIT);
  prev_rst_state = READ_PIN(RESET_PIN, RESET_BIT);
  PCICR  |= (1 << PCIE0);
  PCMSK0 |= (1 << CLK_BIT) | (1 << RESET_BIT);
  sei();
}

void Random_change(bool includeMute, bool allChannels, byte select_ch = 0) {
  static constexpr uint8_t MAX_RANDOM = 100;
  byte activePreset = selected_preset;
  if (!currentConfig.lastLoadedFromPreset && selected_preset < NUM_PRESETS) activePreset = selected_preset;
  
  RandomProfile profile;
  if (activePreset < NUM_PRESETS) memcpy_P(&profile, &randomProfiles[activePreset], sizeof(RandomProfile));
  else memcpy_P(&profile, &randomProfiles[0], sizeof(RandomProfile));
  
  for (uint8_t k = 0; k < MAX_CHANNELS; k++) {
    if (!allChannels && k != select_ch) continue;
    uint8_t rv = random(1, MAX_RANDOM + 1);
    if (pgm_read_byte(&profile.hit_occ[k]) >= rv) {
      currentConfig.hits[k] = random(pgm_read_byte(&profile.hit_rng_min[k]), pgm_read_byte(&profile.hit_rng_max[k]) + 1);
    }
    rv = random(1, MAX_RANDOM + 1);
    if (pgm_read_byte(&profile.off_occ[k]) >= rv) currentConfig.offset[k] = random(0, MAX_STEPS);
    if (includeMute && k > 0) {
      rv = random(1, MAX_RANDOM + 1);
      currentConfig.mute[k] = pgm_read_byte(&profile.mute_occ[k]) >= rv;
    } else {
      currentConfig.mute[k] = false;
    }
  }
}

void onOverlayTimeout(EncoderButton &eb) { showOverlay = false; disp_refresh = true; }

void saveConfigSettings() {
  configSettings.encoderReversed = isEncoderReversed;
  configSettings.displayRotation = (displayRotation == 2) ? 2 : 0;
  strncpy(configSettings.magic, "CONFIG2", 8);
  EEPROM.put(CONFIG_SETTINGS_ADDRESS, configSettings);
}

void loadConfigSettings() {
  ConfigSettings loaded;
  EEPROM.get(CONFIG_SETTINGS_ADDRESS, loaded);
  if (strncmp(loaded.magic, "CONFIG2", 7) == 0) {
    isEncoderReversed = loaded.encoderReversed;
    displayRotation = (loaded.displayRotation == 2) ? 2 : 0;
  } else {
    isEncoderReversed = false;
    displayRotation = 2;
    saveConfigSettings();
  }
}

// ====== SIMPLIFIED Boot Config (saves ~250 bytes) ======
static byte bootSetting = 0;

void drawBootMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 10);
  display.print(F("BOOT: "));
  display.print(bootSetting == 0 ? F("ENC") : F("DISP"));
  display.setCursor(20, 30);
  if (bootSetting == 0) display.print(isEncoderReversed ? F("REV") : F("NORM"));
  else { display.print(displayRotation == 0 ? F("0") : F("180")); display.write(247); }
  display.setCursor(10, 50);
  display.print(F("LONG PRESS "));
  display.display();
}

void handleBootConfig() {
  bootSetting = 0;
  display.clearDisplay();
  display.setCursor(20, 20);
  display.print(F("BOOT CONFIG"));
  display.display();
  delay(800);
  
  while (digitalRead(ENCODER_SW_PIN) == LOW) delay(10);
  
  drawBootMenu();
  
  while (true) {
    if (digitalRead(ENCODER_PIN1) != digitalRead(ENCODER_PIN2)) {
      delay(5);
      bool dir = digitalRead(ENCODER_PIN1);
      if (isEncoderReversed) dir = !dir;
      if (bootSetting == 0) isEncoderReversed = !isEncoderReversed;
      else { displayRotation = (displayRotation == 0) ? 2 : 0; display.setRotation(displayRotation); }
      drawBootMenu();
      while (digitalRead(ENCODER_PIN1) != digitalRead(ENCODER_PIN2)) delay(1);
      delay(10);
    }
    
    if (digitalRead(ENCODER_SW_PIN) == LOW) {
      unsigned long pressTime = millis();
      while (digitalRead(ENCODER_SW_PIN) == LOW && millis() - pressTime < 500) delay(10);
      if (millis() - pressTime >= 500) {
        saveConfigSettings();
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(35, 24);
        display.print(F("SAVED"));
        display.display();
        delay(800);
        display.setTextSize(1);
        break;
      } else {
        bootSetting = 1 - bootSetting;
        drawBootMenu();
        while (digitalRead(ENCODER_SW_PIN) == LOW) delay(10);
      }
    }
  }
}

void onEncoderClicked(EncoderButton &eb);
void onEncoderLongClicked(EncoderButton &eb);
void onEncoderRotation(EncoderButton &eb);
void onEncoderPressedRotation(EncoderButton &eb);

void setupEncoder() {
  encoder.setDebounceInterval(2);
  encoder.setMultiClickInterval(20);
  encoder.setRateLimit(100);
  encoder.setIdleTimeout(5000);
  encoder.setIdleHandler(onOverlayTimeout);
  encoder.setLongClickDuration(375);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);
}

void initIO() {
  SET_INPUT(RESET_DDR, RESET_BIT);
  SET_INPUT(CLK_DDR, CLK_BIT);
  
  SET_OUTPUT(OUTPUT1_DDR, OUTPUT1_BIT);
  WRITE_LOW(OUTPUT1_PORT, OUTPUT1_BIT);
  SET_OUTPUT(OUTPUT2_DDR, OUTPUT2_BIT);
  WRITE_LOW(OUTPUT2_PORT, OUTPUT2_BIT);
  SET_OUTPUT(OUTPUT3_DDR, OUTPUT3_BIT);
  WRITE_LOW(OUTPUT3_PORT, OUTPUT3_BIT);
  SET_OUTPUT(OUTPUT4_DDR, OUTPUT4_BIT);
  WRITE_LOW(OUTPUT4_PORT, OUTPUT4_BIT);
  SET_OUTPUT(OUTPUT5_DDR, OUTPUT5_BIT);
  WRITE_LOW(OUTPUT5_PORT, OUTPUT5_BIT);
  SET_OUTPUT(OUTPUT6_DDR, OUTPUT6_BIT);
  WRITE_LOW(OUTPUT6_PORT, OUTPUT6_BIT);
  
  SET_OUTPUT(LED1_DDR, LED1_BIT);
  WRITE_LOW(LED1_PORT, LED1_BIT);
  SET_OUTPUT(LED2_DDR, LED2_BIT);
  WRITE_LOW(LED2_PORT, LED2_BIT);
  SET_OUTPUT(LED3_DDR, LED3_BIT);
  WRITE_LOW(LED3_PORT, LED3_BIT);
  SET_OUTPUT(LED4_DDR, LED4_BIT);
  WRITE_LOW(LED4_PORT, LED4_BIT);
  SET_OUTPUT(LED5_DDR, LED5_BIT);
  WRITE_LOW(LED5_PORT, LED5_BIT);
  SET_OUTPUT(LED6_DDR, LED6_BIT);
  WRITE_LOW(LED6_PORT, LED6_BIT);
  
  SET_OUTPUT(CLK_LED_DDR, CLK_LED_BIT);
  WRITE_LOW(CLK_LED_PORT, CLK_LED_BIT);
}

void initDisplay() {
  delay(1000);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  Wire.setClock(400000L);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setRotation(displayRotation);
}

void outputGateSignals() {
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (offset_buf[i][playing_step[i]] == 1 && currentConfig.mute[i] == 0 && random(100) < currentConfig.probability[i]) {
      switch (i) {
        case 0: WRITE_HIGH(OUTPUT1_PORT, OUTPUT1_BIT); WRITE_HIGH(LED1_PORT, LED1_BIT); break;
        case 1: WRITE_HIGH(OUTPUT2_PORT, OUTPUT2_BIT); WRITE_HIGH(LED2_PORT, LED2_BIT); break;
        case 2: WRITE_HIGH(OUTPUT3_PORT, OUTPUT3_BIT); WRITE_HIGH(LED3_PORT, LED3_BIT); break;
        case 3: WRITE_HIGH(OUTPUT4_PORT, OUTPUT4_BIT); WRITE_HIGH(LED4_PORT, LED4_BIT); break;
        case 4: WRITE_HIGH(OUTPUT5_PORT, OUTPUT5_BIT); WRITE_HIGH(LED5_PORT, LED5_BIT); break;
        case 5: WRITE_HIGH(OUTPUT6_PORT, OUTPUT6_BIT); WRITE_HIGH(LED6_PORT, LED6_BIT); break;
      }
    }
  }
}

bool isClockRunning() { return internalClock || (millis() - last_clock_input < period); }

inline int mainGetIncrement() {
  int inc = encoder.increment();
  if (isEncoderReversed) inc = -inc;
  return inc;
}

void onEncoderClicked(EncoderButton &eb) {
  if (!isClockRunning()) force_refresh = true;
  disp_refresh = true;
  if (showOverlay) {
    onOverlayTimeout(eb);
  } else {
    if (selected_menu <= MENU_CH_6) {
      selected_setting = static_cast<Setting>((selected_setting + 1) % SETTING_LAST);
    } else if (selected_menu == MENU_ALL_RESET) {
      resetTriggerFlag = false;
      resetSeq();
    } else if (selected_menu == MENU_ALL_MUTE) {
      toggleAllMutes();
    } else if (selected_menu == MENU_RAND) {
      Random_change(false, true);
    } else if (selected_menu == MENU_RANDOM_ADVANCE) {
      Random_change(false, false);
    } else {
      showOverlay = !showOverlay;
    }
  }
}

void onEncoderLongClicked(EncoderButton &eb) {
  if (!isClockRunning()) force_refresh = true;
  disp_refresh = true;
  if (selected_menu <= MENU_CH_6 && selected_setting != SETTING_TOP_MENU) {
    selected_setting = static_cast<Setting>((selected_setting - 1 + SETTING_LAST) % SETTING_LAST);
  } else if (showOverlay) {
    if (selected_menu == MENU_PRESET) {
      loadDefaultConfig(&currentConfig, selected_preset);
      currentConfig.lastLoadedFromPreset = true;
      tempo = currentConfig.tempo;
      period = 60000 / tempo / 4;
      showOverlay = false;
    } else if (selected_menu == MENU_SAVE) {
      saveToEEPROM(selected_slot);
      showOverlay = false;
    } else if (selected_menu == MENU_LOAD) {
      loadFromEEPROM(selected_slot);
      currentConfig.lastLoadedFromPreset = false;
      showOverlay = false;
    }
  } else if (selected_menu == MENU_TEMPO) {
    internalClock = !internalClock;
    showOverlay = true;
    if (internalClock) period = 60000 / tempo / 4;
  } else if (selected_setting == SETTING_TOP_MENU && selected_menu <= MENU_CH_6) {
    int ch = selected_menu - MENU_CH_1;
    currentConfig.mute[ch] = !currentConfig.mute[ch];
  }
}

void onEncoderRotation(EncoderButton &eb) {
  int increment = mainGetIncrement();
  if (increment == 0) return;
  disp_refresh = true;
  int accel = increment * increment;
  if (increment < 0) accel = -accel;
  if (!allMutedFlag && !showOverlay) {
    if (selected_menu <= MENU_CH_6) handleSettingNavigation(accel);
    else selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
    return;
  }
  if (selected_setting == SETTING_TOP_MENU && showOverlay) {
    if (selected_menu == MENU_PRESET) {
      selected_preset = (selected_preset + increment + NUM_PRESETS) % NUM_PRESETS;
    } else if (selected_menu == MENU_TEMPO) {
      tempo += accel;
      tempo = constrain(tempo, 40, 250);
      period = 60000 / (tempo * 4);
    } else if (selected_menu == MENU_SAVE || selected_menu == MENU_LOAD) {
      selected_slot = (selected_slot + increment + NUM_MEMORY_SLOTS) % NUM_MEMORY_SLOTS;
    }
  }
}

void onEncoderPressedRotation(EncoderButton &eb) {
  int increment = mainGetIncrement();
  if (increment == 0) return;
  disp_refresh = true;
  int accel = increment * increment;
  if (increment < 0) accel = -accel;
  if (selected_menu == MENU_ALL_MUTE) {
    static int current_channel = 0;
    current_channel = (current_channel + increment + MAX_CHANNELS) % MAX_CHANNELS;
    if (increment > 0) currentConfig.mute[current_channel] = true;
    else currentConfig.mute[MAX_CHANNELS - 1 - current_channel] = false;
    bool all_muted = true, all_unmuted = true;
    for (int i = 0; i < MAX_CHANNELS; i++) {
      if (!currentConfig.mute[i]) all_muted = false;
      if (currentConfig.mute[i]) all_unmuted = false;
    }
    if (all_muted) { current_channel = 0; for (int i = 0; i < MAX_CHANNELS; i++) currentConfig.mute[i] = false; }
    if (all_unmuted) { current_channel = 0; for (int i = 0; i < MAX_CHANNELS; i++) currentConfig.mute[i] = true; }
    return;
  }
  if (selected_menu <= MENU_CH_6) {
    int ch = selected_menu - MENU_CH_1;
    if (selected_setting == SETTING_TOP_MENU) {
      currentConfig.hits[ch] = (currentConfig.hits[ch] + increment + 17) % 17;
    } else {
      selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
      if (selected_menu > MENU_CH_6) selected_menu = MENU_CH_1;
    }
  } else if (selected_menu == MENU_RAND) {
    Random_change(increment > 0 ? false : true, true);
  } else if (selected_menu == MENU_RANDOM_ADVANCE) {
    bar_select += increment;
    bar_select = constrain(bar_select, 1, 6);
  } else if (selected_setting != SETTING_TOP_MENU) {
    selected_menu = static_cast<TopMenu>((selected_menu + increment + MENU_LAST) % MENU_LAST);
    if (selected_menu > MENU_CH_6) selected_menu = MENU_CH_1;
  }
}

void initializeCurrentConfig(bool loadDefaults = false) {
  if (loadDefaults) {
    memcpy_P(&currentConfig, &defaultSlots[0], sizeof(SlotConfiguration));
  } else {
    EEPROM.get(EEPROM_START_ADDRESS, currentConfig);
    tempo = currentConfig.tempo;
    internalClock = currentConfig.internalClock;
    lastUsedSlot = currentConfig.lastUsedSlot;
    selected_preset = currentConfig.selectedPreset;
    if (currentConfig.lastLoadedFromPreset) loadFromPreset(selected_preset);
    else loadFromEEPROM(lastUsedSlot);
  }
}

void handleSettingNavigation(int changeDirection) {
  int ch = selected_menu - MENU_CH_1;
  switch (selected_setting) {
    case SETTING_TOP_MENU: selected_menu = static_cast<TopMenu>((selected_menu + changeDirection + MENU_LAST) % MENU_LAST); break;
    case SETTING_HITS: currentConfig.hits[ch] = (currentConfig.hits[ch] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS; break;
    case SETTING_OFFSET: currentConfig.offset[ch] = (currentConfig.offset[ch] - changeDirection + MAX_STEPS) % MAX_STEPS; break;
    case SETTING_LIMIT: currentConfig.limit[ch] = (currentConfig.limit[ch] + changeDirection + MAX_PATTERNS) % MAX_PATTERNS; break;
    case SETTING_MUTE: currentConfig.mute[ch] = !currentConfig.mute[ch]; break;
    case SETTING_RESET: playing_step[ch] = 15; break;
    case SETTING_RANDOM: Random_change(false, false, ch); break;
    case SETTING_PROB: currentConfig.probability[ch] = (currentConfig.probability[ch] + changeDirection + 101) % 101; break;
  }
}

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
    currentConfig.lastLoadedFromPreset = false;
    EEPROM.put(baseAddress, currentConfig);
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
    period = 60000 / tempo / 4;
    updateRythm();
  }
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
    memcpy_P(&config, &defaultSlots[i], sizeof(SlotConfiguration));
    EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, config);
  }
}

void toggleAllMutes() {
  bool allMuted = true;
  for (int i = 0; i < MAX_CHANNELS; i++) { if (currentConfig.mute[i] == 0) { allMuted = false; break; } }
  for (int i = 0; i < MAX_CHANNELS; i++) currentConfig.mute[i] = !allMuted;
  allMutedFlag = !allMuted;
}

void resetSeq() {
  for (int k = 0; k < MAX_CHANNELS; k++) {
    uint8_t lim = currentConfig.limit[k] ? currentConfig.limit[k] : 16;
    playing_step[k] = (lim - 1);
  }
  step_cnt = 0;
  bar_now = 1;
}

// ====== OPTIMIZED: Compact menu rendering (saves ~120 bytes) ======
void drawSideMenus() {
  if (showOverlay) return;
  
  // Right menu
  int x = 120;
  char chars[4] = {' ',' ',' ',' '};
  if (selected_menu <= MENU_CH_6) {
    chars[0] = '1' + (selected_menu - MENU_CH_1);
    chars[1] = 'H'; chars[2] = 'O';
  } else {
    switch (selected_menu) {
      case MENU_RANDOM_ADVANCE: chars[0]='R'; chars[1]='N'; chars[2]='D'; break;
      case MENU_SAVE: chars[0]='S'; break;
      case MENU_LOAD: chars[0]='L'; break;
      case MENU_PRESET: chars[0]='P'; break;
      case MENU_ALL_RESET: chars[0]='R'; break;
      case MENU_ALL_MUTE: chars[0]='M'; break;
      case MENU_RAND: chars[0]='X'; break;
      case MENU_TEMPO: 
        if (internalClock) { chars[0]='I'; chars[1]='N'; chars[2]='T'; }
        else { chars[0]='E'; chars[1]='X'; chars[2]='T'; }
        break;
    }
  }
  for (int i = 0; i < 4; i++) {
    display.setCursor(x, i * 9);
    display.print(chars[i]);
  }
  
  // Left menu
  x = 0;
  chars[0] = chars[1] = chars[2] = chars[3] = ' ';
  if (selected_menu <= MENU_CH_6) {
    switch (selected_setting) {
      case SETTING_HITS: chars[0]='H'; chars[1]='I'; chars[2]='T'; chars[3]='S'; break;
      case SETTING_OFFSET: chars[0]='O'; chars[1]='F'; chars[2]='F'; chars[3]='S'; break;
      case SETTING_LIMIT: chars[0]='L'; chars[1]='I'; chars[2]='M'; chars[3]='I'; break;
      case SETTING_MUTE: chars[0]='M'; chars[1]='U'; chars[2]='T'; chars[3]='E'; break;
      case SETTING_RESET: chars[0]='R'; chars[1]='S'; chars[2]='E'; chars[3]='T'; break;
      case SETTING_RANDOM: chars[0]='R'; chars[1]='A'; chars[2]='N'; chars[3]='D'; break;
      case SETTING_PROB: chars[0]='P'; chars[1]='R'; chars[2]='O'; chars[3]='B'; break;
    }
  } else {
    switch (selected_menu) {
      case MENU_SAVE: chars[0]='S'; chars[1]='A'; chars[2]='V'; chars[3]='E'; break;
      case MENU_LOAD: chars[0]='L'; chars[1]='O'; chars[2]='A'; chars[3]='D'; break;
      case MENU_ALL_RESET: chars[0]='R'; chars[1]='S'; chars[2]='E'; chars[3]='T'; break;
      case MENU_ALL_MUTE: chars[0]='M'; chars[1]='U'; chars[2]='T'; chars[3]='E'; break;
      case MENU_PRESET: chars[0]='P'; chars[1]='R'; chars[2]='S'; chars[3]='T'; break;
      case MENU_TEMPO: chars[0]='*'; chars[1]='C'; chars[2]='L'; chars[3]='K'; break;
      case MENU_RAND: chars[0]='R'; chars[1]='A'; chars[2]='N'; chars[3]='D'; break;
    }
  }
  for (int i = 0; i < 4; i++) {
    display.setCursor(x, 30 + i * 9);
    display.print(chars[i]);
  }
}

void checkAndInitializeSettings() {
  char magic[sizeof(FIRMWARE_MAGIC)];
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);
  if (strncmp(magic, FIRMWARE_MAGIC, sizeof(FIRMWARE_MAGIC)) != 0) {
    initializeDefaultRhythms();
    EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
    initializeCurrentConfig(true);
  } else {
    initializeCurrentConfig(false);
  }
}

// ====== OPTIMIZED: Simplified display (saves ~300 bytes) ======
void OLED_display() {
  static uint32_t last_refresh_local = 0;
  uint32_t now = millis();
  if (!force_refresh && (!disp_refresh || (now - last_refresh_local < MIN_REFRESH_DURATION))) return;
  disp_refresh = false;
  force_refresh = false;
  last_refresh_local = now;
  display.clearDisplay();

  if (allMutedFlag) {
    display.setTextSize(2);
    display.setCursor(35, 24);
    display.print(F("MUTE"));
    display.drawRect(27, 16, 74, 32, WHITE);
    display.setTextSize(1);
  } else {
    drawSideMenus();
    
    // Draw selection indicators
    if (!showOverlay) {
      if (selected_setting == SETTING_TOP_MENU) display.fillTriangle(113, 0, 113, 6, 118, 3, WHITE);
      else if (selected_setting == SETTING_HITS) display.fillTriangle(113, 9, 113, 15, 118, 12, WHITE);
      else if (selected_setting == SETTING_OFFSET) display.fillTriangle(113, 18, 113, 24, 118, 21, WHITE);
      else if (selected_setting >= SETTING_LIMIT && selected_setting <= SETTING_PROB) {
        int y = 34 + (selected_setting - SETTING_LIMIT) * 8;
        display.fillTriangle(12, y, 12, y + 6, 7, y + 3, WHITE);
      }
    }
    
    // Draw random mode indicator
    if (selected_menu == MENU_RANDOM_ADVANCE && !showOverlay) {
      int bm = pgm_read_word(&bar_max[bar_select]);
      display.drawRect(1, 62 - bm * 2, 6, bm * 2 + 2, WHITE);
      display.fillRect(1, 64 - bar_now * 2, 6, bar_now * 2, WHITE);
    }

    if (selected_setting == SETTING_PROB && !showOverlay) {
      // Simplified probability bars
      for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        int x = graph_x[ch] + 12;
        int y = graph_y[ch] + 30;
        int h = map(currentConfig.probability[ch], 0, 100, 0, 15);
        display.drawRect(x - 1, y - 16, 6, 17, WHITE);
        display.fillRect(x, y - h, 4, h, WHITE);
      }
    } else if (!showOverlay) {
      // Draw Euclidean patterns
      for (int k = 0; k < MAX_CHANNELS; k++) {
        if (currentConfig.mute[k]) {
          display.setCursor(graph_x[k] + 12, graph_y[k] + 11);
          display.print('M');
          continue;
        }
        
        // Draw step dots
        for (int j = 0; j < currentConfig.limit[k]; j++) {
          int x_pos = x16[j] + graph_x[k];
          int y_pos = y16[j] + graph_y[k];
          display.drawPixel(x_pos, y_pos, WHITE);
        }
        
        // Draw hit lines
        byte buf_count = 0;
        for (int m = 0; m < MAX_STEPS; m++) {
          if (offset_buf[k][m] == 1) {
            line_xbuf[buf_count] = x16[m] + graph_x[k];
            line_ybuf[buf_count] = y16[m] + graph_y[k];
            buf_count++;
          }
        }
        for (int j = 0; j < buf_count - 1; j++) {
          display.drawLine(line_xbuf[j], line_ybuf[j], line_xbuf[j + 1], line_ybuf[j + 1], WHITE);
        }
        if (buf_count > 0) display.drawLine(line_xbuf[0], line_ybuf[0], line_xbuf[buf_count - 1], line_ybuf[buf_count - 1], WHITE);
        
        // Current position
        int px = x16[playing_step[k]] + graph_x[k];
        int py = y16[playing_step[k]] + graph_y[k];
        if (offset_buf[k][playing_step[k]]) display.fillCircle(px, py, 2, WHITE);
        else display.drawCircle(px, py, 2, WHITE);
      }
    }
  }

  // Simplified overlays (saves ~200 bytes)
  if (showOverlay && selected_setting == SETTING_TOP_MENU) {
    display.clearDisplay();
    if (selected_menu == MENU_PRESET) {
      char name[10];
      memcpy_P(&name, &defaultSlots[selected_preset].name, sizeof(name));
      display.setCursor(30, 8);
      display.print(F("Select Preset"));
      display.setCursor(30, 28);
      display.setTextSize(2);
      display.print(name);
      display.setTextSize(1);
      display.setCursor(20, 52);
      display.print(F("LONG PRESS"));
    } else if (selected_menu == MENU_SAVE || selected_menu == MENU_LOAD) {
      display.setCursor(30, 8);
      display.print(selected_menu == MENU_SAVE ? F("Save") : F("Load"));
      display.setCursor(58, 28);
      display.setTextSize(2);
      display.print(selected_slot + 1);
      display.setTextSize(1);
      display.setCursor(30, 52);
      display.print(F("LONG PRESS"));
    } else if (selected_menu == MENU_TEMPO) {
      if (internalClock) {
        display.setCursor(30, 8);
        display.print(F("Tempo"));
        display.setCursor(50, 28);
        display.setTextSize(2);
        display.print(tempo);
        display.setTextSize(1);
      } else {
        display.setCursor(30, 20);
        if (externalBPM == 0) display.print(F("Ext CLK"));
        else { display.print(F("BPM:")); display.print(externalBPM); }
      }
    }
  }
  
  display.display();
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

void setup() {
  pinMode(ENCODER_PIN1, INPUT_PULLUP);
  pinMode(ENCODER_PIN2, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  loadConfigSettings();
  initDisplay();
  setupEncoder();
  if (digitalRead(ENCODER_SW_PIN) == LOW) {
    delay(100);
    if (digitalRead(ENCODER_SW_PIN) == LOW) {
      handleBootConfig();
      display.setRotation(displayRotation);
      setupEncoder();
    }
  }
  initIO();
  setupPinChangeInterrupts();
  checkAndInitializeSettings();
  EEPROM.get(LAST_USED_SLOT_ADDRESS, lastUsedSlot);
  if (lastUsedSlot >= NUM_MEMORY_SLOTS) lastUsedSlot = 0;
  loadFromEEPROM(lastUsedSlot);
  if (currentConfig.lastLoadedFromPreset) loadFromPreset(currentConfig.selectedPreset);
  updateRythm();
  OLED_display();
  randomSeed(analogRead(A0));
  last_clock_input = millis();
  internalClockTimer = millis();
}

void loop() {
  encoder.update();
  updateRythm();
  bool beat_start = false;
  if (resetTriggerFlag) { resetTriggerFlag = false; resetSeq(); disp_refresh = true; }
  if (!internalClock && clockTriggerFlag) { clockTriggerFlag = false; beat_start = true; }
  if (internalClock && (millis() - internalClockTimer >= period)) {
    beat_start = true;
    internalClockTimer = millis();
    last_clock_input = millis();
  }
  if (beat_start) {
    gate_timer = millis();
    force_refresh = true;
    WRITE_HIGH(CLK_LED_PORT, CLK_LED_BIT);
    for (int i = 0; i < MAX_CHANNELS; i++) {
      playing_step[i]++;
      if (playing_step[i] >= currentConfig.limit[i]) playing_step[i] = 0;
    }
    outputGateSignals();
    disp_refresh = true;
    if (selected_menu == MENU_RANDOM_ADVANCE) {
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
  if (gate_timer + 10 <= millis()) {
    WRITE_LOW(OUTPUT1_PORT, OUTPUT1_BIT);
    WRITE_LOW(OUTPUT2_PORT, OUTPUT2_BIT);
    WRITE_LOW(OUTPUT3_PORT, OUTPUT3_BIT);
    WRITE_LOW(OUTPUT4_PORT, OUTPUT4_BIT);
    WRITE_LOW(OUTPUT5_PORT, OUTPUT5_BIT);
    WRITE_LOW(OUTPUT6_PORT, OUTPUT6_BIT);
    WRITE_LOW(CLK_LED_PORT, CLK_LED_BIT);
  }
  if (gate_timer + 30 <= millis()) {
    WRITE_LOW(LED1_PORT, LED1_BIT);
    WRITE_LOW(LED2_PORT, LED2_BIT);
    WRITE_LOW(LED3_PORT, LED3_BIT);
    WRITE_LOW(LED4_PORT, LED4_BIT);
    WRITE_LOW(LED5_PORT, LED5_BIT);
    WRITE_LOW(LED6_PORT, LED6_BIT);
  }
  OLED_display();
}