#include <TeensyThreads.h>

volatile unsigned long int count = 0;
volatile unsigned long int count2 = 0;


void thread_func(int inc) {
  while(1) count += inc;
}

void thread_func2(int inc) {
  while(1) count2 += inc;
}

void setup() {
  threads.addThread(thread_func, 1);
  threads.addThread(thread_func2, 2);
}

void loop() {
  Serial.print(count);
  Serial.print(",");
  Serial.println(count2);
  threads.delay(1000);
}
