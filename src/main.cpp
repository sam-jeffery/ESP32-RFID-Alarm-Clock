#include <Arduino.h>
#include <Audio.h>
#include <SPIFFS.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include <Adafruit_PN532.h>

#include <RTClib.h>

#include <WiFi.h>

#include "WifiCredentials.h"
#include "esp_adc_cal.h"
#include "time.h"

// NTP Server
const char *ntpServer = "pool.ntp.org";

// I2S Connections
#define I2S_DOUT 12
#define I2S_BCLK 4
#define I2S_LRC 16

// OLED display
Adafruit_SSD1306 display(128, 32, &Wire, (uint8_t)-1);

// All valid NFC cards
static byte card1[4] = {0x33, 0xEE, 0xB9, 0x12};
static byte card2[4] = {0x33, 0x18, 0x40, 0xFE};
static byte card3[4] = {0x53, 0x09, 0xC2, 0xA2};
static byte card4[4] = {0x7A, 0x89, 0xB2, 0x80};
static byte card5[4] = {0x36, 0x57, 0x02, 0x83};

RTC_DS3231 rtc;

#define CLOCK_INTERRUPT_PIN 39

Audio audio;

bool alarmRinging = false;

TaskHandle_t secondCoreTask;

// 3 control buttons
#define BUTTON_1 2
#define BUTTON_2 0
#define BUTTON_3 26

#define MOSFET_PIN 15

DateTime alarmTime;

unsigned long snoozeAtTime = 0;

bool snooze = false;

int snoozeCount = 0;

#define SNOOZE_LIMIT 5

unsigned long lastPressTime = 0;

#define SNOOZE_TIME 30000
#define SNOOZE_LIMIT_TIME 15000

#define LONG_SNOOZE_LENGTH_MINUTES 4
#define LONG_SNOOZE_LIMIT 2

bool interruptPressed = false;
bool wear = false;

#define PN532_RESET (14)
#define PN532_IRQ (0)

bool canAdjustTime = true;

bool show_true_voltage = false;

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

void Sleep() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  digitalWrite(LED_BUILTIN, LOW);
  gpio_hold_en((gpio_num_t)LED_BUILTIN);

  // Set mosfet pin to low during sleep
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0);

  uint64_t mask = ((uint64_t)(((uint64_t)1) << BUTTON_1)) |
                  ((uint64_t)(((uint64_t)1) << BUTTON_2)) |
                  ((uint64_t)(((uint64_t)1) << BUTTON_3));
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);

  esp_deep_sleep_start();
}

void alarmButtonInterrupt() { interruptPressed = true; }

void SetAlarm(DateTime alarmTime) {
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);

  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.disable32K();

  rtc.disableAlarm(2);

  rtc.setAlarm1(alarmTime, DS3231_A1_Hour);
}

void ClearAlarm() {
  rtc.clearAlarm(1);
  rtc.disableAlarm(1);
}

void TriggerAlarm() {
  pinMode(14, OUTPUT);
  digitalWrite(14, HIGH);
  alarmRinging = true;
  audio.connecttoFS(SPIFFS, "/alarm.mp3");

  display.clearDisplay();
  display.setTextSize(3);

  // Draw the time remaining centred in the middle of the screen
  display.setCursor(24, 4);
  display.print("Alarm");

  display.display();

  display.setTextSize(1);

  attachInterrupt(BUTTON_1, alarmButtonInterrupt, RISING);
  attachInterrupt(BUTTON_2, alarmButtonInterrupt, RISING);
  attachInterrupt(BUTTON_3, alarmButtonInterrupt, RISING);
}

