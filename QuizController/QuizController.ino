////////////////////////////////////////////////////////////////
// Wireless Quiz Buzzer System                                //
// Copyright (C) RobSmithDev 2022                             //
// GPL3 Licence                                               //
////////////////////////////////////////////////////////////////
// Video: https://youtu.be/b3iqji1DUG0
// https://robsmithdev.co.uk
// https://youtube.com/c/robsmithdev

#include <RF24.h>
#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>


//   2 -> White LED to GND  (Status)
//   3 -> RED LED to GND    (Button Status)
//   4 -> YELLOW LED to GND (Button Status)
//   5 -> GREEN LED to GND  (Button Status)
//   6 -> BLUE LED to GND   (Button Status)
//   7 -> RESET btn to GND
//   8 -> READY btn to GND
//   9 -> CE  (nRF24)
//  10 -> CSN (nRF24)
//  11 -> MO  (nRF24)
//  12 -> MI  (nRF24)
//  13 -> SCK (nRF24)
//  A0 -> 1K Reststor -> RX on DFPlayerMini
//  A1 -> TX on DFPlayerMini


RF24 radio(9, 10); // CE, CSN

#define LED_STATUS      2     // Status LED
#define BTN_RESET       7
#define BTN_READY       8

#define DFMINI_TX       A0   // connect to pin 2 on the DFPlayer via a 1K resistor
#define DFMINI_RX       A1   // connect to pin 3 on the DFPlayer
SoftwareSerial softwareSerial(DFMINI_RX, DFMINI_TX);

// Player
// Tip: If you have any problems with the DFPlayerMini, power it from the Arduino's 3.3v pin rather than 5v.
DFRobotDFPlayerMini player;

// LED pins
unsigned char BTN_LEDS[4] = {3, 4, 5, 6};

// LED status type
enum LedStatus : unsigned char { lsOff = 0, lsOn = 1, lsFlashing = 2 };

// Status we want to share with the buttons
LedStatus ledStatus[4]  = {lsOff, lsOff, lsOff, lsOff};
bool buttonEnabled[4]   = {false, false, false, false};
bool buttonConnected[4] = {false, false, false, false};
bool hasAnswered[4]     = {false, false, false, false};
unsigned long lastContact[4] = {0, 0, 0, 0};

// Last loop time
unsigned long lastLoopTime = 0;

// System status
bool isReady = false;

// Is audio playing?
bool isPlaying = false;
bool dfPlayerReady = false;

// searches the radio spectrum for a quiet channel
bool findEmptyChannel() {
  Serial.write("Scanning for empty channel...\n");
  char buffer[10];

  // Scan all channels looking for a quiet one.  We skip every 10
  for (int channel = 125; channel > 0; channel -= 10) {
    radio.setChannel(channel);
    delay(20);

    unsigned int inUse = 0;
    unsigned long testStart = millis();
    // Check for 400 ms per channel
    while (millis() - testStart < 400) {
      digitalWrite(LED_STATUS, millis() % 500 > 400);
      if ((radio.testCarrier()) || (radio.testRPD())) inUse++;
      delay(1);
    }

    // Low usage?
    if (inUse < 10) {
      itoa(channel, buffer, 10);
      Serial.write("Channel ");
      Serial.write(buffer);
      Serial.write(" selected\n");
      return true;
    }
  }
  return false;
}

// Sends a new ACK payload to the transmitter
void setupACKPayload() {
  // Update the ACK for the next payload
  unsigned char payload[4];
  for (unsigned char button=0; button<4; button++)
      payload[button] = (buttonEnabled[button] ? 128 : 0) | ledStatus[button];
  radio.writeAckPayload(1, &payload, 4);
}

