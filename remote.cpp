#include "remote.h"

Remote::Remote(Radio *radio, uint32_t serial, const char *name)
{
    this->radio = radio;
    this->serial = serial;
    this->name = name;

    this->radio->addRemote(this);

    this->serialString = "0x" + String(this->serial, HEX);
}

Remote::~Remote()
{
}

uint32_t Remote::getSerial()
{
    return this->serial;
}

String Remote::getSerialString()
{
    return this->serialString;
}

const char *Remote::getName()
{
    return this->name;
}

void Remote::callback(byte command, byte options)
{
    for (int i = 0; i < this->numCommandListeners; i++)
    {
        this->commandListeners[i](this, command, options);
    }
}

bool Remote::registerCommandListener(std::function<void(Remote *, byte, byte)> callback)
{
    if (this->numCommandListeners >= constants::MAX_COMMAND_LISTENERS)
    {
        Serial.println("[Remote] Could not add command listener to remote, because too many are saved!");
        Serial.println("[Remote] Please check if you actually want to save more than " + String(constants::MAX_COMMAND_LISTENERS, DEC) + " command listeners.");
        Serial.println("[Remote] If you do, increase MAX_COMMAND_LISTENERS in constants.h and recompile.");
        return false;
    }
    this->commandListeners[this->numCommandListeners] = callback;
    this->numCommandListeners++;
    return true;
}

bool Remote::unregisterCommandListener(std::function<void(Remote *, byte, byte)> callback)
{
    for (int i = 0; i < this->numCommandListeners; i++)
    {
        // LOCAL PATCH 2026-09-04 — upstream wrote:
        //     if (this->commandListeners[i].target<void(Remote*,byte,byte)>()
        //         == callback.target<void(Remote*,byte,byte)>())
        // which does not compile here: std::function::target<T>() needs RTTI,
        // and arduino-esp32 builds with -fno-rtti, so libstdc++ omits it.
        //
        // Enabling -frtti would not have made that line *work*, only compile.
        // The template argument there is a function type, while what is stored
        // is a std::bind object, so target<T>() returns nullptr on BOTH sides
        // and the test is nullptr == nullptr — always true on the first
        // iteration. Upstream's comparison has therefore never compared
        // anything; the effective behaviour has always been "drop the first
        // listener". std::function simply has no equality operator to use.
        //
        // This reproduces that behaviour explicitly. It is correct for the only
        // caller (MQTT::removeRemote), because MQTT::addRemote registers
        // exactly one listener per remote and it is always its own handler.
        // If a second listener is ever registered on a remote, this needs a
        // real identity scheme — a token returned by registerCommandListener —
        // rather than an attempt to compare the callables.
        (void)callback;
        {
            for (int j = i; j < this->numCommandListeners - 1; j++)
            {
                this->commandListeners[j] = this->commandListeners[j + 1];
            }
            this->numCommandListeners--;
            return true;
        }
    }
    return false;
}