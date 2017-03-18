/*
  Threads.cpp - Library for threading on the Teensy.
  Created by Fernando Trias, January 2017.
  Copyright 2017 by Fernando Trias. All rights reserved.
*/
#include "Threads.h"
#include <Arduino.h>

Threads threads;

// These variables are used by the assembly context_switch() function.
// They are copies or pointers to data in Threads and ThreadInfo
// and put here seperately in order to simplify the code.
extern "C" {
  int currentActive;
  int currentCount;
  ThreadInfo *currentThread;
  void *currentSave;
  int currentMSP;
  void *currentSP;
  void loadNextThread() {
    threads.getNextThread();
  }
}

Threads::Threads() : thread_active(FIRST_RUN), current_thread(0), thread_count(0), thread_error(0) {
  // initialize context_switch() globals from thread 0, which is MSP and always running
  currentThread = thread;        // thread 0 is active
  currentSave = &thread[0].save;
  currentMSP = 1;
  currentSP = 0;
  currentCount = Threads::DEFAULT_TICKS;
  currentActive = thread_active;
  thread[0].flags = RUNNING;
  thread[0].ticks = DEFAULT_TICKS;
}

/*
 * start() - Begin threading
 */
int Threads::start(int prev_state) {
  __asm volatile("CPSID I");
  int old_state = thread_active;
  if (prev_state == -1) prev_state = STARTED;
  thread_active = prev_state;
  __asm volatile("CPSIE I");  
  return old_state;
}

/*
 * stop() - Stop threading, even if active.
 *
 * If threads have already started, this should be called sparingly
 * because it could destabalize the system if thread 0 is stopped.
 */
int Threads::stop() {
  __asm volatile("CPSID I");
  int old_state = thread_active;
  thread_active = STOPPED;
  __asm volatile("CPSIE I");  
  return old_state; 
}

/*
 * getNextThread() - Find next running thread
 *
 * This will also set the context_switcher() state variables
 */
void Threads::getNextThread() {
  // First, save the currentSP set by context_switch
  thread[current_thread].sp = currentSP;
  // Find the next running thread
  do {
    current_thread++;
    if (current_thread >= MAX_THREADS) {
      current_thread = 0; // thread 0 is MSP; always active so return
      break;
    }
  }
  while (thread[current_thread].flags != RUNNING);
  currentThread = &thread[current_thread];
  currentSave = &thread[current_thread].save;
  currentMSP = (current_thread==0?1:0);
  currentSP = thread[current_thread].sp;
  currentCount = thread[current_thread].ticks;
  currentActive = thread_active;
}

/* 
 * Replace the SysTick interrupt for our context switching. Note that
 * this function is "naked" meaning it does not save it's registers
 * on the stack. This is so we can preserve the stack of the caller.
 *
 * Interrupts will save r0-r4 in the stack and since this function 
 * is short and simple, it should only use those registers.
 */
extern volatile uint32_t systick_millis_count;
void __attribute((naked)) systick_isr(void)
{
  systick_millis_count++;
  if (threads.thread_active == Threads::STARTED) { // switch only if active
    // we branch in order to preserve LR and the stack
    __asm volatile("b context_switch");
  }
  __asm volatile("bx lr");
}

void __attribute((naked)) svcall_isr(void)
{
  register int *rsp __asm("sp");
  int svc = ((uint8_t*)rsp[6])[-2];
  if (svc == 0x21) {
    __asm volatile("b context_switch_direct");
  }
  __asm volatile("bx lr");
}

/*
 * del_process() - This is called when the task returns
 *
 * Turns thread off. Thread continues running until next call to
 * context_switch() at which point it all stops. The while(1) statement
 * just stalls until such time.
 */
void Threads::del_process(void){
  int old_state = threads.stop();
  ThreadInfo *me = &threads.thread[threads.current_thread];
  // Would love to delete stack here but the thread doesn't
  // end now. It continues until the next tick.
  // if (me->my_stack) {
  //   delete[] me->stack;
  //   me->stack = 0;
  // }
  threads.thread_count--;
  me->flags = ENDED; //clear the flags so thread can stop and be reused
  threads.start(old_state);
  while(1); // just in case, keep working until context change when execution will not return to this thread
}

/*
 * Initializes a thread's stack. Called when thread is created
 */
void *Threads::loadstack(ThreadFunction p, void * arg, void *stackaddr, int stack_size)
{
  interrupt_stack_t * process_frame = (interrupt_stack_t *)((uint8_t*)stackaddr + stack_size - sizeof(interrupt_stack_t) - 8);
  process_frame->r0 = (uint32_t)arg;
  process_frame->r1 = 0;
  process_frame->r2 = 0;
  process_frame->r3 = 0;
  process_frame->r12 = 0;
  process_frame->lr = (uint32_t)Threads::del_process;
  process_frame->pc = ((uint32_t)p);
  process_frame->xpsr = 0x1000000;
  uint8_t *ret = (uint8_t*)process_frame;
  // ret -= sizeof(software_stack_t); // uncomment this if we are saving R4-R11 to the stack
  return (void*)ret;
}

