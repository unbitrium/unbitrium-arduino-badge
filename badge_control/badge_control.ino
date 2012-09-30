#include <avr/wdt.h>
#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"

#define RF_ADDR_BADGE 0xBABABABABALL
#define RF_ADDR_CONTROL 0xF0F0F0F0F0LL
#define RF_SIZE 32
#define RF_CHANNELS 120
// CE, CS
RF24 radio(A0,A1);

byte find_channel()
{
  radio.begin();
  radio.setAutoAck(false);
  radio.startListening();
  radio.stopListening();
  int values[RF_CHANNELS];
  int totalSignal = 0;
  memset(values,0,sizeof(values));

  int rep_counter = 100;
  while (rep_counter--)
  {
    int i = RF_CHANNELS;
    while (i--)
    {
      // Select this channel
      radio.setChannel(i);

      // Listen for a little
      radio.startListening();
      delayMicroseconds(128);
      radio.stopListening();

      // Did we get a carrier?
      if (radio.testCarrier())
      {
        values[(i-4)%RF_CHANNELS] += 1;
        values[(i-3)%RF_CHANNELS] += 2;
        values[(i-2)%RF_CHANNELS] += 3;
        values[(i-1)%RF_CHANNELS] += 4;
        values[i] += 5;
        values[(i+1)%RF_CHANNELS] += 4;
        values[(i+2)%RF_CHANNELS] += 3;
        values[(i+3)%RF_CHANNELS] += 2;
        values[(i+4)%RF_CHANNELS] += 1;
        totalSignal++;
      }
    }
  }
  int quietest = 255;
  int loudest = 0;
  int i = RF_CHANNELS;
  while (i--)
  {
    if (values[i] < quietest)
      quietest = values[i];
    if (values[i] > loudest)
      loudest = values[i];
  }
  byte channel;
  do
  {
    channel = random()%RF_CHANNELS;
  } while (values[channel] > quietest);

    Serial.println(":");
    for (int x=0; x<RF_CHANNELS; x++)
    {
      values[x] = values[x]/10;
      if (values[x] > 15)
        values[x] = 15;
      Serial.print(values[x], HEX);
    }

  Serial.print("[");
  Serial.print(values[channel]);
  Serial.print("/");
  Serial.print(quietest);
  Serial.print("-");
  Serial.print(loudest);
  Serial.print("] noise=");
  Serial.print((float)totalSignal/RF_CHANNELS);
  Serial.print(" Picked ");
//  Serial.print(channel);
  return channel;
}

void bring_up_radio(byte channel)
{
  radio.begin();
  radio.setRetries(15,15);
  radio.setPayloadSize(RF_SIZE);
  radio.setAutoAck(true);
  radio.setChannel(channel);
  radio.openReadingPipe(1,RF_ADDR_CONTROL);
  radio.openWritingPipe(RF_ADDR_BADGE);
}

void setup(void)
{
  randomSeed(analogRead(5) + analogRead(4) + analogRead(3));
  Serial.begin(115200);

  Serial.print("Selecting channel ");
  byte target_channel = find_channel();
  Serial.println(target_channel);
  for (int x=0; x<target_channel; x++)
    Serial.print(" ");
  Serial.println("|");
  for (int x=0; x<target_channel; x++)
    Serial.print(" ");
  Serial.println(target_channel);

  byte data[RF_SIZE] = {0};
  data[0] = 'R';
  data[1] = target_channel;
  Serial.println("Finding Device...");

  bring_up_radio(0);

  wdt_enable(WDTO_8S);
  bool connected = false;
  while (!radio.write(data, RF_SIZE));

  Serial.print("Connecting");
  wdt_enable(WDTO_2S);
  bring_up_radio(target_channel);

  memset(data, 0, sizeof(data));
  data[0] = '0';
  while (!radio.write(data, RF_SIZE))
  {
      Serial.print(".");
      delay(10);
  }
  Serial.println(" Done");
  wdt_reset();
  radio.startListening();
}

void loop()
{
  if (Serial.available())
  {
    char data[RF_SIZE];
    switch(Serial.read())
    {
      case 'L':
        data[0] = 'L';
        data[1] = Serial.parseInt();
        data[2] = Serial.parseInt();
        data[3] = Serial.parseInt();
        data[4] = Serial.parseInt();
        data[5] = Serial.parseInt();
        data[6] = Serial.parseInt();
        radio.stopListening();
        radio.write(data, RF_SIZE);
        radio.startListening();
      break;
      case 'B':
        data[0] = 'B';
        data[1] = Serial.parseInt();
        radio.stopListening();
        radio.write(data, RF_SIZE);
        radio.startListening();
      break;
      case 'M':
        data[0] = 'M';
        data[1] = Serial.parseInt();
        radio.stopListening();
        radio.write(data, RF_SIZE);
        radio.startListening();
      break;
      case 'N':
        data[0] = 'N';
        radio.stopListening();
        radio.write(data, RF_SIZE);
        radio.startListening();
      break;
      case 'R':
        memset(data, 0, sizeof(data));
        data[0] = 'R';
        data[1] = Serial.parseInt();
        radio.stopListening();
        radio.write(data, RF_SIZE);
        
        bring_up_radio(data[1]);
        Serial.print("Retune to ");
        Serial.print((int)data[1]);
        data[0] = '0';
        while (!radio.write(data, RF_SIZE))
        {
            Serial.print(".");
            delay(10);
        }
        Serial.println(" Done");
        radio.startListening();
      break;
      case 'K':
        data[0] = 'K';
        radio.stopListening();
        radio.write(data, RF_SIZE);
        radio.startListening();
      break;
    }
  }
  static unsigned long pingTime = 0;
  if (millis() - pingTime > 500)
  {
    byte data[RF_SIZE];
    memset(data, 0, RF_SIZE);
    data[0] = 'P';
    radio.stopListening();
    pingTime = millis();
    if(radio.write(data, RF_SIZE))
    {
      wdt_reset();
    }
    radio.startListening();
  }
  if (radio.available())
  {
    byte data[RF_SIZE];
    memset(data, 0, RF_SIZE);
    if (radio.read( &data, RF_SIZE))
    {
      switch(data[0])
      {
        case 'P':
          Serial.print("Ping ");
          Serial.print(millis() - pingTime);
          Serial.print("ms");
          
          Serial.print(" Left {R:");
          Serial.print(data[1]);
          Serial.print(" G:");
          Serial.print(data[2]);
          Serial.print(" B:");
          Serial.print(data[3]);
          
          Serial.print("} Right {R:");
          Serial.print(data[4]);
          Serial.print(" G:");
          Serial.print(data[5]);
          Serial.print(" B:");
          Serial.print(data[6]);
          Serial.println("}");
        break;
        
        default:
          Serial.print("RCV:");
          Serial.println((char*)data);
      }
    }
  }  
}

