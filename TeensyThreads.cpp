/*
 * Threads.cpp - Library for threading on the Teensy.
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
 */
#include "TeensyThreads.h"
#include <Arduino.h>

#ifndef __IMXRT1062__

#include <IntervalTimer.h>
IntervalTimer context_timer;

#endif

Threads threads;

unsigned int time_start;
unsigned int time_end;

#define __flush_cpu() __asm__ volatile("DMB");

// These variables are used by the assembly context_switch() function.
// They are copies or pointers to data in Threads and ThreadInfo
// and put here seperately in order to simplify the code.
extern "C" {
  int currentUseSystick;      // using Systick vs PIT/GPT
  int currentActive;          // state of the system (first, start, stop)
  int currentCount;
  ThreadInfo *currentThread;  // the thread currently running
  void *currentSave;
  int currentMSP;             // Stack pointers to save
  void *currentSP;
  void loadNextThread() {
    threads.getNextThread();
  }
}

extern "C" void stack_overflow_default_isr() { 
  currentThread->flags = Threads::ENDED;
}
extern "C" void stack_overflow_isr(void)       __attribute__ ((weak, alias("stack_overflow_default_isr")));

extern unsigned long _estack;   // the main thread 0 stack

// static void threads_svcall_isr(void);
// static void threads_systick_isr(void);

IsrFunction Threads::save_systick_isr;
IsrFunction Threads::save_svcall_isr;

/*
 * Teensy 3:
 * Replace the SysTick interrupt for our context switching. Note that
 * this function is "naked" meaning it does not save it's registers
 * on the stack. This is so we can preserve the stack of the caller.
 *
 * Interrupts will save r0-r4 in the stack and since this function
 * is short and simple, it should only use those registers. In the
 * future, this should be coded in assembly to make sure.
 */
extern volatile uint32_t systick_millis_count;
extern "C" void systick_isr();
void __attribute((naked, noinline)) threads_systick_isr(void)
{
  if (Threads::save_systick_isr) {
    asm volatile("push {r0-r4,lr}");
    (*Threads::save_systick_isr)();
    asm volatile("pop {r0-r4,lr}");
  }

  // TODO: Teensyduino 1.38 calls MillisTimer::runFromTimer() from SysTick
  if (currentUseSystick) {
    // we branch in order to preserve LR and the stack
    __asm volatile("b context_switch");
  }
  __asm volatile("bx lr");
}

void __attribute((naked, noinline)) threads_svcall_isr(void)
{
  if (Threads::save_svcall_isr) {
    asm volatile("push {r0-r4,lr}");
    (*Threads::save_svcall_isr)();
    asm volatile("pop {r0-r4,lr}");
  }

  // Get the right stack so we can extract the PC (next instruction)
  // and then see the SVC calling instruction number
  __asm volatile("TST lr, #4 \n"
                 "ITE EQ \n"
                 "MRSEQ r0, msp \n"
                 "MRSNE r0, psp \n");
  register unsigned int *rsp __asm("r0");
  unsigned int svc = ((uint8_t*)rsp[6])[-2];
  if (svc == Threads::SVC_NUMBER) {
    __asm volatile("b context_switch_direct");
  }
  else if (svc == Threads::SVC_NUMBER_ACTIVE) {
    currentActive = Threads::STARTED;
    __asm volatile("b context_switch_direct_active");
  }
  __asm volatile("bx lr");
}

#ifdef __IMXRT1062__

/*
 * 
 * Teensy 4:
 * Use unused GPT timers for context switching
 */

extern "C" void unused_interrupt_vector(void);

static void __attribute((naked, noinline)) gpt1_isr() {
  GPT1_SR |= GPT_SR_OF1;  // clear set bit
  __asm volatile ("dsb"); // see github bug #20 by manitou48
  __asm volatile("b context_switch");
}

static void __attribute((naked, noinline)) gpt2_isr() {
  GPT2_SR |= GPT_SR_OF1;  // clear set bit
  __asm volatile ("dsb"); // see github bug #20 by manitou48
  __asm volatile("b context_switch");
}

