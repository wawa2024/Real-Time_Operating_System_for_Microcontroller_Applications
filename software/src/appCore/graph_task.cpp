////////////////////////////////////////////////////////////////////////
/// @file graph_task.cpp
/// Voltage grapher
///////////////////////////// 1.Libraries //////////////////////////////

#include <graph_task.h>

#include <esp32-oscilloscope.h>
#include <hmiCore.h>
#include "afeCore.h"
#include <cstring>
#include <stdio.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define GRID_COUNT_X 20
#define GRID_COUNT_Y 15
#define GRID_OFFSET_X (RESOLUTION_X / GRID_COUNT_X) // 16
#define GRID_OFFSET_Y (RESOLUTION_Y / GRID_COUNT_Y) // 16
#define REFRESH_RATE_MS 100
#define ADC_RESOLUTION 4096
#define MAX_VOLTS 16.0
#define TIMESTEP_MS 1
#define STR_LEN 20
#define CH1_BUF_SIZE 1180
#define CH2_BUF_SIZE (CH1_BUF_SIZE * 20)

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
  float x_zoom;
  int32_t x_offset;
} Timebase;

typedef struct {
  float y_zoom;
  int32_t y_offset;
  uint8_t x_multiplier;
} ViewState;

typedef struct {
  uint16_t *samples;
  uint32_t buffer_size;
  size_t write_head = 0;
  size_t read_head = 0;
} RingBuffer;

typedef struct {
  bool stop = false;
  afeChannel_t ch_selected = CHANNEL_1;
  bool ch1_active = true;
  bool ch2_active = true;
  int32_t ch1_color = TFT_YELLOW;
  int32_t ch2_color = TFT_GREEN;
} ChannelState;

typedef struct {
  uint16_t level;
  afeTrigMode_t mode;
  afeTrigType_t type;
  afeChannel_t selected_channel;
  bool is_triggered;
} Trigger;

typedef struct {
  uint16_t x;
  uint16_t y;
  boolean en;
} Cursor;

typedef struct {
  char x[GRID_COUNT_X-1][STR_LEN];
  char y[GRID_COUNT_Y-1][STR_LEN];
} GridValues;

typedef struct {
  char ch1[4];
  char ch2[4];
  char current_state[13];
} UiText;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////

constexpr const char* state_names[] = {
  "trigger",
  "trigsettings",
  "scale",
  "position",
  "measure",
  "cursor",
  "autoset",
  "run",
  "stop",
  "math"
};

//////////////////////////// 5.Definitions /////////////////////////////

static QueueHandle_t q = NULL;

TaskHandle_t adc;

//////////////////////////// 5.1.Variables /////////////////////////////

Timebase timebase = {
  .x_zoom = 2.0,
  .x_offset = 0
};

ViewState ch1_view = {
  .y_zoom = 1.0,
  .y_offset = 2385,
  .x_multiplier = 1
};

ViewState ch2_view = {
  .y_zoom = 1.0,
  .y_offset = 2385,
  .x_multiplier = 20
};

uint16_t rb_ch1_storage[CH1_BUF_SIZE];
uint16_t rb_ch2_storage[CH2_BUF_SIZE];

RingBuffer rb_ch1{
  .samples = rb_ch1_storage,
  .buffer_size = CH1_BUF_SIZE,
  .write_head = 0,
  .read_head = 0
};
RingBuffer rb_ch2{
  .samples = rb_ch2_storage,
  .buffer_size = CH2_BUF_SIZE,
  .write_head = 0,
  .read_head = 0
};

uint16_t copy_rb_ch1_storage[CH1_BUF_SIZE];
uint16_t copy_rb_ch2_storage[CH2_BUF_SIZE];

RingBuffer copy_rb_ch1{
  .samples = copy_rb_ch1_storage,
  .buffer_size = CH1_BUF_SIZE,
  .write_head = 0,
  .read_head = 0
};
RingBuffer copy_rb_ch2{
  .samples = copy_rb_ch2_storage,
  .buffer_size = CH2_BUF_SIZE,
  .write_head = 0,
  .read_head = 0
};

ChannelState ch_states{};

Trigger trigger = {
  .level = 2335,
  .mode = NO_TRIGGER,
  .type = RISING_EDGE_TRIGGER,
  .selected_channel = CHANNEL_1,
  .is_triggered = false
};

