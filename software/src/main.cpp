////////////////////////////////////////////////////////////////////////
/// @file main.cpp
/// Main program
////////////////////////////// Libraries ///////////////////////////////

#include <Arduino.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <graph.h>
#include <snake.h>

//////////////////////////////// Macros ////////////////////////////////

#define LED_BUILTIN 22

///////////////////////////// Definitions //////////////////////////////

static uint8_t conv2d(const char* p);

//////////////////////////////// Types /////////////////////////////////

/**
 * @struct Time
 * @brief A structure representing time variables.
 *
 * This struct is used to store time variables.
 */
typedef struct {
    uint8_t hh=conv2d(__TIME__)
      , mm=conv2d(__TIME__+3)
      , ss=conv2d(__TIME__+6)
      ; // Get H, M, S from compile time
} Time ;

/**
 * @struct Adc
 * @brief A structure representing adc variables.
 *
 * This struct is used to store adc variables.
 */
typedef struct {
  float voltage = 0;
} Adc ;

/**
 * @struct Global
 * @brief A structure representing global variables.
 *
 * This struct is used to store inter-process variables.
 */
typedef struct {
  Time time;
  Adc adc;
} Global ;

/////////////////////////////// Globals ////////////////////////////////

TFT_eSPI tft = TFT_eSPI(); // invoke lib, pins defined in User_Setup.h
Global global; // global organization tree

////////////////////////////// Functions ///////////////////////////////

/**
 * @brief Converts a double digit string to byte value.
 *
 * @param p Constant string value.
 *
 * @return Returns uint8_t value, from 0-99.
 */
static uint8_t conv2d(const char* p) {
  uint8_t v = 0;
  if ('0' <= *p && *p <= '9')
    v = *p - '0';
  return 10 * v + *++p - '0';
}

//////////////////////////////// Tasks /////////////////////////////////

void blink_task(void *pvParameter) {
  pinMode(LED_BUILTIN, OUTPUT);  // Initialize GPIO 2 (usually onboard LED)
  while (true) {
    digitalWrite(LED_BUILTIN, HIGH); // LED on
    vTaskDelay(500 / portTICK_PERIOD_MS); // wait
    digitalWrite(LED_BUILTIN, LOW); // LED off
    vTaskDelay(500 / portTICK_PERIOD_MS); // wait
  }
}

void serial_task(void *pvParameter) {
  while (true) {
    Serial.println("FreeRTOS: Hello World!");
    vTaskDelay(1000 / portTICK_PERIOD_MS); // wait
  }
}

void screen_task(void *pvParameter) {
  int i = 0;
  const int color[]= { TFT_RED, TFT_GREEN, TFT_BLUE, };
  const char* msg[] = { "It is windy", "It is rainy", "It is cloudy" };

  while (true) {
    //tft.setCursor(8, 52); // setCursor(x,y)
    //tft.print(__DATE__); // This uses the standard ADAFruit small font
    tft.fillScreen(TFT_BLACK); // Fill screen black background
    tft.setTextColor(color[i], TFT_BLACK); // setTextColor(fg,bg)

    String str = "Voltage: ";
    str += String(global.adc.voltage).c_str();
    Serial.println(str.c_str());

    tft.drawCentreString(__DATE__,120,24,2); // drawCentreString(string,x,y,font_px_size)
    tft.drawCentreString(msg[i],120,48,2); // drawCentreString(string,x,y,font_px_size)
    tft.drawCentreString(str.c_str(),120,72,2); // drawCentreString(string,x,y,font_px_size)

    vTaskDelay(1000 / portTICK_PERIOD_MS); // wait

    i++;
    i = i == 3 ? 0 : i;
  }
}

void clock_task(void *pvParameter) {
  while (true) {
    global.time.ss++; // increment second
    if ( global.time.ss > 59 ) {
      global.time.ss = 0;
      global.time.mm++; // increment minute
      if( global.time.mm > 59 ) {
        global.time.mm = 0;
        global.time.hh++; // increment hour
        if( global.time.hh > 23 ) {
          global.time.hh = 0;
        }
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // wait
  }
}

void adc_task(void *pvParameter) {
  const int adc_pin = 32; // Note: Arduino.h uses GPIO pin numbers
  pinMode(adc_pin,INPUT);

  while (true) {
    int sensorValue = analogRead(adc_pin);
    global.adc.voltage = sensorValue * (3.3 / 4095.0);
    vTaskDelay(1 / portTICK_PERIOD_MS); // wait
  }
}

///////////////////////////////// Main /////////////////////////////////

void setup() {
  Serial.begin(115200); // Start serial communication
  tft.init(); // Initialize screen
  tft.setRotation(1); // Set screen rotation
  tft.fillScreen(TFT_BLACK); // Fill screen black background
  tft.setTextColor(TFT_YELLOW, TFT_BLACK); // setTextColor(fg,bg)

  // Create FreeRTOS tasks

  // Example tasks
  //xTaskCreate(blink_task, "Blink Task", 1024, NULL, 1, NULL);
  //xTaskCreate(serial_task, "Serial Task", 1024, NULL, 1, NULL);
  //xTaskCreate(screen_task, "Screen Task", 1024, NULL, 1, NULL);
  //xTaskCreate(clock_task, "Clock Task", 1024, NULL, 1, NULL);
  //xTaskCreate(adc_task, "ADC Task", 1024, NULL, 1, NULL);
  xTaskCreate(snake_task,"Snake Task",16384,NULL,1,NULL);

  // Sensor tasks
  //xTaskCreate(ui_task, "UI", 4096, NULL, 1, NULL);
  //xTaskCreate(sensor_task, "Sensor", 4096, NULL, 1, NULL);
}

void loop() {
  // Empty, as FreeRTOS is handling everything
}
