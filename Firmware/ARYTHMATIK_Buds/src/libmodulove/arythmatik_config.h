/**
 * @file arythmatik_config.h
 * @author Adam Wonak (https://github.com/awonak)
 * @brief collection of configuration settings for the A-RYTH-MATIK..
 * @version 0.1
 * @date 2024-05-04
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef ARYTHMATIK_CONFIG_H
#define ARYTHMATIK_CONFIG_H

namespace modulove {
namespace arythmatik {

// Configuration settings for the A-RYTH-MATIK module.
struct Config {
    // When compiling for the "updside down" A-RYTH-MATIK panel, set this to true.
    bool RotatePanel;

    // Set ReverseEncoder to true if rotating the encoder counterclockwise should increment.
    bool ReverseEncoder;
};

}  // namespace arythmatik
}  // namespace modulove

#endif