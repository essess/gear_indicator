/**
 * Copyright (c) 2018 Sean Stasiak. All rights reserved.
 * Developed by: Sean Stasiak <sstasiak@protonmail.com>
 * Refer to license terms in license.txt; In the absence of such a file,
 * contact me at the above email address and I can provide you with one.
 */

#include <system.h>
#include "types.h"
#include "main.h"

#ifdef _SIM
#pragma message "_SIM - defined, assuming use of simulator and plugins from SourceBoost IDE"
#endif // _SIM

#pragma DATA _IDLOC , 0x00, 0x01, 0x00, 0x00   //0100, only lower nibbles of each byte are used
#pragma CLOCK_FREQ	4000000

#ifdef _ICD2
#include <icd2.h>
#pragma message "_ICD2 - defined, using ICD2 debugger reservations"
#pragma DATA _CONFIG, _CPD_OFF & _CP_OFF & _BODEN & _MCLRE_OFF & _PWRTE_ON & _WDT_OFF & _INTRC_OSC_NOCLKOUT
#else
#pragma DATA _CONFIG, _CPD & _CP & _BODEN & _MCLRE_OFF & _PWRTE_ON & _WDT_OFF & _INTRC_OSC_NOCLKOUT
#endif //_ICD2



/**************************************************************************************
 *  DEVICE: 16F676, 4mhz internal osc used, Not sure if I can use ICSP since the port
 *          is slightly different from 'normal'
 *
 *  PINOUT:
 *							        +----U----+
 *							Vdd/+5v | 1    14 | Vss/Gnd
 *                           dacCS  | 2    13 | A/D in
 *                         debugCS  | 3    12 | Serial Clock
 *                   Input Only     | 4    11 | Serial Data
 *                         'N' Lamp | 5    10 | D0
 *                        Heartbeat | 6     9 | D1
 *                                  | 7     8 | D2
 *			        				+---------+
 *
 * function by pin:
 *
 * 1  - +5
 * 14 - Gnd
 *
 * 6  - RC4, Hearbeat, Power Indicator, 1ms Indicator -> goes low momentarily every 1ms
 *      I plan on using a freq cntr to see how stable this is, also shows a hang if ever happens
 *      This is updated in the main loop once a 1ms flag is rcvd. Active high, use it
 *      to drive the cathode of an led, flicker should be unoticeable.
 *
 * 5  - RC5, 'N' lamp driver, external low side driver, used to mimick the original functionality
 *
 *
 * 3  - debugCS - RA4 - active low, debug channel, where a special message is shifted out
 * 2  - dacCS   - RA5 - active low, when external dac is connect, this provides a cs
 *
 * 12 - RA1 - Serial Clk, goes high to latch current data bit into your device
 * 11 - RA2 - Serial Data, H = 1, L = 0 clock in on rising edge of Clk
 *
 *
 * 10 - D0 - RC0
 * 9  - D1 - RC1
 * 8  - D2 - RC2 - BCD discrete outputs. 0 = N, 1 = 1st Gear, etc. Updated every 10ms
 *
 * Notes: Do a test, figure out what happens if the pickup bridges between to adjacent pads
 *        is it handled gracefully and detected correctly ?
 *
 **************************************************************************************/

volatile bit flag1ms           = false;
volatile bit flagUpdateOutputs = false;

volatile bit t0if@INTCON.T0IF;		 // t0 aliases
volatile bit t0ie@INTCON.T0IE;
volatile bit t0cs@OPTION_REG.T0CS;	 //
volatile bit psa@OPTION_REG.PSA;

volatile bit adif@INTCON.ADIF;		// A/D bit's
volatile bit adGo@ADCON0.GO;

volatile bit ledTris@TRISC.4;		// RC4 is the Led Driver
volatile bit led@PORTC.4;
const bit ledOn  = 0;
const bit ledOff = 1;

volatile bit nDriverTris@TRISC.5;	// RC5 is 'N' Lamp Driver
volatile bit nDriver@PORTC.5;
const bit nOn  = 1;					// TODO: revisit these after lamp driver has been fleshed out
const bit nOff = 0;

volatile bit ps0@OPTION_REG.PS0;	 // prescalar bits
volatile bit ps1@OPTION_REG.PS1;
volatile bit ps2@OPTION_REG.PS2;

const uint8_t tmr0reload = 0x100-15; // based on prescale of 64 and CLKOUT == 1MHz. tmr0 updates at 15.625KHz rate

volatile uint8_t msCnt = 0;			// used by the int handler
volatile uint16_t lastSample = 0xff01;	// holds the latest A/D sample

