#include "pins.h"
#include <TimerOne.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
//#include <IRremote.h>
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include <EEPROM.h>

#define MODE_OFF 0
#define MODE_MULTI 1
#define MODE_PIG 2
#define MODE_TORCH 3
#define MODE_PULSE 4
#define MODE_COUNT 5
#define MODE_TEST 255

#define RF_ADDR_BADGE 0xBABABABABALL
#define RF_ADDR_CONTROL 0xF0F0F0F0F0LL
#define RF_SIZE 32
RF24 radio(A0,A1);

#define PIN_STATUS_LED 13
#define LIGHT_PWM_MAX 128
//IRrecv irrecv(PIN_IR_IN);
//decode_results results;

volatile int mode;
boolean radioup = false;
volatile unsigned long powerdown;
volatile unsigned long position;
volatile int brightness;
unsigned long last_ir;
boolean pwmRunning;

void button_press() {
  digitalWrite(PIN_LED_LEFT, LOW);
  digitalWrite(PIN_LED_RIGHT, LOW);

  powerdown = 0;
  int sweep=0;
  while((digitalRead(PIN_BUTTON) == LOW))
  {
    TXLED0;
    RXLED0;
    digitalWrite(PIN_STATUS_LED, LOW);
    switch((sweep++/20)%3)
    {
      case 0: TXLED1; break;
      case 1: RXLED1; break;
      case 2: digitalWrite(PIN_STATUS_LED, HIGH); break;
    }
    delayMicroseconds(500000);
  }
    
  TXLED0;
  RXLED0;
  digitalWrite(PIN_STATUS_LED, LOW);

  mode = (mode+1)%MODE_COUNT;
  position = 0;
  EEPROM.write(0, mode);
  Serial.print("MODE ");
  Serial.println(mode);
  
  if (radioup)
  {
    byte data[RF_SIZE];
    memset(data, 0, sizeof(data));
    data[0] = 'M';
    data[1] = '0' + mode;
    radio.stopListening();
    radio.write(data, RF_SIZE);
    radio.startListening();
  }
}

byte leftLED[3] = {0};
byte rightLED[3] = {0};

inline void setLEDs(boolean red, boolean green, boolean blue)
{
  digitalWrite(PIN_LED_RED, !red);
  digitalWrite(PIN_LED_GREEN, !green);
  digitalWrite(PIN_LED_BLUE, !blue);
}

void pulseLEDs()
{
  static byte count=0;
  static byte left=0;
  if (left++%2)
  {
    count = (count + 1)%LIGHT_PWM_MAX;
    
    digitalWrite(PIN_LED_RIGHT, LOW);
    setLEDs(leftLED[0] > count, leftLED[1] > count, leftLED[2] > count);
    digitalWrite(PIN_LED_LEFT, HIGH);
  }
  else
  {
    digitalWrite(PIN_LED_LEFT, LOW);
    setLEDs(rightLED[0] > count, rightLED[1] > count, rightLED[2] > count);
    digitalWrite(PIN_LED_RIGHT, HIGH);
  }
}


void SetColour(int pin, byte red, byte green, byte blue)
{
  if (pin == PIN_LED_LEFT || pin == PIN_LED_BOTH)
  {
    leftLED[0] = red;
    leftLED[1] = green;
    leftLED[2] = blue;
  }
  if (pin == PIN_LED_RIGHT || pin == PIN_LED_BOTH)
  {
    rightLED[0] = red;
    rightLED[1] = green;
    rightLED[2] = blue;
  }
  boolean pwm = false;
  // if any LED is partly lit we have to do PWM
  if ((leftLED[0] && leftLED[0] != 255) || (leftLED[1] && leftLED[1] != 255) || (leftLED[2] && leftLED[2] != 255))
    pwm = true;
  else if ((rightLED[0] && rightLED[0] != 255) || (rightLED[1] && rightLED[1] != 255) || (rightLED[2] && rightLED[2] != 255))
    pwm = true;
  // ok we might be able to skip the pwm
  if (!pwm)
  {
    // both LEDs set the same
    if (leftLED[0] == rightLED[0] && leftLED[1] == rightLED[1] && leftLED[2] == rightLED[2])
    {
      Timer1.stop();
      pwmRunning = false;
      setLEDs(leftLED[0],leftLED[1],leftLED[2]);
      digitalWrite(PIN_LED_LEFT, HIGH);
      digitalWrite(PIN_LED_RIGHT, HIGH);
      return;
    }
    // left LED is off, so set right LED and return
    // note: also catches both LEDs off
    if (!leftLED[0] && !leftLED[1] && !leftLED[2])
    {
      Timer1.stop();
      pwmRunning = false;
      digitalWrite(PIN_LED_LEFT, LOW);
      setLEDs(rightLED[0],rightLED[1],rightLED[2]);
      digitalWrite(PIN_LED_RIGHT, HIGH);
      return;
    }
    // right LED is off, so set the left LED and return
    if (!rightLED[0] && !rightLED[1] && !rightLED[2])
    {
      Timer1.stop();
      pwmRunning = false;
      digitalWrite(PIN_LED_RIGHT, LOW);
      setLEDs(leftLED[0],leftLED[1],leftLED[2]);
      digitalWrite(PIN_LED_LEFT, HIGH);
      return;
    }
  }
  // fall through to PWM
  if (!pwmRunning)
  {
    pwmRunning = true;
    Timer1.initialize(72);
    Timer1.attachInterrupt( pulseLEDs );
  }
  return;
}

