/******************************************************************************
  erie-weather-station.ino
  Weather Station w/ Weather Shield and Weather Meters
  by: Jim Lindblom @ SparkFun Electronics
  based on code by Joel Bartlett @ SparkFun Electronics
  Original Creation Date: July 13, 2016

  This sketch prints the temperature, humidity, barometric pressure, wind, and
  rain status to a Phant stream and the serial port.
  
  Make sure you fill out the Phant variables - publicKey, privateKey, and server.

  Development environment specifics:
  	IDE: Particle Dev
  	Hardware Platform: Particle Photon
                       Particle Core

  This code is beerware; if you see me (or any other SparkFun
  employee) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
*******************************************************************************/
#include "SparkFun_Photon_Weather_Shield_Library/SparkFun_Photon_Weather_Shield_Library.h"
#include "SparkFunPhant/SparkFunPhant.h"

////////////PHANT STUFF//////////////////////////////////////////////////////////////////
const char server[] = "data.sparkfun.com";
const char publicKey[] = "";
const char privateKey[] = "";
Phant phant(server, publicKey, privateKey);
/////////////////////////////////////////////////////////////////////////////////////////

int WDIR = A0;
int RAIN = D2;
int WSPEED = D3;

//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond; //The millis counter to see when a second rolls by
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data

//We need to keep track of the following variables:
//Wind speed/dir each update (no storage)
//Wind gust/dir over the day (no storage)
//Wind speed/dir, avg over 2 minutes (store 1 per second)
//Wind gust/dir over last 10 minutes (store 1 per minute)
//Rain over the past hour (store 1 per minute)
//Total rain over date (store one per day)

byte windspdavg[120]; //120 bytes to keep track of 2 minute average
int winddiravg[120]; //120 ints to keep track of 2 minute average
float windgust_10m[10]; //10 floats to keep track of 10 minute max
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
int winddir = 0; // [0-360 instantaneous wind direction]
float windspeedmph = 0; // [mph instantaneous wind speed]
float windgustmph = 0; // [mph current wind gust, using software specific time period]
int windgustdir = 0; // [0-360 using software specific time period]
float windspdmph_avg2m = 0; // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0; // [0-360 2 minute average wind direction]
float windgustmph_10m = 0; // [mph past 10 minutes wind gust mph ]
int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]
float rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
long lastWindCheck = 0;
volatile float dailyrainin = 0; // [rain inches so far today in local time]

float humidity = 0;
float tempf = 0;
float pascals = 0;
//float altf = 0;//uncomment this if using altitude mode instead
float baroTemp = 0;

int count = 0;//This triggers a post and print on the first time through the loop

// volatiles are subject to modification by IRQs
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;
volatile unsigned long raintime, rainlast, raininterval, rain;

//Create Instance of HTU21D or SI7021 temp and humidity sensor and MPL3115A2 barrometric sensor
Weather sensor;

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
  raintime = millis(); // grab current time
  raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
  {
    dailyrainin += 0.011; //Each dump is 0.011" of water
    rainHour[minutes] += 0.011; //Increase this minute's amount of rain

    rainlast = raintime; // set up for next event
  }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
  if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
  {
    lastWindIRQ = millis(); //Grab the current time
    windClicks++; //There is 1.492MPH for each click per second.
  }
}

