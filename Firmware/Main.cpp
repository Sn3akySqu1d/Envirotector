#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_SleepyDog.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define DHTPIN 0
#define DHTTYPE DHT11
#define SD_CS 26
#define ENC_A 28
#define ENC_B 29
#define ENC_SW 27
#define DISPLAY_TIMEOUT 30000
#define SLEEP_DURATION 5000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP085 bmp;
RTC_DS3231 rtc;

int lastEncoded = 0;
long encoderValue = 0;
int lastButtonState = HIGH;
bool buttonPressed = false;
bool displayOn = true;
unsigned long lastActive = 0;

enum Screen { 
  TIME_SCREEN, 
  TEMP_SCREEN, 
  HUMIDITY_SCREEN, 
  PRESSURE_SCREEN, 
  GRAPH_SCREEN 
};
Screen currentScreen = TIME_SCREEN;
const int SCREEN_COUNT = 5;

const int DATA_POINTS = 144;
const int LOG_INTERVAL = 600000;
unsigned long lastLogTime = 0;

struct SensorData {
  float temp;
  float humidity;
  float pressure;
  DateTime time;
};
SensorData dataHistory[DATA_POINTS];
int dataIndex = 0;
bool dataFull = false;

float dayMinTemp = 1000.0, dayMaxTemp = -1000.0;
float dayMinHum = 1000.0, dayMaxHum = -1000.0;
float dayMinPress = 1000.0, dayMaxPress = -1000.0;
int currentDay = -1;

float lastValidTemp = 0;
float lastValidHum = 0;
float lastValidPress = 0;
bool tempValid = false;
bool humValid = false;
bool pressValid = false;

void setup() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while(1);
  }
  display.clearDisplay();
  display.display();
  
  dht.begin();
  if (!bmp.begin()) {
    display.println("BMP180 FAIL");
    display.display();
  }
  if (!rtc.begin()) {
    display.println("RTC FAIL");
    display.display();
  }
  
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  if (!SD.begin(SD_CS)) {
    display.println("SD CARD FAIL");
    display.display();
  }
  
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  
  for(int i=0; i<DATA_POINTS; i++) {
    dataHistory[i] = {0,0,0, DateTime(0)};
  }
  
  if (!SD.exists("datalog.csv")) {
    File dataFile = SD.open("datalog.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("DateTime,TempC,Humidity%,PressurehPa");
      dataFile.close();
    }
  }
  
  DateTime now = rtc.now();
  currentDay = now.day();
  lastActive = millis();
}

void loop() {
  if (!displayOn) {
    Watchdog.sleep(SLEEP_DURATION);
    return;
  }
  
  DateTime now = rtc.now();
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  float pressure = bmp.readPressure() / 100.0F;

  humValid = !isnan(humidity) && humidity >= 0 && humidity <= 100;
  tempValid = !isnan(temperature) && temperature >= -40 && temperature <= 80;
  pressValid = !isnan(pressure) && pressure >= 300 && pressure <= 1100;

  if (!tempValid) temperature = lastValidTemp;
  else lastValidTemp = temperature;
  
  if (!humValid) humidity = lastValidHum;
  else lastValidHum = humidity;
  
  if (!pressValid) pressure = lastValidPress;
  else lastValidPress = pressure;

  if (tempValid && humValid && pressValid) {
    updateMinMax(temperature, humidity, pressure, now);
  }
  
  logData(now, temperature, humidity, pressure);
  
  updateDisplay(now, temperature, humidity, pressure);
  
  if (millis() - lastActive > DISPLAY_TIMEOUT) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    displayOn = false;
  }
  
  delay(50);
}

void updateMinMax(float temp, float hum, float press, DateTime now) {
  if(now.day() != currentDay) {
    currentDay = now.day();
    dayMinTemp = dayMaxTemp = temp;
    dayMinHum = dayMaxHum = hum;
    dayMinPress = dayMaxPress = press;
  }
  
  if(temp < dayMinTemp) dayMinTemp = temp;
  if(temp > dayMaxTemp) dayMaxTemp = temp;
  if(hum < dayMinHum) dayMinHum = hum;
  if(hum > dayMaxHum) dayMaxHum = hum;
  if(press < dayMinPress) dayMinPress = press;
  if(press > dayMaxPress) dayMaxPress = press;
}

