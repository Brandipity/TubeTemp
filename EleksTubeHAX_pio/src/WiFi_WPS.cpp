#include <Arduino.h>
#include <WiFi.h> // ESP32
#include "StoredConfig.h"
#include "TFTs.h"
#include "esp_wps.h"
#include "WiFi_WPS.h"

extern StoredConfig stored_config;

WifiState_t WifiState = disconnected;


uint32_t TimeOfWifiReconnectAttempt = 0;
double GeoLocTZoffset = 0;


#ifdef WIFI_USE_WPS   ////  WPS code
//https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WPS/WPS.ino
static esp_wps_config_t wps_config;
void wpsInitConfig(){
  wps_config.wps_type = ESP_WPS_MODE;
  strcpy(wps_config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(wps_config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(wps_config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(wps_config.factory_info.device_name, DEVICE_NAME);
}
#endif

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info){
  switch(event){
    case ARDUINO_EVENT_WIFI_STA_START:
      WifiState = disconnected;
      Serial.println("Station Mode Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: // IP not yet assigned
      Serial.println("Connected to AP: " + String(WiFi.SSID()));
      break;     
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Got IP: ");
      Serial.println(WiFi.localIP());
      WifiState = connected;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      WifiState = disconnected;
      Serial.print("WiFi lost connection. Reason: ");
      Serial.println(info.wifi_sta_disconnected.reason);
      WifiReconnect();
      break;
#ifdef WIFI_USE_WPS   ////  WPS code      
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      WifiState = wps_success;
      Serial.println("WPS Successful, stopping WPS and connecting to: " + String(WiFi.SSID()));
      esp_wifi_wps_disable();
      delay(10);
      WiFi.begin();
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      WifiState = wps_failed;
      Serial.println("WPS Failed, retrying");
      esp_wifi_wps_disable();
      esp_wifi_wps_enable(&wps_config);
      esp_wifi_wps_start(0);
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      Serial.println("WPS Timeout, retrying");
      tfts.setTextColor(TFT_RED, TFT_BLACK);      
      tfts.print("/");  // retry
      tfts.setTextColor(TFT_BLUE, TFT_BLACK);
      esp_wifi_wps_disable();
      esp_wifi_wps_enable(&wps_config);
      esp_wifi_wps_start(0);
      WifiState = wps_active;
      break;
 #endif     
    default:
      break;
  }
}



void WifiBegin()  {
  WifiState = disconnected;

  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);  
  WiFi.setHostname(DEVICE_NAME);  
  WiFi.begin(WIFI_SSID, WIFI_PASSWD); 
  WiFi.onEvent(WiFiEvent);
  unsigned long StartTime = millis();
  while ((WiFi.status() != WL_CONNECTED)) {
    delay(500);
    tfts.print(".");
    Serial.print(".");
    if ((millis() - StartTime) > (WIFI_CONNECT_TIMEOUT_SEC * 1000)) {
      Serial.println("\r\nWiFi connection timeout!");
      tfts.println("\nTIMEOUT!");
      WifiState = disconnected;
      return; // exit loop, exit procedure, continue clock startup
    }
  }
  

 
  WifiState = connected;

  tfts.println("\n Connected!");
  tfts.println(WiFi.localIP());
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  
  delay(200);
}

void WifiReconnect() {
  if ((WifiState == disconnected) && ((millis() - TimeOfWifiReconnectAttempt) > WIFI_RETRY_CONNECTION_SEC * 1000)) {
    Serial.println("Attempting WiFi reconnection...");
    WiFi.reconnect();
    TimeOfWifiReconnectAttempt = millis();
  }    
}