//---------------------------------------------------------------
void setup()
{
    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor

    Serial.begin(9600);   // open serial over USB
    //Begin posting data immediately instead of waiting for a key press.

    //Initialize the I2C sensors and ping them
    sensor.begin();

    /*You can only receive acurate barrometric readings or acurate altitiude
    readings at a given time, not both at the same time. The following two lines
    tell the sensor what mode to use. You could easily write a function that
    takes a reading in one made and then switches to the other mode to grab that
    reading, resulting in data that contains both acurate altitude and barrometric
    readings. For this example, we will only be using the barometer mode. Be sure
    to only uncomment one line at a time. */
    sensor.setModeBarometer();//Set to Barometer Mode
    //baro.setModeAltimeter();//Set to altimeter Mode

    //These are additional MPL3115A2 functions the MUST be called for the sensor to work.
    sensor.setOversampleRate(7); // Set Oversample rate
    //Call with a rate from 0 to 7. See page 33 for table of ratios.
    //Sets the over sample rate. Datasheet calls for 128 but you can set it
    //from 1 to 128 samples. The higher the oversample rate the greater
    //the time between data samples.

    sensor.enableEventFlags(); //Necessary register calls to enble temp, baro ansd alt

    seconds = 0;
    lastSecond = millis();

    // attach external interrupt pins to IRQ functions
    attachInterrupt(RAIN, rainIRQ, FALLING);
    attachInterrupt(WSPEED, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();
    
    getWeather();//get initial values out of the way
    delay(1000);
    getWeather();//print before entering the loop and waiting for count
    printInfo();
    
}
//---------------------------------------------------------------
void loop()
{
  //Keep track of which minute it is
  if(millis() - lastSecond >= 1000)
  {

    lastSecond += 1000;

    //Take a speed and direction reading every second for 2 minute average
    if(++seconds_2m > 119) seconds_2m = 0;

    //Calc the wind speed and direction every second for 120 second to get 2 minute average
    float currentSpeed = get_wind_speed();
    //float currentSpeed = random(5); //For testing
    int currentDirection = get_wind_direction();
    windspdavg[seconds_2m] = (int)currentSpeed;
    winddiravg[seconds_2m] = currentDirection;
    //if(seconds_2m % 10 == 0) displayArrays(); //For testing

    //Check to see if this is a gust for the minute
    if(currentSpeed > windgust_10m[minutes_10m])
    {
      windgust_10m[minutes_10m] = currentSpeed;
      windgustdirection_10m[minutes_10m] = currentDirection;
    }

    //Check to see if this is a gust for the day
    if(currentSpeed > windgustmph)
    {
      windgustmph = currentSpeed;
      windgustdir = currentDirection;
    }

    if(++seconds > 59)
    {
      seconds = 0;

      if(++minutes > 59) minutes = 0;
      if(++minutes_10m > 9) minutes_10m = 0;

      rainHour[minutes] = 0; //Zero out this minute's rainfall amount
      windgust_10m[minutes_10m] = 0; //Zero out this minute's gust
    }


    //Rather than use a delay, keeping track of a counter allows the photon to
    // still take readings and do work in between printing out data.
    count++;
    //alter this number to change the amount of time between each reading
    getWeather();
    printInfo();
    if(count >= 60)
    {
      //Get readings from all sensors
       if (postToPhant() > 0)
        count = 0;
    }
  }
}
//---------------------------------------------------------------
void printInfo()
{
  //This function prints the weather data out to the default Serial Port
      Serial.print("Wind_Dir: " + get_wind_dir_string() + ", ");

      Serial.print(" Wind_Speed: ");
      Serial.print(windspeedmph, 1);
      Serial.print("mph, ");

      Serial.print("Rain:");
      Serial.print(rainin, 2);
      Serial.print("in., ");

      Serial.print("Temp:");
      Serial.print(tempf);
      Serial.print("F, ");

      Serial.print("Humidity:");
      Serial.print(humidity);
      Serial.print("%, ");

      Serial.print("Baro_Temp:");
      Serial.print(baroTemp);
      Serial.print("F, ");

      Serial.print("Pressure:");
      Serial.print(pascals/100);
      Serial.print("hPa");
      Serial.println();
      //The MPL3115A2 outputs the pressure in Pascals. However, most weather stations
      //report pressure in hectopascals or millibars. Divide by 100 to get a reading
      //more closely resembling what online weather reports may say in hPa or mb.
      //Another common unit for pressure is Inches of Mercury (in.Hg). To convert
      //from mb to in.Hg, use the following formula. P(inHg) = 0.0295300 * P(mb)
      //More info on conversion can be found here:
      //www.srh.noaa.gov/images/epz/wxcalc/pressureConversion.pdf

      //If in altitude mode, print with these lines
      //Serial.print("Altitude:");
      //Serial.print(altf);
      //Serial.println("ft.");
}

String get_wind_dir_string()
{
    switch (winddir)
      {
        case 0:
          return "North";
          break;
        case 1:
          return "North East";
          break;
        case 2:
          return "East";
          break;
        case 3:
          return "South East";
          break;
        case 4:
          return "South";
          break;
        case 5:
          return "South West";
          break;
        case 6:
          return "West";
          break;
        case 7:
          return "North West";
          break;
        default:
          return "No Wind";
          // if nothing else matches, do the
          // default (which is optional)
      }
}
//---------------------------------------------------------------
//Read the wind direction sensor, return heading in degrees
int get_wind_direction()
{
  unsigned int adc;

  adc = analogRead(WDIR); // get the current reading from the sensor

  // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
  // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
  // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

  //Wind Vains may vary in the values they return. To get exact wind direction,
  //it is recomended that you AnalogRead the Wind Vain to make sure the values
  //your wind vain output fall within the values listed below.
  if(adc > 2270 && adc < 2290) return (0);//North
  if(adc > 3220 && adc < 3299) return (1);//NE
  if(adc > 3890 && adc < 3999) return (2);//East
  if(adc > 3780 && adc < 3850) return (3);//SE

  if(adc > 3570 && adc < 3650) return (4);//South
  if(adc > 2790 && adc < 2850) return (5);//SW
  if(adc > 1580 && adc < 1610) return (6);//West
  if(adc > 1930 && adc < 1950) return (7);//NW

  return (-1); // error, disconnected?
}
//---------------------------------------------------------------
//Returns the instataneous wind speed
float get_wind_speed()
{
  float deltaTime = millis() - lastWindCheck; //750ms

  deltaTime /= 1000.0; //Covert to seconds

  float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4

  windClicks = 0; //Reset and start watching for new wind
  lastWindCheck = millis();

  windSpeed *= 1.492; //4 * 1.492 = 5.968MPH

  /* Serial.println();
   Serial.print("Windspeed:");
   Serial.println(windSpeed);*/

  return(windSpeed);
}
//---------------------------------------------------------------
void getWeather()
{
    // Measure Relative Humidity from the HTU21D or Si7021
    humidity = sensor.getRH();

    // Measure Temperature from the HTU21D or Si7021
    tempf = sensor.getTempF();
    // Temperature is measured every time RH is requested.
    // It is faster, therefore, to read it from previous RH
    // measurement with getTemp() instead with readTemp()

    //Measure the Barometer temperature in F from the MPL3115A2
    baroTemp = sensor.readBaroTempF();

    //Measure Pressure from the MPL3115A2
    pascals = sensor.readPressure();

    //If in altitude mode, you can get a reading in feet  with this line:
    //altf = sensor.readAltitudeFt();

    //Calc winddir
    winddir = get_wind_direction();

    //Calc windspeed
    windspeedmph = get_wind_speed();

    //Calc windgustmph
    //Calc windgustdir
    //Report the largest windgust today
    windgustmph = 0;
    windgustdir = 0;

    //Calc windspdmph_avg2m
    float temp = 0;
    for(int i = 0 ; i < 120 ; i++)
      temp += windspdavg[i];
    temp /= 120.0;
    windspdmph_avg2m = temp;

    //Calc winddir_avg2m
    temp = 0; //Can't use winddir_avg2m because it's an int
    for(int i = 0 ; i < 120 ; i++)
      temp += winddiravg[i];
    temp /= 120;
    winddir_avg2m = temp;

    //Calc windgustmph_10m
    //Calc windgustdir_10m
    //Find the largest windgust in the last 10 minutes
    windgustmph_10m = 0;
    windgustdir_10m = 0;
    //Step through the 10 minutes
    for(int i = 0; i < 10 ; i++)
    {
      if(windgust_10m[i] > windgustmph_10m)
      {
        windgustmph_10m = windgust_10m[i];
        windgustdir_10m = windgustdirection_10m[i];
      }
    }

    //Total rainfall for the day is calculated within the interrupt
    //Calculate amount of rainfall for the last 60 minutes
    rainin = 0;
    for(int i = 0 ; i < 60 ; i++)
      rainin += rainHour[i];
}
//---------------------------------------------------------------
int postToPhant()
{
    phant.add("humidity", String(humidity, 1));
    phant.add("pressure", String(pascals/100, 0));
    phant.add("rainin", String(rainin, 2));
    phant.add("temperature", String(tempf, 1));
    phant.add("winddir", get_wind_dir_string());
    phant.add("windspeed", String(windspeedmph, 1));

    TCPClient client;
    char response[512];
    int i = 0;
    int retVal = 0;

    if (client.connect(server, 80))
    {
        Serial.println("Posting!");
        client.print(phant.post());
        //delay(100);
        int timeout = 10000;
        while ((client.available() <= 0) && timeout--)
            delay(1);
        while (client.available())
        {
            char c = client.read();
            if (i < 512)
                response[i++] = c;
        }
        if (strstr(response, "200 OK"))
        {
            Serial.println("Post success!");
            retVal = 1;
        }
        else if (strstr(response, "400 Bad Request"))
        {
            Serial.println("Bad request");
            retVal = -1;
        }
        else
        {
            Serial.println("Bad response: " + String(response));
            retVal = -2;
        }
    }
    else
    {
        Serial.println("connection failed");
        retVal = -3;
    }
    client.stop();
    return retVal;

}
