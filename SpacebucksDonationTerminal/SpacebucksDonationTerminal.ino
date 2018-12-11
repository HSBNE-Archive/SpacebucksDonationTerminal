/*
   Spacebucks Donation Terminal firmare.
   This firmware runs on hardware designed for HSBNE. It allows members of HSBNE (Hackerpsace Brisbane)
   to donate to causes with spacebucks.

   Released under GPL v3 or later by Jaimyn Mayer (@jabelone).
*/

#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header
#include <ESP8266WiFi.h>                  // WiFi Library
#include <ESP8266WiFiMulti.h>             // WiFi Library
#include <SoftwareSerial.h>               // Software Serial Library
#include <PN532_SWHSU.h>                  // PN532 Serial Interface
#include <PN532.h>                        // PN532 Library

// config
const String welcomeText = "Tap Card/Phone";
const int initAmount = 500; // Initial amount (in cents)
const int buttonClickTimeout = 10000; // Milliseconds before everything resets
const int LCD_COLS = 16;
const int LCD_ROWS = 2;
const int I2C_SDA = D4;
const int I2C_SCL = D3;
const int PN_RX = D7;
const int PN_TX = D8;
const int BTN_PLUS = D0;
const int BTN_OK = D2;
const int BTN_MINUS = D1;

hd44780_I2Cexp lcd; // declare lcd object: auto locate & auto config expander chip
ESP8266WiFiMulti wifiMulti; // WiFi object
SoftwareSerial SWSerial(PN_RX, PN_TX); // RX, TX
PN532_SWHSU pn532swhsu(SWSerial);
PN532 nfc(pn532swhsu);

// Variables
int donateAmount = initAmount;
unsigned long lastButtonPress = 0;
int currentBalance = 0;
String currentRfidUID = "";
bool confirmAmount = false;
bool updateAmount = true;

void setup()
{
  pinMode(BTN_PLUS, INPUT);
  pinMode(BTN_OK, INPUT);
  pinMode(BTN_MINUS, INPUT );

  Serial.begin(9600);
  Serial.println("Beginning Setup");

  Serial.println("Starting i2c bus");
  Wire.begin(D4, D3);

  int status;
  status = lcd.begin(LCD_COLS, LCD_ROWS);
  if (status) // non zero status means it was unsuccesful
  {
    status = -status; // convert negative status value to positive number
    Serial.println("Failed to initialise LCD screen.");
    Serial.println("Error: " + status);

    // begin() failed so blink error code using the onboard LED if possible
    hd44780::fatalError(status); // does not return
  }

  lcd.lineWrap(); // turn on auto line wrap
  lcd.clear();
  Serial.print("Setting up NFC.");
  lcd.print("Setting up NFC");
  setupNFC();

  Serial.print("Waiting for WiFi.");
  lcd.clear();
  lcd.print("Waiting For WiFi");
  lcd.blink();

  WiFi.mode(WIFI_STA);
  // Add your WiFi network(s) below
  wifiMulti.addAP("HSBNEWiFi", "HSBNEPortHack");
  //wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(250);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.print("WiFi Connected! IP: ");
  lcd.print(WiFi.localIP());
  lcd.noBlink();
  delay(2000);
  lcd.clear();
  lcd.print("Ready");
}

void setupNFC() {
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }

  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.setPassiveActivationRetries(0xEE);

  // configure board to read RFID tags
  nfc.SAMConfig();
}

String printPN532Response(uint8_t *response, uint8_t responseLength) {
  String respBuffer;

  for (int i = 0; i < responseLength; i++) {
    if (response[i] < 0x10) {
      respBuffer = respBuffer + "0"; //Adds leading zeros if hex value is smaller than 0x10
    }
    respBuffer = respBuffer + String(response[i], HEX);
  }

  Serial.print("response: "); Serial.println(respBuffer);
  return respBuffer;
}

