#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>


/* LCD INITIALIZATIONS */
// Set the I2C address of the LCD display
#define LCD_ADDR 0x27

// To define other pins for SDA and SCL for ESP32
#define I2C_SDA 18
#define I2C_SCL 19

// Set the number of columns and rows of the LCD display
#define LCD_COLS 16
#define LCD_ROWS 2
/* END OF LCD INITIALIZATIONS */

int error_number = 0; // 0: All good, 1: Cannot get time

// Replace with your network credentials
const char* ssid = "SOH-FAMILY-GUEST"; //Enter SSID
const char* password = "s0H7f4miL0G@_u3*"; //Enter Password

//Watering initializations:
char s_currTime[20]; // This variable is to save the formatted string of current time that is to be printed out to LCD. It ensures that the things printed are of format: 00/00 00:00:00, with all the digits in place even if it were to just be a single digit: e.g. 01/02 instead of 1/2
char nextWaterSess[20];

// Moisture Sensor initializations: 
const int moisture_pin = 34;
int moisture_level = 0;
int moisture_threshold = 2400; // Threshold to define "watered"
int watering_time = 5000; // How long to on the pump for. 1000 = 1s

// Water Pump initializations:
const int pump_pin = 32;
int pump_level = 0;

// Local SG date and time initializations:
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;
int* getTime; // Get current time
struct tm timeinfo; // Used for getting one week later using epoch

// One week threshold initialization:
int one_week = 20;// Context: 1 = 1s. One week = 604800s;

// Initialize array to hold date and time details.
int prevTime[7] = {0, 0, 0, 0, 0, 0, 0}; //0: Day of month, 1: Month, 2: Year, 3: Epoch, 4: Seconds, 5: Minutes, 6: Hours
int currTime[7] = {0, 0, 0, 0, 0, 0, 0}; //0: Day of month, 1: Month, 2: Year, 3: Epoch, 4: Seconds, 5: Minutes, 6: Hours

// LCD Variable Declarations:
int screen_flag = 0; // Used to help put display on hold (aka no flickering of words as LCD constantly refreshes in loop())
char wantPrint_1[20]; // Things you want to print on row 1 of LCD screen
char wantPrint_2[20]; // Things you want to print on row 2 of LCD screen

// ************ ThingSpeak Credentials **********
unsigned long readChannelID = 2119618; // enter your Channel ID
char readAPIKey[] = "6IGVCIG1EHSL6Z3F"; // Change to channel read API key
char mqttUserName[] = "GzUKGCQPAiAQEAgDDiIVGA8"; // Use given username
char* clientID = mqttUserName;
char mqttPass[] = "PIloqIeasDTTDNmI8962yvLE"; // MQTT broker password
const char *server = "mqtt3.thingspeak.com";
// **********************************************

// For MQTT:
WiFiClient client; // Initialize the Wi-Fi client library.
PubSubClient mqttClient(client); // Initialize the PuBSubClient library.

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

void setup() {
  pinMode(moisture_pin, INPUT);
  pinMode(pump_pin, OUTPUT);
  Serial.begin(115200);
  delay(1000);

  connectToWiFi();

  //Time configuration:
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Initialize the LCD display
  Wire.begin(I2C_SDA, I2C_SCL); // Configuring of custom pins for SDA and SCL pin of ESP32
  Wire.setClock(5000); //Set clock to a lower speed to prevent random characters from showing due to high refresh rate
  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.init(); // Initialize the LCD
  lcd.backlight(); // On LCD backlight
  lcd.clear();
  lcd.setCursor(0,0); // Set cursor to first row first column.
  printLCD("Version 1", "-By: STR");
  delay(2000);

  // Set up MQTT:
  mqttClient.setServer(server, 1883); 
  mqttClient.setCallback(callback); // The callback is where the mobile phone watering system will be
}

