# ESP32-Alarm-Clock
 An ESP32 based alarm clock that requires an RFID tag to switch off.

 Set the alarm in advance, then when it rings in the morning, press a button to enter the 30s cooldown. During this time you can get out of bed and go over to your RFID tag (ideally left in another room), then scan it to disable the alarm fully. Not scanning the tag during the cooldown will trigger the alarm again.

 Wifi features allow the RTC to be synced over the internet with the NTP pool, as the DS3231 tends to drift a bit over time.

## Setup
Create the file `./include/WifiCredentials.h` and populate it with your wifi credentials:
```cpp
const char *ssid = "my_ssid";
const char *password = "my_password";
```
Then run `pio upload` to upload to the device.

The alarm sound played is loaded from the file `alarm.mp3` on the SPIFFS filesystem on the ESP32's flash. You can replace this file with one of your choosing.

If you decide to take a look at the code, I promise my coding standards have improved since I wrote this; it's a few years old now...
