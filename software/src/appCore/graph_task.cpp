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
#include <stdint.h>
#include <inttypes.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define GRID_COUNT_X 8
#define GRID_COUNT_Y 10
#define GRID_OFFSET_X (RESOLUTION_X / GRID_COUNT_X) // 16
#define GRID_OFFSET_Y (RESOLUTION_Y / GRID_COUNT_Y) // 16
#define REFRESH_RATE_MS 100
#define ADC_RESOLUTION 4096
#define MAX_VOLTS 16.0
#define TIMESTEP_MS 1
#define STR_LEN 20
#define CH1_MULTIPLIER 1
#define CH2_MULTIPLIER 1
#define CH1_BUF_SIZE 10240
#define CH2_BUF_SIZE (CH1_BUF_SIZE * CH2_MULTIPLIER)
#define CH1_SAMPLE_RATE 20000
#define CH2_SAMPLE_RATE 20000
#define HALF_RES_X (RESOLUTION_X / 2)

/////////////////////////////// 3.Types ////////////////////////////////

enum class State {
  TRIGGER,
  TRIGGER_SETTINGS,
  SCALE,
  POSITION,
  MEASURE,
  CURSOR_1,
  CURSOR_2,
  AUTOSET,
  RUN,
  STOP,
  MATH
};

typedef struct {
  float x_zoom;
  int32_t x_offset;
  uint16_t required_sample_rate;
  uint32_t time_per_div_us;
  float time_per_pixel_us;
} Timebase;

typedef struct {
  float y_zoom;
  int32_t y_offset;
  uint8_t x_multiplier;
} ViewState;

typedef struct {
  uint16_t *samples;
  uint32_t buffer_size;
  uint16_t sample_rate;
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
  boolean en = false;
  float voltage = 0.0f;
  int32_t time_us = 0;
  int32_t color;
  uint16_t offset_y;
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
  "cursor 1",
  "cursor 2",
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
  .x_offset = 0,
  .required_sample_rate = RESOLUTION_X / 2,
  .time_per_div_us = GRID_OFFSET_X * 2 * 1000,
  .time_per_pixel_us = 2000.0
};

ViewState ch1_view = {
  .y_zoom = 1.0,
  .y_offset = 2385,
  .x_multiplier = CH1_MULTIPLIER
};

ViewState ch2_view = {
  .y_zoom = 1.0,
  .y_offset = 2385,
  .x_multiplier = CH2_MULTIPLIER
};

uint16_t rb_ch1_storage[CH1_BUF_SIZE];
uint16_t rb_ch2_storage[CH2_BUF_SIZE];

RingBuffer rb_ch1{
  .samples = rb_ch1_storage,
  .buffer_size = CH1_BUF_SIZE,
  .sample_rate = CH1_SAMPLE_RATE,
  .write_head = 0,
  .read_head = 0
};
RingBuffer rb_ch2{
  .samples = rb_ch2_storage,
  .buffer_size = CH2_BUF_SIZE,
  .sample_rate = CH2_SAMPLE_RATE,
  .write_head = 0,
  .read_head = 0
};

uint16_t copy_rb_ch1_storage[CH1_BUF_SIZE];
uint16_t copy_rb_ch2_storage[CH2_BUF_SIZE];

RingBuffer copy_rb_ch1{
  .samples = copy_rb_ch1_storage,
  .buffer_size = CH1_BUF_SIZE,
  .sample_rate = CH1_SAMPLE_RATE,
  .write_head = 0,
  .read_head = 0
};
RingBuffer copy_rb_ch2{
  .samples = copy_rb_ch2_storage,
  .buffer_size = CH2_BUF_SIZE,
  .sample_rate = CH2_SAMPLE_RATE,
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
  .en = false,
  .color = TFT_SKYBLUE,
  .offset_y = 20
};

Cursor cursor_2 = {
  .x = RESOLUTION_X / 2,
  .y = RESOLUTION_Y / 2,
  .en = false,
  .color = TFT_RED,
  .offset_y = 34
};

GridValues grid_values{};

State state = State::POSITION;

UiText ui_text = {
  .ch1 = "CH1",
  .ch2 = "CH2",
  .current_state = "position"
};

//////////////////////////// 5.2.Functions /////////////////////////////

