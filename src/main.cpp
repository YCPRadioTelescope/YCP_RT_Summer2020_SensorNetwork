
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
#define LED1 38
#define LED2 37
#define LED3 36
#define LED4 35

#define TCPPORT 1600  
#define CRPORT 1680   //Control Room Port
// Create an IntervalTimer object 
IntervalTimer myTimer;

WatchDog wdog1; //Watchdog timmer used for resets

// Sensor constructors 
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

// Time for timmer interrupt
int const TIMER_1MS = 1000;

float Celsius = 0;
float Fahrenheit = 0;

bool HeartBeatLED = false;      // Used to see if super loop is running as expected

// Event flags that are set by the Control Room used to configure and initialize sensors
bool InitElTempFlag;
bool InitAzTempFlag;
bool InitElEncoderFlag;
bool InitAzEncoderFlag;
bool InitElAccelFlag;
bool InitAzAccelFlag;
bool InitCbAccelFlag;

// Error flags that are used to tell which temp sensor to use
bool El1Errored = false;
bool El2Errored = false;
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
int encodercounter = 0;

// Time threshold for each clock driven interrupt
int aztempoffset = 500; // offset so temp sensors don't sample at the dame time
int ethernetthreshold = 250;
int eltempthreshold = 1000;
int aztempthreshold = 1000 + aztempoffset;
int encoderthreshold = 20;
// Timer interrupt
void TimerEvent_ISR(){
  //increment each clock event counter by 1
  if(InitElTempFlag){
    eltempcounter++;
  }
  if(InitAzTempFlag){
    aztempcounter++;
  }
  if(InitElEncoderFlag || InitAzEncoderFlag){

    encodercounter++;
  }

  ethernetcounter++;
  
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
  
  // Setup LEDs
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
  digitalWrite(LED3, LOW);  // Turn LED on to indicate power
  digitalWrite(LED4, HIGH);

  // Setup and start Teensy Ethernet
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
        // Request sensor configuration
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
  digitalWrite(LED4, LOW);  // Turn on LED4 to show connection to Control Room

  wdog1.init();   // ~1.5s watchdog timeout

  // Read the configuration packet
  size_t bytes = controlRoomClient.available();
  uint8_t *ptr;
  uint8_t data[bytes] = {0};
  ptr = data;
  controlRoomClient.read(ptr, bytes);

  InitElTempFlag = data[0] == 0 ? false : true;
  InitAzTempFlag = data[1] == 0 ? false : true;
  InitElEncoderFlag = data[2] == 0 ? false : true;
  InitAzEncoderFlag = data[3] == 0 ? false : true;
  InitAzAccelFlag = data[4] == 0 ? false : true;
  InitElAccelFlag = data[5] == 0 ? false : true;
  InitCbAccelFlag = data[6] == 0 ? false : true;

  // Send acknoledgement to Control Room
  controlRoomClient.write("acknoledge");
  // give time to receive the data
  delay(2);
  // close the connection:
  controlRoomClient.stop();

  // Setup ADXLs
  pinMode(AdxlElPowerPin, OUTPUT);
  if(InitElAccelFlag){
    digitalWrite(AdxlElPowerPin, HIGH);
    delay(10);
    adxlEl.init();                               // initialize an ADXL345 to communicate using I2C
    Serial.println();
    Serial.println("El Adxl Initialized");
    Serial.println("Starting El Adxl Self-Test");
    if(adxlEl.selfTest()){
        Serial.println("El ADXL Self-Test Passed"); 
    }
    else{
      Serial.println("El ADXL Self-Test Failed"); 
    }
    Serial.println();
    Serial.println("El Configuration-"); 
    adxlEl.printDataFormat();
    adxlEl.printBWRate();
    adxlEl.printFIFO_CTL();
    attachInterrupt(digitalPinToInterrupt(AdxlElIntPin), ADXLEL_ISR, RISING);   // Attach ADXL345 Interrupt
    ElAccelEventFlag = true;         // init as true to empty 
  }
  else{
    digitalWrite(AdxlElPowerPin, LOW);
  }

  pinMode(AdxlAzPowerPin, OUTPUT);
  if(InitAzAccelFlag){
    digitalWrite(AdxlAzPowerPin, HIGH);
    delay(10);
    adxlAz.init();                               // initialize an ADXL345 to communicate using I2C
    Serial.println();
    Serial.println("Az Adxl Initialized");
    Serial.println("Starting Az ADXL Self-Test");
    if(adxlAz.selfTest()){
        Serial.println("Az ADXL Self-Test Passed");
    }
    else{
        Serial.println("Az ADXL Self-Test Failed");
    }
    Serial.println();
    Serial.println("Az Configuration-"); 
    adxlAz.printDataFormat();
    adxlAz.printBWRate();
    adxlAz.printFIFO_CTL();
    attachInterrupt(digitalPinToInterrupt(AdxlAzIntPin), ADXLAZ_ISR, RISING);   // Attach ADXL345 Interrupt
    AzAccelEventFlag = true;         // init as true to empty 
  }
  else{
    digitalWrite(AdxlAzPowerPin, LOW);
  }

  pinMode(AdxlCbPowerPin, OUTPUT);
  if(InitCbAccelFlag){
    digitalWrite(AdxlCbPowerPin, HIGH);
    delay(10);
    adxlCb.init();                               // initialize an ADXL345 to communicate using I2C
    Serial.println();
    Serial.println("Cb Adxl Initialized");
    Serial.println("Starting Cb ADXL Self-Test");
    if(adxlCb.selfTest()){
        Serial.println("Cb ADXL Self-Test Passed");
    }
    else{
        Serial.println("Cb ADXL Self-Test Failed");
    }
    Serial.println();
    Serial.println("Cb Configuration-"); 
    adxlCb.printDataFormat();
    adxlCb.printBWRate();
    adxlCb.printFIFO_CTL();
    attachInterrupt(digitalPinToInterrupt(AdxlCbIntPin), ADXLCB_ISR, RISING);   // Attach ADXL345 Interrupt
    CbAccelEventFlag = true;         // init as true to empty 
  }
  else{
    digitalWrite(AdxlCbPowerPin, LOW);
  }

  if(InitAzEncoderFlag){
    azencoder.init();                          // initialize azimuth encoder to communicate using SPI
    Serial.println("Az Encoder Initialized");
  }

  myTimer.begin(TimerEvent_ISR, TIMER_1MS);  // TimerEvent to run every millisecond
  
  wdog1.feed(); //reset watchdog
  
}