void setup() {
  // Turn on mosfet
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, HIGH);

  gpio_hold_dis((gpio_num_t)LED_BUILTIN);

  Serial.begin(115200);
  Serial.println("Boot stage 1");

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    while (true) {
      delay(1000);
      Serial.println("SSD1306 allocation failed");
      // This should never be reached
    }
  }

  Serial.println("Boot stage 2");

  display.ssd1306_command(SSD1306_DISPLAYON);

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  display.clearDisplay();
  display.setCursor(0, 0);
  // display.print("Booting...");
  // display.display();

  Serial.println("Boot stage 3");

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    while (true) {
      delay(1000);
      Serial.println("SPIFFS Mount Failed");
      // This should never be reached
    }
  }

  Serial.println("Boot stage 4");

  if (!rtc.begin()) {
    while (true) {
      delay(1000);
      Serial.println("Couldn't find RTC");
      // This should never be reached
    }
  }

  Serial.println("Boot stage 5");

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    while (true) {
      delay(1000);
      Serial.println("Didn't find PN532 board");
      // This should never be reached
    }
  }

  Serial.println("Boot stage 6");

  rtc.disable32K();

  // Initialize Audio
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(100);

  pinMode(0, INPUT_PULLDOWN);
  pinMode(26, INPUT_PULLDOWN);
  pinMode(2, INPUT_PULLDOWN);

  if (digitalRead(BUTTON_1) == HIGH) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Syncing time...");
    display.display();

    // Button 1 held on startup, sync time with ntp pool
    WiFi.begin(ssid, password);
    Serial.printf("Connecting to %s", ssid);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("Connected!");

    configTime(0, 0, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      while (true) {
        delay(1000);
        Serial.println("Failed to obtain time");
        // This should never be reached
      }
    }

    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                        timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                        timeinfo.tm_sec));

    // Account for daylight savings

    DateTime now = rtc.now();

    // Calculate the date of the last sunday in March of the current year
    int lastSundayMarch = 31 - ((5 * now.year() / 4 + 4) % 7);
    int lastSundayOctober = 31 - ((5 * now.year() / 4 + 1) % 7);

    DateTime daylightSavingsStart(now.year(), 3, lastSundayMarch, 0, 1, 0);
    DateTime daylightSavingsEnd(now.year(), 10, lastSundayOctober - 1, 23, 0, 0);

    if (now > daylightSavingsStart && now < daylightSavingsEnd) {
      rtc.adjust(rtc.now() + TimeSpan(0, 1, 0, 0));
    }

    Serial.println(String(now.year()) + "/" + String(now.month()) + "/" +
                   String(now.day()) + " " + String(now.hour()) + ":" +
                   String(now.minute()) + ":" + String(now.second()));

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Time synced!");
    display.setCursor(0, 10);
    display.print("Current date and time: ");
    display.setCursor(0, 20);
    display.print(now.day(), DEC);
    display.print("/");
    display.print(now.month(), DEC);
    display.print("/");
    display.print(now.year(), DEC);
    display.print(" ");
    display.print(now.hour(), DEC);
    display.print(":");
    display.print(now.minute(), DEC);
    display.print(":");
    display.print(now.second(), DEC);
    display.display();

    WiFi.disconnect(true);
  }

  if (digitalRead(BUTTON_3) == HIGH) {
    show_true_voltage = true;
  }

  alarmTime = rtc.getAlarm1();

  if (lastPressTime == 0) {
    lastPressTime = millis();
  }

  if (esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause()) {
    switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("ALARMMM!");
      // Alarm triggered
      TriggerAlarm();
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      // Button pressed
      lastPressTime = millis();
      break;
    default:
      break;
    }
  }

  pinMode(34, INPUT);

  DateTime now = rtc.now();

  int alarmSeconds = (alarmTime.hour() * 60 * 60) + (alarmTime.minute() * 60) +
                     alarmTime.second();
  int nowSeconds = (now.hour() * 60 * 60) + (now.minute() * 60) + now.second();
  int blockStart = alarmSeconds - 7200;

  if (nowSeconds > blockStart && nowSeconds < alarmSeconds) {
    // Less than 2 hours before alarm rings, block adjustments
    canAdjustTime = false;
  }

  DateTime six_hours_ago = DateTime(now.unixtime() - 21600);
  int daysSince = six_hours_ago.unixtime() / 86400;
  if (daysSince % 2 == 0) {
    wear = true;
  }
}
// Define a long variable called button hold time
#define BUTTON_HOLD_TIME 10000