typedef struct
{
  uint32_t index;
  uint32_t sum;
  uint16_t max;
  uint16_t min;
  bool needs_reset = true;
} avg_t;

avg_t ch1_avg;
avg_t ch2_avg;

void average_add( uint16_t sample, afeChannel_t ch )
{
  avg_t *avg = ch == CHANNEL_1 ? &ch1_avg : &ch2_avg;

  if (avg->needs_reset) {
    avg->index = 0;
    avg->sum = 0;
    avg->max = sample;
    avg->min = sample;
    avg->needs_reset = false;
  }
  
  avg->sum += sample;
  avg->index++;

  if (sample < avg->min) avg->min = sample;
  if (sample > avg->max) avg->max = sample;
}

static const uint32_t rollingAverageLength = 3;

typedef struct
{
  uint16_t buff[rollingAverageLength];
  uint32_t index;
  uint32_t sum;
} rollingAvg_t;

uint16_t rollingAverage( uint16_t sample, afeChannel_t ch )
{
  static rollingAvg_t ch1_avg;
  static rollingAvg_t ch2_avg;
  
  rollingAvg_t *avg = ch == CHANNEL_1 ? &ch1_avg : &ch2_avg;

  avg->sum -= avg->buff[avg->index];
  avg->sum += sample;
  avg->buff[avg->index] = sample;
  if( ++avg->index >= rollingAverageLength ) { avg->index = 0; }

  return (uint16_t)(avg->sum / rollingAverageLength);
}

void add_sample( uint16_t val, afeChannel_t ch ) 
{

  RingBuffer *ch1_ptr = ch_states.stop ? &copy_rb_ch1 : &rb_ch1;
  RingBuffer *ch2_ptr = ch_states.stop ? &copy_rb_ch2 : &rb_ch2;

  RingBuffer *rb = ch == CHANNEL_1 ? ch1_ptr : ch2_ptr;

	rb->samples[rb->write_head] = ADC_RESOLUTION - 1 - rollingAverage( val, ch );
	rb->write_head = (rb->write_head + 1) % rb->buffer_size;
}

void adc_task(void *pvParameters) {

	while (true) {

    xTaskNotifyWait( 0, ULONG_MAX, NULL, portMAX_DELAY ); 

    if( ch_states.ch1_active ) 
    {
      if( afeCore_isChannel1Disabled() ) 
      { 
        add_sample(0, CHANNEL_1);
        afeCore_updateNewestSample( 0, CHANNEL_1 ); 
      } 
      else 
      {
        int ch1_reading;

        adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_1 ), 
                          CH1_VOLTAGE, &ch1_reading );

        afeCore_updateNewestSample( ch1_reading, CHANNEL_1 ); 
        
        add_sample( (uint16_t)ch1_reading, CHANNEL_1 );
      } 
    }

    if( ch_states.ch2_active )
    {
      int ch2_reading;

      adc_oneshot_read( afeCore_getChannelAdcHandle( CHANNEL_2 ), 
                        CH2_VOLTAGE, &ch2_reading );

      afeCore_updateNewestSample( ch2_reading, CHANNEL_2 ); 
        
      add_sample( (uint16_t)ch2_reading, CHANNEL_2 );
    }

  }
}

static void oscilloscope_deinit(){
  #ifdef DEBUG
  Serial.println("[oscilloscope_task]: self-deleting");
  #endif

  mutex_release();

  vTaskDelete(NULL); // self-delete
}

