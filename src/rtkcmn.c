#include <stdarg.h>
#include <ctype.h>
#include "rtk.h"

extern FILE* test;

#define AU          149597870691.0      /* 1 AU (m) */
#define AS2R        (D2R/3600.0)        /* arc sec to radian */

/* time system: gps time */
const solopt_t solopt_default = { /* defaults solution output options */
    0,0,1,3,    /* posf,times,timef,timeu */
    0,1,0,0,0,0,0,              /* degf,outhead,outopt,outvel,datum,height,geoid */
    0,0,0,                      /* solstatic,sstat,//trace */
    {0.0,0.0},                  /* nmeaintv */
    " ",""                      /* separator/program name */
};
static char* obscodes[] = {       /* observation code strings */

    ""  ,"1C","1P","1W","1Y", "1M","1N","1S","1L","1E", /*  0- 9 */
    "1A","1B","1X","1Z","2C", "2D","2S","2L","2X","2P", /* 10-19 */
    "2W","2Y","2M","2N","5I", "5Q","5X","7I","7Q","7X", /* 20-29 */
    "6A","6B","6C","6X","6Z", "6S","6L","8L","8Q","8X", /* 30-39 */
    "2I","2Q","6I","6Q","3I", "3Q","3X","1I","1Q","5A", /* 40-49 */
    "5B","5C","9A","9B","9C", "9X","1D","5D","7D","5P"    /* 50-59 */
};
static unsigned char obsfreqs[] = {
    /* 1:L1/E1, 2:L2/B1, 3:L5/E5a/L3, 4:L6/LEX/B3, 5:E5b/B2, 6:E5(a+b), 7:S */
    0, 1, 1, 1, 1,  1, 1, 1, 1, 1, /*  0- 9 */
    1, 1, 1, 1, 2,  2, 2, 2, 2, 2, /* 10-19 */
    2, 2, 2, 2, 3,  3, 3, 5, 5, 5, /* 20-29 */
    4, 4, 4, 4, 4,  4, 4, 6, 6, 6, /* 30-39 */
    2, 2, 4, 4, 3,  3, 3, 1, 1, 3, /* 40-49 */
    3, 3, 7, 7, 7,  7, 2, 3, 6, 3  /* 50-59 */
};

static char codepris[7][MAXFREQ][16] = {  /* code priority table */

   /* L1/E1      L2/B1        L5/E5a/L3 L6/LEX/B3 E5b/B2    E5(a+b)  S */
    {"CPYWMNSL","PYWCMNDSLX","IQX"     ,"PYWCMNDSLX"       ,"PYWCMNDSLX"       ,""      ,""    }, /* GPS */
    {"PC"      ,"PC"        ,"IQX"     ,""       ,""       ,""      ,""    }, /* GLO */
    {"CABXZ"   ,""          ,"IQX"     ,"ABCXZ"  ,"IQX"    ,"IQX"   ,""    }, /* GAL */
    {"CSLXZ"   ,"SLX"       ,"IQX"     ,"SLX"    ,""       ,""      ,""    }, /* QZS */
    {"C"       ,""          ,"IQX"     ,""       ,""       ,""      ,""    }, /* SBS */
    {"IQXDP"     ,"IQXDP"       ,"IQXDP"     ,"IQXDP"    ,"IQXDP"    ,"IQXDP"      ,"IQXDP"    }, /* BDS */
    {""        ,""          ,"ABCX"    ,""       ,""       ,""      ,"ABCX"}  /* IRN */
};
const static double gpst0[] = { 1980,1, 6,0,0,0 }; /* gps time reference */
const static double leaps[][7] = { /* leap seconds {y,m,d,h,m,s,gpst-utc,...} */
    {2017,1,1,0,0,0,18},
    {2015,7,1,0,0,0,17},
    {2012,7,1,0,0,0,16},
    {2009,1,1,0,0,0,15},
    {2006,1,1,0,0,0,14},
    {1999,1,1,0,0,0,13},
    {1997,7,1,0,0,0,12},
    {1996,1,1,0,0,0,11},
    {1994,7,1,0,0,0,10},
    {1993,7,1,0,0,0,9},
    {1992,7,1,0,0,0,8},
    {1991,1,1,0,0,0,7},
    {1990,1,1,0,0,0,6},
    {1988,1,1,0,0,0,5},
    {1985,7,1,0,0,0,4},
    {1983,7,1,0,0,0,3},
    {1982,7,1,0,0,0,2},
    {1981,7,1,0,0,0,1}
};
const double chisqr[100] = {      /* chi-sqr(n) (alpha=0.001) */
    10.8,13.8,16.3,18.5,20.5,22.5,24.3,26.1,27.9,29.6,
    31.3,32.9,34.5,36.1,37.7,39.3,40.8,42.3,43.8,45.3,
    46.8,48.3,49.7,51.2,52.6,54.1,55.5,56.9,58.3,59.7,
    61.1,62.5,63.9,65.2,66.6,68.0,69.3,70.7,72.1,73.4,
    74.7,76.0,77.3,78.6,80.0,81.3,82.6,84.0,85.4,86.7,
    88.0,89.3,90.6,91.9,93.3,94.7,96.0,97.4,98.7,100 ,
    101 ,102 ,103 ,104 ,105 ,107 ,108 ,109 ,110 ,112 ,
    113 ,114 ,115 ,116 ,118 ,119 ,120 ,122 ,123 ,125 ,
    126 ,127 ,128 ,129 ,131 ,132 ,133 ,134 ,135 ,137 ,
    138 ,139 ,140 ,142 ,143 ,144 ,145 ,147 ,148 ,149
};

static const unsigned int tbl_CRC24Q[] = {
    0x000000,0x864CFB,0x8AD50D,0x0C99F6,0x93E6E1,0x15AA1A,0x1933EC,0x9F7F17,
    0xA18139,0x27CDC2,0x2B5434,0xAD18CF,0x3267D8,0xB42B23,0xB8B2D5,0x3EFE2E,
    0xC54E89,0x430272,0x4F9B84,0xC9D77F,0x56A868,0xD0E493,0xDC7D65,0x5A319E,
    0x64CFB0,0xE2834B,0xEE1ABD,0x685646,0xF72951,0x7165AA,0x7DFC5C,0xFBB0A7,
    0x0CD1E9,0x8A9D12,0x8604E4,0x00481F,0x9F3708,0x197BF3,0x15E205,0x93AEFE,
    0xAD50D0,0x2B1C2B,0x2785DD,0xA1C926,0x3EB631,0xB8FACA,0xB4633C,0x322FC7,
    0xC99F60,0x4FD39B,0x434A6D,0xC50696,0x5A7981,0xDC357A,0xD0AC8C,0x56E077,
    0x681E59,0xEE52A2,0xE2CB54,0x6487AF,0xFBF8B8,0x7DB443,0x712DB5,0xF7614E,
    0x19A3D2,0x9FEF29,0x9376DF,0x153A24,0x8A4533,0x0C09C8,0x00903E,0x86DCC5,
    0xB822EB,0x3E6E10,0x32F7E6,0xB4BB1D,0x2BC40A,0xAD88F1,0xA11107,0x275DFC,
    0xDCED5B,0x5AA1A0,0x563856,0xD074AD,0x4F0BBA,0xC94741,0xC5DEB7,0x43924C,
    0x7D6C62,0xFB2099,0xF7B96F,0x71F594,0xEE8A83,0x68C678,0x645F8E,0xE21375,
    0x15723B,0x933EC0,0x9FA736,0x19EBCD,0x8694DA,0x00D821,0x0C41D7,0x8A0D2C,
    0xB4F302,0x32BFF9,0x3E260F,0xB86AF4,0x2715E3,0xA15918,0xADC0EE,0x2B8C15,
    0xD03CB2,0x567049,0x5AE9BF,0xDCA544,0x43DA53,0xC596A8,0xC90F5E,0x4F43A5,
    0x71BD8B,0xF7F170,0xFB6886,0x7D247D,0xE25B6A,0x641791,0x688E67,0xEEC29C,
    0x3347A4,0xB50B5F,0xB992A9,0x3FDE52,0xA0A145,0x26EDBE,0x2A7448,0xAC38B3,
    0x92C69D,0x148A66,0x181390,0x9E5F6B,0x01207C,0x876C87,0x8BF571,0x0DB98A,
    0xF6092D,0x7045D6,0x7CDC20,0xFA90DB,0x65EFCC,0xE3A337,0xEF3AC1,0x69763A,
    0x578814,0xD1C4EF,0xDD5D19,0x5B11E2,0xC46EF5,0x42220E,0x4EBBF8,0xC8F703,
    0x3F964D,0xB9DAB6,0xB54340,0x330FBB,0xAC70AC,0x2A3C57,0x26A5A1,0xA0E95A,
    0x9E1774,0x185B8F,0x14C279,0x928E82,0x0DF195,0x8BBD6E,0x872498,0x016863,
    0xFAD8C4,0x7C943F,0x700DC9,0xF64132,0x693E25,0xEF72DE,0xE3EB28,0x65A7D3,
    0x5B59FD,0xDD1506,0xD18CF0,0x57C00B,0xC8BF1C,0x4EF3E7,0x426A11,0xC426EA,
    0x2AE476,0xACA88D,0xA0317B,0x267D80,0xB90297,0x3F4E6C,0x33D79A,0xB59B61,
    0x8B654F,0x0D29B4,0x01B042,0x87FCB9,0x1883AE,0x9ECF55,0x9256A3,0x141A58,
    0xEFAAFF,0x69E604,0x657FF2,0xE33309,0x7C4C1E,0xFA00E5,0xF69913,0x70D5E8,
    0x4E2BC6,0xC8673D,0xC4FECB,0x42B230,0xDDCD27,0x5B81DC,0x57182A,0xD154D1,
    0x26359F,0xA07964,0xACE092,0x2AAC69,0xB5D37E,0x339F85,0x3F0673,0xB94A88,
    0x87B4A6,0x01F85D,0x0D61AB,0x8B2D50,0x145247,0x921EBC,0x9E874A,0x18CBB1,
    0xE37B16,0x6537ED,0x69AE1B,0xEFE2E0,0x709DF7,0xF6D10C,0xFA48FA,0x7C0401,
    0x42FA2F,0xC4B6D4,0xC82F22,0x4E63D9,0xD11CCE,0x575035,0x5BC9C3,0xDD8538
};
static const unsigned short tbl_CRC16[] = {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
    0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
    0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
    0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
    0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
    0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
    0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
    0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
    0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
    0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
    0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
    0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
    0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
    0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
    0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
    0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
    0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
    0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
    0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
    0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
    0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
    0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
    0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
    0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
    0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
    0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
    0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
    0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
    0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
    0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
    0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0
};

#ifdef WIN32
extern int gettimeofday(struct timeval* tp, void* tzp)
{
    time_t clock;
    struct tm tm;
    SYSTEMTIME wtm;
    GetLocalTime(&wtm);
    tm.tm_year = wtm.wYear - 1900;
    tm.tm_mon = wtm.wMonth - 1;
    tm.tm_mday = wtm.wDay;
    tm.tm_hour = wtm.wHour;
    tm.tm_min = wtm.wMinute;
    tm.tm_sec = wtm.wSecond;
    tm.tm_isdst = -1;
    clock = mktime(&tm);
    tp->tv_sec = clock;
    tp->tv_usec = wtm.wMilliseconds * 1000;
    return (0);
}
#else
#include <sys/time.h>
#endif

extern double timediff(gtime_t t1, gtime_t t2)
{
    return difftime(t1.time, t2.time) + t1.frac - t2.frac;
}

extern gtime_t timeadd(gtime_t t, double sec)
{
    t.frac += sec;
    sec = floor(t.frac);
    t.time += sec;
    t.frac -= sec;
    return t;
}

