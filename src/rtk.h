#ifndef __RTK_H
#define __RTK_H
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include "pos_filter.h"
//
//#define WIN32
#ifdef WIN32 
#include <winsock2.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma    comment(lib,"winmm.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma warning( disable : 4996) 
#endif
#define MEMWATCH
#define DMW_STDIO

#ifdef _MSC_VER
#include"memwatch.h"
#endif  

#else 
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#endif
//#define DEBUG_FILE_NL
#define BSLTHRESHOLD    5E3


#include "logmod.h"

#define WL_CAR_K0    1
#define WL_CAR_K1    -1
#define WL_CAR_K2    0
#define WL_PSR_K0    1
#define WL_PSR_K1    1
#define WL_PSR_K2    0

#define NL_CAR_K0    4
#define NL_CAR_K1    -3
#define NL_CAR_K2    0
#define NL_PSR_K0    1
#define NL_PSR_K1    1
#define NL_PSR_K2    0

#define BDS3_WL_CAR_K0    1
#define BDS3_WL_CAR_K1    0
#define BDS3_WL_CAR_K2    -1
#define BDS3_WL_PSR_K0    1
#define BDS3_WL_PSR_K1    0
#define BDS3_WL_PSR_K2    1

#define BDS3_NL_CAR_K0    4
#define BDS3_NL_CAR_K1    0
#define BDS3_NL_CAR_K2    -3
#define BDS3_NL_PSR_K0    1
#define BDS3_NL_PSR_K1    0
#define BDS3_NL_PSR_K2    1

//extern FILE* fpversion;
#define SVN_VERSION 250430

#define MAXEPH        10240
#define MAXGEPH     5120
#define DEBUG_BUFF_LEN    20480
#define MAXQUEUESIZE 30
#include <time.h>
static const char syscodes[] = "GREJSCI"; /* satellite system codes */
static const char frqcodes[] = "1256789"; /* frequency codes */
#define MAXSTRMSG   1024 
#define VER_RTKLIB  "2.4.3"       /* library version */
#define PATCH_LEVEL ""               /* patch level */
#define LLI_SLIP    0x01                /* LLI: cycle-slip */
#define DTTOL       0.025               /* tolerance of time difference (s) */
#define MAXOBSTYPE  64                  /* max number of obs type in RINEX */
#define NUMSYS      6                   /* number of systems */
#define MAXRNXLEN   (16*MAXOBSTYPE+4)   /* max rinex record length */
#define MAXPOSHEAD  1024                /* max head line position */
#define MINFREQ_GLO -7                  /* min frequency number glonass */
#define MAXFREQ_GLO 13                  /* max frequency number glonass */

#define DEBUG_MSG
#define SEC_PER_WEEK 604800
#define SEC_PER_HALF_WEEK 302400
#define SEC_PER_DAY 86400

#define PI          3.1415926535897932  /* pi */
#define D2R         (PI/180.0)          /* deg to rad */
#define R2D         (180.0/PI)          /* rad to deg */
#define CLIGHT      299792458.0         /* speed of light (m/s) */
#define SC2RAD      3.1415926535898     /* semi-circle to radian (IS-GPS) */
#define MUY                    3.986005E14
#define OMGE        7.2921151467E-5     /* earth angular velocity (IS-GPS) (rad/s) */
//#define F             -4.442807633e-10

#define RE_WGS84    6378137.0           /* earth semimajor axis (WGS84) (m) */
#define FE_WGS84    (1.0/298.257223563) /* earth flattening (WGS84) */
//#define FE_WGS84    (1.0/298.257222101) /* earth flattening () */
#define HION        350000.0            /* ionosphere height (m) */
#define TTOL_MOVEB  (0.9+2*DTTOL)
#define P2_5        0.03125             /* 2^-5 */
#define P2_6        0.015625            /* 2^-6 */
#define P2_11       4.882812500000000E-04 /* 2^-11 */
#define P2_15       3.051757812500000E-05 /* 2^-15 */
#define P2_17       7.629394531250000E-06 /* 2^-17 */
#define P2_19       1.907348632812500E-06 /* 2^-19 */
#define P2_20       9.536743164062500E-07 /* 2^-20 */
#define P2_21       4.768371582031250E-07 /* 2^-21 */
#define P2_23       1.192092895507810E-07 /* 2^-23 */
#define P2_24       5.960464477539063E-08 /* 2^-24 */
#define P2_27       7.450580596923828E-09 /* 2^-27 */
#define P2_29       1.862645149230957E-09 /* 2^-29 */
#define P2_30       9.313225746154785E-10 /* 2^-30 */
#define P2_31       4.656612873077393E-10 /* 2^-31 */
#define P2_32       2.328306436538696E-10 /* 2^-32 */
#define P2_33       1.164153218269348E-10 /* 2^-33 */
#define P2_35       2.910383045673370E-11 /* 2^-35 */
#define P2_38       3.637978807091710E-12 /* 2^-38 */
#define P2_39       1.818989403545856E-12 /* 2^-39 */
#define P2_40       9.094947017729280E-13 /* 2^-40 */
#define P2_43       1.136868377216160E-13 /* 2^-43 */
#define P2_48       3.552713678800501E-15 /* 2^-48 */
#define P2_50       8.881784197001252E-16 /* 2^-50 */
#define P2_55       2.775557561562891E-17 /* 2^-55 */

#define NFREQ       6                 /* number of carrier frequencies */

#define ROUND(x)    (int)floor((x)+0.5)

//#define SELETE_SAT_NUM    40 
//#define NX (3+2+SELETE_SAT_NUM+SELETE_SAT_NUM*NFREQ)
//#define NY (SELETE_SAT_NUM*NFREQ*2)
//#define NY (NX)

#define NXLC    (3+2+SELETE_SAT_NUM)
#define NYLC    (NXLC)

//#define MAX_SAT 32
#define MAXRAWLEN 4096
#define MAXDTOE 7200.0
#define MAXDTOE_GLO 1800.0              /* max time difference to GLONASS Toe (s) */
#define MAX_SOL_BUF 5
#define MAXSTRPATH  1024                /* max length of stream path */
//#define MAXSTRMSG                   /* max length of stream message */
#define FREQ1       1.57542E9           /* L1/E1  frequency (Hz) */
#define FREQ2       1.22760E9           /* L2     frequency (Hz) */
#define FREQ5       1.17645E9           /* L5/E5a frequency (Hz) */
#define FREQ6       1.27875E9           /* E6/LEX frequency (Hz) */
#define FREQ7       1.20714E9           /* E5b    frequency (Hz) */
#define FREQ8       1.191795E9          /* E5a+b  frequency (Hz) */
#define FREQ9       2.492028E9          /* S      frequency (Hz) */
#define FREQ1_GLO   1.60200E9           /* GLONASS G1 base frequency (Hz) */
#define DFRQ1_GLO   0.56250E6           /* GLONASS G1 bias frequency (Hz/n) */
#define FREQ2_GLO   1.24600E9           /* GLONASS G2 base frequency (Hz) */
#define DFRQ2_GLO   0.43750E6           /* GLONASS G2 bias frequency (Hz/n) */
#define FREQ3_GLO   1.202025E9          /* GLONASS G3 frequency (Hz) */
#define FREQ1_CMP   1.561098E9          /* BeiDou B1 frequency (Hz) */
#define FREQ2_CMP   1.20714E9           /* BeiDou B2 frequency (Hz) */
#define FREQ3_CMP   1.26852E9           /* BeiDou B3 frequency (Hz) */
#define FREQB1C_CMP   1.57542E9          
#define FREQB2a_CMP   1.17645E9          
#define FREQB2b_CMP   1.20714E9 
#define LAM1    CLIGHT/FREQ1

/* solution */
//#define ENCODER
#define TIME_MEASURE

#define SOL_MSG_LEN 160

#define SOLF_LLH    0                   /* solution format: lat/lon/height */
#define SOLF_XYZ    1                   /* solution format: x/y/z-ecef */
#define SOLF_ENU    2                   /* solution format: e/n/u-baseline */
#define SOLF_NMEA   3                   /* solution format: NMEA-183 */
#define SOLF_ORI    4

#define SQR(x)      ((x)*(x))   
#define sos2(x) (x[0]*x[0]+x[1]*x[1])
#define sos3(x) (x[0]*x[0]+x[1]*x[1]+x[2]*x[2])
#define sos4(x) (x[0]*x[0]+x[1]*x[1]+x[2]*x[2]+x[3]*x[3])
#define norm2(x) sqrt(x[0]*x[0]+x[1]*x[1])
#define norm3(x) sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2])

