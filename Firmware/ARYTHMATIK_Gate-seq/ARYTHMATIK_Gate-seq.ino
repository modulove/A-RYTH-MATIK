/**
 * @file ARYTHMATIK_Gate-seq.ino
 * @author Modulove & friends (https://github.com/modulove/)
 * @brief 6CH Eurorack (HAGIWO) Rhythm Generator with SSD1306 0.96 OLED
 * @version 0.1
 * @date 2024-04-30
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
 * Timing: CLK and RST are handled in the pin-change interrupt (step count, AUTO
 * pattern changes, outputs and channel LEDs), so the gates follow the clock exactly
 * while the OLED is redrawn. The loop does the UI and redraws the OLED after a step.
 *
 */

// Flag for enabling debug print to serial monitoring output.
// Note: this affects performance and locks LED 4 & 5 on HIGH.
// #define DEBUG

// Flag for reversing the encoder direction.
// #define ENCODER_REVERSED

// Flag for using the panel upside down
// #define PANEL_USD

int debug = 0; // 1 =on 0 =off

//INTERNAL CLOCK Setup
unsigned long startMillis;  //some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long period = 1000;  //the value is a number of milliseconds
const byte ledPin = 4;    //using the built in LED

int tempo = 120;
int lasttempo = 120;

int ledState = LOW;           
long previousMillis = 0;

long interval = 1000;

//EEPROM
#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM)
#include <util/atomic.h>

//Encoder setting
#define  ENCODER_OPTIMIZE_INTERRUPTS //エンコーダノイズ対策
#include <Encoder.h>

// encoder & direction
#ifdef ENCODER_REVERSED
Encoder myEnc(2, 3);  // 2pin, 3pin is default
#else
Encoder myEnc(3, 2);
#endif

int oldPosition  = -999;
int newPosition = -999;
byte enc = 96; //選択中のエンコーダ。初回起動時はMANUAL表示
byte enc_max = 105; //マニュアルモードではmax=99(16*6ch+option3+mute6)。AUTOでは11(MANUAL,genre,fillin,repeat,sw+mute6)
unsigned int enc_bit = 0x00;//

//ボタン設定
byte button = 0;//0=OFF,1=ON
byte old_button = 0;//チャタリング対策
byte button_on = 0;//チャタリング判定後のボタン状態。0=OFF,1=ON

//OLED settings
#include<Wire.h>
#include<Adafruit_GFX.h>
#include<Adafruit_SSD1306.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128//128が推奨表示
#define SCREEN_HEIGHT 64//64が推奨表示 

//----------------変数の定義---------------------------
// volatile: shared with the CLK/RST interrupt (pinChange()). The loop changes the patterns
// only inside ATOMIC_BLOCK (16-bit values; the interrupt also replaces them in AUTO mode).
volatile byte step_count = 0; //clock in毎に1繰り上がり。17になると1に戻る。増えた時にOUTPUTをONする。
volatile byte clock_in = 0 ; //外部クロックの入力状態。H=1,L=0 (set by the interrupt)

volatile unsigned int ch1_step = 0x8888; //テスト用
volatile byte CH1_mute = 0 ;//0=ミュートしない、1=ミュートする

volatile unsigned int ch2_step = 0x0808; //テスト用
volatile byte CH2_mute = 0 ;//0=ミュートしない、1=ミュートする

volatile unsigned int ch3_step = 0xCCCC; //テスト用
volatile byte CH3_mute = 0 ;//0=ミュートしない、1=ミュートする

volatile unsigned int ch4_step = 0x2222; //テスト用
volatile byte CH4_mute = 0 ;//0=ミュートしない、1=ミュートする

volatile unsigned int ch5_step = 0xFFFF; //テスト用
volatile byte CH5_mute = 0 ;//0=ミュートしない、1=ミュートする

volatile unsigned int ch6_step = 0x0000; //テスト用
volatile byte CH6_mute = 0 ;//0=ミュートしない、1=ミュートする

// ディスプレイ変数の宣言
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//オプション
volatile byte mode = 0;//0=MANUAL,1=AUTO
byte count_reset = 0;//1でcountが0になる
byte save = 0;//1でseve実行しすぐに0に戻る

volatile byte genre = 0; //パターンジャンル0=techno,1=dub,2=house,3=half
volatile byte repeat = 2; //0=4times,1=8times,2=16times,3=32times,4=eternal
volatile byte fillin = 1; //0=OFF,1=ON
volatile byte sw = 0;//0=2,1=4,2=8,3=16,4=eternal

