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

// Use SimpleRotary
#include <SimpleRotary.h>

// Define pins for the rotary encoder.
int pinA = 3;       // CLK
int pinB = 2;       // DT
int buttonPin = 4;  // SW

// Create a SimpleRotary object.
SimpleRotary rotary(pinA, pinB, buttonPin);

//Oled setting
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define ENCODER_COUNT_PER_CLICK 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//rotery encoder
Encoder myEnc(3, 2);  //use 3pin 2pin
int oldPosition = -999;
int newPosition = -999;
int i = 0;
bool flr = 1;  //first loop run -> no encU wanted

// internal clock not yet implemented
byte bpm = 120, lastbpm = 120, ledState = LOW;

// Configuration data structure for each save state slot
struct SlotConfiguration {
  byte hits[6];
  byte offset[6];
  byte mute[6];
  byte limit[6];
};

// Default patterns for five slots (for factoryRest)
// put in RAM?
SlotConfiguration defaultSlots[5] = {
  { { 3, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { false, true, true, true, true, true }, { 8, 8, 8, 8, 8, 8 } },        // Bossa Nova
  { { 5, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { false, true, true, true, true, true }, { 12, 12, 12, 12, 12, 12 } },  // Son Clave
  { { 5, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { false, true, true, true, true, true }, { 12, 12, 12, 12, 12, 12 } },  // Rumba Clave
  { { 3, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { false, true, true, true, true, true }, { 8, 8, 8, 8, 8, 8 } },        // Tresillo
  { { 7, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { false, true, true, true, true, true }, { 16, 16, 16, 16, 16, 16 } }   // Cumbia
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
byte select_menu = 0;   //0=CH,1=HIT,2=OFFSET,3=LIMIT,4=MUTE,5=RESET,
byte select_ch = 0;     //0~5 = each channel -1 , 6 = random mode
bool disp_refresh = 0;  //0=not refresh display , 1= refresh display , countermeasure of display refresh bussy

const byte graph_x[6] = { 0, 40, 80, 15, 55, 95 };  //each chanel display offset
const byte graph_y[6] = { 1, 1, 1, 33, 33, 33 };    //each chanel display offset -> +1 vs. original

byte line_xbuf[17];
byte line_ybuf[17];

const byte x16[16] = { 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1, 0, 1, 4, 9 };
const byte y16[16] = { 0, 1, 4, 9, 15, 21, 26, 29, 30, 29, 26, 21, 15, 9, 4, 1 };

//random assign
byte hit_occ[6] = { 5, 1, 20, 20, 40, 80 };   //random change rate of occurrence
byte off_occ[6] = { 1, 3, 20, 30, 40, 20 };   //random change rate of occurrence
byte mute_occ[6] = { 0, 2, 20, 20, 20, 20 };  //random change rate of occurrence
byte hit_rng_max[6] = { 5, 6, 8, 4, 4, 6 };   //random change range of max
byte hit_rng_min[6] = { 3, 2, 2, 1, 1, 1 };   //random change range of min

byte bar_now = 1;
constexpr byte bar_max[4] = { 2, 4, 8, 16 };
byte bar_select = 1;  //selected bar
byte step_cnt = 0;

//#define MAX_STEPS 16  // Adjust the value based on your actual maximum steps
constexpr byte MAX_STEPS = 16;

#define NUM_MEMORY_SLOTS 5      // Number of memory slots for saving patterns
#define EEPROM_START_ADDRESS 0  // Starting address in EEPROM to save data
#define CONFIG_SIZE (6 * 4)     // Size of configuration data to be saved for each slot (hits, offset, mute, limit)

SlotConfiguration memorySlots[NUM_MEMORY_SLOTS];  // Array to store configurations for each slot

#define ButtonPin 12

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
  // TODO: Add upside down switch
  //display.setRotation(2);
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
  if (select_ch == 9 && select_menu == 1) {  // reset whith encoder rotation
    //factoryReset();
    select_menu = 0;
  }
  if (select_ch == 10 && select_menu == 1) {  // check mute status and toggle mute / unmute with encoder rotation
    //muteToggle();
    select_menu = 0;
  }
  if (select_ch == 10 && select_menu == 2) {  // check mute status and toggle mute / unmute with encoder rotation
    //performanceMute();
    disp_refresh = debug;
    //select_menu = 0;
  }
  if (select_ch == 11 && select_menu == 2) {  // modes only having a button
    if (bpm >= 180) {
      bpm = 180;
    }
    if (bpm <= 60) {
      bpm = 60;
    }
    //adjustTempo();
    disp_refresh = 1;
  }
  if (select_ch == 12 && select_menu == 1) {  // check mute status and toggle mute / unmute with encoder rotation
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
        //if (select_ch >= 13) {
        //            select_ch = 12;
        //}
      if (select_ch >= 9) {
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
        // reintroduce the possibility to dial in change duration for random mode
        else if (select_ch == 6) {  // random mode - aber ohne die zweite Bedingung "&& sw == 0"
          bar_select++;
          if (bar_select >= 4) {  //Begrenzer nach oben
            bar_select = 3;       //Bei Überschreitung Rücksetzung auf den Maximalwert
          }
        }
        break;

      case 2:  //offset
        offset[select_ch]--;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 15;  //geaendert von 0 auf 15, d. h. Schleife bei Ueberlauf
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

      case 5:  //reset
        for (k = 0; k <= 5; k++) {
          playing_step[k] = 0;
        }
        break;

      case 6:  //random advance
        if (select_ch >= 2) {
          select_ch = 0;
        }
        Random_change_one();
        disp_refresh = 1;
        break;
    }
  }

  if (old_encD == 0 && encD == 1) {
    switch (select_menu) {
      case 0:  //select chanel
        select_ch--;
        if (select_ch >= 7) {  //Begrenzer nach unten durch Rücklauf "unter Null", d. h. auf 255 wegen vorzeichenloser Byte-Definition, Pendant zu Z 223
          select_ch = 7;
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
          if (bar_select >= 4) {  //Begrenzer "nach unten" durch Rücklauf auf 255 wegen vorzeichenloser Byte-Definition
            bar_select = 0;       //Bei Unterschreitung Rücksetzung auf den Minimalwert, Pendant zu Z 238
          }
        }
        break;

      case 2:  //offset
        offset[select_ch]++;
        if (offset[select_ch] >= 16) {
          offset[select_ch] = 0;
        }
        break;

      case 3:  //limit for now only up direction since there is a bug with the playing indicatior dot hiding
        limit[select_ch]--;
        if (limit[select_ch] >= 17) {
          limit[select_ch] = 0;
        }

        break;


      case 4:  //mute
        mute[select_ch] = !mute[select_ch];
        break;

      case 5:  //reset
        for (k = 0; k <= 5; k++) {
          playing_step[k] = 0;
        }
        break;

      case 6:  //random advance
        Random_change();
        disp_refresh = 1;
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
  rst_in = FastGPIO::Pin<11>::isInputHigh();  //external reset
  if (old_rst_in == 0 && rst_in == 1) {
    for (k = 0; k <= 5; k++) {
      playing_step[k] = 0;
      disp_refresh = 1;
    }
  }
  trg_in = FastGPIO::Pin<13>::isInputHigh();  //external trigger
  // refresh dirsplay if there is no clock for 8 sec ? maybe internal clock here ?
  if (old_trg_in == 0 && trg_in == 0 && gate_timer + 8000 <= millis()) {
    debug = 1;
    disp_refresh = 1;
  } else if (old_trg_in == 0 && trg_in == 1) {
    gate_timer = millis();
    FastGPIO::Pin<4>::setOutput(1);
    debug = 0;
    for (i = 0; i <= 5; i++) {
      playing_step[i]++;
      if (playing_step[i] >= limit[i]) {
        playing_step[i] = 0;  // step limit is reached
      }
    }
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
    display.setCursor(20, 29);
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
    display.setCursor(29, 29);
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
  display.println(F("Config saved to Slot "));
  display.println(1);  // Display the slot number
  display.display();
  delay(300);
  display.clearDisplay();
}

// reset the whole saved slots and put in traditional euclid patterns
void initializeDefaultRhythms() {
  for (int slot = 0; slot < 5; slot++) {
    saveDefaultsToEEPROM(slot, defaultSlots[slot]);
  }
}

void saveDefaultsToEEPROM(int slot, SlotConfiguration config) {
  int address = EEPROM_START_ADDRESS + (slot * sizeof(SlotConfiguration));
  EEPROM.put(address, config);
}

// Random change function for all channels at once
void Random_change() {
  unsigned long seed = analogRead(A0);  //  seed value
  randomSeed(seed);                     // initialize the PRNG with the seed value

  for (k = 1; k <= 5; k++) {

    if (hit_occ[k] >= random(1, 100)) {  //hit random change
      hits[k] = random(hit_rng_min[k], hit_rng_max[k]);
    }

    if (off_occ[k] >= random(1, 100)) {  //hit random change
      offset[k] = random(0, 16);
    }

    if (mute_occ[k] >= random(1, 100)) {  //hit random change
      mute[k] = 1;
    } else if (mute_occ[k] < random(1, 100)) {  //hit random change
      mute[k] = 0;
    }
  }
}

// random change function for one channel at a time only!
void Random_change_one() {

  unsigned long seed = analogRead(A0);  //  seed value
  randomSeed(seed);                     // initialize the PRNG with the seed value

  if (random(100) < hit_occ[select_ch]) {
    hits[select_ch] = random(hit_rng_min[select_ch], hit_rng_max[select_ch] + 1);
  }

  if (off_occ[select_ch] >= random(1, 100)) {  //hit random change
    offset[select_ch] = random(0, 16);
  }

  if (mute_occ[select_ch] >= random(1, 100)) {  //hit random change
    mute[select_ch] = 1;
  } else if (mute_occ[select_ch] < random(1, 100)) {  //hit random change
    mute[select_ch] = 0;
  }
}

// TODO:
// generate five sets of hits, offsets, and mutes and store in array
int randomize_array[5][3][6];
void Randomize() {
  for (int i = 0; i < 5; i++) {
    Random_change();
    for (int j = 0; j < 3; j++) {
      if (j == 0) {
        for (int k = 0; k < 6; k++) {
          randomize_array[i][j][k] = hits[k];
        }
      } else if (j == 1) {
        for (int k = 0; k < 6; k++) {
          randomize_array[i][j][k] = offset[k];
        }
      } else if (j == 2) {
        for (int k = 0; k < 6; k++) {
          randomize_array[i][j][k] = mute[k];
        }
      }
    }
  }
}

// mute all channels one at a time until all channels are muted and while the button is pressed and when rotated anticlockwise
// unmute all channels one at a time until all channels are unmuted and while the button is pressed and when rotated clockwise
// not working as expected

void performanceMute() {
  if (myEnc.read() < 0) {
    for (int i = 0; i < 6; i++) {
      mute[i] = 1;
    }
  }
  // unmute all channels
  if (myEnc.read() > 0) {
    for (int i = 0; i < 6; i++) {
      mute[i] = 0;
    }
  }
}

void muteOne() {
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
  // unmute all channels
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

void OLED_display() {
  display.clearDisplay();
  //-------------------------euclidean oled display------------------
  //draw setting menu
  display.setCursor(120, 0);
  // if select channel is not random mode (6) OR! not in modes higher than 7 (factory reset)
  if (select_ch != 6 && select_ch <= 6) {  // not random mode
    display.print(select_ch + 1);
  }
  if (select_ch == 6) {  // R for RANDOM
    display.print("R");
  } else if (select_ch == 7) {  // S for SAVE
    display.print("S");
  } else if (select_ch == 8) {  // L for LOAD
    display.print("L");
  } else if (select_ch == 9) {  // Factory reset
    display.print("F");
  } else if (select_ch == 10) {  // MUTE
    display.print("M");
  } else if (select_ch == 11) {  // TEMPO
    display.print("T");
  } else if (select_ch == 12) {  // NEW RANDOM
    display.print("X");
  }
  display.setCursor(120, 9);
  
  
  if (select_ch != 6 && select_ch <= 6) {  // not random mode
    display.print("H");                    // Hits
  } else if (select_ch == 6) {             //
    display.print("O");
  } else if (select_ch == 10) {  // Mute
    display.setCursor(120, 9);
    //display.drawRect(120, 9, 8, 8, WHITE);
    //display.println(F("<>"));
  }
  
  display.setCursor(120, 18);
  if (select_ch != 6 && select_ch <= 6) {  // not random mode and no config modes
    display.print("O");                    // Offset
    display.setCursor(0, 29);
    display.print("L");  // Loop
    display.setCursor(0, 38);
    display.print("M");  // Mute
    display.setCursor(0, 47);
    display.print("R");  // Reset
    display.setCursor(0, 56);
    display.print("X");  // Advance random mode (current channel)
  }

  //random count square
  if (select_ch == 6) {  //random mode
    display.drawRect(1, 62 - bar_max[bar_select] * 2, 6, bar_max[bar_select] * 2 + 2, WHITE);
    display.fillRect(1, 64 - bar_now * 2, 6, bar_max[bar_select] * 2, WHITE);
  }
  // write mode text
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
  } else if (select_ch == 9) {  // reset
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

  //draw select triangle
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
  for (k = 0; k <= 5; k++) {  //ch count
    buf_count = 0;
    if (hits[k] == 1) {
      display.drawLine(15 + graph_x[k], 15 + graph_y[k], x16[offset[k]] + graph_x[k], y16[offset[k]] + graph_y[k], WHITE);
    }
  }

  //draw play step circle
  for (k = 0; k <= 5; k++) {  //ch count
    if (mute[k] == 0) {       //mute on = no display circle
      if (offset_buf[k][playing_step[k]] == 0) {
        display.drawCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 2, WHITE);
      }
      if (offset_buf[k][playing_step[k]] == 1) {
        display.fillCircle(x16[playing_step[k]] + graph_x[k], y16[playing_step[k]] + graph_y[k], 3, WHITE);
      }
    }
  }

  //write hit and offset values for H > 6 -> 9 to 16 hits
  for (k = 0; k <= 5; k++) {  //ch count
    if (hits[k] > 6) {
      display.setCursor(7 + graph_x[k], 8 + graph_y[k]);
      display.print("h");
      display.print(hits[k]);
      display.setCursor(7 + graph_x[k], 17 + graph_y[k]);
      display.print("o");
      if (offset[k] == 0) {
        display.print(offset[k]);
      } else {
        display.print(16 - offset[k]);
      }
    }
  }

  // display the buffer on the display
  display.display();
}
