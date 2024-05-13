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

// Flag for reversing the encoder direction.
// ToDo: Put this in config Menue dialog at boot ?
#define ENCODER_REVERSED

// Flag for using the panel upside down
// ToDo: change to be in line with libModulove, put in config Menue dialog
//#define ROTATE_PANEL

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

//#define DEBUG  // Uncomment for enabling debug print to serial monitoring output. Note: this affects performance and locks LED 4 & 5 on HIGH.
int debug = 0;  // ToDo: rework the debug feature (enable in menue?)

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EncoderButton.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
//#include <ArduinoTapTempo.h>


// For debug / UI
#define FIRMWARE_MAGIC "EUCLID"
#define FIRMWARE_MAGIC_ADDRESS 0  // Store the firmware magic number at address 0

// OLED
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// EEPROM
#define NUM_MEMORY_SLOTS 2  // Why does it freeze when loading slot 3?
#define EEPROM_START_ADDRESS 7
#define CONFIG_SIZE (sizeof(SlotConfiguration))

// Pins
const int ENCODER_PIN1 = 2, ENCODER_PIN2 = 3, ENCODER_SW_PIN = 12;
const int rstPin = 11, clkPin = 13;

// Timing
unsigned long startMillis, currentMillis, lastTriggerTime, internalClockPeriod;
bool trg_in = false, old_trg_in = false, rst_in = false, old_rst_in = false;
byte playing_step[6] = { 0, 0, 0, 0, 0, 0 };

// display Menu and UI
byte select_menu = 0;   //0=CH,1=HIT,2=OFFSET,3=LIMIT,4=MUTE,5=RESET,6=RANDOM MOD
byte select_ch = 0;     //0~5 = each channel -1 , 6 = random mode
bool disp_refresh = 0;  //0=not refresh display , 1= refresh display , countermeasure of display refresh bussy

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 };  //each chanel display offset
//const byte graph_y[6] = { 1, 1, 1, 33, 33, 33 };    //each chanel display offset -> +1 vs. original
const byte graph_y[6] = { 0, 0, 0, 32, 32, 32 };  //each chanel display offset -> +1 vs. original

byte line_xbuf[17];
byte line_ybuf[17];

const byte x16[16] = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 };
const byte y16[16] = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//Sequence variables
byte j = 0, k = 0, m = 0, buf_count = 0;
unsigned long gate_timer = 0;

const static byte euc16[17][16] PROGMEM = {  //euclidian rythm
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
bool offset_buf[6][16];  //offset buffer , Stores the offset result

// random assign
byte hit_occ[6] = { 5, 1, 20, 20, 40, 80 };   //random change rate of occurrence
byte off_occ[6] = { 1, 3, 20, 30, 40, 20 };   //random change rate of occurrence
byte mute_occ[6] = { 0, 2, 20, 20, 20, 20 };  //random change rate of occurrence
byte hit_rng_max[6] = { 6, 5, 8, 4, 4, 6 };   //random change range of max
byte hit_rng_min[6] = { 3, 2, 2, 1, 1, 1 };   //random change range of min

byte bar_now = 1;
const int bar_max[6] = { 2, 4, 6, 8, 12, 16 };  // control Random advance mode 6
byte bar_select = 1;                            // ToDo: selected bar needs to be saved as well!
byte step_cnt = 0;

// Display Setup
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// reverse encoder
#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#endif

int i = 0;  // ch 1- 6

// configuration for a channel
struct SlotConfiguration {
  byte hits[6], offset[6];
  bool mute[6];
  byte limit[6];
  byte probability[6];  // New: probability
};

// default config for presets
const SlotConfiguration defaultSlots[3] PROGMEM = {
  { { 4, 3, 4, 2, 4, 3 }, { 0, 1, 2, 1, 0, 2 }, { false, false, false, false, false, false }, { 13, 12, 8, 14, 12, 9 }, { 100, 100, 100, 100, 100, 100 } },    // Techno
  { { 4, 3, 5, 3, 2, 4 }, { 0, 1, 2, 3, 0, 2 }, { false, false, false, false, false, false }, { 15, 15, 15, 10, 12, 14 }, { 100, 100, 100, 100, 100, 100 } },  // House
  { { 2, 3, 2, 3, 4, 2 }, { 0, 1, 0, 2, 1, 0 }, { false, false, false, false, false, false }, { 24, 18, 24, 21, 16, 30 }, { 100, 100, 100, 100, 100, 100 } }   // Ambient (Minimal beats)
};

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS];  // Memory slots for configurations

