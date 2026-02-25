
#include "rtk.h"
/* constants -----------------------------------------------------------------*/
static const double bdt0[] = {2006, 1, 1, 0, 0, 0}; /* beidou time reference */
#define PRUNIT_GPS 299792.458                       /* rtcm ver.3 unit of gps pseudorange (m) */
#define PRUNIT_GLO 599584.916                       /* rtcm ver.3 unit of glonass pseudorange (m) */
#define RANGE_MS (CLIGHT * 0.001)                   /* range in 1 ms */

#define P2_10 0.0009765625          /* 2^-10 */
#define P2_34 5.820766091346740E-11 /* 2^-34 */
#define P2_46 1.421085471520200E-14 /* 2^-46 */
#define P2_59 1.734723475976810E-18 /* 2^-59 */
#define P2_66 1.355252715606880E-20 /* 2^-66 */
#define RTCM3PREAMB 0xD3            /* rtcm ver.3 frame preamble */

#define PRUNIT_BD2 299792.458 /* rtcm ver.3 unit of bd2 pseudorange (m) */
/* type definition -----------------------------------------------------------*/
extern double writeDugTime;

typedef struct
{                               /* multi-signal-message header type */
    unsigned char iod;          /* issue of data station */
    unsigned char time_s;       /* cumulative session transmitting time */
    unsigned char clk_str;      /* clock steering indicator */
    unsigned char clk_ext;      /* external clock indicator */
    unsigned char smooth;       /* divergence free smoothing indicator */
    unsigned char tint_s;       /* soothing interval */
    unsigned char nsat, nsig;   /* number of satellites/signals */
    unsigned char sats[64];     /* satellites */
    unsigned char sigs[32];     /* signals */
    unsigned char cellmask[64]; /* cell mask */
} msm_h_t;

static double lam_carr[MAXFREQ] = {/* carrier wave length (m) */
                                   CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5, CLIGHT / FREQ6,
                                   CLIGHT / FREQ7, CLIGHT / FREQ8, CLIGHT / FREQ9
};
/* msm signal id table -------------------------------------------------------*/
const char* msm_sig_gps2[32] = {
    /* GPS: ref [13] table 3.5-87, ref [14][15] table 3.5-91 */
    "", "1C", "1P", "1W", "1Y", "1M", "",   "2C", "2P", "2W", "2Y", "2M", /*  1-12 */
    "", "",   "2S", "2L", "2X", "",   "",   "",   "",   "5I", "5Q", "5X", /* 13-24 */
    "", "",   "",   "",   "",   "1S", "1L", "1X"                          /* 25-32 */
};
const char* msm_sig_glo2[32] = {
    /* GLONASS: ref [13] table 3.5-93, ref [14][15] table 3.5-97 */
    "", "1C", "1P", "", "", "", "", "2C", "2P", "", "3I", "3Q", "3X", "", "", "",
    "", "",   "",   "", "", "", "", "",   "",   "", "",   "",   "",   "", "", ""
};
const char* msm_sig_gal2[32] = {
    /* Galileo: ref [15] table 3.5-100 */
    "", "1C", "1A", "1B", "1X", "1Z", "",   "6C", "6A", "6B", "6X", "6Z", "", "7I", "7Q", "7X",
    "", "8I", "8Q", "8X", "",   "5I", "5Q", "5X", "",   "",   "",   "",   "", "",   "",   ""
};
const char* msm_sig_qzs2[32] = {
    /* QZSS: ref [15] table 3.5-103 */
    "",   "1C", "", "", "", "",   "",   "",   "6S", "6L", "6X", "", "", "",   "2S", "2L",
    "2X", "",   "", "", "", "5I", "5Q", "5X", "",   "",   "",   "", "", "1S", "1L", "1X"
};
const char* msm_sig_sbs2[32] = {
    /* SBAS: ref [13] table 3.5-T+005 */
    "", "1C", "", "", "", "",   "",   "",   "", "", "", "", "", "", "", "",
    "", "",   "", "", "", "5I", "5Q", "5X", "", "", "", "", "", "", "", ""
};
#if 1
const char* msm_sig_cmp2[32] = {
    /* BeiDou: ref [15] table 3.5-106 */
    "", "1I", "1Q", "1X", "1P", "",   "",   "6I", "6Q", "6X", "", "", "", "7I", "7Q", "7X",
    "", "5D", "",   "",   "",   "5D", "5D", "",   "7D", "",   "", "", "", "1D", "1D", ""
};
#else
const char* msm_sig_cmp2[32] = {
    /* BeiDou: ref [15] table 3.5-106 */
    "", "1I", "1Q", "1X", "", "", "", "6I", "6Q", "6X", "", "", "", "7I", "7Q", "7X",
    "", "",   "",   "",   "", "", "", "",   "",   "",   "", "", "", "",   "",   ""
};
#endif
static char* obscodes[] = {
    /* observation code strings */

    "",   "1C", "1P", "1W", "1Y", "1M", "1N", "1S", "1L", "1E", /*  0- 9 */
    "1A", "1B", "1X", "1Z", "2C", "2D", "2S", "2L", "2X", "2P", /* 10-19 */
    "2W", "2Y", "2M", "2N", "5I", "5Q", "5X", "7I", "7Q", "7X", /* 20-29 */
    "6A", "6B", "6C", "6X", "6Z", "6S", "6L", "8L", "8Q", "8X", /* 30-39 */
    "2I", "2Q", "6I", "6Q", "3I", "3Q", "3X", "1I", "1Q", "5A", /* 40-49 */
    "5B", "5C", "9A", "9B", "9C", "9X", "1D", "5D", "7D", "5P"  /* 50-59 */
};
static unsigned char obsfreqs[] = {
    /* 1:L1/E1, 2:L2/B1, 3:L5/E5a/L3, 4:L6/LEX/B3, 5:E5b/B2, 6:E5(a+b), 7:S */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, /*  0- 9 */
    1, 1, 1, 1, 2, 2, 2, 2, 2, 2, /* 10-19 */
    2, 2, 2, 2, 3, 3, 3, 5, 5, 5, /* 20-29 */
    4, 4, 4, 4, 4, 4, 4, 6, 6, 6, /* 30-39 */
    2, 2, 4, 4, 3, 3, 3, 1, 1, 3, /* 40-49 */
    3, 3, 7, 7, 7, 7, 2, 3, 6, 3  /* 50-59 */
};
static char codepris[7][MAXFREQ][16] = {
    /* code priority table */

    /* L1/E1      L2/B1        L5/E5a/L3 L6/LEX/B3 E5b/B2    E5(a+b)  S */
    {"CPYWMNSL", "PYWCMNDSLX",   "IQX", "PYWCMNDSLX", "PYWCMNDSLX",      "",      ""}, /* GPS */
    {      "PC",         "PC",   "IQX",           "",           "",      "",      ""}, /* GLO */
    {   "CABXZ",           "",   "IQX",      "ABCXZ",        "IQX",   "IQX",      ""}, /* GAL */
    {   "CSLXZ",        "SLX",   "IQX",        "SLX",           "",      "",      ""}, /* QZS */
    {       "C",           "",   "IQX",           "",           "",      "",      ""}, /* SBS */
    {   "IQXDP",      "IQXDP", "IQXDP",      "IQXDP",      "IQXDP", "IQXDP", "IQXDP"}, /* BDS */
    {        "",           "",  "ABCX",           "",           "",      "",  "ABCX"}  /* IRN */
};
static double       timeoffset_ = 0.0; /* time offset (s) */
extern void         timeset(gtime_t t) { timeoffset_ += timediff(t, timeget()); }
static unsigned int getbitu(const unsigned char* buff, int pos, int len)
{
    unsigned int bits = 0;
    int          i;
    for (i = pos; i < pos + len; i++)
    {
        bits = (bits << 1) + ((buff[i / 8] >> (7 - i % 8)) & 1u);
    }
    return bits;
}
static int getbits(const unsigned char* buff, int pos, int len)
{
    unsigned int bits = getbitu(buff, pos, len);
    if (len <= 0 || 32 <= len || !(bits & (1u << (len - 1))))
    {
        return (int)bits;
    }
    return (int)(bits | (~0u << len)); /* extend sign */
}

/* loss-of-lock indicator ----------------------------------------------------*/
static int lossoflock(rtcm_t* rtcm, int sat, int freq, int lock)
{
    int lli = (!lock && !rtcm->lock[sat - 1][freq]) || lock < rtcm->lock[sat - 1][freq];
    rtcm->lock[sat - 1][freq] = (unsigned short)lock;
    return lli;
}

