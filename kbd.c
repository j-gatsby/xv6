#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int
kbdgetc(void)
{
	static uint shift;
	static uchar *charcode[4] = {
		normalmap, shiftmap, ctlmap, ctlmap
	};
	uint st, data, c;

	st = inb(KBSTATP);
	if ((st & KBS_DIB) == 0)
		return -1;
	data = inb(KBDATAP);

	if (data == 0xE0)
	{
		shift |= EOESC;
		return 0;
	}
	else if (data & 0x80)
	{
		// Key released
		data = (shift & EOESC ? data : data & 0x7F);
		shift &= (shiftcode[data] | EOESC);
		return 0;
	}
	else if (shift & EOESC)
	{
		// Last character was an EO escape; or with 0x80
		data |= 0x80;
		shift &= ~EOESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];
	c = charcode[shift & (CTL | SHIFT)][data];
	if (shift & CAPSLOCK)
	{
		if ('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if ('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}
	return c;
}


void
kbdintr(void)
{
	consoleintr(kbdgetc);
}