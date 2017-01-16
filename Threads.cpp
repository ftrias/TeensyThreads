/*
  Threads.cpp - Library for threading on the Teensy.
  Created by Fernando Trias, January 2017.
  Copyright 2017 by Fernando Trias. All rights reserved.
*/
#include "Threads.h"

Threads threads;

// These variables are used buy the assembly context_switch function.
// They are copies or pointers to data in Threads and ThreadInfo
// and put here seperately in order to simplify the code.
extern "C" {
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
  currentThread = thread;
  currentSave = &thread[0].save;
  currentMSP = 1;
  currentSP = 0;
}

/*
 * start() - Begin threading
 */
int Threads::start() {
  __asm volatile("CPSID I");
  int old_state = thread_active;
  thread_active = STARTED;
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
  // Find the next open thread
  do {
    current_thread++;
    if (current_thread >= MAX_THREADS) {
      current_thread = 0; // thread 0 is MSP; always active so return
      break;
    }
  }
  while (thread[current_thread].flags == 0);
  currentThread = &thread[current_thread];
  currentSave = &thread[current_thread].save;
  currentMSP = (current_thread==0?1:0);
  currentSP = thread[current_thread].sp;
}

/* 
 * Replace the SysTick interrupt for our context switching. Note that
 * this function is "naked" meaning it does not save it's registers
 * on the stack. This is so we can preserve the stack of the caller.
 *
 * Interrupts will save r0-r4 in the stack and since it's simple
 * and short, it should only use those registers.
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

/*
 * del_process() - This is called when the task returns
 *
 * Turns thread off. Thread continues running until next call to
 * context_switch() at which point it all stops. The while(1) statement
 * just stalls until such time.
 */
void Threads::del_process(void){
  threads.thread[threads.current_thread].flags = 0; //clear the flags so thread can stop and be reused
  threads.thread_count--;
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
  thread[0].flags = 1;
  for (int i=1; i < MAX_THREADS; i++) {
    if (thread[i].flags == 0) { // free thread
      if (thread[i].stack) {
        delete thread[i].stack;
      }
      if (stack==0) {
        stack = new uint8_t[stack_size];
      }
      thread[i].stack = (uint8_t*)stack;
      thread[i].stack_size = stack_size;
      void *psp = loadstack(p, arg, thread[i].stack, thread[i].stack_size);
      thread[i].sp = psp;
      thread[i].flags = 1;
      thread_active = old_state;
      thread_count++;
      if (old_state == STARTED || old_state == FIRST_RUN) start();
      return i;
    }
  }
  if (old_state == STARTED) start();
  return -1;
}

/*
 * On creation, stop threading and save state
 */
ThreadLock::ThreadLock() {
  __asm volatile("CPSID I");
  save_state = threads.thread_active;
  threads.thread_active = 0;
  __asm volatile("CPSIE I");
}

/*
 * On destruction, restore threading state
 */
ThreadLock::~ThreadLock() {
  __asm volatile("CPSID I");  
  threads.thread_active = save_state;
  __asm volatile("CPSIE I");
}