gtime_t epoch2time(const double* ep)
{
    gtime_t temp;
    int doy[12] = { 0,31,59,90,120,151,181,212,243,273,304,334 };    //distance of first day 
    //of months from January 1st in common year
    int year = (int)ep[0], month = (int)ep[1], day = (int)ep[2], sec;

    int days = (year - 1970) * 365 + doy[month - 1] + (((year % 4 == 0) && (month > 2)) ? 1 : 0)
        + day - 1 + (year - 1969) / 4;//(year-1-1968)/4: leap days since 1970, ignore current year.
    sec = (int)ep[5];
    temp.time = days * 86400 + ep[3] * 3600 + ep[4] * 60 + sec;
    temp.frac = ep[5] - sec;
    return temp;
}
/* -- gtime_t utc2gpst(gtime_t t) --------------------------------------
*
* Description    : convert utc time to gps time by adding leap seconds
* Parameters    :
* Return        :
*/
extern gtime_t utc2gpst(gtime_t t)
{
    //    int i;
    //    for (i=0;i<leaps[0][6];i++)
    //    {
    //        if (timediff(t,epoch2time(leaps[i]))>=0)
    //            return timeadd(t,leaps[i][6]);        
    //    }
    //    return t;
    return timeadd(t, leaps[0][6]);
}

extern gtime_t gpst2utc(gtime_t t)
{
    return timeadd(t, -leaps[0][6]);
}

extern gtime_t gpst2time(int week, double sec)
{
    gtime_t t0 = epoch2time(gpst0);
    if (sec > 1E9 || sec < -1E9) sec = 0.0;
    t0.time += week * SEC_PER_WEEK + floor(sec);
    t0.frac = sec - floor(sec);
    return t0;
}

extern double time2gpst(gtime_t t, int* week)
{
    int temp;
    gtime_t t0 = epoch2time(gpst0);
    temp = (t.time - t0.time) / SEC_PER_WEEK;
    if (week)
        *week = temp;
    return (t.time - t0.time - temp * SEC_PER_WEEK + t.frac);
}

extern int adjgpsweek(int week)
{
    int w;
    (void)time2gpst(utc2gpst(timeget()), &w);
    if (w < 1560) w = 1560; /* use 2009/12/1 if time is earlier than 2009/12/1 */
    return week + (w - week + 512) / 1024 * 1024;
}


extern unsigned int getbitu(const unsigned char* buff, int pos, int len)
{
    unsigned int bits = 0;
    int i;
    for (i = pos; i < pos + len; i++)
        bits = (bits << 1) + ((buff[i / 8] >> (7 - i % 8)) & 1u);
    return bits;
}
extern void setbitu(unsigned char* buff, int pos, int len, unsigned int data)
{
    unsigned int mask = 1u << (len - 1);
    int i;
    if (len <= 0 || 32 < len) return;
    for (i = pos; i < pos + len; i++, mask >>= 1) {
        if (data & mask) buff[i / 8] |= 1u << (7 - i % 8); else buff[i / 8] &= ~(1u << (7 - i % 8));
    }
}
extern int getbits(const unsigned char* buff, int pos, int len)
{
    unsigned int bits = getbitu(buff, pos, len);
    if (len <= 0 || 32 <= len || !(bits & (1u << (len - 1)))) return (int)bits;
    return (int)(bits | (~0u << len)); /* extend sign */
}
/* -- void setbit(uint8_t*buff,int word, int pos, int len, int32_t value) --------------------------------------
*
* Description    : word 1-10
* Parameters    : len <= 32
* Return        :
*/
extern void setbit(uint8_t* buff, int word, int pos, int len, int32_t value)
{
    int i, byte_id, mask = 1 << (len - 1);
    word--;
    for (i = pos; i < pos + len; i++)
    {
        byte_id = i >> 3;
        if (value & mask)
            buff[word * 3 + byte_id] |= (0x80 >> (i - (byte_id << 3)));
        else
            buff[word * 3 + byte_id] &= ~(0x80 >> (i - (byte_id << 3)));
        value <<= 1;
    }
}
extern unsigned int rtk_crc24q(const unsigned char* buff, int len)
{
    unsigned int crc = 0;
    int i;

    for (i = 0; i < len; i++) crc = ((crc << 8) & 0xFFFFFF) ^ tbl_CRC24Q[(crc >> 16) ^ buff[i]];
    return crc;
}
extern unsigned short rtk_crc16(const unsigned char* buff, int len)
{
    unsigned short crc = 0;
    int i;

    //trace(4, "rtk_crc16: len=%d\n", len);

    for (i = 0; i < len; i++) {
        crc = (crc << 8) ^ tbl_CRC16[((crc >> 8) ^ buff[i]) & 0xFF];
    }
    return crc;
}
extern double* mat(int r, int c)
{
    double* p;
    if (r <= 0 || c <= 0) return NULL;
    p = (double*)(malloc(sizeof(double) * r * c));
    return p;
}
extern double* zeros(int r, int c)
{
    double* p;
    if (r <= 0 || c <= 0) return NULL;
    p = (double*)(calloc(sizeof(double), r * c));
    return p;
}
#if 0
// error: assignment to 'double *' from incompatible pointer type 'char *' [-Wincompatible-pointer-types]
extern double* zerosChar(int r, int c)
{
    double* p;
    if (r <= 0 || c <= 0) return NULL;
    p = (char*)(calloc(sizeof(char), r * c));
    return p;
}
#endif
/* copy matrix -----------------------------------------------------------------
* copy matrix
* args   : double *A        O   destination matrix A (n x m)
*          double *B        I   source matrix B (n x m)
*          int    n,m       I   number of rows and columns of matrix
* return : none
*-----------------------------------------------------------------------------*/
extern void matcpy(double* A, const double* B, int n, int m)
{
    memcpy(A, B, sizeof(double) * n * m);
}
/* new integer matrix ----------------------------------------------------------
* allocate memory of integer matrix
* args   : int    n,m       I   number of rows and columns of matrix
* return : matrix pointer (if n<=0 or m<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern int* imat(int n, int m)
{
    int* p;
    if (n <= 0 || m <= 0) return NULL;
    p = (int*)malloc(sizeof(int) * n * m);
    return p;
}
/* multiply matrix  -------------------------------------
* multiply matrix by matrix (C=alpha*A*B+beta*C)
* args   : char   *tr       I  transpose flags ("N":normal,"T":transpose)
*          int    n,k,m     I  size of (transposed) matrix A,B
*          double alpha     I  alpha
*          double *A,*B     I  (transposed) matrix A (n x m), B (m x k)
*          double beta      I  beta
*          double *C        IO matrix C (n x k)
* return : none
*-----------------------------------------------------------------------------*/
extern void matmul(const char* tr, int n, int k, int m, double alpha,
    const double* A, const double* B, double beta, double* C)
{
    double d;
    int i, j, x, f = tr[0] == 'N' ? (tr[1] == 'N' ? 1 : 2) : (tr[1] == 'N' ? 3 : 4);

    for (i = 0; i < n; i++) for (j = 0; j < k; j++) {
        d = 0.0;
        switch (f) {
        case 1: for (x = 0; x < m; x++) d += A[i + x * n] * B[x + j * m]; break;
        case 2: for (x = 0; x < m; x++) d += A[i + x * n] * B[j + x * k]; break;
        case 3: for (x = 0; x < m; x++) d += A[x + i * m] * B[x + j * m]; break;
        case 4: for (x = 0; x < m; x++) d += A[x + i * m] * B[j + x * k]; break;
        }
        if (beta == 0.0) C[i + j * n] = alpha * d; else C[i + j * n] = alpha * d + beta * C[i + j * n];
    }
}
extern void matmul33(const char* tr, const double* A, const double* B, const double* C,
    int n, int p, int q, int m, double* D)
{
    char tr_[8];
    double* T = mat(n, q);
    matmul(tr, n, q, p, 1.0, A, B, 0.0, T);
    sprintf(tr_, "N%c", tr[2]);
    matmul(tr_, n, m, q, 1.0, T, C, 0.0, D); free(T);
}

/* LU decomposition ----------------------------------------------------------*/
static int ludcmp(double* A, int n, int* indx, double* d)
{
    double big, s, tmp, * vv = mat(n, 1);
    int i, imax = 0, j, k;

    *d = 1.0;
    for (i = 0; i < n; i++) {
        big = 0.0; for (j = 0; j < n; j++) if ((tmp = fabs(A[i + j * n])) > big) big = tmp;
        if (big > 0.0) vv[i] = 1.0 / big; else { free(vv); return -1; }
    }
    for (j = 0; j < n; j++) {
        for (i = 0; i < j; i++) {
            s = A[i + j * n]; for (k = 0; k < i; k++) s -= A[i + k * n] * A[k + j * n]; A[i + j * n] = s;
        }
        big = 0.0;
        for (i = j; i < n; i++) {
            s = A[i + j * n]; for (k = 0; k < j; k++) s -= A[i + k * n] * A[k + j * n]; A[i + j * n] = s;
            if ((tmp = vv[i] * fabs(s)) >= big) { big = tmp; imax = i; }
        }
        if (j != imax) {
            for (k = 0; k < n; k++) {
                tmp = A[imax + k * n]; A[imax + k * n] = A[j + k * n]; A[j + k * n] = tmp;
            }
            *d = -(*d); vv[imax] = vv[j];
        }
        indx[j] = imax;
        if (A[j + j * n] == 0.0) { free(vv); return -1; }
        if (j != n - 1) {
            tmp = 1.0 / A[j + j * n]; for (i = j + 1; i < n; i++) A[i + j * n] *= tmp;
        }
    }
    free(vv);
    return 0;
}

/* LU back-substitution ------------------------------------------------------*/
static void lubksb(const double* A, int n, const int* indx, double* b)
{
    double s;
    int i, ii = -1, ip, j;

    for (i = 0; i < n; i++) {
        ip = indx[i]; s = b[ip]; b[ip] = b[i];
        if (ii >= 0) for (j = ii; j < i; j++) s -= A[i + j * n] * b[j]; else if (s) ii = i;
        b[i] = s;
    }
    for (i = n - 1; i >= 0; i--) {
        s = b[i]; for (j = i + 1; j < n; j++) s -= A[i + j * n] * b[j]; b[i] = s / A[i + i * n];
    }
}
/* inverse of matrix -----------------------------------------------------------
* inverse of matrix (A=A^-1)
* args   : double *A        IO  matrix (n x n)
*          int    n         I   size of matrix A
* return : status (0:ok,0>:error)
*-----------------------------------------------------------------------------*/
extern int matinv(double* A, int n)
{
    double d, * B;
    int i, j, * indx;

    indx = imat(n, 1); B = mat(n, n);
    if (!indx || !B)
        return -1;
    matcpy(B, A, n, n);
    if (ludcmp(B, n, indx, &d)) { free(indx); free(B); return -2; }
    for (j = 0; j < n; j++) {
        for (i = 0; i < n; i++) A[i + j * n] = 0.0; A[j + j * n] = 1.0;
        lubksb(B, n, indx, A + j * n);
    }
    free(indx); free(B);
    return 0;
}
/* solve linear equation -------------------------------------------------------
* solve linear equation (X=A\Y or X=A'\Y)
* args   : char   *tr       I   transpose flag ("N":normal,"T":transpose)
*          double *A        I   input matrix A (n x n)
*          double *Y        I   input matrix Y (n x m)
*          int    n,m       I   size of matrix A,Y
*          double *X        O   X=A\Y or X=A'\Y (n x m)
* return : status (0:ok,0>:error)
* notes  : matirix stored by column-major order (fortran convention)
*          X can be same as Y
*-----------------------------------------------------------------------------*/
extern int solve(const char* tr, const double* A, const double* Y, int n,
    int m, double* X)
{
    double* B = mat(n, n);
    int info;
    if (!B)
        return -1;
    matcpy(B, A, n, n);
    if (!(info = matinv(B, n))) matmul(tr[0] == 'N' ? "NN" : "TN", n, m, n, 1.0, B, Y, 0.0, X);
    free(B);
    return info;
}


