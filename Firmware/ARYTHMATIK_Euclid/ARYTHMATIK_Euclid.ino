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
 * - Rotary encoder for parameter adjustments and quick menu navigation.
 * - Reset through dedicated input and button press
 * - Configurat
 *
 * Hardware:
 * - Clock input (CLK) for timing triggers.
 * - Inputs: Clock (CLK), Reset (RST), and Rotary Encoder (with Button).
 * - Outputs: 6 Trigger Channels with LED indicators.
 *
 * Encoder:
 * - short press: Toggle between menue options (right side) & left menue options
 * - rotate CW: dial in individual channel values for Hits, Offset, Limit, Mute. Reset channel, randomize channel
 * - rotate CCW: quick access menue for sequence Reset & Mute
 *
 */

// Flag for reversing the encoder direction.
// ToDo: Put this in config Menue dialog at boot ?
// #define REVERSE_ENCODER

// Flag for using the panel upside down
// ToDo: change to be in line with libModulove, put in config Menue dialog
// #define ROTATE_PANEL

#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EEPROM.h>
#include <Encoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

//#define DEBUG  // Uncomment for enabling debug print to serial monitoring output. Note: this affects performance and locks LED 4 & 5 on HIGH.
int debug = 0;  // ToDo: rework the debug feature (menue?)

// For debug / UI
#define FIRMWARE_MAGIC "EUCLIDBEAT"
#define FIRMWARE_MAGIC_ADDRESS 0  // Store the firmware magic number at address 0

// OLED
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define ENCODER_COUNT_PER_CLICK 4

// EEPROM
#define NUM_MEMORY_SLOTS 3
#define EEPROM_START_ADDRESS 5
#define CONFIG_SIZE (sizeof(SlotConfiguration))

// Pins
const int encoderAPin = 3, encoderBPin = 2, encoderButtonPin = 4, ButtonPin = 12;
const int rstPin = 11, clkPin = 13, buttonPin = 12;
const int chPins[6] = { 8, 9, 10, 5, 6, 7 };      // Channel output pins
const int ledPins[6] = { 0, 1, 17, 14, 15, 16 };  // Corresponding LED pins

// Timing
unsigned long startMillis, currentMillis, lastTriggerTime, internalClockPeriod;
unsigned long enc_timer, lastButtonPress;  // Timer for managing encoder input debounce
bool trg_in = false, old_trg_in = false, rst_in = false, old_rst_in = false;
int oldPosition = -999, newPosition = -999;  // positions of the encoder
int internalBPM = 120, lastBPM = 120;        // ToDo: Add internal Clock feature
byte playing_step[6] = { 0, 0, 0, 0, 0, 0 };

// display Menu and UI
byte select_menu = 0;   //0=CH,1=HIT,2=OFFSET,3=LIMIT,4=MUTE,5=RESET,6=RANDOM MOD
byte select_ch = 0;     //0~5 = each channel -1 , 6 = random mode
bool disp_refresh = 0;  //0=not refresh display , 1= refresh display , countermeasure of display refresh bussy

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 };  //each chanel display offset
const byte graph_y[6] = { 1, 1, 1, 33, 33, 33 };    //each chanel display offset -> +1 vs. original

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
constexpr byte bar_max[6] = { 2, 4, 6, 8, 12, 16 };  // control Random advance mode 6
byte bar_select = 4;                                 // ToDo: selected bar needs to be saved as well!
byte step_cnt = 0;

// Display Setup
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Encoder
Encoder myEnc(encoderAPin, encoderBPin);  // Setup encoder on specific pins
bool encD = false, old_encD = false, encU = false, old_encU = false;
bool buttonPressed = false;  // State of the button
bool flr = 1;                // first loop run -> no encU wanted
int i = 0;                   // ch 1- 6

// push button
bool sw = 0, old_sw;
unsigned long sw_timer = 0;

// each channel param
byte hits[6] = { 4, 2, 8, 1, 2, 3 }, offset[6] = { 0, 4, 0, 1, 3, 15 }, mute[6] = { 0, 0, 0, 0, 0, 0 }, limit[6] = { 16, 16, 16, 16, 16, 16 };

// Structure to hold configuration for a channel
struct SlotConfiguration {
  byte hits[6], offset[6], mute[6], limit[6];
};