//AUTOモード (the counters advance in the CLK interrupt)
volatile int repeat_max = 4;//repeat_doneこの値に達したらフィルイン
volatile int repeat_done = 0; //stepが16になったら繰り上がる。
byte test = 0; //開発用
volatile byte change_bnk1 = 1;//プリセット1
volatile byte change_bnk2 = 1;//プリセット2
volatile byte change_bnk3 = 1;//プリセット3（任意に増やしてOK）
volatile byte change_bnk4 = 1;
volatile byte sw_max = 1;//se_doneがこの値に達したらパターン変更（ランダム）
volatile byte sw_done = 0;//repeat_maxになったら繰り上がる


//preset step pattern
//[パターンの数][CHの数*2。頭6個が通常時、後ろ6個がfillin]
//16信数はドラムパターンを示す。

//bnk1 TECHNO
const static word bnk1_ptn[8][12] PROGMEM = {
 { 0x8888,  0x0808, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xDDDD, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xFFFF, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0808, 0xCCCC, 0x2222, 0x1000, 0x0022,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0808, 0x4545, 0x2222, 0x1000, 0x0022,
   0x88AF, 0x0808, 0x4545, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022,
   0x88AF, 0x0808, 0xFFFF, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0809, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x0000, 0x0809, 0xDDDD, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x0000, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022
 },
 { 0x8888,  0x0802, 0xDDDD, 0x2222, 0x1000, 0x0022,
   0x8896, 0x0869, 0xDDDD, 0x2222, 0x1000, 0x0022
 }
};

//bunk2 DUBTECHNO
const static word bnk2_ptn[8][12] PROGMEM = {
 { 0x8888,  0x0808, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0809, 0xDDDD, 0x2222, 0x1240, 0x0022
 },
 { 0x8888,  0x0808, 0xFFFF, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022
 },
 { 0x8889,  0x0808, 0xCCCC, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0000, 0xCCCC, 0x2222, 0x1240, 0x0022
 },
 { 0x8889,  0x0808, 0x4545, 0x2222, 0x1240, 0x0022,
   0x8888, 0x0809, 0x4545, 0x2222, 0x1240, 0x0022
 },
 { 0x888C,  0x0808, 0xFFFF, 0x2222, 0x1240, 0x0022,
   0x8888, 0x8888, 0xFFFF, 0x2222, 0x1240, 0x0022
 },
 { 0x888C,  0x0809, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x999F, 0x0000, 0xDDDD, 0x2222, 0x1240, 0x0022
 },
 { 0x0000,  0x0849, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0849, 0xDDDD, 0x2222, 0x1000, 0x0022
 },
 { 0x0000,  0x0802, 0xDDDD, 0x2222, 0x1240, 0x0022,
   0x000A, 0x0802, 0xDDDD, 0x2222, 0x1000, 0x0022
 }
};

//bunk3 HOUSE
const static word bnk3_ptn[8][12]PROGMEM = {
 { 0x8888, 0x0808, 0x2222, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2222, 0x0000, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0x2323, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2323, 0x0000, 0x0040, 0x0101
 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101
 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112
 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x4112,
   0x88AF, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x4112
 }
};

//bank4 HALFSTEP
const static word bnk4_ptn[8][12]PROGMEM = {
 { 0x8888, 0x0808, 0x0000, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2222, 0x0040, 0x0000, 0x0101
 },
 { 0x888A, 0x0808, 0x2323, 0x0000, 0x0040, 0x0101,
   0x8888, 0x88AF, 0x2323, 0x0000, 0x0040, 0x0101
 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x0101,
   0x8888, 0x88AF, 0xCCCC, 0x2222, 0x0040, 0x0101
 },
 { 0x8888, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x0101
 },
 { 0x888A, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112,
   0x0000, 0x0808, 0xFFFF, 0x2222, 0x0040, 0x4112
 },
 { 0x8888, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x4112,
   0x88AF, 0x0808, 0xCCCC, 0x2222, 0x0040, 0x4112
 }
};

void change_step();
void fillin_step();