unsigned long radiotime = 0;
byte radio_channel = 0;
void check_rf()
{
  if (!radioup && millis() - radiotime < 5000)
  {
    // if radio is down and it has been less than 5 seconds
    // since last activity, do nothing
    return;
  }
  RXLED1;
  // active radio, or idle radio that has been powered down
  // for > 5 seconds
  if (!radioup)
  {
    // if radio is down, bring it up, also reset activity time
    radiotime = millis() + 100; // listen for 100ms
    radio.begin();
    radio.powerUp();
    radio.setRetries(2,2);
    radio.setPayloadSize(RF_SIZE);
    radio.setAutoAck(true);
    radio.setChannel(radio_channel);
    radio.openReadingPipe(1,RF_ADDR_BADGE);
    radio.openWritingPipe(RF_ADDR_CONTROL);
    radio.startListening();
    radioup = true;
    Serial.print("Radio Up ");
    Serial.println(radio_channel);
  }
  // check for new data
  if (radio.available())
  {
    byte data[RF_SIZE];
    memset(data, 0, RF_SIZE);
    radio.read( &data, RF_SIZE);
    if (data[0])
    {
      Serial.print("Radio Data ");
      Serial.println((char)data[0]);
      radiotime = millis() + 1000;
      switch(data[0])
      {
        case 'P':
          memset(data, 0, RF_SIZE);
          data[0] = 'P';
          memcpy(&data[1], leftLED, 3);
          memcpy(&data[4], rightLED, 3);
          radio.stopListening();
          radio.write(data, RF_SIZE);
          radio.startListening();
        break;
        case 'L':
          mode = MODE_TEST;
          SetColour(PIN_LED_LEFT, data[1], data[2], data[3]);
          SetColour(PIN_LED_RIGHT, data[4], data[5], data[6]);
          radio.stopListening();
          radio.write(data, RF_SIZE);
          radio.startListening();
        break;
        case 'B':
          brightness=data[1];
          radio.stopListening();
          radio.write(data, RF_SIZE);
          radio.startListening();
        break;
        case 'M':
          mode = data[1] - 1;
          button_press();
        break;
        case 'N':
          button_press();
        break;
        case 'K':
          while(true)
            sleep_mode();
        case 'R':
          radio_channel = data[1];
          radioup = false;
        break;
      }
    }
  }  
  if (millis() > radiotime)
  {
    // if radio is up, but has been idle for too long
    // shut it down and reset to the lobby channel
    RXLED0;
    radio.stopListening();
    radio.powerDown();
    radioup = false;
    radio_channel = 0;
    Serial.println("Radio Down");
    return;
  }
}

void setup() {
  wdt_enable(WDTO_2S);
  Mouse.begin();
  Serial.begin(9600);
  Serial.println("INIT");
  position = 0;
  powerdown = 0;
  pwmRunning = false;
  brightness = -1;
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_LEFT, OUTPUT);
  pinMode(PIN_LED_RIGHT, OUTPUT);

  mode =   EEPROM.read(0);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  attachInterrupt(INT_BUTTON, button_press, FALLING);
//  irrecv.enableIRIn(); // Start the receiver
  last_ir=millis();
  
}