// default config for presets (WIP)
const SlotConfiguration defaultSlots[3] PROGMEM = {
  { { 4, 3, 4, 2, 4, 3 }, { 0, 1, 2, 1, 0, 2 }, { false, false, false, false, false, false }, { 13, 12, 8, 14, 12, 9 } },    // Techno
  { { 4, 3, 5, 3, 2, 4 }, { 0, 1, 2, 3, 0, 2 }, { false, false, false, false, false, false }, { 15, 15, 15, 10, 12, 14 } },  // House
  { { 5, 4, 7, 3, 4, 5 }, { 0, 2, 1, 0, 2, 3 }, { false, false, false, false, false, false }, { 15, 14, 12, 15, 14, 15 } }  // Drum and Bass
};

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS];  // Memory slots for configurations


void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif

  iniIO();  // ToDo: use libModulove

  // init OLED
  delay(1000);  // Screen needs a sec to initialize
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

//inidDisplay()...
#ifdef ROTATE_PANEL
  display.setRotation(2);  // 180 degree rotation for upside-down use
#endif
  display.setTextSize(1);
  display.setTextColor(WHITE);

  checkAndInitializeSettings();

  // Display UI
  OLED_display();

  enc_timer = 0;
  lastTriggerTime = millis();
}

void loop() {
  old_trg_in = trg_in;
  old_rst_in = rst_in;
  old_encD = encD;
  old_encU = encU;
  oldPosition = newPosition;

  //-----------------push button----------------------
  sw = 1;
  if ((!FastGPIO::Pin<12>::isInputHigh()) && (sw_timer + 300 <= millis())) {  //push button on ,Logic inversion , sw_timer is countermeasure of chattering
    sw_timer = millis();
    sw = 0;
    disp_refresh = debug;
  }
  if (sw == 0) {
    disp_refresh = debug;
    select_menu++;
  }
  if (select_ch != 6 && select_menu >= 7) {  // not random mode
    select_menu = 0;
  }
  if (select_ch == 6 && select_menu > 1) {  // random mode
    select_menu = 0;                        //Ruecksetzung nach weiterem Klick aus Stufe select_menu = 1 (Occurence)
  }
  if (select_ch != 6 && select_menu < 0) {  // not random mode
    select_menu = 5;
  }
  if (select_ch == 6 && select_menu < 0) {  // random mode  //Begrenzer
    select_menu = 0;
  }
  if (select_ch >= 7 && select_menu >= 2) {  // modes only having a button
    select_menu = 0;
  }

  if (select_ch == 7 && select_menu == 1) {  // save whith encoder rotation
    saveConfiguration();
    select_menu = 0;
  }
  if (select_ch == 8 && select_menu == 1) {  // load whith encoder rotation
    loadConfiguration();
    select_menu = 0;
  }
  if (select_ch == 9 && select_menu == 1) {  // reset the whole sequence
    resetSeq();
    select_menu = 0;
  }
  if (select_ch == 10 && select_menu == 1) {  // check mute status and toggle mute
    toggleAllMutes();
    select_menu = 0;
  }


  if (select_ch == 11 && select_menu == 1) {  // modes only having a button
    if (internalBPM >= 180) {
      internalBPM = 180;
    }
    if (internalBPM <= 60) {
      internalBPM = 60;
    }
    // Dial in tempo with the encoder or TapTempo via encoder button
    adjustTempo();
    //disp_refresh = 1;
  }
  if (select_ch == 12 && select_menu == 1) {  //
    // This needs to work as before where you advance the random state by rotating the encoder
    Random_change();
    disp_refresh = 1;
    //select_menu = 0;
  }


  //-----------------Rotary encoder read----------------------
  newPosition = myEnc.read() / ENCODER_COUNT_PER_CLICK;
  if (newPosition < oldPosition && enc_timer + 100 < millis()) {  //turn left
    enc_timer = millis();
    oldPosition = newPosition;
    disp_refresh = debug;  //Enable while debugging.
    encD = 1;
  } else {
    encD = 0;
  }

  if (newPosition > oldPosition && enc_timer + 100 < millis()) {  //turn right
    enc_timer = millis();
    oldPosition = newPosition;
    disp_refresh = debug;  //Enable while debugging.
    encU = 1;
    if (flr == 1) {          //suppresses faulty encU in first loop run
      disp_refresh = debug;  // only refresh whith encoder when debug mode is enabled (debug=1), else there might be jumps in the sequence while running
      encU = 0;
      flr = 0;
    }
  } else {
    encU = 0;
  }

  if (old_encU == 0 && encU == 1) {
    switch (select_menu) {
      case 0:  //select channel
               // Modes are: 0,1,2,3,4,5,6,7,8,9,10,11,12 (0-5 are channels, 6 is random, 7 is complete mute, 8 is save, 9 load, 10 settings factory reset, 11 MUTE ALL, 12 tempo adjust, 13 new random mode)
        select_ch++;
        // Mode 12 + 13 are in development and testing right now
        if (select_ch >= 11) {
          select_ch = 0;
        }
        break;

      case 1:                                    // Hits
        if (select_ch != 6 && select_ch <= 6) {  // dial in hits
          hits[select_ch]++;
          if (hits[select_ch] >= 17) {
            hits[select_ch] = 0;
          }
        }
        // reintroduce the dial to change duration for random mode with more fine control. Defaults to 8 bars now.
        else if (select_ch == 6) {  // random mode
          bar_select++;
          if (bar_select >= 6) {
            bar_select = 5;
          }
        }
        break;

      case 2:  // offset
        offset[select_ch]++;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 15;
        }
        break;

      case 3:  // limit
        limit[select_ch]++;
        if (limit[select_ch] >= 17) {
          limit[select_ch] = 0;
        }
        break;

      case 4:  // mute
        mute[select_ch] = !mute[select_ch];
        break;

      case 5:  // reset ch
        playing_step[select_ch] = 0;
        break;

      case 6:  // random advance selected ch
        Random_change_one(select_ch);
        break;
    }
  }

  if (old_encD == 0 && encD == 1) {
    switch (select_menu) {
      case 0:  // select ch
        select_ch--;
        if (select_ch >= 0) {  // quick access menue (reset and mute)
          select_ch = 9;
        }
        break;

      case 1:                  // Hits
        if (select_ch != 6) {  // not random mode
          hits[select_ch]--;
          if (hits[select_ch] >= 17) {  // prevent overflow
            hits[select_ch] = 16;
          }
        } else if (select_ch == 6) {  // random mode
          bar_select--;
          if (bar_select >= 6) {  // six different change length setting
            bar_select = 0;
          }
        }
        break;

      case 2:  // offset
        offset[select_ch]--;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 0;
        }
        break;

      case 3:                        // Limit
        if (limit[select_ch] > 0) {  // Check if greater 0
          limit[select_ch]--;
        }
        break;

      case 4:  // mute
        mute[select_ch] = !mute[select_ch];
        break;

      case 5:  // reset ch
        playing_step[select_ch] = 0;
        break;

      case 6:  // random advance selected ch
        Random_change_one(select_ch);
        break;
    }
  }

  //-----------------offset setting----------------------
  for (k = 0; k <= 5; k++) {  //k = 1~6ch
    for (i = offset[k]; i <= 15; i++) {
      offset_buf[k][i - offset[k]] = (pgm_read_byte(&(euc16[hits[k]][i])));
    }

    for (i = 0; i < offset[k]; i++) {
      offset_buf[k][16 - offset[k] + i] = (pgm_read_byte(&(euc16[hits[k]][i])));
    }
  }

