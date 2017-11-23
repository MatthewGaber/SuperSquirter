#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include <SD.h>
#include "memorysaver.h"

//Espino 192.168.1.20 Port 81
//Arduino 192.168.1.21 Port 82

//Set your WiFi details
const char* ssid="###.";             // yourSSID
const char* password="####";     // yourPASSWORD

static bool hasSD = false;
File uploadFile;

ESP8266WebServer server(82);

const int CS = 16;
//Version 2,set GPIO0 as the slave select :
const int SD_CS = 0;

static const size_t bufferSize = 2048; //4096;
static uint8_t buffer[bufferSize] = {0xFF};
uint8_t temp = 0, temp_last = 0;
int i = 0;
bool is_header = false;

#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  ArduCAM myCAM(OV2640, CS);
#endif

void start_capture(){
  myCAM.clear_fifo_flag();
  myCAM.start_capture();
}

void myCAMSaveToSDFile(){
  WiFiClient client = server.client();
  server.send(200, "text/plain", "This is response to client");
  //Probably don't need to loop over all this, !!research loop over buffer
  for (uint8_t q=0; q<10; q++){
      char str[8];
      byte buf[256];
      static int i = 0;
      static int k = 0;
      uint8_t temp = 0, temp_last = 0;
      uint32_t length = 0;
      bool is_header = false;
      File outFile;
      //Flush the FIFO
      myCAM.flush_fifo();
      //Clear the capture done flag
      myCAM.clear_fifo_flag();
      //Start capture
      myCAM.start_capture();
      Serial.println(F("Star Capture"));
      while(!myCAM.get_bit(ARDUCHIP_TRIG , CAP_DONE_MASK));
      Serial.println(F("Capture Done."));  
      
      length = myCAM.read_fifo_length();
      Serial.print(F("The fifo length is :"));
      Serial.println(length, DEC);
      if (length >= MAX_FIFO_SIZE) //8M
      {
        Serial.println(F("Over size."));
      }
        if (length == 0 ) //0 kb
      {
        Serial.println(F("Size is 0."));
      }
      //Construct a file name
      k = k + 1;
      itoa(k, str, 10);
      strcat(str, ".jpg");
      //Open the new file
      outFile = SD.open(str, O_WRITE | O_CREAT | O_TRUNC);
      if(! outFile){
      Serial.println(F("File open faild"));
      return;
      }
      i = 0;
      myCAM.CS_LOW();
      myCAM.set_fifo_burst();
      
      while ( length-- )
      {
        temp_last = temp;
        temp =  SPI.transfer(0x00);
        //Read JPEG data from FIFO
        if ( (temp == 0xD9) && (temp_last == 0xFF) ) //If find the end ,break while,
        {
            buf[i++] = temp;  //save the last  0XD9     
           //Write the remain bytes in the buffer
            myCAM.CS_HIGH();
            outFile.write(buf, i);    
          //Close the file
            outFile.close();
            Serial.println(F("Image save OK."));
            is_header = false;
            i = 0;
        }  
        if (is_header == true)
        { 
           //Write image data to buffer if not full
            if (i < 256)
            buf[i++] = temp;
            else
            {
              //Write 256 bytes image data to file
              myCAM.CS_HIGH();
              outFile.write(buf, 256);
              i = 0;
              buf[i++] = temp;
              myCAM.CS_LOW();
              myCAM.set_fifo_burst();
            }        
        }
        else if ((temp == 0xD8) & (temp_last == 0xFF))
        {
          is_header = true;
          buf[i++] = temp_last;
          buf[i++] = temp;   
        } 
      }
  } 
}


void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}


