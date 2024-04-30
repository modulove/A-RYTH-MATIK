// This firmware is for testing the A-RYTH-MATIK hardware
// info here: https://modulove.io/arythmatik/
#include<Wire.h>
#include<Adafruit_GFX.h>
#include<Adafruit_SSD1306.h>
#include <FastGPIO.h>

//Encoder setting
#define  ENCODER_OPTIMIZE_INTERRUPTS //countermeasure of encoder noise
#include <Encoder.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// encoder & direction
#ifdef ENCODER_REVERSED
#define ENCODER_PIN1 3
#define ENCODER_PIN2 2  // 2pin, 3pin is default
#else
#define ENCODER_PIN1 2
#define ENCODER_PIN2 3
#endif
#define ENCODER_SW_PIN 12
#define CLK_PIN 13
#define RST_PIN 11
#define ENCODER_COUNT_PER_ROTATION 4

#define OUT_CH1 5
#define OUT_CH2 6
#define OUT_CH3 7
#define OUT_CH4 8
#define OUT_CH5 9
#define OUT_CH6 10
#define LED_CH1 14
#define LED_CH2 15
#define LED_CH3 16
#define LED_CH4 0
#define LED_CH5 1
#define LED_CH6 17
#define LED_CLK 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//rotery encoder
Encoder myEnc(ENCODER_PIN1, ENCODER_PIN2);//use 3pin 2pin
int oldPosition  = -999;
int newPosition = -999;
int i = 0;

//push button
bool sw = 0;//push button
bool old_sw;//countermeasure of sw chattering
unsigned long sw_timer = 0;//countermeasure of sw chattering

bool disp_reflesh = 1;//0=not reflesh display , 1= reflesh display , countermeasure of display reflesh bussy
int clk_val = 0;



#define CHASER_DELAY 50 // Delay between chaser steps (in ms)
#define PWM_DELAY 10 // Delay between PWM steps (in ms)
#define PWM_MAX 255 // Maximum PWM value
#define PWM_MIN 5 // Minimum PWM value

void setup() {

  Serial.begin(115200);
  // Initialize OLED display
  delay(1000); // Screen needs a sec to initialize
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  // Initialize PWM on LED_CLK pin
  pinMode(LED_CLK, OUTPUT);
  analogWrite(LED_CLK, 0);


  // OLED setting
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  #ifdef PANEL_USD
  display.setRotation(2);  // 180 degree rotation for upside-down use
  #else
  display.setRotation(0);  // Normal orientation
  #endif
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.setCursor(10, 10);
  display.setTextSize(2);
  display.setCursor(20, 14);
  display.println("MODULE");
  display.setCursor(30, 32);
  display.println("LIEBE");
  display.display();
  //pin mode setting
  FastGPIO::Pin<ENCODER_SW_PIN>::setInputPulledUp(); //BUTTON
  FastGPIO::Pin<CLK_PIN>::setInput(); // CLK
  FastGPIO::Pin<RST_PIN>::setInput(); // RST

  // Print out the I2C address of the display
  Serial.print(F("Display found at address: 0x"));
  Serial.println(0x3C, HEX);
  display.setTextSize(1);
  display.setCursor(20, 12);
  display.println("Display found at address: 0x");


}


