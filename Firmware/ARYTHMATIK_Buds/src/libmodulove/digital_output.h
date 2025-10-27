/**
 * @file digital_output.h
 * @author Adam Wonak (https://github.com/awonak)
 * @brief Class for interacting with trigger / gate outputs.
 * @version 0.1
 * @date 2023-09-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef DIGITAL_OUTPUT_H
#define DIGITAL_OUTPUT_H

#include <Arduino.h>

namespace modulove {

class DigitalOutput {
   public:
    /**
     * @brief Initializes an CV Output paired object.
     * 
     * @param cv_pin gpio pin for the cv output
     */
    void Init(uint8_t cv_pin) {
        pinMode(cv_pin, OUTPUT);  // Gate/Trigger Output
        cv_pin_ = cv_pin;
    }
    
    /**
     * @brief Initializes an LED & CV Output paired object.
     * 
     * @param cv_pin gpio pin for the cv output
     * @param led_pin gpio pin for the LED
     */
    void Init(uint8_t cv_pin, uint8_t led_pin) {
        pinMode(led_pin, OUTPUT);  // LED
        led_pin_ = led_pin;
        #define LED_PIN_DEFINED
        Init(cv_pin);
    }

    /**
     * @brief Turn the CV and LED on or off according to the input state.
     * 
     * @param state Arduino digital HIGH or LOW values.
     */
    inline void Update(uint8_t state) {
        if (state == HIGH) High();  // Rising
        if (state == LOW) Low();    // Falling
    }

    /// @brief Sets the cv output HIGH to about 5v.
    inline void High() { update(HIGH); }

    /// @brief Sets the cv output LOW to 0v.
    inline void Low() { update(LOW); }

    /**
     * @brief Return a bool representing the on/off state of the output.
     * 
     * @return true if current cv state is high
     * @return false if current cv state is low
     */
    inline bool On() { return on_; }

   private:
    uint8_t cv_pin_;
    uint8_t led_pin_;
    bool on_;

    void update(uint8_t state) {
        digitalWrite(cv_pin_, state);
        #ifdef LED_PIN_DEFINED
        digitalWrite(led_pin_, state);
        #endif
        on_ = state == HIGH;
    }
};

}  // namespace modulove

#endif