void power_save_sleep()
{
  if (!powerdown)
    powerdown = millis();
  if (millis() - powerdown < 20000)
  {
    if (millis()%1000 < 20)
      TXLED1;
    else
      TXLED0;
  }
  else if (millis() - powerdown < 40000)
  {
    if (millis()%2000 < 20)
      RXLED1;
    else
      RXLED0;
  }
  else if (millis() - powerdown < 60000)
  {
    if (millis()%4000 < 20)
      digitalWrite(PIN_STATUS_LED, HIGH);
    else
      digitalWrite(PIN_STATUS_LED, LOW);
  }
  else
  {
    // shutdown
    wdt_disable();
    USBDevice.detach();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    radio.stopListening();
    radio.powerDown();
    TXLED1; RXLED1; digitalWrite(PIN_STATUS_LED, HIGH);
    delay(100);
    TXLED0; RXLED0; digitalWrite(PIN_STATUS_LED, LOW);
    if (powerdown)
      sleep_mode();
    radio.powerUp();
    // DEVICE IS POWERED DOWN HERE UNTIL INTERRUPT
    void(* resetFunc) (void) = 0;
    resetFunc();
  }
  delay(1);
  TXLED0; RXLED0; digitalWrite(PIN_STATUS_LED, LOW);
}

boolean check_mouse();

void check_ir()
{
/*
  if (irrecv.decode(&results))
  {
    irrecv.resume();
    if (!last_ir)
    {
      if(!check_mouse())
      {
        button_press();
      }
    }
    last_ir = millis();
    powerdown = 0;
  }
  */
}
boolean check_mouse()
{
  /*
  if (results.decode_type == RC5)
  {
    if (results.value == 0x15 || results.value == 0x815)
    {
      Mouse.move(-20, 0, 0);
      return true;
    }
    if (results.value == 0x16 || results.value == 0x816)
    {
      Mouse.move(20, 0, 0);
      return true;
    }
    if (results.value == 0x20 || results.value == 0x820)
    {
      Mouse.move(0, -20, 0);
      return true;
    }
    if (results.value == 0x21 || results.value == 0x821)
    {
      Mouse.move(0, 20, 0);
      return true;
    }
  }
  return false;
  */
}

