
#include <OneWire.h>
#include <Wire.h>
#include "Teensy41TCP_IP.hpp"
#include "DS18B20.hpp"
#include "ADXL345_Accelerometer.hpp"
#include "procElEnEvent.h"
#include "procAzEnEvent.h"
#include "WatchDog.hpp"
#include <NativeEthernet.h>
#include <queue>
#include <NativeEthernet.h>

#define TempEl1Pin 0
#define TempEl2Pin 1
#define TempAz1Pin 2
#define TempAz2Pin 3
#define AdxlElIntPin 7
#define AdxlAzIntPin 8
#define AdxlCbIntPin 9
#define AdxlElPowerPin 32
#define AdxlAzPowerPin 4
#define AdxlCbPowerPin 5

#define TCPPORT 1600
#define CRPORT 1680   //Control Room Port
// Create an IntervalTimer object 
IntervalTimer myTimer;
IntervalTimer myTimeout;

WatchDog wdog1;

TemperatureSensor tempSensorEl1(TempEl1Pin);
TemperatureSensor tempSensorEl2(TempEl2Pin);
TemperatureSensor tempSensorAz1(TempAz1Pin);
TemperatureSensor tempSensorAz2(TempAz2Pin);
ADXL345 adxlEl = ADXL345(Wire);
ADXL345 adxlAz = ADXL345(Wire1);
ADXL345 adxlCb = ADXL345(Wire2);
ElevationEncoder elencoder = ElevationEncoder();
AzimuthEncoder azencoder = AzimuthEncoder();

//ethernet data
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
byte ip[] = { 192, 168, 0, 197 }; // The IP will need reset for every different PC the Teensy is connected to
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);



IPAddress ControlRoomIP = IPAddress(192, 168, 0, 10);


EthernetClient client;
//ethernet server
EthernetServer server(CRPORT);

std::queue <int16_t> emptyBuff;
std::queue <acc> emptyAccBuff;

int const ADXL345_TimeOut_1S = 1000000; 
int const TIMER_1MS = 1000;

float Celsius = 0;
float Fahrenheit = 0;

// Event flags that are set by the Control Room used to Configure and Initialize
bool InitEl1TempFlag;
bool InitEl2TempFlag;
bool InitAz1TempFlag;
bool InitAz2TempFlag;
bool InitElEncoderFlag;
bool InitAzEncoderFlag;
bool InitElAccelFlag;
bool InitAzAccelFlag;
bool InitCbAccelFlag;

// Error flags that are used to tell which temp sensor to use
bool EL1Errored = false;
bool EL2Errored = false;
bool Az1Errored = false;
bool Az2Errored = false;

// Event flags that are check in the main loop to see what processes should be run
bool ElAccelEventFlag = false;
bool AzAccelEventFlag = false;
bool CbAccelEventFlag = false;

// counters for each clock driven interrupt
int ethernetcounter = 0;
int eltempcounter = 0;
int aztempcounter = 0;    
int encodercounter =0;

// Timer interrupt
void TimerEvent_ISR(){
  //increment each clock event counter by 1
  if(InitEl1TempFlag || InitEl2TempFlag){
    eltempcounter++;
  }
  if(InitAz1TempFlag || InitAz2TempFlag){
    aztempcounter++;
  }
  //Serial.println("TimerISR hit");
  if(InitElEncoderFlag || InitAzEncoderFlag){

    encodercounter++;
  }

  ethernetcounter++;
  
}
// Time out interrupt
void TimeOutEvent_ISR(){
  Serial.println("Time out hit");
}

/********************* ISR *********************/
/* Look for ADXL Interrupts     */
void ADXLEL_ISR() {
  ElAccelEventFlag = true;
  
}
void ADXLAZ_ISR() {
  AzAccelEventFlag = true;

}
void ADXLCB_ISR() {
  CbAccelEventFlag = true;

}

