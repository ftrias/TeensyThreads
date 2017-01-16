
/*
 * Threads.h - Library for threading on the Teensy.
 * Created by Fernando Trias, January 2017.
 * Copyright 2017 by Fernando Trias. All rights reserved.
 *
 * Multithreading library for Teensy board.
 * See Threads.cpp for explanation of internal functions.
 *
 * A global variable "threads" of type Threads will be created
 * to provide all threading functions. See example below:
 *
 *   #include <Threads.h>
 *
 *   volatile int count = 0;
 *
 *   void thread_func(void *data){
 *     while(1) count++;
 *   }
 *
 *   void setup() {
 *     threads.addThread(thread_func, 0);
 *     threads.start();
 *   }
 *
 *   void loop() {
 *     Serial.print(count);
 *   }
 *
 */

#ifndef _THREADS_H
#define _THREADS_H

#include <stdint.h>

extern "C" {
  void context_switch(void);
  void systick_isr(void);
  void loadNextThread();
}

/* 
 * ThreadLock is a simple function for starting/stopping
 * threading during critical sections by using scope. It stops threading
 * on creation and then restarts it on destruction when it goes
 * out of scope.
 *
 * usage example:
 *
 *   while(state == 1) {
 *     if (needupdate) {
 *       ThreadLock lock;
 *       updatedata++;
 *       errorcode = 15;
 *     }
 *   }
 *
 */
class ThreadLock {
private:
  int save_state;
public:
  ThreadLock();      // Stop threads and save thread state
  ~ThreadLock();     // Restore saved state
};

// The stack frame saved by th interrupt
typedef struct {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t xpsr;
} interrupt_stack_t;
 
// The stack frame saves by the context switch
typedef struct {
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
} software_stack_t;

// The state of each thread (including thread 0)
class ThreadInfo {
  public:
    int stack_size;
    uint8_t *stack=0;
    software_stack_t save;
    int flags = 0;
    void *sp;
};

typedef void (*ThreadFunction)(void*);
typedef void (*ThreadFunctionInt)(int);
typedef void (*ThreadFunctionNone)();

/*
 * Threads handles all the threading interaction with users. It gets
 * instantiated in a global variable "threads".
 */
class Threads {
public:
  // The maximum number of threads is hard-coded to simplify
  // the implementation. See notes of ThreadInfo.
  static const int MAX_THREADS = 8;
  static const int DEFAULT_STACK_SIZE = 4096;

  static const int STARTED = 1;
  static const int STOPPED = 2;
  static const int FIRST_RUN = 3;

protected:
  int thread_active;
  int current_thread;
  int thread_count;
  int thread_error;

  /* 
   * The maximum number of threads is hard-coded. Alternatively, we could implement
   * a linked list which would mean using up less memory for a small number of 
   * threads while allowing an unlimited number of possible threads. This would
   * probably not slow down thread switching too much, but it would introduce
   * complexity and possibly bugs. So to simplifiy for now, we use an array.
   * But in the future, a linked list might be more appropriate.
   */
  ThreadInfo thread[MAX_THREADS];

public:
  Threads();

  // Create a new thread for function "p", passing argument "arg". If stack is 0,
  // stack allocated on heap. Function "p" has form "void p(void *)".
  int addThread(ThreadFunction p, void * arg=0, int stack_size=DEFAULT_STACK_SIZE, void *stack=0);
  // For void f(int)
  int addThread(ThreadFunctionInt p, int arg=0, int stack_size=DEFAULT_STACK_SIZE, void *stack=0) {
    return addThread((ThreadFunction)p, (void*)arg, stack_size, stack);
  }
  // For void f()
  int addThread(ThreadFunctionNone p, int arg=0, int stack_size=DEFAULT_STACK_SIZE, void *stack=0) {
    return addThread((ThreadFunction)p, (void*)arg, stack_size, stack);
  }

  int start();        // Start threading
  int stop();        // Stop threading (see warnings in code)

  // Allow these static functions and classes to access our members
  friend void context_switch(void); 
  friend void systick_isr(void);
  friend void loadNextThread();
  friend class ThreadLock;

protected:
  void getNextThread();
  void *loadstack(ThreadFunction p, void * arg, void *stackaddr, int stack_size);

private:
  static void del_process(void);
};

extern Threads threads;

/* 
 * Rudimentary compliance to C++11 class
 * 
 * See http://www.cplusplus.com/reference/thread/thread/
 *
 * Example:   
 * int x; 
 * void thread_func() { x++; }
 * int main() {
 *   std::thread(thread_func);
 * }
 *
 */
namespace std {
  class thread {
  public:
    template <class F, class ...Args> explicit thread(F&& f, Args&&... args) {
      // by casting all (args...) to (void*), if there are more than one args, the compiler
      // will fail to find a matching function
      threads.addThread((ThreadFunction)f, (void*)args...);
    }
  };
}

#endif