Cursor cursor_1 = {
  .x = RESOLUTION_X / 2,
  .y = RESOLUTION_Y / 2,
  .en = false
};

GridValues grid_values{};

State state = State::POSITION;

UiText ui_text = {
  .ch1 = "CH1",
  .ch2 = "CH2",
  .current_state = "position"
};

//////////////////////////// 5.2.Functions /////////////////////////////

void add_sample( uint16_t val, afeChannel_t ch ) {

  RingBuffer *ch1_ptr = ch_states.stop ? &copy_rb_ch1 : &rb_ch1;
  RingBuffer *ch2_ptr = ch_states.stop ? &copy_rb_ch2 : &rb_ch2;

  RingBuffer *rb = ch == CHANNEL_1 ? ch1_ptr : ch2_ptr;

	rb->samples[rb->write_head] = ADC_RESOLUTION - 1 - val;
	rb->write_head = (rb->write_head + 1) % rb->buffer_size;
}

void adc_task(void *pvParameters) {
  TickType_t xLastWakeTime;
  const TickType_t xPeriod = pdMS_TO_TICKS(1);
  xLastWakeTime = xTaskGetTickCount();

//  static const uint32_t TEMP_BUFF_SIZE = 2000;
//  static uint16_t ch1_tempBuffer[TEMP_BUFF_SIZE] = {};
//  static uint16_t ch2_tempBuffer[TEMP_BUFF_SIZE] = {};


	while (true) {
    
    // afeCore_getNewestSamples( ch1_tempBuffer, ch2_tempBuffer, TEMP_BUFF_SIZE );

    if( ch_states.ch1_active ) 
    {
      int ch1_reading;
      
      if ( afeCore_isChannel1Disabled() )
      {
        //for( uint32_t i = 0; i < TEMP_BUFF_SIZE; i++ )
        //{ 
          //add_sample( 0, CHANNEL_1 ); 
        //}
        add_sample(0, CHANNEL_1);
      } 
      else 
      {
        //for( uint32_t i = 0; i < TEMP_BUFF_SIZE; i++ )
        //{ 
          //add_sample( ch1_tempBuffer[i], CHANNEL_1 ); 
        //}
        adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_1 ), ADC_CHANNEL_8, &ch1_reading );
        add_sample((uint16_t)ch1_reading, CHANNEL_1);
      } 
    }

    // if( ch_states.ch2_active ) 
    // {
    //   for( uint32_t i = 0; i < TEMP_BUFF_SIZE; i++ )
    //   { 
    //     add_sample( ch2_tempBuffer[i], CHANNEL_2 ); 
    //   }
    //   //int ch2_reading; 
    //   //adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_2 ), ADC_CHANNEL_5, &ch2_reading );
    //   //add_sample((uint16_t)ch2_reading, ch2_ptr);
    // }

		vTaskDelayUntil(&xLastWakeTime, xPeriod);
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

float get_voltage(uint16_t y, afeChannel_t ch) {
  ViewState *view_ptr = (ch == CHANNEL_1) ? &ch1_view : &ch2_view;
  if (view_ptr->y_zoom == 0.0f)
    return 0;

  float adjusted = (float)(RESOLUTION_Y / 2 - y);
  float val = (adjusted / view_ptr->y_zoom) + view_ptr->y_offset;

  if (val < 0.0f) val = 0.0f;
  if (val > 65535.0f) val = 65535.0f;

  uint16_t adc_val = ADC_RESOLUTION - (int16_t)val - 1;
  float voltage = afeCore_sample2VoltageCal(adc_val, ch);
  return voltage;
}

void float_to_string(float val, char *out, size_t out_len) {
  int sign = (val < 0.0f) ? -1 : 1;
  float abs_val = val * sign;

  int int_part = (int)abs_val;
  int frac_part = (int)((abs_val - int_part) * 100.0f + 0.5f);

  if (frac_part >= 100) {
    frac_part = 0;
    int_part += 1;
  }

  if (sign < 0) {
    snprintf(out, out_len, "-%d.%02dV", int_part, frac_part);
  } else {
    snprintf(out, out_len, "%d.%02dV", int_part, frac_part);
  }
}