void loop()
{
  wdt_reset();
  check_rf();
  check_ir();
  if (millis() - last_ir <150)
  {
    TXLED1; RXLED0; digitalWrite(PIN_STATUS_LED, LOW);
  }
  else if (millis() - last_ir < 300)
  {
    TXLED0; RXLED1; digitalWrite(PIN_STATUS_LED, LOW);
  }
  else if (millis() - last_ir < 450)
  {
    TXLED0; RXLED0; digitalWrite(PIN_STATUS_LED, HIGH);
  }
  else if (last_ir)
  {
    TXLED0; RXLED0; digitalWrite(PIN_STATUS_LED, LOW);
    last_ir = 0;
  }
  
  if (Serial.available())
  {
    switch(Serial.read())
    {
      case 'P':
        delay(100);
      break;
      case 'M':
        mode = Serial.parseInt() - 1;
        button_press();
      break;
      case 'N':
        button_press();
      break;
      case 'T':
        Serial.println("MODE TEST");
        mode = MODE_TEST;
      break;
      case 'L': if (mode == MODE_TEST)
      {
        int r=Serial.parseInt();
        int g=Serial.parseInt();
        int b=Serial.parseInt();
        Serial.print("Left Brightness set to - R:");
        Serial.print(r);
        Serial.print(" G:");
        Serial.print(g);
        Serial.print(" B:");
        Serial.println(b);
        SetColour(PIN_LED_LEFT, r, g, b);
      }
      break;
      case 'R': if (mode == MODE_TEST)
      {
        int r=Serial.parseInt();
        int g=Serial.parseInt();
        int b=Serial.parseInt();
        Serial.print("Right Brightness set to - R:");
        Serial.print(r);
        Serial.print(" G:");
        Serial.print(g);
        Serial.print(" B:");
        Serial.println(b);
        SetColour(PIN_LED_RIGHT, r, g, b);
      }
      break;
      case 'B':
        brightness=Serial.parseInt();
        Serial.print("Brightness set to: ");
        Serial.println(brightness);
      break;
    }
  }

  int howBright = brightness;
  switch(mode)
  {
    case MODE_OFF:
    {
        SetColour(PIN_LED_BOTH, 0, 0, 0);
    }
    power_save_sleep(); break;
    case MODE_TORCH:
    if (howBright == -1) howBright = 255;
    {
      SetColour(PIN_LED_BOTH, howBright, howBright, howBright);
    }
    break;
    
    case MODE_MULTI:
    if (howBright == -1) howBright = 1;
    {
      static byte r=1,g=1,b=1;
      static unsigned long last = millis();
      if (millis() - last > 500)
      {
        r++;
        last = millis();
      }
      if (r>1){r=0;g++;}
      if (g>1){g=0;b++;}
      if (b>1){b=0;}
      SetColour(PIN_LED_LEFT, r*howBright, g*howBright, b*howBright);
      SetColour(PIN_LED_RIGHT, g*howBright, b*howBright, r*howBright);
    }
    delay(5); break;
    
    case MODE_PIG:
    if (howBright == -1) howBright = 255;
    switch(position++%7)
    {
      case 0: SetColour(PIN_LED_LEFT, howBright, 0, 0); SetColour(PIN_LED_RIGHT, 0, 0, 0); break;
      case 1: SetColour(PIN_LED_LEFT, 0, 0, 0); SetColour(PIN_LED_RIGHT, howBright, 0, 0); break;
      case 2: SetColour(PIN_LED_LEFT, 0, 0, howBright); SetColour(PIN_LED_RIGHT, 0, 0, 0); break;
      case 3: SetColour(PIN_LED_LEFT, 0, 0, 0); SetColour(PIN_LED_RIGHT, 0, 0, howBright); break;
      case 4: SetColour(PIN_LED_BOTH, 0, 0, 0); break;
      case 5: SetColour(PIN_LED_BOTH, howBright, howBright, howBright); break;
      case 6: SetColour(PIN_LED_BOTH, 0, 0, 0); break;
    }
    delay(100); break;
    
    case MODE_PULSE:
    if (howBright == -1) howBright = 255;
    switch (position++/LIGHT_PWM_MAX%14)
    {
      case 0:
        SetColour(PIN_LED_LEFT, position%LIGHT_PWM_MAX, 0, 0);
        SetColour(PIN_LED_RIGHT, 1,0,0);
      break;
      case 1:
        SetColour(PIN_LED_BOTH, 255, 0, 0);
      break;
      case 2:
        SetColour(PIN_LED_LEFT, 0, position%LIGHT_PWM_MAX, 0);
        SetColour(PIN_LED_RIGHT, 0,1,0);
      break;
      case 3:
        SetColour(PIN_LED_BOTH, 0, 255, 0);
      break;
      case 4:
        SetColour(PIN_LED_LEFT, 0, 0, position%LIGHT_PWM_MAX);
        SetColour(PIN_LED_RIGHT, 0,0,1);
      break;
      case 5:
        SetColour(PIN_LED_BOTH, 0, 0, 255);
      break;
      case 6:
        SetColour(PIN_LED_LEFT, position%LIGHT_PWM_MAX, position%LIGHT_PWM_MAX, 0);
        SetColour(PIN_LED_RIGHT, 1,1,0);
      break;
      case 7:
        SetColour(PIN_LED_BOTH, 255, 255, 0);
      break;
      case 8:
        SetColour(PIN_LED_LEFT, 0, position%LIGHT_PWM_MAX, position%LIGHT_PWM_MAX);
        SetColour(PIN_LED_RIGHT, 0,1,1);
      break;
      case 9:
        SetColour(PIN_LED_BOTH, 0, 255, 255);
      break;
      case 10:
        SetColour(PIN_LED_LEFT, position%LIGHT_PWM_MAX, 0, position%LIGHT_PWM_MAX);
        SetColour(PIN_LED_RIGHT, 1,0,1);
      break;
      case 11:
        SetColour(PIN_LED_BOTH, 255, 0, 255);
      break;
      case 12:
        SetColour(PIN_LED_LEFT, position%LIGHT_PWM_MAX, position%LIGHT_PWM_MAX, position%LIGHT_PWM_MAX);
        SetColour(PIN_LED_RIGHT, 0, 0, 0);
      break;
      case 13:
        SetColour(PIN_LED_BOTH, 255, 255, 255);
      break;
    }
    delay(1000/LIGHT_PWM_MAX); break;
  }
}

