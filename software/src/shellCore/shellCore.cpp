////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file shellCore.cpp
/// @date 2026-04-22
/// @author wawa2024
/// @brief A command line shell for interacting with FreeRTOS
///////////////////////////// 1.Libraries //////////////////////////////

#include <stdio.h>
#include <stdlib.h>

#include <esp32-oscilloscope.h>
#include <time_task.h>
#include <telnetCore.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define WORD_SIZE 16

/////////////////////////////// 3.Types ////////////////////////////////

typedef String(*funcptr)(const String&);

typedef struct {
  char name[WORD_SIZE] = "";
  funcptr f = nullptr;
} builtin;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////

static String help(const String&);
static String ls(const String&);
static String pp(const String&);
static String lscpu(const String&);
static String kill(const String&);
static String reboot(const String&);
static String resume(const String&);
static String suspend(const String&);
static String time(const String&);
static String telnet(const String&);

//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static const char* TAG = "shell";

static builtin table[] =
  { { "help", help }
  , { "ls", ls }
  , { "pp" , pp }
  , { "lscpu", lscpu }
  , { "kill", kill }
  , { "reboot", reboot }
  , { "resume", resume }
  , { "suspend", suspend }
  , { "time", time}
  , { "telnet", telnet}
};

//////////////////////////// 5.2.Functions /////////////////////////////

static String ok(const String& word){
  String s = "";
  return s + "OK '" + word + "'\r\n";
}

static String unknown(const String& word){
  String s = "";
  return s + "? '" + word + "'\r\n";
}

static String telnet(const String& args){
  return telnet_toggle() ? "Telnet online\r\n" : "Telnet offline\r\n";
}

static String time(const String& args){

  timeMethod_t method = args.equals("stop") ? TIME_STOP :
                        args.equals("start") ? TIME_START :
                        args.length() == 0 ? TIME_READ :
                        TIME_UNKNOWN;

  if(method == TIME_UNKNOWN) return unknown(args);

  timeRequest_t request = { .method = method };
  timeResponse_t response = time_request(request);

  char buf[16];
  for(size_t i = 0 ; i < sizeof(buf) ; i++) buf[i] = 0;

  snprintf( buf
            , sizeof(buf)
            ,"%02d:%02d:%02d\r\n"
            , response.time.hh
            , response.time.mm
            , response.time.ss
            );

  String s = buf;

  if( method != TIME_READ ) s = ok(args) + s;

  return s;
}

static const char* task_state_name(eTaskState state){
  switch (state) {
  case eRunning:   return "Running";
  case eReady:     return "Ready";
  case eBlocked:   return "Blocked";
  case eSuspended: return "Suspended";
  case eDeleted:   return "Deleted";
  case eInvalid:   return "Invalid";
  default:         return "Unknown";
  }
}

static inline String print_all_tasks_info(const String& args){

  String s = "No tasks\r\n";

  UBaseType_t task_count = uxTaskGetNumberOfTasks();
  if (task_count == 0) {
    return s;
  }

  TaskStatus_t* tasks = (TaskStatus_t*)pvPortMalloc(task_count * sizeof(TaskStatus_t));
  if (tasks == NULL) {
#ifdef DEBUG
    ESP_LOGI(TAG,"print_all_tasks_info allocation failed");
#endif
    return s;
  }

  // total_run_time is in the same units returned by the run-time counter
  uint32_t total_run_time = 0;
  UBaseType_t returned = uxTaskGetSystemState(tasks, task_count, &total_run_time);

  // Avoid divide-by-zero. If runtime not enabled, ulRunTimeCounter may be zero for all tasks.
  if (total_run_time == 0) total_run_time = 1;

  char buf_head[256];
  snprintf( buf_head
            ,sizeof(buf_head)
            ,"%-16s%-16s%-16s%-16s%-16s%-16s\r\n"
            ,"Name"
            ,"State"
            ,"Prio"
            ,"StackHWM"
            ,"RunTime"
            ,"CPU%%"
            );
  s = buf_head;

  for (UBaseType_t i = 0; i < returned; ++i) {
    TaskStatus_t *ts = &tasks[i];
    uint32_t rt = (uint32_t)ts->ulRunTimeCounter;
    float pct = (100.0f * rt) / (float)total_run_time;
    snprintf( buf_head
              , sizeof(buf_head)
              ,"%-16s%-16s%-16u%-16u%-16lu%-16f\r\n"
              ,ts->pcTaskName
              ,task_state_name(ts->eCurrentState)
              ,(unsigned)ts->uxCurrentPriority
              ,(unsigned)ts->usStackHighWaterMark
              ,rt
              ,pct
              );
    s += buf_head;
  }
  vPortFree(tasks);
  return s;
}

