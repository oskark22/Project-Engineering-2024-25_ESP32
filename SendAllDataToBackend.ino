#include <WiFi.h>                               //Allows ESP32 to connect to the internet
#include <HTTPClient.h>                         //Make HTTP GET, POST, PUT requests to a web server
#include <TinyGPS++.h>                          // Library decodes the NMEA sentences into useful GPS information
#include <HardwareSerial.h>                     //Allows you to use the UART.Read() function to collect serial data from a software serial port


#define rxPin 16       //GPIO16
#define txPin 17

const char* ssid = "Dan's iPhone";
const char* password = "";
const char* serverURL = "http://172.20.10.2:8000/api/sendMessage";

HTTPClient http;
TinyGPSPlus gnss;
HardwareSerial gnssSerial(1);                   //Create an instance of HardwareSerial for UART1




void setup() {
  // put your setup code here, to run once:

  pinMode(16, INPUT);                           //RX pin receives data
  pinMode(17, OUTPUT);

  Serial.begin(9600);
  gnssSerial.begin(9600, SERIAL_8N1, rxPin, txPin);         //Initialize UART 1. SERIAL_8N1: format being sent over serial connection is 8 data bits, No parity bit, 1 stop bit


  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());             //If you need, get the boards IP address*/

}

void loop() {
  // put your main code here, to run repeatedly:
  String sentence = "";
  String satelliteList = "[";

  while (gnssSerial.available() > 0) {                                                        //Check if data is available
    char ch = gnssSerial.read();                                                              //Read a character from the GNSS module
    Serial.write(ch);                                                                         //Output raw data to the serial monitor
    if (ch == '\n') {                                                                         //End of sentence. Full NMEA sentence is received
      if (sentence.startsWith("$GPGSV")) {
        String GPSsatelliteId = extractSatelliteID(sentence);                                   //Return a comma-separated string of satellite IDs
        if (GPSsatelliteId.length() > 0) {
          //Serial.println("Extracted IDs: " + GPSsatelliteId);
          int firstIndex = 0, lastIndex = 0;
          while ((lastIndex = GPSsatelliteId.indexOf(",", firstIndex)) != -1) {               //Search for first occurrence of "," starting from firstIndex. If character is not found, return -1
            String GPSid = GPSsatelliteId.substring(firstIndex, lastIndex);
            satelliteList += "{\"id\":\"" + GPSid + "\",\"type\":\"GPS\",\"country\":\"USA\"},";
            firstIndex = lastIndex + 1;
          }
        }
        //satelliteList += "{\"id\":\"" + satelliteId + "\",\"type\":\"GPS\",\"country\":\"USA\"},";
      }
      else if (sentence.startsWith("$GLGSV")) {
        String GLONASSsatelliteId = extractSatelliteID(sentence);
        //Separate the satellite IDs
        if (GLONASSsatelliteId.length() > 0) {
          //Serial.println("Extracted IDs: " + GLONASSsatelliteId);
          int startIndex = 0, endIndex = 0;
          while ((endIndex = GLONASSsatelliteId.indexOf(",", startIndex)) != -1) {
            String GLONASSid = GLONASSsatelliteId.substring(startIndex, endIndex);
            satelliteList += "{\"id\":\"" + GLONASSid + "\",\"type\":\"GLONASS\",\"country\":\"Russia\"},";
            startIndex = endIndex + 1;
          }
          //satelliteList += "{\"id\":\"" + satelliteId + "\",\"type\":\"GLONASS\",\"country\":\"Russia\"},";
        }
      }
      sentence = "";                                                                         //Reset the sentence to collect next NMEA sentence
    }
    else {
      sentence += ch;                                                                        //Add character to sentence
    }
  }
  if (satelliteList.length() > 1) {                                                          // 1 is the opening bracket [, length is 1. > 1 = length is over 1
    satelliteList.remove(satelliteList.length() - 1);                                        //Remove the last comma to keep the JSON valid
    satelliteList += "]";
    //Serial.println(satelliteList);
    sendSatellitesToServer(satelliteList);
  }

  delay(5000);
}

String extractSatelliteID(String nmeaSentence) {
  int commas = 0;
  bool getID = false;
  String satelliteIDs = "";
  bool firstSatelliteID = true;


  for (int i = 0; i < nmeaSentence.length(); i++) {
    char character = nmeaSentence[i];

    if (character == ',') {
      commas++;

      //Satellite IDs start at the 4th comma and repeat every 4 commas. If commas is 4, 4>=4 is true, 4-4 = 0, 0%4 = 0 is true, if statement will execute.
      if (commas >= 4 && (commas - 4) % 4 == 0) {
        getID = true;
      }
      else {
        getID = false;
      }
    }
    else if (getID) {
      //Extract satellite ID
      int startIndex = i;
      int endIndex = nmeaSentence.indexOf(",", startIndex);
      if (endIndex == -1) {                                         //No more commas left. -1 is returned by indexOf() when the character you're searching for is not found
        endIndex = nmeaSentence.length();                           //Set the index length to the total sentence length
      }
      String satID = nmeaSentence.substring(startIndex, endIndex);

      //if NMEA sentence is corrupted. If a new NMEA sentence is detected in the middle of the existing NMEA sentence
      /*if (satID.startsWith("0$GP") || satID.startsWith("0$GL")) {
        break;
      }*/
      
      //if NMEA sentence is corrupted.
      if(satID.length() > 3) {
        break;
      }
      

      //Ignore IDs with '*'
      if (satID.indexOf('*') != -1) {
        break;
      }


      if (satID.length() > 0) {
        if (!firstSatelliteID) {                                    //If it's not the first satellite PRN, place a comma
          satelliteIDs += ",";                                     //Add comma before new satellite ID
        }
        satelliteIDs += satID;                       //Add to JSON list
        firstSatelliteID = false;
      }
      i = endIndex - 1;                                             //Move index forward. Ensure the loop correctly continues from the next comma.
    }
  }
  return satelliteIDs;
  //Close JSON list
  //satelliteList += "]";
  //sendSatellitesToServer(satelliteList);
  //Serial.println("Extracted satellite IDs: " + satelliteList);
}



void sendSatellitesToServer(const String& satelliteList) {
  if (WiFi.status() == WL_CONNECTED) {
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    String payLoad = "{\"satellites\":" + satelliteList + "}";
    int httpStatusCode = http.POST(payLoad);

    if (httpStatusCode > 0) {
      Serial.println("Data sent: " + String(httpStatusCode));
    }
    else {
      Serial.println("Error sending data: " + String(httpStatusCode));
    }
    http.end();
  }
  else {
    Serial.println("WiFi not connected");
  }
}
