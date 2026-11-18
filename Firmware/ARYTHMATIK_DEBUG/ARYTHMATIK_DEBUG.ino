// A-RYTH-MATIK hardware test / DEBUG firmware v2
// info: https://modulove.io/arythmatik/
//
// One .hex for Arduino Nano (ATmega328P) and LGT8F328P boards (LGT8F_Compat.h).
// Never hangs on a bad display bus: I2C timeout, bus recovery and automatic OLED re-init.
//
// Encoder: turn = next page, click = page action. Pages:
//   1 SYSTEM  chip, CPU clock, supply voltage, free RAM, reset cause   (click: EEPROM test)
//   2 I2C     OLED bus voltages + pull-ups, devices, frame time      (click: bus speed test)
//   3 INPUTS  CLK/RST level, BPM, pulse width, counts, encoder, button
//             (click: clear counts, hold 1 s: next page - turning is the encoder test here)
//   4 OUTPUTS chase / all 1 Hz / follow CLK / LED4+5 / off            (click: next mode)
//   5 LOOP    patch any output into CLK or RST: finds the output, checks delay and width
// No OLED found: the CLK LED blinks 3x every 2 s and the display is retried every 4 s.
//
// USB serial 115200 8N1, single-character commands (used by the web page, also by hand):
//   r / ?  readable report           j      one JSON status line
//   b      measure the bus again     t      bus speed test (~4 s)
//   e      EEPROM test               c      clear counters
//   0..4   select page               m0..m4 output mode (m3 = LED4+5: serial pauses 10 s)
// Lines starting with '{' are JSON, all other lines are text for people.
// See README.md in this folder.
#include "LGT8F_Compat.h"  // Nano + LGT8F328P from one .hex (chip setup, EEPROM) - first include
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>

#define FW_VERSION "2.0"

#ifdef ENCODER_REVERSED
#define ENCODER_PIN1 3
#define ENCODER_PIN2 2
#else
#define ENCODER_PIN1 2
#define ENCODER_PIN2 3
#endif
#define ENCODER_SW_PIN 12
#define CLK_BIT (1 << 5)  // D13 = PB5
#define RST_BIT (1 << 3)  // D11 = PB3
#define OLED_ADDRESS 0x3C
#define LED_CLK 4

static const uint8_t OUT_PIN[6] = { 5, 6, 7, 8, 9, 10 };
static const uint8_t LED_PIN[6] = { 14, 15, 16, 0, 1, 17 };  // LED4/LED5 share D0/D1 with serial

// Types first: the Arduino IDE puts its generated prototypes above the first function.
struct InputStat {
  uint32_t tRise, period, width;
  uint16_t count;
};
struct LoopStat {
  uint16_t hits[2][6];         // [CLK, RST][output]
  uint16_t latMin, latMax;     // us
  uint16_t wMin, wMax;         // 10 us units
  uint16_t sent;
};

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Encoder enc(ENCODER_PIN1, ENCODER_PIN2);

// ---------- reset cause (optiboot passes it in r2; the old Nano bootloader leaves MCUSR) ----------
uint8_t bootR2 __attribute__((section(".noinit")));
uint8_t bootMCUSR __attribute__((section(".noinit")));
void saveResetCause() __attribute__((naked, used, section(".init0")));
void saveResetCause() {
  asm volatile("sts %0, r2\n\t"
               "in r2, %2\n\t"
               "sts %1, r2\n" : "=m"(bootR2), "=m"(bootMCUSR) : "I"(_SFR_IO_ADDR(MCUSR)));
}

// ---------- free RAM: paint unused RAM at start-up, later find the largest untouched gap ----------
extern char __heap_start;
extern char *__brkval;
static void __attribute__((noinline, used)) paintRam() {
  uint8_t *p = (uint8_t *)&__heap_start, *end = (uint8_t *)SP - 16;
  while (p < end) *p++ = 0xAA;
}
static void __attribute__((naked, used, section(".init3"))) paintRamHook() { paintRam(); }
static uint16_t freeRam() {
  uint8_t *p = __brkval ? (uint8_t *)__brkval : (uint8_t *)&__heap_start, *end = (uint8_t *)SP;
  uint16_t best = 0, run = 0;
  for (; p < end; p++) {
    if (*p == 0xAA) {
      if (++run > best) best = run;
    } else {
      run = 0;
    }
  }
  return best;
}