void loop() {

  // Read CPU clock rate and convert to string
  String clockrate = String(F_CPU / 1000000) + " MHz";

  // Clear OLED display and print clock rate and other debug info
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  // Draw dot in bottom right corner of OLED display
  display.drawRect(120, 60, 4, 4, WHITE);

  unsigned long start = micros();
  unsigned long startMillis = millis();
  // display text for referencing platform
  display.setCursor(15, 2);
#if F_CPU == 32000000
  display.print("LGT8F328P");
  display.setCursor(5, 12);
  display.print("CPU speed: ");
  display.println("32 MHz");
#elif F_CPU == 16000000
  display.print("Arduino nano v3.0");
  display.setCursor(14, 12);
  display.print("CPU speed: ");
  display.println("16 MHz");
#endif
  // display frame rate for performance measure
  display.setCursor(7, 32);
  Serial.print("Free RAM: ");
  Serial.println(freeMemory());
  Serial.print("Millis: ");
  Serial.println(millis());
  // loop time for performance measure
  display.setCursor(8, 44);
  display.print("time: ");
  display.print(millis() - start);
  display.print("ms");
  // reset loop time  for performance measure
  start = millis();
  display.display();
  delay(10);



  oldPosition = newPosition;
  //-----------------Rotery encoder read----------------------
  newPosition = myEnc.read() / ENCODER_COUNT_PER_ROTATION;

  if ( newPosition   < oldPosition ) {//turn left

    Serial.println("left");
    display.setTextSize(2);
    display.setCursor(50, 25);
    display.println("left");
    display.display();
    oldPosition = newPosition;
  }
  if (digitalRead(CLK_PIN) != clk_val) {
    clk_val = digitalRead(CLK_PIN);
    FastGPIO::Pin<LED_CLK>::setOutputValue(clk_val);
    display.fillRect(120, 60, 4, 4, WHITE);
    Serial.println("CLOCK");
  }

  else if ( newPosition    > oldPosition ) {//turn right
    Serial.println("right");
    display.setTextSize(2);
    display.setCursor(50, 25);
    display.println("right");
    display.display();
  }
  sw = 1;
  if ((!FastGPIO::Pin<ENCODER_SW_PIN>::isInputHigh()) && ( sw_timer + 300 <= millis() )) {
    sw_timer = millis();
    Serial.println("click");
    display.setTextSize(2);
    display.setCursor(50, 25);
    display.println("click");
    display.fillRect(120, 60, 4, 4, WHITE);
    display.display();
    sw = 0;
  }
  if (millis() % 1000 < 500) {

    FastGPIO::Pin<OUT_CH1>::setOutputValue(HIGH);
    FastGPIO::Pin<OUT_CH2>::setOutputValue(HIGH);
    FastGPIO::Pin<OUT_CH3>::setOutputValue(HIGH);
    FastGPIO::Pin<OUT_CH4>::setOutputValue(HIGH);
    FastGPIO::Pin<OUT_CH5>::setOutputValue(HIGH);
    FastGPIO::Pin<OUT_CH6>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH1>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH2>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH3>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH4>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH5>::setOutputValue(HIGH);
    FastGPIO::Pin<LED_CH6>::setOutputValue(HIGH);
  }
  else {
    FastGPIO::Pin<OUT_CH1>::setOutputValue(LOW);
    FastGPIO::Pin<OUT_CH2>::setOutputValue(LOW);
    FastGPIO::Pin<OUT_CH3>::setOutputValue(LOW);
    FastGPIO::Pin<OUT_CH4>::setOutputValue(LOW);
    FastGPIO::Pin<OUT_CH5>::setOutputValue(LOW);
    FastGPIO::Pin<OUT_CH6>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH1>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH2>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH3>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH4>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH5>::setOutputValue(LOW);
    FastGPIO::Pin<LED_CH6>::setOutputValue(LOW);
  }

}

// debug outpuf for setup function and OLED display
// show for 5 seconds
void debug_display()
{
  // measure time for debug
  unsigned long start = micros();
  unsigned long startMillis = millis();
  // clear display for debugging performance
  display.clearDisplay();
  // display text for referencing platform
  display.setCursor(15, 2);
#if F_CPU == 32000000
  display.print("LGT8F328P");
  display.setCursor(5, 12);
  display.print("CPU speed: ");
  display.println("32 MHz");
#elif F_CPU == 16000000
  display.print("Arduino nano v3.0");
  display.setCursor(5, 12);
  display.print("CPU speed: ");
  display.println("16 MHz");
#endif
  // calculate and display elapsed time for performance measure
  display.setCursor(5, 20);
  display.print("elapsed time : ");
  display.print(millis() - start);
  display.print(" ms");
  // display frame rate for performance measure
  display.setCursor(7, 32);
  display.print("frame rate : ");
  display.print(1000 / (millis() - start));
  display.print(" fps");
  // loop time for performance measure
  display.setCursor(12, 44);
  display.print("loop time : ");
  display.print(millis() - start);
  display.print(" ms");
  // reset loop time  for performance measure
  start = millis();
  delay(10);
}

// Function to calculate free RAM
int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
