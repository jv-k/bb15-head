/*
 * For Arduino Nano
 * 
 * NOTE: Select "Old Bootleader"
 *
 */

#include <MIDI.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

//#include <Adafruit_SSD1306.h>
//#include <Wire.h>

#ifndef DEBUG
#define DEBUG 1
#endif

// Set-up GPIO

// Note: On ATMEGA328P A6 & A7 are analog input ONLY and cannot be used as output pins
const int SW_BRIGHT_PIN  = A7;
const int SW_DEEP_PIN    = A6;
const int SW_MODE_PIN    = A3;
const int SW_MUTE_PIN    = A5;

const int LED_BRIGHT_PIN = A2;
const int LED_DEEP_PIN   = A1;
const int LED_MODE_PIN   = A4;
const int LED_MUTE_PIN   = A0;

const int REL_MODE_1_PIN = 2;  // 7B ULN2003
const int REL_MODE_2_PIN = 3;  // 6B

const int REL_VOL_1_PIN  = 4;  // 5B
const int REL_VOL_2_PIN  = 5;  // 4B

const int REL_GAIN_1_PIN = 6;  // 3B
const int REL_GAIN_2_PIN = 7;  // 2B

const int REL_BRIGHT_PIN = 8;  // 1B
const int REL_DEEP_PIN   = 12; // 8 NOT CONNECTED RIGHT NOW

const int POPMUTE_PIN        = 9;
const int POPMUTE_DELAY_PRE  = 20; // length of time to mute while relay bounce subsides
const int POPMUTE_DELAY_POST = 20; // length of time to mute while relay bounce subsides

const int MUTE_PIN = 10;

// MISC PINS
const int OLED_RESET = 11;   // Not using the reset, set it to unused pin

// MIDI codes
const int MIDI_RX_PIN = 13; // A6 is a dicky port for writing, and we won't use it
const int MIDI_TX_PIN = 11;  // an unused pin

// Misc
const int AMP_MIDI_CHANNEL = 16; // MIDI_CHANNEL_OMNI

const int CH_LOW         = 1;
const int CH_MID         = 2;
const int CH_HI          = 3;
const int MODE_BRIGHT    = 4;
const int MODE_DEEP      = 5;

int  state_MODE          = 0;
bool state_BRIGHT        = 0;
bool state_DEEP          = 0;
bool state_MUTE          = 0;

// BUTTON states & debounce
bool btnstate_MODE        = 0;
bool btnstate_BRIGHT      = 0;
bool btnstate_DEEP        = 0;
bool btnstate_MUTE        = 0;

bool btn_reading_MODE     = 0;
bool btn_reading_BRIGHT   = 0;
bool btn_reading_DEEP     = 0;
bool btn_reading_MUTE     = 0;

bool btnstate_MODE_last   = 0;
bool btnstate_BRIGHT_last = 0;
bool btnstate_DEEP_last   = 0;
bool btnstate_MUTE_last   = 0;

// Unsigned longs because the time (ms), will quickly become a bigger number than can be stored in an int
unsigned long debounce_time_MODE = 0;  // the last time the output pin was toggled
unsigned long debounce_time_BRIGHT = 0;
unsigned long debounce_time_DEEP = 0;
unsigned long debounce_time_MUTE = 0;

const unsigned long DEBOUNCE_DELAY = 50; // the debounce time; increase if the output flickers

// EEPROM addresses
const int ADDR_MODE      = 0;
const int ADDR_BRIGHT    = 4; // address 1 + 2 + 3 for each mode
const int ADDR_DEEP      = 8; // address 4 + 5 + 6 for each mode

// LED
const int LED_DATA_DELAY = 50; // the pulse width in microseconds (plus the 2 microseconds or so that the digitalWrite command takes)
const float LED_BRIGHTNESS = 0.4;

