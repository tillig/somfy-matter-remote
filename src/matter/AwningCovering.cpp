#include "AwningCovering.h"

#include "../config.h"
#include "../rf/SomfyController.h"
#include "../storage/ConfigStore.h"

// Matter lift convention: 0% = fully open (awning retracted), 100% = fully
// closed (awning extended).
static constexpr uint8_t LIFT_OPEN = 0;
static constexpr uint8_t LIFT_CLOSED = 100;

AwningCovering::AwningCovering(SomfyController& rf, ConfigStore& store) : rf(rf), store(store) {}

void AwningCovering::begin() {
    const uint8_t restoredLift = store.getLiftPercent();

    // Start the endpoint as an AWNING covering with the restored lift position.
    // No tilt: an awning is a lift-only covering.
    covering.begin(restoredLift, 0, MatterWindowCovering::AWNING);

    // "Open" in Matter means lift to 0% (retract). With INVERT_DIRECTION the
    // physical direction is flipped while the reported state stays convention-
    // correct, so Google Home's tile still reads right.
    covering.onOpen([this]() -> bool {
        INVERT_DIRECTION ? rf.extend() : rf.retract();
        moveToEndStop(LIFT_OPEN);
        return true;
    });

    covering.onClose([this]() -> bool {
        INVERT_DIRECTION ? rf.retract() : rf.extend();
        moveToEndStop(LIFT_CLOSED);
        return true;
    });

    covering.onStop([this]() -> bool {
        rf.stop();
        return true;
    });

    // A Somfy awning has no intermediate position control by default. Treat any
    // GoToLiftPercentage as a bang-bang decision: values in the lower half open
    // (retract), the rest close (extend). This keeps slider drags and mid-value
    // routines working sensibly without pretending to have real positioning.
    covering.onGoToLiftPercentage([this](uint8_t target) -> bool {
        if (target < 50) {
            INVERT_DIRECTION ? rf.extend() : rf.retract();
            moveToEndStop(LIFT_OPEN);
        } else {
            INVERT_DIRECTION ? rf.retract() : rf.extend();
            moveToEndStop(LIFT_CLOSED);
        }
        return true;
    });

    wasCommissioned = Matter.isDeviceCommissioned();

    Matter.begin();

    if (Matter.isDeviceCommissioned()) {
        Serial.println("[matter] Already commissioned; joining existing fabric.");
    } else {
        Serial.println("[matter] Not commissioned. Add this device in Google Home:");
        Serial.print("[matter]   Manual pairing code: ");
        Serial.println(Matter.getManualPairingCode());
        Serial.print("[matter]   QR code URL: ");
        Serial.println(Matter.getOnboardingQRCodeUrl());
        Serial.println("[matter] Accept the \"uncertified device\" warning during setup.");
    }
}

void AwningCovering::loop() {
    // Surface commissioning transitions to the serial log for bench debugging.
    const bool commissioned = Matter.isDeviceCommissioned();
    if (commissioned && !wasCommissioned) {
        Serial.println("[matter] Commissioning complete.");
    }
    wasCommissioned = commissioned;
}

// cppcheck-suppress functionStatic ; part of the AwningCovering instance API,
// even though it delegates to the global Matter singleton.
bool AwningCovering::isCommissioned() const {
    return Matter.isDeviceCommissioned();
}

// cppcheck-suppress functionStatic ; part of the AwningCovering instance API,
// even though it delegates to the global Matter singleton.
void AwningCovering::decommission() {
    Serial.println("[matter] Decommissioning and factory-resetting Matter data.");
    Matter.decommission();
}

void AwningCovering::moveToEndStop(uint8_t liftPercent) {
    // Report the new end-stop position back to Matter controllers and persist
    // it so the tile shows a sensible state after a reboot.
    covering.setLiftPercentage(liftPercent);
    covering.updateAccessory();
    store.setLiftPercent(liftPercent);
}