/* least square estimation -----------------------------------------------------
* least square estimation by solving normal equation (x=(A*A')^-1*A*y)
* args   : double *A        I   transpose of (weighted) design matrix (n x m)
*          double *y        I   (weighted) measurements (m x 1)
*          int    n,m       I   number of parameters and measurements (n<=m)
*          double *x        O   estmated parameters (n x 1)
*          double *Q        O   esimated parameters covariance matrix (n x n)
* return : status (0:ok,0>:error)
* notes  : for weighted least square, replace A and y by A*w and w*y (w=W^(1/2))
*          matirix stored by column-major order (fortran convention)*/
extern int lsq(const double* A, const double* y, int n, int m, double* x,
    double* Q)
{
    double* Ay;
    int info;

    if (m < n) return -1;
    Ay = mat(n, 1);
    matmul("NN", n, 1, m, 1.0, A, y, 0.0, Ay); /* Ay=A*y */
    matmul("NT", n, n, m, 1.0, A, A, 0.0, Q);  /* Q=A*A' */
    if (!(info = matinv(Q, n))) matmul("NN", n, 1, n, 1.0, Q, Ay, 0.0, x); /* x=Q^-1*Ay */
    free(Ay);
    return info;
}

/* inner product ---------------------------------------------------------------
* inner product of vectors
* args   : double *a,*b     I   vector a,b (n x 1)
*          int    n         I   size of vector a,b
* return : a'*b
*-----------------------------------------------------------------------------*/
extern double dot(const double* a, const double* b, int n)
{
    double c = 0.0;

    while (--n >= 0) c += a[n] * b[n];
    return c;
}
/* euclid norm -----------------------------------------------------------------
* euclid norm of vector
* args   : double *a        I   vector a (n x 1)
*          int    n         I   size of vector a
* return : || a ||
*-----------------------------------------------------------------------------*/
extern double norm(const double* a, int n)
{
    return sqrt(dot(a, a, n));
}

/* outer product of 3d vectors -------------------------------------------------
* outer product of 3d vectors
* args   : double *a,*b     I   vector a,b (3 x 1)
*          double *c        O   outer product (a x b) (3 x 1)
* return : none
*-----------------------------------------------------------------------------*/
extern void cross3(const double* a, const double* b, double* c)
{
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
}

/* normalize 3d vector ---------------------------------------------------------
* normalize 3d vector
* args   : double *a        I   vector a (3 x 1)
*          double *b        O   normlized vector (3 x 1) || b || = 1
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int normv3(const double* a, double* b)
{
    double r;
    if ((r = norm(a, 3)) <= 0.0) return 0;
    b[0] = a[0] / r;
    b[1] = a[1] / r;
    b[2] = a[2] / r;
    return 1;
}

/* transform ecef to geodetic postion ------------------------------------------
* transform ecef position to geodetic position
* args   : double *r        I   ecef position {x,y,z} (m)
*          double *pos      O   geodetic position {lat,lon,h} (rad,m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
extern void ecef2pos(const double* r, double* pos)
{
    double e2 = FE_WGS84 * (2.0 - FE_WGS84), r2, z, zk, v = RE_WGS84, sinp;
    r2 = r[0] * r[0] + r[1] * r[1];
    for (z = r[2], zk = 0.0; fabs(z - zk) >= 1E-4;) {
        zk = z;
        sinp = z / sqrt(r2 + z * z);
        v = RE_WGS84 / sqrt(1.0 - e2 * sinp * sinp);
        z = r[2] + v * e2 * sinp;
    }
    pos[0] = r2 > 1E-12 ? atan(z / sqrt(r2)) : (r[2] > 0.0 ? PI / 2.0 : -PI / 2.0);
    pos[1] = r2 > 1E-12 ? atan2(r[1], r[0]) : 0.0;
    pos[2] = sqrt(r2 + z * z) - v;
}
/* geometric distance ----------------------------------------------------------
* compute geometric distance and receiver-to-satellite unit vector
* args   : double *rs       I   satellilte position (ecef at transmission) (m)
*          double *rr       I   receiver position (ecef at reception) (m)
*          double *e        O   line-of-sight vector (ecef)
* return : geometric distance (m) (0>:error/no satellite position)
* notes  : distance includes sagnac effect correction
*-----------------------------------------------------------------------------*/
extern double geodist(const double* rs, const double* rr, double* e)
{
    double r;
    int i;

    if (sos3(rs) < RE_WGS84 * RE_WGS84)//ephemeris unavailable 
        return -1.0;
    for (i = 0; i < 3; i++) e[i] = rs[i] - rr[i];
    r = norm3(e);
    for (i = 0; i < 3; i++) e[i] /= r;
    return r + OMGE * (rs[0] * rr[1] - rs[1] * rr[0]) / CLIGHT;
}
/* ecef to local coordinate transfromation matrix ------------------------------
* compute ecef to local coordinate transfromation matrix
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *E        O   ecef to local coord transformation matrix (3x3)
* return : none
* notes  : matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern void xyz2enu(const double* pos, double* E)
{
    double sinp = sin(pos[0]), cosp = cos(pos[0]), sinl = sin(pos[1]), cosl = cos(pos[1]);

    E[0] = -sinl;      E[3] = cosl;       E[6] = 0.0;
    E[1] = -sinp * cosl; E[4] = -sinp * sinl; E[7] = cosp;
    E[2] = cosp * cosl;  E[5] = cosp * sinl;  E[8] = sinp;
}
/* transform local enu coordinate covariance to xyz-ecef -----------------------
* transform local enu covariance to xyz-ecef coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *Q        I   covariance in local enu coordinate
*          double *P        O   covariance in xyz-ecef coordinate
* return : none
*-----------------------------------------------------------------------------*/
extern void covecef(const double* pos, const double* Q, double* P)
{
    double E[9], EQ[9];

    xyz2enu(pos, E);
    matmul("TN", 3, 3, 3, 1.0, E, Q, 0.0, EQ);
    matmul("NN", 3, 3, 3, 1.0, EQ, E, 0.0, P);
}
/* transform ecef vector to local tangental coordinate -------------------------
* transform ecef vector to local tangental coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *r        I   vector in ecef coordinate {x,y,z}
*          double *e        O   vector in local tangental coordinate {e,n,u}
* return : none
*-----------------------------------------------------------------------------*/
extern void ecef2enu(const double* pos, const double* r, double* e)
{
    double E[9];

    xyz2enu(pos, E);
    matmul("NN", 3, 1, 3, 1.0, E, r, 0.0, e);
}
extern void enu2ecef(const double* pos, const double* e, double* r)
{
    double E[9];

    xyz2enu(pos, E);
    matmul("TN", 3, 1, 3, 1.0, E, e, 0.0, r);
}
/* satellite azimuth/elevation angle -------------------------------------------
* compute satellite azimuth/elevation angle
* args   : double *pos      I   geodetic position {lat,lon,h} (rad,m)
*          double *e        I   receiver-to-satellilte unit vevtor (ecef)
*          double *azel     IO  azimuth/elevation {az,el} (rad) (NULL: no output)
*                               (0.0<=azel[0]<2*pi,-pi/2<=azel[1]<=pi/2)
* return : elevation angle (rad)
*-----------------------------------------------------------------------------*/
extern double satazel(const double* pos, const double* e, double* azel)
{
    double az = 0.0, el = PI / 2.0, enu[3];

    if (pos[2] > -RE_WGS84) {
        ecef2enu(pos, e, enu);
        az = enu[0] * enu[0] + enu[1] * enu[1] < 1E-12 ? 0.0 : atan2(enu[0], enu[1]);
        if (az < 0.0) az += 2 * PI;
        el = asin(enu[2]);
    }
    if (azel) { azel[0] = az; azel[1] = el; }
    return el;
}

/* coordinate rotation matrix ------------------------------------------------*/
#define Rx(t,X) do { \
    (X)[0]=1.0; (X)[1]=(X)[2]=(X)[3]=(X)[6]=0.0; \
    (X)[4]=(X)[8]=cos(t); (X)[7]=sin(t); (X)[5]=-(X)[7]; \
} while (0)

#define Ry(t,X) do { \
    (X)[4]=1.0; (X)[1]=(X)[3]=(X)[5]=(X)[7]=0.0; \
    (X)[0]=(X)[8]=cos(t); (X)[2]=sin(t); (X)[6]=-(X)[2]; \
} while (0)

#define Rz(t,X) do { \
    (X)[8]=1.0; (X)[2]=(X)[5]=(X)[6]=(X)[7]=0.0; \
    (X)[0]=(X)[4]=cos(t); (X)[3]=sin(t); (X)[1]=-(X)[3]; \
} while (0)

/* astronomical arguments: f={l,l',F,D,OMG} (rad) ----------------------------*/
static void ast_args(double t, double* f)
{
    static const double fc[][5] = { /* coefficients for iau 1980 nutation */
        { 134.96340251, 1717915923.2178,  31.8792,  0.051635, -0.00024470},
        { 357.52910918,  129596581.0481,  -0.5532,  0.000136, -0.00001149},
        {  93.27209062, 1739527262.8478, -12.7512, -0.001037,  0.00000417},
        { 297.85019547, 1602961601.2090,  -6.3706,  0.006593, -0.00003169},
        { 125.04455501,   -6962890.2665,   7.4722,  0.007702, -0.00005939}
    };
    double tt[4];
    int i, j;

    for (tt[0] = t, i = 1; i < 4; i++) tt[i] = tt[i - 1] * t;
    for (i = 0; i < 5; i++) {
        f[i] = fc[i][0] * 3600.0;
        for (j = 0; j < 4; j++) f[i] += fc[i][j + 1] * tt[j];
        f[i] = fmod(f[i] * AS2R, 2.0 * PI);
    }
}