void logData(DateTime now, float temp, float hum, float press) {
  if (millis() - lastLogTime >= LOG_INTERVAL && tempValid && humValid && pressValid) {
    dataHistory[dataIndex] = {temp, hum, press, now};
    dataIndex = (dataIndex + 1) % DATA_POINTS;
    if(!dataFull && dataIndex == 0) dataFull = true;
    
    File dataFile = SD.open("datalog.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.print(now.timestamp(DateTime::TIMESTAMP_FULL));
      dataFile.print(",");
      dataFile.print(temp);
      dataFile.print(",");
      dataFile.print(hum);
      dataFile.print(",");
      dataFile.println(press);
      dataFile.close();
    }
    
    lastLogTime = millis();
  }
}

void readEncoder() {
  int MSB = digitalRead(ENC_A);
  int LSB = digitalRead(ENC_B);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
    encoderValue++;
    if(encoderValue % 2 == 0) {
      currentScreen = static_cast<Screen>((currentScreen + 1) % SCREEN_COUNT);
    }
    lastActive = millis();
  }
  else if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
    encoderValue--;
    if(encoderValue % 2 == 0) {
      currentScreen = static_cast<Screen>((currentScreen + SCREEN_COUNT - 1) % SCREEN_COUNT);
    }
    lastActive = millis();
  }
  lastEncoded = encoded;

  int btnState = digitalRead(ENC_SW);
  if (btnState == LOW && lastButtonState == HIGH) {
    if (!displayOn) {
      displayOn = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
    }
    buttonPressed = true;
    lastActive = millis();
    delay(50);
  }
  lastButtonState = btnState;
}

void updateDisplay(DateTime now, float temp, float hum, float press) {
  if (!displayOn) return;
  
  display.clearDisplay();
  readEncoder();
  
  switch (currentScreen) {
    case TIME_SCREEN:
      drawTimeScreen(now);
      break;
    case TEMP_SCREEN:
      drawTempScreen(temp);
      break;
    case HUMIDITY_SCREEN:
      drawHumidityScreen(hum);
      break;
    case PRESSURE_SCREEN:
      drawPressureScreen(press);
      break;
    case GRAPH_SCREEN:
      drawGraphScreen();
      break;
  }
  
  if (!tempValid || !humValid || !pressValid) {
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    if (!tempValid) display.print("TEMP ERR ");
    if (!humValid) display.print("HUM ERR ");
    if (!pressValid) display.print("PRESS ERR");
  }
  
  display.display();
}

void drawTimeScreen(DateTime now) {
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  
  display.print(now.year());
  display.print('-');
  if(now.month() < 10) display.print('0');
  display.print(now.month());
  display.print('-');
  if(now.day() < 10) display.print('0');
  display.println(now.day());

  display.setTextSize(3);
  display.setCursor(0, 25);
  if(now.hour() < 10) display.print('0');
  display.print(now.hour());
  display.print(':');
  if(now.minute() < 10) display.print('0');
  display.print(now.minute());
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  display.print(days[now.dayOfTheWeek()]);
}

void drawTempScreen(float temp) {
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Temperature");
  
  display.setTextSize(3);
  display.setCursor(0, 25);
  if (tempValid) {
    display.print(temp, 1);
    display.println(" C");
  } else {
    display.println("-- ERR --");
  }
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("Min: ");
  display.print(dayMinTemp, 1);
  display.print(" C");
  
  display.setCursor(64, 55);
  display.print("Max: ");
  display.print(dayMaxTemp, 1);
  display.print(" C");
}

void drawHumidityScreen(float hum) {
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Humidity");
  
  display.setTextSize(3);
  display.setCursor(0, 25);
  if (humValid) {
    display.print(hum, 0);
    display.println(" %");
  } else {
    display.println("-- ERR --");
  }
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("Min: ");
  display.print(dayMinHum, 0);
  display.print(" %");
  
  display.setCursor(64, 55);
  display.print("Max: ");
  display.print(dayMaxHum, 0);
  display.print(" %");
}