SlotConfiguration currentConfig;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif

  encoder.setDebounceInterval(3);
  encoder.setMultiClickInterval(10);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setRateLimit(5);

  initIO();
  initDisplay();

  checkAndInitializeSettings();
  initializeCurrentConfig();

  OLED_display();
  lastTriggerTime = millis();
}

void loop() {

  encoder.update();  // Process Encoder & button updates


  //-----------------offset setting----------------------
  for (k = 0; k <= 5; k++) {  //k = 1~6ch
    for (i = currentConfig.offset[k]; i <= 15; i++) {
      offset_buf[k][i - currentConfig.offset[k]] = (pgm_read_byte(&(euc16[currentConfig.hits[k]][i])));
    }

    for (i = 0; i < currentConfig.offset[k]; i++) {
      offset_buf[k][16 - currentConfig.offset[k] + i] = (pgm_read_byte(&(euc16[currentConfig.hits[k]][i])));
    }
  }

  //-----------------trigger detect, reset & output----------------------
  bool rst_in = RESET::isInputHigh();  // External reset
  bool trg_in = CLK::isInputHigh();

  if (old_rst_in == 0 && rst_in == 1) {
    for (int k = 0; k <= 5; k++) {
      playing_step[k] = 0;
    }
    disp_refresh = 1;
  }

  // Trigger detection and response
  if (old_trg_in == 0 && trg_in == 1) {
    gate_timer = millis();
    FastGPIO::Pin<4>::setOutput(1);
    debug = 0;
    for (int i = 0; i <= 5; i++) {
      playing_step[i]++;
      if (playing_step[i] >= currentConfig.limit[i]) {
        playing_step[i] = 0;  // Step limit is reached
      }
    }

    // Output gate signal, there must be a better way to do this..
    if (offset_buf[0][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[0]) {
      OUTPUT1::setOutput(1);
      LED1::setOutput(1);
    }
    if (offset_buf[1][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[1]) {
      OUTPUT2::setOutput(1);
      LED2::setOutput(1);
    }
    if (offset_buf[2][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[2]) {
      OUTPUT3::setOutput(1);
      LED3::setOutput(1);
    }
    if (offset_buf[3][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[3]) {
      OUTPUT4::setOutput(1);
      LED4::setOutput(1);
    }
    if (offset_buf[4][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[4]) {
      OUTPUT5::setOutput(1);
      LED5::setOutput(1);
    }
    if (offset_buf[5][playing_step[0]] == 1 && currentConfig.mute[0] == 0 && random(100) < currentConfig.probability[5]) {
      OUTPUT6::setOutput(1);
      LED6::setOutput(1);
    }

    disp_refresh = 1;  // Updates the display where the trigger was entered.

    // Random advance mode (mode 6)
    if (select_ch == 6) {  // random mode setting
      step_cnt++;
      if (step_cnt >= 16) {
        bar_now++;
        step_cnt = 0;
        if (bar_now > bar_max[bar_select]) {
          bar_now = 1;
          Random_change();
        }
      }
    }
  }

  if (gate_timer + 10 <= millis()) {  //off all gate , gate time is 10msec

    FastGPIO::Pin<5>::setOutput(0);
    FastGPIO::Pin<6>::setOutput(0);
    FastGPIO::Pin<7>::setOutput(0);
    FastGPIO::Pin<8>::setOutput(0);
    FastGPIO::Pin<9>::setOutput(0);
    FastGPIO::Pin<10>::setOutput(0);
  }
  if (gate_timer + 30 <= millis()) {  //off all gate , gate time is 10msec, reduced from 100 ms to 30 ms
    FastGPIO::Pin<4>::setOutput(0);   // CLK LED
    FastGPIO::Pin<14>::setOutput(0);
    FastGPIO::Pin<15>::setOutput(0);
    FastGPIO::Pin<16>::setOutput(0);
    FastGPIO::Pin<17>::setOutput(0);
    FastGPIO::Pin<0>::setOutput(0);
    FastGPIO::Pin<1>::setOutput(0);
  }

  if (old_trg_in == 0 && trg_in == 0 && gate_timer + 3000 <= millis()) {
    //useInternalClock = true;
    //debug = 1;
    disp_refresh = 1;
  }

  if (disp_refresh) {
    OLED_display();  // refresh display
    disp_refresh = 0;
  }

  old_trg_in = trg_in;
  old_rst_in = rst_in;
}

void initIO() {
  FastGPIO::Pin<4>::setOutputLow();  // CLK LED
  //FastGPIO::Pin<12>::setInputPulledUp();  // BUTTON
  //FastGPIO::Pin<3>::setInputPulledUp();   // ENCODER A
  //FastGPIO::Pin<2>::setInputPulledUp();   // ENCODER B
  RESET::setInput();        // RST
  CLK::setInput();          // CLK
  OUTPUT1::setOutputLow();  // CH1
  OUTPUT2::setOutputLow();  // CH2
  OUTPUT3::setOutputLow();  // CH3
  OUTPUT4::setOutputLow();  // CH4
  OUTPUT5::setOutputLow();  // CH5
  OUTPUT6::setOutputLow();  // CH6
  // LED outputs
  LED1::setOutputLow();  // CH1 LED
  LED2::setOutputLow();  // CH2 LED
  LED3::setOutputLow();  // CH3 LED
  LED4::setOutputLow();  // CH6 LED
  LED5::setOutputLow();  // CH4 LED
  LED6::setOutputLow();  // CH5 LED
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

void onEncoderClicked(EncoderButton &eb) {

  if (encoder.buttonState()) {  // button pressed without debounce (handled by library)
    disp_refresh = debug;
    select_menu++;
  }

  if (select_menu > 7) select_menu = 0;  // Wraps around the channel individual settings menus

  if (select_ch > 5 && select_menu > 1) select_menu = 0;  // Wrap around the other menu items
  // Mode-specific actions
  if (select_ch == 7 && select_menu == 1) {
    saveConfiguration();
    select_menu = 0;
  }
  if (select_ch == 8 && select_menu == 1) {
    loadConfiguration();
    select_menu = 0;
  }
  if (select_ch == 9 && select_menu == 1) {
    resetSeq();
    select_menu = 0;
  }
  if (select_ch == 10 && select_menu == 1) {
    toggleAllMutes();
    select_menu = 0;
  }
  if (select_ch == 11 && select_menu == 1) {  // modes only having a button

    // Dial in tempo with the encoder and / or TapTempo via encoder button
    //adjustTempo();
    select_menu = 0;
  }
  if (select_ch == 12 && select_menu == 1) {  //
    // This needs to work as before where you advance through the random array by rotating the encoder.
    // should make it possible to go back and forth like 5 steps and have a set of steady values
    Random_change();
    //disp_refresh = 1;
  }
}

void onEncoderRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment != 0) {
    if (increment < 0) {
      acceleratedIncrement = -acceleratedIncrement;  // Ensure that the direction of increment is preserved
    }
    handleMenuNavigation(acceleratedIncrement);
  }
}

void initializeCurrentConfig() {
  // Copy the configuration from PROGMEM to RAM
  memcpy_P(&currentConfig, &defaultSlots[1], sizeof(SlotConfiguration));
}

void loadConfigurationFromPreset(int presetIndex) {
  if (presetIndex >= 0 && presetIndex < 3) {  // Assuming there are 3 presets
    memcpy_P(&currentConfig, &defaultSlots[presetIndex], sizeof(SlotConfiguration));
  }
}

void handleMenuNavigation(int changeDirection) {
  if (changeDirection != 0) {

    switch (select_menu) {
      case 0:                                                 // Select channel
        select_ch = (select_ch + changeDirection + 13) % 13;  // Wrap-around for channel selection
        break;
      case 1:                                                                                           // Hits
        if (select_ch != 6) {                                                                           // Handling channels 0 to 5
          currentConfig.hits[select_ch] = (currentConfig.hits[select_ch] + changeDirection + 17) % 17;  // Ensure hits wrap properly
        } else {                                                                                        // Handling Random Mode (select_ch == 6)
          // Increment or decrement `bar_select` based on encoder direction
          bar_select += changeDirection;
          // Ensure `bar_select` stays within the range of 1 to 5
          if (bar_select < 1) bar_select = 6;
          if (bar_select > 6) bar_select = 1;
        }
        break;
      case 2:
        currentConfig.offset[select_ch] = (currentConfig.offset[select_ch] + changeDirection + 16) % 16;  // Wrap-around for offset
        break;
      case 3:                                                                                           // Limit
        currentConfig.limit[select_ch] = (currentConfig.limit[select_ch] + changeDirection + 17) % 17;  // Wrap-around for limit
        break;
      case 4:                                                            // Mute
        currentConfig.mute[select_ch] = !currentConfig.mute[select_ch];  // Toggle mute state
        break;
      case 5:  // Reset channel step
        playing_step[select_ch] = 0;
        break;
      case 6:  // Randomize channel
        Random_change_one(select_ch);
        break;
      case 7:  // Set probability
        currentConfig.probability[select_ch] = (currentConfig.probability[select_ch] + changeDirection + 101) % 101;
        break;
    }
  }
}


// Loading SlotConfiguration from PROGMEM
void loadDefaultConfig(SlotConfiguration *config, int index) {
  memcpy_P(config, &defaultSlots[index], sizeof(SlotConfiguration));
}

void saveToEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * sizeof(SlotConfiguration));
  if (baseAddress + sizeof(SlotConfiguration) <= EEPROM.length()) {
    EEPROM.put(baseAddress, currentConfig);  // Changed from memorySlots[slot] to currentConfig
  } else {
    // set error flag or display message ?
  }
}
void loadFromEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * sizeof(SlotConfiguration));
  if (baseAddress + sizeof(SlotConfiguration) <= EEPROM.length()) {
    EEPROM.get(baseAddress, currentConfig);  // Changed from memorySlots[slot] to currentConfig
  } else {
    // Handle the error
  }
}