// ---------- analog measurements ----------
static uint16_t adcSample() {
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)) {}
  return ADC;
}
static uint16_t adcAvg(uint8_t n) {  // LGT8F: average a normal and a swapped-input sample
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (IS_LGT) {
      _SFR_MEM8(0x7D) |= (1 << 5);  // ADCSRC.SPN
      uint16_t a = adcSample();
      _SFR_MEM8(0x7D) &= ~(1 << 5);
      sum += (a + adcSample()) >> 1;
    } else {
      sum += adcSample();
    }
  }
  return sum / n;
}
// Supply voltage in mV. LGT8F: VCC/5 channel against the calibrated 2.048 V reference (~1 %).
// ATmega328P: 1.1 V bandgap against VCC (the bandgap varies by up to +-10 % between chips).
static uint16_t readVcc() {
  uint16_t mv;
  if (IS_LGT) {
    uint8_t dacon = _SFR_MEM8(0xA0);
    _SFR_MEM8(0xA0) = (dacon & 0x0C) | 0x02;            // DACON: ADC uses the internal reference
    _SFR_MEM8(0xC8) = _SFR_MEM8(0xCE);                  // VCAL = VCAL2 (2.048 V trim)
    _SFR_MEM8(0xAD) = (_SFR_MEM8(0xAD) & 0xB0) | 0x06;  // ADCSRD: VCC/5 channel on
    ADMUX = (2 << 6) | 8;
    delay(2);
    mv = (uint32_t)adcAvg(8) * 10240UL / 4096UL;
    _SFR_MEM8(0xAD) &= 0xF0;
    _SFR_MEM8(0xA0) = dacon;
  } else {
    ADMUX = (1 << REFS0) | 0x0E;
    delay(2);
    adcSample();
    mv = 1125300UL / adcAvg(8);
  }
  ADMUX = (1 << REFS0);
  return mv;
}
static uint16_t readPinMv(uint8_t ch, uint16_t vcc) {
  ADMUX = (1 << REFS0) | ch;
  delayMicroseconds(200);
  adcSample();
  return (uint32_t)adcAvg(4) * vcc / (IS_LGT ? 4096UL : 1024UL);
}

// ---------- I2C bus health ----------
uint16_t vccMv, sdaMv, sclMv;
bool pullupsOk, oledOk;
uint16_t i2cTimeouts, oledReinits;
uint8_t i2cDevices[4], i2cDeviceCount;
uint16_t drawUs, sendUs;

// Idle bus with the I2C unit off and no MCU pull-ups: the voltage is the OLED board's own
// supply seen through its pull-ups (about 3.3 V on most modules, ~0 V = no pull-up/no power).
static void measureBus() {
  TWCR = 0;
  pinMode(SDA, INPUT);
  pinMode(SCL, INPUT);
  delayMicroseconds(50);
  sdaMv = readPinMv(4, vccMv);
  sclMv = readPinMv(5, vccMv);
  bool ok = true;  // pull each line low for a moment: with a pull-up it is back high in 100 us
  for (uint8_t pin = SDA; pin <= SCL; pin++) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(20);
    pinMode(pin, INPUT);
    delayMicroseconds(100);
    if (!digitalRead(pin)) ok = false;
  }
  pullupsOk = ok;
}

// A slave can hold SDA low when the MCU was reset in the middle of a transfer:
// clock SCL until it lets go, then send a STOP.
static void recoverBus() {
  TWCR = 0;
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  delayMicroseconds(10);
  for (uint8_t i = 0; i < 18 && !digitalRead(SDA); i++) {
    digitalWrite(SCL, LOW);
    pinMode(SCL, OUTPUT);
    delayMicroseconds(5);
    pinMode(SCL, INPUT_PULLUP);
    delayMicroseconds(5);
  }
  digitalWrite(SDA, LOW);
  pinMode(SDA, OUTPUT);
  delayMicroseconds(5);
  pinMode(SDA, INPUT_PULLUP);
  delayMicroseconds(5);
}

static void scanBus() {
  i2cDeviceCount = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    uint8_t e = Wire.endTransmission();
    if (e == 0 && i2cDeviceCount < 4) i2cDevices[i2cDeviceCount++] = a;
    if (e == 5) {  // timeout: bus stuck, give up the scan
      i2cTimeouts++;
      break;
    }
  }
}

static void oledStart() {
  recoverBus();
  Wire.begin();
  Wire.setWireTimeout(25000, true);  // never hang on a stuck bus
  Wire.beginTransmission(OLED_ADDRESS);
  oledOk = Wire.endTransmission() == 0;
  if (oledOk) {
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
#if defined(ROTATE_PANEL) || defined(PANEL_USD)
    display.setRotation(2);
#endif
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
  }
  Wire.clearWireTimeoutFlag();
}

static void busCheck() {
  measureBus();
  oledStart();
  scanBus();
}

// ---------- inputs (pin change interrupt on CLK D13 / RST D11) ----------
volatile InputStat inClk, inRst;
volatile uint8_t pinbPrev;

enum { OUT_CHASE, OUT_BLINK, OUT_FOLLOW, OUT_LED45, OUT_OFF, OUT_MODES };
volatile uint8_t outMode;
volatile bool outputsActive;  // output tests run only on the OUTPUTS page
volatile bool loopTest;       // loopback test runs only on the LOOP page
volatile uint8_t loopOut;     // output pulsing right now
volatile uint32_t loopT0;
volatile LoopStat loopStat;

static inline void setOutputs(uint8_t mask) {  // bit i = output i (pins 5..10)
  PORTD = (PORTD & 0x1F) | ((mask & 0x07) << 5);
  PORTB = (PORTB & 0xF8) | ((mask >> 3) & 0x07);
}