void drawPressureScreen(float press) {
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Pressure");
  
  display.setTextSize(3);
  display.setCursor(0, 25);
  if (pressValid) {
    display.print(press, 1);
    display.println(" hPa");
  } else {
    display.println("-- ERR --");
  }
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("Min: ");
  display.print(dayMinPress, 1);
  display.print(" hPa");
  
  display.setCursor(64, 55);
  display.print("Max: ");
  display.print(dayMaxPress, 1);
  display.print(" hPa");
}

void drawGraphScreen() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("24h Trends");
  
  const int graphHeight = 38;
  const int graphTop = 10;
  const int graphBottom = graphTop + graphHeight;
  
  display.drawLine(0, graphTop, 0, graphBottom, WHITE);
  display.drawLine(0, graphBottom, 127, graphBottom, WHITE);
  
  float minTemp = 1000, maxTemp = -1000;
  float minHum = 1000, maxHum = -1000;
  float minPress = 1000, maxPress = -1000;
  
  int validPoints = 0;
  for(int i=0; i < (dataFull ? DATA_POINTS : dataIndex); i++) {
    if(dataHistory[i].time.year() > 2000) {
      minTemp = min(minTemp, dataHistory[i].temp);
      maxTemp = max(maxTemp, dataHistory[i].temp);
      minHum = min(minHum, dataHistory[i].humidity);
      maxHum = max(maxHum, dataHistory[i].humidity);
      minPress = min(minPress, dataHistory[i].pressure);
      maxPress = max(maxPress, dataHistory[i].pressure);
      validPoints++;
    }
  }
  
  float tempRange = maxTemp - minTemp;
  float humRange = maxHum - minHum;
  float pressRange = maxPress - minPress;
  
  minTemp -= tempRange * 0.1;
  maxTemp += tempRange * 0.1;
  minHum -= humRange * 0.1;
  maxHum += humRange * 0.1;
  minPress -= pressRange * 0.1;
  maxPress += pressRange * 0.1;
  
  for (int y = graphTop; y <= graphBottom; y += 10) {
    for (int x = 0; x < 128; x += 2) {
      display.drawPixel(x, y, WHITE);
    }
  }
  
  int prevTempY = -1, prevHumY = -1, prevPressY = -1;
  int pointCount = min(validPoints, 128);
  
  for(int x=0; x<pointCount; x++) {
    int dataIdx = (dataIndex - pointCount + x + DATA_POINTS) % DATA_POINTS;
    
    if(dataHistory[dataIdx].time.year() > 2000) {
      int tempY = map(dataHistory[dataIdx].temp, minTemp, maxTemp, graphBottom, graphTop);
      int humY = map(dataHistory[dataIdx].humidity, minHum, maxHum, graphBottom, graphTop);
      int pressY = map(dataHistory[dataIdx].pressure, minPress, maxPress, graphBottom, graphTop);
      
      if(x > 0) {
        display.drawLine(x-1, prevTempY, x, tempY, WHITE);
        
        if(x % 2 == 0) {
          display.drawLine(x-1, prevHumY, x, humY, WHITE);
        }

        if(x % 3 != 0) {
          display.drawLine(x-1, prevPressY, x, pressY, WHITE);
        }
      }
      
      prevTempY = tempY;
      prevHumY = humY;
      prevPressY = pressY;
    }
  }
  
  display.setCursor(0, 50);
  display.print("T:");
  display.print(minTemp,0);
  display.print("-");
  display.print(maxTemp,0);
  display.print("C");
  
  display.setCursor(50, 50);
  display.print("H:");
  display.print(minHum,0);
  display.print("-");
  display.print(maxHum,0);
  display.print("%");
  
  display.setCursor(90, 50);
  display.print("P:");
  display.print(minPress,0);
  display.print("-");
  display.print(maxPress,0);
}