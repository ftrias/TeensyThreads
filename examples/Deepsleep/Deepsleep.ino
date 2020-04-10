//quick n dirty teensythreads deepSleep test with Snooze

#include <Snooze.h>
#include <SnoozeBlock.h>
#include <Arduino.h>

#include "TeensyThreads.h"
SnoozeTimer timer;
SnoozeBlock config(timer);

const int LED = 13;
const int UserLED = 2;

//your sleeping funcion
int enter_sleep(int ms) {
  timer.setTimer(ms);//set sleep time in milliseconds  
  Snooze.hibernate( config ); //go to actual sleep
  //additional, one can use the RTC or a low power timer
  //to calculate actual time spent asleep and return the
  //value to the scheduler to calculate better times.
  return ms;
}

void heartbeat() {
  while (1) {
    threads.sleep(3000);
    digitalWriteFast(LED, !digitalRead(LED));
  }
}

void fastbeat() {
  while (1) {
    threads.sleep(1555);
    digitalWriteFast(UserLED, !digitalRead(UserLED));
  }
}

void setup() {
  pinMode(2, OUTPUT);
  digitalWriteFast(2, LOW); 
  pinMode(LED, OUTPUT);
  digitalWriteFast(LED, LOW);
  delay(2000);
  threads.addThread(heartbeat);
  threads.addThread(fastbeat);
  while(1) {
    threads.idle();
    //custom infinite loop
  }
}
void loop() {
  //i prefer to prevent the main loop from running as it causes CPU overhead because USB and other event handlers..
}