#define PMODE_DGPS   1                  /* positioning mode: DGPS/DGNSS */
#define PMODE_KINEMA 2                  /* positioning mode: kinematic */
#define PMODE_STATIC 3
#define PMODE_MOVEB  4                  /* positioning mode: moving-base */
#define PMODE_SINGLE  5                  /* positioning mode: moving-base */
#define PMODE_PPP_KINEMA 6              /* positioning mode: PPP-kinemaric */
#define PMODE_PPP_STATIC 7              /* positioning mode: PPP-static */
#define PMODE_PPP_FIXED 8 
#define PMODE_FIXED  9                  /* positioning mode: fixed */


#define EPHOPT_BRDC 0                   /* ephemeris option: broadcast ephemeris */
#define EPHOPT_PREC 1                   /* ephemeris option: precise ephemeris */
#define EPHOPT_SBAS 2                   /* ephemeris option: broadcast + SBAS */
#define EPHOPT_SSRAPC 3                 /* ephemeris option: broadcast + SSR_APC */
#define EPHOPT_SSRCOM 4                 /* ephemeris option: broadcast + SSR_COM */
#define EPHOPT_LEX  5                   /* ephemeris option: QZSS LEX ephemeris */

#define SOL_TYPE_BACKWARD 0
#define SOL_TYPE_FORWARD 1
#define SOL_TYPE_COMBINED 2

#define ION_BRDC        1
#define ION_SBAS        2

#define ARMODE_OFF  0                   /* AR mode: off */
#define ARMODE_CONT 1                   /* AR mode: continuous */
#define ARMODE_INST 2                   /* AR mode: instantaneous */
#define ARMODE_FIXHOLD 3                /* AR mode: fix and hold */
#define ARMODE_PPPAR 4                  /* AR mode: PPP-AR */
#define ARMODE_PPPAR_ILS 5              /* AR mode: PPP-AR ILS */
#define ARMODE_WLNL 6                   /* AR mode: wide lane/narrow lane */
#define ARMODE_TCAR 7                   /* AR mode: triple carrier ar */
#define ARMODE_WLNLC 8                  /* AR mode: wide lane/narrow lane */
#define ARMODE_TCARC 9                  /* AR mode: triple carrier ar */

#define SOLQ_NONE   0                   /* solution status: no solution */
#define SOLQ_FIX    1                   /* solution status: fix */
#define SOLQ_FLOAT  2                   /* solution status: float */
#define SOLQ_SBAS   3                   /* solution status: SBAS */
#define SOLQ_DGPS   4                   /* solution status: DGPS/DGNSS */
#define SOLQ_SINGLE 5                   /* solution status: single */
#define SOLQ_PPP    6                   /* solution status: PPP */
#define SOLQ_DR     7                   /* solution status: dead reconing */ 

#define EFACT_GPS   1.0                 /* error factor: GPS */
#define EFACT_GLO   1.5                 /* error factor: GLONASS */
#define EFACT_GAL   1.0                 /* error factor: Galileo */
#define EFACT_QZS   1.0                 /* error factor: QZSS */
#define EFACT_CMP   1.0                 /* error factor: BeiDou */
#define EFACT_IRN   1.5                 /* error factor: IRNSS */
#define EFACT_SBS   3.0                 /* error factor: SBAS */

//#define SYS_NONE    0x00                /* navigation system: none */
//#define SYS_GPS     0x01                /* navigation system: GPS */
//#define SYS_SBS     0x02                /* navigation system: SBAS */
//#define SYS_GLO     0x04                /* navigation system: GLONASS */
//#define SYS_GAL     0x08                /* navigation system: Galileo */
//#define SYS_QZS     0x10                /* navigation system: QZSS */
//#define SYS_BDS     0x20                /* navigation system: BeiDou */
//#define SYS_LEO     0x40                /* navigation system: LEO */
//#define SYS_ALL     0xFF                /* navigation system: all */

#define SYS_GPS     (0)                   /* navigation system: GPS */
#define SYS_QZS     (1)                   /* navigation system: QZSS */
#define SYS_BDS     (2)                   /* navigation system: BeiDou */
#define SYS_GLO     (4)                   /* navigation system: GLONASS */
#define SYS_GAL     (3)                   /* navigation system: Galileo */
#define SYS_SBS     0x10                /* navigation system: SBAS */
#define SYS_LEO     0x40                /* navigation system: LEO */
#define SYS_NONE    0xFF                /* navigation system: none */
#define SYS_ALL     0xFF                /* navigation system: all */

#define TSYS_GPS    0                   /* time system: GPS time */
#define TSYS_UTC    1                   /* time system: UTC */
#define TSYS_GLO    2                   /* time system: GLONASS time */
#define TSYS_GAL    3                   /* time system: Galileo time */
#define TSYS_QZS    4                   /* time system: QZSS time */
#define TSYS_CMP    5                   /* time system: BeiDou time */
#define TSYS_IRN    6                   /* time system: IRNSS time */


#define IONOOPT_OFF 0                   /* ionosphere option: correction off */
#define IONOOPT_BRDC 1                  /* ionosphere option: broadcast model */
#define IONOOPT_SBAS 2                  /* ionosphere option: SBAS model */
#define IONOOPT_IFLC 3                  /* ionosphere option: L1/L2 or L1/L5 iono-free LC */
#define IONOOPT_EST 4                   /* ionosphere option: estimation */
#define IONOOPT_TEC 5                   /* ionosphere option: IONEX TEC model */
#define IONOOPT_QZS 6                   /* ionosphere option: QZSS broadcast model */
#define IONOOPT_LEX 7                   /* ionosphere option: QZSS LEX ionospehre */
#define IONOOPT_STEC 8                  /* ionosphere option: SLANT TEC model */

#define TROPOPT_OFF 0                   /* troposphere option: correction off */
#define TROPOPT_SAAS 1                  /* troposphere option: Saastamoinen model */
#define TROPOPT_SBAS 2                  /* troposphere option: SBAS model */
#define TROPOPT_EST 3                   /* troposphere option: ZTD estimation */
#define TROPOPT_ESTG 4                  /* troposphere option: ZTD+grad estimation */
#define TROPOPT_ZTD 5                   /* troposphere option: ZTD correction */

//#define BASE_POS_TYPE_LLH
#define BASE_POS_TYPE_XYZ_ECEF

#define STR_NONE     0                  /* stream type: none */
#define STR_SERIAL   1                  /* stream type: serial */
#define STR_FILE     2                  /* stream type: file */
#define STR_TCPSVR   3                  /* stream type: TCP server */
#define STR_TCPCLI   4                  /* stream type: TCP client */
#define STR_UDP      5                  /* stream type: UDP stream */
#define STR_NTRIPSVR 6                  /* stream type: NTRIP server */
#define STR_NTRIPCLI 7                  /* stream type: NTRIP client */
#define STR_FTP      8                  /* stream type: ftp */
#define STR_HTTP     9                  /* stream type: http */
#define STR_NTRIPC_S 10  
#define STR_NTRIPC_C 11                 /* stream type: NTRIP caster client */

#define STRFMT_RTCM2 0                  /* stream format: RTCM 2 */
#define STRFMT_RTCM3 1                  /* stream format: RTCM 3 */
#define STRFMT_OEM4  2                  /* stream format: NovAtel OEMV/4 */
#define STRFMT_OEM3  3                  /* stream format: NovAtel OEM3 */
#define STRFMT_UBX   4                  /* stream format: u-blox LEA-*T */
#define STRFMT_SS2   5                  /* stream format: NovAtel Superstar II */
#define STRFMT_CRES  6                  /* stream format: Hemisphere */
#define STRFMT_STQ   7                  /* stream format: SkyTraq S1315F */
#define STRFMT_GW10  8                  /* stream format: Furuno GW10 */
#define STRFMT_JAVAD 9                  /* stream format: JAVAD GRIL/GREIS */
#define STRFMT_NVS   10                 /* stream format: NVS NVC08C */
#define STRFMT_BINEX 11                 /* stream format: BINEX */
#define STRFMT_RT17  12                 /* stream format: Trimble RT17 */
#define STRFMT_LEXR  13                 /* stream format: Furuno LPY-10000 */
#define STRFMT_SEPT  14                 /* stream format: Septentrio */
#define STRFMT_RINEX 15                 /* stream format: RINEX */
#define STRFMT_SP3   16                 /* stream format: SP3 */
#define STRFMT_RNXCLK 17                /* stream format: RINEX CLK */
#define STRFMT_SBAS  18                 /* stream format: SBAS messages */
#define STRFMT_NMEA  19                 /* stream format: NMEA 0183 */


