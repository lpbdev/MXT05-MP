#include "types.h"

#define FFT_LEN 256
#define FFT_LEN_BITWIDTH 8

#define Saturate11bit(Sample)\
{\
	if (Sample > ((1<<10)-1))\
	Sample = ((1<<10)-1);\
	else if (Sample < -(1<<10))\
	Sample = -(1<<10);\
}

#define Saturate12bit(Sample)\
{\
	if (Sample > 2047)\
	Sample = 2047;\
	else if (Sample < -2048)\
	Sample = -2048;\
}


int  bit_reverse( int in, int BitWidth );
uint12  calc_mag(compx_12bit x);
void fft_256_12b12b(compx_12bit InPara[],compx_12bit OutPara[]);