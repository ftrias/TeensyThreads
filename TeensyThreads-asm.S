/*
 * Threads-asm.S - Library for threading on the Teensy.
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
 * context_switch() changes the context to a new thread. It follows this strategy:
 *
 * 1. Abort if called from within an interrupt (unless using PIT)
 * 2. Save registers r4-r11 to the current thread state (s0-s31 for FPU)
 * 3. If not running on MSP, save PSP to the current thread state
 * 4. Get the next running thread state
 * 5. Restore r4-r11 from thread state (s0-s31 for FPU)
 * 6. Set MSP or PSP depending on state
 * 7. Switch MSP/PSP on return
 *
 * Notes:
 * - Cortex-M has two stack pointers, MSP and PSP, which we alternate. See the
 *   reference manual under the Exception Model section.
 * - I tried coding this in asm embedded in Threads.cpp but the compiler
 *   optimizations kept changing my code and removing lines so I have to use
 *   a separate assembly file. But if you try it, make sure to declare the
 *   function "naked" so the stack pointer SP is not modified when called.
 *   This means you can't use local variables, which are stored in stack.
 *   Try to turn optimizations off using optimize("O0") (which doesn't really
 *   turn off all optimizations).
 * - Function can be called from systick_isr() or from the PIT timer (implemented
 *   by IntervalTimer)
 * - If using systick, we override the default systick_isr() in order
 *   to preserve the stack and LR. If using PIT, we override the pitX_isr() for
 *   the same reason.
 * - Since Systick can be called from within another interrupt, for simplicity, we
 *   check for this and abort.
 * - Teensy uses MSP for it's main thread; we preserve that. Alternatively, we
 *   could have used PSP for all threads, including main, and reserve MSP for
 *   interrupts only. This would simplify the code slightly, but could introduce
 *   incompatabilities.
 * - If this interrupt is nested within another interrupt, all kinds of bad
 *   things can happen. This is especially true if usb_isr() is active. In theory
 *   we should be able to do a switch even within an interrupt, but in my
 *   tests, it would not work reliably.
 * - If using the PIT interrupt, it's priority is set to 255 (the lowest) so it
 *   cannot interrupt an interrupt.
 */

  .syntax unified
  .align  2
  .thumb

  .global context_switch_direct
  .thumb_func
context_switch_direct:
  CPSID I
  // Call here to force a context switch, so we skip checking the tick counter.
  B call_direct

  .global context_switch_direct_active
  .thumb_func
context_switch_direct_active:
  CPSID I
  // Call here to force a context switch, so we skip checking the tick counter.
  B call_direct_active

  .global context_switch_pit_isr
  .thumb_func
context_switch_pit_isr:
  CPSID I
  LDR r0, =context_timer_flag   // acknowledge the interrupt by
  LDR r0, [r0]                  // getting the pointer to the pointer
  MOVS r1, #1                   //
  STR r1, [r0]                  // and setting to 1
  B context_switch_check        // now go do the context switch

  .global context_switch
  .thumb_func
context_switch:

  // Disable all interrupts; if we get interrupted during a context switch this
  // could corrupt the system.
  CPSID I

  // Did we interrupt another interrupt? If so, don't switch. Switching would
  // wreck the system. In theory, we could reschedule the switch until the
  // other interrupt is done. Or we could do a more sophisticated switch, but the
  // easiest thing is to just ignore this condition.
  CMP lr, #0xFFFFFFF1      // this means we interrupted an interrupt
  BEQ to_exit              // so don't do anything until next time
  CMP lr, #0xFFFFFFE1      // this means we interrupted an interrupt with FPU
  BEQ to_exit              // so don't do anything until next time

context_switch_check:

  // Count down number of ticks we should stay in thread
  LDR r0, =currentCount    // get the tick count (address to variable)
  LDR r1, [r0]             // get the value from the address
  CMP r1, #0               // is it 0?
  BEQ call_direct          // if so, thread is done, so switch
  SUB r1, #1               // otherwise, subtract 1 tick
  STR r1, [r0]             // and put it back
  B to_exit                // and quit until next context_switch

call_direct:

  // Just do the context-switch even if it's not time
  LDR r0, =currentActive
  LDR r0, [r0]
  CMP r0, #1
  BNE to_exit

call_direct_active:

  // Save the r4-r11 registers; (r0-r3,r12 are saved by the interrupt handler).
  // Most thread libraries save this to the thread stack. I don't for simplicity
  // and to make debugging easier. Since the Teensy doesn't have a debugging port,
  // it's hard to examine the stack so this is easier.
  LDR r0, =currentSave         // get the address of the pointer
  LDR r0, [r0]                 // get the pointer itself
  STMIA r0!, {r4-r11,lr}       // save r4-r11 to buffer

#ifdef __ARM_PCS_VFP           // compile if using FPU
  VSTMIA r0!, {s0-s31}         // save all FPU registers
  VMRS r1, FPSCR               // and FPU app status register
  STMIA r0!, {r1}
#endif

  // Are we running on thread 0, which is MSP?
  // It so, there is no need to save the stack pointer because MSP is never changed.
  // If not, save the stack pointer.
  LDR r0, =currentMSP          // get the address of the variable
  LDR r0, [r0]                 // get value from address
  CMP r0, #0                   // it is 0? This means it's PSP
  BNE current_is_msp           // not 0, so MSP, we can skip saving SP
  MRS r0, psp                  // get the PSP value
  LDR r1, =currentSP           // get the address of our save variable
  STR r0, [r1]                 // and store the PSP value there
  current_is_msp:

  BL loadNextThread;           // set the state to next running thread

  // Restore the r4-r11 registers from the saved thread
  LDR r0, =currentSave         // get address of pointer save buffer
  LDR r0, [r0]                 // get the actual pointer
  LDMIA r0!, {r4-r11,lr}       // and restore r4-r11 & lr from save buffer

#ifdef __ARM_PCS_VFP           // compile if using FPU
  VLDMIA r0!, {s0-s31}         // restore all FPU registers
  LDMIA r0!, {r1}              // and the FP app status register
  VMSR FPSCR, r1
#endif

  // Setting LR causes the handler to switch MSP/PSP when returning.
  // Switching to MSP? no need to restore MSP.
  AND lr, lr, #0x10            // return stack with FP bit?
  ORR lr, lr, #0xFFFFFFE9      // add basic LR bits
  LDR r0, =currentMSP          // get address of the variable
  LDR r0, [r0]                 // get the actual value
  CMP r0, #0                   // is it 0? Then it's PSP
  BNE to_exit                  // it's not 0, so it's MSP, all done
                               // if it's PSP, we need to switch PSP
  LDR r0, =currentSP           // get address of stack pointer
  LDR r0, [r0]                 // get the actual value
  MSR psp, r0                  // save it to PSP
  ORR lr, lr, #0b100           // set the PSP context switch

to_exit:
  // Re-enable interrupts
  CPSIE I
  // Return. The CPU will change MSP/PSP as needed based on LR
  BX lr
