////////////////////////////////////////////////////////////////////////
/// @file graph_task.cpp
/// Voltage grapher
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <driver/timer.h>
#include <hmiCore.h>
#include "afeCore.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define GRID_COUNT_X 20
#define GRID_COUNT_Y 15
#define GRID_OFFSET_X (RESOLUTION_X / GRID_COUNT_X)
#define GRID_OFFSET_Y (RESOLUTION_Y / GRID_COUNT_Y)
#define REFRESH_RATE_MS 300
#define ADC_RESOLUTION 4096
#define BUF_LEN 10240
#define ZERO_VOLTS 2048.0
#define MAX_VOLTS 16.0
#define TIMESTEP_MS 1
#define STR_LEN 16

/////////////////////////////// 3.Types ////////////////////////////////

enum class State {
  TRIGGER,
  TRIGGER_SETTINGS,
  SCALE,
  POSITION,
  MEASURE,
  CURSOR,
  AUTOSET,
  RUN,
  STOP,
  MATH
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

enum class Channel {
  CH1,
  CH2
};

typedef struct {
  Channel ch_selected = Channel::CH1;
  bool ch1_active = 1;
  bool ch2_active = 1;
} ChannelState;

typedef struct {
  uint16_t ch1_level;
  uint16_t ch2_level;
} TriggerLevel;

typedef struct {
  uint16_t x;
  uint16_t y;
  boolean en;
} Cursor;

typedef struct {
  char x[GRID_COUNT_X-1][STR_LEN];
  char y[GRID_COUNT_Y-1][STR_LEN];
} GridValues;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////

static QueueHandle_t q = NULL;

TaskHandle_t adc;

//////////////////////////// 5.1.Variables /////////////////////////////

RingBuffer rb_ch1{};
RingBuffer rb_ch2{};

ChannelState ch_states{};

ViewState ch1_view = {
  .x_zoom = 2.0,
  .y_zoom = 1.0,
  .x_offset = 0,
  .y_offset = 1710
};

ViewState ch2_view = {
  .x_zoom = 2.0,
  .y_zoom = 1.0,
  .x_offset = 0,
  .y_offset = 1710
};

TriggerLevel triggers = {
  .ch1_level = 1760,
  .ch2_level = 1765
};

Cursor cursor_1 = {
  .x = RESOLUTION_X / 2,
  .y = RESOLUTION_Y / 2,
  .en = false
};

GridValues grid_values{};

State state = State::TRIGGER;

//////////////////////////// 5.2.Functions /////////////////////////////

void add_sample(uint16_t val, RingBuffer *rb) {
	rb->samples[rb->write_head] = val;
	rb->write_head = (rb->write_head + 1) % BUF_LEN;
}

void adc_task(void *pvParameters) {
	while (true) {
    if (ch_states.ch1_active) {
      int ch1_reading;
      adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_1 ), ADC_CHANNEL_8, &ch1_reading );
      if( afeCore_isChannel1Disabled() ){ add_sample(0, &rb_ch1); }
      else{ add_sample((uint16_t)ch1_reading, &rb_ch1); }
    }
    if (ch_states.ch2_active) {
      int ch2_reading; 
      adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_2 ), ADC_CHANNEL_5, &ch2_reading );
      add_sample((uint16_t)ch2_reading, &rb_ch2);
    }

		vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void oscilloscope_deinit(){
  #ifdef DEBUG
  Serial.println("[oscilloscope_task]: self-deleting");
  #endif

  mutex_release();

  vTaskDelete(adc);
  vTaskDelete(NULL); // self-delete
}

float get_voltage(int y, ViewState view) {
  if (view.y_zoom == 0.0f)
    return 0;

  float adjusted = (float)(RESOLUTION_Y - y);
  float val = (adjusted / view.y_zoom) + view.y_offset;

  if (val < 0.0f) val = 0.0f;
  if (val > 65535.0f) val = 65535.0f;

  float voltage = MAX_VOLTS*((val-ZERO_VOLTS)/ZERO_VOLTS);
  return voltage;
}

void autoset(ViewState view) {
  
}

