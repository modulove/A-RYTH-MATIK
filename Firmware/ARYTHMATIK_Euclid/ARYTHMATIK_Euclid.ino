/**
 * @file ARYTHMATIK_Euclid.ino
 * @author Modulove & friends (https://github.com/modulove/)
 * @brief 6CH Eurorack (HAGIWO) euclidean Rhythm Generator with SSD1306 0.96 OLED
 * @version 0.1
 * @date 2024-04-23
 *
 * @copyright Copyright (c) 2024
 *
 * Connect a clock source to the CLK input and each output will
 * output triggers according to settings set in the UI OLED screen
 *
 * Encoder:
 *      short press: Toggle between top menue options (dial with rotation)
 *      long press: Save settings to default slot 1.
 *
 *
 * RST: Trigger this input to reset the sequence.
 *
 */

// Flag for enabling debug print to serial monitoring output.
// Note: this affects performance and locks LED 4 & 5 on HIGH.
// #define DEBUG

// Flag for reversing the encoder direction.
// #define ENCODER_REVERSED

// Flag for using the panel upside down
// #define PANEL_USD

int debug = 0;  // 1 =on 0 =off
unsigned long startMillis;
unsigned long currentMillis;
const unsigned long period = 1000;
const byte ledPin = 4;
int tempo = 120, lasttempo = 120;
long previousMillis = 0, interval = 1000;

#include <FastGPIO.h>
#include <EEPROM.h>

//Encoder setting
#define ENCODER_OPTIMIZE_INTERRUPTS  //countermeasure of encoder noise
#include <Encoder.h>

/*

// Use SimpleRotary (not yet)
#include <SimpleRotary.h>

// Define pins for the rotary encoder.
int pinA = 3;       // CLK
int pinB = 2;       // DT
int buttonPin = 4;  // SW

// Create a SimpleRotary object.
SimpleRotary rotary(pinA, pinB, buttonPin);

*/

//Oled setting
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define ENCODER_COUNT_PER_CLICK 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// rotary encoder
// ToDo: Add switch for inverting encoder direction
Encoder myEnc(3, 2);  //use 3pin 2pin
int oldPosition = -999;
int newPosition = -999;
int i = 0;
bool flr = 1;  //first loop run -> no encU wanted

// additional internal clock
// ToDo: Add internal clock running at 120BPM if no clock for 8 sec
// Use encoder to Tap BPM and dial in Menue (like in cvSeq)
// Save BPM in EEPROM
unsigned long lastTriggerTime = 0;
bool useInternalClock = false;
int internalBPM = 120;
int lastBPM = 120;
unsigned long internalClockPeriod = 60000 / internalBPM / 2;
unsigned long lastInternalClockTime = 0;

// Configuration data structure for each save state slot
// ToDo: Add version and routine to populate default settings in EEPROM if nothing is in there
struct SlotConfiguration {
  byte hits[6];
  byte offset[6];
  byte mute[6];
  byte limit[6];
};

// Default patterns for five slots (factoryReset) test and select
// ToDO: Add more generative spatterns?
// Why using 10 save slots breake the thing?

/*
// Traditional music with euclid
SlotConfiguration defaultSlots[5] = {
  { { 3, 3, 2, 3, 1, 1 }, { 0, 1, 0, 3, 0, 2 }, { true, true, true, true, true, true }, { 8, 8, 8, 8, 8, 8 } },        // Bossa Nova (use clave, bass drum, snare, and hi-hat)
  { { 5, 4, 3, 4, 3, 2 }, { 0, 1, 3, 2, 1, 0 }, { true, true, true, true, true, true }, { 12, 12, 12, 12, 12, 12 } },  // Son Clave (Rhythm in many Afro-Cuban music styles, often played with claves, congas, and other percussion instruments)
  { { 5, 4, 3, 4, 3, 2 }, { 0, 2, 3, 1, 1, 0 }, { true, true, true, true, true, true }, { 12, 12, 12, 12, 12, 12 } },  // Rumba Clave (Similar instruments as Son Clave but with a different rhythm)
  { { 3, 3, 2, 3, 1, 1 }, { 0, 1, 0, 3, 0, 2 }, { true, true, true, true, true, true }, { 8, 8, 8, 8, 8, 8 } },        // Tresillo (Common rhythm in Latin American music, played on a variety of percussion instruments)
  { { 7, 6, 5, 4, 3, 2 }, { 0, 1, 3, 2, 1, 0 }, { true, true, true, true, true, true }, { 16, 16, 16, 16, 16, 16 } }  // Cumbia (steady backbeat along with syncopated rhythms typically found in Colombian music)
};
*/

