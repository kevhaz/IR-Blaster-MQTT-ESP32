/*
Works on ESP32

Receives IR codes on pin RECV_PIN and sends it via MQTT
Receives Codes via MQTT and sends them through SEND_PIN
Code consist of 3 parts: Type, value, and length.

JSON Message format (example):
{"type":"3", "value": "1303526340","length":"32"}

Please receive first a signal (sent by your original remote) and look for 
the three values. Then, build your own MQTT message

Copyright <2017> <Andreas Spiess>

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.

Based on the Examples of the libraries

MQTT = SHA1 Fingerprint=73:58:A2:E1:90:95:AF:D0:43:C7:60:01:39:52:93:35:A0:6B:3E:66

*/
#define MQTT_SOCKET_TIMEOUT 30

#include <IRremote.h>
//#include <credentials.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SimpleTimer.h>

#define SENDTOPIC "IR/key"
#define COMMANDTOPIC "IR/command"
#define SERVICETOPIC "IR/service"
#define SERVICE_ALIVE "Alive"
#define SERVICE_DEAD "Dead"

int RECV_PIN = 12;
int SEND_PIN = 13;
int STATUS_PIN = 23;

decode_results results;

IRrecv irrecv(RECV_PIN);
IRsend irsend(SEND_PIN);

// WiFi
WiFiClient wifiClient;

#ifdef CREDENTIALS
char *ssid      = mySSID;               // Set your WiFi SSID
char *password  = myPASSWORD;               // Set your WiFi password
#else
char *ssid      = "EDGECLIFF";               // Set your WiFi SSID
char *password  = "leapfr0g";                // Set your WiFi password
#endif

// Storage for the recorded code
int codeType = -1; // The type of code
unsigned long codeValue; // The code value if not raw
unsigned int rawCodes[RAWBUF]; // The durations if raw
int codeLen; // The length of the code
int toggle = 0; // The RC5/6 toggle state

//MQTT
IPAddress server(10, 0, 1, 12);
void callback(char*, byte*, unsigned int);
PubSubClient client(server, 1883, callback, wifiClient);

//MQTT Connection Check
SimpleTimer mqtttimer;
int mqtttimerid = 0;


void callback(char* topic, byte* payload, unsigned int length) {
  
  String IRcommand = "";
  int i = 0;

  digitalWrite(STATUS_PIN, HIGH);
  while (i < length) {
    IRcommand = IRcommand + (char)payload[i];
    i++;
  }
  
  Serial.print("IRcommand=");
  Serial.println(IRcommand);

  if ( 0==IRcommand.compareTo("PING") )
  {
    client.publish(SERVICETOPIC, SERVICE_ALIVE);
  } 
  else 
  {
    DynamicJsonBuffer jsonBuffer(200);
    JsonObject& root = jsonBuffer.parseObject(IRcommand);
  
    // Test if parsing succeeds.
    if (!root.success()) {
      Serial.println("parseObject() failed");
      //return;
    }
    int type = root["type"]; 
    unsigned long valu = root["value"];
    int len = root["length"];
    Serial.println("");
    Serial.print("Payload ");
    Serial.println(IRcommand);
    Serial.print("type ");
    Serial.println(type);
    Serial.print("value ");
    Serial.println(valu);
      Serial.print("length ");
    Serial.println(len);
    sendCode(type, valu, len);
  }
  digitalWrite(STATUS_PIN, LOW);  
}



void publishMQTT(String topic, String message) {
  if (!client.connected()) {
    reconnect();
  }
  client.publish(topic.c_str(), message.c_str());
}

// simple MQTT reconnection handler
void reconnect() {

  digitalWrite(STATUS_PIN, HIGH); 
  
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("IRB","homebridge", "leapfr0g", SERVICETOPIC, 1, false, SERVICE_DEAD )) {
      Serial.println("connected");
      client.subscribe(COMMANDTOPIC);
      // Once connected, publish an announcement...
      Serial.println("(3)Sending Alive");
      client.publish(SERVICETOPIC, SERVICE_ALIVE);
      // ... and resubscribe
      //  client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc = ");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      digitalWrite(STATUS_PIN, LOW); 
      delay(500);
      digitalWrite(STATUS_PIN, HIGH); 
      delay(500);
      digitalWrite(STATUS_PIN, LOW);
      delay(500);
      digitalWrite(STATUS_PIN, HIGH); 
      delay(3500);
    }
  }
  digitalWrite(STATUS_PIN, LOW); 
}


