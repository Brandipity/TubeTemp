#include "Clock.h"
#include "WiFi_WPS.h"
#include "esp_wifi.h" 
#include "main.h"

void disableWiFi();



  // For the DS1307 RTC
  #include <DS1307RTC.h>
    void RtcBegin() {}
    uint32_t RtcGet() {
      return RTC.get();
    }
    void RtcSet(uint32_t tt) {
      RTC.set(tt);
    }




void Clock::begin(StoredConfig::Config::Clock *config_) {
  config = config_;
  

  if (config->is_valid != StoredConfig::valid) {
    // Config is invalid, probably a new device never had its config written.
    // Load some reasonable defaults.
    Serial.println("Loaded Clock config is invalid, using default.  This is normal on first boot.");
    setTwelveHour(true);
    setBlankHoursZero(false);
    setTimeZoneOffset(-5 * 3600);  // EST
    setActiveGraphicIdx(1);
    config->is_valid = StoredConfig::valid;
  }
  
  RtcBegin();
  ntpTimeClient.begin();
  ntpTimeClient.update();
  Serial.print("NTP time = ");
  Serial.println(ntpTimeClient.getFormattedTime());
  setSyncProvider(&Clock::syncProvider);
}

void Clock::loop() {
  if (timeStatus() == timeNotSet) {
    time_valid = false;
  }
  else {
    loop_time = now();
    local_time = loop_time + config->time_zone_offset;
    time_valid = true;
  }
}


// Static methods used for sync provider to TimeLib library.
time_t Clock::syncProvider() {
    Serial.println("syncProvider()");
    time_t ntp_now, rtc_now;
    rtc_now = RtcGet();
    
    if (millis() - millis_last_ntp > refresh_ntp_every_ms || millis_last_ntp == 0) {
        // Switch to WiFi mode
        switchToWifi();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Getting NTP.");
            if (ntpTimeClient.update()) {
                Serial.print(".");
                ntp_now = ntpTimeClient.getEpochTime();
                Serial.println("NTP query done.");
                Serial.print("NTP time = ");
                Serial.println(ntpTimeClient.getFormattedTime());
                
                if (ntp_now != rtc_now) {
                    RtcSet(ntp_now);
                    Serial.println("Updating RTC");
                }
                millis_last_ntp = millis();
                
                // Switch back to Bluetooth 
                switchToBluetooth();
                return ntp_now;
            }
        }
        
        // If we get here, something failed - switch back to Bluetooth
        switchToBluetooth();
        Serial.println("Using RTC time due to failure");
        return rtc_now;
    }
    
    Serial.println("Using RTC time (not time for update yet)");
    return rtc_now;
}

uint8_t Clock::getHoursTens() {
  uint8_t hour_tens = getHour()/10;
  
  if (config->blank_hours_zero && hour_tens == 0) {
    return TFTs::blanked;
  }
  else {
    return hour_tens;
  }
}

uint32_t Clock::millis_last_ntp = 0;
WiFiUDP Clock::ntpUDP;
NTPClient Clock::ntpTimeClient(ntpUDP);