void loop() {
  // Check if MQTT is connected:
  if (!mqttClient.connected()) // Reconnect if connection is lost
  {
    reconnect();
    if (mqttSubscribe(readChannelID,0,readAPIKey,0)) { //readChannelID,1,readAPIKey,0)
      Serial.println("Subscribed to topic");
    }
  }

  // Your code here
  Serial.print("Curr epoch: ");
  Serial.println(currTime[3]);
  Serial.print("Next epoch: ");
  Serial.println(prevTime[3]+one_week);
  moisture_level = analogRead(moisture_pin); // Read the moisture sensor via analog. 4095 if it is dry. Lesser than 2300 if fully watered. 
  getTime = localTime(); //0: Day of month, 1: Month, 2: Year, 3: Epoch

  f1_oneWeekAlgo();

  mqttClient.loop();

  delay(10); // <- fixes some issues with WiFi stability
}

void connectToWiFi() {
  String ipAdd;  // Replace this with your actual String object

  //Connect to WiFi Network
  WiFi.mode(WIFI_STA); //Optional
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting");

  while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(100);
  }

  ipAdd = String(WiFi.localIP());
  char charArray[ipAdd.length() + 1];  // +1 for null terminator
  ipAdd.toCharArray(charArray, ipAdd.length() + 1);

  printLCD("Wifi Connected!", charArray);
  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());

  return;
}

/* MQTT SECTION */
// Connect or Reconnect to MQTT
void reconnect() {
  //char* clientID = mqttUserName;
  while (!mqttClient.connected()) {
    Serial.println("\nAttempting MQTT connection...");

    if (mqttClient.connect(clientID, mqttUserName, mqttPass)) {
    Serial.println("Connected with Client ID: " + String(clientID));
    Serial.println('\n');
    } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.print( "Try again in 5 seconds");
    delay(5000);
    }
  }
  return;
}

int mqttSubscribe(long subChannelID, int field, char* readKey, int unsubSub) {
  String myTopic;

  if (field==0) {
    myTopic="channels/"+String(subChannelID)+"/subscribe/fields/+";
  }
  else {
    //myTopic="channels/"+String(subChannelID)+"/subscribe/fields/+"; //Subscribes to all field
    myTopic="channels/"+String(subChannelID)+"/subscribe/fields/field"+String(field);
  }

  Serial.println("Subscribing to " + myTopic);
  Serial.print("State = ");
  Serial.println(mqttClient.state());

  if (unsubSub==1) {
    return mqttClient.unsubscribe(myTopic.c_str());
  }

  return mqttClient.subscribe(myTopic.c_str(), 0);
}

// Handle incoming messages from the broker
// This function only display the text as read
void callback(char* myTopic, byte* payload, unsigned int length) {
  Serial.println("-----------------------");
  char p[length+1];
  Serial.print("Received message:");
  memcpy(p, payload, length);
  p[length]=NULL;
  Serial.println(String(p));

  if(*p == '1'){
    getTime = localTime();
    prevTime[3] = getTime[3];
    printLCD("Phone Control", "Watering...");
    waterPlants();
    
    Serial.print("CALLBACK EPOCH: ");
    Serial.println(prevTime[3]);
    epochToLocalTime((prevTime[3]+one_week), &timeinfo);
    strftime(nextWaterSess, sizeof(nextWaterSess), "%d/%m %H:%M:%S", &timeinfo);
    strcpy(wantPrint_1, "Next water time:");
    printLCD(wantPrint_1, nextWaterSess);
  }
  return;
}
/* END OF MQTT SECTION */

