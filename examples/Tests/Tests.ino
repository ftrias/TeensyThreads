#include <Arduino.h>

#include "Threads.h"

volatile int p1 = 0;
volatile int p2 = 0;
volatile int p3 = 0;

void my_priv_func1(int data){
  p1 = 0;
  data *= 1000; // convert sec to ms;
  int mx = millis();
  while(1) {
    p1++;
    if ((int)millis() - mx > data) break;
  }
}

void my_priv_func2() {
  p2 = 0;
  while(1) p2++;
}

void my_priv_func3() {
  p3 = 0;
  while(1) p3++;
}

void showp() {
  Serial.print(p1);
  Serial.print(" ");
  Serial.print(p2);
  Serial.print(" ");
  Serial.print(p3);
  Serial.println();
}

int id1, id2, id3;

#define delayx delay

void runtest() {
  int save_p;
  int save_time;

  // benchmark with no threading
  my_priv_func1(1);
  save_time = p1;

  Serial.print("Test thread start ");
  id1 = threads.addThread(my_priv_func1, 1);
  delayx(500);
  if (p1 != 0) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread run state ");
  if (threads.getState(id1) == Threads::RUNNING) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread return ");
  delayx(1000);
  save_p = p1;
  delayx(300);
  if (p1 != 0 && p1 == save_p) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread speed ");
  float rate = (float)p1 / (float)save_time;
  if (rate < 0.6 && rate > 0.4) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Speed no threads: ");
  Serial.println(save_time);
  Serial.print("Speed 1 thread: ");
  Serial.println(p1);
  Serial.print("Ratio: ");
  Serial.println(rate);

  Serial.print("Test set time slice ");
  id1 = threads.addThread(my_priv_func1, 1);
  delayx(2000);
  save_p = p1;
  id1 = threads.addThread(my_priv_func1, 1);
  threads.setTimeSlice(id1, 200);
  delayx(2000);
  float expected = (float)save_p * 2.0*200.0 / ((float)Threads::DEFAULT_TICKS + 200.0);
  rate = (float)p1 / (float)expected;
  if (rate > 0.9 && rate < 1.1) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Speed default ticks: ");
  Serial.println(save_p);
  Serial.print("Speed 200 ticks: ");
  Serial.println(p1);
  Serial.print("Expected: ");
  Serial.println(expected);
  Serial.print("Ratio with expected: ");
  Serial.println(rate);

  Serial.print("Test delay yield ");
  id1 = threads.addThread(my_priv_func1, 1);
  threads.delay(1100);
  rate = (float)p1 / (float)save_time;
  if (rate > 0.9 && rate < 1.1) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Yield wait ratio: ");
  Serial.println(rate);

  Serial.print("Test thread end state ");
  if (threads.getState(id1) == Threads::ENDED) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread reinitialize ");
  p2 = 0;
  id2 = threads.addThread(my_priv_func2);
  delayx(200);
  if (p2 != 0) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test stack usage ");
  int sz = threads.getStackUsed(id2);
  // Seria.println(sz);
  if (sz>=40 && sz<=48) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread suspend ");
  delayx(200);
  threads.suspend(id2);
  delayx(200);
  save_p = p2;
  delayx(200);
  if (p2 != 0 && p2 == save_p) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread restart ");
  p2 = 0;
  threads.restart(id2);
  delayx(1000);
  if (p2 != 0) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread stop ");
  threads.stop();
  delayx(200);
  p2 = 0;
  delayx(200);
  if (p2 == 0) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread start ");
  threads.start();
  delayx(200);
  if (p2 != 0) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread wait ");
  id3 = threads.addThread(my_priv_func1, 1);
  int time = millis();
  int r = threads.wait(id3);
  delayx(100);
  time = millis() - time;
  if (r==id3) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread wait time ");
  if (time > 1000 && time < 2000) Serial.println("OK");
  else Serial.println("***FAIL***");

  Serial.print("Test thread kill ");
  id3 = threads.addThread(my_priv_func1, 2);
  delayx(300);
  threads.kill(id3);
  delayx(300);
  save_p = p1;
  delayx(300);
  if (save_p==p1) Serial.println("OK");
  else Serial.println("***FAIL***");

  delayx(1000);

  Serial.print("Test std::thread scope ");
  {
    std::thread th2(my_priv_func3);
    delayx(500);
  }
  delayx(500);
  save_p = p3;
  delayx(500);
  if (p3 != 0 && p3 == save_p) Serial.println("OK"); 
  else Serial.println("***FAIL***");
}

void runloop() {
  static int timeloop = millis();
  static int mx = 0;
  static int count = 0;
  if (millis() - mx > 5000) {
    Serial.print(count);
    Serial.print(": ");
    Serial.print((millis() - timeloop)/1000);
    Serial.print(" sec ");
    Serial.print(p2);
    Serial.println();
    count++;
    mx = millis();
  }
}


void setup() {
  delay(1000);
  runtest();
  Serial.println("Test infinite loop (will not end)");
}

void loop() {
  runloop();
}
