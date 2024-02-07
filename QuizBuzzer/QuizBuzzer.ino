////////////////////////////////////////////////////////////////
// Wireless Quiz Buzzer System                                //
// Copyright (C) RobSmithDev 2022                             //
// GPL3 Licence                                               //
////////////////////////////////////////////////////////////////
// Video: https://youtu.be/b3iqji1DUG0
// https://robsmithdev.co.uk
// https://youtube.com/c/robsmithdev


#include <RF24.h>
#include <EEPROM.h>

//   4 -> Button to GND 
//   5 -> LED to GND (Button)
//   9 -> CE  (nRF24)
//  10 -> CSN (nRF24)
//  11 -> MO  (nRF24)
//  12 -> MI  (nRF24)
//  13 -> SCK (nRF24)
RF24 radio(9, 10); // CE, CSN

#define PIN_BUTTON   4
#define PIN_LED      5

// LED status options
enum LedStatus : unsigned char { lsOff = 0, lsOn = 1, lsFlashing = 2 };

// Last loop start time
unsigned long lastLoopTime = 0;

// If this is in contact with the controller
bool isConnected = false;
// Last time we sent some status
unsigned long lastStatusSend = 0;
// When the button was pressed down
unsigned long buttonDownTime = 0;
// If the button is enabled
bool buttonEnabled = false;
// Status of the LED
LedStatus ledStatus = lsOff;

// Which button number we are
unsigned char buttonNumber = EEPROM.read(0);

// Main setup function
void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  // put your setup code here, to run once:
  Serial.begin(57600);
  while (!Serial) {};

  while ((buttonNumber<1) || (buttonNumber>4)) {
    // A dirty PWM for dim brightness
    digitalWrite(PIN_LED, HIGH);
    delay(1);
    digitalWrite(PIN_LED, LOW);
    delay(10);
    if (Serial.available()) {
      char id = Serial.read();
      if ((id >= '1') && (id<='4')) {
        buttonNumber = id - '0';
        EEPROM.write(0, buttonNumber);
      }
    }
  }

  // Setup the radio device
  if (!radio.begin()) {
    Serial.write("RF24 device failed to begin\n");
  }
  radio.setPALevel(RF24_PA_LOW);     // Max power
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setDataRate(RF24_250KBPS);
  radio.setRetries(2, 2);
  radio.maskIRQ(false, false, false);  // not using the IRQs

   if (!radio.isChipConnected()) {
     Serial.write("RF24 device not detected.\n");
   } else {  
     Serial.write("RF24 device found\n");
   }

   // Configure the i/o
   char pipe[6] = "1QBTN";
   radio.openWritingPipe((uint8_t*)pipe);
   pipe[0] = '0';
   radio.openReadingPipe(1, (uint8_t*)pipe);
   radio.stopListening();
}

// Search for the button controller channel
bool findButtonController() {
   Serial.write("Searching for controller...\n");

   for (int a = 125; a > 0; a-=10) {
      radio.setChannel(a);
      delay(15);
      // Send a single byte for status
      if (sendButtonStatus(false)) {
        Serial.write("Quiz Controller found on channel ");
        char buffer[10];
        itoa(a,buffer,10);
        Serial.write(buffer);
        Serial.write("\n");
        return true;          
      }
      digitalWrite(PIN_LED, (millis() & 2047) > 2000);
   }

   // Add a 1.5 second pause before trying again (but still flash the LED)
   unsigned long m = millis();
   while (millis() - m < 1500) {
     digitalWrite(PIN_LED, (millis() & 2047) > 2000);
     delay(15);
   }
   
   return false;
}

// Attempt to send the sttaus of the button and receive what we shoudl be doing
bool sendButtonStatus(bool isDown) {
  unsigned char message = buttonNumber;
  if (isDown) message |= 128;

  for (unsigned char retries=0; retries<4; retries++) {  
    // This delay is used incase transmit fails.  We will assume it fails because of data collision with another button.
    // This is inspired by https://www.geeksforgeeks.org/back-off-algorithm-csmacd/
    unsigned int randomDelayAmount = random(1,2+((retries*retries)*2));
    if (radio.write(&message, 1)) {
      if (radio.available()) {
       if (radio.getDynamicPayloadSize() == 4) {
          unsigned char tmp[4];
          radio.read(&tmp, 4);
  
          buttonEnabled = (tmp[buttonNumber-1] & 128) != 0;
          ledStatus = (LedStatus)(tmp[buttonNumber-1] & 3);
          Serial.write("Write OK, ACK Payload\n");
  
          return true;          
        } else {
          // Remove redundant data
          int total = radio.getDynamicPayloadSize();
          unsigned char tmp;
          while (total-- > 0) radio.read(&tmp, 1);
          Serial.write("Write OK, ACK wrong size\n");
          delay(randomDelayAmount);
        }
      } else {
          // This shouldn't really happen, but can sometimes if the controller is busy
          Serial.write("Write OK, no ACK\n");
          return true;
      }
    } else {
       delay(randomDelayAmount);
    }
  }
  
  Serial.write("Write Failed\n");
  return false;
}

// Main loop
void loop() {
  lastLoopTime = millis();
    
  if (radio.isChipConnected()) {

    // If connectin ACK timeout or not connected
    if ((lastLoopTime - lastStatusSend > 1000) || (!isConnected)) {
        // A short blip meaning its powered up, but not working
        while (!findButtonController()) {};
        digitalWrite(PIN_LED, LOW);    
        isConnected = true;
        lastStatusSend = lastLoopTime;
    }  
  
    // If the button was pressed down (and its been 300ms since last check)
    if ((digitalRead(PIN_BUTTON) == LOW) && (lastLoopTime - buttonDownTime>300) && (buttonEnabled)) {
      // This ensures we get a random number sequence unique to this player.  The random number is used to prevent packet collision
      randomSeed(lastLoopTime);       
      // Send the DOWN state
      if (sendButtonStatus(true)) {
        buttonDownTime = lastLoopTime;
        lastStatusSend = lastLoopTime;
      }
    }
  
    // If its been 150ms since last TX send status
    if (lastLoopTime-lastStatusSend > 150) {    
      if (sendButtonStatus(false)) {
        lastStatusSend = lastLoopTime;
      } else delay(10); 
    }

     digitalWrite(PIN_LED, (ledStatus == lsOn) || ((ledStatus == lsFlashing) && ((lastLoopTime & 255)>128)));
  } else {
     // Error flash sequence
     digitalWrite(PIN_LED, (lastLoopTime & 1023) < 100);
  }

  // Slow the main loop down
  delay(1);
}
