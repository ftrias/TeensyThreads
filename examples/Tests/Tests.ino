#include <Arduino.h>
#include "Threads.h"

volatile long int p1 = 0;
volatile long int p2 = 0;
volatile long int p3 = 0;

void test_fun1(int data){
  p1 = 0;
  data *= 1000; // convert sec to ms;
  int mx = millis();
  while(1) {
    p1++;
    if ((int)millis() - mx > data) break;
  }
}

void test_fun2() {
  p2 = 0;
  while(1) p2++;
}

void test_fun3() {
  p3 = 0;
  while(1) p3++;
}

int id1, id2, id3;

void setup() {
  int save_p;
  int save_time;

  // benchmark with no threading
  test_fun1(1);
  save_time = p1;

  Serial.print("Test thread start ");
  id1 = threads.addThread(test_fun1, 1);
  delay(300);
  if (p1 != 0) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread run state ");
  if (threads.getState(id1) == Threads::RUNNING) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread return ");
  delay(1000);
  save_p = p1;
  delay(300);
  if (p1 != 0 && p1 == save_p) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread speed ");
  float rate = (float)p1 / (float)save_time;
  if (rate < 0.6 && rate > 0.4) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Speed no threads: ");
  Serial.println(save_time);
  Serial.print("Speed 1 thread: ");
  Serial.println(p1);
  Serial.print("Ratio: ");
  Serial.println(rate);

  Serial.print("Test set time slice ");
  id1 = threads.addThread(test_fun1, 1);
  delay(1100);
  save_p = p1;
  id1 = threads.addThread(test_fun1, 1);
  threads.setTimeSlice(id1, 10);
  delay(1100);
  float expected = (float)save_time / (float)Threads::DEFAULT_TICKS * 10.0;
  rate = (float)p1 / (float)expected;
  if (rate > 0.9 && rate < 1.1) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Speed default ticks: ");
  Serial.println(save_p);
  Serial.print("Speed 10 ticks: ");
  Serial.println(p1);
  Serial.print("Expected: ");
  Serial.println(expected);
  Serial.print("Ratio with expected: ");
  Serial.println(rate);

  Serial.print("Test delay yield ");
  id1 = threads.addThread(test_fun1, 1);
  threads.delay(1100);
  rate = (float)p1 / (float)save_time;
  if (rate > 0.9 && rate < 1.1) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Yield wait ratio: ");
  Serial.println(rate);

  Serial.print("Test thread end state ");
  if (threads.getState(id1) == Threads::ENDED) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread reinitialize ");
  p2 = 0;
  id2 = threads.addThread(test_fun2);
  delay(200);
  if (p2 != 0) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread suspend ");
  delay(200);
  threads.suspend(id2);
  delay(200);
  save_p = p2;
  delay(200);
  if (p2 != 0 && p2 == save_p) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread restart ");
  p2 = 0;
  threads.restart(id2);
  delay(1000);
  if (p2 != 0) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread stop ");
  threads.stop();
  delay(100);
  p2 = 0;
  delay(100);
  if (p2 == 0) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread start ");
  threads.start();
  delay(100);
  if (p2 != 0) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread wait ");
  id3 = threads.addThread(test_fun1, 1);
  int time = millis();
  int r = threads.wait(id3);
  delay(100);
  time = millis() - time;
  if (r==id3) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test thread wait time ");
  if (time > 1000 && time < 2000) Serial.println("ok");
  else Serial.println("***FAIL***");

  Serial.print("Test std::thread scope ");
  {
    std::thread th2(test_fun3);
    delay(100);
  }
  delay(10);
  save_p = p3;
  delay(100);
  if (p3 != 0 && p3 == save_p) Serial.println("ok"); 
  else Serial.println("***FAIL***");
}

void loop() {
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
