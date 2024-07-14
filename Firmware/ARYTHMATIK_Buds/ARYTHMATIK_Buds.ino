#include "src/libmodulove/arythmatik.h"
#include <EEPROM.h>
#include "EncoderButton.h"

//#define ENCODER_REVERSED
//#define ROTATE_PANEL
//#define DISABLE_BOOT_LOGO

using namespace modulove;
using namespace arythmatik;

const unsigned long OLED_UPDATE_INTERVAL = 125;  // OLED update interval in milliseconds

// Enum for menu items
enum Menu {
  PROB,
  //TOGGLE,
  //LATCH,
  SWING,
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

// Struct for channel configuration
struct ChannelConfig {
  float probability = 0.5;
  bool state = false;
  bool toggleMode = false;
  bool latchMode = false;
  float swingAmount = 0.5;  // 0.5 means no swing, range 0.5 to 0.7
  bool swingMode = false;   // true: swing mode active
  unsigned long lastSwingTime = 0;
  bool swingState = false;
  Menu currentMenu = PROB;  // Add the current menu mode for each channel
};

// Create configurations for both channels
ChannelConfig channelA;
ChannelConfig channelB;

bool isChannelASelected = true;

unsigned long lastTriggerTime = 0;
unsigned long lastUpdateTime = 0;

void setup() {

#ifdef ROTATE_PANEL
  hw.config.RotatePanel = true;
#endif

#ifdef REVERSE_ENCODER
  hw.config.ReverseEncoder = true;
#endif

  // Initialize the A-RYTH-MATIK peripherals.
  hw.Init();

  // Set up encoder parameters
  //encoder.setDebounceInterval(5);  // Increase debounce interval
  //encoder.setMultiClickInterval(10);
  //encoder.setRateLimit(10);
  encoder.setLongClickDuration(500);
  encoder.setClickHandler(onEncoderClicked);
  encoder.setLongClickHandler(onEncoderLongClicked);  // Add long click handler
  encoder.setEncoderHandler(onEncoderRotation);
}

// abstract tree
void drawTree(int x, int y, float probability) {
  // main branch
  hw.display.drawLine(x, y, x, y - 10, WHITE);

  // primary twigs
  hw.display.drawLine(x, y - 5, x - 5, y - 15, WHITE);
  hw.display.drawLine(x, y - 5, x + 5, y - 15, WHITE);

  // two dots based on probability
  if (probability <= 0.5) {
    hw.display.fillCircle(x - 5, y - 15, 2, WHITE);
    hw.display.drawCircle(x + 5, y - 15, 2, WHITE);
  } else {
    hw.display.drawCircle(x - 5, y - 15, 2, WHITE);
    hw.display.fillCircle(x + 5, y - 15, 2, WHITE);
  }
}

// bottom bar indicator
void drawBottomBar() {
  hw.display.setCursor(0, 56);

  if (channelA.currentMenu == PROB) {

    // taller center line for 50% in prob mode
    hw.display.drawLine(30, 54, 30, 64, WHITE);  // Taller center line for visualization
    //hw.display.drawLine(94, 54, 94, 64, WHITE); // Taller center line for visualization
    // bar for Channel A (centered at 50% and fill left or right)
    int barLengthA = (channelA.probability - 0.5) * 40;
    hw.display.drawRect(10, 56, 40, 6, WHITE);
    if (barLengthA > 0) {
      hw.display.fillRect(30, 56, barLengthA, 6, WHITE);
    } else {
      hw.display.fillRect(30 + barLengthA, 56, -barLengthA, 6, WHITE);
    }
  } else if (channelA.currentMenu == SWING) {
    // bar for Swing A (50% to 70%)
    int barLengthA = (channelA.swingAmount - 0.5) * 200;
    hw.display.drawRect(10, 56, 40, 6, WHITE);
    hw.display.fillRect(10, 56, barLengthA, 6, WHITE);
  }

  if (channelB.currentMenu == PROB) {
    //hw.display.drawLine(30, 54, 30, 64, WHITE); // Taller center line for visualization
    hw.display.drawLine(94, 54, 94, 64, WHITE);  // Taller center line for visualization
    // bar for Channel B (centered at 50% and fill left or right)
    int barLengthB = (channelB.probability - 0.5) * 40;
    hw.display.drawRect(74, 56, 40, 6, WHITE);
    if (barLengthB > 0) {
      hw.display.fillRect(94, 56, barLengthB, 6, WHITE);
    } else {
      hw.display.fillRect(94 + barLengthB, 56, -barLengthB, 6, WHITE);
    }
  } else if (channelB.currentMenu == SWING) {
    // bar for Swing B (50% to 70%)
    int barLengthB = (channelB.swingAmount - 0.5) * 200;
    hw.display.drawRect(74, 56, 40, 6, WHITE);
    hw.display.fillRect(74, 56, barLengthB, 6, WHITE);
  }
}

// top bar menu
void drawTopBar() {
  hw.display.setCursor(0, 0);

  const char* modeStrA = channelA.currentMenu == PROB ? "Prob" : "Swing";
  const char* modeStrB = channelB.currentMenu == PROB ? "Prob" : "Swing";





  if (isChannelASelected) {
    hw.display.setCursor(16, 1);
    hw.display.print(modeStrA);
    hw.display.fillRect(0, 0, 64, 12, INVERSE);  // Invert the mode description for Channel A
    hw.display.setCursor(80, 1);
    hw.display.print(modeStrB);
  } else {
    hw.display.setCursor(80, 0);
    hw.display.print(modeStrB);
    hw.display.fillRect(64, 0, 64, 12, INVERSE);  // Invert the mode description for Channel B
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
  hw.display.drawCircle(x, y, 10, WHITE);  // Increased size of the knob

  // Calculate angle based on value (0 to 1)
  float angle = value * 2 * PI - PI / 2;
  int x1 = x + 10 * cos(angle);  // Calculate x position of the line
  int y1 = y + 10 * sin(angle);  // Calculate y position of the line

  hw.display.drawLine(x, y, x1, y1, WHITE);  // line indicating the position
}

// update the OLED display
void updateOLED() {
  hw.display.clearDisplay();
  drawTopBar();
  hw.display.drawLine(64, 0, 64, 64, WHITE);  // Centerline

  if (channelA.currentMenu == SWING) {
    drawSwingUI(channelA, 32);
  } else {
    hw.display.setCursor(24, 16);
    //
    hw.display.print(channelA.probability * 100, 0);
    hw.display.print("% ");
    drawTree(32, 48, channelA.probability);
  }

  if (channelB.currentMenu == SWING) {
    drawSwingUI(channelB, 96);
  } else {
    hw.display.setCursor(88, 16);
    hw.display.print(channelB.probability * 100, 0);
    hw.display.print("% ");
    drawTree(96, 48, channelB.probability);
  }

  drawBottomBar();
  hw.display.display();
}

// save the state to EEPROM
void saveState() {
  EEPROM.put(0, channelA);
  EEPROM.put(sizeof(ChannelConfig), channelB);
}

// load the state from EEPROM
void loadState() {
  EEPROM.get(0, channelA);
  EEPROM.get(sizeof(ChannelConfig), channelB);
}

void onEncoderClicked() {
  isChannelASelected = !isChannelASelected;
}

void onEncoderLongClicked() {
  if (isChannelASelected) {
    channelA.currentMenu = static_cast<Menu>((channelA.currentMenu + 1) % MENU_COUNT);
  } else {
    channelB.currentMenu = static_cast<Menu>((channelB.currentMenu + 1) % MENU_COUNT);
  }
}

void onEncoderRotation(EncoderButton& eb) {
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
      case SWING:
        channelA.swingAmount = constrain(channelA.swingAmount + acceleratedIncrement * 0.01, 0.5, 0.7);
        break;
      default:
        break;
    }
  } else {
    switch (channelB.currentMenu) {
      case PROB:
        channelB.probability = constrain(channelB.probability + acceleratedIncrement * 0.02, 0.0, 1.0);
        break;
      case SWING:
        channelB.swingAmount = constrain(channelB.swingAmount + acceleratedIncrement * 0.01, 0.5, 0.7);
        break;
      default:
        break;
    }
  }