/* iau 1980 nutation ---------------------------------------------------------*/
static void nut_iau1980(double t, const double* f, double* dpsi, double* deps)
{
    static const double nut[106][10] = {
        {   0,   0,   0,   0,   1, -6798.4, -171996, -174.2, 92025,   8.9},
        {   0,   0,   2,  -2,   2,   182.6,  -13187,   -1.6,  5736,  -3.1},
        {   0,   0,   2,   0,   2,    13.7,   -2274,   -0.2,   977,  -0.5},
        {   0,   0,   0,   0,   2, -3399.2,    2062,    0.2,  -895,   0.5},
        {   0,  -1,   0,   0,   0,  -365.3,   -1426,    3.4,    54,  -0.1},
        {   1,   0,   0,   0,   0,    27.6,     712,    0.1,    -7,   0.0},
        {   0,   1,   2,  -2,   2,   121.7,    -517,    1.2,   224,  -0.6},
        {   0,   0,   2,   0,   1,    13.6,    -386,   -0.4,   200,   0.0},
        {   1,   0,   2,   0,   2,     9.1,    -301,    0.0,   129,  -0.1},
        {   0,  -1,   2,  -2,   2,   365.2,     217,   -0.5,   -95,   0.3},
        {  -1,   0,   0,   2,   0,    31.8,     158,    0.0,    -1,   0.0},
        {   0,   0,   2,  -2,   1,   177.8,     129,    0.1,   -70,   0.0},
        {  -1,   0,   2,   0,   2,    27.1,     123,    0.0,   -53,   0.0},
        {   1,   0,   0,   0,   1,    27.7,      63,    0.1,   -33,   0.0},
        {   0,   0,   0,   2,   0,    14.8,      63,    0.0,    -2,   0.0},
        {  -1,   0,   2,   2,   2,     9.6,     -59,    0.0,    26,   0.0},
        {  -1,   0,   0,   0,   1,   -27.4,     -58,   -0.1,    32,   0.0},
        {   1,   0,   2,   0,   1,     9.1,     -51,    0.0,    27,   0.0},
        {  -2,   0,   0,   2,   0,  -205.9,     -48,    0.0,     1,   0.0},
        {  -2,   0,   2,   0,   1,  1305.5,      46,    0.0,   -24,   0.0},
        {   0,   0,   2,   2,   2,     7.1,     -38,    0.0,    16,   0.0},
        {   2,   0,   2,   0,   2,     6.9,     -31,    0.0,    13,   0.0},
        {   2,   0,   0,   0,   0,    13.8,      29,    0.0,    -1,   0.0},
        {   1,   0,   2,  -2,   2,    23.9,      29,    0.0,   -12,   0.0},
        {   0,   0,   2,   0,   0,    13.6,      26,    0.0,    -1,   0.0},
        {   0,   0,   2,  -2,   0,   173.3,     -22,    0.0,     0,   0.0},
        {  -1,   0,   2,   0,   1,    27.0,      21,    0.0,   -10,   0.0},
        {   0,   2,   0,   0,   0,   182.6,      17,   -0.1,     0,   0.0},
        {   0,   2,   2,  -2,   2,    91.3,     -16,    0.1,     7,   0.0},
        {  -1,   0,   0,   2,   1,    32.0,      16,    0.0,    -8,   0.0},
        {   0,   1,   0,   0,   1,   386.0,     -15,    0.0,     9,   0.0},
        {   1,   0,   0,  -2,   1,   -31.7,     -13,    0.0,     7,   0.0},
        {   0,  -1,   0,   0,   1,  -346.6,     -12,    0.0,     6,   0.0},
        {   2,   0,  -2,   0,   0, -1095.2,      11,    0.0,     0,   0.0},
        {  -1,   0,   2,   2,   1,     9.5,     -10,    0.0,     5,   0.0},
        {   1,   0,   2,   2,   2,     5.6,      -8,    0.0,     3,   0.0},
        {   0,  -1,   2,   0,   2,    14.2,      -7,    0.0,     3,   0.0},
        {   0,   0,   2,   2,   1,     7.1,      -7,    0.0,     3,   0.0},
        {   1,   1,   0,  -2,   0,   -34.8,      -7,    0.0,     0,   0.0},
        {   0,   1,   2,   0,   2,    13.2,       7,    0.0,    -3,   0.0},
        {  -2,   0,   0,   2,   1,  -199.8,      -6,    0.0,     3,   0.0},
        {   0,   0,   0,   2,   1,    14.8,      -6,    0.0,     3,   0.0},
        {   2,   0,   2,  -2,   2,    12.8,       6,    0.0,    -3,   0.0},
        {   1,   0,   0,   2,   0,     9.6,       6,    0.0,     0,   0.0},
        {   1,   0,   2,  -2,   1,    23.9,       6,    0.0,    -3,   0.0},
        {   0,   0,   0,  -2,   1,   -14.7,      -5,    0.0,     3,   0.0},
        {   0,  -1,   2,  -2,   1,   346.6,      -5,    0.0,     3,   0.0},
        {   2,   0,   2,   0,   1,     6.9,      -5,    0.0,     3,   0.0},
        {   1,  -1,   0,   0,   0,    29.8,       5,    0.0,     0,   0.0},
        {   1,   0,   0,  -1,   0,   411.8,      -4,    0.0,     0,   0.0},
        {   0,   0,   0,   1,   0,    29.5,      -4,    0.0,     0,   0.0},
        {   0,   1,   0,  -2,   0,   -15.4,      -4,    0.0,     0,   0.0},
        {   1,   0,  -2,   0,   0,   -26.9,       4,    0.0,     0,   0.0},
        {   2,   0,   0,  -2,   1,   212.3,       4,    0.0,    -2,   0.0},
        {   0,   1,   2,  -2,   1,   119.6,       4,    0.0,    -2,   0.0},
        {   1,   1,   0,   0,   0,    25.6,      -3,    0.0,     0,   0.0},
        {   1,  -1,   0,  -1,   0, -3232.9,      -3,    0.0,     0,   0.0},
        {  -1,  -1,   2,   2,   2,     9.8,      -3,    0.0,     1,   0.0},
        {   0,  -1,   2,   2,   2,     7.2,      -3,    0.0,     1,   0.0},
        {   1,  -1,   2,   0,   2,     9.4,      -3,    0.0,     1,   0.0},
        {   3,   0,   2,   0,   2,     5.5,      -3,    0.0,     1,   0.0},
        {  -2,   0,   2,   0,   2,  1615.7,      -3,    0.0,     1,   0.0},
        {   1,   0,   2,   0,   0,     9.1,       3,    0.0,     0,   0.0},
        {  -1,   0,   2,   4,   2,     5.8,      -2,    0.0,     1,   0.0},
        {   1,   0,   0,   0,   2,    27.8,      -2,    0.0,     1,   0.0},
        {  -1,   0,   2,  -2,   1,   -32.6,      -2,    0.0,     1,   0.0},
        {   0,  -2,   2,  -2,   1,  6786.3,      -2,    0.0,     1,   0.0},
        {  -2,   0,   0,   0,   1,   -13.7,      -2,    0.0,     1,   0.0},
        {   2,   0,   0,   0,   1,    13.8,       2,    0.0,    -1,   0.0},
        {   3,   0,   0,   0,   0,     9.2,       2,    0.0,     0,   0.0},
        {   1,   1,   2,   0,   2,     8.9,       2,    0.0,    -1,   0.0},
        {   0,   0,   2,   1,   2,     9.3,       2,    0.0,    -1,   0.0},
        {   1,   0,   0,   2,   1,     9.6,      -1,    0.0,     0,   0.0},
        {   1,   0,   2,   2,   1,     5.6,      -1,    0.0,     1,   0.0},
        {   1,   1,   0,  -2,   1,   -34.7,      -1,    0.0,     0,   0.0},
        {   0,   1,   0,   2,   0,    14.2,      -1,    0.0,     0,   0.0},
        {   0,   1,   2,  -2,   0,   117.5,      -1,    0.0,     0,   0.0},
        {   0,   1,  -2,   2,   0,  -329.8,      -1,    0.0,     0,   0.0},
        {   1,   0,  -2,   2,   0,    23.8,      -1,    0.0,     0,   0.0},
        {   1,   0,  -2,  -2,   0,    -9.5,      -1,    0.0,     0,   0.0},
        {   1,   0,   2,  -2,   0,    32.8,      -1,    0.0,     0,   0.0},
        {   1,   0,   0,  -4,   0,   -10.1,      -1,    0.0,     0,   0.0},
        {   2,   0,   0,  -4,   0,   -15.9,      -1,    0.0,     0,   0.0},
        {   0,   0,   2,   4,   2,     4.8,      -1,    0.0,     0,   0.0},
        {   0,   0,   2,  -1,   2,    25.4,      -1,    0.0,     0,   0.0},
        {  -2,   0,   2,   4,   2,     7.3,      -1,    0.0,     1,   0.0},
        {   2,   0,   2,   2,   2,     4.7,      -1,    0.0,     0,   0.0},
        {   0,  -1,   2,   0,   1,    14.2,      -1,    0.0,     0,   0.0},
        {   0,   0,  -2,   0,   1,   -13.6,      -1,    0.0,     0,   0.0},
        {   0,   0,   4,  -2,   2,    12.7,       1,    0.0,     0,   0.0},
        {   0,   1,   0,   0,   2,   409.2,       1,    0.0,     0,   0.0},
        {   1,   1,   2,  -2,   2,    22.5,       1,    0.0,    -1,   0.0},
        {   3,   0,   2,  -2,   2,     8.7,       1,    0.0,     0,   0.0},
        {  -2,   0,   2,   2,   2,    14.6,       1,    0.0,    -1,   0.0},
        {  -1,   0,   0,   0,   2,   -27.3,       1,    0.0,    -1,   0.0},
        {   0,   0,  -2,   2,   1,  -169.0,       1,    0.0,     0,   0.0},
        {   0,   1,   2,   0,   1,    13.1,       1,    0.0,     0,   0.0},
        {  -1,   0,   4,   0,   2,     9.1,       1,    0.0,     0,   0.0},
        {   2,   1,   0,  -2,   0,   131.7,       1,    0.0,     0,   0.0},
        {   2,   0,   0,   2,   0,     7.1,       1,    0.0,     0,   0.0},
        {   2,   0,   2,  -2,   1,    12.8,       1,    0.0,    -1,   0.0},
        {   2,   0,  -2,   0,   1,  -943.2,       1,    0.0,     0,   0.0},
        {   1,  -1,   0,  -2,   0,   -29.3,       1,    0.0,     0,   0.0},
        {  -1,   0,   0,   1,   1,  -388.3,       1,    0.0,     0,   0.0},
        {  -1,  -1,   0,   2,   1,    35.0,       1,    0.0,     0,   0.0},
        {   0,   1,   0,   1,   0,    27.3,       1,    0.0,     0,   0.0}
    };
    double ang;
    int i, j;

    *dpsi = *deps = 0.0;

    for (i = 0; i < 106; i++) {
        ang = 0.0;
        for (j = 0; j < 5; j++) ang += nut[i][j] * f[j];
        *dpsi += (nut[i][6] + nut[i][7] * t) * sin(ang);
        *deps += (nut[i][8] + nut[i][9] * t) * cos(ang);
    }
    *dpsi *= 1E-4 * AS2R; /* 0.1 mas -> rad */
    *deps *= 1E-4 * AS2R;
}
/* ionospheric pierce point position -------------------------------------------
* compute ionospheric pierce point (ipp) position and slant factor
* args   : double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double re        I   earth radius (km)
*          double hion      I   altitude of ionosphere (km)
*          double *posp     O   pierce point position {lat,lon,h} (rad,m)
* return : slant factor
* notes  : see ref [2], only valid on the earth surface
*          fixing bug on ref [2] A.4.4.10.1 A-22,23
*-----------------------------------------------------------------------------*/
extern double ionppp(const double* pos, const double* azel, double re,
    double hion, double* posp)
{
    double cosaz, rp, ap, sinap, tanap;

    rp = re / (re + hion) * cos(azel[1]);
    ap = PI / 2.0 - azel[1] - asin(rp);
    sinap = sin(ap);
    tanap = tan(ap);
    cosaz = cos(azel[0]);
    posp[0] = asin(sin(pos[0]) * cos(ap) + cos(pos[0]) * sinap * cosaz);

    if ((pos[0] > 70.0 * D2R && tanap * cosaz > tan(PI / 2.0 - pos[0])) ||
        (pos[0]<-70.0 * D2R && -tanap * cosaz>tan(PI / 2.0 + pos[0]))) {
        posp[1] = pos[1] + PI - asin(sinap * sin(azel[0]) / cos(posp[0]));
    }
    else {
        posp[1] = pos[1] + asin(sinap * sin(azel[0]) / cos(posp[0]));
    }
    return 1.0 / sqrt(1.0 - rp * rp);
}
/* troposphere model -----------------------------------------------------------
* compute tropospheric delay by standard atmosphere and saastamoinen model
* args   : gtime_t time     I   time
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double humi      I   relative humidity
* return : tropospheric delay (m)
*-----------------------------------------------------------------------------*/
extern double tropmodel(gtime_t time, const double* pos, const double* azel,
    double humi)
{
    const double temp0 = 15.0; /* temparature at sea level */
    double hgt, pres, temp, e, z, trph, trpw;

    if (pos[2] < -100.0 || 1E4 < pos[2] || azel[1] <= 0) return 0.0;

    /* standard atmosphere */
    hgt = pos[2] < 0.0 ? 0.0 : pos[2];

    pres = 1013.25 * pow(1.0 - 2.2557E-5 * hgt, 5.2568);
    temp = temp0 - 6.5E-3 * hgt + 273.16;
    e = 6.108 * humi * exp((17.15 * temp - 4684.0) / (temp - 38.45));

    /* saastamoninen model */
    z = PI / 2.0 - azel[1];
    trph = 0.0022768 * pres / (1.0 - 0.00266 * cos(2.0 * pos[0]) - 0.00028 * hgt / 1E3) / cos(z);
    trpw = 0.002277 * (1255.0 / temp + 0.05) * e / cos(z);
    return trph + trpw;
}
/* ionosphere model ------------------------------------------------------------
* compute ionospheric delay by broadcast ionosphere model (klobuchar model)
* args   : gtime_t t        I   time (gpst)
*          double *ion      I   iono model parameters {a0,a1,a2,a3,b0,b1,b2,b3}
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
* return : ionospheric delay (L1) (m)
*-----------------------------------------------------------------------------*/
extern double ionmodel(gtime_t t, const double* ion, const double* pos,
    const double* azel)
{
    const double ion_default[] = { /* 2004/1/1 */
        0.1118E-07,-0.7451E-08,-0.5961E-07, 0.1192E-06,
        0.1167E+06,-0.2294E+06,-0.1311E+06, 0.1049E+07
    };
    double tt, f, psi, phi, lam, amp, per, x;
    int week;

    if (pos[2] < -1E3 || azel[1] <= 0) return 0.0;
    if (norm(ion, 8) <= 0.0) ion = ion_default;

    /* earth centered angle (semi-circle) */
    psi = 0.0137 / (azel[1] / PI + 0.11) - 0.022;

    /* subionospheric latitude/longitude (semi-circle) */
    phi = pos[0] / PI + psi * cos(azel[0]);
    if (phi > 0.416) phi = 0.416;
    else if (phi < -0.416) phi = -0.416;
    lam = pos[1] / PI + psi * sin(azel[0]) / cos(phi * PI);

    /* geomagnetic latitude (semi-circle) */
    phi += 0.064 * cos((lam - 1.617) * PI);

    /* local time (s) */
    tt = 43200.0 * lam + time2gpst(t, &week);
    tt -= floor(tt / 86400.0) * 86400.0; /* 0<=tt<86400 */

    /* slant factor */
    f = 1.0 + 16.0 * pow(0.53 - azel[1] / PI, 3.0);

    /* ionospheric delay */
    amp = ion[0] + phi * (ion[1] + phi * (ion[2] + phi * ion[3]));
    per = ion[4] + phi * (ion[5] + phi * (ion[6] + phi * ion[7]));
    amp = amp < 0.0 ? 0.0 : amp;
    per = per < 72000.0 ? 72000.0 : per;
    x = 2.0 * PI * (tt - 50400.0) / per;

    return CLIGHT * f * (fabs(x) < 1.57 ? 5E-9 + amp * (1.0 + x * x * (-0.5 + x * x / 24.0)) : 5E-9);
}
/* compute dops ----------------------------------------------------------------
* compute DOP (dilution of precision)
* args   : int    ns        I   number of satellites
*          double *azel     I   satellite azimuth/elevation angle (rad)
*          double elmin     I   elevation cutoff angle (rad)
*          double *dop      O   DOPs {GDOP,PDOP,HDOP,VDOP}
* return : none
* notes  : dop[0]-[3] return 0 in case of dop computation error
*-----------------------------------------------------------------------------*/
#define SQRT(x)     ((x)<0.0?0.0:sqrt(x))