//-----------------trigger detect, reset & output----------------------
#ifdef ROTATE_PANEL
  rst_in = FastGPIO::Pin<13>::isInputHigh();  //external reset
#else
  rst_in = FastGPIO::Pin<11>::isInputHigh();  //external reset
#endif
  if (old_rst_in == 0 && rst_in == 1) {
    for (k = 0; k <= 5; k++) {
      playing_step[k] = 0;
      disp_refresh = 1;
    }
  }
#ifdef ROTATE_PANEL
  trg_in = FastGPIO::Pin<11>::isInputHigh();
#else
  trg_in = FastGPIO::Pin<13>::isInputHigh();
#endif

  // no external trigger for more than 8 sec -> internal clock (not implemented yet)
  if (old_trg_in == 0 && trg_in == 0 && gate_timer + 8000 <= millis()) {
    //useInternalClock = true;
    //debug = 1;
    disp_refresh = 1;
  } else if (old_trg_in == 0 && trg_in == 1) {
    gate_timer = millis();
    //useInternalClock = false;
    FastGPIO::Pin<4>::setOutput(1);
    debug = 0;
    for (i = 0; i <= 5; i++) {
      playing_step[i]++;
      if (playing_step[i] >= limit[i]) {
        playing_step[i] = 0;  // step limit is reached
      }
    }
#ifdef ROTATE_PANEL
    // do stuff the other way around
    for (k = 0; k <= 5; k++) {  //output gate signal
      if (offset_buf[k][playing_step[k]] == 1 && mute[k] == 0) {
        switch (k) {
          case 0:  //CH1
            FastGPIO::Pin<8>::setOutput(1);
            FastGPIO::Pin<0>::setOutput(1);
            break;

          case 1:  //CH2
            FastGPIO::Pin<9>::setOutput(1);
            FastGPIO::Pin<1>::setOutput(1);
            break;

          case 2:  //CH3
            FastGPIO::Pin<10>::setOutput(1);
            FastGPIO::Pin<17>::setOutput(1);
            break;

          case 3:  //CH4
            FastGPIO::Pin<5>::setOutput(1);
            FastGPIO::Pin<14>::setOutput(1);
            break;

          case 4:  //CH5
            FastGPIO::Pin<6>::setOutput(1);
            FastGPIO::Pin<15>::setOutput(1);
            break;

          case 5:  //CH6
            FastGPIO::Pin<7>::setOutput(1);
            FastGPIO::Pin<16>::setOutput(1);
            break;
        }
      }
    }
#else
    for (k = 0; k <= 5; k++) {  //output gate signal
      if (offset_buf[k][playing_step[k]] == 1 && mute[k] == 0) {
        switch (k) {
          case 0:  //CH1
            FastGPIO::Pin<5>::setOutput(1);
            FastGPIO::Pin<14>::setOutput(1);
            break;

          case 1:  //CH2
            FastGPIO::Pin<6>::setOutput(1);
            FastGPIO::Pin<15>::setOutput(1);
            break;

          case 2:  //CH3
            FastGPIO::Pin<7>::setOutput(1);
            FastGPIO::Pin<16>::setOutput(1);
            break;

          case 3:  //CH4
            FastGPIO::Pin<8>::setOutput(1);
            FastGPIO::Pin<0>::setOutput(1);
            break;

          case 4:  //CH5
            FastGPIO::Pin<9>::setOutput(1);
            FastGPIO::Pin<1>::setOutput(1);
            break;

          case 5:  //CH6
            FastGPIO::Pin<10>::setOutput(1);
            FastGPIO::Pin<17>::setOutput(1);
            break;
        }
      }
    }
#endif
    disp_refresh = 1;  //Updates the display where the trigger was entered.If it update it all the time, the response of gate on will be worse.

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
    FastGPIO::Pin<4>::setOutput(0);
    FastGPIO::Pin<14>::setOutput(0);
    FastGPIO::Pin<15>::setOutput(0);
    FastGPIO::Pin<16>::setOutput(0);
    FastGPIO::Pin<17>::setOutput(0);
    FastGPIO::Pin<0>::setOutput(0);
    FastGPIO::Pin<1>::setOutput(0);
  }

  if (disp_refresh) {
    OLED_display();  // refresh display
    disp_refresh = 0;
  }
}