// This is the super loop where we will be keeping track of counters, setting eventflags and calling proccess base on if any event flags were set
void loop() {
  
  //check if temp sensors are ready to be read. Read every 1s
  if(eltempcounter >= eltempthreshold){
    eltempcounter = 0;
    
    if(InitElTempFlag && !El1Errored){
      
      // gets the Elvation motor temperature and checks if sensor is good
      if(!tempSensorEl1.getTemp()){
        El1Errored = true;
        Serial.println("El Temp Senor 1 Stoped Working");
        El2Errored = false;
      }

    }

    // if first temp sensor fails then use the second temp sesnor
    if(InitElTempFlag && El1Errored && !El2Errored){

      if(!tempSensorEl2.getTemp()){ 
        El2Errored = true;
        Serial.println("El Temp Senor 2 Stoped Working");
        El1Errored = false;  // try temp sensor 1 again if the second one fails
      }        
    }
    
  }
  if(aztempcounter >= aztempthreshold){
    aztempcounter = aztempoffset;    //add offset so temps are not samples at the same time

    if(!Az1Errored && InitAzTempFlag){
       // gets the Azmuth motor temperature
      if(!tempSensorAz1.getTemp()){
        Az1Errored = true;
        Serial.println("Az Temp Senor 1 Stoped Working");
        Az2Errored = false;
      }       
    }
    if(Az1Errored && InitAzTempFlag && !Az2Errored){
      if(!tempSensorAz2.getTemp()){
        Az2Errored = true;
        Serial.println("Az Temp Senor 2 Stoped Working");
        Az1Errored = false;  // try temp sensor 1 again if the second one fails
      }           
    }

  }
  //check if elevation encoder is ready to be read. Read every 20ms
  if(encodercounter >= encoderthreshold){//TODO: switch to constant
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
    dataToSend = (uint8_t *)malloc(dataSize * sizeof(uint8_t)); 
    // Create packet to send to Control Room
    prepairTransit(dataToSend, dataSize, &emptyAccBuff, &emptyAccBuff, &emptyAccBuff, &emptyBuff, &emptyBuff, &elencoder.buffer, &azencoder.buffer);
    // Send packet to Control Room
    SendDataToControlRoom(dataToSend, dataSize, ControlRoomIP, TCPPORT, client);
    
    free(dataToSend);
  }

  if(ElAccelEventFlag){
    
    ElAccelEventFlag = false;
    adxlEl.emptyFifo();      // gets the x y and z cordnates and prints them to the serial port.
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
  if(ethernetcounter >= ethernetthreshold){
    ethernetcounter = 0;
    
    // Check if ADXLs stopped working and power cycle them if so
    if(adxlAz.buffer.size() == 0 && InitAzAccelFlag){
      digitalWrite(AdxlAzPowerPin, HIGH);
      delay(10);
      AzAccelEventFlag = true;
      Serial.println("Az ADXL reset");
    }
    if(adxlEl.buffer.size() == 0 && InitElAccelFlag){
      digitalWrite(AdxlElPowerPin, HIGH);
      delay(10);
      ElAccelEventFlag = true; 
      Serial.println("El ADXL reset");
    }
    if(adxlCb.buffer.size() == 0 && InitCbAccelFlag){
      digitalWrite(AdxlCbPowerPin, HIGH);
      delay(10);
      CbAccelEventFlag = true;  
      Serial.println("Cb ADXL reset");       
    }

    uint32_t dataSize = calcTransitSize(adxlEl.buffer.size(), adxlAz.buffer.size(), adxlCb.buffer.size(), tempSensorEl1.buffer.size(), tempSensorAz1.buffer.size(),0,0); // determine the size of the array that needs to be alocated
    uint8_t *dataToSend;
    dataToSend = (uint8_t *)malloc(dataSize * sizeof(uint8_t)); //malloc needs to be used becaus stack size on the loop task is about 4k so this needs to go on the heap
    
    prepairTransit(dataToSend, dataSize, &adxlEl.buffer, &adxlAz.buffer, &adxlCb.buffer, &tempSensorEl1.buffer, &tempSensorAz1.buffer, &emptyBuff, &emptyBuff);

    if(client.connected()){
      SendDataToControlRoom(dataToSend, dataSize, ControlRoomIP, TCPPORT, client);
    }
    else{
      SCB_AIRCR = 0x05FA0004;  // does a software reset
    }

    free(dataToSend);

    wdog1.feed(); //reset watchdog
    HeartBeatLED = !HeartBeatLED;
    digitalWrite(LED1, HeartBeatLED);
    // We need this but I'm not sure why it's hanging here :(
    //client.flush();
  }

}

