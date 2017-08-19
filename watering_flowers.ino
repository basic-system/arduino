#include <SoftwareSerial.h>
#include <SimpleDHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define DEBUG true
#define POMP_PIN 7
#define POMP_INTERVAL 3000
#define WATER_PIN A0
#define WATER_LIMIT 300
#define HUMIDITY_PIN A1
#define HUMIDITY_MIN 400
#define HUMIDITY_MAX 800
#define INTERVAL        60000 * 3


SoftwareSerial esp8266(8,9);
LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display
SimpleDHT11 dht11;

char reply[500];
const bool printReply = true;
const char line[] = "-----\r\n";
// статическая переменная для хранения времени
unsigned long waitTime = 0;
int pinDHT11 = 2;


void setup()
{
  Serial.begin(9600);
  esp8266.begin(9600); // your esp's baud rate might be different
  pinMode(POMP_PIN, OUTPUT);

  lcd.init();
  // Print a message to the LCD.
  lcd.backlight();
  lcd.setCursor(3,0);
  lcd.print("Hello, world!");

  sendData("AT+RST\r\n",2000,DEBUG); // reset module
  // sendData("AT",2000,DEBUG); // send AT command
  // sendData("AT+GMR\r\n",2000,DEBUG); // returns firmware
  // Serial.println("checking mode");
  // sendData("AT+CWMODE=?\r\n",5000,DEBUG); // check supported modes

  // Serial.println("scanning APs");
  // sendData("AT+CWLAP\r\n",21000,DEBUG); // scan list of access points

  write_message("set mode 1");
  sendData("AT+CWMODE=1\r\n",5000,DEBUG); // configure as both mode

  write_message("joining AP");
  sendData("AT+CWJAP=\"<SSID>\",\"<PASSWD>\"\r\n", 10000, DEBUG); // connect to wifi

  write_message("Testing CIFSR");
  sendData("AT+CIFSR\r\n",5000,DEBUG); // get ip address

  write_message("setting for multiple connection");
  sendData("AT+CIPMUX=1\r\n",2000,DEBUG); // configure for multiple connections

  write_message("set port 80 for server");
  sendData("AT+CIPSERVER=1,80\r\n",2000,DEBUG); // turn on server on port 80

  write_message("Ready");
}

void loop()
{
  bool haveWatering = false;
  int currentWater = check_water();
  int currentHumidity =  check_humidity();
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;

  if ((err = dht11.read(pinDHT11, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT11 failed, err="); Serial.println(err);delay(1000);
    return;
  }

  if ( currentWater > WATER_LIMIT && (waitTime == 0 || millis() - waitTime > INTERVAL ) && currentHumidity < HUMIDITY_MIN && currentHumidity != 0 ) {
      watering();
      Serial.println("watering");
      waitTime = millis();
  } else {
       if (millis() - waitTime < INTERVAL) {
          info("We wait INTERVAL", currentHumidity, currentWater, (int)humidity, (int)temperature);
       } else if  (currentHumidity == 0) {
          info("Soil sensor not working", currentHumidity, currentWater, (int)humidity, (int)temperature);
      }  else if (currentHumidity > HUMIDITY_MIN) {
          info("All good", currentHumidity, currentWater, (int)humidity, (int)temperature);
       } else {
        info("Water ended", currentHumidity, currentWater, (int)humidity, (int)temperature);
       }
  }

  if(esp8266.available()) // check if the esp is sending a message
  {

    getReply(2000);

    bool foundIPD = false;
    for (int i=0; i<strlen(reply); i++)
    {
        if (  (reply[i]=='I') && (reply[i+1]=='P') && (reply[i+2]=='D')   ) { foundIPD = true;    }
     }


    if ( foundIPD  ) {
      int connectionId = 0; // esp8266.read()-48; // subtract 48 because the read() function returns
                                           // the ASCII decimal value and 0 (the first decimal number) starts at 48

    // int nameStartPos = 0;
      for (int i=0; i<strlen(reply); i++)
      {
         if (!haveWatering) // just get the first occurrence of name
         {
             if (  (reply[i]=='w') && (reply[i+1]=='a') && (reply[i+2]=='t') && (reply[i+3]=='e')  && (reply[i+4]=='r') && (reply[i+5]=='i') && (reply[i+6]=='n') && (reply[i+7]=='g') )
             {
                 haveWatering = true;
             }
         }
      }
      String webpage;
      if (haveWatering) {
         if ( currentWater > WATER_LIMIT) {
             watering();
             webpage = info("Watering", currentHumidity, currentWater, (int)humidity, (int)temperature);
         } else {
             webpage = info("Water ended", currentHumidity, currentWater, (int)humidity, (int)temperature);
         }
      }
      else {
        if ( currentWater > WATER_LIMIT) {
            webpage = info("All good", currentHumidity, currentWater, (int)humidity, (int)temperature);
         } else {
            webpage = info("Water ended, please refill tank", currentHumidity, currentWater, (int)humidity, (int)temperature);
         }
     }

     String cipSend = "AT+CIPSEND=";
     cipSend += connectionId;
     cipSend += ",";
     cipSend +=webpage.length();
     cipSend +="\r\n";

     sendData(cipSend,200,DEBUG);
     sendData(webpage,200,DEBUG);

     String closeCommand = "AT+CIPCLOSE=";
     closeCommand+=connectionId; // append connection id
     closeCommand+="\r\n";

     sendData(closeCommand,300,DEBUG);
    }
  }
  delay(1000);
}


void sendData(String command, const int timeout, boolean debug)
{
    esp8266.print(command); // send the read character to the esp8266
    long int time = millis();

    while( (time+timeout) > millis())
    {
      while(esp8266.available())
      {
        // The esp has data so display its output to the serial window
        Serial.write(esp8266.read());
      }
    }
}


void getReply(int wait)
{
    int tempPos = 0;
    long int time = millis();
    while( (time + wait) > millis())
    {
        while(esp8266.available())
        {
            char c = esp8266.read();
            if (tempPos < 500) { reply[tempPos] = c; tempPos++;   }
        }
        reply[tempPos] = 0;
    }

    if (printReply) { Serial.println( reply );  Serial.println(line);     }

}


void watering() {
    digitalWrite(POMP_PIN, HIGH);
    delay(POMP_INTERVAL);
    digitalWrite(POMP_PIN, LOW);
}

int check_water() {
    int valuePot = analogRead(WATER_PIN);
    return valuePot;
}

int check_humidity() {
  int valuePot = analogRead(HUMIDITY_PIN);
  return valuePot;
}


void write_message(String message) {
  Serial.println(message);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(message);
}


String info(String status, int SoilHum, int Water, int Hum, int Temp) {
  String web_msg;
  web_msg += "{";
  web_msg += "\"status\":\"" + status + "\"";
  web_msg += ",\"temperature\":\"" + String(Temp) + "\"";
  web_msg += ",\"humidity\":\"" + String(Hum) + "\"";
  web_msg += ",\"water\":\"" + String(Water) + "\"";
  web_msg += ",\"soil_humidity\":\"" + String(SoilHum) + "\"";
  web_msg += "}";

  Serial.println(web_msg);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(status);
  lcd.setCursor(12,0);
  lcd.print(" ");
  lcd.print(Temp);
  lcd.print("C");
  lcd.setCursor(0,1);
  lcd.print("SH:");
  lcd.print(SoilHum);
  lcd.print(" H:");
  lcd.print(Hum);
  lcd.print("% W:");
  lcd.print(Water);

  return web_msg;
}