#define CODE_NONE   0                   /* obs code: none or unknown */
#define CODE_L1C    1                   /* obs code: L1C/A,G1C/A,E1C (GPS,GLO,GAL,QZS,SBS) */
#define CODE_L1P    2                   /* obs code: L1P,G1P    (GPS,GLO) */
#define CODE_L1W    3                   /* obs code: L1 Z-track (GPS) */
#define CODE_L1Y    4                   /* obs code: L1Y        (GPS) */
#define CODE_L1M    5                   /* obs code: L1M        (GPS) */
#define CODE_L1N    6                   /* obs code: L1codeless (GPS) */
#define CODE_L1S    7                   /* obs code: L1C(D)     (GPS,QZS) */
#define CODE_L1L    8                   /* obs code: L1C(P)     (GPS,QZS) */
#define CODE_L1E    9                   /* (not used) */
#define CODE_L1A    10                  /* obs code: E1A        (GAL) */
#define CODE_L1B    11                  /* obs code: E1B        (GAL) */
#define CODE_L1X    12                  /* obs code: E1B+C,L1C(D+P) (GAL,QZS) */
#define CODE_L1Z    13                  /* obs code: E1A+B+C,L1SAIF (GAL,QZS) */
#define CODE_L2C    14                  /* obs code: L2C/A,G1C/A (GPS,GLO) */
#define CODE_L2D    15                  /* obs code: L2 L1C/A-(P2-P1) (GPS) */
#define CODE_L2S    16                  /* obs code: L2C(M)     (GPS,QZS) */
#define CODE_L2L    17                  /* obs code: L2C(L)     (GPS,QZS) */
#define CODE_L2X    18                  /* obs code: L2C(M+L),B1I+Q (GPS,QZS,CMP) */
#define CODE_L2P    19                  /* obs code: L2P,G2P    (GPS,GLO) */
#define CODE_L2W    20                  /* obs code: L2 Z-track (GPS) */
#define CODE_L2Y    21                  /* obs code: L2Y        (GPS) */
#define CODE_L2M    22                  /* obs code: L2M        (GPS) */
#define CODE_L2N    23                  /* obs code: L2codeless (GPS) */
#define CODE_L5I    24                  /* obs code: L5/E5aI    (GPS,GAL,QZS,SBS) */
#define CODE_L5Q    25                  /* obs code: L5/E5aQ    (GPS,GAL,QZS,SBS) */
#define CODE_L5X    26                  /* obs code: L5/E5aI+Q/L5B+C (GPS,GAL,QZS,IRN,SBS) */
#define CODE_L7I    27                  /* obs code: E5bI,B2I   (GAL,CMP) */
#define CODE_L7Q    28                  /* obs code: E5bQ,B2Q   (GAL,CMP) */
#define CODE_L7X    29                  /* obs code: E5bI+Q,B2I+Q (GAL,CMP) */
#define CODE_L6A    30                  /* obs code: E6A        (GAL) */
#define CODE_L6B    31                  /* obs code: E6B        (GAL) */
#define CODE_L6C    32                  /* obs code: E6C        (GAL) */
#define CODE_L6X    33                  /* obs code: E6B+C,LEXS+L,B3I+Q (GAL,QZS,CMP) */
#define CODE_L6Z    34                  /* obs code: E6A+B+C    (GAL) */
#define CODE_L6S    35                  /* obs code: LEXS       (QZS) */
#define CODE_L6L    36                  /* obs code: LEXL       (QZS) */
#define CODE_L8I    37                  /* obs code: E5(a+b)I   (GAL) */
#define CODE_L8Q    38                  /* obs code: E5(a+b)Q   (GAL) */
#define CODE_L8X    39                  /* obs code: E5(a+b)I+Q (GAL) */
#define CODE_L2I    40                  /* obs code: B1I        (BDS) */
#define CODE_L2Q    41                  /* obs code: B1Q        (BDS) */
#define CODE_L6I    42                  /* obs code: B3I        (BDS) */
#define CODE_L6Q    43                  /* obs code: B3Q        (BDS) */
#define CODE_L3I    44                  /* obs code: G3I        (GLO) */
#define CODE_L3Q    45                  /* obs code: G3Q        (GLO) */
#define CODE_L3X    46                  /* obs code: G3I+Q      (GLO) */
#define CODE_L1I    47                  /* obs code: B1I        (BDS) */
#define CODE_L1Q    48                  /* obs code: B1Q        (BDS) */
#define CODE_L5A    49                  /* obs code: L5A SPS    (IRN) */
#define CODE_L5B    50                  /* obs code: L5B RS(D)  (IRN) */
#define CODE_L5C    51                  /* obs code: L5C RS(P)  (IRN) */
#define CODE_L9A    52                  /* obs code: SA SPS     (IRN) */
#define CODE_L9B    53                  /* obs code: SB RS(D)   (IRN) */
#define CODE_L9C    54                  /* obs code: SC RS(P)   (IRN) */
#define CODE_L9X    55                  /* obs code: SB+C       (IRN) */
#define CODE_L1D    56                  /* obs code: B1C       (BDS) */
#define CODE_L5D    57                  /* obs code: B2a       (BDS) */
#define CODE_L7D    58                  /* obs code: B2a       (BDS) */
#define MAXCODE     59                  /* max number of obs code */


#define MAXFREQ     7                   /* max NFREQ */
#define NEXOBS      0


#define ENGPS
#ifdef ENGPS
#define MINPRNGPS   1                   /* min satellite PRN number of GPS */
#define MAXPRNGPS   32                  /* max satellite PRN number of GPS */
#define NSATGPS     (MAXPRNGPS-MINPRNGPS+1) /* number of GPS satellites */
#define NSYSGPS     1
#else
#define MINPRNGPS   0                   /* min satellite PRN number of GPS */
#define MAXPRNGPS   0                  /* max satellite PRN number of GPS */
#define NSATGPS     0                  /* number of GPS satellites */
#define NSYSGPS     0
#endif

#define ENAGLO
#ifdef ENAGLO
#define MINPRNGLO   1                   /* min satellite slot number of GLONASS */
#define MAXPRNGLO   24                  /* max satellite slot number of GLONASS */
#define NSATGLO     (MAXPRNGLO-MINPRNGLO+1) /* number of GLONASS satellites */
#define NSYSGLO     1
#else
#define MINPRNGLO   0
#define MAXPRNGLO   0
#define NSATGLO     0
#define NSYSGLO     0
#endif

#define ENAGAL
#ifdef ENAGAL
#define MINPRNGAL   1                   /* min satellite PRN number of Galileo */
#define MAXPRNGAL   37                  /* max satellite PRN number of Galileo */
#define NSATGAL    (MAXPRNGAL-MINPRNGAL+1) /* number of Galileo satellites */
#define NSYSGAL     1
#else
#define MINPRNGAL   0
#define MAXPRNGAL   0
#define NSATGAL     0
#define NSYSGAL     0
#endif

#define ENAQZS
#ifdef ENAQZS
#define MINPRNQZS   193                 /* min satellite PRN number of QZSS */
#define MAXPRNQZS   199                 /* max satellite PRN number of QZSS */
#define MINPRNQZS_S 183                 /* min satellite PRN number of QZSS SAIF */
#define MAXPRNQZS_S 189                 /* max satellite PRN number of QZSS SAIF */
#define NSATQZS     (MAXPRNQZS-MINPRNQZS+1) /* number of QZSS satellites */
#define NSYSQZS     1
#else
#define MINPRNQZS   0
#define MAXPRNQZS   0
#define MINPRNQZS_S 0
#define MAXPRNQZS_S 0
#define NSATQZS     0
#define NSYSQZS     0
#endif