// Mhh yummy electronica
SlotConfiguration defaultSlots[5] = {
  { { 4, 3, 4, 2, 4, 3 }, { 0, 1, 2, 1, 0, 2 }, { true, true, true, true, true, true }, { 16, 12, 8, 16, 12, 9 } },    // Techno (Pattern with varying lengths that evolves over time)
  { { 4, 3, 5, 3, 2, 4 }, { 0, 1, 2, 3, 0, 2 }, { true, true, true, true, true, true }, { 16, 15, 16, 10, 12, 18 } },  // House (mix of steady beats and syncopated patterns to keep groove fresh)
  { { 5, 4, 7, 3, 4, 5 }, { 0, 2, 1, 0, 2, 3 }, { true, true, true, true, true, true }, { 16, 14, 12, 16, 18, 16 } },  // Drum and Bass (Fast, complex drum patterns that shift against each other to create energetic grooves)
  { { 3, 4, 2, 3, 5, 4 }, { 0, 1, 2, 1, 0, 2 }, { true, true, true, true, true, true }, { 14, 12, 10, 14, 16, 12 } },  // Dubstep (halftime rhythms and syncopated snares, with different grove)
  { { 2, 3, 2, 3, 4, 2 }, { 0, 1, 0, 2, 1, 0 }, { true, true, true, true, true, true }, { 24, 18, 24, 21, 16, 30 } }   // Ambient (Minimal beats)
};

//push button
bool sw = 0, old_sw;
unsigned long sw_timer = 0;

//each channel param (modified by ms)
byte hits[6] = { 4, 2, 8, 1, 2, 3 }, offset[6] = { 0, 4, 0, 1, 3, 15 }, mute[6] = { 0, 0, 0, 0, 0, 0 }, limit[6] = { 16, 16, 16, 16, 16, 16 };

// Hagiwo defaults
//byte hits[6] = { 4, 4, 5, 3, 2, 16 };        //each channel hits
//byte offset[6] = { 0, 2, 0, 8, 3, 9 };       //each channele step offset
//byte mute[6] = { 0, 0, 0, 0, 0, 0 };         //mute 0 = off , 1 = on
//byte limit[6] = { 16, 16, 16, 16, 16, 16 };  //eache channel max step

//Sequence variables
byte j = 0, k = 0, m = 0, buf_count = 0;

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

bool encD = 0;
bool old_encD = 0;
bool encU = 0;
bool old_encU = 0;

bool trg_in = 0;
bool old_trg_in = 0;
bool rst_in = 0;
bool old_rst_in = 0;
byte playing_step[6] = { 0, 0, 0, 0, 0, 0 };
unsigned long gate_timer = 0;
unsigned long enc_timer = 0;

//display param
byte select_menu = 0;   //0=CH,1=HIT,2=OFFSET,3=LIMIT,4=MUTE,5=RESET,6=RANDOM MOD
byte select_ch = 0;     //0~5 = each channel -1 , 6 = random mode
bool disp_refresh = 0;  //0=not refresh display , 1= refresh display , countermeasure of display refresh bussy

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 };  //each chanel display offset
const byte graph_y[6] = { 1, 1, 1, 33, 33, 33 };    //each chanel display offset -> +1 vs. original

byte line_xbuf[17];
byte line_ybuf[17];

const byte x16[16] = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 };
const byte y16[16] = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//random assign
// making these settings configurable ?
byte hit_occ[6] = { 5, 1, 20, 20, 40, 80 };   //random change rate of occurrence
byte off_occ[6] = { 1, 3, 20, 30, 40, 20 };   //random change rate of occurrence
byte mute_occ[6] = { 0, 2, 20, 20, 20, 20 };  //random change rate of occurrence
byte hit_rng_max[6] = { 6, 5, 8, 4, 4, 6 };   //random change range of max
byte hit_rng_min[6] = { 3, 2, 2, 1, 1, 1 };   //random change range of min