extern gtime_t timeget(void)
{
    gtime_t time;
    double  ep[6] = {0};
#ifdef WIN32
    SYSTEMTIME ts;

    GetSystemTime(&ts); /* utc */
    ep[0] = ts.wYear;
    ep[1] = ts.wMonth;
    ep[2] = ts.wDay;
    ep[3] = ts.wHour;
    ep[4] = ts.wMinute;
    ep[5] = ts.wSecond + ts.wMilliseconds * 1E-3;
#else
    struct timeval tv;
    struct tm*     tt;

    if (!gettimeofday(&tv, NULL) && (tt = gmtime(&tv.tv_sec)))
    {
        ep[0] = tt->tm_year + 1900;
        ep[1] = tt->tm_mon + 1;
        ep[2] = tt->tm_mday;
        ep[3] = tt->tm_hour;
        ep[4] = tt->tm_min;
        ep[5] = tt->tm_sec + tv.tv_usec * 1E-6;
    }
#endif
    time = epoch2time(ep);

#ifdef CPUTIME_IN_GPST /* cputime operated in gpst */
    time = gpst2utc(time);
#endif
    return timeadd(time, timeoffset_);
}

/* get sign-magnitude bits ---------------------------------------------------*/
static double getbitg(const unsigned char* buff, int pos, int len)
{
    double value = getbitu(buff, pos + 1, len - 1);
    return getbitu(buff, pos, 1) ? -value : value;
}
extern gtime_t gpst2bdt(gtime_t t) { return timeadd(t, -14.0); }

extern gtime_t bdt2gpst(gtime_t t) { return timeadd(t, 14.0); }

extern gtime_t bdt2time(int week, double sec)
{
    gtime_t t = epoch2time(bdt0);

    if (sec < -1E9 || 1E9 < sec)
    {
        sec = 0.0;
    }
    t.time += (time_t)86400 * 7 * week + (int)sec;
    t.frac = sec - (int)sec;
    return t;
}
extern double time2bdt(gtime_t t, int* week)
{
    gtime_t t0  = epoch2time(bdt0);
    time_t  sec = t.time - t0.time;
    int     w   = (int)(sec / (86400 * 7));

    if (week)
    {
        *week = w;
    }
    return (double)(sec - (double)w * 86400 * 7) + t.frac;
}

/* adjust weekly rollover of gps time ----------------------------------------*/
#if 0
static void adjweek(rtcm_t* rtcm, double tow)
{
    rtcm->time = gpst2time(rtcm->week, tow);
}
#endif
#if 1
extern int           g_week;
extern unsigned char rtcmMode;
static void          adjweek(rtcm_t* rtcm, double tow)
{
    double tow_p;
    int    week;
    if (rtcmMode == 0)
    {
        rtcm->time = utc2gpst(timeget());
    }
    else if (rtcmMode == 1)
    {
        if (rtcm->time.time == 0)
        {
            rtcm->time = gpst2time(g_week, tow);
        }
    }
    else if (rtcmMode == 2)
    {
        if (g_week == 0)
        {
            return;
        }
        if (rtcm->time.time == 0)
        {
            rtcm->time = gpst2time(g_week, tow);
        }
    }

    tow_p = time2gpst(rtcm->time, &week);
    if (tow < tow_p - 302400.0)
    {
        tow += 604800.0;
    }
    else if (tow > tow_p + 302400.0)
    {
        tow -= 604800.0;
    }
    rtcm->time = gpst2time(week, tow);
}
#endif

/* adjust weekly rollover of bdt time ----------------------------------------*/
static int adjbdtweek(int week)
{
    int w;
    (void)time2bdt(gpst2bdt(utc2gpst(timeget())), &w);
    // printf("adjbdtweek=%d week=%d\n",w,week);
    if (w < 1)
    {
        w = 1; /* use 2006/1/1 if time is earlier than 2006/1/1 */
    }
    return week + (w - week + 512) / 1024 * 1024;
}
/* adjust daily rollover of glonass time -------------------------------------*/
static void adjday_glot(rtcm_t* rtcm, double tod)
{
    gtime_t time;
    double  tow, tod_p;
    int     week;
    if (rtcm->time.time == 0)
    {
        return;
    }
    // rtcm->time = utc2gpst(timeget());
    time  = timeadd(gpst2utc(rtcm->time), 10800.0);
    tow   = time2gpst(time, &week);
    tod_p = fmod(tow, 86400.0);
    tow -= tod_p;
    if (tod < tod_p - 43200.0)
    {
        tod += 86400.0;
    }
    else if (tod > tod_p + 43200.0)
    {
        tod -= 86400.0;
    }
    time       = gpst2time(week, tow + tod);
    rtcm->time = utc2gpst(timeadd(time, -10800.0));
}

/* get observation data index ------------------------------------------------*/
static int obsindex(obs_t* obs, gtime_t time, int sat)
{
    int i, j;

    for (i = 0; i < obs->n; i++)
    {
        if (obs->data[i].sat == sat)
        {
            return i; /* field already exists */
        }
    }
    if (i >= MAXOBS)
    {
        return -1; /* overflow */
    }

    /* add new field */
    obs->data[i].time = time;
    obs->data[i].sat  = sat;
    for (j = 0; j < NFREQ; j++)
    {
        obs->data[i].L[j] = obs->data[i].P[j] = 0.0;
        obs->data[i].D[j]                     = 0.0;
        obs->data[i].SNR[j] = obs->data[i].LLI[j] = obs->data[i].code[j] = 0;
    }
    obs->n++;
    return i;
}

/* get signal index ----------------------------------------------------------*/
static void sigindex(
    int sys, const unsigned char* code, const int* freq, int n, const char* opt, int* ind
)
{
    int i, nex, pri, pri_h[8] = {0}, index[8] = {0}, ex[32] = {0};

    /* test code priority */
    for (i = 0; i < n; i++)
    {
        if (!code[i])
        {
            continue;
        }

        if (freq[i] > NFREQ)
        { /* save as extended signal if freq > NFREQ */
            ex[i] = 1;
            continue;
        }
        /* code priority */
        pri = getcodepri(sys, code[i], opt);

        /* select highest priority signal */
        if (pri > pri_h[freq[i] - 1])
        {
            if (index[freq[i] - 1])
            {
                ex[index[freq[i] - 1] - 1] = 1;
            }
            pri_h[freq[i] - 1] = pri;
            index[freq[i] - 1] = i + 1;
        }
        else
        {
            ex[i] = 1;
        }
    }
    /* signal index in obs data */
    for (i = nex = 0; i < n; i++)
    {
        if (ex[i] == 0)
        {
            ind[i] = freq[i] - 1;
        }
        else if (nex < 0)
        {
            ind[i] = NFREQ + nex++;
        }
        else
        { /* no space in obs data */
            // trace(2, "rtcm msm: no space in obs data sys=%d code=%d\n", sys, code[i]);
            ind[i] = -1;
        }
#if 0
        //trace(2, "sig pos: sys=%d code=%d ex=%d ind=%d\n", sys, code[i], ex[i], ind[i]);
#endif
    }
}

/* test station id consistency -----------------------------------------------*/
static int test_staid(rtcm_t* rtcm, int staid)
{
    char* p;
    int   type, id;

    /* test station id option */
    if ((p = strstr(rtcm->opt, "-STA=")) && sscanf(p, "-STA=%d", &id) == 1)
    {
        if (staid != id)
        {
            return 0;
        }
    }
    /* save station id */
    if (rtcm->staid == 0 || rtcm->obsflag)
    {
        rtcm->staid = staid;
    }
    else if (staid != rtcm->staid)
    {
        type = getbitu(rtcm->buff, 24, 12);
        sprintf(rtcm->msg, "rtcm3 %d staid invalid id=%d %d\n", type, staid, rtcm->staid);
        /* reset station id if station id error */
        rtcm->staid = 0;
        return 0;
    }
    return 1;
}
/* get signed 38bit field ----------------------------------------------------*/
static double getbits_38(const unsigned char* buff, int pos)
{
    return (double)getbits(buff, pos, 32) * 64.0 + getbitu(buff, pos + 32, 6);
}

