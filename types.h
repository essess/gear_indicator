/**
 * Copyright (c) 2018 Sean Stasiak. All rights reserved.
 * Developed by: Sean Stasiak <sstasiak@protonmail.com>
 * Refer to license terms in license.txt; In the absence of such a file,
 * contact me at the above email address and I can provide you with one.
 */

#ifndef _TYPES_H_
#define _TYPES_H_

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;

#define Input  1				// used for TRIS registers and ANSEL
#define Output 0

inline void hang(void) {
asm hanghere:	goto hanghere
}

#define testbit(var, bit)   ((var) & (1 <<(bit)))
#define setbit(var, bit)    ((var) |= (1 << (bit)))
#define clrbit(var, bit)    ((var) &= ~(1 << (bit)))

#endif // _TYPES_H_