int32_t get_time_us(uint16_t x) {
  return (int32_t)((float)(x - HALF_RES_X) * timebase.time_per_pixel_us);
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

void us_to_string(int32_t us, char *out, size_t out_len) {
  char sign = ' ';
  uint32_t abs_us;

  if (us < 0) {
    sign = '-';
    abs_us = (uint32_t)(-us);
  } else {
    abs_us = (uint32_t)us;
  }

  const char *unit;
  uint32_t whole, frac;
  uint32_t factor;

  if (abs_us >= 1000000UL) {
    unit = "s";
    factor = 1000000UL;
  } else if (abs_us >= 1000UL) {
    unit = "m";
    factor = 1000UL;
  } else {
    unit = "u";
    factor = 1UL;
  }

  whole = abs_us / factor;
  uint32_t rem = abs_us % factor;

  char buf[24];

  if (whole >= 100) {
    // xxx
    snprintf(buf, sizeof(buf), "%lu%s", (unsigned long)whole, unit);
  }
  else if (whole >= 10) {
    // xx.x
    frac = (rem * 10) / factor;
    snprintf(buf, sizeof(buf), "%lu.%lu%s", (unsigned long)whole, (unsigned long)frac, unit);
  }
  else {
    // x.xx
    frac = (rem * 100) / factor;
    snprintf(buf, sizeof(buf), "%lu.%02lu%s", (unsigned long)whole, (unsigned long)frac, unit);
  }

  snprintf(out, out_len, "%c%s", sign, buf);
}

void float_to_string(float val, char *out, size_t out_len) {
  char sign = ' ';
  float abs_val;

  if (val < 0.0f) {
      sign = '-';
      abs_val = (float)(-val);
  } else {
      abs_val = (float)val;
  }

  int int_part = (int)abs_val;
  int frac_part = (int)((abs_val - int_part) * 100.0f + 0.5f);

  if (frac_part >= 100) {
    frac_part = 0;
    int_part += 1;
  }

  snprintf(out, out_len, "%c%d.%02dV", sign, int_part, frac_part);
}

void set_x_zoom(float zoom) {
  timebase.x_zoom = zoom;
  
  if (timebase.x_zoom < 0.001) timebase.x_zoom = 0.001;
  if (timebase.x_zoom > 20)  timebase.x_zoom = 20;

  timebase.required_sample_rate = (uint16_t)(RESOLUTION_X / timebase.x_zoom + 0.5f);
  timebase.time_per_div_us = (uint32_t)((float)GRID_OFFSET_X * timebase.x_zoom * 100.0f + 0.5f);
  timebase.time_per_pixel_us = timebase.x_zoom * 100.0f;
}

void apply_autoset(RingBuffer *rb, ViewState *view) {
  uint16_t min_val = rb->samples[0];
  uint16_t max_val = rb->samples[0];

  for (size_t i = 1; i < rb->buffer_size; ++i) {
    if (rb->samples[i] < min_val) {
      min_val = rb->samples[i];
    }
    if (rb->samples[i] > max_val) {
      max_val = rb->samples[i];
    }
  }

  uint16_t val_delta = max_val - min_val;

  view->y_offset = (uint32_t)((float)(min_val + max_val) * 0.5f + 0.5f);
  if (val_delta == 0) {
    view->y_zoom = 20.0f;
  } else {
    view->y_zoom = ((float)RESOLUTION_Y / (float)val_delta) * 0.8f;
  }
  if (view->y_zoom > 20.0f) view->y_zoom = 20.0f;
  if (view->y_zoom < 0.01f) view->y_zoom = 0.01f;

  uint32_t sum = 0;
  for (size_t i = 0; i < rb->buffer_size; i++) {
    sum += rb->samples[i];
  }
  uint32_t mean = (uint32_t)((sum / rb->buffer_size) + 0.5f);

  uint32_t crossings[5000];
  uint32_t count = 0;
  uint16_t hysteresis = (uint16_t)((float)val_delta * 0.04f + 0.5f);
  if (hysteresis < 2) hysteresis = 2;

  for (size_t i = 1; i < rb->buffer_size; i++) {
    if (rb->samples[i-1] < mean - hysteresis && rb->samples[i] >= mean + hysteresis) {
      if (count < 5000) crossings[count++] = i;
    }
  }

  float avg_period = 0;

  if (count > 1) {
    uint32_t total = 0;
    for (int i = 1; i < count; i++) {
      total += (crossings[i] - crossings[i-1]);
    }
    avg_period = (float)total / (float)(count - 1);
  }

  timebase.x_offset = 0;

  if (avg_period <= 0.0f) {
    set_x_zoom(1.0f);
    return;
  }

  float desired_cycles = 1.5f;
  float samples_per_screen = avg_period * desired_cycles;

  Serial.println(samples_per_screen);
  set_x_zoom((samples_per_screen / (float)RESOLUTION_X));
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

void moveCursor(Cursor& cursor, int inputs) {
  if (inputs & BTN_UP) {
    cursor.y += 5;
  }
  if (inputs & BTN_DOWN) {
    cursor.y -= 5;
  }
  if (inputs & BTN_RIGHT) {
    cursor.x += 5;
  }
  if (inputs & BTN_LEFT) {
    cursor.x -= 5;
  }
}

void draw_cursor(Cursor *cursor) {
  if (cursor->en) {
    tft.drawFastHLine(0, RESOLUTION_Y - cursor->y, RESOLUTION_X, cursor->color);
    tft.drawFastVLine(cursor->x, 0, RESOLUTION_Y, cursor->color);
    cursor->voltage = get_voltage(RESOLUTION_Y - cursor->y, ch_states.ch_selected);
    cursor->time_us = get_time_us(cursor->x);
    char voltage[STR_LEN];
    char time[STR_LEN];
    float_to_string(cursor->voltage, voltage, STR_LEN);
    us_to_string(cursor->time_us, time, STR_LEN);
    tft.setTextSize(TFT_SMALL);
    tft.setTextColor(cursor->color);
    tft.drawString(voltage, 230, cursor->offset_y);
    tft.drawString(time, 270, cursor->offset_y);
    tft.setTextColor(TFT_WHITE);
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
	for (uint16_t i = 0; i < GRID_COUNT_X - 1; i++) {
    int32_t microseconds = timebase.time_per_div_us * (int32_t)((i+1) - (uint16_t)((float)(GRID_COUNT_X) * 0.5f + 0.5f));
    us_to_string(microseconds, grid_values.x[i], STR_LEN);
	}
	for (uint16_t i = 0; i < GRID_COUNT_Y - 1; i++) {
    uint16_t y = (i+1) * GRID_OFFSET_Y;
    float val = get_voltage(y, ch_states.ch_selected);
    float_to_string(val, grid_values.y[i], STR_LEN);
	}
}

void draw_grid(ViewState view) {
	for (uint16_t i = 0; i < GRID_COUNT_X-1; i++) {
    uint16_t x = (i+1)*GRID_OFFSET_X;
		tft.drawFastVLine(x, 0, RESOLUTION_Y, TFT_DARKGREY);
    tft.setTextSize(TFT_SMALL);
    tft.drawString(grid_values.x[i], x-16, RESOLUTION_Y - 12);
	}
	for (uint16_t i = 0; i < GRID_COUNT_Y-1; i++) {
    uint16_t y = (i+1)*GRID_OFFSET_Y;
		tft.drawFastHLine(0, y, RESOLUTION_X, TFT_DARKGREY);
    tft.setTextSize(TFT_SMALL);
    tft.drawString(grid_values.y[i], 4, y-3);
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

void process_esc() {
  switch (state) {
    case State::CURSOR_1:
      cursor_1.en = false;
      if (cursor_2.en) {
        state = State::CURSOR_2;
        break;
      }
      state = State::POSITION;
      break;
    case State::CURSOR_2:
      cursor_2.en = false;
      if (cursor_1.en) {
        state = State::CURSOR_1;
        break;
      }
      state = State::POSITION;
      break;

    default:
      oscilloscope_deinit();
      break;
  }
}

void button_logic(ViewState *view) {
  hmiEventData_t data = getinputs(q);
  uint32_t& inputs = data.inputs;

  if ( inputs & BTN_ESC ) process_esc();

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
    if (state == State::CURSOR_1) {
      cursor_2.en = true;
      state = State::CURSOR_2;
    } else {
      cursor_1.en = true;
      state = State::CURSOR_1;
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
        set_x_zoom(timebase.x_zoom * 0.9f);
        update_grid();
      } else if (inputs & BTN_LEFT) {
        set_x_zoom(timebase.x_zoom * 1.1f);
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

    case State::CURSOR_1:
      moveCursor(cursor_1, inputs);
      break;
    case State::CURSOR_2:
      moveCursor(cursor_2, inputs);
      break;

    default:
        break;
  }

  if (view->y_zoom < 0.01) view->y_zoom = 0.01;
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
      draw_cursor(&cursor_1);
      draw_cursor(&cursor_2);
      vTaskDelay(pdMS_TO_TICKS(REFRESH_RATE_MS));
    }

}