/* decode type 1005: stationary rtk reference station arp --------------------*/
static int decode_type1005(rtcm_t* rtcm)
{
    double rr[3], re[3], pos[3];
    char*  msg;
    int    i = 24 + 12, j, staid, itrf;

    if (i + 140 == rtcm->len * 8)
    {
        staid = getbitu(rtcm->buff, i, 12);
        i += 12;
        itrf = getbitu(rtcm->buff, i, 6);
        i += 6 + 4;
        rr[0] = getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[1] = getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[2] = getbits_38(rtcm->buff, i);
    }
    else
    {
        ////trace(2,"rtcm3 1005 length error: len=%d\n",rtcm->len);
        return -1;
    }
    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        for (j = 0; j < 3; j++)
        {
            re[j] = rr[j] * 0.0001;
        }
        ecef2pos(re, pos);
        sprintf(msg, " staid=%4d pos=%.8f %.8f %.3f", staid, pos[0] * R2D, pos[1] * R2D, pos[2]);
    }
    /* test station id */
    if (!test_staid(rtcm, staid))
    {
        return -1;
    }

    rtcm->sta.deltype = 0; /* xyz */
    for (j = 0; j < 3; j++)
    {
        rtcm->sta.pos[j] = rr[j] * 0.0001;
        rtcm->sta.del[j] = 0.0;
        if (rtcm->index == 0)
        {
            // printf(" %14.4lf %14.4lf %14.4lf\n", rtcm->sta.pos[0], rtcm->sta.pos[1],
            // rtcm->sta.pos[2]);
        }
    }
    rtcm->sta.hgt  = 0.0;
    rtcm->sta.itrf = itrf;
    return 5;
}
#if 1
/* decode type 1019: gps ephemerides -----------------------------------------*/
static int decode_type1019(rtcm_t* rtcm)
{
    eph_t  eph = {0};
    double toc, sqrtA;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, sys = SYS_GPS;
    if (i + 476 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 6);
        i += 6;
        week = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.code = getbitu(rtcm->buff, i, 2);
        i += 2;
        eph.idot = getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.iode = getbitu(rtcm->buff, i, 8);
        i += 8;
        toc = getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.f2 = getbits(rtcm->buff, i, 8) * P2_55;
        i += 8;
        eph.f1 = getbits(rtcm->buff, i, 16) * P2_43;
        i += 16;
        eph.f0 = getbits(rtcm->buff, i, 22) * P2_31;
        i += 22;
        eph.iodc = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.crs = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.cic = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = getbits(rtcm->buff, i, 8) * P2_31;
        i += 8;
        eph.svh = getbitu(rtcm->buff, i, 6);
        i += 6;
        eph.flag = getbitu(rtcm->buff, i, 1);
        i += 1;
        eph.fit = getbitu(rtcm->buff, i, 1) ? 0.0 : 4.0; /* 0:4hr,1:>4hr */
    }
    else
    {
        // trace(2, "rtcm3 1019 length error: len=%d\n", rtcm->len);
        return -1;
    }
    if (prn >= 40)
    {
        sys = SYS_SBS;
        prn += 80;
    }
    // trace(4, "decode_type1019: prn=%d iode=%d toe=%.0f\n", prn, eph.iode, eph.toes);

    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%2d iode=%3d iodc=%3d week=%d toe=%6.0f toc=%6.0f svh=%02X", prn, eph.iode,
            eph.iodc, week, eph.toes, toc, eph.svh
        );
    }
    sat = satno(sys, prn);
    if (!sat)
    {
        // trace(2, "rtcm3 1019 satellite number error: prn=%d\n", prn);
        return -1;
    }
    eph.sat  = sat;
    eph.week = adjgpsweek(week);
    if (rtcmMode == 2)
    {
        if (g_week < eph.week)
        {
            g_week = eph.week;
        }
    }
    //---------------add by leixiaoqiang-----------
    eph.toe = gpst2time(eph.week, eph.toes);
    eph.toc = gpst2time(eph.week, toc);
    eph.ttr = rtcm->time;
    eph.A   = sqrtA * sqrtA;

    if (eph.iode == g_nav.eph[sat - 1].iode)
    {
        return 0; /* unchanged */
    }
    g_nav.eph[sat - 1] = eph;
    rtcm->ephsat       = sat;
    // pthread_mutex_unlock(&mutex_nav);
    return 2;
}

static int decode_type1020(rtcm_t* rtcm)
{
    geph_t geph = {0};
    double tk_h, tk_m, tk_s, toe, tow, tod, tof;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, tb, bn, sys = SYS_GLO;

    if (i + 348 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 6);
        i += 6;
        geph.frq = getbitu(rtcm->buff, i, 5) - 7;
        i += 5 + 2 + 2;
        tk_h = getbitu(rtcm->buff, i, 5);
        i += 5;
        tk_m = getbitu(rtcm->buff, i, 6);
        i += 6;
        tk_s = getbitu(rtcm->buff, i, 1) * 30.0;
        i += 1;
        bn = getbitu(rtcm->buff, i, 1);
        i += 1 + 1;
        tb = getbitu(rtcm->buff, i, 7);
        i += 7;
        geph.vel[0] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[0] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[0] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5;
        geph.vel[1] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[1] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[1] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5;
        geph.vel[2] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[2] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[2] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5 + 1;
        geph.gamn = getbitg(rtcm->buff, i, 11) * P2_40;
        i += 11 + 3;
        geph.taun = getbitg(rtcm->buff, i, 22) * P2_30;
    }
    else
    {
        // trace(2,"rtcm3 1020 length error: len=%d\n",rtcm->len);
        return -1;
    }
    sat = satno(sys, prn);
    if (!sat)
    {
        // trace(2,"rtcm3 1020 satellite number error: prn=%d\n",prn);
        return -1;
    }
    // trace(4,"decode_type1020: prn=%d tk=%02.0f:%02.0f:%02.0f\n",prn,tk_h,tk_m,tk_s);
    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%2d tk=%02.0f:%02.0f:%02.0f frq=%2d bn=%d tb=%d", prn, tk_h, tk_m, tk_s,
            geph.frq, bn, tb
        );
    }
    geph.sat  = sat;
    geph.svh  = bn;
    geph.iode = tb & 0x7F;
    if (rtcm->time.time == 0)
    {
        return 0;
        rtcm->time = utc2gpst(timeget());
    }
    tow = time2gpst(gpst2utc(rtcm->time), &week);
    tod = fmod(tow, 86400.0);
    tow -= tod;
    tof = tk_h * 3600.0 + tk_m * 60.0 + tk_s - 10800.0; /* lt->utc */
    if (tof < tod - 43200.0)
    {
        tof += 86400.0;
    }
    else if (tof > tod + 43200.0)
    {
        tof -= 86400.0;
    }
    geph.tof = utc2gpst(gpst2time(week, tow + tof));
    toe      = tb * 900.0 - 10800.0; /* lt->utc */
    if (toe < tod - 43200.0)
    {
        toe += 86400.0;
    }
    else if (toe > tod + 43200.0)
    {
        toe -= 86400.0;
    }

    geph.toe = utc2gpst(gpst2time(week, tow + toe)); /* utc->gpst */

    if (fabs(timediff(geph.toe, g_nav.geph[prn - 1].toe)) < 1.0 &&
        geph.svh == g_nav.geph[prn - 1].svh)
    {
        // pthread_mutex_unlock(&mutex_nav);
        return 0; /* unchanged */
    }
    g_nav.geph[prn - 1] = geph;
    rtcm->ephsat        = sat;
    return 2;
}
/* decode type 1044: qzss ephemerides (ref [15]) -----------------------------*/
static int decode_type1044(rtcm_t* rtcm)
{
    eph_t  eph = {0};
    double toc, sqrtA;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, sys = SYS_QZS;

    if (i + 473 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 4) + 192;
        i += 4;
        toc = getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.f2 = getbits(rtcm->buff, i, 8) * P2_55;
        i += 8;
        eph.f1 = getbits(rtcm->buff, i, 16) * P2_43;
        i += 16;
        eph.f0 = getbits(rtcm->buff, i, 22) * P2_31;
        i += 22;
        eph.iode = getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.crs = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.cic = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.idot = getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.code = getbitu(rtcm->buff, i, 2);
        i += 2;
        week = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.svh = getbitu(rtcm->buff, i, 6);
        i += 6;
        eph.tgd[0] = getbits(rtcm->buff, i, 8) * P2_31;
        i += 8;
        eph.iodc = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.fit = getbitu(rtcm->buff, i, 1) ? 0.0 : 2.0; /* 0:2hr,1:>2hr */
    }
    else
    {
        // trace(2,"rtcm3 1044 length error: len=%d\n",rtcm->len);
        return -1;
    }
    // trace(4,"decode_type1044: prn=%d iode=%d toe=%.0f\n",prn,eph.iode,eph.toes);

    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%3d iode=%3d iodc=%3d week=%d toe=%6.0f toc=%6.0f svh=%02X", prn, eph.iode,
            eph.iodc, week, eph.toes, toc, eph.svh
        );
    }
    if (!(sat = satno(sys, prn)))
    {
        // trace(2,"rtcm3 1044 satellite number error: prn=%d\n",prn);
        return -1;
    }
    eph.sat  = sat;
    eph.week = adjgpsweek(week);
    eph.toe  = gpst2time(eph.week, eph.toes);
    eph.toc  = gpst2time(eph.week, toc);
    eph.ttr  = rtcm->time;
    eph.A    = sqrtA * sqrtA;

    if (eph.iode == g_nav.eph[sat - 1].iode && eph.iodc == g_nav.eph[sat - 1].iodc)
    {
        return 0; /* unchanged */
    }
    g_nav.eph[sat - 1] = eph;
    rtcm->ephsat       = sat;
    return 2;
}
/* decode type 1045: galileo satellite ephemerides (ref [15]) ----------------*/
static int decode_type1045(rtcm_t* rtcm)
{
    eph_t  eph = {0};
    double toc, sqrtA;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, e5a_hs, e5a_dvs, rsv, sys = SYS_GAL;

    if (i + 484 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 6);
        i += 6;
        week = getbitu(rtcm->buff, i, 12);
        i += 12; /* gst-week */
        eph.iode = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.idot = getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        toc = getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.f2 = getbits(rtcm->buff, i, 6) * P2_59;
        i += 6;
        eph.f1 = getbits(rtcm->buff, i, 21) * P2_46;
        i += 21;
        eph.f0 = getbits(rtcm->buff, i, 31) * P2_34;
        i += 31;
        eph.crs = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.cic = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5a/E1 */
        e5a_hs = getbitu(rtcm->buff, i, 2);
        i += 2; /* OSHS */
        e5a_dvs = getbitu(rtcm->buff, i, 1);
        i += 1; /* OSDVS */
        rsv = getbitu(rtcm->buff, i, 7);
    }
    else
    {
        // trace(2, "rtcm3 1045 length error: len=%d\n", rtcm->len);
        return -1;
    }
    // trace(4, "decode_type1045: prn=%d iode=%d toe=%.0f\n", prn, eph.iode, eph.toes);

    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%2d iode=%3d week=%d toe=%6.0f toc=%6.0f hs=%d dvs=%d rsv=%d", prn, eph.iode,
            week, eph.toes, toc, e5a_hs, e5a_dvs, rsv
        );
    }
    sat = satno(sys, prn);
    if (!sat)
    {
        // trace(2, "rtcm3 1045 satellite number error: prn=%d\n", prn);
        return -1;
    }
    eph.sat  = sat;
    eph.week = week + 1024; /* gal-week = gst-week + 1024 */
    eph.toe  = gpst2time(eph.week, eph.toes);
    eph.toc  = gpst2time(eph.week, toc);
    eph.ttr  = rtcm->time;
    eph.A    = sqrtA * sqrtA;
    eph.svh  = (e5a_hs << 4) + (e5a_dvs << 3);
    eph.code = 2; /* data source = f/nav e5a */

    if (eph.iode == g_nav.eph[sat - 1].iode)
    {
        return 0; /* unchanged */
    }
    g_nav.eph[sat - 1] = eph;
    rtcm->ephsat       = sat;
    return 2;
}