void iniIO() {
  FastGPIO::Pin<11>::setInput();          // RST
  FastGPIO::Pin<12>::setInputPulledUp();  // BUTTON
  FastGPIO::Pin<13>::setInput();          // CLK
  FastGPIO::Pin<3>::setInputPulledUp();   // ENCODER A
  FastGPIO::Pin<2>::setInputPulledUp();   // ENCODER B
  // Outputs
  FastGPIO::Pin<5>::setOutputLow();   // CH1
  FastGPIO::Pin<6>::setOutputLow();   // CH2
  FastGPIO::Pin<7>::setOutputLow();   // CH3
  FastGPIO::Pin<8>::setOutputLow();   // CH4
  FastGPIO::Pin<9>::setOutputLow();   // CH5
  FastGPIO::Pin<10>::setOutputLow();  // CH6
  // LED outputs
  FastGPIO::Pin<14>::setOutputLow();  // CH1 LED
  FastGPIO::Pin<15>::setOutputLow();  // CH2 LED
  FastGPIO::Pin<16>::setOutputLow();  // CH3 LED
  FastGPIO::Pin<17>::setOutputLow();  // CH6 LED
  FastGPIO::Pin<0>::setOutputLow();   // CH4 LED
  FastGPIO::Pin<1>::setOutputLow();   // CH5 LED
  FastGPIO::Pin<4>::setOutputLow();   // CLK LED
}