byte colourOFF[] = {0, 0, 0};
byte colourHI[]  = {255, 0,   0}; //{255, 0,   0} dimmed by scaling from 70%
byte colourMID[] = {255, 60,  0}; //{255, 60,  0} dimmed by scaling from 60%
byte colourLOW[] = {230, 210, 0}; //{230, 210, 0}; dimmed by 60%
byte colourWHT[] = {255, 255, 255};
byte colourBLU[] = {0, 0, 255};

// LED GLOW
byte R = 0;
byte G = 40;
byte B = 200;

int r_inc = 1;
int g_inc = 1;
int b_inc = 1;

byte col[] = {R, G, B};

// DISPLAY
//Adafruit_SSD1306 display(OLED_RESET);

// MIDI init
SoftwareSerial midiSerial(MIDI_RX_PIN, MIDI_TX_PIN);
MIDI_CREATE_INSTANCE(SoftwareSerial, midiSerial, MIDI);

/*
 * Configure Amp Channel
 * 
 * Relay configuration combinations for each mode:

 *                        LOW     MID    HIGH
 *              ------------------------------
 *              REL 1A    NC      NO     NO
 *              REL 1B    NC      NO     NO
 *            
 *              REL 2A    X       NC     NO
 *              REL 2B    X       NC     NO
 *              -----------------------------
 *              So:
 *              REL1      OFF     ON     ON
 *              REL2      x       OFF    ON
 *
 */
void setChannel(int ch) {

  // set global state
  state_MODE = ch;

  // write to EEPROM
  EEPROM.put(ADDR_MODE, state_MODE);

  // mute relay pop
  mutePop();

  delay(POPMUTE_DELAY_PRE); 
  
  setMode();
  loadOptions();
  setBright();
  setDeep();

  delay(POPMUTE_DELAY_POST); 

  unmutePop();
}

void loadOptions() {
  // Load BRIGHT & DEEP configuration for channel
  EEPROM.get(ADDR_BRIGHT + state_MODE - 1, state_BRIGHT);  
  EEPROM.get(ADDR_DEEP   + state_MODE - 1, state_DEEP);    

  // error correction in case of corrupted EEPROM
  if (state_BRIGHT != 1) state_BRIGHT = 0;
  if (state_DEEP != 1) state_DEEP = 0;  
}

void readButtons() {
  btn_reading_MODE = digitalRead(SW_MODE_PIN);
  btn_reading_BRIGHT = analogRead(SW_BRIGHT_PIN) > 700 ? 1 : 0;   // A6 & A7 pins are ADC analog inputs 
  btn_reading_DEEP = analogRead(SW_DEEP_PIN) > 700 ? 1 : 0;
  btn_reading_MUTE = digitalRead(SW_MUTE_PIN);
  //btnstate_DEEP = digitalRead(SW_DEEP_PIN);
  //btnstate_BRIGHT = digitalRead(SW_BRIGHT_PIN);
}

/*
 * If any of the button states has changed, configure amp to those modes
 */