//--------------CLK / RST interrupt----------------------------------
// CLK (D13 = PB5) and RST (D11 = PB3) are handled in the pin-change interrupt: step count,
// AUTO pattern changes and the outputs happen at the edge. They used to be polled by the
// loop, and an OLED redraw (45-80 ms) kept the gates high until the loop ran again.
#define CLK_PB_BIT 5  // D13, PCINT5
#define RST_PB_BIT 3  // D11, PCINT3
volatile byte displayFlag = 0;  // a step or a reset happened: the loop redraws the OLED
byte displayRetry = 0;          // the display guard skipped the last frame: redraw after the pause
byte pinbLast = 0;              // PINB at the previous pinChange() (CLK/RST low at start)

// OUT1..OUT6 = D5, D6, D7, D8, D9, D10 (PD5..PD7, PB0..PB2); channel LEDs CH1..CH6 =
// A0, A1, A2 (PC0..PC2), D0, D1 (PD0, PD1), A3 (PC3). Bit i of m = channel i + 1.
// Port-wide read-modify-write: call with interrupts off (interrupt or ATOMIC_BLOCK).
static void writeOutputs(byte m) {
 PORTD = (PORTD & 0x1C) | ((m & 0x07) << 5) | ((m >> 3) & 0x03);
 PORTB = (PORTB & 0xF8) | ((m >> 3) & 0x07);
 PORTC = (PORTC & 0xF0) | (m & 0x07) | ((m >> 2) & 0x08);
}

// The output rule of the old loop: an output (and its LED) follows the clock level when the
// channel's bit for the current step is set and the channel is not muted. Step 0 (after RST,
// before the next clock) plays nothing. Call with interrupts off.
static void updateOutputs() {
 byte m = 0;
 byte s = step_count;
 if (clock_in && s >= 1 && s <= 16) {
   unsigned int b = 0x8000U >> (s - 1);  // bit 16 - step_count
   if ((ch1_step & b) && CH1_mute == 0) m |= 0x01;
   if ((ch2_step & b) && CH2_mute == 0) m |= 0x02;
   if ((ch3_step & b) && CH3_mute == 0) m |= 0x04;
   if ((ch4_step & b) && CH4_mute == 0) m |= 0x08;
   if ((ch5_step & b) && CH5_mute == 0) m |= 0x10;
   if ((ch6_step & b) && CH6_mute == 0) m |= 0x20;
 }
 writeOutputs(m);
}

// AUTO limits from the menu tokens (the loop also does this on every pass, for the display).
// Call with interrupts off.
static void autoLimits() {
 switch (repeat) {
   case 0:
     repeat_max = 4;
     break;

   case 1:
     repeat_max = 8;
     break;

   case 2:
     repeat_max = 16;
     break;

   case 3:
     repeat_max = 32;
     break;

   case 4:
     repeat_max = 10000;//ETERNAL
     break;
 }

 switch (sw) {
   case 0:
     sw_max = 2;
     break;

   case 1:
     sw_max = 4;
     break;

   case 2:
     sw_max = 8;
     break;

   case 3:
     sw_max = 16;
     break;

   case 4:
     sw_max = 255;//ETERNAL
     break;
 }
}

// Same order as the old loop pass: clock edge, RST edge, wrap at 17 (AUTO repeat / fill-in /
// pattern switch), switch counter wrap, outputs.
static void pinChange() {
 byte b = PINB;
 byte rise = b & ~pinbLast;
 pinbLast = b;
 clock_in = (b >> CLK_PB_BIT) & 1;

 //--------------外部クロック入力検出,カウント----------------
 if (rise & _BV(CLK_PB_BIT)) {
   step_count++;
   displayFlag = 1;  // OLEDの更新はクロック入ったタイミングのみ
 }

 if (rise & _BV(RST_PB_BIT)) {
   step_count = 0;
   displayFlag = 1;
 }

 if (step_count >= 17) {
   step_count = 1;
   autoLimits();

   if (mode == 1) {
     repeat_done++;

     if ( fillin == 1 && repeat_done == repeat_max - 1) {
       fillin_step();
     }

     else if (repeat_done >= repeat_max) {
       sw_done++;
       repeat_done = 0;
       change_step();
     }
   }

   if (sw_done >= sw_max) {
     sw_done = 0;
   }
 }

 //--------------出力----------------------------------
 updateOutputs();
}

ISR(PCINT0_vect) {
 pinChange();
}

// Saved pattern, EEPROM 1..12 (high byte first).
static void loadSteps() {
 unsigned int s[6];
 for (byte i = 0; i < 6; i++) {
   s[i] = word(EEPROM.read(1 + i * 2), EEPROM.read(2 + i * 2));
 }
 ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
   ch1_step = s[0];
   ch2_step = s[1];
   ch3_step = s[2];
   ch4_step = s[3];
   ch5_step = s[4];
   ch6_step = s[5];
 }
}


