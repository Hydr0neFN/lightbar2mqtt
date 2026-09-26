#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>
#include <Arduino.h>

namespace constants
{
    // The version number of lightbar2mqtt.
    const String VERSION = "0.3.3";

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

    // Once off, the bar sleeps: the first packet only wakes it and is not executed, and since
    // every repeat carries the same package id, the rest of that burst is dropped as duplicates.
    // Lengthening the burst therefore does not help (tried in 0.3.2). Switching on sends a no-op
    // packet first (DIMMER by 0 steps), waits this long, then sends ON_OFF under a new id.
    const uint16_t WAKE_DELAY_MS = 1000;
};

struct SerialWithName
{
    uint32_t serial;
    const char *name;
};

#endif