void setup() {

  Ethernet.begin(mac, ip, gateway, gateway, subnet);
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
  }
  else{
    Serial.println("Hardware found");
  }

  // Keep trying to connect to the control room until a connection is successfully made
  while(!client.connected()) {
    // Connect to the control room TCP Client
    if(client.connect(ControlRoomIP, TCPPORT)){
        Serial.println("Connected to the control room's TCP server.");
        client.write("Send Sensor Configuration");
    }
    else{
        Serial.println("Could not connect to the control room.");
    }
  }

  // start listening for clients
  server.begin();
  Serial.print("ethernet server address:");
  Serial.println(Ethernet.localIP());
  Serial.print("ethernet server port:");
  Serial.println(CRPORT);
  
  // wait for a Control Room client:
  Serial.println("Waiting for client");
  EthernetClient controlRoomClient = server.available();
  
  //Wait till we recieve data from the Control Room
  while(!controlRoomClient){
    delay(1);
    controlRoomClient = server.available();
  }
  Serial.println("Client found");

  size_t bytes = controlRoomClient.available();
  uint8_t *ptr;
  uint8_t data[bytes] = {0};
  ptr = data;
  controlRoomClient.read(ptr, bytes);

  InitEl1TempFlag = data[0] == 0 ? false : true;
  InitEl2TempFlag = data[1] == 0 ? false : true;
  InitAz1TempFlag = data[2] == 0 ? false : true;
  InitAz2TempFlag = data[3] == 0 ? false : true;
  InitElEncoderFlag = data[4] == 0 ? false : true;
  InitAzEncoderFlag = data[5] == 0 ? false : true;
  InitAzAccelFlag = data[6] == 0 ? false : true;
  InitElAccelFlag = data[7] == 0 ? false : true;
  InitCbAccelFlag = data[8] == 0 ? false : true;

  for (uint8_t j = 0; j < bytes; j++)
  {
    Serial.print(data[j]);
    Serial.print("  ");
  }
  Serial.println();

  controlRoomClient.write("acknoledge");
  // give time to receive the data
  delay(2);
  // close the connection:
  controlRoomClient.stop();

  pinMode(AdxlElPowerPin, OUTPUT);
  if(InitElAccelFlag){
    digitalWrite(AdxlElPowerPin, HIGH);
    delay(10);
    adxlEl.init();                               // initialize an ADXL345 to communicate using I2C
    attachInterrupt(digitalPinToInterrupt(AdxlElIntPin), ADXLEL_ISR, RISING);   // Attach ADXL345 Interrupt
    ElAccelEventFlag = true;         // init as true to empty 
    Serial.println("El Adxl Initialized");
  }
  else{
    digitalWrite(AdxlElPowerPin, LOW);
  }

  pinMode(AdxlAzPowerPin, OUTPUT);
  if(InitAzAccelFlag){
    digitalWrite(AdxlAzPowerPin, HIGH);
    delay(10);
    adxlAz.init();                               // initialize an ADXL345 to communicate using I2C
    attachInterrupt(digitalPinToInterrupt(AdxlAzIntPin), ADXLAZ_ISR, RISING);   // Attach ADXL345 Interrupt
    AzAccelEventFlag = true;         // init as true to empty 
    Serial.println("Az Adxl Initialized");
  }
  else{
    digitalWrite(AdxlAzPowerPin, LOW);
  }

  pinMode(AdxlCbPowerPin, OUTPUT);
  if(InitCbAccelFlag){
    digitalWrite(AdxlCbPowerPin, HIGH);
    delay(10);
    adxlCb.init();                               // initialize an ADXL345 to communicate using I2C
    attachInterrupt(digitalPinToInterrupt(AdxlCbIntPin), ADXLCB_ISR, RISING);   // Attach ADXL345 Interrupt
    CbAccelEventFlag = true;         // init as true to empty 
    Serial.println("Cb Adxl Initialized");
  }
  else{
    digitalWrite(AdxlCbPowerPin, LOW);
  }

  if(InitAzEncoderFlag){
    azencoder.init();                          // initialize azimuth encoder to communicate using SPI
    Serial.println("Az Encoder Initialized");
  }

  myTimer.begin(TimerEvent_ISR, TIMER_1MS);  // TimerEvent to run every millisecond
  
  Serial.printf("POR reset ... init for ~1,5s watchdog timeout\n");
  wdog1.init();   // ~1.5s watchdog timeout
}