/*
 * Add a new thread to the queue.
 *    add_thread(fund, arg)
 *
 *    fund : is a function pointer. The function prototype is:
 *           void *func(void *param)
 *    arg  : is a void pointer that is passed as the first parameter
 *           of the function. In the example above, arg is passed
 *           as param.
 *    stack_size : the size of the buffer pointed to by stack. If
 *           it is 0, then "stack" must also be 0. If so, the function
 *           will allocate the default stack size of the heap using new().
 *    stack : pointer to new data stack of size stack_size. If this is 0,
 *           then it will allocate a stack on the heap using new() of size
 *           stack_size. If stack_size is 0, a default size will be used.
 *    return: an integer ID to be used for other calls
 */
int Threads::addThread(ThreadFunction p, void * arg, int stack_size, void *stack)
{
  int old_state = stop();
  if (stack_size == -1) stack_size = DEFAULT_STACK_SIZE;
  for (int i=1; i < MAX_THREADS; i++) {
    if (thread[i].flags == ENDED || thread[i].flags == EMPTY) { // free thread
      if (thread[i].stack && thread[i].my_stack) {
        delete[] thread[i].stack;
      }
      if (stack==0) {
        stack = new uint8_t[stack_size];
        thread[i].my_stack = 1;
      }
      else {
        thread[i].my_stack = 0;
      }
      thread[i].stack = (uint8_t*)stack;
      thread[i].stack_size = stack_size;
      void *psp = loadstack(p, arg, thread[i].stack, thread[i].stack_size);
      thread[i].sp = psp;
      thread[i].ticks = DEFAULT_TICKS;
      thread[i].flags = RUNNING;
      thread[i].save.lr = 0xFFFFFFF9;
      thread_active = old_state;
      thread_count++;
      if (old_state == STARTED || old_state == FIRST_RUN) start();
      return i;
    }
  }
  if (old_state == STARTED) start();
  return -1;
}

int Threads::getState(int id)
{
  return thread[id].flags;
}

int Threads::setState(int id, int state)
{
  thread[id].flags = state;
  return state;
}

int Threads::wait(int id, unsigned int timeout_ms)
{
  unsigned int start = millis();
  // need to store state in temp volatile memory for optimizer.
  // "while (thread[id].flags != RUNNING)" will be optimized away
  volatile int state; 
  while (1) {
    if (timeout_ms != 0 && millis() - start > timeout_ms) return -1;
    state = thread[id].flags;
    if (state != RUNNING) break;
    yield();
  }
  return id;
}

int Threads::kill(int id)
{
  thread[id].flags = ENDED;
  return id;
}

int Threads::suspend(int id)
{
  thread[id].flags = SUSPENDED;
  return id;
}

int Threads::restart(int id)
{
  thread[id].flags = RUNNING;
  return id;
}

void Threads::setTimeSlice(int id, unsigned int ticks)
{
  thread[id].ticks = ticks - 1;
}

void Threads::setDefaultTimeSlice(unsigned int ticks)
{
  DEFAULT_TICKS = ticks - 1;
}

void Threads::setDefaultStackSize(unsigned int bytes)
{
  DEFAULT_STACK_SIZE = bytes;
}

void Threads::yield() {
  __asm volatile("svc 0x21");
}

void Threads::delay(int millisecond) {
  int mx = millis();
  while((int)millis() - mx < millisecond) yield();
}

int Threads::id() {
  volatile int ret;
  __asm volatile("CPSID I");
  ret = current_thread;
  __asm volatile("CPSIE I");
  return ret;
}

int Threads::getStackUsed(int id) {
  return thread[id].stack + thread[id].stack_size - (uint8_t*)thread[id].sp;
}
int Threads::getStackRemaining(int id) {
  return (uint8_t*)thread[id].sp - thread[id].stack;
}

/*
 * On creation, stop threading and save state
 */
Threads::Lock::Lock() {
  __asm volatile("CPSID I");
  save_state = threads.thread_active;
  threads.thread_active = 0;
  __asm volatile("CPSIE I");
}

/*
 * On destruction, restore threading state
 */
Threads::Lock::~Lock() {
  __asm volatile("CPSID I");  
  threads.thread_active = save_state;
  __asm volatile("CPSIE I");
}

int Threads::Mutex::getState() {
  int p = threads.stop();
  int ret = state;
  threads.start(p);
  return ret;
}

int Threads::Mutex::lock(unsigned int timeout_ms) {
  int p;
  uint32_t start = systick_millis_count;
  while (1) {
    p = try_lock();
    if (p) return 1;
    if (timeout_ms && (systick_millis_count - start > timeout_ms)) return 0;
    threads.yield();
  }
  return 0;
}

int Threads::Mutex::try_lock() {
  int p = threads.stop();
  if (state == 0) {
    state = 1;
    threads.start(p);
    return 1;
  }
  threads.start(p);
  return 0;
}

int Threads::Mutex::unlock() {
  int p = threads.stop();
  state = 0;
  threads.start(p);
  return 1;
}