void trigger(RingBuffer *rb, uint16_t trigger_level) {
	for (int i = RESOLUTION_X / 2; i < BUF_LEN; ++i) {
		size_t index = (rb->write_head - 1 - i + BUF_LEN) % BUF_LEN;
		uint16_t value1 = rb->samples[index];
		uint16_t value2 = rb->samples[(index + 1) % BUF_LEN];
		if (trigger_level >= value1 && trigger_level < value2) {
			rb->read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		} else if (trigger_level <= value1 && trigger_level > value2) {
			rb->read_head = (index - RESOLUTION_X / 2 + BUF_LEN) % BUF_LEN;
			return;
		}
	}
	rb->read_head = (rb->write_head - RESOLUTION_X + BUF_LEN) % BUF_LEN;
}

void draw_trigger(uint16_t trigger_level, ViewState view, uint32_t color) {
  float adjusted = (trigger_level - view.y_offset) * view.y_zoom;

  int y = RESOLUTION_Y / 2 - (int)adjusted;
	tft.drawFastHLine(0, y, RESOLUTION_X, color);
}

void draw_cursor(uint32_t color) {
  if (cursor_1.en) {
    tft.drawFastHLine(0, RESOLUTION_Y - cursor_1.y, RESOLUTION_X, color);
    tft.drawFastVLine(cursor_1.x, 0, RESOLUTION_Y, color);
  }
}

void draw_graph(RingBuffer *rb, ViewState view, int32_t color) {
  uint32_t head_snapshot = rb->read_head;

  int prev_x = 0;
  int prev_y = 0;

  for (int i = 0; i < RESOLUTION_X; i++) {

    int32_t sample_index =
      head_snapshot
      - view.x_offset
      - (int32_t)(i * view.x_zoom);

    while (sample_index < 0)
      sample_index += BUF_LEN;

    sample_index %= BUF_LEN;

    uint16_t val = rb->samples[sample_index];

    float adjusted = (val - view.y_offset) * view.y_zoom;

    int y = RESOLUTION_Y / 2 - (int)adjusted;

    if (y < 0) y = 0;
    if (y >= RESOLUTION_Y) y = RESOLUTION_Y - 1;

    if (i > 0) {
      tft.drawLine(RESOLUTION_X - prev_x, prev_y, RESOLUTION_X - i, y, color);
    }

    prev_x = i;
    prev_y = y;
  }
}

void update_grid() {
  ViewState view;
  if (ch_states.ch_selected == Channel::CH1) {
    view = ch1_view;
  } else {
    view = ch2_view;
  }
	for (uint16_t i = GRID_OFFSET_X; i < RESOLUTION_X; i += GRID_OFFSET_X) {
		
	}
	for (uint16_t i = 0; i < GRID_COUNT_Y-1; i++) {
    uint16_t y = (i+1) * GRID_OFFSET_Y;
    float val = get_voltage(y, view);
    int int_part = (int)val;
    int frac_part = abs((int)((val - int_part) * 100));
    snprintf(grid_values.y[i], STR_LEN, "%d.%02d", int_part, frac_part);
	}
}

void draw_grid(ViewState view) {
	for (uint16_t i = GRID_OFFSET_X; i < RESOLUTION_X; i += GRID_OFFSET_X) {
		tft.drawFastVLine(i, 0, RESOLUTION_Y, TFT_DARKGREY);
	}
	for (uint16_t i = 0; i < GRID_COUNT_Y-1; i++) {
    uint16_t y = (i+1)*GRID_OFFSET_Y;
		tft.drawFastHLine(0, y, RESOLUTION_X, TFT_DARKGREY);
    tft.setTextSize(TFT_SMALL);
    tft.drawString(grid_values.y[i], 10, y-3);
	}
}

