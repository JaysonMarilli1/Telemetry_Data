/*
 * @JaysonMarilli
 * Current Issues:
 * Arduino crashes (takes long to initialise if there is no SD card present.
 * Log file properties are incorrect. IE date and time
*
* TSL2561 wired as follows: connect SCL to SCL, pin 21 on MEga | connect SDA to SDA, pin 20 on Mega | connect VDD to 3.3V DC | connect GROUND to common ground | ADDR can be connected to ground, or vdd or left floating to change the i2c address
* TMP36 wired as follows: 3.3v to TMP36 +, GND to TMP36 -, A0 to the middle pin
* DS18B20 wired as follows: 3.3v to DS18B20 + (RED), GND to DS18B20 - (BLUE), Data wire (YELLOW) is plugged into pin 6 on the Arduino
*
* The data are written to the microSD during the ISR(TIMER5_COMPA_vect) timer call.
* This is however not optimal, as using the SPI is not recommended during a timer call.
* In this particular case, however no consequences can be observed as the writeToSD()
* function is call during two transmissions. The initial idea is to have the possibility to
* pause in-between two transmissions, see Line 689 in List. 3.
* 
* http://1.bp.blogspot.com/-nHtGBeCNF2I/UccYLXJBCuI/AAAAAAAAAzQ/mlPT-NNrt3M/s1600/Breadboard.png
*/

// General libraries
#include <Wire.h>

// include the SD library:
#include <SPI.h>
#include <SD.h>

// avr-libc library includes
#include <avr/io.h>
#include <avr/interrupt.h>

// For DS18B20 external temp sensor From: http://www.hobbytronics.co.uk/ds18b20-arduino
#include <OneWire.h>
#include <DallasTemperature.h>

// Library for the TSL2561 Lux Sensor
#include "TSL2561.h"

// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 5

#define LEDPIN 6

// ************************************************************************************************************************************************************
// ************************************************************************************************************************************************************
//                                   Comment the below line out when sending to the field as a possible energy saver
#define ECHO_TO_SERIAL
// ************************************************************************************************************************************************************
// ************************************************************************************************************************************************************

// Setup a oneWire instance to communicate with any OneWire devices
// (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

//TMP36 Pin Variables
int tempInput = 0; //the analog pin the TMP36's Vout (sense) pin is connected to
//the resolution is 10 mV / degree centigrade with a
//500 mV offset to allow for negative temperatures

// Initialised for Timer1
const int READ_SENSOR_MIN = 5;
unsigned int minutes = 0;

bool READ_SENSOR_FLAG = false;

const String SW_VERSION = "1.160202c";

// The address will be different depending on whether you let
// the ADDR pin float (addr 0x39), or tie it to ground or vcc. In those cases
// use TSL2561_ADDR_LOW (0x29) or TSL2561_ADDR_HIGH (0x49) respectively
TSL2561 tsl(TSL2561_ADDR_FLOAT);

// set up variables using the SD utility library functions:
Sd2Card card;
File dataFile; // log data file

// Define the chip select pin on the MEGA
const int chipSelect = 53;

// Data file stored on the SD card as well as the header for the file. Need to arrange for a dynamic name??
char logfilename[13] = "SenData2.txt"; // cmax 8 char befor dot!
String logFileHeader = " Internal Temp , External Temp , IR , Full , Visible , LUX , ";

// define sensor variables
float externalTempDATA = 0.0;
float internalTempDATA = 0.0;
int irDATA = 0;
int fullDATA = 0;
int visibleDATA = 0;
int luxDATA = 0;

void setup()
{
  Serial.begin(9600);

#ifdef ECHO_TO_SERIAL
  Serial.println("ECHO_TO_SERIAL is enabled. Dont forget to comment this out before sending to the field!!");
#else
  Serial.println("ECHO_TO_SERIAL is disabled for power saving. If you want serial data please enable!!");
  Serial.println("There will be no further serial data printed.");
#endif

  pinMode(LEDPIN, OUTPUT);

  // Initialise the sensors. (internal temp sensor doesnt need to be initialised).
  initLuxSensor();
  initTempExternal();

  // Initialise Timer1
  initTimer1();

  // Initialise the SD card then write the header to the log file
  initSDCard();
  //  writeSDHeader();

  // enable global interrupts:
  sei();
}