void loop() {
  if (confirmAmount) {
    // Update the screen if necessary
    if(updateAmount) {
      lcd.clear();
      String line = "     $" + String(float(donateAmount)/100, 2);
      lcd.write(line.c_str());
      lcd.setCursor(0, 2);
      lcd.write("Confirm Amount?");
      lcd.blink();
      updateAmount = false;
    }

    if (lastButtonPress + buttonClickTimeout < millis()) {
      lcd.clear();
      lcd.write("Timed Out...");
      lcd.noBlink();
      donateAmount = initAmount;
      lastButtonPress = millis();
      currentBalance = 0;
      currentRfidUID = "";
      confirmAmount = false;
      updateAmount = false;
      delay(1500);
    }

    // Handle the "ok" button click
    if(!digitalRead(BTN_OK)) {
      delay(20);
      if(!digitalRead(BTN_OK)) {
        processOkClick();
      }
    }

    // Handle our "+" button click
    if(!digitalRead(BTN_PLUS)) {
      delay(20);
      if(!digitalRead(BTN_PLUS)) {
        delay(200);
        updateAmount = true;
        lastButtonPress = millis();
        if (donateAmount >= 300) {
          donateAmount = donateAmount + 100;
        } else if (donateAmount >= 100 && donateAmount <300) {
          donateAmount = donateAmount + 50;
        } else {
          donateAmount = donateAmount + 25;
        }
      }
    }

    // Handle our "-" button click
    if(!digitalRead(BTN_MINUS)) {
      delay(20);
      if(!digitalRead(BTN_MINUS)) {
        delay(200);
        updateAmount = true;
        lastButtonPress = millis();
        if (donateAmount <= 25) {
        }
        else if (donateAmount > 300) {
          donateAmount = donateAmount - 100;
        } else if (donateAmount > 100 && donateAmount <=300) {
          donateAmount = donateAmount - 50;
        } else {
          donateAmount = donateAmount - 25;
        }
      }
    }
    
  } else {
    lcd.clear();
    lcd.write(welcomeText.c_str());
    lcd.blink();

    bool success;
    uint8_t responseLength = 32;
    uint8_t response[responseLength];

    if (nfc.inListPassiveTarget()) {
      Serial.println("Found something!");
      uint8_t selectApdu[] = { 0x00, /* CLA */
                               0xA4, /* INS */
                               0x04, /* P1  */
                               0x00, /* P2  */
                               0x07, /* Length of AID  */
                               0xF0, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, /* AID must match Android App */
                               0x00  /* Le  */
                             };

      // Attempt to connect to a phone by broadcasting an AID
      if (nfc.inDataExchange(selectApdu, sizeof(selectApdu), response, &responseLength)) {
        yield();
        Serial.print("responseLength: ");
        Serial.println(responseLength);
        lcd.clear();
        lcd.write("Found Card:     ");
        // We found a card so lets get the UUID, log it, and process the swipe
        String rfidUID = printPN532Response(response, responseLength);
        lcd.write(rfidUID.c_str());
        processSwipe(rfidUID);
        
      } else { // if the above fails it's probably a card
        Serial.println("Failed sending SELECT AID - trying card read");

        boolean success;
        uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
        uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

        success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);
        yield();
        
        if (success) {
          Serial.println("Found a card!");
          Serial.print("UID Length: ");
          Serial.print(uidLength, DEC);
          Serial.println(" bytes");
          Serial.print("UID Value: ");
          lcd.clear();
          lcd.write("Found 13mhz card");
          
          // We found a card so lets get the UUID and process the swipe
          String rfidUID = printPN532Response(uid, uidLength);
          lcd.write(rfidUID.c_str());
          processSwipe(rfidUID);
        }
      }
    }
  }
}

bool processSwipe(String rfidUID) {
  currentRfidUID = rfidUID;
  lastButtonPress = millis();
  lcd.clear();
  lcd.write("Please wait");
  String balance = getBalance(rfidUID);
  lcd.clear();
  lcd.noBlink();
  lcd.write("Current Balance:     $");
  lcd.write(balance.c_str());
  delay(2000);
  confirmAmount = true;
  updateAmount = true;
}

void processOkClick() {
  confirmAmount = false;
  if (donateMoney(currentRfidUID, donateAmount)) {
    lcd.noBlink();
    lcd.clear();
    String line = "Donated $" + String(float(donateAmount)/100, 2);
    lcd.write(line.c_str());
    lcd.setCursor(0, 2);
    lcd.write("succesfully!");
    delay(3000);
  } else {
    lcd.noBlink();
    lcd.clear();
    lcd.write("Failed to donate   :(");
    delay(1000);
  }
  currentRfidUID = "";
  donateAmount = initAmount;
  confirmAmount = false;
}

int donateMoney(String rfidUID, int amount) {
  // Donates the specified amount (in cents) to the specified member.
  // TODO: Actually implement the web request.
  delay(500);
  return true;
}

String getBalance(String rfidUID) {
  // Returns the balance of a member (in cents) from their RFID UID.
  // TODO: Actually implement the web request.
  delay(500);
  int balance = 350;
  String bal = String(float(balance) / 100, 2);
  Serial.println(bal);
  return bal;
}