void saveConfiguration() {
  int selectedSlot = 0;
  bool saving = true;

  while (saving) {
    // Display selected slot
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(24, 10);
    display.println(F("Save to Slot:"));
    display.setCursor(60, 29);
    display.setTextSize(2);
    display.print(selectedSlot + 1);
    display.setTextSize(1);
    display.display();


    // Check for encoder rotation to select memory slot
    int result = encoder.increment();
    if (result != 0) {
      if (result > 0) {  // CW
        selectedSlot++;
        if (selectedSlot >= NUM_MEMORY_SLOTS) {
          selectedSlot = 0;
        }
      } else if (result < 0) {  // CCW
        selectedSlot--;
        if (selectedSlot < 0) {
          selectedSlot = NUM_MEMORY_SLOTS - 1;
        }
      }
    }
    // Check for button press
    if (encoder.buttonState()) {  // button pressed
      saving = false;
      // Update all fields including the probability
      memorySlots[selectedSlot] = currentConfig;  // This copies all fields including probability
      saveToEEPROM(selectedSlot);
    }
    delay(300);  // slow down for debugging
  }
}

void loadConfiguration() {
  int selectedSlot = 0;
  bool loading = true;

  while (loading) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(24, 10);
    display.println(F("Load from Slot:"));
    display.setCursor(60, 29);
    display.setTextSize(2);
    display.print(selectedSlot + 1);
    display.setTextSize(1);
    display.display();

    // Check for encoder rotation to select memory slot
    int result = encoder.increment();
    if (result != 0) {
      if (result > 0) {  // CW
        selectedSlot++;
        if (selectedSlot >= NUM_MEMORY_SLOTS) {
          selectedSlot = 0;
        }
      } else if (result < 0) {  // CCW
        selectedSlot--;
        if (selectedSlot < 0) {
          selectedSlot = NUM_MEMORY_SLOTS - 1;
        }
      }
    }
    // Check for button press to load configuration
    if (encoder.buttonState()) {
      loadFromEEPROM(selectedSlot);

      // No need for copying fields manually if `loadFromEEPROM` sets `currentConfig` directly
      loading = false;
    }
    delay(300);  // slow down for debugging
  }
}

