#define CFG_INI_FILE "/cfg.ini"

/*
#define D01(x) Serial.println(x)
#define D02(x1,x2) Serial.println(x1 + x2)
#define GET_MACRO(_1,_2,NAME,...) NAME
#define D(...) GET_MACRO(__VA_ARGS__, DO2, DO1)(__VA_ARGS__)
*/

#define DEBUG(x) Serial.println(x)
#define MAX_WIFI_CONNECT_RETRY 20
#define MAX_WIFI_RECONNECT_RETRY 500

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>

#include <switch.h>
#include <config.h>
#include <WiFiConfigurator.h>

#include "FS.h" 


class CShutter
{
  public:
    enum STATE {up, down, off};
  
  private:
    STATE m_currState;
    int m_timeToRoll;
    int m_lastActionTime;

    CReverseSwitch m_up;
    CReverseSwitch m_down;
    CBaseSwitch* m_led;

    void setState(STATE newState, bool up, bool down, bool led)
    {
      m_currState = newState;
      m_up.TurnBool(up);
      m_down.TurnBool(down);
      m_led->TurnBool(led);
      m_lastActionTime = millis();
    }
  
  public:  
    CShutter(int pinUp, int pinDown, CBaseSwitch* pLed, int timeToRoll) :
      m_up(pinUp), 
      m_down(pinDown), 
      m_led(pLed), 
      m_timeToRoll(timeToRoll),
      m_currState(STATE::off) {}

    void turnUp()
    {
      setState(STATE::up, true, false, true);
    }


    void turnDown()
    {
      setState(STATE::down, false, true, true);
    }

    void turnOff()
    {
      setState(STATE::off, false, false, false);
    }

    void onLoop()
    {
      // check if we are in the middle of the action
      if(m_currState != STATE::off)
      {

        int currTime = millis();
        
        // check for overflow - if so, stop
        if(currTime < m_lastActionTime)
        {
          turnOff();
        }
        // if enough time elapsed - stop it
        else if(currTime - m_lastActionTime > m_timeToRoll)
        {
          turnOff();
        }         
      }
    }

    STATE getState() const {return m_currState;}

    void setTimeToRoll(int timeRoll) {m_timeToRoll = timeRoll;} 
};


/**
  Program objects
*/

CReverseSwitch greenLed(12);
CReverseSwitch redLed(13);
CReverseSwitch blueLed(14);

CShutter shutter(4,5,&greenLed,10000);
bool bConnected = false;
int reconnect_retry;
ESP8266WebServer webServer(80);
IniFile iniFile(CFG_INI_FILE);

// mount the file system
void mountFS()
{
  bool mountResult = SPIFFS.begin();
  
  delay(5000);
  
  if(mountResult)
    Serial.print("FS mount result: "); Serial.println(mountResult);  
}

void connectToWiFi()
{
    int retries = 0;

    bool blueLedState = false;
    while ((WiFi.status() != WL_CONNECTED) && (retries++ < MAX_WIFI_CONNECT_RETRY)) {
      blueLedState = !blueLedState;
      blueLed.TurnBool(blueLedState);
      delay(500);
      Serial.print(".");
    }

    if(WiFi.status() != WL_CONNECTED)
    {
      onSetError();
      DEBUG("max retries reached, clearing settings and restarting");
      iniFile.clearAll();
      ESP.restart();
    }

    DEBUG("WiFi connected");
    DEBUG("IP address: ");
    DEBUG(WiFi.localIP());  
    blueLed.TurnOff();
}

void printFSRoot()
{
    
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      DEBUG(dir.fileName());
  }
}

void startCaptive()
{
    blueLed.TurnOn();
    bConnected = false;
    DEBUG("failed to load WiFi settings - setting captive AP");
    WiFiAPCaptive captive("new_shutter_cfg",&iniFile);
    captive.start();
    blueLed.TurnOff();
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);
  mountFS();
  reconnect_retry = 0;

  printFSRoot();

  if(!iniFile.exists())
  {
    startCaptive();    
  }
  
  String wifiName = iniFile.getValue("WIFI_NAME");
  String wifiPassword = iniFile.getValue("WIFI_PASSWORD");
  wifiName.trim();
  wifiPassword.trim();
  
  DEBUG("connecting to WiFi: [" + wifiName + "," + wifiPassword + "]");
  // connect to WiFi
  WiFi.mode(WIFI_STA);
  if(!wifiPassword.length())  
  {
    WiFi.begin(wifiName.c_str());    
  }
  else{
    WiFi.begin(wifiName.c_str(), wifiPassword.c_str());
  }
  connectToWiFi();
  bConnected = true;


  String mdnsName = iniFile.getValue("MDNS_NAME");
  mdnsName.trim();
  DEBUG("setting up MDNS name " + mdnsName);
  // setup MDNS
  if (!MDNS.begin(mdnsName.c_str())) {
      DEBUG("Error setting up MDNS responder!");
  }
  else{
    DEBUG("mDNS responder started");
  }

  int rollTimeInSeconds = iniFile.getValue("ROLL_TIME").toInt();
  // check if error 
  if(rollTimeInSeconds == 0 )
  {
    DEBUG("failed to convert roll time" + iniFile.getValue("ROLL_TIME"));
    onSetError();
  }
  else{
    DEBUG("setting new roll time [SEC] " + String(rollTimeInSeconds,DEC));
    shutter.setTimeToRoll(rollTimeInSeconds * 1000);
  }
  
  
  // begin WebServer
  webServer.on("/up",handleRollUp);
  webServer.on("/off",handleOff);    
  webServer.on("/down",handleRollDown);

  webServer.onFileUpload([](){
      if(webServer.uri() != "/update") return;
      HTTPUpload& upload = webServer.upload();
      if(upload.status == UPLOAD_FILE_START){
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){//start with max available size
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_WRITE){
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });

    webServer.on("/update", HTTP_POST, [](){
      webServer.sendHeader("Connection", "close");
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    });
  
  webServer.begin();
  
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
}

void loop() {
  delay(50);
  
  if(WiFi.status() != WL_CONNECTED)
  {
    onSetError();
    DEBUG("WiFi connection lost - restarting ESP, [status] " + String(WiFi.status(), DEC));
    if(reconnect_retry++ > MAX_WIFI_RECONNECT_RETRY)
    {
      bConnected = false;
      // restart to initiate reconnecting loop
      ESP.restart();
    }
  }
  else
  {
    onClearError();
    reconnect_retry = 0;
    webServer.handleClient();
    shutter.onLoop();
  }
}

void onSetError(){redLed.TurnOn();}
void onClearError(){redLed.TurnOff();}