float readBattery() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100,
                           &adc_chars);

  return esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_6), &adc_chars) *
         2.0 / 1000.0;
}

String getDaySuffix(int day) {
  if (day % 10 == 1 && day != 11) {
    return "st";
  } else if (day % 10 == 2 && day != 12) {
    return "nd";
  } else if (day % 10 == 3 && day != 13) {
    return "rd";
  } else {
    return "th";
  }
}

String getDayName(int day, bool shortened) {
  switch (day) {
  case 1:
    return shortened ? "Mon" : "Monday";
  case 2:
    return shortened ? "Tue" : "Tuesday";
  case 3:
    return shortened ? "Wed" : "Wednesday";
  case 4:
    return shortened ? "Thu" : "Thursday";
  case 5:
    return shortened ? "Fri" : "Friday";
  case 6:
    return shortened ? "Sat" : "Saturday";
  case 0:
    return shortened ? "Sun" : "Sunday";
  default:
    return "";
  }
}

String getMonthName(int month, bool shortened) {
  switch (month) {
  case 1:
    return shortened ? "Jan" : "January";
  case 2:
    return shortened ? "Feb" : "February";
  case 3:
    return shortened ? "Mar" : "March";
  case 4:
    return shortened ? "Apr" : "April";
  case 5:
    return shortened ? "May" : "May";
  case 6:
    return shortened ? "Jun" : "June";
  case 7:
    return shortened ? "Jul" : "July";
  case 8:
    return shortened ? "Aug" : "August";
  case 9:
    return shortened ? "Sep" : "September";
  case 10:
    return shortened ? "Oct" : "October";
  case 11:
    return shortened ? "Nov" : "November";
  case 12:
    return shortened ? "Dec" : "December";
  default:
    return "";
  }
}

void displayTime() {
  // Display current time and alarm time
  DateTime now = rtc.now();

  display.clearDisplay();

  String timeString =
      (now.hour() < 10 ? "0" + String(now.hour()) : String(now.hour())) + ":" +
      (now.minute() < 10 ? "0" + String(now.minute()) : String(now.minute())) +
      ":" +
      (now.second() < 10 ? "0" + String(now.second()) : String(now.second()));
  int16_t x_b, y_b;
  uint16_t w_b, h_b;
  display.getTextBounds(timeString, 0, 0, &x_b, &y_b, &w_b, &h_b);
  display.setCursor(64 - w_b / 2, 0);
  display.print(timeString);

  String constructor = getDayName(now.dayOfTheWeek(), false) + " " + now.day() +
                       getDaySuffix(now.day()) + " " +
                       getMonthName(now.month(), true);
  int16_t x_a, y_a;
  uint16_t w_a, h_a;
  display.getTextBounds(constructor, 0, 0, &x_a, &y_a, &w_a, &h_a);
  display.setCursor(64 - w_a / 2, 10);
  display.print(constructor);

  if (wear) {
    display.setCursor(0, 0);
    display.print("w");
  }

  String alarmTimeString =
      (alarmTime.hour() < 10 ? "0" + String(alarmTime.hour())
                             : String(alarmTime.hour())) +
      ":" +
      ((alarmTime.minute() < 10 ? "0" + String(alarmTime.minute())
                                : String(alarmTime.minute()))) +
      ":00";
  int16_t x_c, y_c;
  uint16_t w_c, h_c;
  display.getTextBounds(alarmTimeString, 0, 0, &x_c, &y_c, &w_c, &h_c);
  display.setCursor(64 - w_c / 2, 20);
  display.print(alarmTimeString);

  if (show_true_voltage) {
    String voltage = String(readBattery(), 2) + "V";
    int16_t x_d, y_d;
    uint16_t w_d, h_d;
    display.getTextBounds(voltage, 0, 0, &x_d, &y_d, &w_d, &h_d);
    display.setCursor(128 - w_d, 0);
    display.print(voltage);
  } else {
    if (readBattery() < 3.4) {
      display.setCursor(122, 20);
      display.print("!");
    }
  }

  display.display();
}

