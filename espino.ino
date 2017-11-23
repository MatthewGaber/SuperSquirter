#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include "memorysaver.h"
#include "FS.h"
#include <ESP8266HTTPClient.h>

//Set your WiFi details
const char* ssid="###.";             // yourSSID
const char* password="####";     // yourPASSWORD

int horSlider=110;     
int vertSlider=103; // Default value of the slider
Servo horServo;
Servo vertServo;

// Interval is the delay between a client connection and the motion sensor being able to trigger again.
// Stops the Arduino trying to write to the SD Card and Stream at the same time.
int interval=10000;
// Tracks the time since last client connection.
unsigned long previousMillis=0;

const int PIR_MOTION_SENSOR = 15;
const int PUMP_PIN = 13;

//Set the port number here
ESP8266WebServer server(81);

bool motion = true;
bool hadClient;

void handleHorSlider() {  
  horSlider = server.arg("hor").toInt();
  horServo.write(horSlider);
}

void handleVerSlider() {    
  vertSlider = server.arg("ver").toInt();
  vertServo.write(vertSlider);
}

void handleSetButton() {
  digitalWrite(PUMP_PIN, HIGH); 
}

void handleButtonRelease() {  
  digitalWrite(PUMP_PIN, LOW);  
}

void motChanged() {
  String mot_status = server.arg("mot");
  if (mot_status == "ON")
    motion = true;
  else if (mot_status == "OFF")
    motion = false;
}

// This moves the pan tilt arm around and turns the pump on when motion is detected.
// Change the delays and servo writes as required
void handleMotion() {
  if(motion==true){
    horServo.write(110);
    vertServo.write(103);
    digitalWrite(PUMP_PIN, HIGH);
    horServo.write(150);
    vertServo.write(80); 
    delay(250);
    vertServo.write(120);
    delay(250);
    horServo.write(80);
    delay(250);
    vertServo.write(103);
    delay(250);
    horServo.write(110);
    delay(1250);   
    digitalWrite(PUMP_PIN, LOW);
    delay(4000);
    }
  
  else{
    digitalWrite(PUMP_PIN, HIGH);
    delay(2500);
    digitalWrite(PUMP_PIN, LOW);
    delay(4000);
    
  }
  
}


void setup() {
  pinMode(PIR_MOTION_SENSOR, INPUT);
  horServo.attach(5);
  vertServo.attach(16);  
  pinMode(PUMP_PIN, OUTPUT); 
  Serial.begin(115200);


  
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED)delay(500);
  WiFi.mode(WIFI_STA);
  //Serial.println("\n\nBOOTING ESP8266 ...");
  //Serial.print("Connected to ");
  //Serial.println(ssid);
  //Serial.print("Station IP address = ");
  //Serial.println(WiFi.localIP());

  // Start the server
  server.on("/setHorSlider", HTTP_POST, handleHorSlider);
  server.on("/setVerSlider", HTTP_POST, handleVerSlider);
  server.on("/setButton", HTTP_POST, handleSetButton);
  server.on("/releaseButton", HTTP_POST, handleButtonRelease);
  server.on("/motButton", HTTP_POST, motChanged);

// If serving a htm from SPIFFS   
  //SPIFFS.begin();
  //server.serveStatic("/index.html", SPIFFS, "/index.html");
  //server.serveStatic("/js", SPIFFS, "/js");
  //server.serveStatic("/css", SPIFFS, "/css", "max-age=86400");
  //server.serveStatic("/images", SPIFFS, "/images", "max-age=86400");
  //server.serveStatic("/", SPIFFS, "/index.html");  
  //webSocket.begin();
  //webSocket.onEvent(webSocketEvent);

  server.begin();
  
  //This is not great but the PIR will go high (false triggers) during the first 30 or so seconds.
  //Stops the pump from turning on and off during the first 30 seconds after power up.
  delay(30000);
  

}
boolean isMove()
{
    int sensorValue = digitalRead(PIR_MOTION_SENSOR);
    
    if(sensorValue == HIGH)
    {
        return true;//yes,return true
    }
    else
    {
        return false;//no,return false
    }
}

void msgArduino() {
  unsigned long currentMillis = millis();
  WiFiClient client = server.client();
  if ((unsigned long)(currentMillis - previousMillis) >= interval){
    
    if(isMove()){
        handleMotion();
        HTTPClient http;
        http.begin("http://192.168.1.18:84/savepic");
        int httpCode = http.GET();
        http.end();  
    }          
  }

  if (client.connected()){
    previousMillis = currentMillis;
  }
  
}

void loop() {
  //webSocket.loop();
  server.handleClient();
  msgArduino();

}
  


