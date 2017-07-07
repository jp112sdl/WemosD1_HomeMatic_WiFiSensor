#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define TasterPin      D7 //Taster gegen GND, um den Konfigurationsmodus zu aktivieren
#define DHT22DataPin   D2 //DATA-Pin 
#define SensorVCCPin   D1 //+VCC-Pin 
#define DS18B20DataPin D4
#define DebugPin       D5 //Optional: D5 gegen GND, um serielle Ausgabe zu aktivieren (115200,8,N,1)
#define DS18B20active  D6 //D6 gegen GND, um OneWire Modus zu aktivieren

DHT dht(DHT22DataPin, DHT22);
OneWire DS18B20Wire(DS18B20DataPin);
DallasTemperature DS18B20Sensors(&DS18B20Wire);

char sleepTimeMin[4]  = "60";
char ccuip[16];
String CUxDDevice;//         = "CUX9002001";
bool SerialDEBUG = false;
bool OneWireMode = false;

//WifiManager - don't touch
byte ConfigPortalTimeout = 180;
bool shouldSaveConfig        = false;
String configJsonFile        = "config.json";
bool wifiManagerDebugOutput = false;
char ip[16]      = "0.0.0.0";
char netmask[16] = "0.0.0.0";
char gw[16]      = "0.0.0.0";
boolean startWifiManager = false;

void setup() {
  pinMode(TasterPin,      INPUT_PULLUP);
  pinMode(DebugPin,       INPUT_PULLUP);
  pinMode(DS18B20active,  INPUT_PULLUP);
  pinMode(DS18B20DataPin, INPUT_PULLUP);
  pinMode(DHT22DataPin,   INPUT_PULLUP);
  pinMode(SensorVCCPin,   OUTPUT);
  pinMode(LED_BUILTIN,    OUTPUT);

  digitalWrite(SensorVCCPin, HIGH);
  digitalWrite(LED_BUILTIN,  HIGH);

  if (digitalRead(DebugPin) == LOW) {
    SerialDEBUG = true;
    wifiManagerDebugOutput = true;
    Serial.begin(115200);
    printSerial("Programmstart...");
  }

  if (digitalRead(TasterPin) == LOW) {
    startWifiManager = true;
    bool state = LOW;
    for (int i = 0; i < 7; i++) {
      state = !state;
      digitalWrite(LED_BUILTIN, state);
      delay(100);
    }
  }

  OneWireMode = (digitalRead(DS18B20active) == LOW);
  printSerial((OneWireMode) ? "DS18B20 - OneWire - Modus aktiv!" : "DHT22 - Modus aktiv!");

  loadSystemConfig();

  if (doWifiConnect()) {
    printSerial("WLAN erfolgreich verbunden!");
    if (OneWireMode) {
      DS18B20Sensors.begin();
      DS18B20Sensors.requestTemperatures();
      float t = DS18B20Sensors.getTempCByIndex(0);
      printSerial("Setze CCU-Wert Temperatur = " + String(t));
      setStateCCU("SET_TEMPERATURE", String(DS18B20Sensors.getTempCByIndex(0)));
    } else {
      dht.begin();
      float h = dht.readHumidity();
      float t = dht.readTemperature();

      while (isnan(t) || isnan(h))
      {
        printSerial("Warte auf Werte");
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        delay(3000);
      }
      printSerial("Setze CCU-Werte Temperatur = " + String(t) + ", Feuchte = " + String(h));
      setStateCCU("SET_TEMPERATURE", String(t));
      setStateCCU("SET_HUMIDITY", String(h));
    }
  } else ESP.restart();
  delay(100);
  printSerial("Gehe schlafen für " + String(sleepTimeMin) + " Minuten");
  ESP.deepSleep((String(sleepTimeMin)).toInt() * 1000000 * 60);
  delay(100);
}

void setStateCCU(String type, String value) {
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String url = "http://" + String(ccuip) + ":8181/cuxd.exe?ret=dom.GetObject(%22CUxD." + CUxDDevice + ":1." + type + "%22).State(" + value + ")";
    printSerial("URL = " + url);
    http.begin(url);
    int httpCode = http.GET();
    printSerial("httpcode = " + String(httpCode));
    if (httpCode > 0) {
      //     String payload = http.getString();
    }
    if (httpCode != 200) {
      printSerial("HTTP fail " + String(httpCode));
    }
    http.end();
  } else ESP.restart();
}

void printSerial(String text) {
  if (SerialDEBUG) {
    Serial.println(text);
  }
}