void button_logic(uint16_t *trigger_level, ViewState *view) {
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
  } else if ( inputs & BTN_CURSORS ) {
    if (state == State::CURSOR) {
      cursor_1.en = !cursor_1.en;
      state = State::POSITION;
    } else {
      cursor_1.en = true;
      state = State::CURSOR;
    }
  } else if ( inputs & BTN_CH1 ) {
    if (ch_states.ch_selected == Channel::CH2) {
      ch_states.ch1_active = true;
      ch_states.ch_selected = Channel::CH1;
    } else {
      ch_states.ch1_active = false;
      ch_states.ch_selected = Channel::CH2;
    }
    update_grid();
  } else if ( inputs & BTN_CH2 ) {
    if (ch_states.ch_selected == Channel::CH1) {
      ch_states.ch2_active = true;
      ch_states.ch_selected = Channel::CH2;
    } else {
      ch_states.ch2_active = false;
      ch_states.ch_selected = Channel::CH1;
    }
    update_grid();
  }

  switch (state) {
    case State::TRIGGER:
      if (inputs & BTN_UP) {
        *trigger_level += 32;
        if (*trigger_level > ADC_RESOLUTION)
          *trigger_level = ADC_RESOLUTION;
      } else if (inputs & BTN_DOWN) {
        if (*trigger_level < 32)
          *trigger_level = 0;
        else
          *trigger_level -= 32;
      } else if (inputs & BTN_SCALE) {
          state = State::SCALE;
      }
      break;

    case State::SCALE:
      if (inputs & BTN_UP) {
        view->y_zoom *= 1.1;
        update_grid();
      } else if (inputs & BTN_DOWN) {
        view->y_zoom *= 0.9;
        update_grid();
      } else if (inputs & BTN_RIGHT) {
        view->x_zoom *= 1.1;
        update_grid();
      } else if (inputs & BTN_LEFT) {
        view->x_zoom *= 0.9;
        update_grid();
      }
      break;

    case State::POSITION:
      if (inputs & BTN_UP) {
        view->y_offset += 50;
        update_grid();
      } else if (inputs & BTN_DOWN) {
        view->y_offset -= 50;
        update_grid();
      } else if (inputs & BTN_RIGHT) {
        view->x_offset += 50;
        update_grid();
      } else if (inputs & BTN_LEFT) {
        view->x_offset -= 50;
        update_grid();
      }
      break;

    case State::CURSOR:
      if (inputs & BTN_UP) {
        cursor_1.y += 5;
      } else if (inputs & BTN_DOWN) {
        cursor_1.y -= 5;
      } else if (inputs & BTN_RIGHT) {
        cursor_1.x += 5;
      } else if (inputs & BTN_LEFT) {
        cursor_1.x -= 5;
      }
      break;

    default:
        break;
  }

  if (view->x_zoom < 0.1) view->x_zoom = 0.1;
  if (view->x_zoom > 20)  view->x_zoom = 20;

  if (view->y_zoom < 0.1) view->y_zoom = 0.1;
  if (view->y_zoom > 20)  view->y_zoom = 20;

  Serial.println((int)state);
}

void oscilloscope_task(void *pvParameters) {
  
  q = *(QueueHandle_t*)pvParameters;
 
  while( !mutex_take() ) { DELAY(100); }

  update_grid();
  
  tft.fillScreen(TFT_BLACK);
//  spr.createSprite(RESOLUTION_X, RESOLUTION_Y);

	xTaskCreate( adc_task, "ADC", 4096, NULL, 1, &adc );

    while(true) {
      if (ch_states.ch_selected == Channel::CH1) {
        button_logic(&triggers.ch1_level, &ch1_view);
        draw_grid(ch1_view);
      } else {
        button_logic(&triggers.ch2_level, &ch2_view);
        draw_grid(ch2_view);
      }
      tft.fillScreen(TFT_BLACK);
//        spr.fillSprite(TFT_BLACK);
    if (ch_states.ch1_active) {
      trigger(&rb_ch1, triggers.ch1_level);
      draw_graph(&rb_ch1, ch1_view, TFT_GREEN);
      draw_trigger(triggers.ch1_level, ch1_view, TFT_CYAN);
    }
    if (ch_states.ch2_active) {
      trigger(&rb_ch2, triggers.ch2_level);
      draw_graph(&rb_ch2, ch2_view, TFT_YELLOW);
      draw_trigger(triggers.ch2_level, ch2_view, TFT_MAGENTA);
    }
    draw_cursor(TFT_SKYBLUE);
//        spr.pushSprite(RESOLUTION_X, RESOLUTION_Y);
        vTaskDelay(pdMS_TO_TICKS(100));
      }

}

/*
Todo:
Autoset
Run/stop
Measure
Cursors
Trigger settings
Math
Time values for x-axis
*/