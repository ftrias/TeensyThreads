/*
 * Threads.h - Library for threading on the Teensy.
 *
 *******************
 * 
 * Copyright 2017 by Fernando Trias.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
 * and associated documentation files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, 
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or 
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *******************
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
 *   void thread_func(int data){
 *     while(1) count++;
 *   }
 *
 *   void setup() {
 *     threads.addThread(thread_func, 0);
 *   }
 *
 *   void loop() {
 *     Serial.print(count);
 *   }
 *
 * Alternatively, you can use the std::threads class defined
 * by C++11
 *
 *   #include <Threads.h>
 *
 *   volatile int count = 0;
 *
 *   void thread_func(){
 *     while(1) count++;
 *   }
 *
 *   void setup() {
 *     std::thead th1(thread_func);
 *     th1.detach();
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

/* Enabling debugging information allows access to:
 *   getCyclesUsed()
 */
// #define DEBUG

extern "C" {
  void context_switch(void);
  void context_switch_direct(void);
  void context_switch_pit_isr(void);
  void loadNextThread();
  void stack_overflow_isr(void);
  void threads_svcall_isr(void);
  void threads_systick_isr(void);
}

// The stack frame saved by the interrupt
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

// The stack frame saved by the context switch
typedef struct {
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
  uint32_t lr;
#ifdef __ARM_PCS_VFP
  uint32_t s0;
  uint32_t s1;
  uint32_t s2;
  uint32_t s3;
  uint32_t s4;
  uint32_t s5;
  uint32_t s6;
  uint32_t s7;
  uint32_t s8;
  uint32_t s9;
  uint32_t s10;
  uint32_t s11;
  uint32_t s12;
  uint32_t s13;
  uint32_t s14;
  uint32_t s15;
  uint32_t s16;
  uint32_t s17;
  uint32_t s18;
  uint32_t s19;
  uint32_t s20;
  uint32_t s21;
  uint32_t s22;
  uint32_t s23;
  uint32_t s24;
  uint32_t s25;
  uint32_t s26;
  uint32_t s27;
  uint32_t s28;
  uint32_t s29;
  uint32_t s30;
  uint32_t s31;
  uint32_t fpscr;
#endif
} software_stack_t;

// The state of each thread (including thread 0)
class ThreadInfo {
  public:
    int stack_size;
    uint8_t *stack=0;
    int my_stack = 0;
    software_stack_t save;
    volatile int flags = 0;
    void *sp;
    int ticks;
#ifdef DEBUG
    unsigned long cyclesStart;  // On T_4 the CycCnt is always active - on T_3.x it currently is not - unless Audio starts it AFAIK
    unsigned long cyclesAccum;
#endif
};

extern "C" void unused_isr(void);

typedef void (*ThreadFunction)(void*);
typedef void (*ThreadFunctionInt)(int);
typedef void (*ThreadFunctionNone)();

typedef void (*IsrFunction)();

/*
 * Threads handles all the threading interaction with users. It gets
 * instantiated in a global variable "threads".
 */
class Threads {
public:
  // The maximum number of threads is hard-coded to simplify
  // the implementation. See notes of ThreadInfo.
  static const int MAX_THREADS = 8;
  int DEFAULT_STACK_SIZE = 1024;
  const int DEFAULT_STACK0_SIZE = 10240; // estimate for thread 0?
  int DEFAULT_TICKS = 10;
  static const int DEFAULT_TICK_MICROSECONDS = 100;

  // State of threading system
  static const int STARTED = 1;
  static const int STOPPED = 2;
  static const int FIRST_RUN = 3;

  // State of individual threads
  static const int EMPTY = 0;
  static const int RUNNING = 1;
  static const int ENDED = 2;
  static const int ENDING = 3;
  static const int SUSPENDED = 4;

  static const int SVC_NUMBER = 0x21;
  static const int SVC_NUMBER_ACTIVE = 0x22;

protected:
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
  ThreadInfo *threadp[MAX_THREADS];
  // This used to be allocated statically, as below. Kept for reference in case of bugs.
  // ThreadInfo thread[MAX_THREADS];

public: // public for debugging
  static IsrFunction save_systick_isr;
  static IsrFunction save_svcall_isr;

public:
  Threads();

  // Create a new thread for function "p", passing argument "arg". If stack is 0,
  // stack allocated on heap. Function "p" has form "void p(void *)".
  int addThread(ThreadFunction p, void * arg=0, int stack_size=-1, void *stack=0);
  // For: void f(int)
  int addThread(ThreadFunctionInt p, int arg=0, int stack_size=-1, void *stack=0) {
    return addThread((ThreadFunction)p, (void*)arg, stack_size, stack);
  }
  // For: void f()
  int addThread(ThreadFunctionNone p, int arg=0, int stack_size=-1, void *stack=0) {
    return addThread((ThreadFunction)p, (void*)arg, stack_size, stack);
  }