void apply_autoset(RingBuffer *rb, ViewState *view) {
  uint16_t min_val = rb->samples[0];
  uint16_t max_val = rb->samples[0];
  uint16_t val_delta = max_val - min_val;

  for (size_t i = 1; i < rb->buffer_size; ++i) {
    if (rb->samples[i] < min_val) {
      min_val = rb->samples[i];
    }
    if (rb->samples[i] > max_val) {
      max_val = rb->samples[i];
    }
  }
  view->y_offset = min_val + (val_delta) / 2;
  if (val_delta == 0) {
    view->y_zoom = RESOLUTION_Y * 1.1;
  } else {
    view->y_zoom = RESOLUTION_Y / (val_delta) * 1.1;
  }

  uint32_t sum = 0;
  for (size_t i = 0; i < rb->buffer_size; i++) {
    sum += rb->samples[i];
  }
  uint32_t mean = sum / (rb->buffer_size + 0.5f);

  uint32_t crossings[100];
  uint32_t count = 0;

  for (size_t i = 1; i < rb->buffer_size; i++) {
    if (rb->samples[i-1] < mean - 5 && rb->samples[i] >= mean + 5) {
      if (count < 100) crossings[count++] = i;
    }
  }

  float avg_period = 0;

  if (count > 1) {
    uint32_t total = 0;
    for (int i = 1; i < count; i++) {
      total += (crossings[i] - crossings[i-1]);
    }
    avg_period = (float)total / (count - 1);
  }

  float desired_cycles = 3.0f;
  float samples_per_screen = avg_period * desired_cycles;
  
  timebase.x_zoom = (uint16_t)(samples_per_screen / (float)RESOLUTION_X + 0.5f);

  timebase.x_offset = 0;
}

void autoset() {
  if (ch_states.ch_selected == CHANNEL_1 && ch_states.ch1_active) {
    apply_autoset(&rb_ch1, &ch1_view);
  }
  if (ch_states.ch_selected == CHANNEL_2 && ch_states.ch2_active) {
    apply_autoset(&rb_ch2, &ch2_view);
  }
}

void ringbuffer_copy(RingBuffer *dst, const RingBuffer *src) {
  dst->buffer_size = src->buffer_size;
  dst->write_head = src->write_head;
  dst->read_head  = src->read_head;

  memcpy(dst->samples, src->samples,
         src->buffer_size * sizeof(uint16_t));
}

void run() {
  ch_states.stop = false;
  ringbuffer_copy(&rb_ch1, &copy_rb_ch1);
  ringbuffer_copy(&rb_ch2, &copy_rb_ch2);
}

void stop() {
  ch_states.stop = true;
  ringbuffer_copy(&copy_rb_ch1, &rb_ch1);
  ringbuffer_copy(&copy_rb_ch2, &rb_ch2);
}

void toggle_run_stop() {
  if (ch_states.stop == true) {
    run();
  } else {
    stop();
  }
}

void set_read_heads() {
  trigger.is_triggered = false;
  rb_ch1.read_head = (rb_ch1.write_head - RESOLUTION_X + rb_ch1.buffer_size) % rb_ch1.buffer_size;
  rb_ch2.read_head = (rb_ch2.write_head - RESOLUTION_X + rb_ch2.buffer_size) % rb_ch2.buffer_size;
}

void trigger_logic() {
  if (trigger.mode == NO_TRIGGER) {
    trigger.is_triggered = false;
    set_read_heads();
    return;
  }

  RingBuffer* rb = (trigger.selected_channel == CHANNEL_1) ? &rb_ch1 : &rb_ch2;
  ViewState* view = (trigger.selected_channel == CHANNEL_1) ? &ch1_view : &ch2_view;
  bool trigger_activated = false;
  int start_value = RESOLUTION_X / 2 * timebase.x_zoom * view->x_multiplier + 0.5f;

	for (size_t i = start_value; i < rb->buffer_size + start_value; ++i) {
		size_t index = (rb->write_head - i + 2 * rb->buffer_size) % rb->buffer_size;
		uint16_t value1 = rb->samples[index];
		uint16_t value2 = rb->samples[(index + 1) % rb->buffer_size];
    uint16_t ch1_read_head_index;
    uint16_t ch2_read_head_index;
    if (trigger.selected_channel == CHANNEL_1) {
      ch1_read_head_index = (index + start_value) % rb_ch1.buffer_size;
      ch2_read_head_index = ((index + start_value) * ch2_view.x_multiplier) % rb_ch2.buffer_size;
    } else {
      ch1_read_head_index = (uint16_t)((index + start_value) / ch2_view.x_multiplier + 0.5f) % rb_ch1.buffer_size;
      ch2_read_head_index = (index + start_value) % rb_ch2.buffer_size;

    }
		if (trigger.level >= value1 && trigger.level < value2) {
      trigger.is_triggered = true;
      trigger_activated = true;
			rb_ch1.read_head = ch1_read_head_index;
      rb_ch2.read_head = ch2_read_head_index;
			return;
		} else if (trigger.level <= value1 && trigger.level > value2) {
      trigger.is_triggered = true;
      trigger_activated = true;
			rb_ch1.read_head = ch1_read_head_index;
      rb_ch2.read_head = ch2_read_head_index;
			return;
		}
	}
  if (!trigger_activated) {
    trigger.is_triggered = false;
    set_read_heads();
  }
}