volatile bool ledsOnSerialPins;  // drive LED4/LED5 too (serial is off while true)
static void setLeds(uint8_t mask) {
  for (uint8_t i = 0; i < 6; i++) {
    if ((i == 3 || i == 4) && !ledsOnSerialPins) continue;
    uint8_t pin = LED_PIN[i];
    if (pin < 8) {
      if (mask & (1 << i)) PORTD |= 1 << pin;
      else PORTD &= ~(1 << pin);
    } else {
      if (mask & (1 << i)) PORTC |= 1 << (pin - 14);
      else PORTC &= ~(1 << (pin - 14));
    }
  }
}

static uint8_t loopRiseOk;  // bit per input: its last rise was caused by our pulse
static void loopEdge(uint8_t input, bool rise, uint32_t t) {
  volatile LoopStat &s = loopStat;
  uint8_t bit = 1 << input;
  if (rise) {
    uint32_t d = t - loopT0;
    if (d > 2000) {  // not caused by our pulse (input delay is a few us)
      loopRiseOk &= ~bit;
      return;
    }
    loopRiseOk |= bit;
    s.hits[input][loopOut]++;
    if (d < s.latMin) s.latMin = d;
    if (d > s.latMax) s.latMax = d;
  } else {
    if (!(loopRiseOk & bit)) return;  // width only of our own pulses, not of a clock on the jack
    loopRiseOk &= ~bit;
    volatile InputStat &in = input ? inRst : inClk;
    uint32_t w = (t - in.tRise) / 10;
    if (w > 6000) return;
    if (w < s.wMin) s.wMin = w;
    if (w > s.wMax) s.wMax = w;
  }
}

static void inputEdge(volatile InputStat &in, bool rise, uint32_t t) {
  if (rise) {
    in.period = t - in.tRise;
    in.tRise = t;
    in.count++;
  } else {
    in.width = t - in.tRise;
  }
}

ISR(PCINT0_vect) {
  uint32_t t = micros();
  uint8_t now = PINB, ch = now ^ pinbPrev;
  pinbPrev = now;
  if (ch & CLK_BIT) {
    bool rise = now & CLK_BIT;
    inputEdge(inClk, rise, t);
    if (outputsActive && outMode == OUT_FOLLOW) setOutputs(rise ? 0x3F : 0);
    if (loopTest) loopEdge(0, rise, t);
  }
  if (ch & RST_BIT) {
    bool rise = now & RST_BIT;
    inputEdge(inRst, rise, t);
    if (loopTest) loopEdge(1, rise, t);
  }
}

// ---------- output timing from Timer2 (0.5 ms ticks), independent of the display ----------
volatile uint16_t t2Tick;
ISR(TIMER2_COMPA_vect) {
  uint16_t t = ++t2Tick;
  if (t >= 60000) t2Tick = 0;  // multiple of all periods below
  if (loopTest) {              // 5 ms pulse every 50 ms, next output each time
    uint16_t ph = t % 100;
    if (ph == 0) {
      loopOut = (loopOut + 1) % 6;
      setOutputs(1 << loopOut);
      loopT0 = micros();
      setLeds(1 << loopOut);
      loopStat.sent++;
    } else if (ph == 10) {
      setOutputs(0);
      setLeds(0);
    }
    return;
  }
  if (!outputsActive) return;
  switch (outMode) {
    case OUT_CHASE:  // 250 ms per output (LED4/LED5 only in the LED4+5 mode)
    case OUT_LED45:
      if (t % 500 == 0) {
        uint8_t i = (t / 500) % 6;
        setOutputs(1 << i);
        setLeds(1 << i);
        if (i == 0) PORTD |= 1 << LED_CLK;
        else PORTD &= ~(1 << LED_CLK);
      }
      break;
    case OUT_BLINK:  // all on/off at 1 Hz
      if (t % 1000 == 0) {
        setOutputs(0x3F);
        setLeds(0x3F);
        PORTD |= 1 << LED_CLK;
      } else if (t % 1000 == 500) {
        setOutputs(0);
        setLeds(0);
        PORTD &= ~(1 << LED_CLK);
      }
      break;
    case OUT_FOLLOW:  // outputs are switched in the pin change interrupt, LEDs follow here
      if (PINB & CLK_BIT) {
        setLeds(0x3F);
        PORTD |= 1 << LED_CLK;
      } else {
        setLeds(0);
        PORTD &= ~(1 << LED_CLK);
      }
      break;
    default:
      break;
  }
}

static void startTimer2() {
  TCCR2A = 1 << WGM21;
#if F_CPU > 20000000UL
  TCCR2B = (1 << CS22) | (1 << CS20);  // /128
#else
  TCCR2B = 1 << CS22;  // /64
#endif
  OCR2A = 124;  // 0.5 ms
  TIMSK2 = 1 << OCIE2A;
}

static void allOff() {
  noInterrupts();
  setOutputs(0);
  setLeds(0);
  PORTD &= ~(1 << LED_CLK);
  interrupts();
}

