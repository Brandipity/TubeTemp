/*
 * Author: Brandon Winston
 * Original FW Author: Aljaz Ogrin
 * Project: Nixie Hardware Monitor
 * Hardware: ESP32
 */

#include <stdint.h>
#include "GLOBAL_DEFINES.h"
#include "Backlights.h"
#include "TFTs.h"
#include "Clock.h"
#include "StoredConfig.h"
#include "WiFi_WPS.h"
#include "esp_wifi.h" 
#include "BluetoothSerial.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"



#ifdef HARDWARE_NovelLife_SE_CLOCK // NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// #include "Gestures.h"
//TODO put into class
#include <Wire.h>
#include <SparkFun_APDS9960.h>
#endif //NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
#include <esp_bt.h>

// Constants


// Global Variables
#ifdef HARDWARE_NovelLife_SE_CLOCK // NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//TODO put into class
SparkFun_APDS9960 apds      = SparkFun_APDS9960();
//interupt signal for gesture sensor
int volatile      isr_flag  = 0;
#endif //NovelLife_SE Clone XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

Backlights    backlights;
TFTs          tfts;
Clock         uclock;
StoredConfig  stored_config;

bool          FullHour        = false;
uint8_t       hour_old        = 255;
bool          DstNeedsUpdate  = false;
uint8_t       yesterday       = 0;

uint8_t value = 0;

BluetoothSerial SerialBT;

bool btConnected = false;
unsigned long lastBtCheck = 0;
const unsigned long BT_CHECK_INTERVAL = 100; // Check every 5 seconds

// Helper function, defined below.
void updateClockDisplay(TFTs::show_t show=TFTs::yes);
void setupMenu(void);
void UpdateDstEveryNight(void);
void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

void setup() {
  Serial.begin(115200);
  delay(500);  // Waiting for serial monitor to catch up.
  Serial.println("");
  Serial.println(FIRMWARE_VERSION);
  Serial.println(F("In setup()."));  

  stored_config.begin();
  stored_config.load();

  backlights.begin(&stored_config.config.backlights);

  // Setup the displays (TFTs) initaly and show bootup message(s)
  tfts.begin();  // and count number of clock faces available
  tfts.fillScreen(TFT_BLACK);
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.setCursor(0, 0, 2);  // Font 2. 16 pixel high
  tfts.println(F("Begin Setup..."));

  tfts.println(F("WiFi Start"));
  WiFi.mode(WIFI_STA);

  tfts.println(F("Waiting for network."));
  // wait for a bit before querying NTP
  for (uint8_t ndx=0; ndx < 5; ndx++) {
    tfts.print(".");
    delay(100);
  }
  tfts.println("");

  // Setup clock and sync time
  tfts.println(F("Clock Start"));
  uclock.begin(&stored_config.config.uclock);

  if (uclock.getActiveGraphicIdx() > tfts.NumberOfClockFaces) {
    uclock.setActiveGraphicIdx(tfts.NumberOfClockFaces);
    Serial.println(F("Last selected index of clock face is larger than currently available number of image sets."));
  }
  if (uclock.getActiveGraphicIdx() < 1) {
    uclock.setActiveGraphicIdx(1);
    Serial.println(F("Last selected index of clock face is less than 1."));
  }
  tfts.current_graphic = uclock.getActiveGraphicIdx();

  SerialBT.register_callback(callback);
      // Configure Bluetooth parameters
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    
    // Initialize controller
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    
    // Disable power saving features
    esp_bt_sleep_disable();
    
    // Set connection timeout
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
     SerialBT.begin("TubeTemp");



  tfts.println(F("Setup complete!"));

  tfts.println("Wrapping up...");

  // Leave boot up messages on screen for a few seconds.
  for (uint8_t ndx=0; ndx < 10; ndx++) {
    tfts.print(".");
    delay(200);
  }
  // Start up the clock displays.
  tfts.fillScreen(TFT_BLACK);
  uclock.loop();
  updateClockDisplay(TFTs::force);
  Serial.println(F("Setup finished."));
}