void saveCurrentConfigToEEPROM() {
  // could save to the last selected save slot. For now default slot1
  saveToEEPROM(1);
}

// reset the whole thing and put in (lots of?) modern / traditional euclid patterns as presets ?
void initializeDefaultRhythms() {
  for (int i = 0; i < NUM_MEMORY_SLOTS; i++) {
    SlotConfiguration config;
    // Load the default config into RAM from PROGMEM
    memcpy_P(&config, &defaultSlots[i], sizeof(SlotConfiguration));
    EEPROM.put(EEPROM_START_ADDRESS + i * sizeof(SlotConfiguration), config);
  }
}

void saveDefaultsToEEPROM(int slot, SlotConfiguration config) {
  int address = EEPROM_START_ADDRESS + (slot * sizeof(SlotConfiguration));
  EEPROM.put(address, config);
}

// Random change function for all channels
void Random_change() {
  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  for (k = 1; k <= 5; k++) {

    if (hit_occ[k] >= random(1, 100)) {
      currentConfig.hits[k] = random(hit_rng_min[k], hit_rng_max[k]);
    }

    if (off_occ[k] >= random(1, 100)) {
      currentConfig.offset[k] = random(0, 16);
    }

    if (mute_occ[k] >= random(1, 100)) {
      currentConfig.mute[k] = 1;
    } else if (mute_occ[k] < random(1, 100)) {
      currentConfig.mute[k] = 0;
    }
  }
}

