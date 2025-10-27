/**
 * @file arythmatik_peripherials.h
 * @author Adam Wonak (https://github.com/awonak)
 * @brief Arduino pin definitions for the Modulove A-RYTH-MATIC module.
 * @version 0.1
 * @date 2023-09-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef ARYTHMATIK_PERIPHERIALS_H
#define ARYTHMATIK_PERIPHERIALS_H

namespace modulove {
namespace arythmatik {

// OLED Display config
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Peripheral input pins
#define ENCODER_PIN1 2
#define ENCODER_PIN2 3
#define ENCODER_SW_PIN 12

// Default panel orientation.
#define CLK_PIN 11
#define RST_PIN 13
// Rotated panel.
#define CLK_PIN_ROTATED 13
#define RST_PIN_ROTATED 11

// Output Pins
#define CLOCK_LED 4
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

// Output Pins for rotated panel.
#define OUT_CH1_ROTATED 8
#define OUT_CH2_ROTATED 9
#define OUT_CH3_ROTATED 10
#define OUT_CH4_ROTATED 1
#define OUT_CH5_ROTATED 6
#define OUT_CH6_ROTATED 7
#define LED_CH1_ROTATED 0
#define LED_CH2_ROTATED 1
#define LED_CH3_ROTATED 17
#define LED_CH4_ROTATED 14
#define LED_CH5_ROTATED 15
#define LED_CH6_ROTATED 16

const uint8_t OUTPUT_COUNT = 6;

}  // namespace arythmatik
}  // namespace modulove

#endif