void setStates(){

  // Reset debounce timers if switch changed due to noise or switching
  if (btn_reading_MODE != btnstate_MODE_last)
    debounce_time_MODE = millis();

  // CHANNEL MODE
  if ((millis() - debounce_time_MODE) > DEBOUNCE_DELAY) {
    
    if (btn_reading_MODE != btnstate_MODE) {    
      btnstate_MODE = btn_reading_MODE;
  
      if(btnstate_MODE == HIGH) {
        switch (state_MODE) {
          case CH_LOW:
            setChannel(CH_MID);
            break;
          case CH_MID:
            setChannel(CH_HI);
            break;
          case CH_HI:
            setChannel(CH_LOW);
            break;
        }
      }
    }
  }
  btnstate_MODE_last = btn_reading_MODE; // save reading
    
  // MUTE
  if (btn_reading_MUTE != btnstate_MUTE_last)    
    debounce_time_MUTE = millis();

  if ((millis() - debounce_time_MUTE) > DEBOUNCE_DELAY) {  
    
    if (btn_reading_MUTE != btnstate_MUTE) {    
      btnstate_MUTE = btn_reading_MUTE;
  
      // MUTE BUTTON PUSHED
      if(btnstate_MUTE == HIGH) {
        state_MUTE = !state_MUTE;
        setMute();
      }
    }
  }
  btnstate_MUTE_last = btn_reading_MUTE; // save reading
    
  // BRIGHT
  if (btn_reading_BRIGHT != btnstate_BRIGHT_last)    
    debounce_time_BRIGHT = millis();

  if ((millis() - debounce_time_BRIGHT) > DEBOUNCE_DELAY) {  
    
    if (btn_reading_BRIGHT != btnstate_BRIGHT) {    
      btnstate_BRIGHT = btn_reading_BRIGHT;
  
      if(btnstate_BRIGHT == HIGH) {
        state_BRIGHT = !state_BRIGHT;
        setBright();
      }
    }
  }
  btnstate_BRIGHT_last = btn_reading_BRIGHT; // save reading

  // DEPTH
  if (btn_reading_DEEP != btnstate_DEEP_last)
    debounce_time_DEEP = millis(); 
  
  if ((millis() - debounce_time_DEEP) > DEBOUNCE_DELAY) {
    
    if (btn_reading_DEEP != btnstate_DEEP) {    
      btnstate_DEEP = btn_reading_DEEP;
  
      if(btnstate_DEEP == HIGH) {
        state_DEEP = !state_DEEP;
        setDeep();
      }
    }
  }
  btnstate_DEEP_last = btn_reading_DEEP; // save reading
 
}

void setMode() {
  switch (state_MODE) {
    
    case CH_LOW:
      digitalWrite(REL_MODE_1_PIN, LOW);
      digitalWrite(REL_MODE_2_PIN, LOW);
      digitalWrite(REL_VOL_1_PIN,  LOW); // REL1 OFF 
      digitalWrite(REL_GAIN_1_PIN, LOW);
      digitalWrite(REL_VOL_2_PIN,  LOW); // REL2 OFF 
      digitalWrite(REL_GAIN_2_PIN, LOW);
      setRGBColour(colourLOW, LED_MODE_PIN);
      if (DEBUG) Serial.println("MODE LOW");
      break;
      
    case CH_MID:
      digitalWrite(REL_MODE_1_PIN, HIGH);
      digitalWrite(REL_MODE_2_PIN, LOW);
      digitalWrite(REL_VOL_1_PIN,  HIGH); // REL1 ON
      digitalWrite(REL_GAIN_1_PIN, HIGH);
      digitalWrite(REL_VOL_2_PIN,  LOW);  // REL2 OFF
      digitalWrite(REL_GAIN_2_PIN, LOW);
      setRGBColour(colourMID, LED_MODE_PIN);
      if (DEBUG) Serial.println("MODE MID");
      break;

    case CH_HI:
      digitalWrite(REL_MODE_1_PIN, HIGH);
      digitalWrite(REL_MODE_2_PIN, HIGH);
      digitalWrite(REL_VOL_1_PIN,  HIGH); // REL1 ON
      digitalWrite(REL_GAIN_1_PIN, HIGH);
      digitalWrite(REL_VOL_2_PIN,  HIGH); // REL2 ON
      digitalWrite(REL_GAIN_2_PIN, HIGH);
      setRGBColour(colourHI, LED_MODE_PIN);
      if (DEBUG) Serial.println("MODE HI");
      break;
  
  }  
}

void setBright() {
  digitalWrite(LED_BRIGHT_PIN, state_BRIGHT);
  digitalWrite(REL_BRIGHT_PIN, state_BRIGHT);

  EEPROM.put(ADDR_BRIGHT + state_MODE - 1, state_BRIGHT);  
  
  if (DEBUG) {
    Serial.print("BRIGHT ");
    Serial.println(state_BRIGHT);
  }
}

void setDeep() {
  digitalWrite(LED_DEEP_PIN, state_DEEP);
  digitalWrite(REL_DEEP_PIN, state_DEEP);

  EEPROM.put(ADDR_DEEP + state_MODE - 1, state_DEEP);  
  
  if (DEBUG) {
    Serial.print("DEPTH ");
    Serial.println(state_DEEP, BIN);
  }
}