// random change function for one channel (no mute))
void Random_change_one(byte select_ch) {

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  if (random(100) < hit_occ[select_ch]) {
    currentConfig.hits[select_ch] = random(hit_rng_min[select_ch], hit_rng_max[select_ch] + 1);
  }
  if (off_occ[select_ch] >= random(1, 100)) {
    currentConfig.offset[select_ch] = random(0, 16);
  }
}

void toggleAllMutes() {
  // Toggle mute for all channels
  bool allMuted = true;
  for (int i = 0; i < 6; i++) {
    if (currentConfig.mute[i] == 0) {
      allMuted = false;
      break;
    }
  }
  for (int i = 0; i < 6; i++) {
    currentConfig.mute[i] = !allMuted;
  }
}


void resetSeq() {
  for (k = 0; k <= 5; k++) {
    playing_step[k] = 0;
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

void setMenuCharacters(byte select_ch, char &c1, char &c2, char &c3, char &c4) {
  if (select_ch < 6) {         // Display numbers 1-6 (not random mode)
    c1 = select_ch + 1 + '0';  // Convert number to character
  } else {
    switch (select_ch) {
      case 6: c1 = 'R', c2 = 'N', c3 = 'D', c4 = ' '; break;   // RANDOM auto mode
      case 7: c1 = 'S', c2 = ' ', c3 = ' ', c4 = ' '; break;   // SAVE
      case 8: c1 = 'L', c2 = ' ', c3 = ' ', c4 = ' '; break;   // LOAD
      case 9: c1 = 'A', c2 = 'L', c3 = 'L', c4 = ' '; break;   // ALL for RESET
      case 10: c1 = 'A', c2 = 'L', c3 = 'L', c4 = ' '; break;  // ALL for MUTE
      case 11: c1 = 'T', c2 = ' ', c3 = ' ', c4 = ' '; break;  // TEMPO
      case 12: c1 = 'X', c2 = ' ', c3 = ' ', c4 = ' '; break;  // NEW RANDOM
      default: c1 = ' ', c2 = ' ', c3 = ' ', c4 = ' ';         // Default blank
    }
  }
}

// right side menue
void drawChannelEditMenu(byte select_ch, int select_menu) {
  // Avoid drawing left menu for random advance mode
  if (select_ch == 6 || select_ch > 6) return;  // Handle only valid channel edit modes and avoid random mode

  const char *labels[] = { "", "HITS", "OFFS", "LIMIT", "MUTE", "REST", "RAND", "PROB" };
  if (select_menu >= 1 && select_menu < 8) {
    leftMenu(labels[select_menu][0], labels[select_menu][1], labels[select_menu][2], labels[select_menu][3]);
  }
}

// left side menue
void drawModeMenu(byte select_ch) {
  if (select_ch < 7) return;  // Only for special (added) modes

  switch (select_ch) {
    case 7: leftMenu('S', 'A', 'V', 'E'); break;   // SAVE
    case 8: leftMenu('L', 'O', 'A', 'D'); break;   // LOAD
    case 9: leftMenu('R', 'E', 'S', 'T'); break;   // REST
    case 10: leftMenu('M', 'U', 'T', 'E'); break;  // MUTE
    case 11: leftMenu('T', 'E', 'M', 'P'); break;  // TEMPO
    case 12: leftMenu('R', 'A', 'N', 'D'); break;  // NEW RANDOM SEQUENCE SELECT MODE
    default: break;
  }
}


// Initialize EEPROM and check magic number
void checkAndInitializeSettings() {
  unsigned int magic;  // Assuming magic number can fit in an unsigned int
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);
  if (magic != FIRMWARE_MAGIC) {
    initializeDefaultRhythms();
    EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
  }
}


// Drawing random advance indicator
void drawRandomModeAdvanceSquare(int bar_select, int bar_now, const int *bar_max) {  // Change to const int*
  if (62 - bar_max[bar_select] * 2 >= 0 && 64 - bar_now * 2 >= 0) {
    display.drawRect(1, 62 - bar_max[bar_select] * 2, 6, bar_max[bar_select] * 2 + 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_now * 2, WHITE);
  }
}

void drawSelectionIndicator(int select_menu) {
  if (select_menu == 0) {
    display.drawTriangle(113, 0, 113, 6, 118, 3, WHITE);
  } else if (select_menu == 1) {
    display.drawTriangle(113, 9, 113, 15, 118, 12, WHITE);
  }

  if (select_ch != 6 && select_ch <= 6) {
    if (select_menu == 2) {
      display.drawTriangle(113, 18, 113, 24, 118, 21, WHITE);
    } else if (select_menu == 3) {
      display.drawTriangle(12, 34, 12, 41, 7, 39, WHITE);
    } else if (select_menu == 4) {
      display.drawTriangle(12, 34, 12, 41, 7, 39, WHITE);
    } else if (select_menu == 5) {
      display.drawTriangle(12, 42, 12, 51, 7, 48, WHITE);
    } else if (select_menu == 6) {
      display.drawTriangle(12, 50, 12, 61, 7, 57, WHITE);
    } else if (select_menu == 7) {
      display.drawTriangle(12, 50, 12, 61, 7, 57, WHITE);
    }
  }
}

void drawStepDots(const SlotConfiguration &currentConfig, const byte *graph_x, const byte *graph_y) {
  for (int k = 0; k <= 5; k++) {
    for (int j = 0; j < currentConfig.limit[k]; j++) {
      int x_pos = x16[j % 16] + graph_x[k];
      int y_pos = y16[j % 16] + graph_y[k];
      if (x_pos < 128 && y_pos < 64 && currentConfig.mute[k] == 0 && select_menu != 7) {
        display.drawPixel(x_pos, y_pos, WHITE);
      }
    }
  }
}

void OLED_display() {
  display.clearDisplay();
  // OLED display for Euclidean rhythm settings
  // Draw setting menu (WIP)
  // select_ch are the channels and >5 the modes -> random advance, save, load, global mute, sequence reset, ..
  // select_menu are parameters and functions for each single channel (hits,offs,limit,mute,rest,random,probability)

  // Characters to be displayed in right side Menu
  char c1 = ' ', c2 = 'H', c3 = 'O', c4 = ' ';
  setMenuCharacters(select_ch, c1, c2, c3, c4);
  rightMenu(c1, c2, c3, c4);  // Called once per update only

  // draw left side Menue
  drawChannelEditMenu(select_ch, select_menu);
  drawModeMenu(select_ch);

  // Random mode advance menu count square
  if (select_ch == 6) {
    drawRandomModeAdvanceSquare(bar_select, bar_now, bar_max);
  }

  // Selection Indicator and Step Dots
  drawSelectionIndicator(select_menu);
  // Draw step dots within display bounds
  drawStepDots(currentConfig, graph_x, graph_y);


  //draw hits line : 2~16hits if not muted
  for (k = 0; k <= 5; k++) {  // Iterate over each channel
    buf_count = 0;
    // Collect the hit points
    for (m = 0; m < 16; m++) {
      if (currentConfig.mute[k] == 0 && offset_buf[k][m] == 1) {
        int x_pos = x16[m] + graph_x[k];
        int y_pos = y16[m] + graph_y[k];
        if (x_pos < 128 && y_pos < 64 && select_menu != 7) {
          line_xbuf[buf_count] = x_pos;
          line_ybuf[buf_count] = y_pos;
          buf_count++;
        }
      }
    }


    // Draw the shape
    for (j = 0; j < buf_count - 1; j++) {
      display.drawLine(line_xbuf[j], line_ybuf[j], line_xbuf[j + 1], line_ybuf[j + 1], WHITE);
    }
    if (buf_count > 0) {
      display.drawLine(line_xbuf[0], line_ybuf[0], line_xbuf[buf_count - 1], line_ybuf[buf_count - 1], WHITE);
    }
  }

  for (j = 0; j < 16; j++) {  //line_buf reset
    line_xbuf[j] = 0;
    line_ybuf[j] = 0;
  }

  // draw hits line : 1hits if not muted
  for (k = 0; k <= 5; k++) {  // Channel count
    if (currentConfig.mute[k] == 0) {
      if (currentConfig.hits[k] == 1) {
        int x1 = 15 + graph_x[k];
        int y1 = 15 + graph_y[k];
        int x2 = x16[currentConfig.offset[k]] + graph_x[k];
        int y2 = y16[currentConfig.offset[k]] + graph_y[k];
        if (x1 < 128 && y1 < 64 && x2 < 128 && y2 < 64) {
          display.drawLine(x1, y1, x2, y2, WHITE);
        }
      }
    }
  }

  //draw play step circle
  for (k = 0; k <= 5; k++) {                               //ch count
    if (currentConfig.mute[k] == 0 && select_menu != 7) {  //mute on = no display circle
      if (offset_buf[k][playing_step[k]] == 0) {
        display.drawCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 2, WHITE);
      }
      if (offset_buf[k][playing_step[k]] == 1) {
        display.fillCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 3, WHITE);
      }
    }
  }