static TaskHandle_t get_task_by_name(const char* name){

  if (name == NULL) return NULL;

  UBaseType_t n = uxTaskGetNumberOfTasks();
  TaskStatus_t* tasks = (TaskStatus_t*)pvPortMalloc(n * sizeof(TaskStatus_t));
  if (!tasks) return NULL;

  UBaseType_t retrieved = uxTaskGetSystemState(tasks, n, NULL);
  TaskHandle_t handle = NULL;

  for (UBaseType_t i = 0; i < retrieved; ++i) {
    if (tasks[i].pcTaskName && strcmp(tasks[i].pcTaskName, name) == 0) {
      handle = tasks[i].xHandle;
      break;
    }
  }

  vPortFree(tasks);

  return handle;
}

static bool delete_task_by_name(const char* name){
  if (name == NULL) return false;
  TaskHandle_t handle = get_task_by_name(name);
  if (handle == NULL) return false;
  vTaskDelete(handle); // kill task
  return true;
}

static bool suspend_task_by_name(const char* name){
  if (name == NULL) return false;
  TaskHandle_t handle = get_task_by_name(name);
  if (handle == NULL) return false;
  vTaskSuspend(handle); // Suspend task
  return true;
}

static bool resume_task_by_name(const char* name){
  if (name == NULL) return false;
  TaskHandle_t handle = get_task_by_name(name);
  if (handle == NULL) return false;
  vTaskResume(handle); // Resume task
  return true;
}

static String pp(const String& args){
  char buf_head[256];
  snprintf( buf_head
           , sizeof(buf_head)
           , "%-16s%-8s%-8s%-8s%-8s\r\n"
           ,"Name"
           ,"State"
           ,"Prio"
           ,"Stack"
           ,"#TCB"
           );
  const size_t buf_sz = 2048;
  char buf[buf_sz];
  vTaskList(buf);
  String s = "";
  return s + buf_head + buf;
}

static String ls(const String& args){
  return print_all_tasks_info(args);
}

static String lscpu(const String& args){
  return "Not implemented\r\n";
}

static String reboot(const String& args){
  esp_restart();
  return "DIE\r\n";
}

static String kill(const String& args){
  if( delete_task_by_name(args.c_str()) ){
    mutex_delete();
    mutex_create();
    return ok(args);
  } else {
    return unknown(args);
  }
}

static String suspend(const String& args){
  if( suspend_task_by_name(args.c_str()) ){
    return ok(args);
  } else {
    return unknown(args);
  }
}

static String resume(const String& args){
  if( resume_task_by_name(args.c_str()) ){
    return ok(args);
  } else {
    return unknown(args);
  }
}

static String help(const String& args){
  String s = table[0].name;
  for(size_t i = 1 ; i < ( sizeof(table)/sizeof(builtin) ) ; i++) {
    s = s + " " + table[i].name;
  }
  return s + "\r\n";
}

static funcptr getcmd(const String& word){
  for(int i = 0 ; i < ( sizeof(table)/sizeof(builtin) ) ; i++) {
    char* name = table[i].name;
    if( word.equals(name) ) {
      return table[i].f;
    }
  }
  return nullptr;
}

String shell(const char* stream){
  String s = stream;

  if (s.length()){

#ifdef DEBUG
    ESP_LOGI(TAG,"input=%s",s);
#endif

    size_t sp = s.indexOf(' ');
    String name;

    if (sp == -1) {
      name = s;
      s = "";
    } else {
      name = s.substring(0, sp);
      s = s.substring(sp + 1);
    }

    funcptr f = getcmd(name);
    if( f != nullptr ) return ok(name) + f(s);
    else return unknown(name);

  }

  return s + "\r\n";
}
