#define CFG_INI_FILE "/cfg.ini"

/*
#define D01(x) Serial.println(x)
#define D02(x1,x2) Serial.println(x1 + x2)
#define GET_MACRO(_1,_2,NAME,...) NAME
#define D(...) GET_MACRO(__VA_ARGS__, DO2, DO1)(__VA_ARGS__)
*/

#define DEBUG(x) Serial.println(x)
#define MAX_WIFI_CONNECT_RETRY 20

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
    CLed m_led;

    void setState(STATE newState, bool up, bool down, bool led)
    {
      m_currState = newState;
      m_up.TurnBool(up);
      m_down.TurnBool(down);
      m_led.TurnBool(led);
      m_lastActionTime = millis();
    }
  
  public:  
    CShutter(int pinUp, int pinDown, int pinLed, int timeToRoll) :
      m_up(pinUp), 
      m_down(pinDown), 
      m_led(pinLed), 
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
};


/**
  Program objects
*/

CShutter shutter(4,5,14,10000);
bool bConnected = false;
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
  
    while ((WiFi.status() != WL_CONNECTED) && (retries++ < MAX_WIFI_CONNECT_RETRY)) {
      delay(500);
      Serial.print(".");
    }

    if(WiFi.status() != WL_CONNECTED)
    {
      DEBUG("max retries reached, clearing settings and restarting");
      iniFile.clearAll();
      ESP.restart();
    }

    DEBUG("WiFi connected");
    DEBUG("IP address: ");
    DEBUG(WiFi.localIP());  
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
    bConnected = false;
    DEBUG("failed to load WiFi settings - setting captive AP");
    WiFiAPCaptive captive("new_shutter_cfg",&iniFile);
    captive.start();
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);
  mountFS();

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
  
  // begin WebServer
  webServer.on("/up",handleRollUp);
  webServer.on("/off",handleOff);    
  webServer.on("/down",handleRollDown);
  webServer.begin();
  
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
}

void loop() {
  delay(50);
  
  if(WiFi.status() != WL_CONNECTED)
  {
    DEBUG("WiFi connection lost - restarting ESP");
    bConnected = false;
    // restart to initiate reconnecting loop
    ESP.restart();
  }
  else
  {
    webServer.handleClient();
    shutter.onLoop();
  }
}
