////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file snake_task.cpp
/// @date 2026-04-19
/// @author wawa2024
/// @brief A snake game task implemented on FreeRTOS
///////////////////////////// 1.Libraries //////////////////////////////

#include <stdint.h>
#include <esp32-oscilloscope.h>
#include <hmiCore.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define GRID_COUNT_X 32
#define GRID_COUNT_Y 16
#define BUTTON_LEFT  0
#define BUTTON_RIGHT 35
#define GRID_OFFSET_X (RESOLUTION_X / GRID_COUNT_X)
#define GRID_OFFSET_Y (RESOLUTION_Y / GRID_COUNT_Y)
#define REFRESH_RATE_MS 300

/////////////////////////////// 3.Types ////////////////////////////////

enum Block {
  APPLE,
  SNAKE,
  EMPTY
};

enum Direction {
  UP,
  DOWN,
  LEFT,
  RIGHT
};

enum Game {
  WIN,
  LOSE,
  UNKNOWN
};

typedef uint8_t Index;

typedef Block Grid[GRID_COUNT_X][GRID_COUNT_Y];

typedef struct {
  Index x,y;
} Point;

typedef Point History[GRID_COUNT_X*GRID_COUNT_Y];

typedef struct {
  Index wp = 0, rp = 0;
  History history = {0};
} Tail;

typedef struct {
  Point head;
  Tail tail;
  Direction dir;
  Game status = UNKNOWN;
} Snake;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////

//////////////////////////// 4.2.Functions /////////////////////////////

static void snake_deinit();

//////////////////////////// 5.Definitions /////////////////////////////

static QueueHandle_t q = NULL;

//////////////////////////// 5.1.Variables /////////////////////////////

static const char* TAG = "snake_task";

//////////////////////////// 5.2.Functions /////////////////////////////

static bool evalApple(Grid grid, Index& x, Index& y){
  Block& block = grid[x][y];
  // Eval point
  switch(block){
  case APPLE: return true; break;
  case SNAKE: break;
  case EMPTY:
  default: block = APPLE; return true; break;
  }
  // Increment point
  if( x < (GRID_COUNT_X-1) ){
    x++;
  } else {
    x = 0;
    if( y < (GRID_COUNT_Y-1) ){
      y++;
    } else {
      y = 0;
    }
  }
  return false;
}

static Game placeApple(Grid grid){
  Index x0 = random(GRID_COUNT_X);
  Index y0 = random(GRID_COUNT_Y);
  Index x = x0;
  Index y = y0;
  if( evalApple(grid,x,y) ) return UNKNOWN;
  while( ! ( x == x0 && y == y0 ) ) {
    if( evalApple(grid,x,y) ) return UNKNOWN;
  }
  return WIN;
}

static void drawSnake(Grid grid){
  for(Index y = 0; y < GRID_COUNT_Y ; y++){
    for(Index x = 0; x < GRID_COUNT_X ; x++){
      Block block = grid[x][y];
      int color;
      switch(block){
      case APPLE: color = TFT_RED; break;
      case SNAKE: color = TFT_GREEN; break;
      case EMPTY:
      default: color = TFT_BLACK; break;
      }
      tft.fillRect( x*GRID_OFFSET_X
                  , y*GRID_OFFSET_Y
                  , GRID_OFFSET_X
                  , GRID_OFFSET_Y
                  , color
                  );
    }
  }
}

static void addTail(Tail& tail, Point& point){
  Index& wp = tail.wp;
  History& history = tail.history;
  history[wp] = point;
  wp = wp < (GRID_COUNT_X*GRID_COUNT_Y-1) ? wp+1 : 0;
}

static void rmTail(Tail& tail, Grid grid){
  Index& rp = tail.rp;
  History& history = tail.history;
  Index& x = history[rp].x;
  Index& y = history[rp].y;
  grid[x][y] = EMPTY;
  rp = rp < (GRID_COUNT_X*GRID_COUNT_Y-1) ? rp+1 : 0;
}

static Direction getDir(Direction dir){

  hmiEventData_t data = getinputs(q);
  uint32_t& inputs = data.inputs;

  if( inputs & BTN_ESC ) snake_deinit();

  switch(dir){
  case UP: case DOWN:
    dir = inputs & BTN_LEFT ? LEFT :
      inputs & BTN_RIGHT ? RIGHT : dir;
      break;
  case LEFT: case RIGHT:
    dir = inputs & BTN_UP ? UP :
        inputs & BTN_DOWN ? DOWN : dir;
        break;
  default: break;
  }

  return dir;
}

static Game evalSnake(Snake& snake, Grid grid){
  Index& x = snake.head.x;
  Index& y = snake.head.y;
  Tail& tail = snake.tail;
  Direction& dir = snake.dir;
  Point origin = snake.head;

  // Move head
  switch(dir){
  case UP: y = y == 0 ? GRID_COUNT_Y-1 : y-1; break;
  case DOWN: y = y < (GRID_COUNT_Y-1) ? y+1 : 0; break;
  case LEFT: x = x == 0 ? GRID_COUNT_X-1 : x-1; break;
  case RIGHT: x = x < (GRID_COUNT_X-1) ? x+1 : 0; break;
  default: return LOSE; break;
  }

  Block& block = grid[x][y];

  // Eval point
  switch(block){
  case APPLE:
    block = SNAKE;
    if( placeApple(grid) == WIN ) return WIN;
    addTail(tail, origin);
    break;
  case SNAKE:
    return LOSE;
    break;
  case EMPTY:
  default:
    block = SNAKE;
    addTail(tail, origin);
    rmTail(tail, grid);
    break;
  }
  return UNKNOWN;
}

static void snake_deinit(){

  mutex_release();

  #ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
  #endif

  vTaskDelete(NULL); // self-delete

}

void snake_task(void* pvParameter){

  #ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
  #endif

  q = *(QueueHandle_t*)pvParameter;

  if(q == NULL){
    snake_deinit();
    return;
  }

  while(true){
    // attempt to take mutex
    if (mutex_take()) {

      reset();

      Grid grid;
      for(Index y=0; y < GRID_COUNT_Y;y++)
        for(Index x=0; x < GRID_COUNT_X;x++)
          grid[x][y] = EMPTY;

      Snake snake;
      snake.head.x = random(GRID_COUNT_X);
      snake.head.y = random(GRID_COUNT_Y);
      snake.tail = Tail();
      snake.dir = (Direction)random(4);
      snake.status = UNKNOWN;

      grid[snake.head.x][snake.head.y] = SNAKE; // seed snake
      placeApple(grid); // seed apple
      drawSnake(grid); // draw screen
      DELAY(REFRESH_RATE_MS); // wait

      while(true){
        snake.dir = getDir(snake.dir); // read direction
        snake.status = evalSnake(snake, grid); // eval snake movement
        drawSnake(grid); // draw screen
        if( snake.status != UNKNOWN ){
          break;
        }
        DELAY(REFRESH_RATE_MS); // wait
      }

      // Print message
      char* msg;
      switch(snake.status){
      case WIN: msg = (char*)"YOU WON"; break;
      case LOSE: msg = (char*)"YOU LOST"; break;
      default: msg = (char*) "???"; break;
      }
      tft.drawCentreString(msg,RESOLUTION_X/2,RESOLUTION_Y/2,1);

      // Wait for ESC button press
      for(hmiEventData_t data = getinputs(q); !(data.inputs & BTN_ESC); data = getinputs(q));

      break;

    } else {
      DELAY(160);
    }
  }

  q = NULL;

  snake_deinit();

}