  //updateOLED();  // Refresh display after changing values (this should not be here i guess for performance reasons?)
}

void triggerOutput(int output, int duration = 10) {
  hw.outputs[output].High();
  delayMicroseconds(duration);  // Use delayMicroseconds for a more precise and non-blocking delay
  hw.outputs[output].Low();
}


// Swing mode logic (is this mathematical or musical swing ?)
void handleSwing(ChannelConfig& channel, bool trigger, int output) {
  if (channel.swingMode && trigger) {
    if (channel.swingState) {
      channel.swingState = false;
      unsigned long delayTime = channel.swingAmount * 1000;  // Convert to milliseconds
      if (millis() - channel.lastSwingTime >= delayTime) {
        triggerOutput(output);
      }
    } else {
      channel.swingState = true;
      channel.lastSwingTime = millis();
    }
  }
}

void handleTrigger(ChannelConfig& channel, bool trigger, int output, int clockOutput) {
  if (trigger) {
    lastTriggerTime = millis();
    if (channel.toggleMode) {
      channel.state = !channel.state;
    } else if (channel.latchMode) {
      channel.state = !channel.state;
    } else {
      channel.state = (random(100) < (int)(channel.probability * 100));
    }
    if (channel.state) {
      triggerOutput(output);
    }
    triggerOutput(clockOutput);
  }
}

void loop() {
  hw.ProcessInputs();

  bool triggerA = hw.rst.State() == DigitalInput::STATE_RISING;
  bool triggerB = hw.clk.State() == DigitalInput::STATE_RISING;



  // Swing mode logic for Channel A
  handleSwing(channelA, triggerA, 1);

  // Swing mode logic for Channel B
  handleSwing(channelB, triggerB, 4);

  handleTrigger(channelA, triggerA, 0, 2);
  handleTrigger(channelB, triggerB, 3, 5);

  // Update encoder state
  encoder.update();

  // Throttle the OLED update rate
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= OLED_UPDATE_INTERVAL || triggerA || triggerB) {
    lastUpdateTime = currentTime;
    updateOLED();
  }
}
