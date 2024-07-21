#include "src/libmodulove/arythmatik.h"
#include <avr/pgmspace.h>
#include <FastGPIO.h>
#include <EEPROM.h>
#include "EncoderButton.h"

//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

using namespace modulove;
using namespace arythmatik;

const unsigned long OLED_UPDATE_INTERVAL_NORMAL = 150;  // Normal OLED update interval in milliseconds
const unsigned long OLED_UPDATE_INTERVAL_IDLE = 300;    // OLED update interval when idle
const unsigned long TRIGGER_PULSE_DURATION = 12;  // Trigger pulse duration in milliseconds

// Enum for menu items
enum Menu {
  PROB,
  TOGGLE,
  SWING,
  FACTOR,
  MENU_COUNT  // Total number of menu items
};

// Declare A-RYTH-MATIK hardware variable.
Arythmatik hw;

// reverse encoder
#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#endif

// channel configuration Struct
struct ChannelConfig {
  float probability = 0.5;
  bool state = false;
  float swingAmount = 0.5;  // 0.5 means no swing, range 0.0 to 0.7
  unsigned long lastSwingTime = 0;
  bool swingState = false;
  Menu currentMenu = PROB;  // current menu mode
  unsigned long lastTriggerTime = 0;
  bool triggerActive = false;
  int factor = 1;  // clock divider factor
  int triggerCount = 0;  // trigger counter for factor mode
};

// create configuration for both channels
ChannelConfig channelA;
ChannelConfig channelB;

bool isChannelASelected = true;
bool isEncoderIdle = false;

unsigned long lastUpdateTime = 0;

void setup() {

#ifdef ROTATE_PANEL
  hw.config.RotatePanel = false;
#endif

#ifdef REVERSE_ENCODER
  hw.config.ReverseEncoder = true;
#endif

  // Initialize the A-RYTH-MATIK peripherals.
  hw.Init();

  // Set up encoder parameters
  encoder.setDebounceInterval(5);
  encoder.setMultiClickInterval(70);
  encoder.setLongClickDuration(400);
  encoder.setRateLimit(10);
  encoder.setIdleTimeout(1000);
  encoder.setIdleHandler(onEncoderIdle);
  encoder.setClickHandler(onEncoderClicked); // short press to switch sideA-sideB
  encoder.setDoubleClickHandler(onEncoderDoubleClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);  // long click to switch Mode
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setEncoderPressedHandler(onEncoderPressedRotation);
}

// abstract tree UI
void drawTree(int x, int y, float probability) {
  // main branch
  hw.display.drawLine(x, y, x, y - 10, WHITE);

  // primary twigs
  hw.display.drawLine(x, y - 5, x - 5, y - 15, WHITE);
  hw.display.drawLine(x, y - 5, x + 5, y - 15, WHITE);

  // two dots based on probability
  if (probability <= 0.5) {
    hw.display.fillCircle(x - 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x - 5, y - 15, 4, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 2, WHITE);
  } else {
    hw.display.drawCircle(x - 5, y - 15, 2, WHITE);
    hw.display.fillCircle(x + 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 4, WHITE);
  }
}

// draw vertical line
void drawDottedLine(int x, int y, int length, int spacing) {
  for (int i = 0; i < length; i += spacing) {
    hw.display.drawPixel(x, y + i, WHITE);
  }
}

