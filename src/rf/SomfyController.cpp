#include "SomfyController.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <NVSRollingCodeStorage.h>

#include "../config.h"

// The rolling code store and the Somfy remote are file-scope singletons because
// SomfyRemote takes a RollingCodeStorage pointer by reference for its lifetime,
// and there is exactly one radio in this device.
static NVSRollingCodeStorage rollingCodeStorage(NVS_ROLLING_CODE_NAMESPACE, NVS_ROLLING_CODE_KEY);
static SomfyRemote somfyRemote(EMITTER_GPIO, REMOTE_ID, &rollingCodeStorage);

SomfyController::SomfyController() {}

bool SomfyController::begin() {
    // Bring up the Somfy emitter first so the GDO0 pin is configured as an
    // output before the CC1101 is told to key on it.
    somfyRemote.setup();

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);
    ELECHOUSE_cc1101.setGDO0(EMITTER_GPIO);
    ELECHOUSE_cc1101.setModulation(2); // 2 = ASK/OOK, which Somfy RTS uses
    ELECHOUSE_cc1101.setMHZ(FREQUENCY_MHZ);

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