void storeCode(decode_results *results) {
  // Stores the code for later playback
  // Most of this code is just logging
  char buf[10];
  String cType = "";
  String IRcommand = "";
  DynamicJsonBuffer jsonBuffer(200);
  JsonObject& root = jsonBuffer.createObject();

  codeType = results->decode_type;
  //int count = results->rawlen;
  if (codeType == UNKNOWN) {
    Serial.println("Received unknown code, saving as raw");
    codeLen = results->rawlen - 1;
    // To store raw codes:
    // Drop first value (gap)
    // Convert from ticks to microseconds
    // Tweak marks shorter, and spaces longer to cancel out IR receiver distortion
    for (int i = 1; i <= codeLen; i++) {
      if (i % 2) {
        // Mark
        rawCodes[i - 1] = results->rawbuf[i] * USECPERTICK - MARK_EXCESS;
        Serial.print(" m");
      }
      else {
        // Space
        rawCodes[i - 1] = results->rawbuf[i] * USECPERTICK + MARK_EXCESS;
        Serial.print(" s");
      }
      Serial.print(rawCodes[i - 1], DEC);
    }
    Serial.println("");
  }
  else {
    if (codeType == NEC) {
      Serial.print("Received NEC: ");
      if (results->value == REPEAT) {
        // Don't record a NEC repeat value as that's useless.
        Serial.println("repeat; ignoring.");
        return;
      } else cType = "NEC";
    }
    else if (codeType == SONY) {
      Serial.print("Received SONY: ");
      cType = "SON";
    }
    else if (codeType == PANASONIC) {
      Serial.print("Received PANASONIC: ");
      cType = "PAN";
    }
    else if (codeType == JVC) {
      Serial.print("Received JVC: ");
      cType = "JVC";
    }
    else if (codeType == RC5) {
      Serial.print("Received RC5: ");
      cType = "RC5";
    }
    else if (codeType == RC6) {
      Serial.print("Received RC6: ");
      cType = "RC6";
    }
    else {
      Serial.print("Unexpected codeType ");
      Serial.print(codeType, DEC);
      Serial.println("");
      cType = "UNKNOWN";
    }
    codeValue = results->value;
    codeLen = results->bits;
    Serial.println(results->value, HEX);
    Serial.println(results->bits);
    root["type"] = codeType;
    root["value"] = results->value;
    root["length"] = results->bits;
    root.printTo(IRcommand);
    root.prettyPrintTo(Serial);
    // call the "safe" publish method which can reconnect if the connection has been lost
    publishMQTT(SENDTOPIC, IRcommand);
  }
}

void sendCode( int codeType, unsigned long codeValue, int codeLen) {
  
  digitalWrite(STATUS_PIN, HIGH);  
  
  if (codeType == NEC) {
    irsend.sendNEC(codeValue, codeLen);
    Serial.print("Sent NEC ");
    Serial.println(codeValue, HEX);
    Serial.println(codeLen);
  }
  else if (codeType == SONY) {
    irsend.sendSony(codeValue, codeLen);
    Serial.print("Sent Sony ");
    Serial.println(codeValue, HEX);
  }
  else if (codeType == PANASONIC) {
    irsend.sendPanasonic(codeValue, codeLen);
    Serial.print("Sent Panasonic");
    Serial.println(codeValue, HEX);
  }
  else if (codeType == JVC) {
    irsend.sendJVC(codeValue, codeLen, false);
    Serial.print("Sent JVC");
    Serial.println(codeValue, HEX);
  }
  else if (codeType == RC5 || codeType == RC6) {
    toggle = 1 - toggle;
    // Put the toggle bit into the code to send
    codeValue = codeValue & ~(1 << (codeLen - 1));
    codeValue = codeValue | (toggle << (codeLen - 1));
    if (codeType == RC5) {
      Serial.print("Sent RC5 ");
      Serial.println(codeValue, HEX);
      irsend.sendRC5(codeValue, codeLen);
    }
    else {
      irsend.sendRC6(codeValue, codeLen);
      Serial.print("Sent RC6 ");
      Serial.println(codeValue, HEX);
    }
  }
  else if (codeType == UNKNOWN /* i.e. raw */) {
    // Assume 38 KHz
    irsend.sendRaw(rawCodes, codeLen, 38);
    Serial.println("Sent raw");
  }

  digitalWrite(STATUS_PIN, LOW);   
}