/* decode type 1046: galileo I/NAV satellite ephemerides (ref [17]) ----------*/
static int decode_type1046(rtcm_t* rtcm)
{
    eph_t  eph = {0};
    double toc, sqrtA;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, e5b_hs, e5b_dvs, e1_hs, e1_dvs, sys = SYS_GAL;

    if (i + 492 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 6);
        i += 6;
        week = getbitu(rtcm->buff, i, 12);
        i += 12;
        eph.iode = getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.idot = getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        toc = getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.f2 = getbits(rtcm->buff, i, 6) * P2_59;
        i += 6;
        eph.f1 = getbits(rtcm->buff, i, 21) * P2_46;
        i += 21;
        eph.f0 = getbits(rtcm->buff, i, 31) * P2_34;
        i += 31;
        eph.crs = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.cic = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5a/E1 */
        eph.tgd[1] = getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5b/E1 */
        e5b_hs = getbitu(rtcm->buff, i, 2);
        i += 2; /* E5b OSHS */
        e5b_dvs = getbitu(rtcm->buff, i, 1);
        i += 1; /* E5b OSDVS */
        e1_hs = getbitu(rtcm->buff, i, 2);
        i += 2; /* E1 OSHS */
        e1_dvs = getbitu(rtcm->buff, i, 1);
        i += 1; /* E1 OSDVS */
    }
    else
    {
        // trace(2,"rtcm3 1046 length error: len=%d\n",rtcm->len);
        return -1;
    }
    // trace(4,"decode_type1046: prn=%d iode=%d toe=%.0f\n",prn,eph.iode,eph.toes);

    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%2d iode=%3d week=%d toe=%6.0f toc=%6.0f hs=%d %d dvs=%d %d", prn, eph.iode,
            week, eph.toes, toc, e5b_hs, e1_hs, e5b_dvs, e1_dvs
        );
    }
    if (!(sat = satno(sys, prn)))
    {
        // trace(2,"rtcm3 1046 satellite number error: prn=%d\n",prn);
        return -1;
    }
    if (strstr(rtcm->opt, "-GALFNAV"))
    {
        return 0;
    }
    eph.sat  = sat;
    eph.week = week + 1024; /* gal-week = gst-week + 1024 */
    eph.toe  = gpst2time(eph.week, eph.toes);
    eph.toc  = gpst2time(eph.week, toc);
    eph.ttr  = rtcm->time;
    eph.A    = sqrtA * sqrtA;
    eph.svh  = (e5b_hs << 7) + (e5b_dvs << 6) + (e1_hs << 1) + (e1_dvs << 0);
    eph.code = (1 << 0) | (1 << 9); /* data source = i/nav e1b + af0-2,toc,sisa for e5b-e1 */

    if (eph.iode == g_nav.eph[sat - 1].iode)
    {
        // pthread_mutex_unlock(&mutex_nav);
        return 0; /* unchanged */
    }
    g_nav.eph[sat - 1] = eph;
    rtcm->ephsat       = sat;
    return 2;
}