void factoryReset() {
  initializeDefaultRhythms();  // Re-initialize EEPROM with default rhythms
  // Indicate on display or via LED that factory reset is complete?
}


void saveToEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);
  EEPROM.put(baseAddress, memorySlots[slot]);
}

void loadFromEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);
  EEPROM.get(baseAddress, memorySlots[slot]);
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
    display.print("Save to Slot:");
    display.setCursor(60, 29);
    display.setTextSize(2);
    display.print(selectedSlot + 1);
    display.setTextSize(1);
    display.display();


    // Check for encoder rotation to select memory slot
    newPosition = myEnc.read() / ENCODER_COUNT_PER_CLICK;
    if (newPosition != oldPosition) {
      if (newPosition > oldPosition) {
        selectedSlot++;
        if (selectedSlot >= NUM_MEMORY_SLOTS) {
          selectedSlot = 0;
        }
      } else {
        selectedSlot--;
        if (selectedSlot < 0) {
          selectedSlot = NUM_MEMORY_SLOTS - 1;
        }
      }
      oldPosition = newPosition;
    }

    // Check for button press
    sw = digitalRead(ButtonPin) == LOW;
    if (sw && sw_timer + 300 <= millis()) {  // button pressed
      sw_timer = millis();
      saving = false;
      for (int ch = 0; ch < 6; ++ch) {
        memorySlots[selectedSlot].hits[ch] = hits[ch];
        memorySlots[selectedSlot].offset[ch] = offset[ch];
        memorySlots[selectedSlot].mute[ch] = mute[ch];
        memorySlots[selectedSlot].limit[ch] = limit[ch];
      }
      saveToEEPROM(selectedSlot);
      displaySuccessMessage();
    }
  }
}

void loadConfiguration() {
  int selectedSlot = 0;
  bool loading = true;

  while (loading) {
    // Display selected slot
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(24, 10);
    display.print("Load from Slot:");
    display.setCursor(60, 29);
    display.setTextSize(2);
    display.print(selectedSlot + 1);
    display.setTextSize(1);
    display.display();

    // encoder rotation to select memory slot
    newPosition = myEnc.read() / ENCODER_COUNT_PER_CLICK;
    if (newPosition != oldPosition) {
      if (newPosition > oldPosition) {
        selectedSlot++;
        if (selectedSlot >= NUM_MEMORY_SLOTS) {
          selectedSlot = 0;
        }
      } else {
        selectedSlot--;
        if (selectedSlot < 0) {
          selectedSlot = NUM_MEMORY_SLOTS - 1;
        }
      }
      oldPosition = newPosition;
    }

    // Check for button press to load configuration
    sw = digitalRead(ButtonPin) == LOW;
    if (sw && sw_timer + 300 <= millis()) {  // Push button pressed
      sw_timer = millis();
      loadFromEEPROM(selectedSlot);  // Load values from EEPROM first

      // Update individual arrays after loading from EEPROM
      for (int ch = 0; ch < 6; ++ch) {
        hits[ch] = memorySlots[selectedSlot].hits[ch];
        offset[ch] = memorySlots[selectedSlot].offset[ch];
        mute[ch] = memorySlots[selectedSlot].mute[ch];
        limit[ch] = memorySlots[selectedSlot].limit[ch];  // Load limit values
      }

      displaySuccessMessage();
      loading = false;
    }
  }
}

void saveCurrentConfigurationToEEPROM() {
  // could save to the last selected save slot. For now default slot5
  saveToEEPROM(5);
}

