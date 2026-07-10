# Matter Commissioning

- [What Commissioning Does](#what-commissioning-does)
- [Connect The Device To Wi-Fi First](#connect-the-device-to-wi-fi-first)
- [Before You Commission](#before-you-commission)
- [Add The Device To Google Home](#add-the-device-to-google-home)
- [Test Voice And App Control](#test-voice-and-app-control)
- [The Web Dashboard](#the-web-dashboard)
- [Reconnection And Recovery](#reconnection-and-recovery)
- [Multi-Admin Sharing](#multi-admin-sharing)
- [Troubleshooting](#troubleshooting)

## What Commissioning Does

Commissioning joins the ESP32 to a Matter fabric so a controller such as Google Home can adopt it as a Window Covering and talk to it directly over the local network. There is no hub, driver, or cloud account in the path: the controller commissions the device directly, then communicates peer-to-peer.

The order matters on this hardware. The classic ESP32 build of the Arduino Matter library does not include Bluetooth (CHIPoBLE) commissioning, so the device cannot receive Wi-Fi credentials from the phone during pairing the way some Matter devices do. Instead, the device must already be on Wi-Fi before commissioning runs. Connecting to Wi-Fi is therefore a separate first step, described next.

This is hardware-gated work. It cannot be validated without the physical device on the network.

## Connect The Device To Wi-Fi First

On first boot, with no Wi-Fi credentials stored, the device hosts its own open Wi-Fi access point and setup page.

1. Power on the device. From a phone or laptop, join the open Wi-Fi network named `Awning-Setup-XXXX`, where `XXXX` is a per-device suffix (so two units never present the same name).
2. A setup page should open automatically (a captive portal). If it does not, browse to `http://192.168.4.1`.
3. Enter your home Wi-Fi network name and password, then choose `Save and Restart`.
4. The device tests the connection while keeping the setup network up, so the page can report the result. If it connects, the page confirms and the device saves the credentials and reboots onto your Wi-Fi. If it cannot connect (for example a mistyped password), the page says so and lets you correct the entry and try again. Credentials are only saved after a successful test, so a typo is never stored.

The credentials are stored in NVS and reused on later boots, so this is a one-time step. To change networks or update the password later, use the `Change Wi-Fi` form on [the web dashboard](#the-web-dashboard); that keeps the Matter commissioning intact and reverts to the current network if the new one cannot be reached. A factory reset (long button press) also clears the credentials and reopens the setup access point, but it additionally decommissions Matter, so prefer the dashboard for a simple network change.

## Before You Commission

1. Complete [the Somfy pairing procedure](pairing.md) so radio control is already proven. Commissioning only adds the network control surface; it does not help debug the radio.
2. Confirm the device is on your Wi-Fi (see the previous section), and that the phone running Google Home is on the same network.
3. Get the Matter pairing information. On first boot before commissioning, the firmware prints the manual pairing code and the onboarding QR-code URL to the serial monitor. Once the device is on Wi-Fi, the same information is also available on [the web dashboard](#the-web-dashboard).

The firmware uses the Arduino Matter library's built-in test credentials. That is expected for a do-it-yourself device and is why the controller shows an uncertified-device warning during setup.

## Add The Device To Google Home

1. In the Google Home app, choose to add a device, then choose the Matter path (labeled "Matter-enabled device," or scan the QR code).
2. Scan the QR code from the onboarding URL, or enter the manual pairing code.
3. Accept the "uncertified device" warning. This appears for do-it-yourself Matter devices and is not a failure.
4. Let Google Home commission the device and add it as a window covering. Because the device is already on Wi-Fi, this completes over the local network. Assign it a room and a speakable name, for example "Patio Awning."

The serial log prints a commissioning-complete line when the fabric join succeeds.

## Test Voice And App Control

1. Say "open the awning" and "close the awning," and use the tile's open, close, and stop controls.
2. Confirm the motion matches expectation. With the default mapping, open retracts the awning and close extends it.
3. If the direction feels backward, flip the `INVERT_DIRECTION` build flag, rebuild, and reflash, or simply rename the device and adjust how you phrase commands. See [the direction semantics notes](hardware.md#direction-semantics).

Because a Somfy awning gives no position feedback, the tile reports only the two end states. Dragging the slider or asking for a percentage moves the awning to the nearer end stop rather than to a partial position.

## The Web Dashboard

Once the device is on Wi-Fi, it serves a small status page at `http://somfy-awning-XXXX.local` (where `XXXX` is the per-device suffix), or at the IP address shown in the serial log. The exact hostname is printed to serial on connect and shown on the page itself. The dashboard reports the hostname, IP address, Wi-Fi signal strength, and Matter commissioning state. Before commissioning it also shows the manual pairing code and a link to the QR code, so the pairing information is available without the serial monitor. After commissioning it points to multi-admin sharing for adding other ecosystems. The page also has a `Change Wi-Fi` form for moving the device to another network without a factory reset; if the new network cannot be reached, the device automatically returns to the current one, so a mistyped password will not lock it out.

## Reconnection And Recovery

After a power cycle, the device rejoins Wi-Fi and its Matter fabric automatically; it does not need re-commissioning. If the network is briefly unreachable it keeps retrying rather than dropping back into setup mode. The last-known position is restored from NVS so the tile shows a sensible state immediately.

If the network password is changed while the device is configured, its stored password becomes stale. The device detects the repeated authentication rejection (distinct from a network simply being down) and reopens its `Awning-Setup-XXXX` access point so the new password can be entered, keeping its Matter commissioning. The setup page explains that the saved password was rejected and prefills the network name, so only the new password is needed. This means a changed router password never requires a factory reset. The recommended order is to update the password on the router first, then reconnect the device through its setup portal.

To change only the Wi-Fi network or password proactively, use the `Change Wi-Fi` form on the dashboard; that leaves Matter commissioning intact. To move to a different controller or recover from a bad state, do a factory reset with a long press (about 10 seconds) of the panel-mount button. That decommissions Matter, clears the fabric data, and clears the stored Wi-Fi credentials, so the device reopens the setup access point on the next boot for fresh setup. The Somfy pairing and rolling code are unaffected; they live in separate storage.

## Multi-Admin Sharing

Matter supports multi-admin, so the same physical device can be shared into Alexa, Apple Home, or SmartThings from within Google Home's device sharing. This is useful if a richer automation engine is wanted later. None of those are required, and the device stays fully functional in Google Home alone.

## Troubleshooting

If Google Home cannot find the device, confirm the phone and the ESP32 are on the same Wi-Fi network and subnet, and that the network allows the IPv6 and mDNS traffic Matter relies on. Many mesh and guest networks block this.

If the device does not join Wi-Fi during setup, the setup page reports the failed test and the credentials are not saved; correct the network name or password and try again. If a dashboard `Change Wi-Fi` attempt targets an unreachable network, the device reverts to the current network on its own, so no recovery step is needed. If the network password later changes, the device reopens its setup access point on its own so the new password can be entered; look for the `Awning-Setup-XXXX` network again.

If commissioning starts but fails partway, power-cycle the ESP32 and retry from the serial-printed pairing code. If it still fails, factory-reset Matter with the long button press and try once more.

If voice control does nothing but the tile works, re-check the device name and room assignment in the controller, since voice matching depends on them.