extern void dops(int ns, const double* azel, double elmin, double* dop)
{
    double H[4 * MAXSAT], Q[16], cosel, sinel;
    int i, n;

    for (i = 0; i < 4; i++) dop[i] = 0.0;
    for (i = n = 0; i < ns && i < MAXSAT; i++) {
        if (azel[1 + i * 2] < elmin || azel[1 + i * 2] <= 0.0) continue;
        cosel = cos(azel[1 + i * 2]);
        sinel = sin(azel[1 + i * 2]);
        H[4 * n] = cosel * sin(azel[i * 2]);
        H[1 + 4 * n] = cosel * cos(azel[i * 2]);
        H[2 + 4 * n] = sinel;
        H[3 + 4 * n++] = 1.0;
    }
    if (n < 4) return;

    matmul("NT", 4, 4, n, 1.0, H, H, 0.0, Q);
    if (!matinv(Q, 4)) {
        dop[0] = SQRT(Q[0] + Q[5] + Q[10] + Q[15]); /* GDOP */
        dop[1] = SQRT(Q[0] + Q[5] + Q[10]);       /* PDOP */
        dop[2] = SQRT(Q[0] + Q[5]);             /* HDOP */
        dop[3] = SQRT(Q[10]);                 /* VDOP */
    }
}
/* time to calendar day/time ---------------------------------------------------
* convert gtime_t struct to calendar day/time
* args   : gtime_t t        I   gtime_t struct
*          double *ep       O   day/time {year,month,day,hour,min,sec}
* return : none
* notes  : proper in 1970-2037 or 1970-2099 (64bit time_t)
*-----------------------------------------------------------------------------*/
extern void time2epoch(gtime_t t, double* ep)
{
    const int mday[] = { /* # of days in a month */
        31,28,31,30,31,30,31,31,30,31,30,31,31,28,31,30,31,30,31,31,30,31,30,31,
        31,29,31,30,31,30,31,31,30,31,30,31,31,28,31,30,31,30,31,31,30,31,30,31
    };
    int days, sec, mon, day;

    /* leap year if year%4==0 in 1901-2099 */
    days = (int)(t.time / 86400);
    sec = (int)(t.time - (time_t)days * 86400);
    for (day = days % 1461, mon = 0; mon < 48; mon++) {
        if (day >= mday[mon]) day -= mday[mon]; else break;
    }
    ep[0] = 1970 + days / 1461 * 4 + mon / 12; ep[1] = mon % 12 + 1; ep[2] = day + 1;
    ep[3] = sec / 3600; ep[4] = sec % 3600 / 60; ep[5] = sec % 60 + t.frac;
}

#if 0
/* time to string --------------------------------------------------------------
* convert gtime_t struct to string
* args   : gtime_t t        I   gtime_t struct
*          char   *s        O   string ("yyyy/mm/dd hh:mm:ss.ssss")
*          int    n         I   number of decimals
* return : none
*-----------------------------------------------------------------------------*/
extern void time_output(gtime_t t, char* s, int n)
{
    double ep[6], sow;
    int week;
    char* sep = " ";

    if (n < 0) n = 0; else if (n > 12) n = 12;
    if (1.0 - t.frac < 0.5 / pow(10.0, n)) { t.time++; t.frac = 0.0; };
    time2epoch(t, ep);
    //sprintf(s,"%04.0f/%02.0f/%02.0f %02.0f:%02.0f:%0*.*f",ep[0],ep[1],ep[2],
    //    ep[3],ep[4],n<=0?2:n+3,n<=0?0:n,ep[5]);

    sow = time2gpst(t, &week);
    sprintf(s, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%4d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, week, sep, sow, sep);
}
#endif


extern void time2str(gtime_t t, char* s, int n)
{
    double ep[6];
    if (n < 0) n = 0; else if (n > 12) n = 12;
    if (1.0 - t.frac < 0.5 / pow(10.0, n)) { t.time++; t.frac = 0.0; };
    time2epoch(t, ep);
    sprintf(s, "%04.0f/%02.0f/%02.0f %02.0f:%02.0f:%0*.*f", ep[0], ep[1], ep[2],
        ep[3], ep[4], n <= 0 ? 2 : n + 3, n <= 0 ? 0 : n, ep[5]);
}


extern char* time_str(gtime_t t, int n)
{
    static char buff[64];
    time2str(t, buff, n);
    return buff;
}
/* transform covariance to local tangental coordinate --------------------------
* transform ecef covariance to local tangental coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *P        I   covariance in ecef coordinate
*          double *Q        O   covariance in local tangental coordinate
* return : none
*-----------------------------------------------------------------------------*/
extern void covenu(const double* pos, const double* P, double* Q)
{
    double E[9], EP[9];

    xyz2enu(pos, E);
    matmul("NN", 3, 3, 3, 1.0, E, P, 0.0, EP);
    matmul("NT", 3, 3, 3, 1.0, EP, E, 0.0, Q);
}
/* time to day of year ---------------------------------------------------------
* convert time to day of year
* args   : gtime_t t        I   gtime_t struct
* return : day of year (days)
*-----------------------------------------------------------------------------*/
extern double time2doy(gtime_t t)
{
    double ep[6];

    time2epoch(t, ep);
    ep[1] = ep[2] = 1.0; ep[3] = ep[4] = ep[5] = 0.0;
    return timediff(t, epoch2time(ep)) / 86400.0 + 1.0;
}
static double interpc(const double coef[], double lat)
{
    int i = (int)(lat / 15.0);
    if (i < 1) return coef[0]; else if (i > 4) return coef[4];
    return coef[i - 1] * (1.0 - lat / 15.0 + i) + coef[i] * (lat / 15.0 - i);
}
static double mapf(double el, double a, double b, double c)
{
    double sinel = sin(el);
    return (1.0 + a / (1.0 + b / (1.0 + c))) / (sinel + (a / (sinel + b / (sinel + c))));
}
static double nmf(gtime_t time, const double pos[], const double azel[],
    double* mapfw)
{
    /* ref [5] table 3 */
    /* hydro-ave-a,b,c, hydro-amp-a,b,c, wet-a,b,c at latitude 15,30,45,60,75 */
    const double coef[][5] = {
        { 1.2769934E-3, 1.2683230E-3, 1.2465397E-3, 1.2196049E-3, 1.2045996E-3},
        { 2.9153695E-3, 2.9152299E-3, 2.9288445E-3, 2.9022565E-3, 2.9024912E-3},
        { 62.610505E-3, 62.837393E-3, 63.721774E-3, 63.824265E-3, 64.258455E-3},

        { 0.0000000E-0, 1.2709626E-5, 2.6523662E-5, 3.4000452E-5, 4.1202191E-5},
        { 0.0000000E-0, 2.1414979E-5, 3.0160779E-5, 7.2562722E-5, 11.723375E-5},
        { 0.0000000E-0, 9.0128400E-5, 4.3497037E-5, 84.795348E-5, 170.37206E-5},

        { 5.8021897E-4, 5.6794847E-4, 5.8118019E-4, 5.9727542E-4, 6.1641693E-4},
        { 1.4275268E-3, 1.5138625E-3, 1.4572752E-3, 1.5007428E-3, 1.7599082E-3},
        { 4.3472961E-2, 4.6729510E-2, 4.3908931E-2, 4.4626982E-2, 5.4736038E-2}
    };
    const double aht[] = { 2.53E-5, 5.49E-3, 1.14E-3 }; /* height correction */

    double y, cosy, ah[3], aw[3], dm, el = azel[1], lat = pos[0] * R2D, hgt = pos[2];
    int i;

    if (el <= 0.0) {
        if (mapfw) *mapfw = 0.0;
        return 0.0;
    }
    /* year from doy 28, added half a year for southern latitudes */
    y = (time2doy(time) - 28.0) / 365.25 + (lat < 0.0 ? 0.5 : 0.0);

    cosy = cos(2.0 * PI * y);
    lat = fabs(lat);

    for (i = 0; i < 3; i++) {
        ah[i] = interpc(coef[i], lat) - interpc(coef[i + 3], lat) * cosy;
        aw[i] = interpc(coef[i + 6], lat);
    }
    /* ellipsoidal height is used instead of height above sea level */
    dm = (1.0 / sin(el) - mapf(el, aht[0], aht[1], aht[2])) * hgt / 1E3;

    if (mapfw) *mapfw = mapf(el, aw[0], aw[1], aw[2]);

    return mapf(el, ah[0], ah[1], ah[2]) + dm;
}