bool gtp1_init(unsigned int microseconds)
{
  // Initialization code derived from @manitou48.
  // See https://github.com/manitou48/teensy4/blob/master/gpt_isr.ino
  // See https://forum.pjrc.com/threads/54265-Teensy-4-testing-mbed-NXP-MXRT1050-EVKB-(600-Mhz-M7)?p=193217&viewfull=1#post193217

  // keep track of which GPT timer we are using
  static int gpt_number = 0;

  // not configured yet, so find an inactive GPT timer
  if (gpt_number == 0) {
    if (! NVIC_IS_ENABLED(IRQ_GPT1)) {
      attachInterruptVector(IRQ_GPT1, &gpt1_isr);
      NVIC_SET_PRIORITY(IRQ_GPT1, 255);
      NVIC_ENABLE_IRQ(IRQ_GPT1);
      gpt_number = 1;
    }
    else if (! NVIC_IS_ENABLED(IRQ_GPT2)) {
      attachInterruptVector(IRQ_GPT2, &gpt2_isr);
      NVIC_SET_PRIORITY(IRQ_GPT2, 255);
      NVIC_ENABLE_IRQ(IRQ_GPT2);
      gpt_number = 2;
    }
    else {
      // if neither timer is free, we fail
      return false;
    }
  }

  switch (gpt_number) {
    case 1:
      CCM_CCGR1 |= CCM_CCGR1_GPT(CCM_CCGR_ON) ;  // enable GPT1 module
      GPT1_CR = 0;                   // disable timer
      GPT1_PR = 23;                  // prescale: divide by 24 so 1 tick = 1 microsecond at 24MHz
      GPT1_OCR1 = microseconds - 1;  // compare value
      GPT1_SR = 0x3F;                // clear all prior status
      GPT1_IR = GPT_IR_OF1IE;        // use first timer
      GPT1_CR = GPT_CR_EN | GPT_CR_CLKSRC(1) ; // set to peripheral clock (24MHz)
      break;
    case 2:
      CCM_CCGR1 |= CCM_CCGR1_GPT(CCM_CCGR_ON) ;  // enable GPT1 module
      GPT2_CR = 0;                   // disable timer
      GPT2_PR = 23;                  // prescale: divide by 24 so 1 tick = 1 microsecond at 24MHz
      GPT2_OCR1 = microseconds - 1;  // compare value
      GPT2_SR = 0x3F;                // clear all prior status
      GPT2_IR = GPT_IR_OF1IE;        // use first timer
      GPT2_CR = GPT_CR_EN | GPT_CR_CLKSRC(1) ; // set to peripheral clock (24MHz)
      break;
    default:
      return false;
  }

  return true;
}

#endif

Threads::Threads() : current_thread(0), thread_count(0), thread_error(0) {
  // initilize thread slots to empty
  for(int i=0; i<MAX_THREADS; i++) {
    threadp[i] = NULL;
  }
  // fill thread 0, which is always running
  threadp[0] = new ThreadInfo();

  // initialize context_switch() globals from thread 0, which is MSP and always running
  currentThread = threadp[0];        // thread 0 is active
  currentSave = &threadp[0]->save;
  currentMSP = 1;
  currentSP = 0;
  currentCount = Threads::DEFAULT_TICKS;
  currentActive = FIRST_RUN;
  threadp[0]->flags = RUNNING;
  threadp[0]->ticks = DEFAULT_TICKS;
  threadp[0]->stack = (uint8_t*)&_estack - DEFAULT_STACK0_SIZE;
  threadp[0]->stack_size = DEFAULT_STACK0_SIZE;

#ifdef __IMXRT1062__

  // commandeer SVCall & use GTP1 Interrupt
  save_svcall_isr = _VectorsRam[11];
  if (save_svcall_isr == unused_interrupt_vector) save_svcall_isr = 0;
  _VectorsRam[11] = threads_svcall_isr;

  currentUseSystick = 0; // disable Systick calls
  gtp1_init(1000);       // tick every millisecond

#else

  currentUseSystick = 1;

  // commandeer the SVCall & SysTick Exceptions
  save_svcall_isr = _VectorsRam[11];
  if (save_svcall_isr == unused_isr) save_svcall_isr = 0;
  _VectorsRam[11] = threads_svcall_isr;
  
  save_systick_isr = _VectorsRam[15];
  if (save_systick_isr == unused_isr) save_systick_isr = 0;
  _VectorsRam[15] = threads_systick_isr;

#ifdef DEBUG
#if defined(__MK20DX256__) || defined(__MK20DX128__)
  ARM_DEMCR |= ARM_DEMCR_TRCENA; // Make ssure Cycle Counter active
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
#endif
#endif

#endif
}

/*
 * start() - Begin threading
 */
int Threads::start(int prev_state) {
  __disable_irq();
  int old_state = currentActive;
  if (prev_state == -1) prev_state = STARTED;
  currentActive = prev_state;
  __enable_irq();
  return old_state;
}