void setup() {
 // ディスプレイの初期化
 i2cGuardBegin();  // free a stuck I2C bus, I2C calls give up after 25 ms
 display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
 #ifdef PANEL_USD
 display.setRotation(2);  // 180 degree rotation for upside-down use
 #else
 display.setRotation(0);  // Normal orientation
 #endif
 pinMode(11, INPUT); //RST
 pinMode(12, INPUT_PULLUP); //BUTTON
 pinMode(5, OUTPUT); //CH1
 pinMode(6, OUTPUT); //CH2
 pinMode(7, OUTPUT); //CH3
 pinMode(8, OUTPUT); //CH4
 pinMode(9, OUTPUT); //CH5
 pinMode(10, OUTPUT); //CH6
 pinMode(14, OUTPUT); //CH1 LED (DIGITAL)
 pinMode(15, OUTPUT); //CH2 LED (DIGITAL)
 pinMode(16, OUTPUT); //CH3 LED (DIGITAL)
 pinMode(17, OUTPUT); //CH? LED (DIGITAL)
 pinMode(0, OUTPUT); //CH? LED (DIGITAL)
 pinMode(1, OUTPUT); //CH? LED (DIGITAL) 
 pinMode(13, INPUT); //Trigger

 //保存データの読み出し
 loadSteps();

 //開発用通信設定
 //  Serial.begin(9600);

 randomSeed(chipRandomSeed());  // AUTO mode picked the same "random" patterns after every power-up

 // CLK/RST pin-change interrupt. pinbLast starts with CLK and RST low, as the first loop pass
 // did, so a clock that is already high counts as a rising edge (step 1).
 ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
   PCMSK0 |= _BV(CLK_PB_BIT) | _BV(RST_PB_BIT);
   PCIFR = _BV(PCIF0);
   PCICR |= _BV(PCIE0);
   pinChange();
 }

 OLED_display();
}