bool doWifiConnect() {
  String _ssid = WiFi.SSID();
  String _psk = WiFi.psk();

  const char* ipStr = ip; byte ipBytes[4]; parseBytes(ipStr, '.', ipBytes, 4, 10);
  const char* netmaskStr = netmask; byte netmaskBytes[4]; parseBytes(netmaskStr, '.', netmaskBytes, 4, 10);
  const char* gwStr = gw; byte gwBytes[4]; parseBytes(gwStr, '.', gwBytes, 4, 10);

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(wifiManagerDebugOutput);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  WiFiManagerParameter custom_ccuip("ccu", "IP der CCU2", ccuip, 16);

  char CUxDDeviceAsCHAR[50];
  CUxDDevice.toCharArray(CUxDDeviceAsCHAR, 50);
  WiFiManagerParameter custom_cuxddevicename("cuxddevice", "CUxD Device Seriennumer", CUxDDeviceAsCHAR, 50);
  WiFiManagerParameter custom_sleeptime("sleeptime", "&Uuml;bertragung alle x Minuten", sleepTimeMin, 4);

  WiFiManagerParameter custom_ip("custom_ip", "IP-Adresse", "", 16);
  WiFiManagerParameter custom_netmask("custom_netmask", "Netzmaske", "", 16);
  WiFiManagerParameter custom_gw("custom_gw", "Gateway", "", 16);

  const char * text = (OneWireMode) ? "<center><h5>DS18B20 Sensor-Modus</h5></center>" : "<center><h5>DHT22/AM2302 Sensor-Modus</h5></center>";
  WiFiManagerParameter custom_modetext(text);

  wifiManager.addParameter(&custom_modetext);
  wifiManager.addParameter(&custom_ccuip);
  wifiManager.addParameter(&custom_cuxddevicename);
  wifiManager.addParameter(&custom_sleeptime);
  WiFiManagerParameter custom_text("<br/><br>Statische IP (wenn leer, dann DHCP):");
  wifiManager.addParameter(&custom_text);
  wifiManager.addParameter(&custom_ip);
  wifiManager.addParameter(&custom_netmask);
  wifiManager.addParameter(&custom_gw);

  String Hostname = "WemosD1-" + WiFi.macAddress();
  char a[] = "";
  Hostname.toCharArray(a, 30);

  wifiManager.setConfigPortalTimeout(ConfigPortalTimeout);

  if (startWifiManager == true) {
    digitalWrite(LED_BUILTIN, LOW);
    if (_ssid == "" || _psk == "" ) {
      wifiManager.resetSettings();
    }
    else {
      if (!wifiManager.startConfigPortal(a)) {
        printSerial("failed to connect and hit timeout");
        delay(1000);
        ESP.restart();
      }
    }
  }

  wifiManager.setSTAStaticIPConfig(IPAddress(ipBytes[0], ipBytes[1], ipBytes[2], ipBytes[3]), IPAddress(gwBytes[0], gwBytes[1], gwBytes[2], gwBytes[3]), IPAddress(netmaskBytes[0], netmaskBytes[1], netmaskBytes[2], netmaskBytes[3]));

  wifiManager.autoConnect(a);

  printSerial("Wifi Connected");
  printSerial("CUSTOM STATIC IP: " + String(ip) + " Netmask: " + String(netmask) + " GW: " + String(gw));
  if (shouldSaveConfig) {
    SPIFFS.begin();
    printSerial("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    if (String(custom_ip.getValue()).length() > 5) {
      printSerial("Custom IP Address is set!");
      strcpy(ip, custom_ip.getValue());
      strcpy(netmask, custom_netmask.getValue());
      strcpy(gw, custom_gw.getValue());
    } else {
      strcpy(ip,      "0.0.0.0");
      strcpy(netmask, "0.0.0.0");
      strcpy(gw,      "0.0.0.0");
    }
    strcpy(ccuip, custom_ccuip.getValue());
    CUxDDevice = custom_cuxddevicename.getValue();
    json["ip"] = ip;
    json["netmask"] = netmask;
    json["gw"] = gw;
    json["ccuip"] = ccuip;
    json["cuxddevice"] = CUxDDevice;
    json["sleeptime"] = String(custom_sleeptime.getValue()).toInt();

    SPIFFS.remove("/" + configJsonFile);
    File configFile = SPIFFS.open("/" + configJsonFile, "w");
    if (!configFile) {
      printSerial("failed to open config file for writing");
    }

    if (SerialDEBUG) {
      json.printTo(Serial);
      Serial.println("");
    }
    json.printTo(configFile);
    configFile.close();

    SPIFFS.end();
    delay(100);
    ESP.restart();
  }

  return true;
}


void configModeCallback (WiFiManager *myWiFiManager) {
  printSerial("AP-Modus ist aktiv!");
}

void saveConfigCallback () {
  printSerial("Should save config");
  shouldSaveConfig = true;
}

void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
  for (int i = 0; i < maxBytes; i++) {
    bytes[i] = strtoul(str, NULL, base);
    str = strchr(str, sep);
    if (str == NULL || *str == '\0') {
      break;
    }
    str++;
  }
}

bool loadSystemConfig() {
  printSerial("mounting FS...");
  if (SPIFFS.begin()) {
    printSerial("mounted file system");
    if (SPIFFS.exists("/" + configJsonFile)) {
      printSerial("reading config file");
      File configFile = SPIFFS.open("/" + configJsonFile, "r");
      if (configFile) {
        printSerial("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (SerialDEBUG) {
          json.printTo(Serial);
          Serial.println("");
        }
        if (json.success()) {
          printSerial("\nparsed json");
          strcpy(ip,          json["ip"]);
          strcpy(netmask,    json["netmask"]);
          strcpy(gw,         json["gw"]);
          strcpy(ccuip,      json["ccuip"]);
          const char* jsoncuxddevice = json["cuxddevice"];
          CUxDDevice = jsoncuxddevice;
          itoa(json["sleeptime"], sleepTimeMin, 10);
        } else {
          printSerial("failed to load json config");
        }
      }
      return true;
    } else {
      printSerial("/" + configJsonFile + " not found.");
      return false;
    }
    SPIFFS.end();
  } else {
    printSerial("failed to mount FS");
    return false;
  }
}


void loop() {
}
