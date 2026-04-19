////////////////////////////////////////////////////////////////////////
/// @file graph_task.cpp
/// Voltage grapher
///////////////////////////// 1.Libraries //////////////////////////////

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

typedef struct {
  uint16_t samples[BUF_LEN];
  size_t write_head = 0;
  size_t read_head = 0;
} RingBuffer;

typedef struct {
  float x_zoom;
  float y_zoom;

  int32_t x_offset;
  int32_t y_offset;
} ViewState;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////

static QueueHandle_t q = NULL;

//////////////////////////// 5.1.Variables /////////////////////////////

RingBuffer rb{};

ViewState view = {
  .x_zoom = 2.0,
  .y_zoom = 1.0,
  .x_offset = 0,
  .y_offset = 1710
};
static uint16_t trigger_level = 1760;
State state = State::TRIGGER;

//////////////////////////// 5.2.Functions /////////////////////////////

void add_sample(uint16_t v) {
	rb.samples[rb.write_head] = v;
	rb.write_head = (rb.write_head + 1) % BUF_LEN;
}

void adc_task(void *pvParameters) {
  // ADC1 (GPIO34)
//  memset(rb.samples, 64, sizeof(rb.samples));
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
		size_t index = (rb.write_head - 1 - i + BUF_LEN) % BUF_LEN;
		uint16_t value1 = rb.samples[index];
		uint16_t value2 = rb.samples[(index + 1) % BUF_LEN];
		if (trigger_level >= value1 && trigger_level < value2) {
			rb.read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		} else if (trigger_level <= value1 && trigger_level > value2) {
			rb.read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		}
	}
	rb.read_head = (rb.write_head - RESOLUTION_X + BUF_LEN) % BUF_LEN;
}

void draw_trigger() {
  float adjusted = (trigger_level - view.y_offset) * view.y_zoom;

  int y = RESOLUTION_Y / 2 - (int)adjusted;
	tft.drawFastHLine(0, y, RESOLUTION_X, TFT_CYAN);
}

void draw_graph() {
  uint32_t head_snapshot = rb.read_head;

  int prev_x = 0;
  int prev_y = 0;

  for (int x = 0; x < RESOLUTION_X; x++) {

    int32_t sample_index =
      head_snapshot
      - view.x_offset
      - (int32_t)(x * view.x_zoom);

    while (sample_index < 0)
      sample_index += BUF_LEN;

    sample_index %= BUF_LEN;

    uint16_t val = rb.samples[sample_index];

    float adjusted = (val - view.y_offset) * view.y_zoom;

    int y = RESOLUTION_Y / 2 - (int)adjusted;

    if (y < 0) y = 0;
    if (y >= RESOLUTION_Y) y = RESOLUTION_Y - 1;

    if (x > 0) {
      tft.drawLine(prev_x, prev_y, x, y, TFT_GREEN);
    }

    prev_x = x;
    prev_y = y;
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

  if ( inputs & BTN_ESC ) oscilloscope_deinit();

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
        trigger_level += 32;
        if (trigger_level > ADC_RESOLUTION)
          trigger_level = ADC_RESOLUTION;
      } else if (inputs & BTN_DOWN) {
        if (trigger_level < 32)
          trigger_level = 0;
        else
          trigger_level -= 32;
      } else if (inputs & BTN_SCALE) {
          state = State::SCALE;
      }
      break;

    case State::SCALE:
      if (inputs & BTN_UP) {
        view.y_zoom *= 1.1;
      } else if (inputs & BTN_DOWN) {
        view.y_zoom *= 0.9;
      } else if (inputs & BTN_RIGHT) {
        view.x_zoom *= 1.1;
      } else if (inputs & BTN_LEFT) {
        view.x_zoom *= 0.9;
      }
      break;

    case State::POSITION:
      if (inputs & BTN_UP) {
        view.y_offset += 50;
      } else if (inputs & BTN_DOWN) {
        view.y_offset -= 50;
      } else if (inputs & BTN_RIGHT) {
        view.x_offset += 50;
      } else if (inputs & BTN_LEFT) {
        view.x_offset -= 50;
      }
      break;

    default:
        break;
  }

  if (view.x_zoom < 0.1) view.x_zoom = 0.1;
  if (view.x_zoom > 20)  view.x_zoom = 20;

  if (view.y_zoom < 0.1) view.y_zoom = 0.1;
  if (view.y_zoom > 20)  view.y_zoom = 20;

  Serial.println((int)state);
}

void ui_task(void *pvParameters) {
  q = *(QueueHandle_t*)pvParameters;

	xTaskCreate(adc_task, "ADC", 1024, NULL, 1, NULL);

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