void serverStream(){
  WiFiClient client = server.client();
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);
  
  while (1){   
    start_capture();
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
    size_t len = myCAM.read_fifo_length();
    if (len >= MAX_FIFO_SIZE) //8M
    {
    Serial.println(F("Over size."));
    continue;
    }
    if (len == 0 ) //0 kb
    {
    Serial.println(F("Size is 0."));
    continue;
    } 
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    if (!client.connected()) break;
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response); 
    while ( len-- )
    {
    temp_last = temp;
    temp =  SPI.transfer(0x00);
    
    //Read JPEG data from FIFO
    if ( (temp == 0xD9) && (temp_last == 0xFF) ) //If find the end ,break while,
    {
    buffer[i++] = temp;  //save the last  0XD9     
    //Write the remain bytes in the buffer
    myCAM.CS_HIGH();; 
    if (!client.connected()) break;
    client.write(&buffer[0], i);
    is_header = false;
    i = 0;
    }  
    if (is_header == true)
    { 
    //Write image data to buffer if not full
    if (i < bufferSize)
    buffer[i++] = temp;
    else
    {
    //Write bufferSize bytes image data to file
    myCAM.CS_HIGH(); 
    if (!client.connected()) break;
    client.write(&buffer[0], bufferSize);
    i = 0;
    buffer[i++] = temp;
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    }        
    }
    else if ((temp == 0xD8) & (temp_last == 0xFF))
    {
    is_header = true;
    buffer[i++] = temp_last;
    buffer[i++] = temp;   
    } 
    }
    if (!client.connected()) break;
   }
}

bool loadFromSdCard(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";

  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";

  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()){
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile)
    return false;

  if (server.hasArg("download")) dataType = "application/octet-stream";

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    //DBG_OUTPUT_PORT.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void returnOK() {
  server.send(200, "text/plain", "");
}


void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    if(SD.exists((char *)upload.filename.c_str())) SD.remove((char *)upload.filename.c_str());
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    //DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(upload.filename);
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    //DBG_OUTPUT_PORT.print("Upload: WRITE, Bytes: "); DBG_OUTPUT_PORT.println(upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(uploadFile) uploadFile.close();
    //DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while(true) {
    File entry = file.openNextFile();
    if (!entry) break;
    String entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
}


void handleCreate(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}
void printDirectory() {
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String path = server.arg("dir");
  if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  File dir = SD.open((char *)path.c_str());
  path = String();
  if(!dir.isDirectory()){
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry)
    break;

    String output;
    if (cnt > 0)
      output = ',';

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
 }
 server.sendContent("]");
 dir.close();
}
void handleNotFound(){
  if(hasSD && loadFromSdCard(server.uri())) return;
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  
}

void setup() {
 
  uint8_t vid, pid;
  uint8_t temp;
  #if defined(__SAM3X8E__)
    Wire1.begin();
  #else
    Wire.begin();
  #endif
  
  Serial.begin(115200); 
  
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED)delay(500);
  WiFi.mode(WIFI_STA);

// set the CS as an output:
  pinMode(CS, OUTPUT);
  
  // initialize SPI:
  SPI.begin();
  SPI.setFrequency(4000000); //4MHz
  
  //Check if the ArduCAM SPI bus is OK
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if (temp != 0x55){
  Serial.println(F("SPI1 interface Error!"));
  while(1);
  }

  if(!SD.begin(SD_CS)){
    Serial.println(F("SD Card Error"));
   } 
  else {
  Serial.println(F("SD Card detected!"));
  hasSD = true;
   }
   
  #if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  //Check if the camera module type is OV2640
  myCAM.wrSensorReg8_8(0xff, 0x01);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  #endif
  if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 )))
    Serial.println(F("Can't find OV2640 module!"));
  else
    Serial.println(F("OV2640 detected."));
 
  //Change to JPEG capture mode and initialize the OV2640 module
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  #if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  myCAM.OV2640_set_JPEG_size(OV2640_320x240);
  
  #endif
  
  myCAM.clear_fifo_flag();

  server.on("/stream", HTTP_GET, serverStream);
  server.on("/savepic", HTTP_GET, myCAMSaveToSDFile);  
  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, [](){ returnOK(); }, handleFileUpload);
  server.onNotFound(handleNotFound);
  server.begin();
 
}

void loop() {
 
  server.handleClient();
  
}

