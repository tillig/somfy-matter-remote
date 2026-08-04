#include "SomfyController.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <NVSRollingCodeStorage.h>
#include <nvs.h>

#include "../config.h"

// The rolling code store and the Somfy remote are file-scope singletons because
// SomfyRemote takes a RollingCodeStorage pointer by reference for its lifetime,
// and there is exactly one radio in this device.
static NVSRollingCodeStorage rollingCodeStorage(NVS_ROLLING_CODE_NAMESPACE, NVS_ROLLING_CODE_KEY);
static SomfyRemote somfyRemote(EMITTER_GPIO, REMOTE_ID, &rollingCodeStorage);

SomfyController::SomfyController() {}

bool SomfyController::begin() {
    // setSpiPin() must come before Init(): Init() is what calls SPI.begin() with
    // whatever pins are configured at that moment, and the driver latches the
    // bus as initialized afterward. Calling it in the other order silently
    // leaves SPI on the driver's default pins (18/19/23/5 on ESP32), which
    // happen to match this build's defaults and so would hide the mistake until
    // a pin is changed.
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);
    ELECHOUSE_cc1101.setGDO0(EMITTER_GPIO);
    ELECHOUSE_cc1101.Init();

    // These two write chip registers over SPI, so they have to run after Init()
    // has brought the bus up and reset the radio. Init() applies the driver's
    // default 433.92 MHz, which is the wrong band for North American Somfy, so
    // setMHZ() here is what actually lands the device on 433.42 MHz.
    ELECHOUSE_cc1101.setModulation(2); // 2 = ASK/OOK, which Somfy RTS uses
    ELECHOUSE_cc1101.setMHZ(FREQUENCY_MHZ);

    // The emitter setup must come LAST, after every CC1101 call that touches the
    // GDO0 pin mode. setGDO0() calls pinMode(GDO0, INPUT) internally, because the
    // driver is written for receiving, where GDO0 is a status output from the
    // radio. This design is the reverse: the Somfy library bit-bangs the waveform
    // INTO the radio, so the pin has to be an ESP32 output.
    //
    // Order matters more than it looks, and the failure is silent. A pin left in
    // INPUT mode still counts as a registered GPIO, so digitalWrite() neither
    // warns nor errors: it sets the output latch on a pin whose output driver is
    // disabled, and nothing reaches the physical line. Every frame would be
    // timed out perfectly in software and transmit nothing. Calling setup() here
    // re-establishes OUTPUT and idles the line LOW.
    somfyRemote.setup();

    // getCC1101() reads the chip version register over SPI; a false result
    // means the radio is not wired correctly or not powered.
    radioReady = ELECHOUSE_cc1101.getCC1101();
    return radioReady;
}

// cppcheck-suppress functionStatic ; part of the SomfyController instance API,
// even though the CC1101 and SomfyRemote it drives are file-scope singletons.
void SomfyController::send(Command command) {
    // Key the carrier, let the Somfy library bit-bang the frame onto GDO0
    // (including sync pulses and the rolling code), then return to idle.
    ELECHOUSE_cc1101.SetTx();
    somfyRemote.sendCommand(command);
    ELECHOUSE_cc1101.setSidle();
}

void SomfyController::retract() {
    send(Command::Up);
}

void SomfyController::extend() {
    send(Command::Down);
}

void SomfyController::stop() {
    send(Command::My);
}

void SomfyController::pair() {
    send(Command::Prog);
}

// cppcheck-suppress functionStatic ; part of the SomfyController instance API,
// even though the rolling code lives in a shared NVS namespace.
uint16_t SomfyController::peekRollingCode() const {
    // Read the value the Somfy library stores, without the increment that
    // nextCode() performs. The library seeds it to 1 on first use, so a result
    // of 0 means "not yet sent".
    //
    // This goes to the NVS API directly rather than through Preferences on
    // purpose. Before the first command is sent, the Somfy library has never
    // written this namespace, so it does not exist yet; a read-only open of a
    // missing namespace is normal here, but Preferences::begin() logs it at
    // error level ("nvs_open failed: NOT_FOUND") with no way to suppress it.
    // That put a scary-looking error in front of every first-run user on a
    // healthy device. Handling ESP_ERR_NVS_NOT_FOUND here keeps the log clean
    // while still surfacing genuine failures.
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_ROLLING_CODE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0; // namespace absent: nothing has been transmitted yet
    }
    if (err != ESP_OK) {
        Serial.printf("[rf] Could not read the rolling code: %s\n", esp_err_to_name(err));
        return 0;
    }

    uint16_t code = 0;
    err = nvs_get_u16(handle, NVS_ROLLING_CODE_KEY, &code);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0; // namespace exists but this key does not
    }
    if (err != ESP_OK) {
        Serial.printf("[rf] Could not read the rolling code: %s\n", esp_err_to_name(err));
        return 0;
    }
    return code;
}