void displaySuccessMessage() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 20);
  display.println(F("DONE."));
  display.display();
  delay(300);
  display.setTextSize(1);
  display.clearDisplay();
}


// reset the whole thing and put in traditional euclid patterns?
// if function is in use, module will not start / init display ?
void initializeDefaultRhythms() {
  SlotConfiguration tempConfig;  // Temporary storage for configuration data

  for (int slot = 0; slot < 2; slot++) {
    // Copy each slot configuration from PROGMEM to RAM
    memcpy_P(&tempConfig, &defaultSlots[slot], sizeof(SlotConfiguration));
    // Now save this configuration to EEPROM
    saveDefaultsToEEPROM(slot, tempConfig);
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
      hits[k] = random(hit_rng_min[k], hit_rng_max[k]);
    }

    if (off_occ[k] >= random(1, 100)) {
      offset[k] = random(0, 16);
    }

    if (mute_occ[k] >= random(1, 100)) {
      mute[k] = 1;
    } else if (mute_occ[k] < random(1, 100)) {
      mute[k] = 0;
    }
  }
}

// random change function for one channel (no mute))
void Random_change_one(int select_ch) {

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  if (random(100) < hit_occ[select_ch]) {
    hits[select_ch] = random(hit_rng_min[select_ch], hit_rng_max[select_ch] + 1);
  }

  if (off_occ[select_ch] >= random(1, 100)) {
    offset[select_ch] = random(0, 16);
  }
}

void toggleAllMutes() {
  // Toggle mute for all channels
  bool allMuted = true;
  for (int i = 0; i < 6; i++) {
    if (mute[i] == 0) {
      allMuted = false;
      break;
    }
  }
  for (int i = 0; i < 6; i++) {
    mute[i] = !allMuted;
  }
}