#define ENABDS
#ifdef ENABDS
#define MINPRNBDS   1                   /* min satellite sat number of BeiDou */
#define MAXPRNBDS  63                  /* max satellite sat number of BeiDou */
#define NSATBDS     (MAXPRNBDS-MINPRNBDS+1) /* number of BeiDou satellites */
#define NSYSBDS     1
#else
#define MINPRNBDS   0
#define MAXPRNBDS   0
#define NSATBDS     0
#define NSYSBDS     0
#endif
#ifdef ENALEO
#define MINPRNLEO   1                   /* min satellite sat number of LEO */
#define MAXPRNLEO   10                  /* max satellite sat number of LEO */
#define NSATLEO     (MAXPRNLEO-MINPRNLEO+1) /* number of LEO satellites */
#define NSYSLEO     1
#else
#define MINPRNLEO   0
#define MAXPRNLEO   0
#define NSATLEO     0
#define NSYSLEO     0
#endif
//#define NSYS        (NSYSGPS+NSYSGLO+NSYSGAL+NSYSQZS+NSYSBDS+NSYSLEO) /* number of systems */
#define NSYS        (NSYSGPS+NSYSGLO+NSYSGAL+NSYSBDS+NSYSLEO) /* number of systems */

#ifdef ENSBS
#define MINPRNSBS   120                 /* min satellite PRN number of SBAS */
#define MAXPRNSBS   142                 /* max satellite PRN number of SBAS */
#define NSATSBS     (MAXPRNSBS-MINPRNSBS+1) /* number of SBAS satellites */
#else
#define MINPRNSBS   0                 /* min satellite PRN number of SBAS */
#define MAXPRNSBS   0                 /* max satellite PRN number of SBAS */
#define NSATSBS     0 /* number of SBAS satellites */
#endif

#define MAXSAT      (NSATGPS+NSATGLO+NSATGAL+NSATQZS+NSATBDS+NSATSBS+NSATLEO+1)

#define NFREQGLO    2                   /* number of carrier frequencies of GLONASS */
#define MAXRCV      64                  /* max receiver number (1 to MAXRCV) */
#define MAXOBS      64                  /* max number of obs in an epoch */

#define STR_MODE_R  0x1                 /* stream mode: read */
#define STR_MODE_W  0x2                 /* stream mode: write */
#define STR_MODE_RW 0x3                 /* stream mode: read/write */

#define MAXBAND     10                  /* max SBAS band of IGP */
#define MAXNIGP     201                 /* max number of IGP in SBAS band */
#define MAXSBSAGEL  1800.0              /* max age of SBAS long term corr (s) */
#define MAXSBSAGEF  30.0                /* max age of SBAS fast correction (s) */
#define MAXSBSMSG   32                  /* max number of SBAS msg in RTK server */

#ifdef WIN32
#define thread_t    HANDLE
#define lock_t      CRITICAL_SECTION
#define initlock(f) InitializeCriticalSection(f)
#define lock(f)     EnterCriticalSection(f)
#define unlock(f)   LeaveCriticalSection(f)
#define FILEPATHSEP '\\'
//#define strcasecmp  _stricmp
#include <io.h>
#define access      _access
#else
#define thread_t    pthread_t
#define lock_t      pthread_mutex_t
#define initlock(f) pthread_mutex_init(f,NULL)
#define lock(f)     pthread_mutex_lock(f)
#define unlock(f)   pthread_mutex_unlock(f)
#define FILEPATHSEP '/'
#endif
typedef struct {
    time_t time;    //seconds since 00:00:00 January 1st 1970
    double frac;    //fraction of second under 1s
}gtime_t;
typedef enum {
    LEN_ERROR = 0,
    CHECKSUM_ERROR = 1,
    INCOMPLETE = 2,
    TIMING_ERROR = 3,
    ID_ERROR = 4,
    IODE_ERROR = 5,
    TEST_ERROR = 6,
    MAT_ERR = 7,
    ZERO_MAT_ERR = 8,
    UNCHANGE = 9,
    OBS_ERROR = 0x0A,
    EPH_ERROR = 0x0B,
    ID4_ERROR = 0x0C,
    ID5_ERROR = 0x0D,
    NO_ERROR1 = 0x0E,
    NO_ERROR2 = 0x0F,
    NO_ERROR3 = 0x10,
    EPHEMERIS = 0x11,
    OBS = 0x12,
    SOLUTION = 0x13,
}Error;
typedef struct {        /* SNR mask type */
    int ena[2];         /* enable flag {rover,base} */
    double mask[NFREQ][9]; /* mask (dBHz) at 5,10,...85 deg */
} snrmask_t;

typedef struct {        /* antenna parameter type */
    int sat;            /* satellite number (0:receiver) */
    char type[64];  /* antenna type */
    char code[64];  /* serial number or satellite code */
    gtime_t ts, te;      /* valid time start and end */
    double off[NFREQ][3]; /* phase center offset e/n/u or x/y/z (m) */
    double var[NFREQ][19]; /* phase center variation (m) */
                        /* el=90,85,...,0 or nadir=0,1,2,3,... (deg) */
} pcv_t;

typedef struct {        /* antenna parameters type */
    int n, nmax;         /* number of data/allocated */
    pcv_t* pcv;         /* antenna parameters data */
} pcvs_t;

typedef struct {
    uint8_t sat; //satellite number
    int iode, iodc;
    int sva;    //SV accuracy
    int svh;    //SV health (0: ok)
    int code;           /* GPS/QZS: code on L2, GAL/CMP: data sources */
    int flag;           /* GPS/QZS: L2 P data flag, CMP: nav type */
    int week;    //gps week
    gtime_t toe, toc, ttr;    //Toe, Toc, T transmit
    double A, e, i0, OMG0, omg, M0, deln, OMgd, OMGd, idot;
    double crc, crs, cuc, cus, cic, cis;
    double toes;//Toe(s) in week
    double fit;//fit interval
    double f0, f1, f2;//SV clock params
    double tgd[4];    //group delay params
}eph_t;

typedef struct {        /* GLONASS broadcast ephemeris type */
    int sat;            /* satellite number */
    int iode;           /* IODE (0-6 bit of tb field) */
    int frq;            /* satellite frequency number */
    int svh, sva, age;    /* satellite health, accuracy, age of operation */
    gtime_t toe;        /* epoch of epherides (gpst) */
    gtime_t tof;        /* message frame time (gpst) */
    double pos[3];      /* satellite position (ecef) (m) */
    double vel[3];      /* satellite velocity (ecef) (m/s) */
    double acc[3];      /* satellite acceleration (ecef) (m/s^2) */
    double taun, gamn;   /* SV clock bias (s)/relative freq bias */
    double dtaun;       /* delay between L1 and L2 (s) */
} geph_t;