static void clearLoopStats() {
  noInterrupts();
  memset((void *)&loopStat, 0, sizeof(loopStat));
  loopStat.latMin = 0xFFFF;
  loopStat.wMin = 0xFFFF;
  interrupts();
}

int lastPos;
static void clearCounts() {
  noInterrupts();
  inClk.count = inRst.count = 0;
  inClk.period = inRst.period = inClk.width = inRst.width = 0;
  interrupts();
  enc.write(0);
  lastPos = 0;
  clearLoopStats();
}

// ---------- serial (paused only during the 10 s LED4+5 test, they share D0/D1) ----------
bool serialOn;
uint32_t led45Start;
static void serialStart() {
  if (serialOn) return;
  noInterrupts();
  ledsOnSerialPins = false;
  interrupts();
  Serial.begin(115200);
  serialOn = true;
}
static void serialStop() {
  if (!serialOn) return;
  Serial.flush();
  Serial.end();
  serialOn = false;
  pinMode(0, OUTPUT);
  pinMode(1, OUTPUT);
  ledsOnSerialPins = true;
}

// ---------- printing helpers (Serial and display) ----------
static void printFixed(Print &p, uint32_t v, uint8_t decimals) {  // v / 10^decimals
  uint32_t d = 1;
  for (uint8_t i = 0; i < decimals; i++) d *= 10;
  p.print(v / d);
  if (decimals) {
    p.print('.');
    uint32_t f = v % d;
    for (uint32_t m = d / 10; m > 1 && f < m; m /= 10) p.print('0');
    p.print(f);
  }
}
static void printVolt(Print &p, uint16_t mv) {
  printFixed(p, (mv + 5) / 10, 2);
  p.print('V');
}
static void printBpm(Print &p, uint32_t periodUs) {  // 4 pulses per beat (16th notes)
  if (!periodUs || periodUs > 4000000UL) {
    p.print('-');
    return;
  }
  printFixed(p, 150000000UL / periodUs, 1);
}
static const __FlashStringHelper *resetCause() {
  // optiboot (new Nano bootloader, LGT8F) clears MCUSR and passes it in r2; the old Nano
  // bootloader leaves MCUSR alone and r2 is random then.
  uint8_t f = bootMCUSR ? bootMCUSR : bootR2;
  if (f & (1 << PORF)) return F("power-on");
  if (f & (1 << BORF)) return F("brown-out");
  if (f & (1 << EXTRF)) return F("reset-pin");
  if (f & (1 << WDRF)) return F("watchdog/boot");
  return F("unknown");
}

uint8_t eepromResult;  // 0 = not run, 1 = ok, 2 = fail
static void eepromTest() {
  const int a = 1010;  // spare address no firmware uses; the old value is put back
  uint8_t orig = EEPROM.read(a);
  EEPROM.write(a, orig ^ 0x5A);
  bool ok = EEPROM.read(a) == (uint8_t)(orig ^ 0x5A);
  EEPROM.write(a, orig);
  ok = ok && EEPROM.read(a) == orig;
  eepromResult = ok ? 1 : 2;
}

// ---------- bus speed test: send the same frame at 400 k / 800 k / 1 MHz ----------
uint32_t speedTestUs[3];
static void pushFrame(uint32_t clk) {
  Wire.setClock(clk);
  static const uint8_t setup[] = { 0x22, 0x00, 0xFF, 0x21, 0x00, 127 };
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write((uint8_t)0x00);
  Wire.write(setup, sizeof(setup));
  Wire.endTransmission();
  const uint8_t *p = display.getBuffer();
  for (uint16_t n = 0; n < 1024; n += 31) {
    uint8_t len = (1024 - n) < 31 ? (1024 - n) : 31;
    Wire.beginTransmission(OLED_ADDRESS);
    Wire.write((uint8_t)0x40);
    Wire.write(p + n, len);
    Wire.endTransmission();
  }
}
static void speedTest() {
  if (!oledOk) return;
  static const uint32_t hz[3] = { 400000UL, 800000UL, 1000000UL };
  for (uint8_t i = 0; i < 3; i++) {
    display.clearDisplay();
    display.drawRect(0, 0, 128, 64, WHITE);
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.print(hz[i] / 1000);
    display.print(F(" kHz"));
    display.setTextSize(1);
    display.setCursor(10, 40);
    display.print(F("clean picture?"));
    for (uint8_t y = 52; y < 62; y++)
      for (uint8_t x = 10; x < 118; x++)
        if ((x + y) & 1) display.drawPixel(x, y, WHITE);
    uint32_t t = micros();
    pushFrame(hz[i]);
    speedTestUs[i] = micros() - t;
    delay(1200);
  }
  Wire.setClock(100000UL);
  if (Wire.getWireTimeoutFlag()) {
    i2cTimeouts++;
    oledStart();
  }
}

