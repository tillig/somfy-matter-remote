# Matter Commissioning

- [What Commissioning Does](#what-commissioning-does)
- [Before You Start](#before-you-start)
- [Add The Device To Google Home](#add-the-device-to-google-home)
- [Test Voice And App Control](#test-voice-and-app-control)
- [Reconnection And Recovery](#reconnection-and-recovery)
- [Multi-Admin Sharing](#multi-admin-sharing)
- [Troubleshooting](#troubleshooting)

## What Commissioning Does

Commissioning joins the ESP32 to a Matter fabric so a controller such as Google Home can adopt it as a Window Covering and talk to it directly over the local network. It is done once, over Wi-Fi. There is no hub, driver, or cloud account in the path: the controller commissions the device directly, then communicates peer-to-peer.

This is hardware-gated work. It cannot be validated without the physical device on the network.

## Before You Start

1. Complete [the Somfy pairing procedure](pairing.md) so radio control is already proven. Commissioning only adds the network control surface; it does not help debug the radio.
2. Make sure the phone running the controller app is on the same Wi-Fi network the ESP32 will join.
3. Open the serial monitor at 115200 baud. On first boot, when the device is not yet commissioned, the firmware prints the Matter manual pairing code and the onboarding QR-code URL. Note these.

The firmware uses the Arduino Matter library's built-in test credentials. That is expected for a do-it-yourself device and is why the controller shows an uncertified-device warning during setup.

## Add The Device To Google Home

1. In the Google Home app, choose to add a device, then choose the Matter path (labeled "Matter-enabled device," or scan the QR code).
2. Scan the QR code from the onboarding URL printed to the serial monitor, or enter the manual pairing code.
3. Accept the "uncertified device" warning. This appears for do-it-yourself Matter devices and is not a failure.
4. Let Google Home commission the device, join it to Wi-Fi, and add it as a window covering. Assign it a room and a speakable name, for example "Patio Awning."

The serial log prints a commissioning-complete line when the fabric join succeeds.

## Test Voice And App Control

1. Say "open the awning" and "close the awning," and use the tile's open, close, and stop controls.
2. Confirm the motion matches expectation. With the default mapping, open retracts the awning and close extends it.
3. If the direction feels backward, flip the `INVERT_DIRECTION` build flag, rebuild, and reflash, or simply rename the device and adjust how you phrase commands. See [the direction semantics notes](hardware.md#direction-semantics).

Because a Somfy awning gives no position feedback, the tile reports only the two end states. Dragging the slider or asking for a percentage moves the awning to the nearer end stop rather than to a partial position.

## Reconnection And Recovery

After a power cycle, the device rejoins Wi-Fi and its Matter fabric automatically; it does not need re-commissioning. The last-known position is restored from NVS so the tile shows a sensible state immediately.

To move the device to a different network or controller, or to recover from a bad state, factory-reset Matter with a long press (about 10 seconds) of the panel-mount button. That decommissions the device and clears the fabric data so it can be commissioned again. The Somfy pairing and rolling code are unaffected by a Matter reset; they live in separate storage.

## Multi-Admin Sharing

Matter supports multi-admin, so the same physical device can be shared into Alexa, Apple Home, or SmartThings from within Google Home's device sharing. This is useful if a richer automation engine is wanted later. None of those are required, and the device stays fully functional in Google Home alone.

## Troubleshooting

If Google Home cannot find the device, confirm the phone and the ESP32 are on the same Wi-Fi network and subnet, and that the network allows the IPv6 and mDNS traffic Matter relies on. Many mesh and guest networks block this.

If commissioning starts but fails partway, power-cycle the ESP32 and retry from the serial-printed pairing code. If it still fails, factory-reset Matter with the long button press and try once more.

If voice control does nothing but the tile works, re-check the device name and room assignment in the controller, since voice matching depends on them.
