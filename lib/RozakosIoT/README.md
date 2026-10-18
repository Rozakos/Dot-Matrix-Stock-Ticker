# RozakosIoT ESP32 library

One pin API for local Wi-Fi, USB serial and the Rozakos cloud. The application
owns hardware behavior; this library owns setup, transport and reconnects.
No Wi-Fi password or device key is compiled into the generic example.

```cpp
#include <RozakosIoT.h>
RozakosIoT iot;
void setup() {
  Serial.begin(115200);
  iot.identity("Workshop sensor");
  iot.pin("V0"); // Read-only telemetry
  iot.pin("V1", [](JsonVariantConst value) -> const char* {
    // Validate and apply your hardware command here.
    return nullptr; // Or return a static error string.
  });
  iot.begin();
}
void loop() {
  iot.virtualWrite("V0", millis() / 1000);
  iot.loop();
  delay(10);
}
```

See `firmware/examples/virtual-pins` for an executable example with validation
and a GPIO2 LED. Compile with `pio run` in that directory. Its virtual V1
accepts boolean or 0/1 commands from a toggle widget. V0 is uptime, not a
simulated temperature. Other boards require porting; this release targets ESP32
Arduino core 2.x and was compiled with PlatformIO espressif32 6.13.0.

## Install

Arduino IDE: install this folder as a ZIP library, plus ArduinoJson 7,
ESPAsyncWebServer, AsyncTCP and WebSockets by Markus Sattler. PlatformIO:
use this directory as a local `symlink://` dependency (see the example), or
vendor it into `lib/RozakosIoT` and declare its dependency libraries in
platformio.ini. The ticker repository vendors a release copy so it builds
without access to the private IoT application repository.

## Setup and account linking

1. Flash the generic example. The USB monitor prints an eight-digit setup
   code and `Rozakos-Setup-XXXX`. Join that network with password
   `Rozakos-<eight-digit-code>`.
2. In IoT, Devices -> Set up a device -> Connect Wi-Fi, enter the home network
   and setup code. Alternatively visit http://192.168.4.1. Rejoin home Wi-Fi
   and discover/add the device. Local dashboards require no account.
3. For remote use, sign in, choose the local device's Link to cloud action,
   hold BOOT for three seconds and enter the code. Display-equipped adapters
   can show `setupCode()`; other boards use USB serial or an application UI.
4. With the code, the app asks the board (`POST /iot/receipt`) for the hash
   of a fresh receipt only the board knows, then creates a ten-minute
   capability bound to this hardware identity and that hash. The board
   exchanges capability plus receipt over verified HTTPS, on a background
   task so `loop()` keeps running, and stores its credential in a separate
   NVS namespace. The permanent key is never returned by local HTTP. Every
   link issues a new key; the previous one stops working.

Setup is open for two minutes, with at most five incorrect code attempts.
Holding BOOT for three seconds opens a new window; holding it ten seconds
clears only cloud credentials; the board can then be linked to any account.
A cloud record left in the previous account simply stays offline until removed. `IOT_SETUP` and
`IOT_RESET_CLOUD` on the physical USB serial port provide the same actions.
Physical USB also accepts `{ "wifi": { "ssid": "...", "password": "..." } }`
for provisioning without embedding credentials in a sketch. Values are never
echoed. There is no network factory-reset endpoint. Existing ticker firmware retains
its own Wi-Fi portal and settings; it passes `manageWifi=false` to the library.

## Application API and execution

- `identity(name, deviceType)` and `pin(name, writer?)` declare up to128 pins.
- `virtualWrite(pin, value)` holds current primitive state. Numbers, strings
  and booleans work in both local and cloud dashboards.
- `onValidate(callback)` optionally rejects local commands before they queue.
  It must be pure and safe to call from the HTTP task. `onCommand(callback)`
  can adapt an existing application's command parser; `onSnapshot(callback)`
  supplies its state without copying application logic into this library.
- `localSecret("…")` optionally requires a password for local `/state`,
  `/command` and the port-81 WebSocket (header `X-Device-Password`, or
  `?token=` on the WebSocket; the app's device password field sends both).
  Without it, any client on the LAN can read state and switch pins.
- `begin(existingHttpServer?, manageWifi=true)` can attach to a device's
  existing port80 server. It always owns the telemetry WebSocket on port81.
- Call `loop()` regularly on one task. It executes hardware callbacks and
  serial/cloud work there. Local HTTP commands return202 when queued and
  errors when validation fails. Application callback errors produce an error
  frame; accepted queued commands are not an application-level success ACK.
- The library retains the latest state during a disconnect and sends it again
  after reconnect. It does not queue actuator commands or replay historical
  sensor samples after an outage. This avoids delayed surprise actuation.

## Protocol

Local: `/info`, `/state`, `POST /command {pin,value}` and WebSocket port81
single-pin frames. Setup: `GET /iot/setup`, `POST /iot/receipt {code}`,
`POST /iot/enroll {code,token}`, `POST /iot/wifi {code,ssid,password}`. USB uses newline-delimited `{pin,value}` at115200 baud.
Cloud: `wss://iot.rozakos.eu/ws/device`, credential in `X-Device-Key`,
`WELCOME`, `TELEMETRY {data:{...}}`, `COMMAND {pin,value}`. The backend binds
identity to the credential, publishes data into MQTT, and routes MQTT commands
back to the device. Existing direct MQTT/WSS devices remain supported.

TLS validates the server certificate against bundled GTS, ISRG and SSL.com
roots (the CAs Cloudflare issues from); NTP must set the clock before connecting. Reconnects use a ten-second interval and a
heartbeat. Keys and Wi-Fi credentials are private configuration in ordinary
NVS, not flash-encrypted storage. Local commands are a trusted-LAN protocol
unless `localSecret` is set; enrollment and Wi-Fi changes always require
physical setup authorization.

This release supplies reusable firmware and enrollment. It does not add OTA,
ESP8266 support, BLE provisioning, or an unattended remote recovery path.