void loop() {

 old_button = button;

 //-----------モード判定----------------


 //MANUALモード
 if (mode == 0) {
   enc_max = 105;
 }

 //AUTOモード
 else if (mode == 1) {
   enc_max = 11;
 }

 //-----------ロータリーエンコーダ読み取り----------------
 newPosition = myEnc.read();

 if ( newPosition + 2  < oldPosition ) {//左回し
   oldPosition = newPosition;
   enc = enc - 1;
 }

 else if ( newPosition - 2   > oldPosition ) {//右回し
   oldPosition = newPosition;
   enc = enc + 1;
 }

 if (enc <= 0) {
   enc = enc_max;//選択が各モードの最小値に達したら、最大値に戻す
 }
 else if ( enc > enc_max) {
   enc = 1;//選択が各モードの最大値に達したら、最小値に戻す
 }

 if (mode == 0) {//選択中のパターンのビット管理
   enc_bit = 0;
   bitSet(enc_bit, abs(enc % 16 - 16));
   if (abs(enc % 16 - 16) == 16) {
     bitSet(enc_bit, 0);
   }
 }

 //----------BUTTON読み取り----------------

 button = 1 - digitalRead(12); //input_pullupのため"1-"

 if (old_button == 0 && button == 1) {//0→1の時に1回だけpush判定
   button_on = 1;

 }
 else {
   button_on = 0;
 }

 //-------------------MANUALモード-------------------------
 if (mode == 0) {
   if (button_on == 1) {//任意のステップを選択中にボタンを押すと、ゲートのON/OFFを切り替える
     if ( enc <= 96 ) {
       ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {  // the CLK interrupt reads the patterns
         if ( enc <= 16) {
           ch1_step = ch1_step ^ enc_bit;
         }
         else if ( enc <= 32 && enc > 16) {
           ch2_step = ch2_step ^ enc_bit;
         }
         else if ( enc <= 48 && enc > 32) {
           ch3_step = ch3_step ^ enc_bit;
         }
         else if ( enc <= 64 && enc > 48) {
           ch4_step = ch4_step ^ enc_bit;
         }
         else if ( enc <= 80 && enc > 64) {
           ch5_step = ch5_step ^ enc_bit;
         }
         else if ( enc <= 96 && enc > 80) {
           ch6_step = ch6_step ^ enc_bit;
         }
       }
     }

     //AUTO選択
     else if ( enc == 97 ) {
       ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {  // change_step() also runs in the CLK interrupt
         mode = 1;
         change_step();//AUTOモードのパターンに切り替え
       }
     }

     //RESET選択
     else if ( enc == 98 ) {
       step_count = 1;
     }

     //SAVE選択
     else if ( enc == 99 ) {
       save_data();//EEPROMにセーブ保存
       step_count = 1;
     }

     //MUTE設定
     else if ( enc == 100) {
       CH1_mute = !CH1_mute;
     }
     else if ( enc == 101) {
       CH2_mute = !CH2_mute;
     }
     else if ( enc == 102) {
       CH3_mute = !CH3_mute;
     }
     else if ( enc == 103) {
       CH4_mute = !CH4_mute;
     }
     else if ( enc == 104) {
       CH5_mute = !CH5_mute;
     }
     else if ( enc == 105) {
       CH6_mute = !CH6_mute;
     }

   }
 }

 //-------------------AUTOモード-------------------------
 else if (mode == 1) {
   if (button_on == 1) {
     //MANUAL選択
     if ( enc == 1 ) {
       mode = 0;//MANUAL呼び出し (from now on the CLK interrupt leaves the patterns alone)
       enc = 97;//AUTOからMANUALに戻ったときに、MANUALを選択状態にする。自由に変えてOK
       //保存データの読み出し
       loadSteps();
     }


     //genre選択
     else if ( enc == 2 ) {
       genre++;
       if (genre >= 4) {
         genre = 0;
       }
     }

     //fillin選択 0=OFF,1=ON
     else if ( enc == 3 ) {
       fillin = !fillin;
     }

     //repeat選択
     else if ( enc == 4 ) {
       repeat++;
       if (repeat >= 5) {//0=4times,1=8times,2=16times,3=32times,4=eternal
         repeat = 0;
       }
     }

     //switch選択
     else if ( enc == 5) {
       sw++;
       if (sw >= 5) {//0=2,1=4,2=8,3=16,4=eternal
         sw = 0;
       }
     }

     //MUTE設定
     else if ( enc == 6) {
       CH1_mute = !CH1_mute;
     }
     else if ( enc == 7) {
       CH2_mute = !CH2_mute;
     }
     else if ( enc == 8) {
       CH3_mute = !CH3_mute;
     }
     else if ( enc == 9) {
       CH4_mute = !CH4_mute;
     }
     else if ( enc == 10) {
       CH5_mute = !CH5_mute;
     }
     else if ( enc == 11) {
       CH6_mute = !CH6_mute;
     }

   }
 }
 //-------------AUTOモード処理---------------
 // Clock, reset, the wrap at step 17 and the outputs are in pinChange() (CLK/RST interrupt).
 ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
   autoLimits();  // limits for the display (the interrupt recomputes them at the wrap)
   if (sw_done >= sw_max) {
     sw_done = 0;
   }
   //--------------出力----------------------------------
   if (button_on == 1) {
     updateOutputs();  // a step toggle, mute, AUTO/MANUAL or RESET takes effect at once, as before
   }
 }

 //--------------OLED出力----------------------------------
 //注意：OLEDの更新はクロック入ったタイミングのみ。Arduinoのビジー状態を避けるため。
 // (a redraw no longer delays the outputs; displayRetry = the display guard skipped a frame)
 if (displayFlag || (displayRetry && !i2cGuardPaused())) {
   displayFlag = 0;
   OLED_display();
 }

 //  //開発用
 //    Serial.print(repeat_done);
 //    Serial.print(",");
 //    Serial.print(sw_max);
 //    Serial.println("");
}
//--------------OLED出力----------------------------------
void OLED_display() {
 // One consistent copy of the patterns for this frame (the CLK interrupt replaces them in AUTO
 // mode). These locals hide the volatile globals of the same name inside this function.
 unsigned int ch1_step, ch2_step, ch3_step, ch4_step, ch5_step, ch6_step;
 ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
   ch1_step = ::ch1_step;
   ch2_step = ::ch2_step;
   ch3_step = ::ch3_step;
   ch4_step = ::ch4_step;
   ch5_step = ::ch5_step;
   ch6_step = ::ch6_step;
 }

 display.clearDisplay();  // ディスプレイをクリア
 display.setTextSize(1);  // 出力する文字の大きさ
 display.setTextColor(WHITE);  // 出力する文字の色

 //CH1表示
 display.setCursor(0, 0);  // 文字の一番端の位置
 display.print("CH1");

 display.setCursor(30, 0);
 if (CH1_mute == 0) {
   if (bitRead(ch1_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch1_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }

 //CH2表示
 display.setCursor(0, 9);  // 文字の一番端の位置
 display.print("CH2");

 display.setCursor(30, 9);
 if (CH2_mute == 0) {
   if (bitRead(ch2_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch2_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
   if (bitRead(ch2_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }
 //CH3表示
 display.setCursor(0, 18);  // 文字の一番端の位置
 display.print("CH3");

 display.setCursor(30, 18);
 if (CH3_mute == 0) {
   if (bitRead(ch3_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch3_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
   if (bitRead(ch3_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }

 //CH4表示
 display.setCursor(0, 27);  // 文字の一番端の位置
 display.print("CH4");

 display.setCursor(30, 27);
 if (CH4_mute == 0) {
   if (bitRead(ch4_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch4_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
   if (bitRead(ch4_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }

 //CH5表示
 display.setCursor(0, 36);  // 文字の一番端の位置
 display.print("CH5");

 display.setCursor(30, 36);
 if (CH5_mute == 0) {
   if (bitRead(ch5_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch5_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
   if (bitRead(ch5_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }

 //CH6表示
 display.setCursor(0, 45);  // 文字の一番端の位置
 display.print("CH6");

 display.setCursor(30, 45);
 if (CH6_mute == 0) {
   if (bitRead(ch6_step, 15 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 14 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 13 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 12 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 11 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 10 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 9 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 8 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 7 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 6 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 5 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 4 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 3 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 2 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }

   if (bitRead(ch6_step, 1 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
   if (bitRead(ch6_step, 0 ) == 1) {
     display.print("*");
   }
   else {
     display.print("_");
   }
 }

 //-------------エンコード表示---------------------------------


 //MANUALモード
 if (mode == 0) {

   //選択中のステップ
   if ( enc <= 16) {
     display.drawRect(enc * 6 + 24, 0 , 6, 8, WHITE);
   }
   else if ( enc <= 32 && enc > 16) {
     display.drawRect((enc - 16) * 6 + 24, 9 , 6, 8, WHITE);
   }
   else if ( enc <= 48 && enc > 32) {
     display.drawRect((enc - 32) * 6 + 24, 18 , 6, 8, WHITE);
   }
   else if ( enc <= 64 && enc > 48) {
     display.drawRect((enc - 48) * 6 + 24, 27 , 6, 8, WHITE);
   }
   else if ( enc <= 80 && enc > 64) {
     display.drawRect((enc - 64) * 6 + 24, 36 , 6, 8, WHITE);
   }
   else if ( enc <= 96 && enc > 80) {
     display.drawRect((enc - 80) * 6 + 24, 45 , 6, 8, WHITE);
   }

   //オプション項目
   display.setCursor(0, 54);  // 文字の一番端の位置
   if (enc == 97) {
     display.setTextColor(BLACK, WHITE); // (BLACK, WHITE)は出力する文字の色を反転
     display.print("MNAL");
   }
   else {
     display.setTextColor(WHITE);
     display.print("MNAL");
   }

   display.setCursor(48, 54);

   if (enc == 98) {
     display.setTextColor(BLACK, WHITE);
     display.print("RESET");
   }
   else {
     display.setTextColor(WHITE);
     display.print("RESET");
   }

   display.setCursor(102, 54);

   if (enc == 99) {
     display.setTextColor(BLACK, WHITE);
     display.print("SAVE");
   }
   else {
     display.setTextColor(WHITE);
     display.print("SAVE");
   }

   if (enc == 100) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 0);
     display.print("CH1");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 0);
     display.print("CH1");
   }

   if (enc == 101) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 9);
     display.print("CH2");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 9);
     display.print("CH2");
   }

   if (enc == 102) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 18);
     display.print("CH3");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 18);
     display.print("CH3");
   }

   if (enc == 103) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 27);
     display.print("CH4");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 27);
     display.print("CH4");
   }

   if (enc == 104) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 36);
     display.print("CH5");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 36);
     display.print("CH5");
   }

   if (enc == 105) {
     display.setTextColor(BLACK, WHITE);
     display.setCursor(0, 45);
     display.print("CH6");
   }
   else {
     display.setTextColor(WHITE);
     display.setCursor(0, 45);
     display.print("CH6");
   }

 }

 //AUTOモード
 display.setCursor(0, 54);  // 文字の一番端の位置
 if (mode == 1) {
   if (enc <= 3 ) {
     if (enc == 1) {
       display.setTextColor(BLACK, WHITE);
       display.print("AUTO");
     }
     else {
       display.setTextColor(WHITE);
       display.print("AUTO");
     }

     display.setTextColor(WHITE);
     display.print("  ");

     if (enc == 2) {
       display.setTextColor(BLACK, WHITE);

       switch (genre) {//プリセットの選択
         case 0:
           display.print("TECHNO");//表示領域の都合上、6文字とする
           break;

         case 1:
           display.print("DUBTCN");
           break;

         case 2:
           display.print("HOUSE ");
           break;

         case 3:
           display.print("HALF ");
           break;
       }

     }
     else {
       display.setTextColor(WHITE);

       switch (genre) {
         case 0:
           display.print("TECHNO");
           break;

         case 1:
           display.print("DUBTCN");
           break;

         case 2:
           display.print("HOUSE ");
           break;
       }
     }

     display.setTextColor(WHITE);
     display.print("  ");

     if (enc == 3) {//Fillinの選択
       display.setTextColor(BLACK, WHITE);
       switch (fillin) {
         case 0:
           display.print("FilIN:N");
           break;

         case 1:
           display.print("FilIN:Y");
           break;
       }
     }
     else {
       display.setTextColor(WHITE);
       switch (fillin) {
         case 0:
           display.print("FilIN:N");
           break;

         case 1:
           display.print("FilIN:Y");
           break;
       }
     }

   }

   else if (enc >= 4 ) {

     if (enc == 4) {//リピートの選択
       display.setCursor(0, 54);  // 文字の一番端の位置
       display.setTextColor(BLACK, WHITE);
       display.print("REP:");
       display.print(repeat_done + 1);
       display.print("/");
       if (repeat <= 3) {
         display.print(repeat_max);
       }
       else if (repeat >= 4) {
         display.print("ET");
       }
     }

     else {
       display.setCursor(0, 54);
       display.setTextColor(WHITE);
       display.print("REP:");
       display.print(repeat_done + 1);
       display.print("/");
       if (repeat <= 3) {
         display.print(repeat_max);
       }
       else if (repeat >= 4) {
         display.print("ET");
       }
     }

     if (enc == 5) {//SWの選択
       display.setTextColor(BLACK, WHITE);
       display.setCursor(70, 54);
       display.print("SW:");
       display.print(sw_done + 1);
       display.print("/");
       if (sw <= 3) {
         display.print(sw_max);
       }
       else if (sw >= 4) {
         display.print("ET");
       }
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(70, 54);
       display.print("SW:");
       display.print(sw_done + 1);
       display.print("/");
       if (sw <= 3) {
         display.print(sw_max);
       }
       else if (sw >= 4) {
         display.print("ET");
       }
     }

     if (enc == 6) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 0);
       display.print("CH1");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 0);
       display.print("CH1");
     }

     if (enc == 7) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 9);
       display.print("CH2");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 9);
       display.print("CH2");
     }

     if (enc == 8) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 18);
       display.print("CH3");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 18);
       display.print("CH3");
     }

     if (enc == 9) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 27);
       display.print("CH4");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 27);
       display.print("CH4");
     }

     if (enc == 10) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 36);
       display.print("CH5");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 36);
       display.print("CH5");
     }

     if (enc == 11) {
       display.setTextColor(BLACK, WHITE);
       display.setCursor(0, 45);
       display.print("CH6");
     }
     else {
       display.setTextColor(WHITE);
       display.setCursor(0, 45);
       display.print("CH6");
     }

   }
 }


 //出力中のstep
 display.drawRect(step_count * 6 + 24, 0 , 6, 53, WHITE);
 display.setCursor(0, 54);  // 文字の一番端の位置

 // ディスプレイへの表示. After an I2C timeout the guard frees the bus, re-initialises the
 // display and skips frames for 1 s; the loop then redraws (displayRetry).
 displayRetry = !i2cGuardShow(display);
}