// This is the super loop where we will be keeping track of counters, setting eventflags and calling proccess base on if any event flags were set
void loop() {
  
  //check if temp sensors are ready to be read. Read every 1s
  if(eltempcounter >= 1000){
    eltempcounter = 0;
    
    if(InitEl1TempFlag && !EL1Errored){

      // gets the Elvation motor temperature and checks if sensor is good
      if(!tempSensorEl1.getTemp()){
        EL1Errored = true;
        Serial.println("El Temp Senor 1 Stoped Working");
      }

    }

    // if first temp sensor fails then use the second temp sesnor
    if(InitEl2TempFlag && EL1Errored && !EL2Errored){

      if(!tempSensorEl2.getTemp()){ 
        EL2Errored = true;
        Serial.println("El Temp Senor 2 Stoped Working");
      }        
    }
    
  }
  if(aztempcounter >= 1500){
    aztempcounter = 500;    //add offset so temps are not samples at the same time

    if(!Az1Errored && InitAz1TempFlag){
       // gets the Azmuth motor temperature
      if(!tempSensorAz1.getTemp()){
        Az1Errored = true;
        Serial.println("Az Temp Senor 1 Stoped Working");
      }       
    }
    if(Az1Errored && InitAz2TempFlag && !Az2Errored){
      if(!tempSensorAz2.getTemp()){
        Az2Errored = true;
        Serial.println("Az Temp Senor 2 Stoped Working");
      }           
    }

  }
  //check if elevation encoder is ready to be read. Read every 20ms
  if(encodercounter >= 20){//TODO: switch to constant
    encodercounter = 0;
    
    if(InitElEncoderFlag){
      elencoder.procElEnEvent();
    }
    if(InitAzEncoderFlag){
      azencoder.procAzEnEvent();         
    }
    //Send only the encoder information to the Control Room
    uint32_t dataSize = calcTransitSize(0, 0, 0, 0, 0, elencoder.buffer.size(), azencoder.buffer.size()); // determine the size of the array that needs to be alocated
    uint8_t *dataToSend;
    dataToSend = (uint8_t *)malloc(dataSize * sizeof(uint8_t)); //malloc needs to be used becaus stack size on the loop task is about 4k so this needs to go on the heap
    
    prepairTransit(dataToSend, dataSize, &emptyAccBuff, &emptyAccBuff, &emptyAccBuff, &emptyBuff, &emptyBuff, &elencoder.buffer, &azencoder.buffer);

    SendDataToControlRoom(dataToSend, dataSize, ControlRoomIP, TCPPORT, client);
    
    free(dataToSend);
  }

  if(ElAccelEventFlag){
    
    ElAccelEventFlag = false;
    myTimeout.begin(TimeOutEvent_ISR, ADXL345_TimeOut_1S);
    adxlEl.emptyFifo();      // gets the x y and z cordnates and prints them to the serial port.
    myTimeout.end();
  }
  
  if(AzAccelEventFlag){
    
    AzAccelEventFlag = false;
    adxlAz.emptyFifo();      // gets the x y and z cordnates and prints them to the serial port.
  }

  if(CbAccelEventFlag){
    
    CbAccelEventFlag = false;
    adxlCb.emptyFifo();      // gets the x y and z cordnates and prints them to the serial port.
  }

  // Send all sensor data except encoder
  if(ethernetcounter >= 250){
    ethernetcounter = 0;

    uint32_t dataSize = calcTransitSize(adxlEl.buffer.size(), adxlAz.buffer.size(), adxlCb.buffer.size(), tempSensorEl1.buffer.size(), tempSensorAz1.buffer.size(),0,0); // determine the size of the array that needs to be alocated
    uint8_t *dataToSend;
    dataToSend = (uint8_t *)malloc(dataSize * sizeof(uint8_t)); //malloc needs to be used becaus stack size on the loop task is about 4k so this needs to go on the heap
    
    prepairTransit(dataToSend, dataSize, &adxlEl.buffer, &adxlAz.buffer, &adxlCb.buffer, &tempSensorEl1.buffer, &tempSensorAz1.buffer, &emptyBuff, &emptyBuff);

    if(client.connected()){
      SendDataToControlRoom(dataToSend, dataSize, ControlRoomIP, TCPPORT, client);
    }
    else{
      delay(1550);
      //SCB_AIRCR = 0x05FA0004;  // does a software reset
    }

    free(dataToSend);

    wdog1.feed(); //reset watchdog
    // We need this but I'm not sure why it's hanging here :(
    //client.flush();
  }

}