extern double tropmapf(gtime_t time, const double pos[], const double azel[],
    double* mapfw)
{
#ifdef IERS_MODEL
    const double ep[] = { 2000,1,1,12,0,0 };
    double mjd, lat, lon, hgt, zd, gmfh, gmfw;
#endif
    if (pos[2] < -1000.0 || pos[2]>20000.0) {
        if (mapfw) *mapfw = 0.0;
        return 0.0;
    }
#ifdef IERS_MODEL
    mjd = 51544.5 + (timediff(time, epoch2time(ep))) / 86400.0;
    lat = pos[0];
    lon = pos[1];
    hgt = pos[2] - geoidh(pos); /* height in m (mean sea level) */
    zd = PI / 2.0 - azel[1];

    /* call GMF */
    gmf_(&mjd, &lat, &lon, &hgt, &zd, &gmfh, &gmfw);

    if (mapfw) *mapfw = gmfw;
    return gmfh;
#else
    return nmf(time, pos, azel, mapfw); /* NMF */
#endif
}
/* identity matrix -------------------------------------------------------------
* generate new identity matrix
* args   : int    n         I   number of rows and columns of matrix
* return : matrix pointer (if n<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern double* eye(int n)
{
    double* p;
    int i;

    if ((p = zeros(n, n))) for (i = 0; i < n; i++) p[i + i * n] = 1.0;
    return p;
}
/* kalman filter ---------------------------------------------------------------
* kalman filter state update as follows:
*
*   K=P*H*(H'*P*H+R)^-1, xp=x+K*v, Pp=(I-K*H')*P
*
* args   : double *x        I   states vector (n x 1)
*          double *P        I   covariance matrix of states (n x n)
*          double *H        I   transpose of design matrix (n x m)
*          double *v        I   innovation (measurement - model) (m x 1)
*          double *R        I   covariance matrix of measurement error (m x m)
*          int    n,m       I   number of states and measurements
*          double *xp       O   states vector after update (n x 1)
*          double *Pp       O   covariance matrix of states after update (n x n)
* return : status (0:ok,<0:error)
* notes  : matirix stored by column-major order (fortran convention)
*          if state x[i]==0.0, not updates state x[i]/P[i+i*n]
*-----------------------------------------------------------------------------*/
extern int filter(rtk_t* rtk, double* x, double* P, double* H, double* v, double* R, int n, int m,
    double* xp, double* Pp)
{
    int info, i;
    memset(rtk->F, 0, sizeof(double) * NY * NX);
    memset(rtk->K, 0, sizeof(double) * NY * NX);
    double* F = rtk->F, * K = rtk->K, * I = rtk->I;

    matcpy(xp, x, n, 1);
    matmul("NN", n, m, n, 1.0, P, H, 0.0, F);       /* Q=H'*P*H+R */
    matmul("TN", m, m, n, 1.0, H, F, 1.0, R);
    if (!(info = matinvLambda(rtk, R, m))) {
        matmul("NN", n, m, m, 1.0, F, R, 0.0, K);   /* K=P*H*Q^-1 */
        matmul("NN", n, 1, m, 1.0, K, v, 1.0, xp);  /* xp=x+K*v */

        memset(rtk->I, 0, sizeof(double) * NX * NX);
        I = rtk->I;
        for (i = 0; i < n; i++) I[i + i * n] = 1.0;

        matmul("NT", n, n, m, -1.0, K, H, 1.0, I);  /* Pp=(I-K*H')*P */
        matmul("NN", n, n, n, 1.0, I, P, 0.0, Pp);
    }
    return info;
}

/* transform geodetic to ecef position -----------------------------------------
* transform geodetic position to ecef position
* args   : double *pos      I   geodetic position {lat,lon,h} (rad,m)
*          double *r        O   ecef position {x,y,z} (m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
extern void pos2ecef(const double* pos, double* r)
{
    double sinp = sin(pos[0]), cosp = cos(pos[0]), sinl = sin(pos[1]), cosl = cos(pos[1]);
    double e2 = FE_WGS84 * (2.0 - FE_WGS84), v = RE_WGS84 / sqrt(1.0 - e2 * sinp * sinp);

    r[0] = (v + pos[2]) * cosp * cosl;
    r[1] = (v + pos[2]) * cosp * sinl;
    r[2] = (v * (1.0 - e2) + pos[2]) * sinp;
}
/* test SNR mask ---------------------------------------------------------------
* test SNR mask
* args   : int    base      I   rover or base-station (0:rover,1:base station)
*          int    freq      I   frequency (0:L1,1:L2,2:L3,...)
*          double el        I   elevation angle (rad)
*          double snr       I   C/N0 (dBHz)
*          snrmask_t *mask  I   SNR mask
* return : status (1:masked,0:unmasked)
*-----------------------------------------------------------------------------*/
extern int testsnr(int base, int freq, double el, double snr,
    const snrmask_t* mask)
{
    double minsnr, a;
    int i;

    if (!mask->ena[base] || freq < 0 || freq >= NFREQ) return 0;

    a = (el * R2D + 5.0) / 10.0;
    i = (int)floor(a); a -= i;
    if (i < 1) minsnr = mask->mask[freq][0];
    else if (i > 8) minsnr = mask->mask[freq][8];
    else minsnr = (1.0 - a) * mask->mask[freq][i - 1] + a * mask->mask[freq][i];

    return snr < minsnr;
}
/* satellite system+prn/slot number to satellite number ------------------------
* convert satellite system+prn/slot number to satellite number
* args   : int    sys       I   satellite system (SYS_GPS,SYS_GLO,...)
*          int    prn       I   satellite prn/slot number
* return : satellite number (0:error)
*-----------------------------------------------------------------------------*/
#if 0
extern unsigned char satno(unsigned char  sys, unsigned char  prn)
{
    if (prn <= 0) return 0;
    switch (sys) {
    case SYS_GPS:
        if (prn < MINPRNGPS || MAXPRNGPS < prn) return 0;
        return prn - MINPRNGPS + 1;
    case SYS_GLO:
        if (prn < MINPRNGLO || MAXPRNGLO < prn) return 0;
        return NSATGPS + prn - MINPRNGLO + 1;
    case SYS_GAL:
        if (prn < MINPRNGAL || MAXPRNGAL < prn) return 0;
        return NSATGPS + NSATGLO + prn - MINPRNGAL + 1;
    case SYS_QZS:
        if (prn < MINPRNQZS || MAXPRNQZS < prn) return 0;
        return NSATGPS + NSATGLO + NSATGAL + prn - MINPRNQZS + 1;
    case SYS_BDS:
        if (prn < MINPRNBDS || MAXPRNBDS < prn) return 0;
        return NSATGPS + NSATGLO + NSATGAL + NSATQZS + prn - MINPRNBDS + 1;
    case SYS_LEO:
        if (prn < MINPRNLEO || MAXPRNLEO < prn) return 0;
        return NSATGPS + NSATGLO + NSATGAL + NSATQZS + NSATBDS + prn - MINPRNLEO + 1;
    case SYS_SBS:
        if (prn < MINPRNSBS || MAXPRNSBS < prn) return 0;
        return NSATGPS + NSATGLO + NSATGAL + NSATQZS + NSATBDS + NSATLEO + prn - MINPRNSBS + 1;
    }
    return 0;
}
extern unsigned char  satsys(unsigned char  sat, unsigned char* prn)
{
    int sys = SYS_NONE;
    if (sat <= 0 || MAXSAT < sat) sat = 0;
    else if (sat <= NSATGPS) {
        sys = SYS_GPS; sat += MINPRNGPS - 1;
    }
    else if ((sat -= NSATGPS) <= NSATGLO) {
        sys = SYS_GLO; sat += MINPRNGLO - 1;
    }
    else if ((sat -= NSATGLO) <= NSATGAL) {
        sys = SYS_GAL; sat += MINPRNGAL - 1;
    }
    else if ((sat -= NSATGAL) <= NSATQZS) {
        sys = SYS_QZS; sat += MINPRNQZS - 1;
    }
    else if ((sat -= NSATQZS) <= NSATBDS) {
        sys = SYS_BDS; sat += MINPRNBDS - 1;
    }
    else if ((sat -= NSATBDS) <= NSATLEO) {
        sys = SYS_LEO; sat += MINPRNLEO - 1;
    }
    else if ((sat -= NSATLEO) <= NSATSBS) {
        sys = SYS_SBS; sat += MINPRNSBS - 1;
    }
    else sat = 0;
    if (prn) *prn = sat;
    return sys;
}
#else
extern unsigned char satno(unsigned char sys, unsigned char prn)
{
    if (prn == 0) return 0xFF;
    switch (sys) {
    case SYS_GPS:
        if (prn < MINPRNGPS || MAXPRNGPS < prn) return 0xFF;
        return prn - MINPRNGPS + 1;
    case SYS_QZS:
        if (prn < MINPRNQZS || MAXPRNQZS < prn) return 0xFF;
        return prn - MINPRNQZS + NSATGPS + 1;
    case SYS_BDS:
        if (prn < MINPRNBDS || MAXPRNBDS < prn) return 0xFF;
        return prn - MINPRNBDS + NSATGPS + NSATQZS + 1;
    case SYS_GLO:
        if (prn < MINPRNGLO || MAXPRNGLO < prn) return 0xFF;
        return prn - MINPRNGLO + NSATGPS + NSATQZS + NSATBDS + 1;

    case SYS_GAL:
        if (prn < MINPRNGAL || MAXPRNGAL < prn) return 0xFF;
        return NSATGPS + NSATQZS + NSATBDS + NSATGLO + prn - MINPRNGAL + 1;
#if 0
    case SYS_QZS:
        if (prn < MINPRNQZS || MAXPRNQZS < prn) return 0xFF;
        return NSATGPS + NSATBDS + NSATGLO + NSATGAL + prn - MINPRNQZS;
    case SYS_SBS:
        if (prn < MINPRNSBS || MAXPRNSBS < prn) return 0xFF;
        return NSATGPS + NSATBDS + NSATGLO + NSATGAL + NSATQZS + NSATBDS + prn - MINPRNSBS;
#endif
    }
    return 0xFF;
}

extern unsigned char satsys(unsigned char sat, unsigned char* prn)
{
    unsigned char sys = SYS_NONE;
    if (MAXSAT <= sat) sat = 0xFF;
    else if (sat <= NSATGPS) {
        sys = SYS_GPS; sat += MINPRNGPS - 1;
    }
    else if ((sat -= NSATGPS) <= NSATQZS) {
        sys = SYS_QZS; sat += MINPRNQZS - 1;
    }
    else if ((sat -= NSATQZS) <= NSATBDS) {
        sys = SYS_BDS; sat += MINPRNBDS - 1;
    }
    else if ((sat -= NSATBDS) <= NSATGLO) {
        sys = SYS_GLO; sat += MINPRNGLO - 1;
    }
    else if ((sat -= NSATGLO) <= NSATGAL) {
        sys = SYS_GAL; sat += MINPRNGAL - 1;
    }
#if 0
    else if ((sat -= NSATGAL) < NSATQZS) {
        sys = SYS_QZS; sat += MINPRNQZS - 1;
    }
    else if ((sat -= NSATQZS) < NSATSBS) {
        sys = SYS_SBS; sat += MINPRNSBS - 1;
    }
#endif
    else sat = 0xFF;
    if (prn) *prn = sat;
    return sys;
}
#endif

