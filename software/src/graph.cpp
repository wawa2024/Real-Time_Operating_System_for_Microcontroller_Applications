////////////////////////////////////////////////////////////////////////
/// @file graph.cpp
/// Voltage grapher
///////////////////////////// 1.Libraries //////////////////////////////

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define LED_BUILTIN 22
#define BUTTON_DOWN 0
#define BUTTON_UP 35
#define RESOLUTION_X 256
#define RESOLUTION_Y 128
#define BUF_LEN 2560

///////////////////////////// 3.Types //////////////////////////////

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////

extern TFT_eSPI tft;

//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static uint16_t trigger_level = 3584;
uint16_t samples[BUF_LEN];
size_t write_head = 0;
size_t read_head = 0;

//////////////////////////// 5.2.Functions /////////////////////////////


void trigger() {
	for (int i = RESOLUTION_X / 2; i < BUF_LEN; ++i) {
		size_t index = (write_head - 1 - i + BUF_LEN) % BUF_LEN;
		uint16_t value1 = samples[index];
		uint16_t value2 = samples[(index + 1) % BUF_LEN];
		if (trigger_level >= value1 && trigger_level < value2) {
			read_head = index - RESOLUTION_X / 2 + BUF_LEN % BUF_LEN;
			return;
		} else if (trigger_level <=value1 && trigger_level > value2) {
			read_head = index - RESOLUTION_X / 2 + BUF_LEN % BUF_LEN;
			return;
		}
	}
	read_head = (write_head - RESOLUTION_X + BUF_LEN) % BUF_LEN;
}

void draw_trigger() {
	tft.drawFastHLine(0, trigger_level / 32, 256, TFT_CYAN);
}

void draw_graph() {
	uint8_t previous_value = RESOLUTION_Y - samples[read_head] / 32;
	for (size_t i = 1; i < RESOLUTION_X; i++) {
		size_t index = (read_head + i) % BUF_LEN;
		uint8_t value = RESOLUTION_Y - samples[index] / 32;
		tft.drawLine(i-1, previous_value, i, value, TFT_GREEN);
		previous_value = value;
	}
}

void draw_grid() {
	for (uint8_t i = 1; i < 16; i++) {
		tft.drawFastVLine(i*16, 0, RESOLUTION_Y, TFT_DARKGREY);
	}
	for (uint8_t i = 1; i < 8; i++) {
		tft.drawFastHLine(0, i*16, RESOLUTION_X, TFT_DARKGREY);
	}
}

void ui_task(void *pvParameters) {
	uint8_t counter = 0;
	while (1) {
		tft.fillScreen(TFT_BLACK);
		trigger();
		draw_grid();
		draw_graph();
		draw_trigger();
		counter++;
		if (counter >= 30) {
			trigger_level = random(0, 4096);
			counter = 0;
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void add_sample(uint16_t v) {
	samples[write_head] = v;
	write_head = (write_head + 1) % BUF_LEN;
}

void sensor_generator(uint16_t& sensor_reading, bool& adding) {
	uint16_t incrementer = random(20, 100);
	if (random(0,10) > 8) {
		incrementer = incrementer * 3;
	}
	uint16_t temp = sensor_reading + incrementer;
	if (adding) {
		temp = sensor_reading + incrementer;
		if (temp > 3396) {
			adding = false;
		}
	} else if (!adding) {
		temp = sensor_reading - incrementer;
		if (temp < 600) {
			adding = true;
		}
	}
	sensor_reading = temp;
}

void sensor_task(void *pvParameters) {
    memset(samples, 64, sizeof(samples));
	uint16_t sensor_reading = 2048;
	bool adding = true;
	while(1) {
		sensor_generator(sensor_reading, adding);
		add_sample(sensor_reading);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