byte bar_now = 1;
constexpr byte bar_max[6] = { 2, 4, 6, 8, 12, 16 };  // more fine control
byte bar_select = 4;                                 // selected bar -> This needs to be in EEPROM as well
byte step_cnt = 0;

//#define MAX_STEPS 16  // Adjust the value based on your actual maximum steps
constexpr byte MAX_STEPS = 16;

#define NUM_MEMORY_SLOTS 5      // Number of memory slots for saving patterns
#define EEPROM_START_ADDRESS 0  // Starting address in EEPROM to save data
#define CONFIG_SIZE (6 * 4)     // Size of configuration data to be saved for each slot (hits, offset, mute, limit)

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS];  // Array to store configurations for each slot

#define ButtonPin 12

// output pins for channels and corresponding LED pins (flip Panel 2024)
const int chPins[6] = {8, 9, 10, 5, 6, 7}; // CH1 to CH6 now mapped to pins as CH4, CH5, CH6, CH1, CH2, CH3
const int ledPins[6] = {0, 1, 17, 14, 15, 16}; // Mapping LEDs accodringly

void setup() {

#ifdef DEBUG
  Serial.begin(115200);
#endif

  // test display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  //   if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
  //  Serial.println(("SSD1306 allocation failed"));
  //  for(;;); // Don't proceed, loop forever
  // }

  // OLED setting
  delay(1000);  // Screen needs a sec to initialize
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

#ifdef PANEL_USD
  display.setRotation(2);  // 180 degree rotation for upside-down use
#else
  display.setRotation(0);  // Normal orientation
#endif
  display.setTextSize(1);
  display.setTextColor(WHITE);
  OLED_display();

  //pin mode setting
  FastGPIO::Pin<11>::setInput();          // RST
  FastGPIO::Pin<12>::setInputPulledUp();  //BUTTON
  FastGPIO::Pin<13>::setInput();          // CLK
  FastGPIO::Pin<3>::setInputPulledUp();   //ENCODER A
  FastGPIO::Pin<2>::setInputPulledUp();   //ENCODER B
  FastGPIO::Pin<5>::setOutputLow();       //CH1
  FastGPIO::Pin<6>::setOutputLow();       //CH2
  FastGPIO::Pin<7>::setOutputLow();       //CH3
  FastGPIO::Pin<8>::setOutputLow();       //CH4
  FastGPIO::Pin<9>::setOutputLow();       //CH5
  FastGPIO::Pin<10>::setOutputLow();      //CH6
  FastGPIO::Pin<14>::setOutputLow();      //CH1 LED (DIGITAL)
  FastGPIO::Pin<15>::setOutputLow();      //CH2 LED (DIGITAL)
  FastGPIO::Pin<16>::setOutputLow();      //CH3 LED (DIGITAL)
  FastGPIO::Pin<17>::setOutputLow();      //CH6 LED (DIGITAL)
  FastGPIO::Pin<0>::setOutputLow();       //CH4 LED (DIGITAL)
  FastGPIO::Pin<1>::setOutputLow();       //CH5 LED (DIGITAL)
  FastGPIO::Pin<4>::setOutputLow();       //CLK LED (DIGITAL)

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
    // Dial in tempo with rotating the encoder or TapTempo via encoder knob
    adjustTempo();
    //disp_refresh = 1;
  }
  if (select_ch == 12 && select_menu == 1) {  //
    // This need to work as before where you advance the random state by rotating the encoder
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

      case 1:                                    //hits
        if (select_ch != 6 && select_ch <= 6) {  // dial in hits for each channel
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

      case 2:  //offset (now its possible to use encoder in both directions)
        offset[select_ch]--;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 15;
        }
        break;

      case 3:  //limit
        limit[select_ch]++;
        if (limit[select_ch] >= 17) {
          limit[select_ch] = 0;
        }
        break;

      case 4:  //mute
        mute[select_ch] = !mute[select_ch];
        break;

      case 5:  //reset channel
        playing_step[select_ch] = 0;
        break;

      case 6:  //random advance (X in bottom left corner of OLED UI)
        Random_change_one(select_ch);
        //disp_refresh = 1;
        break;
    }
  }

  if (old_encD == 0 && encD == 1) {
    switch (select_menu) {
      case 0:  //select chanel
        select_ch--;
        if (select_ch >= 0) {  // quick access menue for reset and mute
          select_ch = 9;
        }
        break;

      case 1:                  //hits
        if (select_ch != 6) {  // not random mode
          hits[select_ch]--;
          if (hits[select_ch] >= 17) {  //Begrenzer nach unten durch Rücklauf "unter Null", d. h. auf 255 wegen vorzeichenloser Byte-Definition
            hits[select_ch] = 16;       //Schleife zu größtem Wert (damit 0 -> 16 möglich wird)
          }
        } else if (select_ch == 6) {  // random mode
          bar_select--;
          if (bar_select >= 6) {  // six different change length setting
            bar_select = 0;
          }
        }
        break;

      case 2:  //offset
        offset[select_ch]++;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 0;
        }
        break;

      case 3:  // there seemed to be a bug with the playing indicatior dot hiding but cant spot it now
        limit[select_ch]--;
        if (limit[select_ch] <= 0) {
          limit[select_ch] = 17;
        }
        break;

      case 4:  //mute
        mute[select_ch] = !mute[select_ch];
        break;

      case 5:  //reset selected ch only
        playing_step[select_ch] = 0;
        break;

      case 6:  //random advance selected ch
        Random_change_one(select_ch);
        //disp_refresh = 1;
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
  #ifdef PANEL_USD
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
  #ifdef PANEL_USD
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
    #ifdef PANEL_USD
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

  if (disp_refresh == 1) {
    OLED_display();  //refresh display
    disp_refresh = 0;
  }
}



