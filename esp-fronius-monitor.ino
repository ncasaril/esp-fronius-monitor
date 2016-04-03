/*

  Fronius Solar generation and Load monitor

  Niklas Casaril 2016
  This code is in the public domain.

*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <ArduinoJson.h>

extern "C" {
#include "user_interface.h"
}

typedef struct{
    float g;
    float l;
} solargraph_t;

void StartUp_OLED();
static void init_OLED(void);
static void clear_display(void);
static void clear_display_row(unsigned char k);
void displayOff(void);
void displayOn(void);
static void sendcommand(unsigned char com);
static void setXY(unsigned char row,unsigned char col);
static void SendChar(unsigned char data);
static void sendCharXY(unsigned char data, int X, int Y);
static void sendStrXY( char *string, int X, int Y);
void Draw_Plot(solargraph_t *slog, int fi, int maxn);

typedef struct {
    String ssid;
    String pass;
} wifi_nets_t;

#include "networkinfo.h"

// Solarpv generated and usage
IPAddress solarpv_ip(192,168,10,6);
WiFiClient client;
float _generate_w = 0;
float _load_w = 0;
unsigned long _inverter_time = 0;
unsigned long _last_inverter_time = 0;
unsigned long _last_plottime = 0;


solargraph_t _solar_log[128];
int _solar_log_i = 0;
char _jsonbuffer[2048];
char *_jp = _jsonbuffer;
bool _skipHeader = true;

void getCurrentKW()
{
    while (client.available())
    {
        char c = client.read();
        if (_skipHeader && c!='{') continue;
        _skipHeader = false;
        if (c == ' ' || c == '\t' || c=='\n' || c=='\r') continue;
        *(_jp++) = c;    
        //Serial.print(c);
    } 
  
    if (!client.connected())
    {
        if (_jp != _jsonbuffer && _jsonbuffer[0]!=0)
        {
            // Remove trailing 0
            if (_jp > _jsonbuffer && *(_jp-1) == '0') *(_jp-1) = 0;
      
            Serial.println("\nStarting json conversion...");
            Serial.println((int)(_jp - _jsonbuffer));
            Serial.println(_jsonbuffer);
            StaticJsonBuffer<2048> jsonBuffer;
            JsonObject& root = jsonBuffer.parseObject(_jsonbuffer);
            if (root.success())
            {
                Serial.println("  json success");
                _generate_w = root["Body"]["Data"]["Power_P_Generate"]["value"];
                _load_w  = root["Body"]["Data"]["Power_P_Load"]["value"];
                _inverter_time  = root["Body"]["Data"]["TimeStamp"]["value"];
                _inverter_time += 10*3600; // Brisbane
            }
            Serial.println("complete");
        }

    
        Serial.println("\nStarting connection to inverter...");
        for (int i=0;i<sizeof(_jsonbuffer);i++) _jsonbuffer[i]=0;
        _jp = _jsonbuffer;
        _skipHeader = true;

        char buffer[32];
        // if you get a connection, report back via serial:
        if (client.connect(solarpv_ip, 80)) 
        {
            Serial.println("connected to server");
            // Make a HTTP request:
            client.println("GET /components/5/0/?print=names HTTP/1.1");
            String hostip = String("Host: ") + String(solarpv_ip[0]) + '.' + String(solarpv_ip[1]) + '.' + String(solarpv_ip[2]) + '.' + String(solarpv_ip[3]);
            hostip.toCharArray(buffer, 20);

            client.println(buffer);
            client.println("Connection: close");
            client.println();
        }
    }
}


unsigned int localPort = 2390;      // local port to listen for UDP packets


void setup()
{
    Serial.begin(115200);
    delay(2000); // wait for uart to settle and print Espressif blurb..
    Serial.println();
    Serial.println();
    
    // print out all system information
    Serial.print("Heap: "); Serial.println(system_get_free_heap_size());
    Serial.print("Boot Vers: "); Serial.println(system_get_boot_version());
    Serial.print("CPU: "); Serial.println(system_get_cpu_freq());
    
    //Wire.pins(2, 14); // esp12e
    Wire.pins(0, 2); // esp-01
    Wire.begin();
    StartUp_OLED(); // Init Oled and fire up!
    Serial.println("OLED Init...");
    clear_display();
    sendStrXY(" SOLAR POWER   ", 0, 1); // 16 Character max per line with font set
    sendStrXY("   MONITOR     ", 2, 1);
    sendStrXY(" CONNECTING... ", 4, 1);
    
    // We start by connecting to a WiFi network

    int i=0;
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print("Connecting to ");
        Serial.println(networks[i].ssid.c_str());

        WiFi.begin(networks[i].ssid.c_str(), networks[i].pass.c_str());
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout++<20)
        {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        if (WiFi.status() == WL_CONNECTED) break;
        if (networks[++i].ssid=="") i=0;
    }  

    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    clear_display(); // Clear OLED
    sendStrXY("NETWORK DETAILS", 0, 1);
    sendStrXY("NET: ", 3, 1);
    IPAddress ip = WiFi.localIP(); // Convert IP Here
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    char buffer[32];
    networks[i].ssid.toCharArray(buffer, 20);
    sendStrXY((buffer), 3, 6);
    ipStr.toCharArray(buffer, 20);
    sendStrXY((buffer), 6, 1); // Print IP

    // Clearing json buffer
    for (int i=0;i<sizeof(_jsonbuffer);i++) _jsonbuffer[i]=0;
    for (int i=0;i<128;i++)
    {
        _solar_log[i].g = 0;//5000*((i*4)%128)/128.0;
        _solar_log[i].l = 0;//1500+sin(i/8.0)*1000;
    }
}

unsigned long _last_check_t=0;
void loop()
{
    unsigned long t=millis();
    if (t - _last_check_t > 200)
    {
        getCurrentKW();
        _last_check_t = t;
    }
  
    if (_inverter_time > 0 && _last_inverter_time == 0) 
        clear_display();

    if (_inverter_time && _inverter_time != _last_inverter_time)
    {    
        // Current 24h time (from inverter) up the top
        String timestr = "";
        char buffer[32];
        timestr += String((_inverter_time  % 86400L) / 3600) + ":";
        if ( ((_inverter_time % 3600) / 60) < 10 ) timestr += "0";
        timestr += String(((_inverter_time % 3600) / 60)) + ":";
        if ( (_inverter_time % 60) < 10 ) timestr += "0";
        timestr += String(_inverter_time % 60);
        timestr.toCharArray(buffer, 20);
        sendStrXY((buffer), 0, 1); 

        // Solar - Load = Net
        dtostrf(_generate_w/1000.0,3,2,buffer);
        String kwstring = String(buffer);
        dtostrf(_load_w/1000.0,3,2,buffer);
        kwstring += String(buffer) + "=";
        dtostrf((_generate_w+_load_w)/1000.0,3,2,buffer);
        kwstring += String(buffer);
    
        sendStrXY("Sol  Load  Net ", 2, 0); 
        kwstring.toCharArray(buffer, 20);
        sendStrXY((buffer), 3, 0); 
    
        // Graph at the bottom
        if (_inverter_time - _last_plottime > 10)
        {
            _last_plottime = _inverter_time;
            _solar_log[_solar_log_i].g = _generate_w;
            _solar_log[_solar_log_i].l = -_load_w;
            Serial.print("storling gen :");Serial.println(_solar_log[_solar_log_i].g);
            Serial.print("storling load:");Serial.println(_solar_log[_solar_log_i].l);
            if (++_solar_log_i > 127) _solar_log_i=0;
            Serial.println("Plotting data...");
            Draw_Plot(_solar_log, _solar_log_i, 128);
        }
    }
    _last_inverter_time = _inverter_time;
    delay(10);
}