void setMute() {
  if (state_MUTE == HIGH) {
    muteAmp();
  } else {
    unmuteAmp();    
  }
  
  Serial.print("MUTE ");
  Serial.println(state_MUTE);
}

// like Mesa Boogie circuit
void mutePop() {
  if (DEBUG) Serial.print("MUTE POP");
  digitalWrite(POPMUTE_PIN, HIGH);
}

void unmutePop() {
  if (DEBUG) Serial.print("UNMUTE POP");
  digitalWrite(POPMUTE_PIN, LOW);
}

/*
 * Handle MIDI PC commands
 */
void handleProgramChange(byte channel, byte value) {
  if (DEBUG) {
    Serial.print("MIDI PC ");
    Serial.println(channel);
    Serial.println(value);
  }
    
  if (channel == AMP_MIDI_CHANNEL) {
    switch(value) {
      
      case CH_LOW:
        setChannel(CH_LOW);
        break;
      case CH_MID:
        setChannel(CH_MID);
        break;
      case CH_HI:
        setChannel(CH_HI);
        break;
      default:
        break;
    }
  }

  /*
  display.setCursor(0,0);
  display.clearDisplay();
  display.print("PC CH ");
  display.print(channel);
  display.print('\n');
  display.println(value, DEC); 
  display.println(value, BIN); 
  display.display();
  */
}

/* 
 *  
 *  Handle MIDDI CC commands
 *  
 */
void handleControlChange(byte channel, byte number, byte value) {
  if (DEBUG) {
    Serial.print("MIDI CC ");
    Serial.println(channel);
    Serial.println(number, BIN);
    Serial.println(value);
  }
  /*
  display.setCursor(0,0);
  display.clearDisplay();
  display.print("CC CH ");
  display.print(channel);
  display.print('\n');
  display.println(number, BIN); 
  display.print("VALUE ");
  display.print(value);
  display.display();
  */
}

/*
 * 
 * Set RGB LED colour
 * 
 */
void setRGBColour(byte colour[], int LED_PIN) {
  byte r = colour[0] * LED_BRIGHTNESS;
  byte g = colour[1] * LED_BRIGHTNESS;
  byte b = colour[2] * LED_BRIGHTNESS;
  sendByte(b, LED_PIN); // we send the blue first
  sendByte(g, LED_PIN); // then the green
  sendByte(r, LED_PIN); // then the blue

  delay(3);// datasheet requires at least 3ms between refreshes
}

/*
 * Send byte to RGB LED
 */
void sendByte(byte b, int LED_PIN) {
  // each byte is sent LSB first and is 8 bits long
  for (int n = 7; n >= 0; n--) {
    // if we have a high bit, set up the digitalWrite first, then make the pin an output
    // this will engage the 10K internal pullup resistor, but in compariosn to our 1K potential divider, this shouldn't affect the logic level
    if (bitRead(b, n)) { 
      digitalWrite(LED_PIN, HIGH);
      pinMode(LED_PIN, OUTPUT);
      delayMicroseconds(LED_DATA_DELAY);
      pinMode(LED_PIN, INPUT);
      delayMicroseconds(LED_DATA_DELAY);
    }
    // otherwise, if we have a low bit, make the pin an output first and then check it is LOW
    else {
      pinMode(LED_PIN, OUTPUT);
      digitalWrite(LED_PIN, LOW);
      delayMicroseconds(LED_DATA_DELAY);
      pinMode(LED_PIN, INPUT);
      delayMicroseconds(LED_DATA_DELAY);
    }
  }
}