typedef struct {//observation data record, single band
    gtime_t time; //receiver sampling time
    int8_t pvtAvalidPsrIndex;
    uint8_t sat, rcv;//satellite/receiver number
    uint8_t SNR[NFREQ];
    uint8_t LLI[NFREQ];//lost of clock indicator
    unsigned char code[NFREQ]; /* code indicator (CODE_???) */
    unsigned long int LockTime[NFREQ];
    double L[NFREQ];
    double P[NFREQ];
    float D[NFREQ];
}obsd_t;
typedef struct {
    int n, nmax;    //number of observation data/allocated
    obsd_t* data;       /* observation data records */
}obs_t;
typedef struct {
    int n;    //number of observation data/allocated
    obsd_t data[MAXOBS];       /* observation data records */
}qobs_t;
typedef struct {
    unsigned char sat, rcv;
    double L[NFREQ];
    double P[NFREQ];
}obsd_tmp_t;
typedef struct {        /* earth rotation parameter data type */
    double mjd;         /* mjd (days) */
    double xp, yp;       /* pole offset (rad) */
    double xpr, ypr;     /* pole offset rate (rad/day) */
    double ut1_utc;     /* ut1-utc (s) */
    double lod;         /* length of day (s/day) */
} erpd_t;
typedef struct {        /* earth rotation parameter type */
    int n, nmax;         /* number and max number of data */
    erpd_t* data;       /* earth rotation parameter data */
} erp_t;
typedef struct
{      /*Configuration Option for User Setting*/
    unsigned char kMode;  /* Kinematic Calculation Mode: 0-Real-time Kinematic 1-Smoothed Real-time Kinematic; Default;2  */
    unsigned char freq;   /* Frequency Choice:1-L1 2-L2 4-L5;Default:1+2+4=7  all:15*/
    unsigned char iono;    /* Ionosphere Correction: 0-disable 1-enable 2-self-adaptive;Default:0*/
    unsigned char trop;   /* Troposphere Correction: 0-disable 1-enable 2-self-adaptive; Default:0*/
    unsigned char tides;  /* Tides Correction: 0-close 1-open ;Default 0*/
    unsigned char  sys;    /* System Choice: 1-GPS 2-QZSS  4-BDS 8-GAL 16-GLO;Default:1+2+4+8+16=31*/
    unsigned char  senceopt;   /* 0:disaster 1:bridge*/
    unsigned char detectSensitivity;
    unsigned char typeSol;
    unsigned char timeIntervalSolution;
    unsigned char minFixSat;
    unsigned char maxDelSat;
    unsigned char useRtcmPosFlag;
#ifdef MULBASE
    unsigned char masterSlaveBaseFlag;
#endif
    int param;
#ifdef MULBASE
    double masterXyz[3];
    double slaveXyz[3];
#endif
    long long gpsMask;
    long long qzssMask;
    long long glonassMask;
    long long galieoMask;
    long long bdsMask;
    double minSatRes;
    double iggiiik0;
    double iggiiik1;
    double smoothWindowsTime; /*smooth time(h)*/
    double initEnuTime;
    unsigned int diffAgeMax;  /* Max Age of Diff, unit:s Default: 30*/
    unsigned int buffSize;  /*Buffer Size Default:4kB */
    float  cn0Min;      /* C/N0 Cut-off.from0 to 60, unit:dBHz*/
    float elevMin;     /* Elevation Cut-off,from 0 to 90,unit:degree*/
    float gdopThld;    /* GDOP Threshold */
    float postResThld;    /* Posterior Residual Threshold, >0.02;Default:0.02 */
    float timeInterval;   /* Time Interval,supports 30/15/10/5/1/0.5/0.1s; Default:1s */
    float stationPCV[3];  /* Antenna Phase Center {e,n,u}( unit: mm)*/
    int maxPosSat;
    double rb[3];
    int enuWindowIndex[3];
    double enuWindow[3];
}cfgopt_t;
typedef struct {        /* processing options type */
    unsigned char mode;           /* positioning mode (PMODE_???) */
    unsigned char kMode;          /* Kinematic Calculation Mode: 0-Real-time Kinematic 1-Smoothed Real-time Kinematic; Default:1  */
    unsigned char freq;           /* Frequency Choice:1-L1 2-L2 4-L5;Default:1+2+4=7*/
    unsigned char nf;             /* number of frequencies (1:L1,2:L1+L2,3:L1+L2+L5) */
    unsigned char sateph;         /* satellite ephemeris/clock (EPHOPT_???) */
    unsigned char ionoopt;        /* ionosphere option (IONOOPT_???) */
    unsigned char tropopt;        /* troposphere option (TROPOPT_???) */
    unsigned char ioncfg;
    unsigned char trocfg;
    unsigned char tidecfg;
    unsigned char dynamics;       /* dynamics model (0:none,1:velociy,2:accel) */
    unsigned char tidecorr;
    unsigned char  sys;            /* System Choice: 1-GPS 2-QZSS  4-BDS 8-GAL 16-GLO;Default:1+2+4+8+16=33*/
    unsigned char senceopt;
    unsigned char detectSensitivity;
    unsigned char typeSol;
    unsigned char timeIntervalSolution;
    unsigned char minFixSat;
    unsigned char maxDelSat;
    unsigned char useRtcmPosFlag;
#ifdef MULBASE
    unsigned char masterSlaveBaseFlag;
#endif
    long long gpsMask;
    long long qzssMask;
    long long glonassMask;
    long long galieoMask;
    long long bdsMask;
    int param;
#ifdef MULBASE
    double masterXyz[3];
    double slaveXyz[3];
#endif
    double minSatRes;
    double iggiiik0;
    double iggiiik1;
    double smoothWindowsTime; /*smooth time (h)*/
    double initEnuTime;     /*init original point time(h)*/
    unsigned int  cn0Min;         /* C/N0 Cut-off.from0 to 60, unit:dBHz*/
    unsigned int buffSize;  /*Buffer Size Default:4kB */
    float  postResThld;  /* Posterior Residual Threshold, >0.02;Default:0.02 */
    float timeInterval;
    double elmin;       /* elevation mask angle (rad) */
    double maxtdiff;    /* max difference of time (sec) */
    double maxinno;     /* reject threshold of innovation (m) */
    double maxgdop;     /* reject threshold of gdop */
    double ru[3];       /* rover position for fixed mode {x,y,z} (ecef) (m) */
    double rb[3];       /* base position for relative mode {x,y,z} (ecef) (m) */
    double std;
    double odisp[2][6 * 11]; /* ocean tide loading parameters {rov,base} */
    double bl;
    double differHeight;
    int maxPosSat;
    int enuWindowIndex[3];
    double enuWindow[3];
} prcopt_t;

typedef struct {        /* solution options type */
    int posf;           /* solution format (SOLF_???) */
    int times;          /* time system (TIMES_???) */
    int timef;          /* time format (0:sssss.s,1:yyyy/mm/dd hh:mm:ss.s) */
    int timeu;          /* time digits under decimal point */
    int degf;           /* latitude/longitude format (0:ddd.ddd,1:ddd mm ss) */
    int outhead;        /* output header (0:no,1:yes) */
    int outopt;         /* output processing options (0:no,1:yes) */
    int outvel;         /* output velocity options (0:no,1:yes) */
    int datum;          /* datum (0:WGS84,1:Tokyo) */
    int height;         /* height (0:ellipsoidal,1:geodetic) */
    int geoid;          /* geoid model (0:EGM96,1:JGD2000) */
    int solstatic;      /* solution of static mode (0:all,1:single) */
    int sstat;          /* solution statistics level (0:off,1:states,2:residuals) */
    int trace;          /* debug //trace level (0:off,1-5:debug) */
    double nmeaintv[2]; /* nmea output interval (s) (<0:no,0:all) */
                        /* nmeaintv[0]:gprmc,gpgga,nmeaintv[1]:gpgsv */
    char sep[64];       /* field separator */
    char prog[64];      /* program name */
    double maxsolstd;   /* max std-dev for solution output (m) (0:all) */
} solopt_t;

typedef struct {        /* file options type */
    char satantp[MAXSTRPATH]; /* satellite antenna parameters file */
    char rcvantp[MAXSTRPATH]; /* receiver antenna parameters file */
    char stapos[MAXSTRPATH]; /* station positions file */
    char geoid[MAXSTRPATH]; /* external geoid data file */
    char iono[MAXSTRPATH]; /* ionosphere data file */
    char dcb[MAXSTRPATH]; /* dcb data file */
    char eop[MAXSTRPATH]; /* eop data file */
    char blq[MAXSTRPATH]; /* ocean tide loading blq file */
    char tempdir[MAXSTRPATH]; /* ftp/http temporaly directory */
    char geexe[MAXSTRPATH]; /* google earth exec file */
    char solstat[MAXSTRPATH]; /* solution statistics file */
    char trace[MAXSTRPATH]; /* debug //trace file */
} filopt_t;

typedef struct {
    FILE* spppos_r;
    FILE* spppos_b;
    FILE* pdop1;
    FILE* pdop2;
    FILE* elev;
    FILE* resp;
    FILE* resc1;
    FILE* resp1;
    FILE* resc2;
    FILE* resp2;
    FILE* resc3;
    FILE* resp3;
    FILE* resc4;
    FILE* resc5;
    FILE* resc6;
    FILE* resp4;
    FILE* resp5;
    FILE* resp6;
    FILE* mp1;
    FILE* mp2;
    FILE* gf;
    FILE* mw;
    FILE* lp;
    FILE* ambN1;
    FILE* ambN2;
    FILE* ambN3;
    FILE* ion;
    FILE* trop;
    FILE* P1;
    FILE* P2;
    FILE* L1;
    FILE* L2;
    // FILE* navini;
    FILE* fpOut[2];
} myFile_t;
typedef struct {        /* navigation data type */
    int n;         /* number of broadcast ephemeris */
    int ng;       /* number of glonass ephemeris */
    eph_t* eph;         /* GPS/QZS/GAL ephemeris */
    geph_t* geph;       /* GLONASS ephemeris */
    double ion_gps[8];
    erp_t erp;//earth rotation param
} nav_t;
typedef struct {        /* station parameter type */
    char name[64]; /* marker name */
    char marker[64]; /* marker number */
    char antdes[64]; /* antenna descriptor */
    char antsno[64]; /* antenna serial number */
    char rectype[64]; /* receiver type descriptor */
    char recver[64]; /* receiver firmware version */
    char recsno[64]; /* receiver serial number */
    int antsetup;       /* antenna setup id */
    int itrf;           /* ITRF realization year */
    int deltype;        /* antenna delta type (0:enu,1:xyz) */
    double pos[3];      /* station position (ecef) (m) */
    double del[3];      /* antenna position delta (e/n/u or x/y/z) (m) */
    double hgt;         /* antenna height (m) */
} sta_t;

