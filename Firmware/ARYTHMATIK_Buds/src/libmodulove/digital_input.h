/**
 * @file digital_input.h
 * @author Adam Wonak (https://github.com/awonak)
 * @brief Class for interacting with trigger / gate inputs.
 * @version 0.1
 * @date 2023-09-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef DIGITAL_INPUT_H
#define DIGITAL_INPUT_H

#include <Arduino.h>

namespace modulove {

class DigitalInput {
   public:
    /// @brief Enum constants for clk input rising/falling state.
    enum InputState {
        STATE_UNCHANGED,
        STATE_RISING,
        STATE_FALLING,
    };

    DigitalInput() {}
    ~DigitalInput() {}

    /**
    * @brief Initializes a CV Input object.
    * 
    * @param cv_pin gpio pin for the cv output.
    */
    void Init(uint8_t cv_pin) {
        pinMode(cv_pin, INPUT);
        cv_pin_ = cv_pin;
    }

    /**
     * @brief Read the state of the cv input.
     * 
     */
    void Process() {
        old_read_ = read_;
        read_ = digitalRead(cv_pin_);

        // Determine current clock input state.
        state_ = STATE_UNCHANGED;
        if (old_read_ == 0 && read_ == 1) {
            state_ = STATE_RISING;
            on_ = true;
        } else if (old_read_ == 1 && read_ == 0) {
            state_ = STATE_FALLING;
            on_ = false;
        }
    }

    /**
     * @brief Get the current input state of the digital input.
     * 
     * @return InputState 
     */
    inline InputState State() { return state_; }

    /**
     * @brief Current cv state represented as a bool.
     * 
     * @return true if cv signal is high
     * @return false if cv signal is low
     */
    inline bool On() { return on_; }

   private:
    uint8_t cv_pin_;
    uint8_t read_;
    uint8_t old_read_;
    InputState state_;
    bool on_;
};

}  // namespace modulove

#endif