/* decode type 63: beidou ephemerides (rtcm draft) ---------------------------*/
static int decode_type63(rtcm_t* rtcm)
{
    eph_t  eph = {0};
    double toc, sqrtA;
    char*  msg;
    int    i = 24 + 12, prn, sat, week, sys = SYS_BDS;

    if (i + 499 <= rtcm->len * 8)
    {
        prn = getbitu(rtcm->buff, i, 6);
        i += 6;
        week = getbitu(rtcm->buff, i, 13);
        i += 13;
        eph.sva = getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.idot = getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.iode = getbitu(rtcm->buff, i, 5);
        i += 5; /* AODE */
        toc = getbitu(rtcm->buff, i, 17) * 8.0;
        i += 17;
        eph.f2 = getbits(rtcm->buff, i, 11) * P2_66;
        i += 11;
        eph.f1 = getbits(rtcm->buff, i, 22) * P2_50;
        i += 22;
        eph.f0 = getbits(rtcm->buff, i, 24) * P2_33;
        i += 24;
        eph.iodc = getbitu(rtcm->buff, i, 5);
        i += 5; /* AODC */
        eph.crs = getbits(rtcm->buff, i, 18) * P2_6;
        i += 18;
        eph.deln = getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.e = getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        sqrtA = getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = getbitu(rtcm->buff, i, 17) * 8.0;
        i += 17;
        eph.cic = getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.OMG0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.i0 = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = getbits(rtcm->buff, i, 18) * P2_6;
        i += 18;
        eph.omg = getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = getbits(rtcm->buff, i, 10) * 1E-10;
        i += 10;
        eph.tgd[1] = getbits(rtcm->buff, i, 10) * 1E-10;
        i += 10;
        eph.svh = getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    else
    {
        // trace(2,"rtcm3 63 length error: len=%d\n",rtcm->len);
        return -1;
    }
    // trace(4,"decode_type63: prn=%d iode=%d toe=%.0f\n",prn,eph.iode,eph.toes);
    // return 0;
    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " prn=%2d iode=%3d iodc=%3d week=%d toe=%6.0f toc=%6.0f svh=%02X", prn, eph.iode,
            eph.iodc, week, eph.toes, toc, eph.svh
        );
    }
    sat = satno(sys, prn);
    if (!sat)
    {
        // trace(2,"rtcm3 63 satellite number error: prn=%d\n",prn);
        return -1;
    }
    eph.sat = sat;
    // eph.week=adjbdtweek(week);
    eph.week = week;
    eph.toe  = bdt2gpst(bdt2time(eph.week, eph.toes)); /* bdt -> gpst */
    eph.toc  = bdt2gpst(bdt2time(eph.week, toc));      /* bdt -> gpst */

    if (rtcmMode == 2)
    {
        if (g_week < week + 1356)
        {
            g_week = week + 1356;
        }
    }
    // time2gpst(eph.toe, &rtcm->week);
    // printf("sat=%d,week=%d,eph week=%d,rtcm week=%d\n",sat,week,eph.week,rtcm->week);
    eph.ttr = rtcm->time;
    eph.A   = sqrtA * sqrtA;
    if (timediff(eph.toe, g_nav.eph[sat - 1].toe) == 0.0 && eph.iode == g_nav.eph[sat - 1].iode &&
        eph.iodc == g_nav.eph[sat - 1].iodc)
    {
        return 0; /* unchanged */
    }
    g_nav.eph[sat - 1] = eph;
    rtcm->ephsat       = sat;
    return 2;
}
#endif
/* decode type msm message header --------------------------------------------*/
static int decodeMsmHead(rtcm_t* rtcm, int sys, int* sync, int* iod, msm_h_t* h, int* hsize)
{
    msm_h_t h0 = {0};
    double  tow, tod;
    char*   msg;
    int     i = 24, j, dow, mask, staid, type, ncell = 0;

    type = getbitu(rtcm->buff, i, 12);
    i += 12;

    *h = h0;
    if (i + 157 <= rtcm->len * 8)
    {
        staid = getbitu(rtcm->buff, i, 12);
        i += 12;

        if (sys == SYS_GLO)
        {
            dow = getbitu(rtcm->buff, i, 3);
            i += 3;
            tod = getbitu(rtcm->buff, i, 27) * 0.001;
            i += 27;
            adjday_glot(rtcm, tod);
        }
        else if (sys == SYS_BDS)
        {
            tow = getbitu(rtcm->buff, i, 30) * 0.001;
            i += 30;
            tow += 14.0; /* BDT -> GPST */
            adjweek(rtcm, tow);
        }
        else
        {
            tow = getbitu(rtcm->buff, i, 30) * 0.001;
            i += 30;
            adjweek(rtcm, tow);
        }
        *sync = getbitu(rtcm->buff, i, 1);
        i += 1;
        *iod = getbitu(rtcm->buff, i, 3);
        i += 3;
        h->time_s = getbitu(rtcm->buff, i, 7);
        i += 7;
        h->clk_str = getbitu(rtcm->buff, i, 2);
        i += 2;
        h->clk_ext = getbitu(rtcm->buff, i, 2);
        i += 2;
        h->smooth = getbitu(rtcm->buff, i, 1);
        i += 1;
        h->tint_s = getbitu(rtcm->buff, i, 3);
        i += 3;
        for (j = 1; j <= 64; j++)
        {
            mask = getbitu(rtcm->buff, i, 1);
            i += 1;
            if (mask)
            {
                h->sats[h->nsat++] = j;
            }
        }
        for (j = 1; j <= 32; j++)
        {
            mask = getbitu(rtcm->buff, i, 1);
            i += 1;
            if (mask)
            {
                h->sigs[h->nsig++] = j;
            }
        }
    }
    else
    {
        //    trace(2, "rtcm3 %d length error: len=%d\n", type, rtcm->len);
        return -1;
    }
    /* test station id */
    if (!test_staid(rtcm, staid))
    {
        return -1;
    }

    if (h->nsat * h->nsig > 64)
    {
        //    trace(2, "rtcm3 %d number of sats and sigs error: nsat=%d nsig=%d\n",
        //        type, h->nsat, h->nsig);
        return -1;
    }
    if (i + h->nsat * h->nsig > rtcm->len * 8)
    {
        //    trace(2, "rtcm3 %d length error: len=%d nsat=%d nsig=%d\n", type,
        //        rtcm->len, h->nsat, h->nsig);
        return -1;
    }
    for (j = 0; j < h->nsat * h->nsig; j++)
    {
        h->cellmask[j] = getbitu(rtcm->buff, i, 1);
        i += 1;
        if (h->cellmask[j])
        {
            ncell++;
        }
    }
    *hsize = i;

    //    trace(4, "decode_head_msm: time=%s sys=%d staid=%d nsat=%d nsig=%d sync=%d iod=%d
    //    ncell=%d\n",
    //        time_str(rtcm->time, 2), sys, staid, h->nsat, h->nsig, *sync, *iod, ncell);

    if (rtcm->outtype)
    {
        msg = rtcm->msgtype + strlen(rtcm->msgtype);
        sprintf(
            msg, " staid=%4d %s nsat=%2d nsig=%2d iod=%2d ncell=%2d sync=%d", staid,
            time_str(rtcm->time, 2), h->nsat, h->nsig, *iod, ncell, *sync
        );
    }
    return ncell;
}

static double satwavelen(unsigned char sat, int frq, const nav_t* nav)
{
    const double freq_glo[] = {FREQ1_GLO, FREQ2_GLO};
    const double dfrq_glo[] = {DFRQ1_GLO, DFRQ2_GLO};
    int          i, sys = satsys(sat, NULL);

    if (sys == SYS_GLO)
    {
        if (0 <= frq && frq <= 1)
        { /* L1,L2 */
            for (i = 0; i < nav->ng; i++)
            {
                if (nav->geph[i].sat != sat)
                {
                    continue;
                }
                return CLIGHT / (freq_glo[frq] + dfrq_glo[frq] * nav->geph[i].frq);
            }
        }
        else if (frq == 2)
        { /* L3 */
            return CLIGHT / FREQ3_GLO;
        }
    }
    else if (sys == SYS_BDS)
    {
        if (frq == 0)
        {
            return CLIGHT / FREQ1_CMP; /* B1 */
        }
        else if (frq == 1)
        {
            return CLIGHT / FREQ2_CMP; /* B2 */
        }
        else if (frq == 2)
        {
            return CLIGHT / FREQB2a_CMP; /* B3 */
        }
        else if (frq == 3)
        {
            return CLIGHT / FREQB2b_CMP; /* B1C */
        }
        else if (frq == 4)
        {
            return CLIGHT / FREQB1C_CMP; /* B2a */
        }
        else if (frq == 5)
        {
            return CLIGHT / FREQ3_CMP; /* B2b */
        }
    }
    else if (sys == SYS_GAL)
    {
        if (frq == 0)
        {
            return CLIGHT / FREQ1; /* E1 */
        }
        else if (frq == 1)
        {
            return CLIGHT / FREQ7; /* E5b */
        }
        else if (frq == 2)
        {
            return CLIGHT / FREQ5; /* E5a */
        }
        else if (frq == 3)
        {
            return CLIGHT / FREQ6; /* E6 */
        }
        else if (frq == 5)
        {
            return CLIGHT / FREQ8; /* E5ab */
        }
    }
    else
    { /* GPS,QZS */
        if (frq == 0)
        {
            return CLIGHT / FREQ1; /* L1 */
        }
        else if (frq == 1)
        {
            return CLIGHT / FREQ2; /* L2 */
        }
        else if (frq == 2)
        {
            return CLIGHT / FREQ5; /* L5 */
        }
        else if (frq == 3)
        {
            return CLIGHT / FREQ6; /* L6/LEX */
        }
        else if (frq == 6)
        {
            return CLIGHT / FREQ9; /* S */
        }
    }
    return 0.0;
}