/* 
// fixme hackme ToDo
  //write hit and offset values for H > 9 to 16 hits if not muted and not in edit mode. better to just draw dots to where the hits will be instead of the shape and lines ?
  if (select_menu > 3 || select_menu == 0) {
    for (k = 0; k <= 5; k++) {
      if (currentConfig.hits[k] > 9 && currentConfig.mute[k] == 0) {  // show overview of channel if not muted
        int x_base = 10 + graph_x[k];
        int y_base_hit = 11 + graph_y[k];
        int y_base_offset = 17 + graph_y[k];
        if (x_base < 120 && y_base_hit < 64 && y_base_offset < 64 && select_menu != 7) {
          display.setCursor(x_base, y_base_hit);
          display.println(F("H"));
          display.print(currentConfig.hits[k]);

          display.setCursor(x_base, y_base_offset);
          display.println(F("L"));
          if (currentConfig.limit[k] == 0) {
            display.print(currentConfig.limit[k]);
          } else {
            display.print(16 - currentConfig.limit[k]);
          }
        }
      }
    }
  }
  */

  // draw channel info in edit mode, should be helpfull while editing.
  for (int ch = 0; ch < 6; ch++) {
    int x_base = graph_x[ch];
    int y_base = graph_y[ch] + 8;

    // draw selected parameter UI for currently active channel when editing
    if (select_menu != 0) {
      switch (select_menu) {
        case 1:                              // Hits
          if (currentConfig.hits[ch] > 6) {  // Display only if there is space in the UI
            if (x_base + 10 < 120 && y_base < 56) {

              display.setCursor(x_base + 10, y_base);  // Adjust position
              display.print(currentConfig.hits[ch]);
            }
          }
          break;
        case 2:
          break;
        case 3:  // Limit prevents from running draw L in to shape
          if (currentConfig.limit[ch] == 0 && currentConfig.hits[ch] > 3) {
            display.setCursor(x_base + 12, y_base + 4);
            if (x_base + 10 < 128 && y_base < 64) {
              display.println(F("L"));
            }
          }
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
        case 7:

          int barWidth = 4;    // Width of the probability bar
          int maxHeight = 15;  // Maximum height of the bar, adjust as needed
          int margin = 2;      // Margin around the fillin

          int bar_x = graph_x[ch] + 12;  // X position of the bar graph
          int bar_y = graph_y[ch] + 30;  // Y position of the bar graph, adjust for bar to start from the bottom up

          // Calculate the height of the bar based on probability
          int barHeight = map(currentConfig.probability[ch], 0, 100, 0, maxHeight);  // Map probability to bar height
          int startY = bar_y - barHeight;                                            // Calculate the top starting point of the filled bar

          // Calculate the outer rectangle dimensions
          int outerWidth = barWidth + 2 * margin;
          int outerHeight = maxHeight + 2 * margin;
          int outerX = bar_x - margin;
          int outerY = bar_y - maxHeight - margin;

          // Position the percentage text directly on top of the rectangle border
          int text_x = outerX + (outerWidth / 2) - 6;  // Centered over the border
          int text_y = outerY - 10;                    // Positioned just inside the top border

          // Ensure elements stay within display boundaries
          text_x = constrain(text_x, 0, 128);  // Constrain x to OLED width
          text_y = constrain(text_y, 0, 64);   // Constrain y to OLED height

          // Display percentage on top of the bar graph
          display.setCursor(text_x, text_y);
          display.print(currentConfig.probability[ch]);
          //display.println("%");

          if (select_menu == 7) {
            // Draw the outer rectangle
            display.drawRect(outerX, outerY, outerWidth, outerHeight, WHITE);

            // Calculate startY to begin at the bottom of the rectangle
            int startY = bar_y - barHeight;  // Start point for the filled bar, no bottom margin

            // Draw the filled bar part within the margins
            display.fillRect(bar_x, startY, barWidth, barHeight, WHITE);  // Adjust to only fill within the border
          }
          break;
      }
    }
  }
  display.display();
}