// bottom bar indicator
void drawBottomBar() {
  hw.display.setCursor(0, 56);

  if (channelA.currentMenu == PROB) {
    // taller center line for 50% in prob mode
    drawDottedLine(30, 54, 10, 2);  // center line 
    // bar for Channel A (centered at 50% and fill left or right)
    int barLengthA = (channelA.probability - 0.5) * 40;
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    if (barLengthA > 0) {
      hw.display.fillRoundRect(30, 58, barLengthA, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(30 + barLengthA, 58, -barLengthA, 4, 2, WHITE);
    }
  } else if (channelA.currentMenu == TOGGLE) {
    // Toggle mode indicator for Channel A
    hw.display.fillCircle(30, 25, 3, channelA.state ? WHITE : BLACK);
    hw.display.drawCircle(30, 25, 5, WHITE);
    hw.display.fillCircle(30, 45, 3, channelA.state ? BLACK : WHITE);
    hw.display.drawCircle(30, 45, 5, WHITE);
    hw.display.setCursor(10, 52);
    //hw.display.print(channelA.state ? "ON " : "OFF");
  } else if (channelA.currentMenu == SWING) {
    // Swing mode indicator for Channel A
    int barLengthA = (channelA.swingAmount - 0.5) * 200;
    hw.display.drawRoundRect(10, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(10, 58, barLengthA, 4, 2, WHITE);
  } else if (channelA.currentMenu == FACTOR) {
    // Factor mode indicator for Channel A
    hw.display.setCursor(24, 30);
    hw.display.setTextSize(2);
    hw.display.print(channelA.factor);
    hw.display.setTextSize(1);
  }

  if (channelB.currentMenu == PROB) {
    drawDottedLine(94, 54, 10, 4);  // center line
    // bar for Channel B (centered at 50% and fill left or right)
    int barLengthB = (channelB.probability - 0.5) * 40;
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    if (barLengthB > 0) {
      hw.display.fillRoundRect(94, 58, barLengthB, 4, 2, WHITE);
    } else {
      hw.display.fillRoundRect(94 + barLengthB, 58, -barLengthB, 4, 2, WHITE);
    }
  } else if (channelB.currentMenu == TOGGLE) {
    // Toggle mode indicator for Channel B
    hw.display.fillCircle(96, 25, 3, channelB.state ? WHITE : BLACK);
    hw.display.drawCircle(96, 25, 5, WHITE);
    hw.display.fillCircle(96, 45, 3, channelB.state ? BLACK : WHITE);
    hw.display.drawCircle(96, 45, 5, WHITE);
    hw.display.setCursor(74, 52);
    //hw.display.print(channelB.state ? "ON " : "OFF");
  } else if (channelB.currentMenu == SWING) {
    // Swing mode indicator for Channel B
    int barLengthB = (channelB.swingAmount - 0.5) * 200;
    hw.display.drawRoundRect(74, 56, 40, 8, 2, WHITE);
    hw.display.fillRoundRect(74, 58, barLengthB, 4, 2, WHITE);
  } else if (channelB.currentMenu == FACTOR) {
    // Factor mode indicator for Channel B
    hw.display.setCursor(94, 30);
    hw.display.setTextSize(2);
    hw.display.print(channelB.factor);
    hw.display.setTextSize(1);
  }
}

// top bar menu
void drawTopBar() {
  hw.display.setCursor(0, 0);

  const char* modeStrA = channelA.currentMenu == PROB ? "Buds" : (channelA.currentMenu == TOGGLE ? "Toggle" : (channelA.currentMenu == SWING ? "Swing" : "Factor"));
  const char* modeStrB = channelB.currentMenu == PROB ? "Buds" : (channelB.currentMenu == TOGGLE ? "Toggle" : (channelB.currentMenu == SWING ? "Swing" : "Factor"));

  if (isChannelASelected) {
    hw.display.setCursor(16, 1);
    hw.display.print(modeStrA);
    hw.display.fillRoundRect(10, 0, 44, 12, 3, INVERSE);  // Invert the mode description for Channel A
    hw.display.setCursor(80, 1);
    hw.display.print(modeStrB);
  } else {
    hw.display.setCursor(80, 1);
    hw.display.print(modeStrB);
    hw.display.fillRoundRect(74, 0, 44, 12, 3, INVERSE);  // Invert the mode description for Channel B
    hw.display.setCursor(16, 1);
    hw.display.print(modeStrA);
  }
}

// swing mode UI
void drawSwingUI(ChannelConfig& channel, int x) {
  hw.display.setCursor(x - 10, 16);
  hw.display.print(channel.swingAmount * 100, 0);
  hw.display.print("%");

  // knob for swing amount
  drawKnob(x, 38, (channel.swingAmount - 0.5) / 0.2);  // Normalized to 0-1 range
}

void drawKnob(int x, int y, float value) {
  hw.display.drawCircle(x, y, 5, WHITE);  
  hw.display.drawCircle(x, y, 8, WHITE);  // fancy knob

  // Calculate angle based on value (0 to 1)
  float angle = value * 2 * PI - PI / 2;
  int x1 = x + 10 * cos(angle);  // Calculate x position of the line
  int y1 = y + 10 * sin(angle);  // Calculate y position of the line

  hw.display.drawLine(x, y, x1, y1, WHITE);  // knob position
}

void updateOLED() {
  hw.display.clearDisplay();
  drawTopBar();
  drawDottedLine(64, 0, 64, 2);  // Centerline

  if (channelA.currentMenu == PROB) {
    hw.display.setCursor(24, 16);
    hw.display.print(channelA.probability * 100, 0);
    hw.display.print("% ");
    drawTree(32, 48, channelA.probability);
  } else if (channelA.currentMenu == SWING) {
    drawSwingUI(channelA, 32);
  } 

  if (channelB.currentMenu == PROB) {
    hw.display.setCursor(88, 16);
    hw.display.print(channelB.probability * 100, 0);
    hw.display.print("% ");
    drawTree(96, 48, channelB.probability);
  } else if (channelB.currentMenu == SWING) {
    drawSwingUI(channelB, 96);
  } 

  drawBottomBar();
  hw.display.display();
}

// save state to EEPROM
void saveState() {
  EEPROM.put(0, channelA);
  EEPROM.put(sizeof(ChannelConfig), channelB);
}

// load state
void loadState() {
  EEPROM.get(0, channelA);
  EEPROM.get(sizeof(ChannelConfig), channelB);
}

void onEncoderIdle() {
  isEncoderIdle = true;
}

void onEncoderClicked() {
  isEncoderIdle = false;
  isChannelASelected = !isChannelASelected;
}

void onEncoderDoubleClicked() {
  isEncoderIdle = false;
  isChannelASelected = !isChannelASelected;  // Switch sides
}

void onEncoderLongClicked() {
  isEncoderIdle = false;
  if (isChannelASelected) {
    channelA.state = !channelA.state;  // Mute/unmute channel A
  } else {
    channelB.state = !channelB.state;  // Mute/unmute channel B
  }
}

void onEncoderPressedRotation() {
  isEncoderIdle = false;
  if (isChannelASelected) {
    channelA.currentMenu = static_cast<Menu>((channelA.currentMenu + 1) % MENU_COUNT);
  } else {
    channelB.currentMenu = static_cast<Menu>((channelB.currentMenu + 1) % MENU_COUNT);
  }
}

void onEncoderRotation(EncoderButton& eb) {
  isEncoderIdle = false;
  int increment = encoder.increment();  // Get the incremental change (could be negative, positive, or zero)
  if (increment == 0) return;

  int acceleratedIncrement = increment * increment;  // Squaring the increment
  if (increment < 0) {
    acceleratedIncrement = -acceleratedIncrement;  // Ensure that the direction of increment is preserved
  }

  if (isChannelASelected) {
    switch (channelA.currentMenu) {
      case PROB:
        channelA.probability = constrain(channelA.probability + acceleratedIncrement * 0.02, 0.0, 1.0);
        break;
      case TOGGLE:
        channelA.state = !channelA.state;
        break;
      case SWING:
        channelA.swingAmount = constrain(channelA.swingAmount + acceleratedIncrement * 0.01, 0.0, 0.7);
        break;
      case FACTOR:
        channelA.factor = constrain(channelA.factor + acceleratedIncrement, 1, 8);
        break;
      default:
        break;
    }
  } else {
    switch (channelB.currentMenu) {
      case PROB:
        channelB.probability = constrain(channelB.probability + acceleratedIncrement * 0.02, 0.0, 1.0);
        break;
      case TOGGLE:
        channelB.state = !channelB.state;
        break;
      case SWING:
        channelB.swingAmount = constrain(channelB.swingAmount + acceleratedIncrement * 0.01, 0.0, 0.7);
        break;
      case FACTOR:
        channelB.factor = constrain(channelB.factor + acceleratedIncrement, 1, 8);
        break;
      default:
        break;
    }
  }
}

void triggerOutput(int output, bool state) {
  if (state) {
    hw.outputs[output].High();
  } else {
    hw.outputs[output].Low();
  }
}

void handleTrigger(ChannelConfig& channel, bool trigger, int mainOutput, int invOutput, int clockOutput) {
  if (trigger) {
    if (channel.currentMenu == TOGGLE) {
      channel.state = !channel.state;
      triggerOutput(mainOutput, channel.state);
      triggerOutput(invOutput, !channel.state);
    } else if (channel.currentMenu == SWING) {
      if (channel.swingState) {
        unsigned long delayTime = map(channel.swingAmount * 100, 0, 70, -150, 150);  // Convert swing amount to milliseconds
        if (millis() - channel.lastSwingTime >= abs(delayTime)) {
          triggerOutput(mainOutput, true);
          triggerOutput(invOutput, false);
          channel.lastSwingTime = millis();
          channel.swingState = false;
        }
      } else {
        unsigned long delayTime = map(channel.swingAmount * 100, 0, 70, -50, 50);  // Convert swing amount to milliseconds
        if (delayTime < 0) {
          delayMicroseconds(abs(delayTime) * 1000);  // Delay for negative swing
        }
        triggerOutput(mainOutput, true);
        triggerOutput(invOutput, false);
        channel.lastSwingTime = millis();
        channel.swingState = true;
      }
    } else if (channel.currentMenu == PROB) {
      channel.state = (random(100) < (int)(channel.probability * 100));
      triggerOutput(mainOutput, channel.state);
      triggerOutput(invOutput, !channel.state);
    } else if (channel.currentMenu == FACTOR) {
      channel.triggerCount++;
      if (channel.triggerCount >= channel.factor) {
        channel.triggerCount = 0;
        triggerOutput(mainOutput, true);
        triggerOutput(invOutput, false);
      }
    }
    channel.lastTriggerTime = millis();
    channel.triggerActive = true;
    triggerOutput(clockOutput, true);
  }
  // Turn off the outputs after the pulse duration
  if (channel.triggerActive && millis() - channel.lastTriggerTime >= TRIGGER_PULSE_DURATION) {
    triggerOutput(clockOutput, false);
    if (channel.currentMenu != TOGGLE) { // Only turn off outputs in non-toggle modes
      triggerOutput(mainOutput, false);
      triggerOutput(invOutput, false);
    }
    channel.triggerActive = false;
  }
}

void loop() {
  hw.ProcessInputs();

  bool triggerA = hw.rst.State() == DigitalInput::STATE_RISING;
  bool triggerB = hw.clk.State() == DigitalInput::STATE_RISING;

  handleTrigger(channelA, triggerA, 1, 0, 2);
  handleTrigger(channelB, triggerB, 4, 3, 5);

  // Update encoder state
  encoder.update();

  // Throttle OLED update rate
  unsigned long currentTime = millis();
  unsigned long updateInterval = isEncoderIdle ? OLED_UPDATE_INTERVAL_IDLE : OLED_UPDATE_INTERVAL_NORMAL;

  // updating on triggerA only bc doin both inputs is a bad thing for performance
    if (currentTime - lastUpdateTime >= updateInterval || triggerA ) {
    lastUpdateTime = currentTime;
    updateOLED();
  }
}
