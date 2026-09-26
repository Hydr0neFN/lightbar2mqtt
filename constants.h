#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>
#include <Arduino.h>

namespace constants
{
    // The version number of lightbar2mqtt.
    const String VERSION = "0.3.2";

    // The maximum number of light bars that can be connected to the controller.
    const uint8_t MAX_LIGHTBARS = 10;

    // The maximum number of remotes that can be connected to the controller.
    const uint8_t MAX_REMOTES = 10;

    // The maximum number of serials, the controller will be able to save latest package ids for.
    // This should always >= MAX_REMOTES + MAX_LIGHTBARS.
    const uint8_t MAX_SERIALS = 32;

    // The maximum number of command listeners that can be registered for a remote.
    const uint8_t MAX_COMMAND_LISTENERS = 10;

    // How often a command packet is repeated (10 ms apart). The bar ignores repeats of a
    // package id it has already seen, so repeating is safe even for the ON_OFF toggle.
    const uint8_t SEND_REPEATS = 20;

    // Repeats used when switching the bar on. Once off, the bar's receiver sleeps and needs
    // roughly a second to wake, so a 200 ms burst lands before it is listening, the toggle is
    // lost, and HA's optimistic state inverts. 150 x 10 ms = 1.5 s spans the wake-up.
    const uint8_t WAKE_SEND_REPEATS = 150;
};

struct SerialWithName
{
    uint32_t serial;
    const char *name;
};

#endif