void loop() {

  uint32_t millis_at_top = millis();
  // Do all the maintenance work
  //WifiReconnect(); // if not connected attempt to reconnect
  backlights.loop();
  uclock.loop();

    if (SerialBT.available()) {
        String message = SerialBT.readStringUntil('\n');  // Read until newline
        int16_t value = (int16_t)message.toInt();
        backlights.adjustColorPhase(value);
        Serial.print("Received message: ");
        Serial.println(message);
        
        // Optional: Echo back to Bluetooth terminal
        SerialBT.println("Got: " + message);
    }


  // Update the clock.
  updateClockDisplay();
  
  UpdateDstEveryNight();

  uint32_t time_in_loop = millis() - millis_at_top;
  if (time_in_loop < 20) {
    // we have free time, spend it for loading next image into buffer
    tfts.LoadNextImage();

    // we still have extra time
    time_in_loop = millis() - millis_at_top;
    if (time_in_loop < 20) {
      
      // Sleep for up to 20ms, less if we've spent time doing stuff above.
      time_in_loop = millis() - millis_at_top;
      if (time_in_loop < 20) {
        delay(20 - time_in_loop);
      }
    }
  }
#ifdef DEBUG_OUTPUT
  if (time_in_loop <= 1) Serial.print(".");
  else {
    Serial.print("time spent in loop (ms): ");
    Serial.println(time_in_loop);
  }
#endif
} //loop 


void setupMenu() {
  tfts.chip_select.setHoursTens();
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.fillRect(0, 120, 135, 120, TFT_BLACK);
  tfts.setCursor(0, 124, 4);  // Font 4. 26 pixel high
}

bool isNightTime(uint8_t current_hour) {
    if (DAY_TIME < NIGHT_TIME) {
      // "Night" spans across midnight
      return (current_hour < DAY_TIME) || (current_hour >= NIGHT_TIME);
    }
    else {
      // "Night" starts after midnight, entirely contained within the day
      return (current_hour >= NIGHT_TIME) && (current_hour < DAY_TIME);  
    }
}


void UpdateDstEveryNight() {
  uint8_t currentDay = uclock.getDay();
  // This `DstNeedsUpdate` is True between 3:00:05 and 3:00:59. Has almost one minute of time slot to fetch updates, incl. eventual retries.
  DstNeedsUpdate = (currentDay != yesterday) && (uclock.getHour24() == 3) && (uclock.getMinute() == 0) && (uclock.getSecond() > 5);
  if (DstNeedsUpdate) {
  Serial.print("DST needs update...");

  // Update day after geoloc was sucesfully updated. Otherwise this will immediatelly disable the failed update retry.
  yesterday = currentDay;
  }
}

void updateClockDisplay(TFTs::show_t show) {
  // refresh starting on seconds
  tfts.setDigit(SECONDS_ONES, uclock.getSecondsOnes(), show);
  tfts.setDigit(SECONDS_TENS, uclock.getSecondsTens(), show);
  tfts.setDigit(MINUTES_ONES, uclock.getMinutesOnes(), show);
  tfts.setDigit(MINUTES_TENS, uclock.getMinutesTens(), show);
  tfts.setDigit(HOURS_ONES, uclock.getHoursOnes(), show);
  tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), show);
}


void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch(event) {
        case ESP_SPP_SRV_OPEN_EVT:
            Serial.println("Client Connected");
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            break;
            
        case ESP_SPP_CLOSE_EVT:
            Serial.println("Client Disconnected - Enabling Discovery");
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            break;
            
        case ESP_SPP_START_EVT:
            Serial.println("SPP Started");
            break;
    }
    
}
void disableBluetooth() {
    SerialBT.end();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    Serial.println("Bluetooth disabled");
}

void enableBluetooth() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_sleep_disable();
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    
    SerialBT.begin("TubeTemp");
    Serial.println("Bluetooth enabled");
}

void switchToWifi() {
    Serial.println("Switching to WiFi...");
    disableBluetooth();
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    
    // Wait for connection with timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
    } else {
        Serial.println("\nWiFi connection failed");
    }
}

void switchToBluetooth() {
    Serial.println("Switching to Bluetooth...");
    WiFi.disconnect(true);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(500);
    enableBluetooth();
}