// Check for messages from the buttons
void checkRadioMessageReceived() {
  // Check if data is available
  if (radio.available()) {
    unsigned char buffer;
    radio.read(&buffer, 1);

    // Grab the button number from the data
    unsigned char buttonNumber = buffer & 0x7F; // Get the button number
    if ((buttonNumber >= 1) && (buttonNumber <= 4)) {
      buttonNumber--;

      // Update the last contact time for this button
      lastContact[buttonNumber] = lastLoopTime;

      // And that it's connected
      buttonConnected[buttonNumber] = true;

      // If the button was pressed, was enabled, hasn't answered and the system is ready for button presses
      if ((buffer & 128) && (buttonEnabled[buttonNumber]) && (!hasAnswered[buttonNumber]) && (isReady)) {
        // No longer ready
        isReady = false;

        if (dfPlayerReady) {
          player.play(buttonNumber + 1);
          isPlaying = true;
        }

        // Signal the button was pressed
        hasAnswered[buttonNumber] = true;

        // Change button status
        for (unsigned char btn = 0; btn < 4; btn++)
          ledStatus[btn] = (btn == buttonNumber) ? lsOn : lsOff;

        // Turn off the ready light
        digitalWrite(LED_STATUS, LOW);
      }
    }

    setupACKPayload();
  }
}

// Setup the controller
void setup() {
  // put your setup code here, to run once:
  Serial.begin(57600);
  while (!Serial) {};

  // small delay to allow the DFPlayerMini to boot
  delay(1000);

  // For the DFPlayerMini
  softwareSerial.begin(9600);
  if (player.begin(softwareSerial)) {
    player.volume(30);
    dfPlayerReady = true;
  }

  // Setup the radio device
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setDataRate(RF24_250KBPS);
  radio.setRetries(4, 8);
  radio.maskIRQ(false, false, false);  // not using the IRQs

  // Setup our I/O
  pinMode(LED_STATUS, OUTPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_READY, INPUT_PULLUP);


  if (!radio.isChipConnected()) {
    Serial.write("RF24 device not detected.\n");
  } else {
    Serial.write("RF24 detected.\n");

    // Trun off the LED
    digitalWrite(LED_STATUS, LOW);

    // Now setup the pipes for the four buttons
    char pipe[6] = "0QBTN";
    radio.openWritingPipe((uint8_t*)pipe);
    pipe[0] = '1';
    radio.openReadingPipe(1, (uint8_t*)pipe);
    for (char channel = 0; channel < 4; channel++) {
      pinMode(BTN_LEDS[channel], OUTPUT);
      digitalWrite(BTN_LEDS[channel], LOW);
    }

    // Start listening for messages
    radio.startListening();

    // Find an empty channel to run on
    while (!findEmptyChannel()) {};

    // Start listening for messages
    radio.startListening();
    
    // Ready
    digitalWrite(LED_STATUS, LOW);

    setupACKPayload();
  }
}

// Main loop
void loop() {
  lastLoopTime = millis();


  if (digitalRead(BTN_RESET) == LOW) {                 // Reset button pressed?
    // Turn all buttons off
    for (unsigned char button = 0; button < 4; button++) {
      ledStatus[button] = lsOff;
      buttonEnabled[button] = false;
      hasAnswered[button] = false;
      if (isPlaying) {
        player.stop();
        isPlaying = false;
      }
    }
    isReady = false;
    digitalWrite(LED_STATUS, LOW);
  } else if (digitalRead(BTN_READY) == LOW) {                // Ready button pressed
    // Make the buttons flash that havent answered yet
    for (unsigned char button = 0; button < 4; button++) {
      buttonEnabled[button] = !hasAnswered[button];
      ledStatus[button] = hasAnswered[button] ? lsOff : lsFlashing;
    }
    isReady = true;
    if (isPlaying) {
      player.stop();
      isPlaying = false;
    }
    digitalWrite(LED_STATUS, HIGH);
  }

  // Update our LEDs and monitor for ones that are out of contact
  for (unsigned char button = 0; button < 4; button++) {
    // If the button is connected
    if (buttonConnected[button]) {
      // If its been 1 second since we heard from it
      if (lastLoopTime - lastContact[button] > 1000) {
        // Disconnect it
        buttonConnected[button] = false;
        digitalWrite(BTN_LEDS[button], LOW);
      } else {
        // Set the LED to match the state we have it in
        digitalWrite(BTN_LEDS[button], (ledStatus[button] == lsOn) || ((ledStatus[button] == lsFlashing) && (lastLoopTime & 255) > 128));
      }
    } else {
      // For disconnected ones we just give a short 'blip' once per few second
      digitalWrite(BTN_LEDS[button], (lastLoopTime & 2047) > 2000);
    }
  }

  // Check for messages on the 'network'
  checkRadioMessageReceived();
}