extern void satno2id(unsigned char sat, char* id)
{
    unsigned char prn;
    switch (satsys(sat, &prn)) {
    case SYS_GPS: sprintf(id, "G%02d", prn - MINPRNGPS + 1); return;
    case SYS_GLO: sprintf(id, "R%02d", prn - MINPRNGLO + 1); return;
    case SYS_GAL: sprintf(id, "E%02d", prn - MINPRNGAL + 1); return;
    case SYS_QZS: sprintf(id, "J%02d", prn - MINPRNQZS + 1); return;
    case SYS_BDS: sprintf(id, "C%02d", prn - MINPRNBDS + 1); return;
    case SYS_LEO: sprintf(id, "L%02d", prn - MINPRNLEO + 1); return;
    case SYS_SBS: sprintf(id, "%03d", prn); return;
    }
    strcpy(id, "");
}
/* interpolate antenna phase center variation --------------------------------*/
static double interpvar(double ang, const double* var)
{
    double a = ang / 5.0; /* ang=0-90 */
    int i = (int)a;
    if (i < 0) return var[0]; else if (i >= 18) return var[18];
    return var[i] * (1.0 - a + i) + var[i + 1] * (a - i);
}
/* interpolate antenna phase center variation --------------------------------*/
static double interpvar0(unsigned char sat, double ang, const double* var, int bsat)
{
    int i, limit = 18;
    double a;
    unsigned char sys;
    if (bsat) {
        sys = satsys(sat, NULL);

        ang = ang / 5.0;

        //if (sys==SYS_GPS) limit=14;
        //else if (sys==SYS_GLO) limit=15;

        if (ang >= limit) {
            if (ang > limit + 0.25) {
                printf("%d (nadir=%f) >= %2d°\n", sat, ang, limit);
            }
            return var[limit];
        }
        if (ang < 0) {
            printf("*** ERROR: compute satellite antenna offset: nadir < 0\n");
            return var[0];
        }

        i = (int)ang;

        return var[i] * (1.0 + i - ang) + var[i + 1] * (ang - i);
    }
    else {
        a = ang / 5.0; /* ang=0-90 */
        i = (int)a;
        if (i < 0) {
            printf("*** ERROR: compute receiver antenna offset: i<0\n");
            return var[0];
        }
        else if (i > 18) {
            printf("*** ERROR: compute receiver antenna offset: i>18\n");
            return var[18];
        }
        return var[i] * (1.0 - a + i) + var[i + 1] * (a - i);
    }
}
extern void antmodel(const pcv_t* pcv, const double* del, const double* azel,
    int opt, double* dant)
{
    double e[3], off[3], cosel = cos(azel[1]);
    int i, j;
    //
    ////trace(4, "antmodel: azel=%6.1f %4.1f opt=%d\n", azel[0] * R2D, azel[1] * R2D, opt);

    e[0] = sin(azel[0]) * cosel;
    e[1] = cos(azel[0]) * cosel;
    e[2] = sin(azel[1]);

    for (i = 0; i < NFREQ; i++) {
        for (j = 0; j < 3; j++) off[j] = pcv->off[i][j] + del[j];

        dant[i] = -dot(off, e, 3) + (opt ? interpvar(90.0 - azel[1] * R2D, pcv->var[i]) : 0.0);
    }
    ////trace(5, "antmodel: dant=%6.3f %6.3f\n", dant[0], dant[1]);
}
extern void antmodel_s(unsigned char sat, const pcv_t* pcv, double nadir, double* dant)
{
    unsigned char i, sys;

    sys = satsys(sat, NULL);
    for (i = 0; i < NFREQ; i++) {
        //在interpvar函数里对nadir也除以了5.0，这是正确的吗？
        //输出nadir*R2D的值，都在14°以内；
        //分别使用两种方案计算alrt站2010年的数据，发现除以5.0的结果高程方向偏差系统为正；
        //不除以5.0的结果高程方向无系统偏差
        //dant[i]=interpvar(sat, nadir*R2D*5.0, pcv->var[i], true);
        if (sys == SYS_GPS) {
            dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[i], 1);
            if (i == 2) {
                dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[1], 1);
            }
        }
        else if (sys == SYS_GLO) {
            dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[i + NFREQ], 1);
            if (i == 2) {
                dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[1 + NFREQ], 1);
            }
        }
        else if (sys == SYS_BDS) {
            dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[i + 2 * NFREQ], 1);
            if (i == 2) {
                dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[1 + 2 * NFREQ], 1);
            }
        }
        else if (sys == SYS_GAL) {
            dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[i + 3 * NFREQ], 1);
            if (i == 2) {
                dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[1 + 3 * NFREQ], 1);
            }
        }
        else if (sys == SYS_QZS) {
            dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[i + 4 * NFREQ], 1);
            if (i == 2) {
                dant[i] = interpvar0(sat, nadir * R2D * 5.0, pcv->var[1 + 4 * NFREQ], 1);
            }
        }
    }
}
/* interpolate antenna phase center variation --------------------------------*/
//static double interpvar(double ang, const double *var)
//{
//    double a=ang/5.0; /* ang=0-90 */
//    int i=(int)a;
//    if (i<0) return var[0]; else if (i>=18) return var[18];
//    return var[i]*(1.0-a+i)+var[i+1]*(a-i);
//}
//extern void antmodel_s(int sat,const pcv_t *pcv, double nadir, double *dant)
//{
//    int i;
//    
//    for (i=0;i<NFREQ;i++) {
//        dant[i]=interpvar(sat,nadir*R2D*5.0,pcv->var[i]);
//    }
//    
//}


/* sleep ms --------------------------------------------------------------------
* sleep ms
* args   : int   ms         I   miliseconds to sleep (<0:no sleep)
* return : none
*-----------------------------------------------------------------------------*/
extern void sleepms(int ms)
{
#ifdef WIN32
    if (ms < 5) Sleep(1); else Sleep(ms);
#else
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000 * 1000000);
    nanosleep(&ts, NULL);
#endif
}

static int repstr(char* str, const char* pat, const char* rep)
{
    int len = (int)strlen(pat);
    char buff[1024], * p, * q, * r;

    for (p = str, r = buff; *p; p = q + len) {
        if (!(q = strstr(p, pat))) break;
        strncpy(r, p, q - p);
        r += q - p;
        r += sprintf(r, "%s", rep);
    }
    if (p <= str) return 0;
    strcpy(r, p);
    strcpy(str, buff);
    return 1;
}
extern int reppath(const char* path, char* rpath, gtime_t time, const char* rov,
    const char* base)
{
    double ep[6], ep0[6] = { 2000,1,1,0,0,0 };
    int week, dow, doy, stat = 0;
    char rep[64];

    strcpy(rpath, path);

    if (!strstr(rpath, "%")) return 0;
    if (*rov) stat |= repstr(rpath, "%r", rov);
    if (*base) stat |= repstr(rpath, "%b", base);
    if (time.time != 0) {
        time2epoch(time, ep);
        ep0[0] = ep[0];
        dow = (int)floor(time2gpst(time, &week) / 86400.0);
        doy = (int)floor(timediff(time, epoch2time(ep0)) / 86400.0) + 1;
        sprintf(rep, "%02d", ((int)ep[3] / 3) * 3);   stat |= repstr(rpath, "%ha", rep);
        sprintf(rep, "%02d", ((int)ep[3] / 6) * 6);   stat |= repstr(rpath, "%hb", rep);
        sprintf(rep, "%02d", ((int)ep[3] / 12) * 12); stat |= repstr(rpath, "%hc", rep);
        sprintf(rep, "%04.0f", ep[0]);              stat |= repstr(rpath, "%Y", rep);
        sprintf(rep, "%02.0f", fmod(ep[0], 100.0));  stat |= repstr(rpath, "%y", rep);
        sprintf(rep, "%02.0f", ep[1]);              stat |= repstr(rpath, "%m", rep);
        sprintf(rep, "%02.0f", ep[2]);              stat |= repstr(rpath, "%d", rep);
        sprintf(rep, "%02.0f", ep[3]);              stat |= repstr(rpath, "%h", rep);
        sprintf(rep, "%02.0f", ep[4]);              stat |= repstr(rpath, "%M", rep);
        sprintf(rep, "%02.0f", floor(ep[5]));       stat |= repstr(rpath, "%S", rep);
        sprintf(rep, "%03d", doy);                stat |= repstr(rpath, "%n", rep);
        sprintf(rep, "%04d", week);               stat |= repstr(rpath, "%W", rep);
        sprintf(rep, "%d", dow);                stat |= repstr(rpath, "%D", rep);
        sprintf(rep, "%c", 'a' + (int)ep[3]);     stat |= repstr(rpath, "%H", rep);
        sprintf(rep, "%02d", ((int)ep[4] / 15) * 15); stat |= repstr(rpath, "%t", rep);
    }
    else if (strstr(rpath, "%ha") || strstr(rpath, "%hb") || strstr(rpath, "%hc") ||
        strstr(rpath, "%Y") || strstr(rpath, "%y") || strstr(rpath, "%m") ||
        strstr(rpath, "%d") || strstr(rpath, "%h") || strstr(rpath, "%M") ||
        strstr(rpath, "%S") || strstr(rpath, "%n") || strstr(rpath, "%W") ||
        strstr(rpath, "%D") || strstr(rpath, "%H") || strstr(rpath, "%t")) {
        return -1; /* no valid time */
    }
    return stat;
}
extern int execcmd(const char* cmd)
{
#ifdef WIN32
    PROCESS_INFORMATION info;
    STARTUPINFO si = { 0 };
    DWORD stat;
    char cmds[1024];

    si.cb = sizeof(si);
    sprintf(cmds, "cmd /c %s", cmd);
    if (!CreateProcess(NULL, (LPTSTR)cmds, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
        NULL, &si, &info)) return -1;
    WaitForSingleObject(info.hProcess, INFINITE);
    if (!GetExitCodeProcess(info.hProcess, &stat)) stat = -1;
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return (int)stat;
#else
    return system(cmd);
#endif
}
extern void deg2dms(double deg, double* dms, int ndec)
{
    double sign = deg < 0.0 ? -1.0 : 1.0, a = fabs(deg);
    double unit = pow(0.1, ndec);
    dms[0] = floor(a); a = (a - dms[0]) * 60.0;
    dms[1] = floor(a); a = (a - dms[1]) * 60.0;
    dms[2] = floor(a / unit + 0.5) * unit;
    if (dms[2] >= 60.0) {
        dms[2] = 0.0;
        dms[1] += 1.0;
        if (dms[1] >= 60.0) {
            dms[1] = 0.0;
            dms[0] += 1.0;
        }
    }
    dms[0] *= sign;
}