bool ScanCard() {
  // Read RFID card
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;

  success =
      nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

  if (success) {
    // Card detected, check if it is the correct one
    bool correctCard = false;
    if (uidLength == 4) {
      if (uid[0] == card1[0] && uid[1] == card1[1] && uid[2] == card1[2] &&
          uid[3] == card1[3]) {
        correctCard = true;
      } else if (uid[0] == card2[0] && uid[1] == card2[1] &&
                 uid[2] == card2[2] && uid[3] == card2[3]) {
        correctCard = true;
      } else if (uid[0] == card3[0] && uid[1] == card3[1] &&
                 uid[2] == card3[2] && uid[3] == card3[3]) {
        correctCard = true;
      } else if (uid[0] == card4[0] && uid[1] == card4[1] &&
                 uid[2] == card4[2] && uid[3] == card4[3]) {
        correctCard = true;
      } else if (uid[0] == card5[0] && uid[1] == card5[1] &&
                 uid[2] == card5[2] && uid[3] == card5[3]) {
        correctCard = true;
      }
    }

    return correctCard;
  } else {
    return false;
  }
}

uint32_t last_button_2_release = 0;

void adjustTimeLoop() {
  if (canAdjustTime) {
    if (digitalRead(BUTTON_1) == HIGH) {
      // Subtract 10 seconds from alarm time
      alarmTime = alarmTime - TimeSpan(0, 0, 0, 10);
    }

    if (digitalRead(BUTTON_3) == HIGH) {
      // Add 10 seconds to alarm time
      alarmTime = alarmTime + TimeSpan(0, 0, 0, 10);
    }

    if (digitalRead(BUTTON_2) == HIGH) {
      if (millis() - last_button_2_release > 3000) {
        display.clearDisplay();
        display.setCursor(20, 10);
        display.print("Alarm disabled!");
        display.display();

        delay(2000);

        ClearAlarm();
        Sleep();
      }
    } else {
      last_button_2_release = millis();
    }
  } else {
    // Cannot adjust time but trying to anyway, require card scan
    if (ScanCard()) {
      canAdjustTime = true;
      last_button_2_release = millis();
    }
  }

  if (digitalRead(BUTTON_1) == HIGH | digitalRead(BUTTON_2) == HIGH |
      digitalRead(BUTTON_3) == HIGH) {
    // Button pressed
    lastPressTime = millis();
  }

  if (millis() - lastPressTime >= (long)BUTTON_HOLD_TIME) {
    // No button pressed for BUTTON_HOLD_TIME,save alarm and go to sleep
    // Set alarmTime seconds to 0

    DateTime nowTime = rtc.now();

    alarmTime = DateTime(nowTime.year(), nowTime.month(), nowTime.day() + 1,
                         alarmTime.hour(), alarmTime.minute(), 0);

    SetAlarm(alarmTime);

    Sleep();
  } else {
    displayTime();
  }
}

