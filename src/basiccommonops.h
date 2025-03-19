#ifndef __COMMON_OPS__
#define __COMMON_OPS__


// bit position to be round up
#define ROUND_BIT(shift_bit) (1 << (shift_bit-1))
// bit value at round up position
#define ROUND_FLAG(data, shift_bit) ((shift_bit == 0) ? 0 : ((data & ROUND_BIT(shift_bit)) ? 1 : 0))
// mask to determine convergent round (shift_bit+1 number of 1s)
#define ROUND_MASK_BIT(shift_bit) ((1 << (shift_bit+1)) - 1)
// flag to indicate it is an exact even number + 0.5 (for shift 4 bit case, whether last 5bit is 01000)
#define ROUND_EVEN_FLAG(data, shift_bit) (((data & ROUND_MASK_BIT(shift_bit)) ^ ROUND_BIT(shift_bit)) == 0)
// normal round shift
#define ROUND_SHIFT_RAW(data, shift_bit) ((data >> shift_bit) + ROUND_FLAG(data, shift_bit))
// convergent round shift
#define CONVERGENT_ROUND_SHIFT(data, shift_bit) ((data >> shift_bit) + (ROUND_EVEN_FLAG(data, shift_bit) ? 0 : ROUND_FLAG(data, shift_bit)))

#endif