/* sun and moon position in eci (ref [4] 5.1.1, 5.2.1) -----------------------*/
static void sunmoonpos_eci(gtime_t tut, double* rsun, double* rmoon)
{
    const double ep2000[] = { 2000,1,1,12,0,0 };
    double t, f[5], eps, Ms, ls, rs, lm, pm, rm, sine, cose, sinp, cosp, sinl, cosl;

    t = timediff(tut, epoch2time(ep2000)) / 86400.0 / 36525.0;

    /* astronomical arguments */
    ast_args(t, f);

    /* obliquity of the ecliptic */
    eps = 23.439291 - 0.0130042 * t;
    sine = sin(eps * D2R); cose = cos(eps * D2R);

    /* sun position in eci */
    if (rsun) {
        Ms = 357.5277233 + 35999.05034 * t;
        ls = 280.460 + 36000.770 * t + 1.914666471 * sin(Ms * D2R) + 0.019994643 * sin(2.0 * Ms * D2R);
        rs = AU * (1.000140612 - 0.016708617 * cos(Ms * D2R) - 0.000139589 * cos(2.0 * Ms * D2R));
        sinl = sin(ls * D2R); cosl = cos(ls * D2R);
        rsun[0] = rs * cosl;
        rsun[1] = rs * cose * sinl;
        rsun[2] = rs * sine * sinl;

    }
    /* moon position in eci */
    if (rmoon) {
        lm = 218.32 + 481267.883 * t + 6.29 * sin(f[0]) - 1.27 * sin(f[0] - 2.0 * f[3]) +
            0.66 * sin(2.0 * f[3]) + 0.21 * sin(2.0 * f[0]) - 0.19 * sin(f[1]) - 0.11 * sin(2.0 * f[2]);
        pm = 5.13 * sin(f[2]) + 0.28 * sin(f[0] + f[2]) - 0.28 * sin(f[2] - f[0]) -
            0.17 * sin(f[2] - 2.0 * f[3]);
        rm = RE_WGS84 / sin((0.9508 + 0.0518 * cos(f[0]) + 0.0095 * cos(f[0] - 2.0 * f[3]) +
            0.0078 * cos(2.0 * f[3]) + 0.0028 * cos(2.0 * f[0])) * D2R);
        sinl = sin(lm * D2R); cosl = cos(lm * D2R);
        sinp = sin(pm * D2R); cosp = cos(pm * D2R);
        rmoon[0] = rm * cosp * cosl;
        rmoon[1] = rm * (cose * cosp * sinl - sine * sinp);
        rmoon[2] = rm * (sine * cosp * sinl + cose * sinp);
    }
}
static double time2sec(gtime_t time, gtime_t* day)
{
    double ep[6], sec;
    time2epoch(time, ep);
    sec = ep[3] * 3600.0 + ep[4] * 60.0 + ep[5];
    ep[3] = ep[4] = ep[5] = 0.0;
    *day = epoch2time(ep);
    return sec;
}
extern double utc2gmst(gtime_t t, double ut1_utc)
{
    const double ep2000[] = { 2000,1,1,12,0,0 };
    gtime_t tut, tut0;
    double ut, t1, t2, t3, gmst0, gmst;

    tut = timeadd(t, ut1_utc);
    ut = time2sec(tut, &tut0);
    t1 = timediff(tut0, epoch2time(ep2000)) / 86400.0 / 36525.0;
    t2 = t1 * t1; t3 = t2 * t1;
    gmst0 = 24110.54841 + 8640184.812866 * t1 + 0.093104 * t2 - 6.2E-6 * t3;
    gmst = gmst0 + 1.002737909350795 * ut;

    return fmod(gmst, 86400.0) * PI / 43200.0; /* 0 <= gmst <= 2*PI */
}
extern void eci2ecef(gtime_t tutc, const double* erpv, double* U, double* gmst)
{
    const double ep2000[] = { 2000,1,1,12,0,0 };
    static gtime_t tutc_;
    static double U_[9], gmst_;
    gtime_t tgps;
    double eps, ze, th, z, t, t2, t3, dpsi, deps, gast, f[5];
    double R1[9], R2[9], R3[9], R[9], W[9], N[9], P[9], NP[9];
    int i;


    if (fabs(timediff(tutc, tutc_)) < 0.01) { /* read cache */
        for (i = 0; i < 9; i++) U[i] = U_[i];
        if (gmst) *gmst = gmst_;
        return;
    }
    tutc_ = tutc;

    /* terrestrial time */
    tgps = utc2gpst(tutc_);
    t = (timediff(tgps, epoch2time(ep2000)) + 19.0 + 32.184) / 86400.0 / 36525.0;
    t2 = t * t; t3 = t2 * t;

    /* astronomical arguments */
    ast_args(t, f);

    /* iau 1976 precession */
    ze = (2306.2181 * t + 0.30188 * t2 + 0.017998 * t3) * AS2R;
    th = (2004.3109 * t - 0.42665 * t2 - 0.041833 * t3) * AS2R;
    z = (2306.2181 * t + 1.09468 * t2 + 0.018203 * t3) * AS2R;
    eps = (84381.448 - 46.8150 * t - 0.00059 * t2 + 0.001813 * t3) * AS2R;
    Rz(-z, R1); Ry(th, R2); Rz(-ze, R3);
    matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, R);
    matmul("NN", 3, 3, 3, 1.0, R, R3, 0.0, P); /* P=Rz(-z)*Ry(th)*Rz(-ze) */

    /* iau 1980 nutation */
    nut_iau1980(t, f, &dpsi, &deps);
    Rx(-eps - deps, R1); Rz(-dpsi, R2); Rx(eps, R3);
    matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, R);
    matmul("NN", 3, 3, 3, 1.0, R, R3, 0.0, N); /* N=Rx(-eps)*Rz(-dspi)*Rx(eps) */

    /* greenwich aparent sidereal time (rad) */
    gmst_ = utc2gmst(tutc_, erpv[2]);
    gast = gmst_ + dpsi * cos(eps);
    gast += (0.00264 * sin(f[4]) + 0.000063 * sin(2.0 * f[4])) * AS2R;

    /* eci to ecef transformation matrix */
    Ry(-erpv[0], R1); Rx(-erpv[1], R2); Rz(gast, R3);
    matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, W);
    matmul("NN", 3, 3, 3, 1.0, W, R3, 0.0, R); /* W=Ry(-xp)*Rx(-yp) */
    matmul("NN", 3, 3, 3, 1.0, N, P, 0.0, NP);
    matmul("NN", 3, 3, 3, 1.0, R, NP, 0.0, U_); /* U=W*Rz(gast)*N*P */

    for (i = 0; i < 9; i++) U[i] = U_[i];
    if (gmst) *gmst = gmst_;

}

extern void sunmoonpos(gtime_t tutc, const double* erpv, double* rsun,
    double* rmoon, double* gmst)
{
    gtime_t tut;
    double rs[3], rm[3], U[9], gmst_;

    tut = timeadd(tutc, erpv[2]); /* utc -> ut1 */

    /* sun and moon position in eci */
    sunmoonpos_eci(tut, rsun ? rs : NULL, rmoon ? rm : NULL);

    /* eci to ecef transformation matrix */
    eci2ecef(tutc, erpv, U, &gmst_);

    /* sun and moon postion in ecef */
    if (rsun) matmul("NN", 3, 1, 3, 1.0, U, rs, 0.0, rsun);
    if (rmoon) matmul("NN", 3, 1, 3, 1.0, U, rm, 0.0, rmoon);
    if (gmst) *gmst = gmst_;
}

extern char* code2obs(unsigned char code, int* freq)
{
    if (freq) *freq = 0;
    if (code <= CODE_NONE || MAXCODE < code) return "";
    if (freq) *freq = obsfreqs[code];
    return obscodes[code];
}

extern unsigned char obs2code(const char* obs, int* freq)
{
    int i;
    if (freq) *freq = 0;
    for (i = 1; i < MAXCODE; i++) {
        if (strcmp(obscodes[i], obs)) continue;
        if (freq) *freq = obsfreqs[i];
        return (unsigned char)i;
    }
    return CODE_NONE;
}
extern int getcodepri(unsigned char sys, unsigned char code, const char* opt)
{
    const char* p, * optstr;
    char* obs, str[8] = "";
    int i, j;

    switch (sys) {
    case SYS_GPS: i = 0; optstr = "-GL%2s"; break;
    case SYS_GLO: i = 1; optstr = "-RL%2s"; break;
    case SYS_GAL: i = 2; optstr = "-EL%2s"; break;
    case SYS_QZS: i = 3; optstr = "-JL%2s"; break;
    case SYS_SBS: i = 4; optstr = "-SL%2s"; break;
    case SYS_BDS: i = 5; optstr = "-CL%2s"; break;
        //case SYS_IRN: i = 6; optstr = "-IL%2s"; break;
    default: return 0;
    }
    obs = code2obs(code, &j);

    /* parse code options */
    for (p = opt; p && (p = strchr(p, '-')); p++) {
        if (sscanf(p, optstr, str) < 1 || str[0] != obs[0]) continue;
        return str[1] == obs[1] ? 15 : 0;
    }
    /* search code priority */
    return (p = strchr(codepris[i][j - 1], obs[1])) ? 14 - (int)(p - codepris[i][j - 1]) : 0;
}

#define MAXTBUFLEN 20480
static char TRACEBUFF[MAXTBUFLEN];
static FILE* fp_trace = NULL;     /* file pointer of trace */
static char file_trace[1024];   /* trace file */
static int level_trace = 0;       /* level of trace */
static uint32_t tick_trace = 0;   /* tick time at traceopen (ms) */
static gtime_t time_trace = { 0 };  /* time at traceopen */
static lock_t lock_trace;       /* lock for trace */


#define INT_SWAP_TRAC 86400.0           /* swap interval of trace file (s) */
static void traceswap(void)
{
    gtime_t time = utc2gpst(timeget());
    char path[1024];

    lock(&lock_trace);

    if ((int)(time2gpst(time, NULL) / INT_SWAP_TRAC) ==
        (int)(time2gpst(time_trace, NULL) / INT_SWAP_TRAC)) {
        unlock(&lock_trace);
        return;
    }
    time_trace = time;

    if (!reppath(file_trace, path, time, "", "")) {
        unlock(&lock_trace);
        return;
    }
    if (fp_trace) fclose(fp_trace);

    if (!(fp_trace = fopen(path, "w"))) {
        fp_trace = stderr;
    }
    unlock(&lock_trace);
}
extern void traceopen(const char* file)
{
    gtime_t time = utc2gpst(timeget());
    char path[1024];

    reppath(file, path, time, "", "");
    if (!*path || !(fp_trace = fopen(path, "w"))) fp_trace = stderr;
    strcpy(file_trace, file);
    tick_trace = tickget();
    time_trace = time;
    initlock(&lock_trace);
}

extern void traceclose(void)
{
    if (strlen(TRACEBUFF) > 0) {
        fprintf(fp_trace, "%s", TRACEBUFF);
        fflush(fp_trace);
        memset(TRACEBUFF, '\0', sizeof(char) * MAXTBUFLEN);
    }
    if (fp_trace && fp_trace != stderr) fclose(fp_trace);
    fp_trace = NULL;
    file_trace[0] = '\0';
}
extern void tracelevel(int level)
{
    level_trace = level;
}
extern void trace(int level, const char *format, ...)
{
    va_list ap;
    
    /* print error message to stderr */
    if (level<=1) {
        va_start(ap,format); vfprintf(stderr,format,ap); va_end(ap);
    }
    if (!fp_trace||level>level_trace) return;
    fprintf(fp_trace,"%d ",level);
    va_start(ap,format); vfprintf(fp_trace,format,ap); va_end(ap);
    fflush(fp_trace);
}
/* print matrix ----------------------------------------------------------------
* print matrix to stdout
* args   : double *A        I   matrix A (n x m)
*          int    n,m       I   number of rows and columns of A
*          int    p,q       I   total columns, columns under decimal point
*         (FILE  *fp        I   output file pointer)
* return : none
* notes  : matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
static void matfprint(const double A[], int n, int m, int p, int q, FILE *fp)
{
    int i,j;
    
    for (i=0;i<n;i++) {
        for (j=0;j<m;j++) fprintf(fp," %*.*f",p,q,A[i+j*n]);
        fprintf(fp,"\n");
    }
}
extern void tracemat(int level, const double *A, int n, int m, int p, int q)
{
    if (!fp_trace||level>level_trace) return;
    matfprint(A,n,m,p,q,fp_trace); fflush(fp_trace);
}
#if 0
extern void trace(int level, const char* format, ...)
{
    va_list ap;
    char buff[1024];

    /* print error message to stderr */
    if (level <= 1) {
        va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap);
    }
    if (!fp_trace || level > level_trace) return;

    va_start(ap, format);
    vsnprintf((char*)buff, 1024, format, ap);
    va_end(ap);

    if (strlen(TRACEBUFF) + strlen(buff) >= MAXTBUFLEN) {
        traceswap();
        fprintf(fp_trace, "%s", TRACEBUFF);
        fflush(fp_trace);
        memset(TRACEBUFF, '\0', sizeof(char) * MAXTBUFLEN);
    }
    sprintf(TRACEBUFF, "%s%d %s", TRACEBUFF, level, buff);
}
#endif


