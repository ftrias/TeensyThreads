#ifndef LED_H
#define LED_H

/*
 * File Purpose
 *    The purpose of this file is to create a simple interface for turning on and off an led
 *    And for threading led function so that you can make an led blink for a specified amount of duty at any frequency
 */

#include <stdint.h>
#include <TeensyThreads.h>
#include "Runnable.h"

class LED : public Runnable{
private:
	// Digital pin
	uint8_t pin;
    
	// Track state so the program knows if the LED is ON or OFF
	bool state;

 	 // Timing Variables
	float duration;
	float period;
	float duty;

  	// Thread object
 	std::thread *blinkThread;
	
protected:
  // Runnable function that we need to implement
  void runTarget(void *arg);
    
public:
	// Constructor/Destructor
	LED(uint8_t pin);
 	~LED();

  	// Turn on and off LED, volatile for threading use
	void turnOn() volatile;
	void turnOff() volatile;

	// Start thread that will last for duration
	void startBlinking(float duration, float period, float duty);
};

#endif	// LED_H