// ---------- serial output ----------
static void jsonKey(const __FlashStringHelper *k) {
  Serial.print(',');
  Serial.print('"');
  Serial.print(k);
  Serial.print(F("\":"));
}
static void jsonInput(const __FlashStringHelper *k, volatile InputStat &v, uint8_t bit) {
  InputStat s;
  noInterrupts();
  memcpy(&s, (const void *)&v, sizeof(s));
  interrupts();
  jsonKey(k);
  Serial.print(F("{\"level\":"));
  Serial.print(PINB & bit ? 1 : 0);
  Serial.print(F(",\"count\":"));
  Serial.print(s.count);
  Serial.print(F(",\"period_us\":"));
  Serial.print(s.period);
  Serial.print(F(",\"width_us\":"));
  Serial.print(s.width);
  Serial.print(F(",\"age_ms\":"));
  Serial.print(s.count ? (micros() - s.tRise) / 1000 : 0);
  Serial.print('}');
}
static void jsonList(const volatile uint16_t *v) {
  Serial.print('[');
  for (uint8_t i = 0; i < 6; i++) {
    if (i) Serial.print(',');
    Serial.print(v[i]);
  }
  Serial.print(']');
}

static void jsonStatus() {
  Serial.print(F("{\"t\":\"st\",\"fw\":\"" FW_VERSION "\",\"chip\":\""));
  Serial.print(chipName());
  Serial.print('"');
  jsonKey(F("mhz"));
  Serial.print(F_CPU / 1000000UL);
  jsonKey(F("id"));
  Serial.print('"');
  Serial.print(chipUniqueId(), HEX);
  Serial.print('"');
  jsonKey(F("reset"));
  Serial.print('"');
  Serial.print(resetCause());
  Serial.print('"');
  jsonKey(F("vcc"));
  Serial.print(vccMv);
  jsonKey(F("vref"));
  Serial.print(IS_LGT ? F("\"lgt\"") : F("\"bandgap\""));
  jsonKey(F("ram"));
  Serial.print(freeRam());
  jsonKey(F("ee_size"));
  Serial.print(EEPROM.length());
  jsonKey(F("ee_test"));
  Serial.print(eepromResult);
  jsonKey(F("sda"));
  Serial.print(sdaMv);
  jsonKey(F("scl"));
  Serial.print(sclMv);
  jsonKey(F("pullups"));
  Serial.print(pullupsOk);
  jsonKey(F("oled"));
  Serial.print(oledOk);
  jsonKey(F("devs"));
  Serial.print('[');
  for (uint8_t i = 0; i < i2cDeviceCount; i++) {
    if (i) Serial.print(',');
    Serial.print(i2cDevices[i]);
  }
  Serial.print(']');
  jsonKey(F("i2c_timeouts"));
  Serial.print(i2cTimeouts);
  jsonKey(F("oled_reinits"));
  Serial.print(oledReinits);
  jsonKey(F("draw_us"));
  Serial.print(drawUs);
  jsonKey(F("send_us"));
  Serial.print(sendUs);
  jsonInput(F("clk"), inClk, CLK_BIT);
  jsonInput(F("rst"), inRst, RST_BIT);
  jsonKey(F("enc"));
  Serial.print(enc.read() / 4);
  jsonKey(F("button"));
  Serial.print(digitalRead(ENCODER_SW_PIN) ? 0 : 1);
  jsonKey(F("page"));
  extern uint8_t page;
  Serial.print(page);
  jsonKey(F("mode"));
  Serial.print(outMode);
  jsonKey(F("uptime_ms"));
  Serial.print(millis());
  volatile LoopStat &s = loopStat;  // read in place: a stack copy costs 32 bytes of scarce RAM
  jsonKey(F("loop"));
  Serial.print(F("{\"active\":"));
  Serial.print(loopTest ? 1 : 0);
  Serial.print(F(",\"sent\":"));
  Serial.print(s.sent);
  Serial.print(F(",\"clk\":"));
  jsonList(s.hits[0]);
  Serial.print(F(",\"rst\":"));
  jsonList(s.hits[1]);
  Serial.print(F(",\"delay_us\":"));
  if (s.latMin == 0xFFFF) {
    Serial.print(F("null"));
  } else {
    Serial.print('[');
    Serial.print(s.latMin);
    Serial.print(',');
    Serial.print(s.latMax);
    Serial.print(']');
  }
  Serial.print(F(",\"width_us\":"));
  if (s.wMin == 0xFFFF) {
    Serial.print(F("null"));
  } else {
    Serial.print('[');
    Serial.print((uint32_t)s.wMin * 10);
    Serial.print(',');
    Serial.print((uint32_t)s.wMax * 10);
    Serial.print(']');
  }
  Serial.println(F("}}"));
}