/*
 * stop() - Stop threading, even if active.
 *
 * If threads have already started, this should be called sparingly
 * because it could destabalize the system if thread 0 is stopped.
 */
int Threads::stop() {
  __disable_irq();
  int old_state = currentActive;
  currentActive = STOPPED;
  __enable_irq();
  return old_state;
}

/*
 * getNextThread() - Find next running thread
 *
 * This will also set the context_switcher() state variables
 */
void Threads::getNextThread() {

#ifdef DEBUG
  // Keep track of the number of cycles expended by each thread.
  // See @dfragster: https://forum.pjrc.com/threads/41504-Teensy-3-x-multithreading-library-first-release?p=213086#post213086
  currentThread->cyclesAccum += ARM_DWT_CYCCNT - currentThread->cyclesStart;
#endif

  // First, save the currentSP set by context_switch
  currentThread->sp = currentSP;

  // did we overflow the stack (don't check thread 0)?
  // allow an extra 8 bytes for a call to the ISR and one additional call or variable
  if (current_thread && ((uint8_t*)currentThread->sp - currentThread->stack <= 8)) {
    stack_overflow_isr();
  }

  // Find the next running thread
  while(1) {
    current_thread++;
    if (current_thread >= MAX_THREADS) {
      current_thread = 0; // thread 0 is MSP; always active so return
      break;
    }
    if (threadp[current_thread] && threadp[current_thread]->flags == RUNNING) break;
  }
  currentCount = threadp[current_thread]->ticks;

  currentThread = threadp[current_thread];
  currentSave = &threadp[current_thread]->save;
  currentMSP = (current_thread==0?1:0);
  currentSP = threadp[current_thread]->sp;

#ifdef DEBUG
  currentThread->cyclesStart = ARM_DWT_CYCCNT;
#endif
}

/*
 * Empty placeholder for IntervalTimer class
 */
static void context_pit_empty() {}

/*
 * Store the PIT timer flag register for use in assembly
 */
volatile uint32_t *context_timer_flag;

/*
 * Defined in assembly code
 */
extern "C" void context_switch_pit_isr();

/*
 * Stop using the SysTick interrupt and start using
 * the IntervalTimer timer. The parameter is the number of microseconds
 * for each tick.
 */
int Threads::setMicroTimer(int tick_microseconds)
{
#ifdef __IMXRT1062__

  gtp1_init(tick_microseconds);

#else

/*
 * Implementation strategy suggested by @tni in Teensy Forums; see
 * https://forum.pjrc.com/threads/41504-Teensy-3-x-multithreading-library-first-release
 */

  // lowest priority so we don't interrupt other interrupts
  context_timer.priority(255);
  // start timer with dummy fuction
  if (context_timer.begin(context_pit_empty, tick_microseconds) == 0) {
    // failed to set the timer!
    return 0;
  }
  currentUseSystick = 0; // disable Systick calls

  // get the PIT number [0-3] (IntervalTimer overrides IRQ_NUMBER_t op)
  int number = (IRQ_NUMBER_t)context_timer - IRQ_PIT_CH0;
  // calculate number of uint32_t per PIT; should be 4.
  // Not hard-coded in case this changes in future CPUs.
  const int width = (PIT_TFLG1 - PIT_TFLG0) / sizeof(uint32_t);
  // get the right flag to ackowledge PIT interrupt
  context_timer_flag = &PIT_TFLG0 + (width * number);
  attachInterruptVector(context_timer, context_switch_pit_isr);

#endif

  return 1;
}

/*
 * Set each time slice to be 'microseconds' long
 */
int Threads::setSliceMicros(int microseconds)
{
  setMicroTimer(microseconds);
  setDefaultTimeSlice(1);
  return 1;
}

/*
 * Set each time slice to be 'milliseconds' long
 */
int Threads::setSliceMillis(int milliseconds)
{
  if (currentUseSystick) {
    setDefaultTimeSlice(milliseconds);
  }
  else {
    // if we're using the PIT, we should probably really disable it and
    // re-establish the systick timer; but this is easier for now
    setSliceMicros(milliseconds * 1000);
  }
  return 1;
}

/*
 * del_process() - This is called when the task returns
 *
 * Turns thread off. Thread continues running until next call to
 * context_switch() at which point it all stops. The while(1) statement
 * just stalls until such time.
 */
