# Modulove Hardware Abstraction Library

This package provides a library for creating scripts for the Modulove [A-RYTH-MATIK](https://modulove.io/arythmatik/) eurorack module.

Full Doxyen generated documentation of the library can be found here: [https://awonak.github.io/libModulove/](https://awonak.github.io/libModulove/).

## Installation instructions

There are two ways to install. Include the library directly in your script
repo as a **git submodule**, or **download** the latest release zip and extract the
library into your `~/Arduino/libraries` folder.

**Include the library as a git submodule to use in your scripts.**

In order to include the library source code directly in your repo as a git
submodule, you must follow the Arduino Sketch specifications and place the
code in the location `<sketch>/src/<library>`. This is documented in the
[src subfolder](https://arduino.github.io/arduino-cli/0.34/sketch-specification/#src-subfolder)
section of the Arduino Sketch Specification.

```bash
git submodule add https://github.com/awonak/libmodulove.git <sketch>/src/libmodulove
```

**Download the latest release.**

[TODO]

## Example usage

```cpp
#include "src/libmodulove/arythmatik.h"

using namespace modulove;
using namespace arythmatik;

// Declare A-RYTH-MATIK hardware variable.
Arythmatik hw;

byte counter = 0;

void setup() {
    // Inside the setup, set config values prior to calling hw.Init().
    #ifdef ROTATE_PANEL
        hw.config.RotatePanel = true;
    #endif

    #ifdef REVERSE_ENCODER
        hw.config.ReverseEncoder = true;
    #endif

    // Initialize the A-RYTH-MATIK peripherials.
    hw.Init();
}

void loop() {
    // Read cv inputs and process encoder state to determine state for this loop.
    hw.ProcessInputs();

    // Advance the counter on CLK input
    if (hw.clk.State() == DigitalInput::STATE_RISING) {
        counter = ++counter % OUTPUT_COUNT;
    }

    // Read encoder for a change in direction and update the counter.
    Encoder::Direction dir = hw.encoder.rotate();
    if (dir == Encoder::DIRECTION_INCREMENT) {
        counter = min(++counter, OUTPUT_COUNT);
    } else if (dir == Encoder::DIRECTION_DECREMENT) {
        counter = max(--counter, 0);
    }

    // Reset the counter back to 0 when encoder switch pressed.
    Encoder::PressType press = hw.encoder.Pressed();
    if (press == Encoder::PRESS_SHORT) {
        counter = 0;
    }

    // Activate the current counter output.
    for (int i = 0; i < OUTPUT_COUNT; i++) {
        (i == counter)
            ? hw.outputs[i].High()
            : hw.outputs[i].Low();
    }

    // Display the current counter step on the OLED.
    hw.display.clearDisplay();
    hw.display.setCursor(SCREEN_HEIGHT/2, 0);
    hw.display.println("Counter: " + String(counter));
    hw.display.display();
}
```

### Third-party Arduino Libraries

* [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)
* [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
* [Simple Rotary](https://github.com/mprograms/SimpleRotary/tree/master)