typedef struct {        /* RTCM control struct type */
    int staid;          /* station id */
    int index;
    int stah;           /* station health */
    int seqno;          /* sequence number for rtcm 2 or iods msm */
    int outtype;        /* output message type */
    int rcv;
    //int week;
    gtime_t time;       /* message time */
    gtime_t time_s;     /* message start time */
    obs_t obs;          /* observation data (uncorrected) */
    sta_t sta;          /* station parameters */
    char msg[128];      /* special message */
    char msgtype[256];  /* last message type */
    int obsflag;        /* obs data complete flag (1:ok,0:not complete) */
    int ephsat;         /* update satellite of ephemeris */
    unsigned short lock[MAXSAT][NFREQ]; /* lock time */
    int nbyte;          /* number of bytes in message buffer */
    int nbit;           /* number of bits in word buffer */
    int len;            /* message length (bytes) */
    unsigned char buff[1024]; /* message buffer */
    unsigned int word;  /* word buffer for rtcm 2 */
    char opt[256];      /* RTCM dependent options */
} rtcm_t;

typedef struct {
    gtime_t time;
    gtime_t time_pre;
    float  qv[6];       /* velocity variance/covariance (m^2/s^2) */
    double rr[9];//pos/vel (m,m/s)
    double rr_lsq[3];
    double rr_ref[3];
    double rr_original[3];
    double enu_original_window[60][3];
    double rr_filer[3];
    double vel[3];
    double acc[3];
    double rr_pre[3];//pos/vel (m,m/s)
    double vel_pre[3];//pos/vel (m,m/s)
    float qr[6];//pos variance/covariance (m^2)
    double enu[3];
    double enu_original[3];
    double fixxyz[3];
    double rr_smooth[3];
    double rr_smooth_cnt;
    /* {c_xx,c_yy,c_zz,c_xy,c_yz,c_zx} or */
/* {c_ee,c_nn,c_uu,c_en,c_nu,c_ue} */
    double dtr[NSYS];//receiver clock bias
    uint8_t type;//0: xyz-ecef, 1:enu-baseline
    uint8_t stat,statPre;//solution status
    uint8_t ns[2];//number of valid satellites
    uint8_t nsLsq;
    uint8_t nsLsqPre;
    uint8_t nsWL, nsNL;
    double bslLength;//
    float age;//age of differential (s)
    float ratio;//for validation
    double dop[2][4];
    //unsigned int nAveFixCnt[3];
    //unsigned int nVarFixCnt[3];
    unsigned int fixCnt;//
    unsigned int floatCnt;//
    unsigned char nsFixPre;
    double ori_ave[3];
    double ori_var[3];
    double enu_shift[3];
    double enu_sum[3];
    unsigned int ilterCout;
    unsigned char bslConstrain;
    unsigned char thresCnt1[3];
    unsigned char thresCnt2[3];
    double aveFixSat;
    unsigned int aveFixSatCnt;
    int nAveFixCnt[3];

    wind_t window[3];
    double jump[3];
    double tmpjump[3];
    data_t wdata;
}sol_t;

typedef struct {
    char vs;//valid sat flag
    uint8_t sat;
    uint8_t vsat[NFREQ];//valid sat flag
    uint8_t vsatWL, vsatNL, fixWL, fixNL;
    uint8_t fix[NFREQ];//1:fix,2:float,3:hold
    uint8_t slip[NFREQ];//cycle-slip flag
    uint8_t half[NFREQ];//cycle-slip flag
    uint8_t rejRes;
    uint8_t slip_cout[NFREQ];
    uint8_t quickSelSatDel;
    uint8_t resMaxCnt;
    uint8_t SNR[NFREQ];
    double azel[2][2];//azimuth, elevation angle (rad)
    double resc[NFREQ];
    double resc2[NFREQ];
    double rs[3];  //satellite position
    //double resp[NFREQ];
    unsigned int resCnt;
    unsigned int timeCout;
    double gf[NFREQ];
    double ph[2][NFREQ];
    double fix_amb[NFREQ];
    double resBias[NFREQ];
    double ddAmb[NFREQ];
    double resDdAmb[NFREQ];
    double ddion;
    double ddtrp;
    double dist[2];
    double fix_trop;
    double fix_ion;
    double dion;
    gtime_t ddionTime;
    double ddFixBiasWL;
    double ddFixBiasNL;
    double fbias[NFREQ];
    int ionIndexCnt; 
    double Ri;
    double ddl;
    double ddlcru;
    //unsigned int useCnt[NFREQ];
    //gtime_t pt[2][NFREQ];
    //double ddtrp;
    //double dion;
    //gtime_t pt[2][NFREQ];//previous carrier-phase time
    //gtime_t gf_t[NFREQ];//previous carrier-phase time
    //double ph[2][NFREQ];//previous carrier-phase observable (cycle)
    FILE* fp_ssat;
}ssat_t;

//#define xyzWindowSize 86400
typedef struct {        /* RTK control/result type */
    unsigned char allSlipFlag[NSYS][NFREQ];
    unsigned char base_prn[NSYS][NFREQ * 2];
    unsigned char base_prn_pre[NSYS][NFREQ * 2];
    unsigned char base_prn_fix[NSYS][NFREQ * 2];
    unsigned char basePrnLsq[NSYS][NFREQ * 2];
    unsigned char nsLsq[NFREQ];
    unsigned char satLsq[NFREQ][MAXOBS];
    unsigned char fix_state;
    unsigned char nsSat[MAXOBS];
    unsigned char nsSatPre[MAXOBS];
    unsigned char ns;
    unsigned char nsPre;
    unsigned char nxRecordSat[5 + 40 + 40 * NFREQ];
    unsigned char nxRecordFrq[5 + 40 + 40 * NFREQ];
    unsigned char nxRecordSatPre[5 + 40 + 40 * NFREQ];
    unsigned char nxRecordFrqPre[5 + 40 + 40 * NFREQ];
    unsigned char nxFixSat[5 + 40 + 40 * NFREQ];
    unsigned char nxFixFrq[5 + 40 + 40 * NFREQ];
    unsigned char nxFixSatPre[5 + 40 + 40 * NFREQ];
    unsigned char nxFixFrqPre[5 + 40 + 40 * NFREQ];
    unsigned char nxFixNx;
    unsigned char nxFixNxPre;
    unsigned char preStat;
    unsigned char rejSatCnt;
    unsigned char noRejectSatCnt;
    unsigned char fix30flag;
    unsigned char fixCheckCnt;
    unsigned char fixErrorLargeCnt;
    sol_t  sol;         /* RTK solution */
    sol_t solb;
    double rb[6];       /* base position/velocity (ecef) (m|m/s) */
    double prb[3];
    int nx, np, na, nt, ni;          /* number of float states/fixed states */
    int nxPre, npPre, naPre, ntPre, niPre;
    unsigned int nfloat;
    unsigned int nfix;           /* number of continuous fixes of ambiguity */
    double tt, fs;          /* time difference between current and previous (s) */
    double tt_pre;
    double sumPostCarV;
    double Pa[9];
    double* x, * P;      /* float states and their covariance */
    double* xp, * Pp;  //31*2k
    double* H;
    double* F;
    double* K;
    double* I;
    double* Ri;
    double* Rj;
    double* R;//41k
    double* v;
    ssat_t ssat[MAXSAT]; /* satellite status */
    prcopt_t opt;       /* processing options */
    double* enuWindow[3];
    //double *eWindow;
    //double *nWindow;
    //double* uWindow;
    double* enuWindowMedian[3];
    int enuWindowMedianShiftNum[3];
    int cntEnuWind;
    int maxMedianFilterPoint;
    unsigned char sumPostCarVCnt;
    double aveXyz[3];
    double aveEnu[3];
    double stdEnu[3];
    unsigned int xyzWindwoIndex;
    unsigned int enuWindwoIndex[3];
    double fftFrq[3];
    double fftPower[3];
    double satMapEnu[5 + 40 + 40 * NFREQ][3];
    double satMapNfix[5 + 40 + 40 * NFREQ];
    char s[64];
    int maxSmoothPoint;
    double sum_enu[3];
    double sum_sqeun[3];
    gtime_t enuShiftEpochTime;
    int delpoint[3];
    int iniCnt;
    double enuDelay[60][3];
    double dr[3];
    double masterEnu[3];
    double masterRr[3];
    char path[MAXSTRPATH];
    int mpflag;
} rtk_t;