void draw_trigger(afeChannel_t ch, uint32_t color) {
  ViewState *view_ptr = (ch == CHANNEL_1) ? &ch1_view : &ch2_view;
//  RingBuffer *rb_ptr = (ch == CHANNEL_1) ? &rb_ch1 : &rb_ch2;

  if (trigger.mode == NO_TRIGGER) return;

  float adjusted = (trigger.level - view_ptr->y_offset) * view_ptr->y_zoom;

  int y = RESOLUTION_Y / 2 - (int)adjusted;
	tft.drawFastHLine(0, y, RESOLUTION_X, color);

  if (trigger.is_triggered) {

  }
}

void draw_cursor(uint32_t color) {
  if (cursor_1.en) {
    tft.drawFastHLine(0, RESOLUTION_Y - cursor_1.y, RESOLUTION_X, color);
    tft.drawFastVLine(cursor_1.x, 0, RESOLUTION_Y, color);
    float val = get_voltage(RESOLUTION_Y - cursor_1.y, CHANNEL_1);
    char voltage[STR_LEN];
    float_to_string(val, voltage, STR_LEN);
    tft.setTextSize(TFT_SMALL);
    tft.drawString(voltage, 230, 20);
  }
}

void draw_graph(RingBuffer *rb, ViewState view, int32_t color) {
  uint32_t head_snapshot = rb->read_head;

  int prev_x = 0;
  int prev_y = 0;

  for (int i = 0; i < RESOLUTION_X; i++) {

    int32_t sample_index =
      head_snapshot
      - timebase.x_offset
      - (int32_t)(i * timebase.x_zoom * view.x_multiplier);

    while (sample_index < 0)
      sample_index += rb->buffer_size;

    sample_index %= rb->buffer_size;

    uint16_t val = rb->samples[sample_index];

    float adjusted = (val - view.y_offset) * view.y_zoom + 0.5f;

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
	for (uint16_t i = GRID_OFFSET_X; i < RESOLUTION_X; i += GRID_OFFSET_X) {
		
	}
	for (uint16_t i = 0; i < GRID_COUNT_Y-1; i++) {
    uint16_t y = (i+1) * GRID_OFFSET_Y;
    float val = get_voltage(y, ch_states.ch_selected);
    float_to_string(val, grid_values.y[i], STR_LEN);
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

void draw_ui_text() {
  uint16_t select_color = tft.color565(140, 170, 200);
  std::strncpy(ui_text.current_state,
               state_names[static_cast<int>(state)],
               sizeof(ui_text.current_state));
  ui_text.current_state[sizeof(ui_text.current_state) - 1] = '\0';
  tft.setTextSize(TFT_SMALL);
  tft.drawString(ui_text.current_state, 270, 7);
  if (ch_states.ch1_active) {
    if (ch_states.ch_selected == CHANNEL_1) {
      tft.setTextColor(ch_states.ch1_color, select_color);
    } else {
      tft.setTextColor(ch_states.ch1_color);
    }
    tft.drawString(ui_text.ch1, 230, 7);
  }
  if (ch_states.ch2_active) {
    if (ch_states.ch_selected == CHANNEL_2) {
      tft.setTextColor(ch_states.ch2_color, select_color);
    } else {
      tft.setTextColor(ch_states.ch2_color);
    }
    tft.drawString(ui_text.ch2, 250, 7);
  }
  tft.setTextColor(TFT_WHITE);
}

void button_logic(ViewState *view) {
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
    if (trigger.mode == NO_TRIGGER) trigger.mode = AUTO_TRIGGER;
  } else if ( inputs & BTN_MEASURE ) {
    state = State::MEASURE;
  } else if ( inputs & BTN_AUTOSET ) {
    autoset();
    update_grid();
  } else if ( inputs & BTN_STOP ) {
    toggle_run_stop();
  } else if ( inputs & BTN_CURSORS ) {
    if (state == State::CURSOR) {
      cursor_1.en = !cursor_1.en;
      state = State::POSITION;
    } else {
      cursor_1.en = true;
      state = State::CURSOR;
    }
  } else if ( inputs & BTN_CH1 ) {
    if (ch_states.ch_selected == CHANNEL_2) {
      ch_states.ch1_active = true;
      ch_states.ch_selected = CHANNEL_1;
    } else {
      ch_states.ch1_active = false;
      ch_states.ch_selected = CHANNEL_2;
    }
    update_grid();
  } else if ( inputs & BTN_CH2 ) {
    if (ch_states.ch_selected == CHANNEL_1) {
      ch_states.ch2_active = true;
      ch_states.ch_selected = CHANNEL_2;
    } else {
      ch_states.ch2_active = false;
      ch_states.ch_selected = CHANNEL_1;
    }
    update_grid();
  }

  switch (state) {
    case State::TRIGGER:
      if (inputs & BTN_UP) {
        trigger.level += 32;
        if (trigger.level > ADC_RESOLUTION)
          trigger.level = ADC_RESOLUTION;
      } else if (inputs & BTN_DOWN) {
        if (trigger.level < 32)
          trigger.level = 0;
        else
          trigger.level -= 32;
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
        timebase.x_zoom *= 0.9;
        update_grid();
      } else if (inputs & BTN_LEFT) {
        timebase.x_zoom *= 1.1;
        update_grid();
      }
      break;

    case State::POSITION:
      if (inputs & BTN_UP) {
        view->y_offset -= (uint16_t)(16.0f / view->y_zoom + 0.5f);
        update_grid();
      } else if (inputs & BTN_DOWN) {
        view->y_offset += (uint16_t)(16.0f / view->y_zoom + 0.5f);
        update_grid();
      } else if (inputs & BTN_RIGHT) {
        timebase.x_offset += (uint16_t)(32.0f * timebase.x_zoom + 0.5f);
        update_grid();
      } else if (inputs & BTN_LEFT) {
        timebase.x_offset -= (uint16_t)(32.0f * timebase.x_zoom + 0.5f);
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

  if (timebase.x_zoom < 0.1) timebase.x_zoom = 0.1;
  if (timebase.x_zoom > 20)  timebase.x_zoom = 20;

  if (view->y_zoom < 0.1) view->y_zoom = 0.1;
  if (view->y_zoom > 20)  view->y_zoom = 20;

  Serial.println((int)state);
}

void oscilloscope_task(void *pvParameters) {
  
  q = *(QueueHandle_t*)pvParameters;
 
  while( !mutex_take() ) { DELAY(100); }

  update_grid();
  
  tft.fillScreen(TFT_BLACK);

	xTaskCreate( adc_task, "ADC", 4096, NULL, 1, &adc );

    while(true) {
      tft.fillScreen(TFT_BLACK);
      if (ch_states.ch_selected == CHANNEL_1) {
        button_logic(&ch1_view);
        draw_grid(ch1_view);
      } else {
        button_logic(&ch2_view);
        draw_grid(ch2_view);
      }
      draw_ui_text();
      trigger_logic();
      if (ch_states.ch1_active) {
        draw_graph(&rb_ch1, ch1_view, ch_states.ch1_color);
        if (trigger.selected_channel == CHANNEL_1) {
          draw_trigger(CHANNEL_1, TFT_CYAN);
        }
      }
      if (ch_states.ch2_active) {
        draw_graph(&rb_ch2, ch2_view, ch_states.ch2_color);
        if (trigger.selected_channel == CHANNEL_2) {
          draw_trigger(CHANNEL_2, TFT_MAGENTA);
        }
      }
      draw_cursor(TFT_SKYBLUE);
      vTaskDelay(pdMS_TO_TICKS(REFRESH_RATE_MS));
    }

}

/*
Todo:
Measure
Cursors
Trigger settings
Math
Time values for x-axis
*/