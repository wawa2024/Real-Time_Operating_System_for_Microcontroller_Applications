////////////////////////////////////////////////////////////////////////
// @file serial_task.cpp
// Serial task
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>

/////////////////////////////// 2.Macros ///////////////////////////////

#define REFRESH_RATE_MS 100
#define WORD_SIZE 16

/////////////////////////////// 3.Types ////////////////////////////////

typedef void(*funcptr)(void);

typedef struct {
  char name[WORD_SIZE];
  funcptr f;
} builtin;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////

static void help();

//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static builtin table[] = { { "HELP", help }
  };

//////////////////////////// 5.2.Functions /////////////////////////////

static void help(){
  for(int i = 0 ; i < ( sizeof(table)/sizeof(builtin) ) ; i++) {
    char* name = table[i].name;
    Serial.print(name); Serial.print(" ");
  }
  Serial.println();
}

static funcptr getcmd(String& word){

  for(int i = 0 ; i < ( sizeof(table)/sizeof(builtin) ) ; i++) {

    char* name = table[i].name;

    if( word.equals(name) ) {
      return table[i].f;
    }

  }
  Serial.println("[serial_task]: WORD ' " + word + "' WAS NOT FOUND");
  return nullptr;
}

static String getline(){
  bool flag = true;
  String s = "";
  while(flag){
    if(Serial.available()){
      char c = Serial.read();
      switch(c){
      case '\r':
        break;
      case '\n':
        Serial.println();
        flag = false;
        break;
      case '\b':
        if (!s.isEmpty()) s.remove(s.length() - 1);
        Serial.write(c);
        Serial.write(' ');
        Serial.write(c);
        break;
      default:
        s += c;
        Serial.write(c);
        break;
      }
    }
    DELAY(20);
  }
  return s;
}

static void serial_init(){
  #ifdef DEBUG
  Serial.println("[serial_task]: launched");
  #endif
}

static void serial_deinit(){
  #ifdef DEBUG
  Serial.println("[serial_task]: self-deleting");
  #endif
  vTaskDelete(NULL); // self-delete
}

void serial_task(void* pvParameter) {

  serial_init();

  if( not mutex_take(serial_mutex) ) {
    serial_deinit();
    return;
  }

  while (true) {

    String line = getline();

    if (line.length()) {
      line = "[serial_task]: input=" + line;
      Serial.println(line);
      funcptr f = getcmd(line);
      if( f != nullptr ) f();
    }

    DELAY(REFRESH_RATE_MS);
  }

  serial_deinit();

}