typedef struct {        /* stream type */
    int type;           /* type (STR_???) */
    int mode;           /* mode (STR_MODE_?) */
    int state;          /* state (-1:error,0:close,1:open) */
    uint32_t inb, inr;   /* input bytes/rate */
    uint32_t outb, outr; /* output bytes/rate */
    uint32_t tick_i;    /* input tick tick */
    uint32_t tick_o;    /* output tick */
    uint32_t tact;      /* active tick */
    uint32_t inbt, outbt; /* input/output bytes at tick */
    lock_t lock;        /* lock flag */
    void* port;         /* type dependent port control struct */
    char path[MAXSTRPATH]; /* stream path */
    char msg[MAXSTRPATH];  /* stream message */
} stream_t;
typedef struct {
    rtk_t rtk;
    //rtk_t rtkepoch;
    int state;
    int cycle;          /* processing cycle (ms) */
    int nb[2];
    int cputime;
    int nsbs;           /* number of sbas message */
    int npb[2];         /* bytes in input peek buffers */
    int buffPtr[2];//buffer pointer
    int navsel;         /* ephemeris select (0:all,1:rover,2:base,3:corr) */
    int prcout;         /* missing observation data count */
    unsigned int tick;  /* start tick */
    thread_t thread;    /* server thread */
    unsigned char* pbuf[2]; /* peek buffers {rov,base,corr} */
    unsigned char* buff[2]; /* input buffers {rov,base,corr} */
    rtcm_t rtcm[2];
    gtime_t ftime;//download time
    obs_t obs[2][128];//observation data (rover,base)
    stream_t stream[3];
    int format[3];      /* input format {rov,base,corr} */
    lock_t lock;        /* lock flag */
}rtksvr_t;

typedef struct
{
    qobs_t data[MAXQUEUESIZE];
    int front;
    int rear;
}obsqueue_t;


extern const solopt_t solopt_default; /* default solution output options */
extern unsigned char trace_flag[64];
extern struct timeval tvl;
extern double start, end, ntime[10];
extern unsigned int g_nfloat;
extern unsigned int g_nfix;
extern myFile_t oFile;
extern nav_t g_nav;
extern rtk_t g_rtk;
extern obsd_t g_preBaseObsRtk[MAXOBS];
extern int g_preBaseObsRtkNum;
extern int SELETE_SAT_NUM;
extern int NX;
extern int NY;


extern double g_gpsLam[NFREQ];
extern double g_galLam[NFREQ];
extern double g_bdsLam[NFREQ];
extern double g_gloLam[MAXPRNGLO][NFREQ];


extern void strlock(stream_t* stream);
extern void strunlock(stream_t* stream);
extern int stropen(stream_t* stream, int type, int mode, const char* path);
extern int strread(stream_t* stream, uint8_t* buff, int n);
extern void strclose(stream_t* stream);
extern void strinitcom(void);
extern void decodetcppath(const char* path, char* addr, char* port, char* user,
    char* passwd, char* mntpnt, char* str);
extern void outResult(rtk_t* rtk, const solopt_t* sopt);
extern const double chisqr[100];
extern gtime_t epoch2time(const double* ep);
extern gtime_t utc2gpst(gtime_t t);
extern gtime_t gpst2utc(gtime_t t);
extern gtime_t gpst2time(int week, double sec);
extern double time2doy(gtime_t t);
extern double time2gpst(gtime_t t, int* week);
extern gtime_t timeget(void);
extern void setbit(uint8_t* buff, int word, int pos, int len, int32_t value);
extern double timediff(gtime_t t1, gtime_t t2);
extern gtime_t timeadd(gtime_t t, double sec);
extern void timeset(gtime_t t);
extern unsigned int rtk_crc24q(const unsigned char* buff, int len);
extern unsigned short rtk_crc16(const unsigned char* buff, int len);
extern double* mat(int r, int c);
extern double* zeros(int r, int c);
extern double* zerosChar(int r, int c);
extern void matcpy(double* A, const double* B, int n, int m);
extern int* imat(int n, int m);
extern void matmul(const char* tr, int n, int k, int m, double alpha,
    const double* A, const double* B, double beta, double* C);
extern void matmul33(const char* tr, const double* A, const double* B, const double* C,
    int n, int p, int q, int m, double* D);
extern int matinv(double* A, int n);
extern int solve(const char* tr, const double* A, const double* Y, int n,
    int m, double* X);
extern int lsq(const double* A, const double* y, int n, int m, double* x,
    double* Q);
extern int filter(rtk_t* rtk, double* x, double* P, double* H, double* v, double* R, int n, int m,
    double* xp, double* Pp);
extern int matinvLambda(rtk_t* rtk, double* A, int n);
extern double norm(const double* a, int n);
extern void dops(int ns, const double* azel, double elmin, double* dop);
extern void xyz2enu(const double* pos, double* E);
extern void ecef2enu(const double* pos, const double* r, double* e);
extern void enu2ecef(const double* pos, const double* e, double* r);
extern double satazel(const double* pos, const double* e, double* azel);
extern void ecef2pos(const double* r, double* pos);
extern void pos2ecef(const double* pos, double* r);
extern double geodist(const double* rs, const double* rr, double* e);
extern double tropmodel(gtime_t time, const double* pos, const double* azel,
    double humi);
extern int ionocorr(gtime_t time, int sat, const double* pos,
    const double* azel, int ionoopt, double* ion, double* var);
extern int tropcorr(gtime_t time, const double* pos,
    const double* azel, int tropopt, double* trp, double* var);
extern double ionmodel(gtime_t t, const double* ion, const double* pos,
    const double* azel);
extern double dot(const double* a, const double* b, int n);
extern void time2epoch(gtime_t t, double* ep);
extern void covenu(const double* pos, const double* P, double* Q);
extern double tropmapf(gtime_t time, const double pos[], const double azel[],
    double* mapfw);
extern double* eye(int n);
extern int testsnr(int base, int freq, double el, double snr,
    const snrmask_t* mask);
extern unsigned char satno(unsigned char sys, unsigned char prn);
extern int satid2no(const char *id);

extern double get_sid_T(unsigned char sat, gtime_t teph, const nav_t* nav);
extern int test_update_data();
extern int initDat(FILE* fp, int npoint);
extern int calOffset(unsigned char sat, gtime_t ctime, int intv);

extern int invalidBDS(unsigned char sat);
extern int isGEO(unsigned char sat);
extern int isIGSO(unsigned char sat);
extern int isMEO(unsigned char sat);
extern int periodDay(unsigned char sat);

extern void rtksvrstart(rtksvr_t* svr);
extern int rtkpos(rtk_t* rtk, obsd_t* obs, int n);
extern int rtkepoch(rtk_t* rtk, obsd_t* obs, int n);

extern int pntpos(int base, obsd_t* obs, int n, sol_t* sol,
    double* azel, ssat_t* ssat, const prcopt_t* opt, double tt);
extern int test_sys(int sys, int m);
//ephemeris.c
extern void satposs(gtime_t teph, obsd_t* obs, int n,
    int ephopt, double* rs, double* dts, double* var, int* svh);
extern int peph2pos(gtime_t time, int sat, const nav_t* nav, int opt,
    double* rs, double* dts, double* var);
extern void geph2pos(gtime_t time, const geph_t* geph, double* rs, double* dts,
    double* var);
extern int lambda(rtk_t* rtk, int n, int m, const double* a, const double* Q, double* f, double* s, int lcopt);
extern int plambda(const double* a, const double* Qa, int n, int m, double* F, double* s, double p0);
extern int bootstrap(int n, const double* a, const double* Q, double* F, double* Ps, char** msg);
extern int readrnxh(FILE* fp, double* ver, char* type, unsigned char* sys, int* tsys,
    char tobs[][MAXOBSTYPE][4], nav_t* nav, sta_t* sta, int* lineCount, int maxLine);
extern int readrnxnav(FILE* fp, const char* opt, double ver, unsigned char sys, nav_t* nav, int* lineCount, int maxLine);
extern int readrnxobs(FILE* fp, gtime_t ts, gtime_t te, double tint, const char* opt,
    int rcv, double ver, int* tsys, char tobs[][MAXOBSTYPE][4], obs_t* obs, sta_t* sta, int* lineCount, int maxLine);