  // Get the state; see class constants. Can be EMPTY, RUNNING, etc.
  int getState(int id);
  // Explicityly set a state. See getState(). Call with care.
  int setState(int id, int state);
  // Wait until thread returns up to timeout_ms milliseconds. If ms is 0, wait
  // indefinitely.
  int wait(int id, unsigned int timeout_ms = 0);
  // Permanently stop a running thread. Thread will end on the next thread slice tick.
  int kill(int id);
  // Suspend a thread (on the next slice tick). Can be restarted with restart().
  int suspend(int id);
  // Restart a suspended thread.
  int restart(int id);
  // Set the slice length time in ticks for a thread (1 tick = 1 millisecond, unless using MicroTimer)
  void setTimeSlice(int id, unsigned int ticks);
  // Set the slice length time in ticks for all new threads (1 tick = 1 millisecond, unless using MicroTimer)
  void setDefaultTimeSlice(unsigned int ticks);
  // Set the stack size for new threads in bytes
  void setDefaultStackSize(unsigned int bytes_size);
  // Use the microsecond timer provided by IntervalTimer & PIT; instead of 1 tick = 1 millisecond,
  // 1 tick will be the number of microseconds provided (default is 100 microseconds)
  int setMicroTimer(int tick_microseconds = DEFAULT_TICK_MICROSECONDS);
  // Simple function to set each time slice to be 'milliseconds' long
  int setSliceMillis(int milliseconds);
  // Set each time slice to be 'microseconds' long
  int setSliceMicros(int microseconds);

  // Get the id of the currently running thread
  int id();
  int getStackUsed(int id);
  int getStackRemaining(int id);
#ifdef DEBUG
  unsigned long getCyclesUsed(int id);
#endif

  // Yield current thread's remaining time slice to the next thread, causing immediate
  // context switch
  void yield();
  // Wait for milliseconds using yield(), giving other slices your wait time
  void delay(int millisecond);

  // Start/restart threading system; returns previous state: STARTED, STOPPED, FIRST_RUN
  // can pass the previous state to restore
  int start(int old_state = -1);
  // Stop threading system; returns previous state: STARTED, STOPPED, FIRST_RUN
  int stop();

  // Allow these static functions and classes to access our members
  friend void context_switch(void);
  friend void context_switch_direct(void);
  friend void context_pit_isr(void);
  friend void threads_systick_isr(void);
  friend void threads_svcall_isr(void);
  friend void loadNextThread();
  friend class ThreadLock;

protected:
  void getNextThread();
  void *loadstack(ThreadFunction p, void * arg, void *stackaddr, int stack_size);
  static void force_switch_isr();

private:
  static void del_process(void);
  void yield_and_start();

public:
  class Mutex {
  private:
    volatile int state = 0;
    volatile int waitthread = -1;
    volatile int waitcount = 0;
  public:
    int getState(); // get the lock state; 1=locked; 0=unlocked
    int lock(unsigned int timeout_ms = 0); // lock, optionally waiting up to timeout_ms milliseconds
    int try_lock(); // if lock available, get it and return 1; otherwise return 0
    int unlock();   // unlock if locked
  };

  class Scope {
  private:
    Mutex *r;
  public:
    Scope(Mutex& m) { r = &m; r->lock(); }
    ~Scope() { r->unlock(); }
  };

  class Suspend {
  private:
    int save_state;
  public:
    Suspend();      // Stop threads and save thread state
    ~Suspend();     // Restore saved state
  };

  template <class C> class GrabTemp {
    private:
      Mutex *lkp;
    public:
      C *me;
      GrabTemp(C *obj, Mutex *lk) { me = obj; lkp=lk; lkp->lock(); }
      ~GrabTemp() { lkp->unlock(); }
      C &get() { return *me; }
  };

  template <class T> class Grab {
    private:
      Mutex lk;
      T *me;
    public:
      Grab(T &t) { me = &t; }
      GrabTemp<T> grab() { return GrabTemp<T>(me, &lk); }
      operator T&() { return grab().get(); }
      T *operator->() { return grab().me; }
      Mutex &getLock() { return lk; }
  };

#define ThreadWrap(OLDOBJ, NEWOBJ) Threads::Grab<decltype(OLDOBJ)> NEWOBJ(OLDOBJ);
#define ThreadClone(NEWOBJ) (NEWOBJ.grab().get())

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
  private:
    int id;          // internal thread id
    int destroy;     // flag to kill thread on instance destruction
  public:
    // By casting all (args...) to (void*), if there are more than one args, the compiler
    // will fail to find a matching function. This fancy template just allows any kind of
    // function to match.
    template <class F, class ...Args> explicit thread(F&& f, Args&&... args) {
      id = threads.addThread((ThreadFunction)f, (void*)args...);
      destroy = 1;
    }
    // If thread has not been detached when destructor called, then thread must end
    ~thread() {
      if (destroy) threads.kill(id);
    }
    // Threads are joinable until detached per definition, but in this implementation
    // that's not so. We emulate expected behavior anyway.
    bool joinable() { return destroy==1; }
    // Once detach() is called, thread runs until it terminates; otherwise it terminates
    // when destructor called.
    void detach() { destroy = 0; }
    // In theory, the thread merges with the running thread; if we just wait until
    // termination, it's basically the same thing except it's slower because
    // there are two threads running instead of one. Close enough.
    void join() { threads.wait(id); }
    // Get the unique thread id.
    int get_id() { return id; }
  };

  class mutex {
    private:
      Threads::Mutex mx;
    public:
      void lock() { mx.lock(); }
      bool try_lock() { return mx.try_lock(); }
      void unlock() { mx.unlock(); }
  };

  template <class cMutex> class lock_guard {
    private:
      cMutex *r;
    public:
      explicit lock_guard(cMutex& m) { r = &m; r->lock(); }
      ~lock_guard() { r->unlock(); }
  };
}

#endif
