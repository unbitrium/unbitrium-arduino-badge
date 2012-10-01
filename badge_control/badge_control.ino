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
  radio.setRetries(1,15);
  radio.setPayloadSize(RF_SIZE);
  radio.setAutoAck(true);
  radio.enableAckPayload();
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(channel);
  radio.openReadingPipe(1,RF_ADDR_CONTROL);
  radio.openWritingPipe(RF_ADDR_BADGE);
}
byte target_channel;
void setup(void)
{
  randomSeed(analogRead(5) + analogRead(4) + analogRead(3));
  Serial.begin(115200);

  Serial.print("Selecting channel ");
  target_channel = find_channel();
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
}

void loop()
{
  static unsigned long recvTime = millis();
  if (Serial.available())
  {
    char data[RF_SIZE];
    unsigned long cmdtime = millis();
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
        while (!radio.write(data, RF_SIZE));
      break;
      case 'B':
        data[0] = 'B';
        data[1] = Serial.parseInt();
        while (!radio.write(data, RF_SIZE));
      break;
      case 'M':
        data[0] = 'M';
        data[1] = Serial.parseInt();
        while (!radio.write(data, RF_SIZE));
      break;
      case 'N':
        data[0] = 'N';
        while (!radio.write(data, RF_SIZE));
      break;
      case 'R':
        memset(data, 0, sizeof(data));
        data[0] = 'R';
        data[1] = Serial.parseInt();
        Serial.print("Retune to ");
        Serial.print((int)data[1]);
        wdt_enable(WDTO_8S);
        while (!radio.write(data, RF_SIZE))
        {
          Serial.print("+");
          if (millis() - cmdtime > 100)
            bring_up_radio(0);
        }
        wdt_enable(WDTO_2S);
        bring_up_radio(data[1]);
        data[0] = '0';
        while (!radio.write(data, RF_SIZE))
        {
            Serial.print(".");
            delay(10);
        }
        Serial.println(" Done");
      break;
      case 'K':
        data[0] = 'K';
        radio.write(data, RF_SIZE);
      break;
    }
    recvTime = millis();
  }
  static unsigned long sendCounter = 0;
  if (radio.available() || radio.isAckPayloadAvailable())
  {
    byte data[RF_SIZE];
    memset(data, 0, RF_SIZE);
    if (radio.read( &data, RF_SIZE))
    {
      recvTime = millis();
      switch(data[0])
      {
        case 'P':
          static unsigned long throughputStartPacket = 0;
          static unsigned long throughputCount = 0;
          static unsigned long throughputTime = 0;
          static float rate = 0;
          throughputCount += RF_SIZE;
          if (millis() - throughputTime >= 1000)
          {
            rate = (float)throughputCount / (millis() - throughputTime);
            Serial.print("Ping ");
            Serial.print(recvTime - *(unsigned long*)&data[7]);
            Serial.print("ms");
          
            Serial.print(" {");
            Serial.print(data[1]);
            Serial.print(":");
            Serial.print(data[2]);
            Serial.print(":");
            Serial.print(data[3]);
          
            Serial.print("} {");
            Serial.print(data[4]);
            Serial.print(":");
            Serial.print(data[5]);
            Serial.print(":");
            Serial.print(data[6]);
            Serial.print("}");
            Serial.print(rate, 2);
            Serial.print("KB/sec (");
            Serial.print(throughputCount/RF_SIZE);
            Serial.print("/");
            Serial.print(sendCounter - throughputStartPacket);
            Serial.println(")");
            throughputStartPacket = sendCounter;
            throughputTime = millis();
            throughputCount = 0;
          }
          wdt_reset();
        break;
        
        default:
          Serial.print("RCV:");
          Serial.println((char*)data);
      }
    }
  }  
  if (recvTime && millis() - recvTime > 1000)
  {
    wdt_enable(WDTO_8S);
    target_channel = (target_channel + 3) % RF_CHANNELS;
    Serial.print("Ping Fail, Retune to ");
    Serial.print(target_channel);
    bring_up_radio(0);
    char data[RF_SIZE];
    data[0] = 'R';
    data[1] = target_channel;
    while (!radio.write(data, RF_SIZE))
    {
      Serial.print(".");
      delay(10);
    }
    Serial.print(" ACK ");
    bring_up_radio(target_channel);
    data[0] = '0';
    while (!radio.write(data, RF_SIZE))
    {
      Serial.print(".");
    }
    Serial.println("Done");
    wdt_enable(WDTO_2S);
    recvTime = millis();
  }
  static unsigned long pingTime = 0;
  byte data[RF_SIZE];
  memset(data, 0, RF_SIZE);
  data[0] = 'P';
  *(unsigned long*)(data+1) = millis();
  pingTime = millis();
  if (radio.write(data, RF_SIZE))
    sendCounter++;
}

