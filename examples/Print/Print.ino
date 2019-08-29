#include <TeensyThreads.h>

volatile long int count = 0;

void thread_func(int inc) {
  while(1) count += inc;
}

void setup() {
  threads.addThread(thread_func, 1);
}

void loop() {
  Serial.println(count);
  threads.delay(1000);
}
