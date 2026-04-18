////////////////////////////////////////////////////////////////////////
/// @file graph_task.cpp
/// Voltage grapher
///////////////////////////// 1.Libraries //////////////////////////////

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <esp32-oscilloscope.h>
#include <driver/adc.h>
#include <driver/timer.h>
#include <hmiCore.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define GRID_COUNT_X 20
#define GRID_COUNT_Y 15
#define GRID_OFFSET_X (RESOLUTION_X / GRID_COUNT_X)
#define GRID_OFFSET_Y (RESOLUTION_Y / GRID_COUNT_Y)
#define REFRESH_RATE_MS 300
#define ADC_RESOLUTION 4096
#define BUF_LEN 10240

/////////////////////////////// 3.Types ////////////////////////////////

enum class State {
  TRIGGER,
  TRIGGER_SETTINGS,
  SCALE,
  POSITION,
  MEASURE
};

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////

static QueueHandle_t q = NULL;

//////////////////////////// 5.1.Variables /////////////////////////////

static uint16_t trigger_level = 512;
uint16_t samples[BUF_LEN];
size_t write_head = 0;
size_t read_head = 0;
uint16_t horizontal_zoom = 10;
uint16_t vertical_zoom = 10;
State state = State::TRIGGER;

//////////////////////////// 5.2.Functions /////////////////////////////

void add_sample(uint16_t v) {
	samples[write_head] = v;
	write_head = (write_head + 1) % BUF_LEN;
}

void adc_task(void *pvParameters) {
  // ADC1 (GPIO34)
  memset(samples, 64, sizeof(samples));
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_12);

	while (true) {
		uint16_t ch1 = adc1_get_raw(ADC1_CHANNEL_5);
		add_sample(ch1);
		vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void oscilloscope_deinit(){
  #ifdef DEBUG
  Serial.println("[ui_task]: self-deleting");
  #endif

  mutex_release();

  vTaskDelete(NULL); // self-delete
}

void trigger() {
	for (int i = RESOLUTION_X / 2; i < BUF_LEN; ++i) {
		size_t index = (write_head - 1 - i + BUF_LEN) % BUF_LEN;
		uint16_t value1 = samples[index];
		uint16_t value2 = samples[(index + 1) % BUF_LEN];
		if (trigger_level >= value1 && trigger_level < value2) {
			read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		} else if (trigger_level <= value1 && trigger_level > value2) {
			read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		}
	}
	read_head = (write_head - RESOLUTION_X + BUF_LEN) % BUF_LEN;
}

void draw_trigger() {
	tft.drawFastHLine(0, RESOLUTION_Y - trigger_level / 17 * vertical_zoom / 10, RESOLUTION_X, TFT_CYAN);
}

void draw_graph() {
	uint8_t previous_value = RESOLUTION_Y - samples[read_head] / (18 - vertical_zoom);
	for (size_t i = 1; i < RESOLUTION_X; i++) {
		size_t index = (read_head + i) % BUF_LEN * horizontal_zoom / 10;
		uint8_t value = RESOLUTION_Y - samples[index] / 17 * vertical_zoom / 10;
		tft.drawLine(i-1, previous_value, i, value, TFT_GREEN);
		previous_value = value;
	}
}

void draw_grid() {
	for (uint16_t i = GRID_OFFSET_X; i < RESOLUTION_X; i += GRID_OFFSET_X) {
		tft.drawFastVLine(i, 0, RESOLUTION_Y, TFT_DARKGREY);
	}
	for (uint16_t i = GRID_OFFSET_Y; i < RESOLUTION_Y; i += GRID_OFFSET_Y) {
		tft.drawFastHLine(0, i, RESOLUTION_X, TFT_DARKGREY);
	}
}

void button_logic() {
  hmiEventData_t data = getinputs(q);
  uint32_t& inputs = data.inputs;

  if( inputs & BTN_ESC ) oscilloscope_deinit();

  if ( inputs & BTN_SCALE ) {
    if (state == State::SCALE) {
      state = State::POSITION;
    } else {
      state = State::SCALE;
    }
  } else if ( inputs & BTN_TRIGGER ) {
    state = State::TRIGGER;
  } else if ( inputs & BTN_MEASURE ) {
    state = State::MEASURE;
  }

  switch (state) {
    case State::TRIGGER:
      if (inputs & BTN_UP) {
        trigger_level += 128;
        if (trigger_level > ADC_RESOLUTION)
          trigger_level = ADC_RESOLUTION;
      } else if (inputs & BTN_DOWN) {
        if (trigger_level < 128)
          trigger_level = 0;
        else
          trigger_level -= 128;
      } else if (inputs & BTN_SCALE) {
          state = State::SCALE;
      }
      break;

    case State::SCALE:
      if (inputs & BTN_UP) {
        vertical_zoom++;
      } else if (inputs & BTN_DOWN) {
        if (vertical_zoom != 0) {
          vertical_zoom--;
        }
      } else if (inputs & BTN_RIGHT) {
        horizontal_zoom++;
      } else if (inputs & BTN_LEFT) {
        if (horizontal_zoom != 0) {
          horizontal_zoom--;
        }
      }
      break;

    default:
        break;
  }

  Serial.println((int)state);
}

void ui_task(void *pvParameters) {
  q = *(QueueHandle_t*)pvParameters;

	xTaskCreate(adc_task
              ,"ADC"
              ,1024
              ,NULL
              ,1
              ,NULL
              );

	while (true) {

    if(mutex_take()) {

      while(true) {
        button_logic();
        tft.fillScreen(TFT_BLACK);
        trigger();
        draw_grid();
        draw_graph();
        draw_trigger();
        vTaskDelay(pdMS_TO_TICKS(100));
      }

    }

	}
}