void loop()
{
  //  float myTemp = 0.0;   // stores the calculated temperature

  // main program
  if (READ_SENSOR_FLAG) {
    readLux();
    readTempInternal();
    readTempExternal();
    READ_SENSOR_FLAG = false;
    writeToSD(); // Write the data to the SD card one all the sensors have been polled
  }


  //  delay(1000);
}

ISR(TIMER1_COMPA_vect) {
  minutes++;
 //   Serial.println(minutes);
  if (minutes == (READ_SENSOR_MIN))// * 60))
  {
    digitalWrite(LEDPIN, !digitalRead(LEDPIN));
    READ_SENSOR_FLAG = true;
    minutes = 0;
  }
}

void initTimer1() {
  // initialize Timer1
  cli();          // disable global interrupts
  TCCR1A = 0;     // set entire TCCR1A register to 0
  TCCR1B = 0;     // same for TCCR1B
  // set compare match register to desired timer count:
  OCR1A = 15624;
  // turn on CTC mode:
  TCCR1B |= (1 << WGM12);
  // Set CS10 and CS12 bits for 1024 prescaler:
  TCCR1B |= (1 << CS10);
  TCCR1B |= (1 << CS12);
  // enable timer compare interrupt:
  TIMSK1 |= (1 << OCIE1A);
}

void initLuxSensor() {
  if (tsl.begin()) {
#ifdef ECHO_TO_SERIAL
    Serial.println("TSL2561 (LUX Sensor) found");
#endif

  } else {
#ifdef ECHO_TO_SERIAL
    Serial.println("TSL2561 (LUX Sensor) NOT found???");
#endif
  }

  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  //tsl.setGain(TSL2561_GAIN_0X);         // set no gain (for bright situtations)
  tsl.setGain(TSL2561_GAIN_16X);      // set 16x gain (for dim situations)

  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_13MS);  // shortest integration time (bright light)
  //tsl.setTiming(TSL2561_INTEGRATIONTIME_101MS);  // medium integration time (medium light)
  tsl.setTiming(TSL2561_INTEGRATIONTIME_402MS);  // longest integration time (dim light)
}

void readLux() {
  // Simple data read example. Just read the infrared, fullspecrtrum diode
  // or 'visible' (difference between the two) channels.
  // This can take 13-402 milliseconds! Uncomment whichever of the following you want to read
  uint16_t x = tsl.getLuminosity(TSL2561_VISIBLE);
  //uint16_t x = tsl.getLuminosity(TSL2561_FULLSPECTRUM);
  //uint16_t x = tsl.getLuminosity(TSL2561_INFRARED);

  //  Serial.println(x, DEC);

  // More advanced data read example. Read 32 bits with top 16 bits IR, bottom 16 bits full spectrum
  // That way you can do whatever math and comparisons you want!
  uint32_t lum = tsl.getFullLuminosity();
  uint16_t ir, full;
  ir = lum >> 16;
  full = lum & 0xFFFF;
  irDATA = ir;
  fullDATA = full;
  visibleDATA = full - ir;
  luxDATA = tsl.calculateLux(full, ir);
#ifdef ECHO_TO_SERIAL
  Serial.print("IR: "); Serial.print(irDATA);   Serial.print("\t");
  Serial.print("Full: "); Serial.print(fullDATA);   Serial.print("\t");
  Serial.print("Visible: "); Serial.print(visibleDATA);   Serial.print("\t");
  Serial.print("Lux: "); Serial.println(luxDATA);
#endif
}

void readTempInternal() {
  float temperature = 0.0;   // stores the calculated temperature
  int sample;                // counts through ADC samples
  float ten_samples = 0.0;   // stores sum of 10 samples

  // take 10 samples from the TMP36
  for (sample = 0; sample < 10; sample++) {
    // convert A0 value to temperature
    temperature = ((float)analogRead(tempInput) * 5.0 / 1024.0) - 0.5;
    temperature = temperature / 0.01;
    // sample every 0.1 seconds
    delay(100);
    // sum of all samples
    ten_samples = ten_samples + temperature;
  }
  // get the average value of 10 temperatures
  internalTempDATA = ten_samples / 10.0;
#ifdef ECHO_TO_SERIAL
  Serial.print("Internal temp sensor reading: "); Serial.print(internalTempDATA); Serial.println(" degrees C");
#endif
  ten_samples = 0.0;
}

