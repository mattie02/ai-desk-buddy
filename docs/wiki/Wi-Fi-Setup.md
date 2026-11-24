# Phone-based Wi-Fi setup

[Documentation home](Home.md) · [Setup and operation](Setup-and-Operation.md)

Implemented in the `esp32voice` firmware using the pinned Arduino ESP32 framework. The bench diagnostic target does not open a setup network. The current-board phone workflow is bench verified: the page opens, failed credentials preserve the previous configuration, and the user confirmed a successful save, restart to Ready, and audible reply.

## Enter setup

With the touch module powered, press and hold the pad, briefly press the ESP32 EN/RESET button, and keep holding until the OLED displays the setup network and password. The firmware requires three continuous seconds after the hold prompt appears. Release once the AP/PW details appear. Resetting clears a running timer, so finish or cancel it first.

Use EN/RESET rather than holding the pad through a cold power-up: the touch module calibrates at power-up. An early release resumes normal startup. Ordinary long push-to-talk holds cannot enter setup. With neither saved credentials nor a valid legacy fallback, setup opens automatically.

## Configure from a phone

1. Join the temporary `Buddy-XXXXXX` network shown on the OLED, using its displayed password. Keep the phone connected even though the network has no internet.
2. Open **http://192.168.4.1** in a browser. There is no automatic captive-portal redirect.
3. Select a nearby network from the suggested names, or type its exact name. Only 2.4 GHz networks are supported by this board.
4. Enter the network password and choose **Test and save**. Use the explicit open-network checkbox only when that network has no password.
5. Stay connected to the setup network during the test. It can take up to 25 seconds. The access point may briefly change channel while the ESP32 joins the target network; rejoin the setup network if needed.
6. After **Wi-Fi saved**, the setup network closes shortly. Rejoin your normal network and wait for device Ready.

The OLED setup password changes each time setup opens. Keep Wi-Fi passwords out of chat and source control. Enter real credentials yourself on the phone; they are not echoed in responses or logs.

## Failure, exit, and timeout

Incorrect credentials or no DHCP connection produce an error without saving the candidate. Retry on the same page, or choose **Exit setup** to reconnect using the previous profile/fallback. The portal closes after five minutes even if unused. A fresh device with no profile will show Setup afterward; reset to reopen setup.

A network name can be 1–32 bytes. Protected networks accept an 8–63-byte printable ASCII passphrase or a 64-digit hexadecimal key. Open networks require the checkbox and an empty password. Enterprise authentication, WEP, and router captive-portal login are not implemented.

## What is saved

The device first loads a validated profile from its `buddy-net` Preferences namespace. If none is available, it can use the legacy Wi-Fi fields in local `include/secrets.h`. A phone-entered profile takes priority after it has been tested and saved. There is no need to edit or reflash for subsequent Wi-Fi changes.

Connection testing checks association and a nonzero DHCP address for the requested network. The implementation disconnects any previous candidate before starting another test, preventing a stale connection from falsely validating a password. It saves one versioned, checksummed NVS blob only after success. Preferences retains data across normal resets and power loss; an erase-flash operation can remove it. The default prototype does not encrypt NVS against physical flash access.

## Backend remains separate

This step changes Wi-Fi credentials only. The Mac address/hostname, backend port, and device token remain firmware configuration in `secrets.h`. Changing routers can change the Mac's address or put it on a different network. Successful Wi-Fi setup does not prove the backend is reachable; the Mac backend must still be running and reachable at its configured address. Remote hosting and editable server destinations remain future work.

Voice capture/turns are paused during setup. Normal audio and timer tasks resume after exit; timers are not made persistent by this feature. Setup is entered at startup, so an existing countdown has already been cleared by the reset.

## Implementation and checks

The local HTTP service is bound to the AP interface and only runs during setup. The AP uses a random password shown only on the OLED. State-changing requests require the setup-session nonce. Requests have bounded headers/body and a three-second read deadline; form decoding rejects embedded control/NUL characters, duplicate required fields, and truncation. Network names are rendered as data rather than HTML.

Native checks cover profile validation, record corruption/version rejection, boot gesture/release/wrap behavior, HTTP bounds, duplicate length headers, host checks, malformed form encodings, and randomized malformed input under address/undefined-behavior sanitizers. The full suite passes 36 tests. Both firmware targets build, the voice firmware uploaded with flash hash verification, and normal startup reached Realtime Ready using the existing fallback. The user confirmed page access, wrong-password failure followed by Ready after Exit, and a successful save followed by restart to Ready and an audible reply. Serial also recorded candidate failures and a successful save. A post-setup two-second timer was device-confirmed, finished locally with its tone, returned the display to Ready, and reported completed status. Fresh-device automatic setup without a legacy fallback, open-network provisioning, and the full five-minute timeout have not been separately bench verified.

References: [ESP32 Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html), [Preferences storage](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html). Implementation was checked against the locally pinned Arduino 2.0.17 headers/source, not just the latest online API.