void factoryReset() {
  initializeDefaultRhythms();  // Re-initialize EEPROM with default rhythms
  // Indicate on display or via LED that factory reset is complete
}


void saveToEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);

  for (int ch = 0; ch < 6; ++ch) {
    EEPROM.put(baseAddress + ch * sizeof(byte), memorySlots[slot].hits[ch]);        // hits
    EEPROM.put(baseAddress + 6 + ch * sizeof(byte), memorySlots[slot].offset[ch]);  // offset, starts after 6 bytes of hits
    EEPROM.put(baseAddress + 12 + ch * sizeof(byte), memorySlots[slot].mute[ch]);   // mute, starts after 6 bytes of offset
    EEPROM.put(baseAddress + 18 + ch * sizeof(byte), memorySlots[slot].limit[ch]);  // limit, starts after 6 bytes of mute
  }
}

void loadFromEEPROM(int slot) {
  int baseAddress = EEPROM_START_ADDRESS + (slot * CONFIG_SIZE);

  for (int ch = 0; ch < 6; ++ch) {
    EEPROM.get(baseAddress + ch * sizeof(byte), memorySlots[slot].hits[ch]);        // hits
    EEPROM.get(baseAddress + 6 + ch * sizeof(byte), memorySlots[slot].offset[ch]);  // offset
    EEPROM.get(baseAddress + 12 + ch * sizeof(byte), memorySlots[slot].mute[ch]);   // mute
    EEPROM.get(baseAddress + 18 + ch * sizeof(byte), memorySlots[slot].limit[ch]);  // limit
  }
}

void saveConfiguration() {
  int selectedSlot = 0;
  bool saving = true;

  while (saving) {
    // Display selected slot and save prompt on OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(20, 20);
    display.print("Save to Slot:");
    display.setTextSize(2);
    display.setCursor(30, 32);
    display.print(selectedSlot + 1);
    display.display();
    display.setTextSize(1);


    // Check for encoder rotation to select memory slot
    newPosition = myEnc.read();
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
      //  message on OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(20, 20);
      display.println(F("Configuration saved!"));
      display.display();
      delay(300);
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
    display.setCursor(20, 20);
    display.print("Load from Slot:");
    display.setCursor(32, 32);
    display.setTextSize(2);
    display.print(selectedSlot + 1);
    display.setTextSize(1);
    display.display();

    // encoder rotation to select memory slot
    newPosition = myEnc.read();
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

      // Display success message on OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(20, 20);
      display.println(F("Configuration loaded!"));
      display.display();
      delay(300);
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
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20, 20);
  display.println(F("Config saved"));
  display.display();
  delay(300);
  display.clearDisplay();
}