void save_data() {
 // Addresses 1..12, high byte first, written as one block: on the LGT8F one
 // flash page swap instead of one per changed byte (each freezes the chip ~46 ms).
 const unsigned int steps[6] = { ch1_step, ch2_step, ch3_step, ch4_step, ch5_step, ch6_step };
 uint8_t data[12];
 for (uint8_t i = 0; i < 6; i++) {
   data[i * 2] = highByte(steps[i]);
   data[i * 2 + 1] = lowByte(steps[i]);
 }
 EEPROM.put(1, data); //変更データをEEPROMに書き込み

}

void change_step() {
 //AUTOモードでSTEPを自動変更

 //bank1
 if (genre == 0) {
   if (sw_done >= sw_max) {//SWが指定値になったときのみ、プリセットからランダムにパターンを選択
     change_bnk1 = random(0, 7);//random maxはパターン上限と一致
   }
   ch1_step = pgm_read_word(&(bnk1_ptn[change_bnk1][0]));
   ch2_step = pgm_read_word(&(bnk1_ptn[change_bnk1][1]));
   ch3_step = pgm_read_word(&(bnk1_ptn[change_bnk1][2]));
   ch4_step = pgm_read_word(&(bnk1_ptn[change_bnk1][3]));
   ch5_step = pgm_read_word(&(bnk1_ptn[change_bnk1][4]));
   ch6_step = pgm_read_word(&(bnk1_ptn[change_bnk1][5]));
 }

 //bank2
 if (genre == 1) {
   if (sw_done >= sw_max) {//SWが指定値になったときのみ、プリセットからランダムにパターンを選択
     change_bnk2 = random(0, 4);//random maxはパターン上限と一致
   }
   ch1_step = pgm_read_word(&(bnk2_ptn[change_bnk2][0]));
   ch2_step = pgm_read_word(&(bnk2_ptn[change_bnk2][1]));
   ch3_step = pgm_read_word(&(bnk2_ptn[change_bnk2][2]));
   ch4_step = pgm_read_word(&(bnk2_ptn[change_bnk2][3]));
   ch5_step = pgm_read_word(&(bnk2_ptn[change_bnk2][4]));
   ch6_step = pgm_read_word(&(bnk2_ptn[change_bnk2][5]));
 }

 //bank3
 if (genre == 2) {
   if (sw_done >= sw_max) {//SWが指定値になったときのみ、プリセットからランダムにパターンを選択
     change_bnk3 = random(0, 4);//random maxはパターン上限と一致
   }
   ch1_step = pgm_read_word(&(bnk3_ptn[change_bnk3][0]));
   ch2_step = pgm_read_word(&(bnk3_ptn[change_bnk3][1]));
   ch3_step = pgm_read_word(&(bnk3_ptn[change_bnk3][2]));
   ch4_step = pgm_read_word(&(bnk3_ptn[change_bnk3][3]));
   ch5_step = pgm_read_word(&(bnk3_ptn[change_bnk3][4]));
   ch6_step = pgm_read_word(&(bnk3_ptn[change_bnk3][5]));
 }

 //bank4
 if (genre == 3) {
   if (sw_done >= sw_max) {//SWが指定値になったときのみ、プリセットからランダムにパターンを選択
     change_bnk4 = random(0, 4);//random maxはパターン上限と一致
   }
   ch1_step = pgm_read_word(&(bnk4_ptn[change_bnk4][0]));
   ch2_step = pgm_read_word(&(bnk4_ptn[change_bnk4][1]));
   ch3_step = pgm_read_word(&(bnk4_ptn[change_bnk4][2]));
   ch4_step = pgm_read_word(&(bnk4_ptn[change_bnk4][3]));
   ch5_step = pgm_read_word(&(bnk4_ptn[change_bnk4][4]));
   ch6_step = pgm_read_word(&(bnk4_ptn[change_bnk4][5]));
 } 

}

void fillin_step() {
  const uint16_t* current_bnk_ptn;

  // Determine which bnk_ptn array to use based on the genre
  switch (genre) {
    case 0:
      current_bnk_ptn = bnk1_ptn[change_bnk1];
      break;
    case 1:
      current_bnk_ptn = bnk2_ptn[change_bnk2];
      break;
    case 2:
      current_bnk_ptn = bnk3_ptn[change_bnk3];
      break;
    case 3:
      current_bnk_ptn = bnk4_ptn[change_bnk4];
      break;
    default:
      // Handle invalid genre
      return;
  }

  // Assign values using the determined bnk_ptn array
  ch1_step = pgm_read_word(&(current_bnk_ptn[6]));
  ch2_step = pgm_read_word(&(current_bnk_ptn[7]));
  ch3_step = pgm_read_word(&(current_bnk_ptn[8]));
  ch4_step = pgm_read_word(&(current_bnk_ptn[9]));
  ch5_step = pgm_read_word(&(current_bnk_ptn[10]));
  ch6_step = pgm_read_word(&(current_bnk_ptn[11]));
}