extern int readrnxobsb(FILE* fp, const char* opt, double ver, int* tsys,
    char tobs[][MAXOBSTYPE][4], int* flag, obsd_t* data, sta_t* sta, int* lineCount, int maxLine);

extern char* code2obs(unsigned char code, int* freq);
extern unsigned char obs2code(const char* obs, int* freq);
extern int getcodepri(unsigned char sys, unsigned char code, const char* opt);
extern int input_rtcm3(rtcm_t* rtcm, unsigned char data);

extern int decode_obsepoch(FILE* fp, char* buff, double ver, gtime_t* time,
    int* flag, int* sats);
extern void decode_obsh(FILE* fp, char* buff, double ver, int* tsys,
    char tobs[][MAXOBSTYPE][4], nav_t* nav, sta_t* sta);
extern char* time_str(gtime_t t, int n);
extern int outhead(const char* outfile, char infile[][MAXSTRPATH], int n,
    const prcopt_t* popt, const solopt_t* sopt);
extern void outheader(FILE* fp, char file[][MAXSTRPATH], int n, const prcopt_t* popt,
    const solopt_t* sopt);
extern int outpos(unsigned char* buff, const char* s, const sol_t* sol,
    const solopt_t* opt);
extern int outsols(unsigned char* buff, rtk_t* rtk, sol_t* sol, const double* rb,
    const solopt_t* opt);
extern int outsol(FILE* fp, rtk_t* rtk, sol_t* sol, const double* rb,
    const solopt_t* opt);
extern unsigned char satsys(unsigned char sat, unsigned char* prn);
extern void time2str(gtime_t t, char* s, int n);
//extern void time_output(gtime_t t, char* s, int n);

extern int rtksvrinit(rtksvr_t* svr);
extern int init_rtcm(rtcm_t* rtcm);
extern int decoderaw(rtksvr_t* svr, int index);
extern int adjgpsweek(int week);
extern gtime_t bdt2time(int week, double sec);
extern gtime_t bdt2gpst(gtime_t t);

extern int showmsg(char* format, ...);
extern void settspan(gtime_t ts, gtime_t te);
extern void settime(gtime_t time);
extern void covecef(const double* pos, const double* Q, double* P);
extern void satno2id(unsigned char sat, char* id);

//-------ppp-----------
extern void sunmoonpos(gtime_t tutc, const double* erpv, double* rsun,
    double* rmoon, double* gmst);
extern void eci2ecef(gtime_t tutc, const double* erpv, double* U, double* gmst);
extern double utc2gmst(gtime_t t, double ut1_utc);
extern void cross3(const double* a, const double* b, double* c);
extern int normv3(const double* a, double* b);
extern void antmodel(const pcv_t* pcv, const double* del, const double* azel,
    int opt, double* dant);
extern void antmodel_s(unsigned char sat, const pcv_t* pcv, double nadir, double* dant);
extern int geterp(const erp_t* erp, gtime_t time, double* erpv);
extern void tidedisp(gtime_t tutc, const double* rr, int opt, const erp_t* erp,
    const double* odisp, double* dr);
extern double str2num(const char* s, int i, int n);
extern int readsap(const char* file, gtime_t time, nav_t* nav);
extern void createdir(const char* path);
extern unsigned int tickget(void);
extern void sleepms(int ms);
extern int reppath(const char* path, char* rpath, gtime_t time, const char* rov,
    const char* base);
extern int execcmd(const char* cmd);
extern void deg2dms(double deg, double* dms, int ndec);
extern void strinit(stream_t* stream);
extern int strwrite(stream_t* stream, uint8_t* buff, int n);
//-----------------------------sabs-----------------------------
extern void traceopen(const char *file);
extern void traceclose(void);
extern void tracelevel(int level);
extern void trace    (int level, const char *format, ...);
extern void tracet   (int level, const char *format, ...);
extern void tracemat (int level, const double *A, int n, int m, int p, int q);
extern void traceobs (int level, const obsd_t *obs, int n);
extern void tracenav (int level, const nav_t *nav);
extern void tracegnav(int level, const nav_t *nav);
extern void tracehnav(int level, const nav_t *nav);
extern void tracepeph(int level, const nav_t *nav);
extern void tracepclk(int level, const nav_t *nav);
extern void traceb   (int level, const unsigned char *p, int n);
//--------------------------------myfun------------------------------
extern int findGephIndex2(geph_t* geph, unsigned char sat);
extern int findGephIndex(geph_t* geph, unsigned char sat);
extern int findEphIndex(eph_t* eph, unsigned char sat);
extern void findMaxRes(rtk_t* rtk, unsigned char* sat, int ns);
extern void detectionRes(rtk_t* rtk, obsd_t* obs, int n);
extern void gloFlag(rtk_t* rtk, const obsd_t* obs, unsigned char nu, unsigned char nr);
extern int selectSatFlag(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr);
extern void quickSelSat(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr);
extern int preBaseObsRTK(rtk_t* rtk, obsd_t* obs, unsigned char* nu1, unsigned char* nr1, int* n1, int flag);
extern int selsatRTK(rtk_t* rtk, const obsd_t* obs, unsigned char nu, unsigned char nr,
    const prcopt_t* opt, unsigned char* sat, unsigned char* iu, unsigned char* ir, unsigned char elFlag);
extern int selsatDGPS(rtk_t* rtk, const obsd_t* obs, int n,
    const prcopt_t* opt, unsigned char* sat, unsigned char* iu, unsigned char* ir);
extern int checkFixP(rtk_t* rtk, int lcopt);
extern int checkFloatP(rtk_t* rtk, int lcopt);
extern int obsScan(rtk_t* rtk, const prcopt_t* popt, obsd_t* obs, const int nobs);
#ifdef WIN32
extern int gettimeofday(struct timeval* tp, void* tzp);
#endif
extern void assignSatBias(rtk_t* rtk, gtime_t teph, gtime_t tepb, obsd_tmp_t* obs_tmp, int ns);
extern int rtkLsq(rtk_t* rtk, const obsd_t* obs, int n, int nu, const int* svh, double* rs, double* dts, double* var_sat,
    double* lsqraim, unsigned char ns, unsigned char* sat, unsigned char* iu, unsigned char* ir, unsigned char* exc);

extern int rtkLsqIF(rtk_t* rtk, const obsd_t* obs, int n, int nu, const int* svh, double* rs, double* dts, double* var_sat,
    double* lsqraim, unsigned char ns, unsigned char* sat, unsigned char* iu, unsigned char* ir, unsigned char* exc);

extern double L_LP(double i, double j, double k, double f1, double f2, double f5, const double* Pi, const double* Pj);
extern double L_LC(double i, double j, double k, double f1, double f2, double f5, const double* Li, const double* Lj);
extern double lam_LC(double i, double j, double k, double f1, double f2, double f5);
extern double L_LP2(double i, double j, double k, double f1, double f2, double f5, const double* Pi);
extern double L_LC2(double i, double j, double k, double f1, double f2, double f5, const double* Li);
extern double var_LC(double i, double j, double k, double f1, double f2, double f5, double sig);
extern double var_LCion(double i, double j, double k, double f1, double f2, double f5, double bl, double el);
extern double SD_var(double var, double el);
extern void rtkARWL(rtk_t* rtk, obsd_t* obs, unsigned char* sat, int n);
extern void rtkARNL(rtk_t* rtk, obsd_t* obs, unsigned char* sat, int n);
extern int resamb_LAMBDA(rtk_t* rtk, int lcopt);
extern int relposWL(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr,
    unsigned char* sat, unsigned char* iu, unsigned char* ir, double* rs, double* dts,
    double* var, int* svh);
extern int zdresWL(rtk_t* rtk, int base, const obsd_t* obs, int n, const double* rs,
    const double* dts, const int* svh, const double* rr, const prcopt_t* opt, int index, double* y,
    double* e, double* azel, double* rdist);
extern int checkWL_res(rtk_t* rtk, const double* x, unsigned char* sat,
    double* y, unsigned char* iu, unsigned char* ir, int ns, double* azel);
extern int relposNL(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr,
    unsigned char* sat, unsigned char* iu, unsigned char* ir, double* rs, double* dts,
    double* var, int* svh);
extern int test_sysWL(int sys, unsigned char prn, int m);
extern int test_sysNL(int sys, unsigned char prn, int m);
extern double varrL(const obsd_t* obs, double el, double bl, int f, const prcopt_t* opt);
#endif 