static void report() {
  Serial.println();
  Serial.println(F("=== A-RYTH-MATIK DEBUG " FW_VERSION " ==="));
  Serial.print(F("chip      "));
  Serial.print(chipName());
  Serial.print(F("  "));
  Serial.print(F_CPU / 1000000UL);
  Serial.print(F(" MHz"));
  if (IS_LGT) {
    Serial.print(F("  id "));
    Serial.print(chipUniqueId(), HEX);
  }
  Serial.println();
  Serial.print(F("reset     "));
  Serial.println(resetCause());
  Serial.print(F("VCC       "));
  printVolt(Serial, vccMv);
  Serial.println(IS_LGT ? F("  (LGT internal reference, ~1 %)") : F("  (ATmega bandgap, +-10 %)"));
  Serial.print(F("RAM free  "));
  Serial.print(freeRam());
  Serial.println(F(" bytes never used so far"));
  Serial.print(F("EEPROM    "));
  Serial.print(EEPROM.length());
  Serial.print(F(" bytes"));
  Serial.println(eepromResult == 1 ? F(", test ok") : eepromResult == 2 ? F(", TEST FAILED") : F(", test: send e"));
  Serial.print(F("I2C idle  SDA "));
  printVolt(Serial, sdaMv);
  Serial.print(F("  SCL "));
  printVolt(Serial, sclMv);
  Serial.println(pullupsOk ? F("  pull-ups ok") : F("  PULL-UPS MISSING / LINE HELD LOW"));
  Serial.print(F("I2C devs  "));
  if (!i2cDeviceCount) Serial.print(F("none"));
  for (uint8_t i = 0; i < i2cDeviceCount; i++) {
    Serial.print(F("0x"));
    Serial.print(i2cDevices[i], HEX);
    Serial.print(' ');
  }
  Serial.print(F("  timeouts "));
  Serial.print(i2cTimeouts);
  Serial.print(F("  OLED re-inits "));
  Serial.println(oledReinits);
  Serial.print(F("OLED      "));
  if (oledOk) {
    Serial.print(F("ok, frame "));
    printFixed(Serial, ((uint32_t)drawUs + sendUs) / 100, 1);
    Serial.print(F(" ms (draw "));
    printFixed(Serial, drawUs / 100, 1);
    Serial.print(F(" + send "));
    printFixed(Serial, sendUs / 100, 1);
    Serial.println(F(" ms at 400 kHz)"));
  } else {
    Serial.println(F("NOT FOUND at 0x3C (CLK LED blinks 3x)"));
  }
  if (speedTestUs[0]) {
    Serial.print(F("bus test  frame send ms 400k "));
    printFixed(Serial, speedTestUs[0] / 100, 1);
    Serial.print(F("  800k "));
    printFixed(Serial, speedTestUs[1] / 100, 1);
    Serial.print(F("  1M "));
    printFixed(Serial, speedTestUs[2] / 100, 1);
    Serial.println();
  }
  Serial.print(F("inputs    CLK "));
  Serial.print(PINB & CLK_BIT ? F("high") : F("low"));
  Serial.print(F(" n="));
  Serial.print(inClk.count);
  Serial.print(F("  RST "));
  Serial.print(PINB & RST_BIT ? F("high") : F("low"));
  Serial.print(F(" n="));
  Serial.print(inRst.count);
  Serial.print(F("  encoder "));
  Serial.print(enc.read() / 4);
  Serial.print(F("  button "));
  Serial.println(digitalRead(ENCODER_SW_PIN) ? F("up") : F("down"));
  Serial.println(F("commands  r report  j json  b bus check  t bus speed test  e EEPROM test"));
  Serial.println(F("          c clear  0-4 page  m0-m4 output mode (m3 pauses serial 10 s)"));
  Serial.println(F("=== end ==="));
}

// ---------- pages ----------
enum { PAGE_SYSTEM, PAGE_I2C, PAGE_INPUTS, PAGE_OUTPUTS, PAGE_LOOP, PAGES };
uint8_t page = PAGE_SYSTEM;
static const char pageNames[] PROGMEM = "SYSTEM \0I2C    \0INPUTS \0OUTPUTS\0LOOP   ";
static const char modeNames[] PROGMEM = "chase   \0all 1 Hz\0follow  \0LED4+5  \0off     ";

static void header() {
  display.fillRect(0, 0, 128, 9, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 1);
  display.print((const __FlashStringHelper *)(pageNames + page * 8));
  display.setCursor(56, 1);
  display.print(page + 1);
  display.print('/');
  display.print(PAGES);
  display.setCursor(86, 1);
  display.print(IS_LGT ? F("LGT8F") : F("NANO"));
  display.setTextColor(WHITE);
}

static void line(uint8_t row) { display.setCursor(0, 12 + row * 9); }