int* localTime() {
  static int timeArray[7];
  struct tm timeinfo;
  time_t now; // Used for Epoch

  if (!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    error_number = 1;
    return NULL;
  }

  timeArray[0] = timeinfo.tm_mday;
  timeArray[1] = timeinfo.tm_mon + 1;       /* 'tm_mon' returns month, range 0 to 11 */
  timeArray[2] = timeinfo.tm_year + 1900;   /* 'tm_year' returns the number of years since 1900 */
  timeArray[3] = (int)time(&now);           /* Epoch */
  timeArray[4] = timeinfo.tm_sec;           /* 'tm_sec' returns seconds, from 0 to 61 */
  timeArray[5] = timeinfo.tm_min;           /* 'tm_min' returns minutes, from 0 to 59 */
  timeArray[6] = timeinfo.tm_hour;          /* 'tm_hour' returns hours, from 0 to 23 */
  //Serial.print(String(timeArray[6]) + ':' + timeArray[5] + ':' + timeArray[4]);
  return timeArray;
}

void epochToLocalTime(unsigned long epochTime, struct tm* timeinfo) {
  // Convert epoch time to local time, accounting for the timezone offset and daylight saving time
  //epochTime += gmtOffset_sec + daylightOffset_sec;
  time_t localTime = (time_t)epochTime;
  
  // Convert local time to struct tm format
  localtime_r(&localTime, timeinfo);
  return;
}

void printLCD(char lcd_row1[20], char lcd_row2[20]){
  //LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
  //lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.clear();
  lcd.print(lcd_row1);
  lcd.setCursor(0,1);
  lcd.print(lcd_row2);
  delay(1000);
  return;
}

void waterPlants(){
  Serial.println("Watering...");
  digitalWrite(pump_pin, 1);
  delay(watering_time);
  digitalWrite(pump_pin, 0);
  return;
}

void f1_oneWeekAlgo(){ // Function 1: One week water the plants by itself algo
  getTime = localTime();

  if(error_number == 0){ // Ensure that there is no error
    currTime[0] = getTime[0];
    currTime[1] = getTime[1];
    currTime[2] = getTime[2];
    currTime[3] = getTime[3];
    currTime[4] = getTime[4];
    currTime[5] = getTime[5];
    currTime[6] = getTime[6];

    if((currTime[3]-prevTime[3]) >= one_week){ // If one week threshold of last watered is up, auto water
      epochToLocalTime(currTime[3], &timeinfo);
      strftime(s_currTime, sizeof(s_currTime), "%d/%m %H:%M:%S", &timeinfo);

      strcpy(wantPrint_1, "Watering time!  ");
      strcpy(wantPrint_2, s_currTime);
      //Serial.println("Now: " + String(currTime[0]) + '/' + String(currTime[1]) + '/' + String(currTime[2]) + " " + String(currTime[6]) + ':' + String(currTime[5]) + ':' + String(currTime[4]));
      printLCD(wantPrint_1, wantPrint_2);
      memcpy(prevTime, currTime, sizeof(currTime)); // Copy array to prev time
      waterPlants(); 
      screen_flag = 0; // Toggle off screen_flag to be able to print the next watering time
    }

    else{ // If one week threshold not up, check up on plants
      if(screen_flag == 0){ //If screen_flag toggle is off, display on screen
        //Convert the e==poch time of one week later to local time:
        epochToLocalTime((prevTime[3]+one_week), &timeinfo);

        // Save next watering session date and time into variable nextWaterSess
        strftime(nextWaterSess, sizeof(nextWaterSess), "%d/%m %H:%M:%S", &timeinfo);

        //Print out the date it was last watered:
        //Serial.print("Last watered: ");
        //Serial.println(String(prevTime[0]) + '/' + String(prevTime[1]) + '/' + String(prevTime[2]) + " " + String(prevTime[6]) + ':' + String(prevTime[5]) + ':' + String(prevTime[4]));
        strcpy(wantPrint_1, "Next water time:");
        printLCD(wantPrint_1, nextWaterSess);
        //Serial.print("Next water session: ");
        //Serial.println(nextWaterSess);
        screen_flag = 1;
      }
    }
    delay(1000);
  }

  else{ // Catch errors
    if (error_number == 1){
      Serial.println("ERROR: CANT GET TIME");
    }
    else{
      Serial.println("UNKNOWN ERROR");
    }
  }
  return;
}