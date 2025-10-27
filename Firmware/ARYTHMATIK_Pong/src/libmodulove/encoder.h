/**
 * @file encoder.h
 * @author Adam Wonak (https://github.com/awonak)
 * @brief Class for interacting with encoder push buttons.
 * @version 0.1
 * @date 2023-09-21
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>

// Encoder & button
#include <SimpleRotary.h>

#include "arythmatik_peripherials.h"

namespace modulove {

class Encoder {
   public:
    /// @brief Enum constants for encoder rotation increment/decrement state.
    enum Direction {
        DIRECTION_UNCHANGED,
        DIRECTION_INCREMENT,
        DIRECTION_DECREMENT,
    };

    ///@brief Enum for type of switch press.
    enum PressType {
        PRESS_NONE,
        PRESS_SHORT,
        PRESS_LONG,
    };

    Encoder() : encoder_(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN) {}
    ~Encoder() {}

    /// @brief Set the encoder direction by passing 0 for cw increment or 1 for ccw increment.
    void setDirection(byte direction) {
        reversed_ = direction == 1;
    }

    /// @brief Get the rotary direction if it has turned.
    /// @return Direction of turn or unchanged.
    Direction Rotate() {
        return (reversed_)
                   ? rotate_reversed()
                   : rotate();
    }

    Direction rotate() {
        switch (encoder_.rotate()) {
            case 1:
                return DIRECTION_INCREMENT;
            case 2:
                return DIRECTION_DECREMENT;
            default:
                return DIRECTION_UNCHANGED;
        }
    }

    Direction rotate_reversed() {
        switch (encoder_.rotate()) {
            case 1:
                return DIRECTION_DECREMENT;
            case 2:
                return DIRECTION_INCREMENT;
            default:
                return DIRECTION_UNCHANGED;
        }
    }

    /// @return Return the press type if the switch was released this loop.
    PressType Pressed() {
        switch (_press()) {
            case 1:
                return PRESS_SHORT;
            case 2:
                return PRESS_LONG;
            default:
                return PRESS_NONE;
        }
    }

    /// @return Return true if the button has been held down for less than (n) milliseconds.
    bool ShortPressed() {
        return Pressed() == PRESS_SHORT;
    }

    /// @return Return true if the button has been held down for (n) milliseconds.
    bool LongPressed() {
        return Pressed() == PRESS_LONG;
    }

   private:
    SimpleRotary encoder_;
    static const int LONG_PRESS_DURATION_MS = 1000;
    byte reversed_ = 0;

    byte _press() {
        // Check for long press to endable editing seed.
        // press and release for < 1 second to return 1 for short press
        // press and release for > 1 second to return 2 for long press.
        return encoder_.pushType(LONG_PRESS_DURATION_MS);
    }
};

}  // namespace modulove

#endif