void initTempExternal() {
#ifdef ECHO_TO_SERIAL
  Serial.println("Dallas Temperature IC Control Library started.");
#endif
  delay(100);
  // Start up the library
  sensors.begin();
}

void readTempExternal() {
  // call sensors.requestTemperatures() to issue a global temperature
  // request to all devices on the bus
  sensors.requestTemperatures(); // Send the command to get temperatures
  externalTempDATA = sensors.getTempCByIndex(0);
  //  Serial.print(sensors.getTempCByIndex(0)); // Why "byIndex"?
  // You can have more than one IC on the same bus.
  // 0 refers to the first IC on the wire
#ifdef ECHO_TO_SERIAL
  Serial.print("External temp sensor reading: ");
  Serial.print(externalTempDATA); Serial.print(" degrees C"); Serial.println("");
#endif
}

// The below function initialises the SD card then, if the data file does not already exsist, writes the header to the file.
void initSDCard() {
#ifdef ECHO_TO_SERIAL
  Serial.print("Initializing SD card... ");
#endif
  // open (create) file and close it, as check
  if (!SD.begin(chipSelect)) {
#ifdef ECHO_TO_SERIAL
    Serial.println("Card failed, or not present");
#endif
    // don't do anything more. But if we cant log data is it worth it to continue? If so send warning SMS>??
    return;
  } else {
    // If the file does not exsist, create it with the header and first time info
    if (SD.exists(logfilename) == false) {
      dataFile = SD.open(logfilename, FILE_WRITE); // if file does not exist it will be created but it should exsist at this point
      if (dataFile) {
        dataFile.print("Software Version: "); dataFile.println(SW_VERSION);
        dataFile.print("Reading intervals: "); dataFile.print(READ_SENSOR_MIN); dataFile.println(" minutes");
        dataFile.println(logFileHeader);
        dataFile.close();
#ifdef ECHO_TO_SERIAL
        Serial.print(logfilename); Serial.println(" created with headers.");
#endif
      }
      // if the file isn't open, pop up an error:
      else {
        Serial.println("Error opening " + String(logfilename));
      }
    } else {
#ifdef ECHO_TO_SERIAL
      Serial.println("Log file already exsists. Card initialized.");
#endif
    }
  }
}

void writeToSD() {
  dataFile = SD.open(logfilename, FILE_WRITE); // if file does not exist it will be created but it should exsist at this point
  if (dataFile) {
    // Below is the string that will be written to the SD card. Need to maybe add error checking if sensor data is invalid.
    // Telemetry data will be written under the below HEADINGS. 1 line per write all data seperated by "," for ease of import to excel. To be upgraded!!!*!*!**!
    //  Internal Temp , External Temp , LUX ,
    dataFile.print(" ");
    dataFile.print(internalTempDATA); dataFile.print(" ,");
    dataFile.print(externalTempDATA); dataFile.print(" ,");
    dataFile.print(irDATA); dataFile.print(" ,");
    dataFile.print(fullDATA); dataFile.print(" ,");
    dataFile.print(visibleDATA); dataFile.print(" ,");
    dataFile.print(luxDATA); dataFile.print(" ,");
    dataFile.println(""); // EOL
    dataFile.close(); // close data file until next write
#ifdef ECHO_TO_SERIAL
    Serial.println("***** DATA LOGGING COMPLETE *****");
#endif
  } else {
#ifdef ECHO_TO_SERIAL
    Serial.println("***** ERROR WRITING TO DATA FILE *****");
#endif
  }
}

//void writeSDHeader() {
//  // If the file does not exsist, create it with the header and first time info
//  if (SD.exists(logfilename) == false) {
//    dataFile = SD.open(logfilename, FILE_WRITE); // if file does not exist it will be created but it should exsist at this point
//    if (dataFile) {
//      //    logFile.println(", "); // Just a leading blank line, incase there was previous data
//      dataFile.println(logFileHeader);
//      dataFile.close();
//      // print to the serial port for easy reading
//      //      Serial.println("Log file created with below headers");
//      //      Serial.println(logFileHeader);
//    }
//    // if the file isn't open, pop up an error:
//    else {
//      Serial.println("error opening " + String(dataFile));
//    }
//  }
//}

