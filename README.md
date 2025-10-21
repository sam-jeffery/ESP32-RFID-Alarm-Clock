# ESP32-Alarm-Clock
 An ESP32 based alarm clock that requires an RFID tag to switch off

## Setup
Create the file `./include/WifiCredentials.h` and populate it with your wifi credentials:
```cpp
const char *ssid = "my_ssid";
const char *password = "my_password";
```
Then run `pio upload` to upload to the device.