// reset the whole saved slots and put in traditional euclid patterns?
void initializeDefaultRhythms() {
  for (int slot = 0; slot < 5; slot++) {
    saveDefaultsToEEPROM(slot, defaultSlots[slot]);
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

// random change function for one channel
void Random_change_one(int select_ch) {

  unsigned long seed = analogRead(A0);
  randomSeed(seed);

  if (random(100) < hit_occ[select_ch]) {
    hits[select_ch] = random(hit_rng_min[select_ch], hit_rng_max[select_ch] + 1);
  }

  if (off_occ[select_ch] >= random(1, 100)) {
    offset[select_ch] = random(0, 16);
  }

  if (mute_occ[select_ch] >= random(1, 100)) {
    mute[select_ch] = 1;
  } else if (mute_occ[select_ch] < random(1, 100)) {
    mute[select_ch] = 0;
  }
}

void toggleAllMutes() {
  // Check if all channels are currently muted
  bool allMuted = true;
  for (int i = 0; i < 6; i++) {
    if (mute[i] == 0) {
      allMuted = false;
      break;
    }
  }

  // Toggle mute state for all channels
  for (int i = 0; i < 6; i++) {
    mute[i] = !allMuted;
  }

  if (!allMuted) {  // If it was not all muted before, it should be all muted now
    display.setTextColor(BLACK, WHITE);
  } else {
    display.setTextColor(WHITE, BLACK);  // Back to normal
  }

  display.setCursor(0, 30);
  display.print("M");
  display.setCursor(0, 39);
  display.print("U");
  display.setCursor(0, 48);
  display.print("T");
  display.setCursor(0, 57);
  display.print("E");

  // full display refresh always only if needed
  disp_refresh = 1;
  // display the buffer on the display
  //display.display();
}

void muteOne() {
  // mute though channels in a row
  if (myEnc.read() < 0) {
    for (int i = 0; i < 6; i++) {
      if (mute[i] == 0) {
        mute[i] = 1;
        if (i > 0) {
          mute[i - 1] = 0;
        }
        break;
      }
    }
  }
  // unmute through all channels in a row
  if (myEnc.read() > 0) {
    for (int i = 5; i >= 0; i--) {
      if (mute[i] == 1) {
        mute[i] = 0;
        if (i < 5) {
          mute[i + 1] = 1;
        }
        break;
      }
    }
  }
}


void adjustTempo() {
  // adjust tempo with the rotary encoder
  // taptempo via encoder click ?
}

void resetSeq() {
  for (k = 0; k <= 5; k++) {
    playing_step[k] = 0;
  }
}


void OLED_display() {
  display.clearDisplay();
  //-------------------------euclidean oled display------------------
  //draw setting menu
  display.setCursor(120, 0);
  // if select channel is not random mode (6) OR! not in modes higher than 7 (reset)
  if (select_ch != 6 && select_ch <= 6) {  // not random mode
    display.print(select_ch + 1);
  }
  if (select_ch == 6) {  // R for RANDOM
    display.print("R");
  } else if (select_ch == 7) {  // S for SAVE
    display.print("S");
  } else if (select_ch == 8) {  // L for LOAD
    display.print("L");
  } else if (select_ch == 9) {  // reset the whole sequence RESET -> ALL
    display.print("A");
  } else if (select_ch == 10) {  // MUTE -> ALL
    display.print("A");
  } else if (select_ch == 11) {  // TEMPO / TapTempo
    display.print("T");
  } else if (select_ch == 12) {  // NEW RANDOM
    display.print("X");
  }
  display.setCursor(120, 9);

  if (select_ch != 6 && select_ch <= 6) {  // not random mode
    display.print("H");                    // H Menue
  } else if (select_ch == 6) {
    display.print("N");  // RND
  } else if (select_ch == 9) {
    display.print("L");  // RESET ALL
  } else if (select_ch == 10) {
    display.print("L");          // MUTE ALL
  } else if (select_ch == 10) {  // Mute
    display.setCursor(120, 9);
    //display.drawRect(120, 9, 8, 8, WHITE);
    //display.println(F("<>"));
  }




  display.setCursor(120, 18);

  if (select_ch != 6 && select_ch <= 6) {  // not random mode
    display.print("O");                    // O Menue
  }
  if (select_ch != 6 && select_ch <= 6 && select_menu == 0) {  // not random mode
    display.setCursor(0, 30);
    display.print("L");  // Limit
    display.setCursor(0, 39);
    display.print("M");  // Mute
    display.setCursor(0, 48);
    display.print("R");  // RST
    display.setCursor(0, 57);
    display.print("X");  // RND
  }

  display.setCursor(120, 18);
  if (select_ch == 6) {
    display.print("D");  // RND
  }
  if (select_ch == 9) {  //
    display.print("L");  // RESET ALL
  }
  if (select_ch == 10) {  //
    display.print("L");   // MUTE ALL
  }

  if (select_ch != 6 && select_ch <= 6 && select_menu >= 7) {  // not random mode and no config modes
    display.print("O");                                        // Offset
    display.setCursor(0, 29);
    display.print("L");  // Loop
    display.setCursor(0, 38);
    display.print("M");  // Mute
    display.setCursor(0, 47);
    display.print("R");  // Reset
    display.setCursor(0, 56);
    display.print("X");  // Advance random mode (current channel)
  }

  // UI improvements showing more descriptive items on the left when curser is on the right side menue
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Hits
    if (select_menu == 1) {
      display.setCursor(0, 29);
      display.print("H");
      display.setCursor(0, 38);
      display.print("I");
      display.setCursor(0, 47);
      display.print("T");
      display.setCursor(0, 56);
      display.print("S");
      display.setCursor(120, 18);
      display.print("O");
    }
  }

  // UI improvement "OFFS"
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Offset
    if (select_menu == 2) {
      display.setCursor(0, 29);
      display.print("O");
      display.setCursor(0, 38);
      display.print("F");
      display.setCursor(0, 47);
      display.print("F");
      display.setCursor(0, 56);
      display.print("S");
      display.setCursor(120, 18);
      display.print("O");
    }
  }
  // UI improvement "LIMIT"
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Limit / Lengths
    if (select_menu == 3) {
      display.setCursor(0, 29);
      display.print("L");
      display.setCursor(0, 38);
      display.print("I");
      display.setCursor(0, 47);
      display.print("M");
      display.setCursor(0, 56);
      display.print("T");
      display.setCursor(120, 18);
      display.print("O");
    }
  }
  // UI improvement "MUTE"
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Mute
    if (select_menu == 4) {
      display.setCursor(0, 29);
      display.print("M");
      display.setCursor(0, 38);
      display.print("U");
      display.setCursor(0, 47);
      display.print("T");
      display.setCursor(0, 56);
      display.print("E");
      display.setCursor(120, 18);
      display.print("O");
    }
  }
  // UI improvement "RST"
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Reset
    if (select_menu == 5) {
      display.setCursor(0, 38);
      display.print("R");
      display.setCursor(0, 47);
      display.print("S");
      display.setCursor(0, 56);
      display.print("T");
      display.setCursor(120, 18);
      display.print("O");
    }
  }

  // UI improvement "RND"
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Random advance
    if (select_menu == 6) {
      display.setCursor(0, 38);
      display.print("R");
      display.setCursor(0, 47);
      display.print("N");
      display.setCursor(0, 56);
      display.print("D");
      display.setCursor(120, 18);
      display.print("O");
    }
  }
  // UI improvement "PROB" Placeholder / Featurecreeep ToDo:
  // Had a go at probability before but couldnt get it working, so leaving this for the future
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Random advance
    if (select_menu == 7) {
      display.setCursor(0, 29);
      display.print("P");
      display.setCursor(0, 38);
      display.print("R");
      display.setCursor(0, 47);
      display.print("O");
      display.setCursor(0, 56);
      display.print("B");
      display.setCursor(120, 18);
      display.print("O");
    }
  }

  // UI improvement "SWNG"
  // Would be nice to introduce some swing
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes -> Channel Edit mode Random advance
    if (select_menu == 8) {
      display.setCursor(0, 29);
      display.print("S");
      display.setCursor(0, 38);
      display.print("W");
      display.setCursor(0, 47);
      display.print("N");
      display.setCursor(0, 56);
      display.print("G");
      display.setCursor(120, 18);
      display.print("O");
    }
  }

  //random count square
  if (select_ch == 6) {  //random mode
    display.drawRect(1, 62 - bar_max[bar_select] * 2, 6, bar_max[bar_select] * 2 + 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_max[bar_select] * 2, WHITE);
  }
  if (select_ch == 7) {  // save
    display.setCursor(0, 30);
    display.print("S");
    display.setCursor(0, 39);
    display.print("A");
    display.setCursor(0, 48);
    display.print("V");
    display.setCursor(0, 57);
    display.print("E");
  } else if (select_ch == 8) {  // load
    display.setCursor(0, 30);
    display.print("L");
    display.setCursor(0, 39);
    display.print("O");
    display.setCursor(0, 48);
    display.print("A");
    display.setCursor(0, 57);
    display.print("D");
  } else if (select_ch == 9) {  // factory rest
    display.setCursor(0, 30);
    display.print("R");
    display.setCursor(0, 39);
    display.print("S");
    display.setCursor(0, 48);
    display.print("E");
    display.setCursor(0, 57);
    display.print("T");
  } else if (select_ch == 10) {  // MUTE ALL
    display.setCursor(0, 30);
    display.print("M");
    display.setCursor(0, 39);
    display.print("U");
    display.setCursor(0, 48);
    display.print("T");
    display.setCursor(0, 57);
    display.print("E");
  } else if (select_ch == 11) {  // TEMPO
    display.setCursor(0, 30);
    display.print("T");
    display.setCursor(0, 39);
    display.print("E");
    display.setCursor(0, 48);
    display.print("M");
    display.setCursor(0, 57);
    display.print("P");
  } else if (select_ch == 12) {  // NEW RANDOM SEQUENCE SELECT MODE
    display.setCursor(0, 30);
    display.print("R");
    display.setCursor(0, 39);
    display.print("A");
    display.setCursor(0, 48);
    display.print("N");
    display.setCursor(0, 57);
    display.print("D");
  }

  // draw select triangle
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
    // ToDo: Add Probability / Swing indicator triangle
  }

  // draw step dot
  for (k = 0; k <= 5; k++) {          // k = 1~6ch
    for (j = 0; j < limit[k]; j++) {  // j = steps
      // Ensure that the index is within bounds
      int x_pos = x16[j % 16] + graph_x[k];
      int y_pos = y16[j % 16] + graph_y[k];
      if (x_pos < 128 && y_pos < 64) {  // Check if in display
        display.drawPixel(x_pos, y_pos, WHITE);
      }
    }
  }

  //draw hits line : 2~16hits
  for (k = 0; k <= 5; k++) {  //ch count
    buf_count = 0;
    for (m = 0; m < 16; m++) {
      if (offset_buf[k][m] == 1) {
        int x_pos = x16[m] + graph_x[k];
        int y_pos = y16[m] + graph_y[k];
        if (x_pos < 128 && y_pos < 64) {  // Only store coordinates within bounds
          line_xbuf[buf_count] = x_pos;
          line_ybuf[buf_count] = y_pos;
          buf_count++;
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

  //draw hits line : 1hits
  for (k = 0; k <= 5; k++) {  // Channel count
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


  //draw play step circle
  // This function draws outside the display bounds!
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


  /*
  //draw play step circle within display bounds
  // here the top 3 channels are not displaying the dot anymore
  for (k = 0; k <= 5; k++) {  // Channel count
    if (mute[k] == 0) {       // Only draw if not muted
      int x = x16[playing_step[k]] + graph_x[k];
      int y = y16[playing_step[k]] + graph_y[k];
      if (x - 3 >= 0 && x + 3 < 128 && y - 3 >= 0 && y + 3 < 64) {  // Check for both non-filled and filled circles max radius
        if (offset_buf[k][playing_step[k]] == 0) {
          display.drawCircle(x, y, 2, WHITE);
        }
        if (offset_buf[k][playing_step[k]] == 1) {
          display.fillCircle(x, y, 3, WHITE);
        }
      }
    }
  }

*/


  //write hit and offset values for H > 9 to 16 hits
  for (k = 0; k <= 5; k++) {  // Channel count
    if (hits[k] > 9) {
      int x_base = 7 + graph_x[k];
      int y_base_hit = 8 + graph_y[k];
      int y_base_offset = 17 + graph_y[k];
      if (x_base < 120 && y_base_hit < 64 && y_base_offset < 64) {  // Ensure text starts within bounds
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

  // display the buffer on the display
  display.display();
}
