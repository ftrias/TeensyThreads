/*
 * Threads-asm.S - Library for threading on the Teensy.
 * Copyright 2017 by Fernando Trias. All rights reserved.
 *
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

  .syntax unified
  .text
  .align  4
  .global context_switch
  .thumb
  .thumb_func
  .type   context_switch, %function
  .extern loadNextThread
context_switch:

  // Disable all interrupts; if we get interrupted during a context switch this
  // could corrupt the system
  CPSID if

  // Did we interrupt another interrupt? If so, don't switch. Switching would
  // wreck the system. In theory, we could reschedule the switch until the
  // other interrupt is done. Or we could do a more sophisticated switch, but the
  // easiest thing is to just ignore this condition.
  CMP lr, #0xFFFFFFF1      // This means caller was an interrupt
  BNE call_ok
  cpsie if                 // Re-enable interrupts
  BX lr                    // Exit to caller
  call_ok:
  
  // Save the r4-r11 registers; (r0-r3,r12 are saved by the interrupt handler).
  // Most thread libraries save this to the thread stack. I don't for simplicity
  // and to make debugging easier. Since the Teensy doesn't have a debugging port,
  // it's hard to examine the stack so this is easier.
  LDR r0, =currentSave         // get the address of the pointer
  LDR r0, [r0]                 // get the pointer itself
  STMIA r0, {r4-r11}           // save r4-r11

  // Are we running on thread 0, which is MSP?
  // It so, there is no need to save the stack pointer because MSP is never changed.
  // If not, save the stack pointer
  LDR r0, =currentMSP          // get the address of the variable
  LDR r0, [r0]                 // get value from address
  CMP r0, #0                   // it is 0? This means it's PSP
  BNE current_is_msp           // Not 0, so MSP, we can skip saving
  MRS r0, psp                  // get the PSP value
  LDR r1, =currentSP           // get the address of our save variable
  STR r0, [r1]                 // and store the PSP value there
  current_is_msp:

  bl loadNextThread;           // set the state to next running thread

  // Restore the r4-r11 registers from the saved thread
  LDR r0, =currentSave         // get address of pointer save buffer
  LDR r0, [r0]                 // get the actual pointer
  LDMIA r0, {r4-r11}           // and restore r4-r11 from save buffer

  // Setting LR causes the handler to switch MSP/PSP as necessary
  // Switching to MSP? no need to restore MSP
  LDR r0, =currentMSP          // get address of the variable
  LDR r0, [r0]                 // get the actual value
  CMP r0, #0                   // is it 0? Then it's PSP
  BEQ current_is_psp           // It's 0, so it's PSP
  MOV lr, #0xFFFFFFF9          // set context switch to MSP
  b to_exit
current_is_psp:
  LDR r0, =currentSP           // get address of stack pointer
  LDR r0, [r0]                 // get the actual value
  MSR psp, r0                  // save it to PSP
  MOV lr, #0xFFFFFFFD          // set context switch to PSP

to_exit:
  // re-enable interrupts
  CPSIE if
  // Return. The CPU will change MSP/PSP as needed based on LR
  BX lr