void mqttConnectionCheck()
{
  Serial.print("Checking connectivity..");
  digitalWrite(STATUS_PIN, HIGH);
  
  bool timerDisabled = false;
  // 1st step is to make sure WiFi is still connected
  if (WiFi.status() != WL_CONNECTED) {

    mqtttimer.disable(mqtttimerid);  
    timerDisabled = true;

    Serial.print("Reconnecting WiFi.");
    // Loop until we're reconnected   
    WiFi.begin (ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      digitalWrite(STATUS_PIN, LOW); 
      delay(500);
      digitalWrite(STATUS_PIN, HIGH); 
    }
    Serial.println(".done!");
  }
  
  if (!client.connected()) {
    // This will reconnect and send an ALIVE message
    mqtttimer.disable(mqtttimerid);
    timerDisabled = true;
    reconnect();
  } else {
    // This is the alive message we want to send on the timer
    Serial.println("(1)Sending Alive");
    client.publish(SERVICETOPIC, SERVICE_ALIVE);   
  }

  if (timerDisabled) {
    // restart the timer if we had to stop it
    mqtttimer.enable(mqtttimerid);
    mqtttimer.restartTimer(mqtttimerid);  
  }
  Serial.println("Check Complete");
  digitalWrite(STATUS_PIN, LOW); 
}

void setup()
{
  Serial.begin(115200);
  pinMode(STATUS_PIN, OUTPUT);
  digitalWrite(STATUS_PIN, HIGH);

  Serial.println("Connecting to Wi - Fi");

  WiFi.mode(WIFI_STA);
  WiFi.begin (ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("WiFi connected");
  irrecv.enableIRIn(); // Start the receiver

  StaticJsonBuffer<200> jsonBuffer;

  // MQTT
  if (client.connect("IRB","homebridge", "leapfr0g", SERVICETOPIC, 1, false, SERVICE_DEAD )) {
    Serial.println("(2)Sending Alive");
    client.publish(SERVICETOPIC, SERVICE_ALIVE);
    client.subscribe(COMMANDTOPIC);
    Serial.println("MQTT Connected");
  } else {
    Serial.println("MQTT Not Connected");
    digitalWrite(STATUS_PIN, LOW);
    delay(500);
    digitalWrite(STATUS_PIN, HIGH);
    delay(500);         
  }

  // check every 2 minutes
  mqtttimerid = mqtttimer.setInterval(120000, mqttConnectionCheck);
  Serial.println("Setup done");
  digitalWrite(STATUS_PIN, LOW);  
}

void loop() {
  mqtttimer.run();
  if (irrecv.decode(&results)) {
    digitalWrite(STATUS_PIN, HIGH);
    storeCode(&results);
    irrecv.resume(); // resume receiver
    digitalWrite(STATUS_PIN, LOW);
  }

  switch(client.state()) {
    case MQTT_CONNECTION_TIMEOUT:
      break;
    case MQTT_CONNECTION_LOST:
      break;
    case MQTT_CONNECT_FAILED:
      break;
    case MQTT_DISCONNECTED:
      break;
    case MQTT_CONNECTED:
      break;           
    case MQTT_CONNECT_BAD_PROTOCOL: 
      break;  
    case MQTT_CONNECT_BAD_CLIENT_ID:  
      break;
    case MQTT_CONNECT_UNAVAILABLE:
      break;    
    case MQTT_CONNECT_BAD_CREDENTIALS: 
      break;
    case MQTT_CONNECT_UNAUTHORIZED: 
      break;  
    default:  
      break; 
  }
  if (!client.loop()) {
    // this will also reconnect wifi if that connection was dropped
    Serial.println("MQTT Disconnected?...Will try to reconnect");
    mqttConnectionCheck();
  }
  
}