void adjustTempo() {
  // adjust tempo with the rotary encoder, taptempo via encoder click
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

void setMenuCharacters(int select_ch, char &c1, char &c2, char &c3, char &c4) {
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
void drawChannelEditMenu(int select_ch, int select_menu) {
  // Avoid drawing left menu for random advance mode
  if (select_ch == 6 || select_ch > 6) return;  // Handle only valid channel edit modes and avoid random mode

  const char *labels[] = { "", "HITS", "OFFS", "LIMIT", "MUTE", "REST", "RAND" };
  if (select_menu >= 1 && select_menu < 7) {
    leftMenu(labels[select_menu][0], labels[select_menu][1], labels[select_menu][2], labels[select_menu][3]);
  }
}

// left side menue
void drawModeMenu(int select_ch) {
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

// Check EEPROM SETTINGS
void checkAndInitializeSettings() {
  int magic;
  EEPROM.get(FIRMWARE_MAGIC_ADDRESS, magic);
  if (magic != FIRMWARE_MAGIC) {
    // Initialize defaults if the magic number does not match
    initializeDefaultRhythms();
    // Set initial settings for each slot
    // no currentBPM, isRunnning yet
    /*
    for (int i = 0; i < NUM_MEMORY_SLOTS; i++) {
      memorySlots[i].currentBPM = 120;  // Default BPM
      memorySlots[i].isRunning = false; // Default state is not running
      EEPROM.put(EEPROM_START_ADDRESS + i * CONFIG_SIZE, memorySlots[i]);
    
    }
    */
    
    // Store the magic number
    EEPROM.put(FIRMWARE_MAGIC_ADDRESS, FIRMWARE_MAGIC);
  }
}

void OLED_display() {
  display.clearDisplay();
  // OLED display for Euclidean rhythm settings

  // Draw setting menu
  // select_ch are the channels and >6 modes random advance, save, load, global mute, sequence reset
  // select_menu are parameters and functions for each single channel (hits,offs,limit,mute,rest,random)

  // Characters to be displayed using rightMenu
  char c1 = ' ', c2 = 'H', c3 = 'O', c4 = ' ';
  setMenuCharacters(select_ch, c1, c2, c3, c4);
  rightMenu(c1, c2, c3, c4);  // Called once per update only

  drawChannelEditMenu(select_ch, select_menu);
  drawModeMenu(select_ch);

  // random mode advance menue count square
  if (select_ch == 6) {  //random mode specific
    display.drawRect(1, 62 - bar_max[bar_select] * 2, 6, bar_max[bar_select] * 2 + 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_max[bar_select] * 2, WHITE);
  }

  // Draw selection indicator triangle TODO: make this more slik
  if (select_menu == 0) {
    display.drawTriangle(113, 0, 113, 6, 118, 3, WHITE);
  } else if (select_menu == 1) {
    display.drawTriangle(113, 9, 113, 15, 118, 12, WHITE);
  }

  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes
    if (select_menu == 2) {
      display.drawTriangle(113, 18, 113, 24, 118, 21, WHITE);
    } else if (select_menu == 3) {
      display.drawTriangle(12, 26, 12, 31, 7, 30, WHITE);
    } else if (select_menu == 4) {
      display.drawTriangle(12, 34, 12, 42, 7, 39, WHITE);
    } else if (select_menu == 5) {
      display.drawTriangle(12, 45, 12, 51, 7, 48, WHITE);
    } else if (select_menu == 6) {
      display.drawTriangle(12, 54, 12, 60, 7, 57, WHITE);
    }
  }

  // Draw step dots within display bounds
  for (k = 0; k <= 5; k++) {          // k = 1~6ch
    for (j = 0; j < limit[k]; j++) {  // j = steps
      int x_pos = x16[j % 16] + graph_x[k];
      int y_pos = y16[j % 16] + graph_y[k];
      if (x_pos < 128 && y_pos < 64) {
        display.drawPixel(x_pos, y_pos, WHITE);
      }
    }
  }

  //draw hits line : 2~16hits if not muted (less is more clear)
  for (k = 0; k <= 5; k++) {  //ch count
    buf_count = 0;
    for (m = 0; m < 16; m++) {
      if (mute[k] == 0) {
        if (offset_buf[k][m] == 1) {
          int x_pos = x16[m] + graph_x[k];
          int y_pos = y16[m] + graph_y[k];
          if (x_pos < 128 && y_pos < 64) {
            line_xbuf[buf_count] = x_pos;
            line_ybuf[buf_count] = y_pos;
            buf_count++;
          }
        }
      }
    }

    for (j = 0; j < buf_count - 1; j++) {
      display.drawLine(line_xbuf[j], line_ybuf[j], line_xbuf[j + 1], line_ybuf[j + 1], WHITE);
    }
    display.drawLine(line_xbuf[0], line_ybuf[0], line_xbuf[j], line_ybuf[j], WHITE);
  }
  for (j = 0; j < 16; j++) {  //line_buf reset
    line_xbuf[j] = 0;
    line_ybuf[j] = 0;
  }

  // draw hits line : 1hits if not muted
  for (k = 0; k <= 5; k++) {  // Channel count
    if (mute[k] == 0) {
      if (hits[k] == 1) {
        int x1 = 15 + graph_x[k];
        int y1 = 15 + graph_y[k];
        int x2 = x16[offset[k]] + graph_x[k];
        int y2 = y16[offset[k]] + graph_y[k];
        if (x1 < 128 && y1 < 64 && x2 < 128 && y2 < 64) {
          display.drawLine(x1, y1, x2, y2, WHITE);
        }
      }
    }
  }

  // draw play step circle -> draws outside the display bounds!
  for (k = 0; k <= 5; k++) {
    if (mute[k] == 0) {
      if (offset_buf[k][playing_step[k]] == 0) {
        display.drawCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 2, WHITE);
      }
      if (offset_buf[k][playing_step[k]] == 1) {
        display.fillCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 3, WHITE);
      }
    }
  }

  //write hit and offset values for H > 9 to 16 hits
  for (k = 0; k <= 5; k++) {
    if (hits[k] > 9) {
      int x_base = 7 + graph_x[k];
      int y_base_hit = 8 + graph_y[k];
      int y_base_offset = 17 + graph_y[k];
      if (x_base < 120 && y_base_hit < 64 && y_base_offset < 64) {
        display.setCursor(x_base, y_base_hit);
        display.print("h");
        display.print(hits[k]);

        display.setCursor(x_base, y_base_offset);
        display.print("o");
        if (offset[k] == 0) {
          display.print(offset[k]);
        } else {
          display.print(16 - offset[k]);
        }
      }
    }
  }
  display.display();
}