void LED_Demo(int LED_PIN) {
 
  byte R = 0;
  byte G = 40;
  byte B = 200;
  
  int r_inc = 1;
  int g_inc = 1;
  int b_inc = 1;

  byte col[] = {R, G, B};

  int starttime = millis();
  int endtime = starttime;

  while ((endtime - starttime) <= 3500) {
    col[0] = R;
    col[1] = G;
    col[2] = B;
  
    setRGBColour(col, LED_PIN);
    //place other sendColour commands in here depending on how many LEDs in your string
  
    R += r_inc*1.5;
    G += g_inc*1.5;
    B += b_inc*1.5;
    
    if (R > 254) r_inc = -1;
    if (R < 1)   r_inc = +1;
    
    if (G > 254)    g_inc = -1;
    else if (G < 1) g_inc = +1;
    
    if (B > 254)    b_inc = -1;
    else if (B < 1) b_inc = +1;

    endtime = millis();
  }  

}

/*
 * Initialisations
 */
void setup() {

  // LEDs
  pinMode(LED_BRIGHT_PIN, OUTPUT);
  pinMode(LED_DEEP_PIN, OUTPUT);
  pinMode(LED_MODE_PIN, OUTPUT);
  pinMode(LED_MUTE_PIN, OUTPUT);
  
  setRGBColour(colourOFF, LED_MODE_PIN); // Init RGD LED
  setRGBColour(colourOFF, LED_MUTE_PIN); // Init RGD LED

  // Buttons
  pinMode(SW_MODE_PIN, INPUT);
  pinMode(SW_BRIGHT_PIN, INPUT);
  pinMode(SW_DEEP_PIN, INPUT);
  pinMode(SW_MUTE_PIN, INPUT);

  // Relays
  pinMode(REL_MODE_1_PIN, OUTPUT);
  pinMode(REL_MODE_2_PIN, OUTPUT);
  pinMode(REL_VOL_1_PIN, OUTPUT);
  pinMode(REL_VOL_2_PIN, OUTPUT);
  pinMode(REL_GAIN_1_PIN, OUTPUT);
  pinMode(REL_GAIN_2_PIN, OUTPUT);
  pinMode(REL_BRIGHT_PIN, OUTPUT);
  pinMode(REL_DEEP_PIN, OUTPUT);

  pinMode(MUTE_PIN, OUTPUT);
  pinMode(POPMUTE_PIN, OUTPUT);

  LED_Demo(LED_MUTE_PIN);
  setRGBColour(colourWHT, LED_MUTE_PIN);
 
  delay(1000);
 
  // get previous states
  EEPROM.get(ADDR_MODE, state_MODE);
  if (state_MODE > 3 || state_MODE < 0) state_MODE = CH_LOW;

  Serial.begin(9600);

  // Set-up MIDI
  MIDI.begin(AMP_MIDI_CHANNEL); // Initialize the Midi Library.
  MIDI.setHandleProgramChange  (handleProgramChange); 
  MIDI.setHandleControlChange  (handleControlChange); 

  // Init OLED
  /*
  display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS, 0);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.clearDisplay();
  display.display();
  */

  setChannel(state_MODE);

}

void animateMUTE_LED() {

  col[0] = R;
  col[1] = G;
  col[2] = B;

  setRGBColour(col, LED_MUTE_PIN);
  //place other sendColour commands in here depending on how many LEDs in your string

  R += r_inc*1.5;
  G += g_inc*1.5;
  B += b_inc*1.5;
  
  if (R > 254) r_inc = -1;
  if (R < 1)   r_inc = +1;
  
  if (G > 254)    g_inc = -1;
  else if (G < 1) g_inc = +1;
  
  if (B > 254)    b_inc = -1;
  else if (B < 1) b_inc = +1;

}

void muteAmp() {
  digitalWrite(MUTE_PIN, LOW);
}

void unmuteAmp() {
  digitalWrite(MUTE_PIN, HIGH);  
}

/*
 * Main amp loop
 */
void loop() {
  readButtons();
  setStates(); 
  
  if (btnstate_MUTE) {
    setRGBColour(colourWHT, LED_MUTE_PIN);
    //unmuteAmp();
  } else {
    animateMUTE_LED();
    //muteAmp();
  }
  
  MIDI.read();
}