static void drawPage() {
  display.clearDisplay();
  display.setTextSize(1);
  header();
  switch (page) {
    case PAGE_SYSTEM:
      line(0);
      display.print(chipName());
      display.print(' ');
      display.print(F_CPU / 1000000UL);
      display.print(F("MHz v" FW_VERSION));
      line(1);
      display.print(F("VCC "));
      printVolt(display, vccMv);
      display.print(IS_LGT ? F("  (1%)") : F(" (10%)"));
      line(2);
      display.print(F("RAM free "));
      display.print(freeRam());
      display.print(F(" B"));
      line(3);
      display.print(F("reset "));
      display.print(resetCause());
      line(4);
      display.print(F("EEPROM "));
      display.print(EEPROM.length());
      display.print(eepromResult == 1 ? F(" ok") : eepromResult == 2 ? F(" FAIL") : F(" (click)"));  // 21 chars max
      break;
    case PAGE_I2C:
      line(0);
      display.print(F("SDA "));
      printVolt(display, sdaMv);
      display.print(F(" SCL "));
      printVolt(display, sclMv);
      line(1);
      display.print(pullupsOk ? F("pull-ups ok") : F("PULL-UPS BAD"));
      line(2);
      display.print(F("devices "));
      for (uint8_t i = 0; i < i2cDeviceCount; i++) {
        display.print(i2cDevices[i], HEX);
        display.print(' ');
      }
      line(3);
      display.print(F("frame "));  // whole ms: the line holds 21 characters
      display.print(((uint32_t)drawUs + sendUs + 500) / 1000);
      display.print(F("ms d"));
      display.print((drawUs + 500) / 1000);
      display.print(F(" s"));
      display.print((sendUs + 500) / 1000);
      line(4);
      display.print(F("tmo "));
      display.print(i2cTimeouts);
      display.print(F(" re "));
      display.print(oledReinits);
      display.print(F(" click=test"));
      break;
    case PAGE_INPUTS: {
      InputStat c, r;
      noInterrupts();
      memcpy(&c, (const void *)&inClk, sizeof(c));
      memcpy(&r, (const void *)&inRst, sizeof(r));
      interrupts();
      bool fresh = c.count && (micros() - c.tRise) < 2000000UL;
      line(0);
      display.print(F("CLK "));
      display.print(PINB & CLK_BIT ? F("HI ") : F("lo "));
      display.print(c.count);
      display.print(F(" w "));
      printFixed(display, c.width / 100, 1);
      display.print(F("ms"));
      line(1);
      display.print(F("    "));
      printBpm(display, fresh ? c.period : 0);
      display.print(F(" bpm (1/16)"));
      line(2);
      display.print(F("RST "));
      display.print(PINB & RST_BIT ? F("HI ") : F("lo "));
      display.print(r.count);
      display.print(F(" w "));
      printFixed(display, r.width / 100, 1);
      display.print(F("ms"));
      line(3);
      display.print(F("encoder "));
      display.print(enc.read() / 4);
      display.print(F("  btn "));
      display.print(digitalRead(ENCODER_SW_PIN) ? F("up") : F("DOWN"));
      line(4);
      display.print(F("click=clear hold=next"));
      break;
    }
    case PAGE_OUTPUTS:
      line(0);
      display.print(F("mode: "));
      display.print((const __FlashStringHelper *)(modeNames + outMode * 9));
      line(1);
      display.print(F("click = next mode"));
      line(2);
      display.print(F("follow: outs = CLK in"));
      line(3);
      display.print(F("LED4+5 pauses serial"));
      break;
    case PAGE_LOOP: {
      volatile LoopStat &s = loopStat;
      line(0);
      display.print(F("patch OUTx->CLK/RST"));
      for (uint8_t in = 0; in < 2; in++) {
        line(1 + in);
        display.print(in ? F("RST ") : F("CLK "));
        for (uint8_t o = 0; o < 6; o++) {
          display.print(s.hits[in][o] ? (char)('1' + o) : '.');
          display.print(' ');
        }
      }
      line(3);
      if (s.latMin != 0xFFFF) {
        display.print(F("delay "));
        display.print(s.latMin);
        display.print('-');
        display.print(s.latMax);
        display.print(F("us"));
      } else {
        display.print(F("no signal yet"));
      }
      line(4);
      if (s.wMin != 0xFFFF) {
        display.print(F("width "));
        printFixed(display, s.wMin, 2);
        display.print('-');
        printFixed(display, s.wMax, 2);
        display.print(F("ms"));
      }
      break;
    }
  }
}

static void showPage() {
  if (!oledOk) return;
  uint32_t t0 = micros();
  drawPage();
  uint32_t t1 = micros();
  display.display();
  uint32_t t2 = micros();
  drawUs = t1 - t0;
  sendUs = t2 - t1;
  if (Wire.getWireTimeoutFlag()) {  // bus trouble during the frame: recover and re-init
    i2cTimeouts++;
    oledReinits++;
    oledStart();
  }
}

static void applyOutMode() {
  allOff();
  if (page == PAGE_OUTPUTS && outMode == OUT_LED45) {
    led45Start = millis();
    serialStop();
  } else {
    serialStart();
  }
}

static void setOutMode(uint8_t m) {
  outputsActive = false;
  outMode = m % OUT_MODES;
  applyOutMode();
  outputsActive = (page == PAGE_OUTPUTS);
}

static void enterPage(uint8_t p) {
  page = p % PAGES;
  outputsActive = false;
  loopTest = false;
  allOff();
  if (page == PAGE_LOOP) {
    clearLoopStats();
    loopTest = true;
  }
  applyOutMode();
  outputsActive = (page == PAGE_OUTPUTS);
  if (page == PAGE_I2C) busCheck();  // fresh bus measurement when the page opens
}

static void pageClick() {
  switch (page) {
    case PAGE_SYSTEM: eepromTest(); break;
    case PAGE_I2C: speedTest(); break;
    case PAGE_INPUTS: clearCounts(); break;
    case PAGE_OUTPUTS: setOutMode(outMode + 1); break;
    case PAGE_LOOP: clearLoopStats(); break;
  }
}

