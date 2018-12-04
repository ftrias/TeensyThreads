// Example for blinking multiple LEDs

#include "LED.h"

// Pins for LED objects
const int pinLED = 13;
// const int pinLED1 = 3;
// const int pinLED2 = 4;
// ...

// LED Objects
LED* led;
// LED* led1;
// LED* led2;
// ...

// Timing variables
const int blinkForMSeconds = 10000;
const int blinkPeriodMSeconds = 1000;
const int blinkDuty = 90;
// const int blinkForMSeconds1 = 20000;
// const int blinkPeriodMSeconds1 = 500;
// const int blinkDuty1 = 25;
// const int blinkForMSeconds2 = 5000;
// const int blinkPeriodMSeconds2 = 1500;
// const int blinkDuty2 = 75;
// ...

void setup(){
	delay(1000);

	led = new LED(pinLED);
	// led1 = new LED(pinLED1);
	// led2 = new LED(pinLED2);
	// ...

	// Start threads
	led->startBlinking(blinkForMSeconds, blinkPeriodMSeconds, blinkDuty);
  	//led1->startBlinking(blinkForMSeconds1, blinkPeriodMSeconds1, blinkDuty1);
  	//led2->startBlinking(blinkForMSeconds2, blinkPeriodMSeconds2, blinkDuty2));
  	// ...

}

void loop(){
	// Implement main as needed
}
