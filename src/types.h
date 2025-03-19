#ifndef  __TYPES_H__
#define  __TYPES_H__

typedef          long int S32;
typedef unsigned long int U32;
typedef unsigned char U8;
typedef signed char S8;
typedef float FLOAT;

typedef  unsigned  short int  uint16;
typedef  signed    short int  int16;

typedef  int16                int12;
typedef  int16                int10;
typedef  int16                int11;
typedef  uint16               uint12;
typedef  uint16               uint13;

typedef  struct {
    int10    real;
    int10    imag;
} compx_10bit ;

typedef  struct {
    int12    real;
    int12    imag;
} compx_12bit ;

#endif