/* save obs data in msm message ----------------------------------------------*/
static void saveMsmObs(
    rtcm_t* rtcm, int sys, msm_h_t* h, const double* r, const double* pr, const double* cp,
    const double* rr, const double* rrf, const double* cnr, const int* lock, const int* ex,
    const int* half
)
{
    // const char* sig[32];
    // double tt, wl;
    // unsigned char code[32], prn;
    // char* msm_type = "", * q = NULL;
    // int i, j, k, type, sat, index = 0, freq[32], ind[32];

    // type = getbitu(rtcm->buff, 24, 12);
    ////char buff[128]={0};
    //  /* id to signal */
    // for (i = 0; i < h->nsig; i++) {
    //    switch (sys) {
    //    case SYS_GPS: sig[i] = msm_sig_gps2[h->sigs[i] - 1]; break;
    //    case SYS_GLO: sig[i] = msm_sig_glo2[h->sigs[i] - 1]; break;
    //    case SYS_GAL: sig[i] = msm_sig_gal2[h->sigs[i] - 1]; break;
    //    case SYS_QZS: sig[i] = msm_sig_qzs2[h->sigs[i] - 1]; break;
    //    case SYS_SBS: sig[i] = msm_sig_sbs2[h->sigs[i] - 1]; break;
    //    case SYS_BDS: sig[i] = msm_sig_cmp2[h->sigs[i] - 1]; break;
    //    default: sig[i] = ""; break;
    //    }
    //    /* signal to rinex obs type */
    //    code[i] = obs2code(sig[i], freq + i);
    //    if (sys == SYS_GAL) {
    //        if (code[i] == 25 || code[i] == 24)  freq[i] = 3;
    //        if (code[i] == 28 || code[i] == 27)  freq[i] = 2;
    //        //printf("%s %d %d\n",sig[i],code[i],freq[i]);
    //    }

    //    /* freqency index for beidou */
    //    if (sys == SYS_BDS) {
    //        if (freq[i] == 5) freq[i] = 2; /* B2 */
    //        else if (freq[i] == 4) freq[i] = 3; /* B3 */
    //        else if (code[i] == 56) freq[i] = 4; /* B1C */
    //        else if (code[i] == 57) freq[i] = 5; /* B2a*/
    //        else if (code[i] == 58) freq[i] = 6; /* B2b*/
    //    }
    //    //if (sys == SYS_CMP && sig[i][1] == 'P') {
    //    //    freq[i] = 4; /* B1C */
    //    //    code[i]=56;
    //    //}
    //    //else if (sys == SYS_GAL) {
    //    //    if (freq[i] == 5) freq[i] = 2; /* E5b */
    //    //}
    //    if (freq[i] > NFREQ) {
    //        code[i] = CODE_NONE;
    //        trace(1, "rtcm3 %d: freq=%d sys=%d signals=%s\n", type, freq[i], sys, msm_type);

    //        //sprintf(buff,"rtcm3 %d: freq=%d sys=%d\n", type,freq[i],sys);
    //      //gnss_string_output((unsigned char *)buff, strlen(buff), OUT_COM_1, PROTOCAL_NMEA);
    //    }
    //    if (code[i] != CODE_NONE) {
    //        if (q) q += sprintf(q, "L%s%s", sig[i], i < h->nsig - 1 ? "," : "");
    //    }
    //    else {
    //        if (q) q += sprintf(q, "(%d)%s", h->sigs[i], i < h->nsig - 1 ? "," : "");
    //        //trace(2, "rtcm3 %d: unknown signal id=%2d\n", type, h->sigs[i]);

    //        //sprintf(buff,"rtcm3 %d: unknown sig[i]=%s signal id=%2d\n", type,sig[i],
    //        h->sigs[i]);
    //      //gnss_string_output((unsigned char *)buff, strlen(buff), OUT_COM_1, PROTOCAL_NMEA);
    //    }
    //}
    ///* get signal index */
    // sigindex(sys, code, freq, h->nsig, rtcm->opt, ind);

    ////for(i=0;i<h->nsig;i++){
    //      //sprintf(buff,"code=%d f=%d ind=%d\n",code[i],freq[i],ind[i]);
    //      //gnss_string_output((unsigned char *)buff, strlen(buff), OUT_COM_1, PROTOCAL_NMEA);
    //  //}

    // for (i = j = 0; i < h->nsat; i++) {

    //    prn = h->sats[i];
    //    if (sys == SYS_QZS) prn += MINPRNQZS - 1;
    //    else if (sys == SYS_SBS) prn += MINPRNSBS - 1;
    //    sat = satno(sys, prn);
    //    if (sat == 255) {
    //        trace(4, "rtcm3 %d satellite error: prn=%d\n", type, prn);
    //        continue;
    //    }
    //    else {
    //        tt = timediff(rtcm->obs.data[0].time, rtcm->time);
    //        if (rtcm->obsflag || fabs(tt) > 1E-9) {
    //            rtcm->obs.n = rtcm->obsflag = 0;
    //        }
    //        index = obsindex(&rtcm->obs, rtcm->time, sat);
    //    }
    //    for (k = 0; k < h->nsig; k++) {
    //        if (!h->cellmask[k + i * h->nsig]) continue;
    //        if (sat && index >= 0 && ind[k] >= 0 && ind[k] < NFREQ) {
    //            wl = satwavelen(sat, freq[k] - 1, &g_nav);
    //            /* glonass wave length by extended info */
    //            //if (sys == SYS_BDS&&ex&&ex[i] <= 13) {
    //            if (wl == 0.0) continue;
    //            if (sys == SYS_GLO) {
    //                wl = g_gloLam[prn - 1][ind[k]];
    //            }
    //            if (sys == SYS_GPS) {
    //                //printf("1sat=%d gps ind[k]=%d freq=%d %lf
    //                %lf\n",sat,ind[k],freq[k]-1,g_gpsLam[ind[k]],wl); if (g_gpsLam[ind[k]] == 0.0)
    //                    g_gpsLam[ind[k]] = wl;
    //                else
    //                    wl = g_gpsLam[ind[k]];
    //            }
    //            else if (sys == SYS_GAL) {
    //                //printf("sat=%d gal ind[k]=%d freq=%d %lf
    //                %lf\n",sat,ind[k],freq[k]-1,g_galLam[ind[k]],wl); if (g_galLam[ind[k]] == 0.0)
    //                    g_galLam[ind[k]] = wl;
    //                else
    //                    wl = g_galLam[ind[k]];
    //            }
    //            else if (sys == SYS_BDS) {
    //                if (g_bdsLam[ind[k]] == 0.0)
    //                    g_bdsLam[ind[k]] = wl;
    //                else
    //                    wl = g_bdsLam[ind[k]];
    //            }

    //            /* pseudorange (m) */
    //            if (r[i] != 0.0 && pr[j] > -1E12) {
    //                rtcm->obs.data[index].P[ind[k]] = r[i] + pr[j];
    //            }
    //            if (sys == SYS_BDS && prn == 21 && ind[k]==0) {
    //                trace(4, "21 p=%lf r=%lf pr=%lf\n", rtcm->obs.data[index].P[ind[k]], r[i],
    //                pr[i]);
    //            }
    //            if (sys == SYS_BDS && prn == 22 && ind[k] == 0) {
    //                trace(4, "24 p=%lf r=%lf pr=%lf\n", rtcm->obs.data[index].P[ind[k]], r[i],
    //                pr[i]);
    //            }
    //            /* carrier-phase (cycle) */
    //            if (r[i] != 0.0 && cp[j] > -1E12 && wl > 0.0) {
    //                rtcm->obs.data[index].L[ind[k]] = (r[i] + cp[j]) / wl;
    //            }
    //            /* doppler (hz) */
    //            if (rr && rrf && rrf[j] > -1E12 && wl > 0.0) {
    //                rtcm->obs.data[index].D[ind[k]] = (float)(-(rr[i] + rrf[j]) / wl);
    //            }
    //            else
    //                rtcm->obs.data[index].D[ind[k]] = 0.0;

    //            rtcm->obs.data[index].LLI[ind[k]] =
    //                lossoflock(rtcm, sat, ind[k], lock[j]) + (half[j] ? 3 : 0);
    //            rtcm->obs.data[index].SNR[ind[k]] = (unsigned char)(cnr[j] * 4.0);
    //            rtcm->obs.data[index].code[ind[k]] = code[k];
    //        }
    //        j++;
    //    }
    //    //for(k=0;k<NFREQ;k++)
    //    //    printf("sat=%d P[%d]=%lf\n",rtcm->obs.data[index].sat,k,rtcm->obs.data[index].P[k]);
    //}
    const char*   sig[32];
    double        tt, wl, ep[6];
    unsigned char code[32];
    char *        msm_type = "", *q = NULL;
    int           i, j, k, type, prn, sat, fn, index = 0, freq[32], ind[32];

    type = getbitu(rtcm->buff, 24, 12);

    /* id to signal */
    for (i = 0; i < h->nsig; i++)
    {
        switch (sys)
        {
            case SYS_GPS:
                sig[i] = msm_sig_gps2[h->sigs[i] - 1];
                break;
            case SYS_GLO:
                sig[i] = msm_sig_glo2[h->sigs[i] - 1];
                break;
            case SYS_GAL:
                sig[i] = msm_sig_gal2[h->sigs[i] - 1];
                break;
            case SYS_QZS:
                sig[i] = msm_sig_qzs2[h->sigs[i] - 1];
                break;
            case SYS_SBS:
                sig[i] = msm_sig_sbs2[h->sigs[i] - 1];
                break;
            case SYS_BDS:
                sig[i] = msm_sig_cmp2[h->sigs[i] - 1];
                break;
            default:
                sig[i] = "";
                break;
        }
        /* signal to rinex obs type */
        code[i] = obs2code(sig[i], freq + i);

        /* freqency index for beidou and galileo */
        if (sys == SYS_BDS)
        {
            // if (freq[i] == 5) freq[i] = 2; /* B2 */
            // else if (freq[i] == 4) freq[i] = 3; /* B3 */
            // else if (freq[i] == 2) freq[i] = 4; /* B1c */
            // else if (freq[i] == 3) freq[i] = 5; /* B2a*/
            // else if (freq[i] == 6) freq[i] = 6; /* B2b */
            if (freq[i] == 5)
            {
                freq[i] = 2; /* B2 */
            }
            else if (freq[i] == 4)
            {
                freq[i] = 6; /* B3 */
            }
            else if (freq[i] == 2)
            {
                freq[i] = 5; /* B1c */
            }
            else if (freq[i] == 3)
            {
                freq[i] = 3; /* B2a*/
            }
            else if (freq[i] == 6)
            {
                freq[i] = 4; /* B2b */
            }
            // printf("i=%d\n", h->sigs[i]);  //2 14 23 25
        }
        if (sys == SYS_BDS && sig[i][1] == 'P')
        {
            freq[i] = 4; /* B1C */
            code[i] = 56;
        }
        else if (sys == SYS_GAL)
        {
            if (freq[i] == 5)
            {
                freq[i] = 2; /* E5b */
            }
        }
        if (code[i] != CODE_NONE)
        {
            if (q)
            {
                q += sprintf(q, "L%s%s", sig[i], i < h->nsig - 1 ? "," : "");
            }
        }
        else
        {
            if (q)
            {
                q += sprintf(q, "(%d)%s", h->sigs[i], i < h->nsig - 1 ? "," : "");
            }

            // trace(2,"rtcm3 %d: unknown signal id=%2d\n",type,h->sigs[i]);
        }
    }
    // trace(3,"rtcm3 %d: signals=%s\n",type,msm_type);

    /* get signal index */
    sigindex(sys, code, freq, h->nsig, rtcm->opt, ind);

    for (i = j = 0; i < h->nsat; i++)
    {
        prn = h->sats[i];
        if (sys == SYS_QZS)
        {
            prn += MINPRNQZS - 1;
        }
        else if (sys == SYS_SBS)
        {
            prn += MINPRNSBS - 1;
        }

        if ((sat = satno(sys, prn)))
        {
            if (sat == 255)
            {
                continue;
            }
            tt = timediff(rtcm->obs.data[0].time, rtcm->time);
            if (rtcm->obsflag || fabs(tt) > 1E-9)
            {
                rtcm->obs.n = rtcm->obsflag = 0;
            }
            index = obsindex(&rtcm->obs, rtcm->time, sat);
        }
        else
        {
            // trace(2,"rtcm3 %d satellite error: prn=%d\n",type,prn);
        }

        for (k = 0; k < h->nsig; k++)
        {
            if (!h->cellmask[k + i * h->nsig])
            {
                continue;
            }

            if (sat && index >= 0 && ind[k] >= 0)
            {
                /* satellite carrier wave length */
                wl = satwavelen(sat, freq[k] - 1, &g_nav);

                if (wl == 0.0)
                {
                    continue;
                }
                if (sys == SYS_GLO)
                {
                    wl = g_gloLam[prn - 1][ind[k]];
                }
                if (sys == SYS_GPS || sys == SYS_QZS)
                {
                    // printf("1sat=%d gps ind[k]=%d freq=%d %lf
                    // %lf\n",sat,ind[k],freq[k]-1,g_gpsLam[ind[k]],wl);
                    if (g_gpsLam[ind[k]] == 0.0)
                    {
                        g_gpsLam[ind[k]] = wl;
                    }
                    else
                    {
                        wl = g_gpsLam[ind[k]];
                    }
                }
                else if (sys == SYS_GAL)
                {
                    // printf("sat=%d gal ind[k]=%d freq=%d %lf
                    // %lf\n",sat,ind[k],freq[k]-1,g_galLam[ind[k]],wl);
                    if (g_galLam[ind[k]] == 0.0)
                    {
                        g_galLam[ind[k]] = wl;
                    }
                    else
                    {
                        wl = g_galLam[ind[k]];
                    }
                }
                else if (sys == SYS_BDS)
                {
                    if (g_bdsLam[ind[k]] == 0.0)
                    {
                        g_bdsLam[ind[k]] = wl;
                    }
                    else
                    {
                        wl = g_bdsLam[ind[k]];
                    }
                }
                time2epoch(rtcm->obs.data[index].time, ep);

                // printf("%.0f %.0f %.0f %.0f %.0f %.0f\n",ep[0],ep[1],ep[2],ep[3],ep[4],ep[5]);
                /* glonass wave length by extended info */
                if (sys == SYS_GLO && ex && ex[i] <= 13)
                {
                    fn = ex[i] - 7;
                    wl = CLIGHT / ((freq[k] == 2 ? FREQ2_GLO : FREQ1_GLO) +
                                   (freq[k] == 2 ? DFRQ2_GLO : DFRQ1_GLO) * fn);
                }
                /* pseudorange (m) */
                if (r[i] != 0.0 && pr[j] > -1E12)
                {
                    rtcm->obs.data[index].P[ind[k]] = r[i] + pr[j];
                }
                // if (ROUND(rtcm->obs.data[index].P[ind[k]]) == 25300153) {
                //     double ri = r[i];
                //     double prj = pr[j];
                //     double p = rtcm->obs.data[index].P[ind[k]];
                //     printf("test\n");
                // }
                // double cpj = cp[j];
                /* carrier-phase (cycle) */
                if (r[i] != 0.0 && cp[j] > -1E12 && wl > 0.0)
                {
                    rtcm->obs.data[index].L[ind[k]] = (r[i] + cp[j]) / wl;
                }
                /* doppler (hz) */
                if (rr && rrf && rrf[j] > -1E12 && wl > 0.0)
                {
                    rtcm->obs.data[index].D[ind[k]] = (float)(-(rr[i] + rrf[j]) / wl);
                }
                int halfj                         = half[j];
                rtcm->obs.data[index].LCK[ind[k]] = lock[j];
                rtcm->obs.data[index].LLI[ind[k]] =
                    lossoflock(rtcm, sat, ind[k], lock[j]) + (half[j] ? 3 : 0);
                rtcm->obs.data[index].SNR[ind[k]]  = (unsigned char)(cnr[j] * 4.0);
                rtcm->obs.data[index].code[ind[k]] = code[k];
            }
            j++;
        }
    }
}
/* decode msm 4: full pseudorange and phaserange plus cnr --------------------*/
static int decode_msm4(rtcm_t* rtcm, int sys)
{
    msm_h_t h = {0};
    double  r[64], pr[64], cp[64], cnr[64];
    int     i, j, type, sync, iod, ncell, rng, rng_m, prv, cpv, lock[64], half[64];
    type = getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decodeMsmHead(rtcm, sys, &sync, &iod, &h, &i)) < 0)
    {
        return -1;
    }

    if (i + h.nsat * 18 + ncell * 48 > rtcm->len * 8)
    {
        // trace(2, "rtcm3 %d length error: nsat=%d ncell=%d len=%d\n", type, h.nsat,ncell,
        // rtcm->len);
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = 0.0;
    }
    for (j = 0; j < ncell; j++)
    {
        pr[j] = cp[j] = -1E16;
    }

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
        {
            r[j] = rng * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
        {
            r[j] += rng_m * P2_10 * RANGE_MS;
        }
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = getbits(rtcm->buff, i, 15);
        i += 15;
        if (prv != -16384)
        {
            pr[j] = prv * P2_24 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = getbits(rtcm->buff, i, 22);
        i += 22;
        if (cpv != -2097152)
        {
            cp[j] = cpv * P2_29 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = getbitu(rtcm->buff, i, 6) * 1.0;
        i += 6;
    }
    /* save obs data in msm message */
    saveMsmObs(rtcm, sys, &h, r, pr, cp, NULL, NULL, cnr, lock, NULL, half);

    rtcm->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode MSM 5: full pseudorange, phaserange, phaserangerate and CNR --------*/
static int decode_msm5(rtcm_t* rtcm, int sys)
{
    msm_h_t h = {0};
    double  r[64], rr[64], pr[64], cp[64], rrf[64], cnr[64];
    int     i, j, type, sync, iod, ncell, rng, rng_m, rate, prv, cpv, rrv, lock[64];
    int     ex[64], half[64];

    type = getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decodeMsmHead(rtcm, sys, &sync, &iod, &h, &i)) < 0)
    {
        return -1;
    }

    if (i + h.nsat * 36 + ncell * 63 > rtcm->len * 8)
    {
        trace(
            0x01, "rtcm3 %d length error: nsat=%d ncell=%d len=%d\n", type, h.nsat, ncell, rtcm->len
        );
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = rr[j] = 0.0;
        ex[j]        = 15;
    }
    for (j = 0; j < ncell; j++)
    {
        pr[j] = cp[j] = rrf[j] = -1E16;
    }

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
        {
            r[j] = rng * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    { /* extended info */
        ex[j] = getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
        {
            r[j] += rng_m * P2_10 * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    { /* phaserangerate */
        rate = getbits(rtcm->buff, i, 14);
        i += 14;
        if (rate != -8192)
        {
            rr[j] = rate * 1.0;
        }
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = getbits(rtcm->buff, i, 15);
        i += 15;
        if (prv != -16384)
        {
            pr[j] = prv * P2_24 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = getbits(rtcm->buff, i, 22);
        i += 22;
        if (cpv != -2097152)
        {
            cp[j] = cpv * P2_29 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = getbitu(rtcm->buff, i, 6) * 1.0;
        i += 6;
    }
    for (j = 0; j < ncell; j++)
    { /* phaserangerate */
        rrv = getbits(rtcm->buff, i, 15);
        i += 15;
        if (rrv != -16384)
        {
            rrf[j] = rrv * 0.0001;
        }
    }
    /* save obs data in msm message */
    saveMsmObs(rtcm, sys, &h, r, pr, cp, rr, rrf, cnr, lock, ex, half);

    rtcm->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode MSM 6: full pseudorange and phaserange plus CNR (high-res) ---------*/
static int decode_msm6(rtcm_t* rtcm, int sys)
{
    msm_h_t h = {0};
    double  r[64], pr[64], cp[64], cnr[64];
    int     i, j, type, sync, iod, ncell, rng, rng_m, prv, cpv, lock[64], half[64];

    type = getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decodeMsmHead(rtcm, sys, &sync, &iod, &h, &i)) < 0)
    {
        return -1;
    }

    if (i + h.nsat * 18 + ncell * 65 > rtcm->len * 8)
    {
        trace(
            0x01, "rtcm3 %d length error: nsat=%d ncell=%d len=%d\n", type, h.nsat, ncell, rtcm->len
        );
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = 0.0;
    }
    for (j = 0; j < ncell; j++)
    {
        pr[j] = cp[j] = -1E16;
    }

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
        {
            r[j] = rng * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
        {
            r[j] += rng_m * P2_10 * RANGE_MS;
        }
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = getbits(rtcm->buff, i, 20);
        i += 20;
        if (prv != -524288)
        {
            pr[j] = prv * P2_29 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = getbits(rtcm->buff, i, 24);
        i += 24;
        if (cpv != -8388608)
        {
            cp[j] = cpv * P2_31 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = getbitu(rtcm->buff, i, 10);
        i += 10;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = getbitu(rtcm->buff, i, 10) * 0.0625;
        i += 10;
    }
    /* save obs data in msm message */
    saveMsmObs(rtcm, sys, &h, r, pr, cp, NULL, NULL, cnr, lock, NULL, half);

    rtcm->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode msm 7: full pseudorange, phaserange, phaserangerate and cnr (h-res) */
static int decode_msm7(rtcm_t* rtcm, int sys)
{
    msm_h_t h = {0};
    double  r[64], rr[64], pr[64], cp[64], rrf[64], cnr[64];
    int     i, j, type, sync, iod, ncell, rng, rng_m, rate, prv, cpv, rrv, lock[64];
    int     ex[64], half[64];
    type = getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decodeMsmHead(rtcm, sys, &sync, &iod, &h, &i)) < 0)
    {
        return -1;
    }

    if (i + h.nsat * 36 + ncell * 80 > rtcm->len * 8)
    {
        trace(
            0x01, "rtcm3 %d length error: nsat=%d ncell=%d len=%d\n", type, h.nsat, ncell, rtcm->len
        );
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = rr[j] = 0.0;
        ex[j]        = 15;
    }
    for (j = 0; j < ncell; j++)
    {
        pr[j] = cp[j] = rrf[j] = -1E16;
    }

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
        {
            r[j] = rng * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    { /* extended info */
        ex[j] = getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
        {
            r[j] += rng_m * P2_10 * RANGE_MS;
        }
    }
    for (j = 0; j < h.nsat; j++)
    { /* phaserangerate */
        rate = getbits(rtcm->buff, i, 14);
        i += 14;
        if (rate != -8192)
        {
            rr[j] = rate * 1.0;
        }
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = getbits(rtcm->buff, i, 20);
        i += 20;
        if (prv != -524288)
        {
            pr[j] = prv * P2_29 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = getbits(rtcm->buff, i, 24);
        i += 24;
        if (cpv != -8388608)
        {
            cp[j] = cpv * P2_31 * RANGE_MS;
        }
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = getbitu(rtcm->buff, i, 10);
        i += 10;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle amiguity */
        half[j] = getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = getbitu(rtcm->buff, i, 10) * 0.0625;
        i += 10;
    }
    for (j = 0; j < ncell; j++)
    { /* phaserangerate */
        rrv = getbits(rtcm->buff, i, 15);
        i += 15;
        if (rrv != -16384)
        {
            rrf[j] = rrv * 0.0001;
        }
    }
    /* save obs data in msm message */
    saveMsmObs(rtcm, sys, &h, r, pr, cp, rr, rrf, cnr, lock, ex, half);

    rtcm->obsflag = !sync;
    return sync ? 0 : 1;
}

extern int decode_rtcm3(rtcm_t* rtcm)
{
    int     ret = 0, type = getbitu(rtcm->buff, 24, 12);
    gtime_t time;
    time = timeget();
    trace(0x04, "decode_rtcm3:rcv=%d len=%3d type=%d\n", rtcm->rcv, rtcm->len, type);

    switch (type)
    {
        case 1005:
            ret = decode_type1005(rtcm);
            break;

        case 1019:
            ret = decode_type1019(rtcm);
            break;
        case 1020:
            ret = decode_type1020(rtcm);
            break;
        case 1044:
            ret = decode_type1044(rtcm);
            break;
        case 1045:
            ret = decode_type1045(rtcm);
            break;
        case 1046:
            ret = decode_type1046(rtcm);
            break; /* extension for IGS MGEX */
        case 1042:
            ret = decode_type63(rtcm);
            break; /* beidou ephemeris (tentative mt) */

        case 1074:
            ret = decode_msm4(rtcm, SYS_GPS);
            break;
        case 1075:
            ret = decode_msm5(rtcm, SYS_GPS);
            break;
        case 1076:
            ret = decode_msm6(rtcm, SYS_GPS);
            break;
        case 1077:
            ret = decode_msm7(rtcm, SYS_GPS);
            break;

        case 1084:
            ret = decode_msm4(rtcm, SYS_GLO);
            break;
        case 1085:
            ret = decode_msm5(rtcm, SYS_GLO);
            break;
        case 1086:
            ret = decode_msm6(rtcm, SYS_GLO);
            break;
        case 1087:
            ret = decode_msm7(rtcm, SYS_GLO);
            break;

        case 1094:
            ret = decode_msm4(rtcm, SYS_GAL);
            break;
        case 1095:
            ret = decode_msm5(rtcm, SYS_GAL);
            break;
        case 1096:
            ret = decode_msm6(rtcm, SYS_GAL);
            break;
        case 1097:
            ret = decode_msm7(rtcm, SYS_GAL);
            break;

        case 1114:
            ret = decode_msm4(rtcm, SYS_QZS);
            break;
        case 1115:
            ret = decode_msm5(rtcm, SYS_QZS);
            break;
        case 1116:
            ret = decode_msm6(rtcm, SYS_QZS);
            break;
        case 1117:
            ret = decode_msm7(rtcm, SYS_QZS);
            break;

        case 1124:
            ret = decode_msm4(rtcm, SYS_BDS);
            break;
        case 1125:
            ret = decode_msm5(rtcm, SYS_BDS);
            break;
        case 1126:
            ret = decode_msm6(rtcm, SYS_BDS);
            break;
        case 1127:
            ret = decode_msm7(rtcm, SYS_BDS);
            break;
    }
    // if(type!=1019 && type != 1019&& type != 1020 && type != 1042 && type != 1046 && type != 1044
    // & type != 1005) printf("decode_rtcm3:rcv=%d len=%3d type=%d time=%d ret=%d\n", rtcm->rcv,
    // rtcm->len, type,rtcm->time.time,ret);
    return ret;
}

extern int input_rtcm3(rtcm_t* rtcm, unsigned char data)
{
    // trace(5, "input_rtcm3: data=%02x\n", data);

    /* synchronize frame */
    if (rtcm->nbyte == 0)
    {
        if (data != RTCM3PREAMB)
        {
            return 0;
        }
        rtcm->buff[rtcm->nbyte++] = data;
        return 0;
    }
    rtcm->buff[rtcm->nbyte++] = data;

    if (rtcm->nbyte == 3)
    {
        rtcm->len = getbitu(rtcm->buff, 14, 10) + 3; /* length without parity */
    }
    if (rtcm->nbyte < 3 || rtcm->nbyte < rtcm->len + 3)
    {
        return 0;
    }
    rtcm->nbyte = 0;

    /* check parity */
    if (rtk_crc24q(rtcm->buff, rtcm->len) != getbitu(rtcm->buff, rtcm->len * 8, 24))
    {
        trace(0x01, "rtcm3 parity error: len=%d\n", rtcm->len);
        return 0;
    }
    /* decode rtcm3 message */
    return decode_rtcm3(rtcm);
}