void Threads::del_process(void)
{
  int old_state = threads.stop();
  ThreadInfo *me = threads.threadp[threads.current_thread];
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
    if (threadp[i] == NULL) { // empty thread, so fill it
      threadp[i] = new ThreadInfo();
    }
    if (threadp[i]->flags == ENDED || threadp[i]->flags == EMPTY) { // free thread
      ThreadInfo *tp = threadp[i]; // working on this thread
      if (tp->stack && tp->my_stack) {
        delete[] tp->stack;
      }
      if (stack==0) {
        stack = new uint8_t[stack_size];
        tp->my_stack = 1;
      }
      else {
        tp->my_stack = 0;
      }
      tp->stack = (uint8_t*)stack;
      tp->stack_size = stack_size;
      void *psp = loadstack(p, arg, tp->stack, tp->stack_size);
      tp->sp = psp;
      tp->ticks = DEFAULT_TICKS;
      tp->flags = RUNNING;
      tp->save.lr = 0xFFFFFFF9;

#ifdef DEBUG
      tp->cyclesStart = ARM_DWT_CYCCNT;
      tp->cyclesAccum = 0;
#endif

      currentActive = old_state;
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
  return threadp[id]->flags;
}

int Threads::setState(int id, int state)
{
  threadp[id]->flags = state;
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
    state = threadp[id]->flags;
    if (state != RUNNING) break;
    yield();
  }
  return id;
}

int Threads::kill(int id)
{
  threadp[id]->flags = ENDED;
  return id;
}

int Threads::suspend(int id)
{
  threadp[id]->flags = SUSPENDED;
  return id;
}

int Threads::restart(int id)
{
  threadp[id]->flags = RUNNING;
  return id;
}

void Threads::setTimeSlice(int id, unsigned int ticks)
{
  threadp[id]->ticks = ticks - 1;
}

void Threads::setDefaultTimeSlice(unsigned int ticks)
{
  DEFAULT_TICKS = ticks - 1;
}

void Threads::setDefaultStackSize(unsigned int bytes_size)
{
  DEFAULT_STACK_SIZE = bytes_size;
}

void Threads::yield() {
  __asm volatile("svc %0" : : "i"(Threads::SVC_NUMBER));
}

void Threads::yield_and_start() {
  __asm volatile("svc %0" : : "i"(Threads::SVC_NUMBER_ACTIVE));
}

void Threads::delay(int millisecond) {
  int mx = millis();
  while((int)millis() - mx < millisecond) yield();
}

int Threads::id() {
  volatile int ret;
  __disable_irq();
  ret = current_thread;
  __enable_irq();
  return ret;
}

int Threads::getStackUsed(int id) {
  return threadp[id]->stack + threadp[id]->stack_size - (uint8_t*)threadp[id]->sp;
}

int Threads::getStackRemaining(int id) {
  return (uint8_t*)threadp[id]->sp - threadp[id]->stack;
}

#ifdef DEBUG
unsigned long Threads::getCyclesUsed(int id) {
  stop();
  unsigned long ret = threadp[id]->cyclesAccum;
  start();
  return ret;
}
#endif

/*
 * On creation, stop threading and save state
 */
Threads::Suspend::Suspend() {
  __disable_irq();
  save_state = currentActive;
  currentActive = 0;
  __enable_irq();
}

/*
 * On destruction, restore threading state
 */
Threads::Suspend::~Suspend() {
  __disable_irq();
  currentActive = save_state;
  __enable_irq();
}

int Threads::Mutex::getState() {
  int p = threads.stop();
  int ret = state;
  threads.start(p);
  return ret;
}

int __attribute__ ((noinline)) Threads::Mutex::lock(unsigned int timeout_ms) {
  if (try_lock()) return 1; // we're good, so avoid more checks

  uint32_t start = systick_millis_count;
  while (1) {
    if (try_lock()) return 1;
    if (timeout_ms && (systick_millis_count - start > timeout_ms)) return 0;
    if (waitthread==-1) { // can hold 1 thread suspend until unlock
      int p = threads.stop();
      waitthread = threads.current_thread;
      waitcount = currentCount;
      threads.suspend(waitthread);
      threads.start(p);
    }
    threads.yield();
  }
  __flush_cpu();
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

int __attribute__ ((noinline)) Threads::Mutex::unlock() {
  int p = threads.stop();
  if (state==1) {
    state = 0;
    if (waitthread >= 0) { // reanimate a suspended thread waiting for unlock
      threads.restart(waitthread);
      waitthread = -1;
      __flush_cpu();
      threads.yield_and_start();
      return 1;
    }
  }
  __flush_cpu();
  threads.start(p);
  return 1;
}