// ------  inlines  -------
volatile bit gie@INTCON.GIE;		// global int enable bit
inline void ei(void) { gie = 1; }	// enable ints
inline void di(void) { gie = 0; }	// disable ints

inline void neutralLampActive(void) {
nDriverTris = Output;
nDriver = nOn;
}

inline void neutralLampInactive(void) {
nDriverTris = Output;
nDriver = nOff;
}


inline void flashLedOff(void) {
ledTris = Output;
led = ledOn;
led = ledOff;
led = ledOn;
}

inline void ledOn(void) {
ledTris = Output;
led = ledOn;
}

inline void nLampOn(void) {
nDriverTris = Output;
nDriver = nOn;
}

inline void nLampOff(void) {
nDriverTris = Output;
nDriver = nOff;
}

inline uint8_t inLimits(uint16_t value, uint16_t UL, uint16_t LL) {

	if( (value<=UL) && (value>=LL) )
		return 0xff; 	// return non-zero if within limits
	else
		return 0x00;

}

// ------------------------

void main(void) {

gearStatus_t gearStatus;

gearStatus.lockFailCnt = lockFailCntReload;
gearStatus.lockCnt = lockCntReload;
gearStatus.unlockCnt = unlockCntReload;
gearStatus.lockFailures = 0;
gearStatus.unknownGearLockCnt = unknownGearLockCntReload;

gearStatus.ms = 0;
gearStatus.currentGear = Neutral;
gearStatus.lockStatus = Locked;

ledOn();
nLampOff();

initTimer();
initAtoD();

ei();
while(1) {
	// do 'idle' stuff here --
	//
	// -----------------------
	if(flag1ms) {
		flag1ms = false;
		flashLedOff();

		lastSample = adresh;	// fetch sample
		lastSample <<= 8;
		lastSample |= adresl;

		gear_t foundGear = findGear(lastSample);

		switch(gearStatus.lockStatus) {
			case Locked:
				if(gearStatus.currentGear == foundGear){
					if(foundGear == Unknown) { // prevent being locked into 'Unknown' gear, go to 'unlock' after x msec
						gearStatus.unknownGearLockCnt-- ? gearStatus.lockStatus = Locked : gearStatus.lockStatus = Unlocked;
						}
					else { // 'Unknown' was a fluke, we're back on the original gear we were expecting
						gearStatus.unknownGearLockCnt = unknownGearLockCntReload;
						}
					gearStatus.lockFailCnt = lockFailCntReload;
					gearStatus.lockCnt = lockCntReload;
					gearStatus.unlockCnt = unlockCntReload;
					}
				else { // didn't read the gear we are currently locked on to, got a different one (might be 'unknown')
					gearStatus.lockCnt--;
					if(gearStatus.lockCnt == 0) {  // if we get enough, we'll eventually unlock and have to search for a new gear
						gearStatus.lockStatus = Unlocked;
						}
					}
				break;
			case Unlocked:
				if(gearStatus.currentGear == foundGear){
					gearStatus.unlockCnt--;
					gearStatus.lockFailCnt = lockFailCntReload;
					if(gearStatus.unlockCnt == 0) {
						gearStatus.lockStatus = Locked;
						gearStatus.unknownGearLockCnt = unknownGearLockCntReload; // it's possible we could be locked onto an 'Unknown' gear
						}
				}
				else { // found a different gear from what we were expecting
					gearStatus.currentGear = foundGear;
					gearStatus.lockFailCnt--;
					if(gearStatus.lockFailCnt == 0) {
						gearStatus.lockStatus = LockFail;
						}
				}
				break;
			case LockFail:
			default:
				gearStatus.lockFailures++;
				// reset all state info to act as if we just transitioned to the unlock state
				// if we keep getting lockfails then we stay safe in 'neutral'
				gearStatus.lockStatus = Unlocked;
				gearStatus.currentGear = Unknown;
				gearStatus.unlockCnt = unlockCntReload;
				gearStatus.lockFailCnt = lockFailCntReload;
				break;
			}

		gearStatus.currentGear = foundGear;
		(gearStatus.currentGear == Neutral)||(gearStatus.currentGear == Unknown)? neutralLampActive() : neutralLampInactive();
		}

	if(flagUpdateOutputs) {
		flagUpdateOutputs = false;

		gear_t gear = gearStatus.currentGear;
		sendGear(gear);
		// sendSerial(debugChan,lastSample);
		// sendSerial(adcChan,lastSample<<2);		// dac is 12b, so we need to adjust accordingly.
		}
	}
}

