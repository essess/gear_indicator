/**
 * Copyright (c) 2018 Sean Stasiak. All rights reserved.
 * Developed by: Sean Stasiak <sstasiak@protonmail.com>
 * Refer to license terms in license.txt; In the absence of such a file,
 * contact me at the above email address and I can provide you with one.
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#define _IDLOC			0x2000	// only the lower nibbles from add 0x2000->0x2003 are used

typedef enum {
	Neutral = 0x00,
	First   = 0x01,
	Second  = 0x02,
	Third   = 0x03,
	Fourth  = 0x04,
	Fifth   = 0x05,
	Sixth   = 0x06,
	Unknown = 0x07
	} gear_t;

typedef enum {
	Locked   = 0x08,
	Unlocked = 0x09,
	LockFail = 0x0A
	} lockStatus_t;

typedef enum {
	debugChan   = 0x02,			// these are simple, if you want more than 3 channels, you'll need to make a mux
	adcChan     = 0x01,			// notice that 0x00 is UNUSED
	InvalidChan = 0x00
	} serialChannel_t;

typedef struct {
	uint16_t 		ms;				// might not need this since we have state countdown timers below
	uint16_t		lockFailures;	// holds the quantity of lock failure states we've hit since powerup
	uint16_t		lockFailCnt;	// number of ticks left in lock failure state timeout
	uint16_t		lockCnt;		// number of ticks left in lock state timeout
	uint16_t		unlockCnt;		// number of ticks left in unlock state timeout
	uint8_t			unknownGearLockCnt;  // used to prevent being locked in an unknown gear
	gear_t 			currentGear;
	lockStatus_t	lockStatus;
} gearStatus_t;


// values in msec
#define lockFailCntReload 	(uint16_t)20	// if we don't acquire a lock in this period than go to failsafe mode
#define lockCntReload 		(uint16_t)15	// msec cnt before being able to transition to unlock state - tune this down as low as possible, but avoid false unlocks
#define unlockCntReload 	(uint16_t)15	// msec cnt before being able to transition to lock state - tune this down as low as possible, but avoid false locks
#define unknownGearLockCntReload  (uint8_t)50		// do not stay locked in an unknown gear for more than x msec

// ~0.00 = A/D -> 0x000,000
#define neutralUL (uint16_t)0x0028		// windows are +/- .2v == 40, 0x28 bits
#define neutralLL (uint16_t)0x0000

// ~4.5v = A/D -> 0x399,921
#define firstUL (uint16_t)0x03c1
#define firstLL (uint16_t)0x0371

// ~3.6v = A/D -> 0x2e1,737
#define secondUL (uint16_t)0x0309
#define secondLL (uint16_t)0x02b9

// ~2.7v = A/D -> 0x228,552
#define thirdUL (uint16_t)0x0250
#define thirdLL (uint16_t)0x0200

// ~1.8v = A/D -> 0x170,368
#define fourthUL (uint16_t)0x0198
#define fourthLL (uint16_t)0x0148

// ~0.9v = A/D -> 0x0b8,184
#define fifthUL (uint16_t)0x00e0
#define fifthLL (uint16_t)0x0090

#define sixthUL (uint16_t)0x0000		// to disable sixth, just 'flip' ul/ll
#define sixthLL (uint16_t)0x03ff

void initTimer(void);
void initAtoD(void);

gear_t  findGear(const uint16_t);						// returns gear based on passed sample, returns 'Unknown' if gear not found
void    sendGear(const gear_t);					// sends current gear to rc0,1,2
void    sendSerial(const serialChannel_t, const uint16_t);

#endif // _MAIN_H_
