/*
 * File Purpose
 *    Implementing LED.h functions
 */
 
#include "Arduino.h"
#include "LED.h"
 
// Initial constructor. Sets the pin, pinmode, and sets initial state as false
LED::LED(uint8_t pin)
{
	this->pin = pin;
	state = false;
	pinMode(pin, OUTPUT);

	this->duration = 0;
	this->period = 0;
	this->duty = 0;
}

LED::~LED(){}

// Turns on the LED and changes state to true. Volatile so it can be run in thread
void LED::turnOn() volatile
{
	digitalWrite(pin, HIGH);
	state = true;
}

// Turns off the LED and changes state to false. Volatile so it can be run in thread
void LED::turnOff() volatile
{
	digitalWrite(pin, LOW);
	state = false;
}

// This function emulates a PWM signal. But used not for controlling voltage but blinking. 
void LED::runTarget(void *arg)
{
 	// Casting the derived object so it can be used within the static runnable functions
  	LED* thisobj = static_cast<LED*>(arg);

 	float elapsedDuration = duration;
	do
	{
		thisobj->turnOn();
		threads.delay(period * (duty / 100));
		thisobj->turnOff();
		threads.delay((period * (1 - (duty / 100))));

		// For quick example, just assuming that thread delay will equal to exact execution time
    	elapsedDuration -= period;
	} while(elapsedDuration > 0);
}
	
// Start thread that will last for duration duration, period period, and duty duty
void LED::startBlinking(float duration, float period, float duty)
{
	this->duration = duration;
	this->period = period;
	this->duty = duty;

	// Start thread
	blinkThread = new std::thread(&Runnable::runThread, this);
 	blinkThread->detach();
}