void initTimer(void) {

	t0cs = 0; 		// select clock source == CLKOUT (1MHz since we're using the internal osc)
	psa  = 0;		// assign prescaler to t0 instead of watchdog

	ps0  = 1;		// divide by 64
	ps1  = 0;		// t0 clk freq == 15.625KHz (.000064)
	ps2  = 1;

	tmr0 = tmr0reload;
	t0if = false;	// ignore any pending int's before we allow them
	t0ie = true;	// allow t0 to generate ints
}

void initAtoD(void) {

	adcon0.ADON = 1;			// turn on A/D module
	ansel.ANS0  = 1;			// select ra0 as our analog in from the divider.
	trisa.0     = 1;			// tris an0
	adGo        = false;		// abort anything in progress just in case
	adcon1      = 0b00110000;	// set conversion clock to internal RC
	adcon0.VCFG = 0;			// set Vref to Vdd
	adcon0.ADFM = 1;			// conversion is right justified

	adcon0.CHS0 = 0;			// select channel 0 as the input
	adcon0.CHS1 = 0;
	adcon0.CHS2 = 0;

}

void interrupt(void) {

if(t0if) {
	t0if = false;
	tmr0 = tmr0reload;
	flag1ms = true;
	adGo    = true;			// start a conversion

	msCnt++;
	if(msCnt >= 10) {
		msCnt = 0;
		flagUpdateOutputs = true;
		}
	}
}

gear_t findGear(const uint16_t sample) {
	if(inLimits(sample, neutralUL, neutralLL))
		return Neutral;
	if(inLimits(sample, firstUL, firstLL))
		return First;
	if(inLimits(sample, secondUL, secondLL))
		return Second;
	if(inLimits(sample, thirdUL, thirdLL))
		return Third;
	if(inLimits(sample, fourthUL, fourthLL))
		return Fourth;
	if(inLimits(sample, fifthUL, fifthLL))
		return Fifth;
	if(inLimits(sample, sixthUL, sixthLL))
		return Sixth;
	return Unknown;
}

void sendGear(const gear_t gear) {
uint8_t temp;
uint16_t dacV;	// 12b dac, each bit == 1mv

ansel.ANS4 = 0;
ansel.ANS5 = 0;
ansel.ANS6 = 0;
trisc.0 = Output;
trisc.1 = Output;
trisc.2 = Output;

switch(gear) {
	case First:
		// 0b001
		temp = 0b001;
		dacV = 1000; //mV
		break;
	case Second:
		// 0b010
		temp = 0b010;
		dacV = 1500; //mV
		break;
	case Third:
		// 0b011
		temp = 0b011;
		dacV = 2000; //mV
		break;
	case Fourth:
		// 0b100
		temp = 0b100;
		dacV = 2500; //mV
		break;
	case Fifth:
		// 0b101
		temp = 0b101;
		dacV = 3000; //mV
		break;
	case Sixth:
		// 0b110
		temp = 0b110;
		dacV = 3500; //mV
		break;
	case Neutral:
	default:
		// 0b000
		temp = 0b000;
		dacV = 0500; //mV
	}

(temp&0b001) ? portc.0 = 1 : portc.0 = 0;	// set digital representation
(temp&0b010) ? portc.1 = 1 : portc.1 = 0;
(temp&0b100) ? portc.2 = 1 : portc.2 = 0;

sendSerial(adcChan,dacV);					// set analog representation

}

void sendSerial(const serialChannel_t chan, const uint16_t data) {

uint16_t mask;
ansel.ANS3 = Output;
trisa.4    = Output;
trisa.5    = Output;

volatile bit dacCS@PORTA.5;
volatile bit debugCS@PORTA.4;

switch(chan) {
	case adcChan:	// target DAC is LTC1451
		mask    = 0b0000100000000000;
		dacCS   = 0;
		debugCS = 1;
		break;
	case debugChan:
		mask    = 0b1000000000000000;
		dacCS   = 1;
		debugCS = 0;
		break;
	case InvalidChan:
	default:
		goto endOfFunc;
	}

ansel.ANS1 = Output;
ansel.ANS2 = Output;
trisa.1    = Output;
trisa.2    = Output;

volatile bit serData@PORTA.2;
volatile bit serClk@PORTA.1;

while(mask != 0) {
	serData = 0;
	serClk = 0;
	(data & mask) ? serData = 1 : serData = 0;
	serClk = 1;
	mask >>= 1;
	}

endOfFunc:

dacCS   = 1;
debugCS = 1;
serClk=0;
serData=0;
}