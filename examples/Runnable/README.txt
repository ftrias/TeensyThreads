The purpose of this example is to demonstrate how to use TeensyThreads as part of std and implement it such that a class could easily implement threading without much overhead
The Runnable.h file is an abstract definition of how to use threads
Any class that dervives from Runnable just needs to implement the runTarget(void *args) and start the thread.
This specific example extends an example LED class such that an LED object can be constructed to blink for some duration, at some period, with some duty cycle