void loop() {
  if (interruptPressed) {
    if (alarmRinging) {
      // Check buttons for presses
      if (digitalRead(BUTTON_1) == HIGH | digitalRead(BUTTON_2) == HIGH |
          digitalRead(BUTTON_3) == HIGH) {
        // Button pressed, snooze alarm
        alarmRinging = false;
        snooze = true;
        audio.stopSong();
        snoozeAtTime = millis();

        snoozeCount++;

        detachInterrupt(BUTTON_1);
        detachInterrupt(BUTTON_2);
        detachInterrupt(BUTTON_3);
      }
    }
    interruptPressed = false;
  }
  if (snooze) {
    // Check for button presses, enter long snooze if middle pressed
    if (digitalRead(BUTTON_2) == HIGH) {
      // Read snooze count from file
      File snooze_file = SPIFFS.open("/snooze.n", "r");
      int long_snooze_count = snooze_file.readString().toInt();
      snooze_file.close();

      if (long_snooze_count < LONG_SNOOZE_LIMIT)
      // We have snoozes remaining
      {
        // Increase snooze count by 1
        snooze_file = SPIFFS.open("/snooze.n", "w");
        int new_snooze_count = long_snooze_count + 1;
        snooze_file.print(new_snooze_count);
        snooze_file.close();

        // Set alarm to go off after the snooze interval
        alarmTime = alarmTime + TimeSpan(0, 0, LONG_SNOOZE_LENGTH_MINUTES, 0);
        SetAlarm(alarmTime);

        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Alarm set for: ");
        display.setCursor(0, 10);
        if (alarmTime.hour() < 10) {
          display.print("0");
        }
        display.print(alarmTime.hour(), DEC);
        display.print(":");
        if (alarmTime.minute() < 10) {
          display.print("0");
        }
        display.print(alarmTime.minute(), DEC);
        display.print(":00");

        display.setCursor(0, 20);
        display.print("Snoozes remaining: ");
        display.print((LONG_SNOOZE_LIMIT - new_snooze_count), DEC);

        display.display();

        delay(2000);

        audio.stopSong();
        Sleep();
      }
    }

    int timeRemaining;
    if (snoozeCount > SNOOZE_LIMIT) {
      timeRemaining = (snoozeAtTime + SNOOZE_LIMIT_TIME) - millis();
    } else {
      timeRemaining = (snoozeAtTime + SNOOZE_TIME) - millis();
    }
    display.clearDisplay();
    display.setTextSize(3);

    // Draw the time remaining centred in the middle of the screen
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(String(timeRemaining / 1000), 64, 16, &x1, &y1, &w,
                          &h);
    display.setCursor(64 - w / 2, 16 - h / 2);
    display.print(timeRemaining / 1000);

    display.display();

    display.setTextSize(1);

    delay(100);

    if (ScanCard()) {
      snooze = false;
      alarmRinging = false;
      audio.stopSong();
      lastPressTime = millis();

      // Reset snooze count
      File snooze_file = SPIFFS.open("/snooze.n", "w");
      snooze_file.print("0");
      snooze_file.close();
    } else {
      if (timeRemaining <= 0) {
        display.setTextSize(1);
        snooze = false;
        alarmRinging = true;
        audio.connecttoFS(SPIFFS, "/alarm.mp3");

        display.clearDisplay();
        display.setTextSize(3);

        // Draw the time remaining centred in the middle of the screen
        display.setCursor(24, 4);
        display.print("Alarm");

        display.display();

        display.setTextSize(1);

        attachInterrupt(BUTTON_1, alarmButtonInterrupt, RISING);
        attachInterrupt(BUTTON_2, alarmButtonInterrupt, RISING);
        attachInterrupt(BUTTON_3, alarmButtonInterrupt, RISING);
      }
    }
  } else {
    if (alarmRinging) {
      Serial.println("Alarm ringing");
      audio.loop();
      if (!audio.isRunning()) {
        audio.connecttoFS(SPIFFS, "/alarm.mp3");
      }
    } else {
      DateTime rtc_now = rtc.now();
      if (rtc_now.hour() == alarmTime.hour() &&
          rtc_now.minute() == alarmTime.minute() &&
          rtc_now.second() == alarmTime.second()) {
        TriggerAlarm();
        return;
      }

      adjustTimeLoop();
    }
  }
}