static void command(char c) {
  static bool modeNext;
  if (modeNext) {
    modeNext = false;
    if (c >= '0' && c < '0' + OUT_MODES) {
      if (page != PAGE_OUTPUTS) enterPage(PAGE_OUTPUTS);
      if (c - '0' == OUT_LED45) Serial.println(F("{\"t\":\"ok\",\"note\":\"serial pauses 10 s\"}"));
      setOutMode(c - '0');
      if (serialOn) Serial.println(F("{\"t\":\"ok\"}"));
      return;
    }
  }
  switch (c) {
    case 'r':
    case '?':
    case 'h': report(); break;
    case 'j': jsonStatus(); break;
    case 'b':
      busCheck();
      jsonStatus();
      break;
    case 't':
      speedTest();
      Serial.print(F("{\"t\":\"bus\",\"oled\":"));
      Serial.print(oledOk);
      Serial.print(F(",\"k400_us\":"));
      Serial.print(speedTestUs[0]);
      Serial.print(F(",\"k800_us\":"));
      Serial.print(speedTestUs[1]);
      Serial.print(F(",\"k1000_us\":"));
      Serial.print(speedTestUs[2]);
      Serial.print(F(",\"i2c_timeouts\":"));
      Serial.print(i2cTimeouts);
      Serial.println('}');
      break;
    case 'e':
      eepromTest();
      Serial.print(F("{\"t\":\"ee\",\"ok\":"));
      Serial.print(eepromResult == 1 ? 1 : 0);
      Serial.println('}');
      break;
    case 'c':
      clearCounts();
      Serial.println(F("{\"t\":\"ok\"}"));
      break;
    case 'm': modeNext = true; break;
    default:
      if (c >= '0' && c < '0' + PAGES) {
        enterPage(c - '0');
        if (serialOn) Serial.println(F("{\"t\":\"ok\"}"));
      }
      break;
  }
}

// ---------- setup / loop ----------
void setup() {
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(OUT_PIN[i], OUTPUT);
    if (LED_PIN[i] > 1) pinMode(LED_PIN[i], OUTPUT);
  }
  pinMode(LED_CLK, OUTPUT);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  pinMode(13, INPUT);
  pinMode(11, INPUT);
  MCUSR = 0;  // next reset shows only its own cause (the old Nano bootloader keeps adding flags)
  serialStart();

  vccMv = readVcc();
  delay(300);  // OLED power-up
  busCheck();
  if (oledOk) {
    display.setTextSize(2);
    display.setCursor(16, 8);
    display.print(F("A-RYTH-"));
    display.setCursor(28, 26);
    display.print(F("MATIK"));
    display.setTextSize(1);
    display.setCursor(16, 50);
    display.print(F("hardware test " FW_VERSION));
    display.display();
    delay(900);
  }
  clearCounts();
  pinbPrev = PINB;
  PCMSK0 |= CLK_BIT | RST_BIT;
  PCICR |= 1 << PCIE0;
  startTimer2();
  showPage();  // first frame, so the report has a frame time
  report();
}

void loop() {
  static uint32_t lastDraw, lastVcc, lastBlink, swTime;
  static bool swPrev = true, swHeld = false;
  uint32_t now = millis();

  // encoder: one detent = one page (on INPUTS the encoder count is shown instead)
  int pos = enc.read() / 4;
  if (page != PAGE_INPUTS && pos != lastPos) {
    enterPage(page + PAGES + (pos > lastPos ? 1 : -1));
    lastPos = pos;
  } else if (page == PAGE_INPUTS) {
    lastPos = pos;
  }

  // button: click = page action; on INPUTS a 1 s hold = next page (its release is ignored)
  bool sw = digitalRead(ENCODER_SW_PIN);
  if (sw != swPrev && now - swTime > 30) {
    swTime = now;
    swPrev = sw;
    if (sw) {
      if (!swHeld) pageClick();
      swHeld = false;
    }
  }
  if (page == PAGE_INPUTS && !swPrev && !swHeld && now - swTime > 1000) {
    swHeld = true;
    enterPage(page + 1);
  }

  // the LED4+5 test pauses serial for 10 s, then goes back to chase
  if (!serialOn && now - led45Start > 10000) setOutMode(OUT_CHASE);

  while (serialOn && Serial.available()) command(Serial.read());

  if (now - lastVcc > 1000) {
    lastVcc = now;
    vccMv = readVcc();
  }

  // no OLED: CLK LED blinks 3x every 2 s, and the display is retried every 4 s
  if (!oledOk) {
    uint16_t ph = (now - lastBlink) % 2000;
    if (!outputsActive) digitalWrite(LED_CLK, ph < 600 && (ph % 200) < 100);
    if (now - lastBlink > 4000) {
      lastBlink = now;
      oledStart();
      if (oledOk) oledReinits++;
    }
  }

  if (now - lastDraw >= 100) {
    lastDraw = now;
    showPage();
  }
}
