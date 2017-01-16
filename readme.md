Teensy Threading Library
===================================================

Copyright 2017 by Fernando Trias. All rights reserved.
Revision 1, January 2017

Overview
------------------------------

Teensy Threading Library uses the built-in threading support of the Cortex-M4
to implement basic threading for the Teensy 3.x platform.

Simple example
------------------------------

```C++
#include <Threads.h>

volatile int count = 0;

void thread_func(void * data){
  while(1) count++;
}

void setup() {
  threads.addThread(thread_func, 0);
}

void loop() {
  Serial.println(count);
}
```

Usage
-----------------------------

A global variable `threads` of `class Threads` is used to control the threading
action. The library is hard-coded to support 8 threads, but this may be changed
in the source code of Threads.cpp.

Threads are created by `threads.addThread()` with parameters:

```
addThread(func, arg, stack_size, stack)

  func : function to call to perform thread work. This has the form of
         "void f(void *arg)" or "void f(int arg)" or "void f()"
  arg  : the "arg" passed to "func" when it starts.
  stack_size: (optional) the size of the thread stack [1]
  stack : (optional) pointer to a buffer to use as stack [2]
  Returns an ID number or -1 for failure

  [1] If stack_size is 0 or missing, then 4096 is used
  [2] If stack is 0 or missing, then the buffer is allocated from the heap
```

All threads start immediately and run until the function terminates with
a return.

If a thread ends because the function returns, then the thread will be reused
by a new function.

std::thread minimal interface
-----------------------------

The header also supports the construction of minimal std::thread as indicated 
in C++11. See http://www.cplusplus.com/reference/thread/thread/

Example:

```C++
  std::thread first(thread_func);
```

Notes on implementation
-----------------------------

Threads take turns on the CPU and are switched by the `context_switch()` function.
This function is called by the SysTick ISR. The library overrides the default
`systick_isr()` to accomplish this. On the Teensy, by default, each tick is 
1 millisecond long.

The code comments give some explanation of the process:

```C
/*
 * context_switch() changes the context to a new thread. It follows this strategy:
 *
 * 1. Abort if called from within another interrupt
 * 2. Save registers r4-r11 to the current thread state
 * 3. If not running on MSP, save PSP to the current thread state
 * 4. Get the next running thread state
 * 5. Restore r4-r11 from thread state
 * 6. Set MSP or PSP depending on state
 * 7. Switch MSP/PSP on return
 *
 * Notes:
 * - Cortex-M has two stack pointers, MSP and PSP, which we alternate. See the 
 *   reference manual under the Exception Model section.
 * - I tried coding this in asm embedded in Threads.cpp but the compiler
 *   optimizations kept chaning my code and removing lines so I have to use
 *   a separate assembly file. But if you try it, make sure to declare the
 *   function "naked" so the stack pointer SP is not modified when called.
 *   This means you can't use local variables, which are stored in stack. 
 *   Also turn optimizations off using optimize("O0").
 * - Function is called from systick_isr (also naked) via a branch. Again, this is
 *   to preserve the stack and LR.
 * - Since Systick can be called from within another interrupt, for simplicity, we check
 *   for this and abort.
 * - Teensy uses MSP for it's main thread; we preserve that. Alternatively, we
 *   could have used PSP for all threads, including main, and reserve MSP for
 *   interrupts only. This would simplify the code slightly, but could introduce
 *   incompatabilities.
 */
```

Todo
-----------------------------

1. Implement yielding functionality so threads can give up their time slices
2. Implement a priority or time slice length for each thread
3. Time slices smaller than 1 millisecond
4. Check for stack overflow during context_change() to aid in debugging
5. Optimize assembly
6. Use a standard thread class interface, like the new C++11 std::thread
   or POSIX threads. See http://www.cplusplus.com/reference/thread/thread/