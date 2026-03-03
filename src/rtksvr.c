#include "cJSON.h"
#include "logmod.h"
#include "multipath.h"
#include "rtk.h"
#include <stdlib.h>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#define __USE_MISC
#ifndef CRTSCTS
#define CRTSCTS 020000000000
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <termios.h>
#endif

#ifdef _MSC_VER
#include "../src/include/dirent_bak.h"
#else
// #include <dirent.h>
#endif

rtksvr_t      svr;
nav_t         g_nav    = {0};
myFile_t      oFile    = {0};
cfgopt_t      g_cfgOpt = {0};
unsigned char rtkReturnValue;
unsigned char rebootFlag                 = 0;
unsigned char rtcmMode                   = 0;
FILE*         fptcp                      = NULL;
FILE*         fpcof                      = NULL;
char          configFileFath[MAXSTRPATH] = {0};
char          tcpFileFath[MAXSTRPATH]    = {0};

char   logFileSizeName[1024] = {0};
double logFileSize           = 100;
int    logFileNum            = 2;
double writeConfigTime       = 5;
double writeDugTime          = 1;

#define OBSBASELEN 30
obsd_t        g_baseObsSync[OBSBASELEN][MAXOBS];
unsigned char g_nbaseObsSync[OBSBASELEN] = {0};
unsigned char g_baseObsSyncIndex         = 255;
unsigned char g_baseObsBuffFull          = 0;

double g_gpsLam[NFREQ] = {CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5};
double g_galLam[NFREQ] = {CLIGHT / FREQ1, CLIGHT / FREQ7, CLIGHT / FREQ5};
// double g_bdsLam[NFREQ] = { CLIGHT / FREQ1_CMP, CLIGHT / FREQ2_CMP, CLIGHT /
// FREQ3_CMP,CLIGHT / FREQB1C_CMP ,CLIGHT / FREQB2a_CMP ,CLIGHT / FREQB2b_CMP };
double g_bdsLam[NFREQ] = {CLIGHT / FREQ1_CMP,   CLIGHT / FREQ2_CMP,
                          CLIGHT / FREQB2a_CMP, CLIGHT / FREQB2b_CMP,
                          CLIGHT / FREQB1C_CMP, CLIGHT / FREQ3_CMP};  // FREQB2a_CMP

double        g_gloLam[MAXPRNGLO][NFREQ] = {0.0};
obsd_t        g_preBaseObsRtk[MAXOBS];
int           g_preBaseObsRtkNum;
unsigned char level_trace         = 0xff;
unsigned char trace_flag[64]      = {0};
double        baseRtcmPosition[3] = {0};

int SELETE_SAT_NUM = 40;
int NX;
int NY;
int g_week;
#define BUFFSIZE 32768
#define NTRIP_MAXRSP 32768 /* max size of ntrip response */
#define NTRIP_MAXSTR 256   /* max length of mountpoint string */

#ifdef WIN32
#define dev_t HANDLE
#define socket_t SOCKET
typedef int socklen_t;
#else
#define dev_t int
#define socket_t int
#define closesocket close
#endif

typedef struct
{                                  /* tcp control type */
    int                state;      /* state (0:close,1:wait,2:connect) */
    char               saddr[256]; /* address string */
    int                port;       /* port */
    struct sockaddr_in addr;       /* address resolved */
    socket_t           sock;       /* socket descriptor */
    int                tcon;       /* reconnect time (ms) (-1:never,0:now) */
    uint32_t           tact;       /* data active tick */
    uint32_t           tdis;       /* disconnect tick */
} tcp_t;

typedef struct
{                  /* tcp cilent type */
    tcp_t svr;     /* tcp server control */
    int   toinact; /* inactive timeout (ms) (0:no timeout) */
    int   tirecon; /* reconnect interval (ms) (0:no reconnect) */
} tcpcli_t;

typedef struct
{                                 /* ntrip control type */
    int       state;              /* state (0:close,1:wait,2:connect) */
    int       type;               /* type (0:server,1:client) */
    int       nb;                 /* response buffer size */
    char      url[MAXSTRPATH];    /* url for proxy */
    char      mntpnt[256];        /* mountpoint */
    char      user[256];          /* user */
    char      passwd[256];        /* password */
    char      str[NTRIP_MAXSTR];  /* mountpoint string for server */
    uint8_t   buff[NTRIP_MAXRSP]; /* response buffer */
    tcpcli_t* tcp;                /* tcp client */
} ntrip_t;

int  strtype[3]       = {STR_TCPCLI, STR_TCPCLI};  // STR_TCPCLI STR_TCPSVR
int  format[3]        = {STRFMT_RTCM3, STRFMT_RTCM3, STRFMT_RTCM3};
char strpath[3][1024] = {"192.168.21.196:5000", "192.168.21.196:5001", ""};

extern void rtksvrlock(rtksvr_t* svr) { lock(&svr->lock); }
extern void rtksvrunlock(rtksvr_t* svr) { unlock(&svr->lock); }
static void iniGloLam()
{
    int i = 0, frq;
    for (i = 1; i <= MAXPRNGLO; i++)
    {
        if (i == 1 || i == 5)
        {
            frq = 1;
        }
        else if (i == 20 || i == 24)
        {
            frq = 2;
        }
        else if (i == 19 || i == 23)
        {
            frq = 3;
        }
        else if (i == 17 || i == 21)
        {
            frq = 4;
        }
        else if (i == 3 || i == 7)
        {
            frq = 5;
        }
        else if (i == 4 || i == 8)
        {
            frq = 6;
        }
        else if (i == 11 || i == 15)
        {
            frq = 0;
        }
        else if (i == 12 || i == 16)
        {
            frq = -1;
        }
        else if (i == 9 || i == 13)
        {
            frq = -2;
        }
        else if (i == 18 || i == 22)
        {
            frq = -3;
        }
        else if (i == 2 || i == 6)
        {
            frq = -4;
        }
        else if (i == 10 || i == 14)
        {
            frq = -7;
        }
        else
        {
            frq = 0;
        }
        g_gloLam[i - 1][0] = CLIGHT / (FREQ1_GLO + DFRQ1_GLO * frq);
        g_gloLam[i - 1][1] = CLIGHT / (FREQ2_GLO + DFRQ2_GLO * frq);
        g_gloLam[i - 1][2] = CLIGHT / FREQ3_GLO;
    }
}
obsqueue_t Qrover;
int        InitQueue(obsqueue_t* Q)
{
    Q->rear = -1;
    return 1;
}

int GetHead(obsqueue_t* Q)
{
    int i, j, k;
    if (Q->rear <= 0)
    {
        return 0;
    }
    // printf("GetHead rear=%d time=%d n=%d\n", Q->rear,
    // Q->data[0].data[0].time.time, Q->data[0].n);
    if (g_baseObsSyncIndex == 255)
    {
        return 0;
    }
    if (Q->data[0].data[0].time.time > g_baseObsSync[g_baseObsSyncIndex - 1][0].time.time)
    {
        return 0;
    }
    return 1;
}
int EnQueue(obsqueue_t* Q, qobs_t e)
{
    int i;
    if (Q->rear == MAXQUEUESIZE || Q->rear == -1)
    {
        Q->rear = 0;
    }
    Q->data[Q->rear] = e;
    Q->rear++;
    return 1;
}

/* compare observation data -------------------------------------------------*/
static int cmpobs(const void* p1, const void* p2)
{
    obsd_t *q1 = (obsd_t*)p1, *q2 = (obsd_t*)p2;
    double  tt = timediff(q1->time, q2->time);
    if (fabs(tt) > DTTOL)
    {
        return tt < 0 ? -1 : 1;
    }
    if (q1->rcv != q2->rcv)
    {
        return (int)q1->rcv - (int)q2->rcv;
    }
    return (int)q1->sat - (int)q2->sat;
}

static int sortobs(obs_t* obs)
{
    int i, j, n;

    // trace(3, "sortobs: nobs=%d\n", obs->n);

    if (obs->n <= 0)
    {
        return 0;
    }

    qsort(obs->data, obs->n, sizeof(obsd_t), cmpobs);

    /* delete duplicated data */
    for (i = j = 0; i < obs->n; i++)
    {
        if (obs->data[i].sat != obs->data[j].sat || obs->data[i].rcv != obs->data[j].rcv ||
            timediff(obs->data[i].time, obs->data[j].time) != 0.0)
        {
            obs->data[++j] = obs->data[i];
        }
    }
    obs->n = j + 1;

    for (i = n = 0; i < obs->n; i = j, n++)
    {
        for (j = i + 1; j < obs->n; j++)
        {
            if (timediff(obs->data[j].time, obs->data[i].time) > DTTOL)
            {
                break;
            }
        }
    }
    return n;
}

static void updatesvr(rtksvr_t* svr, int ret, obs_t* obs, int index, int iobs)
{
    double pos[3], del[3] = {0}, dr[3];
    int    i, n           = 0;

    // tracet(4, "updatesvr: ret=%d sat=%2d index=%d\n", ret, sat, index);

    if (ret == 1)
    { /* observation data */
        trace(
            4, "update_obs, %s, %d, %d, %d\n", time_str(obs->data[0].time, 0), obs->n, index, iobs
        );

        if (iobs < 128)
        {
            for (i = 0; i < obs->n; i++)
            {
                // if(obs->data[i].SNR[0]<40*4)continue;
                svr->obs[index][iobs].data[n]     = obs->data[i];
                svr->obs[index][iobs].data[n].rcv = index + 1;
                n++;
            }
            svr->obs[index][iobs].n = n;
            sortobs(&svr->obs[index][iobs]);
        }
        // svr->nmsg[index][0]++;
    }
    else if (ret == 5 && svr->rtk.opt.useRtcmPosFlag == 1)
    { /* antenna postion parameters */
        if (index == 1)
        {
            for (i = 0; i < 3; i++)
            {
                if (svr->rtk.rb[i] != svr->rtcm[1].sta.pos[i])
                {
                    svr->rtk.sol.rr_smooth_cnt = 0;
                }
                svr->rtk.rb[i]      = svr->rtcm[1].sta.pos[i];
                baseRtcmPosition[i] = svr->rtcm[1].sta.pos[i];
            }
            /* antenna delta */
            ecef2pos(svr->rtk.rb, pos);
            if (svr->rtcm[1].sta.deltype)
            { /* xyz */
                del[2] = svr->rtcm[1].sta.hgt;
                enu2ecef(pos, del, dr);
                for (i = 0; i < 3; i++)
                {
                    svr->rtk.rb[i] += svr->rtcm[1].sta.del[i] + dr[i];
                }
            }
            else
            { /* enu */
                enu2ecef(pos, svr->rtcm[1].sta.del, dr);
                for (i = 0; i < 3; i++)
                {
                    svr->rtk.rb[i] += dr[i];
                }
            }
        }
    }
}

extern int decoderaw(rtksvr_t* svr, int index)
{
    obs_t* obs = NULL;
    int    i, ret = 0, fobs = 0;
    rtksvrlock(svr);
    svr->format[index] = STRFMT_RTCM3;
    for (i = 0; i < svr->nb[index]; i++)
    {
        if (svr->format[index] == STRFMT_RTCM3)
        {
            svr->rtcm[index].rcv = index;
            ret                  = input_rtcm3(svr->rtcm + index, svr->buff[index][i]);
            obs                  = &svr->rtcm[index].obs;
            if (ret != 0)
            {
                trace(
                    2, "index,%d,time,%s,obs->n,%d\n", index, time_str(obs[0].data[0].time, 0),
                    obs->n
                );
                // printf("decode rtcm3 main=%d ok\n",index);
            }
        }
        /* update rtk server */
        if (ret > 0)
        {
            updatesvr(svr, ret, obs, index, fobs);
        }
        /* observation data received */
        if (ret == 1)
        {
            if (fobs < 128)
            {
                fobs++;
            }
            else
            {
                svr->prcout++;
            }
        }
    }
    svr->nb[index] = 0;
    rtksvrunlock(svr);
    return fobs;
}

#ifdef MULBASE
static void outDnyResult(
    rtksvr_t* svr, char** pbuff, char* s1, unsigned char iniEnuFlag, int fixCnt
)
{
    if (svr->rtk.opt.kMode == 0)
    {
        if (svr->rtk.opt.timeIntervalSolution != 0 && fixCnt > 0)
        {
            svr->rtk.sol.stat = 1;
            trace(4, "outDnyResult set stat 1\n");
        }
        if (svr->rtk.opt.masterSlaveBaseFlag == 0 && svr->rtk.opt.masterXyz[0] != 0 &&
            svr->rtk.opt.slaveXyz[0] != 0)
        {
            if (svr->rtk.dr[0] == 0.0)
            {
                return;
            }
            *pbuff += sprintf(
                *pbuff,
                "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%"
                ".9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%."
                "4lf,%d;",
                s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0],
                svr->rtk.sol.rr_original[0] + svr->rtk.dr[0],
                svr->rtk.sol.rr_original[1] + svr->rtk.dr[1],
                svr->rtk.sol.rr_original[2] + svr->rtk.dr[2], svr->rtk.sol.enu_original[0],
                svr->rtk.sol.enu_original[1], svr->rtk.sol.enu_original[2],
                svr->rtk.sol.rr_filer[0] + svr->rtk.dr[0],
                svr->rtk.sol.rr_filer[1] + svr->rtk.dr[1],
                svr->rtk.sol.rr_filer[2] + svr->rtk.dr[2], svr->rtk.sol.enu[0], svr->rtk.sol.enu[1],
                svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1], svr->rtk.sol.vel[2],
                svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
                svr->rtk.opt.masterXyz[0], svr->rtk.opt.masterXyz[1], svr->rtk.opt.masterXyz[2],
                iniEnuFlag
            );
        }
        else
        {
            *pbuff += sprintf(
                *pbuff,
                "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%"
                ".9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%."
                "4lf,%d;",
                s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0], svr->rtk.sol.rr_original[0],
                svr->rtk.sol.rr_original[1], svr->rtk.sol.rr_original[2],
                svr->rtk.sol.enu_original[0], svr->rtk.sol.enu_original[1],
                svr->rtk.sol.enu_original[2], svr->rtk.sol.rr_filer[0], svr->rtk.sol.rr_filer[1],
                svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0], svr->rtk.sol.enu[1],
                svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1], svr->rtk.sol.vel[2],
                svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
                svr->rtk.opt.masterXyz[0], svr->rtk.opt.masterXyz[1], svr->rtk.opt.masterXyz[2],
                iniEnuFlag
            );
        }
    }
    else
    {
        if (svr->rtk.opt.timeIntervalSolution != 0 && fixCnt > 0)
        {
            svr->rtk.sol.stat = 1;
            trace(4, "outDnyResult set stat 1\n");
        }
        if (svr->rtk.opt.masterSlaveBaseFlag == 0 && svr->rtk.opt.masterXyz[0] != 0 &&
            svr->rtk.opt.slaveXyz[0] != 0)
        {
            if (svr->rtk.dr[0] == 0.0)
            {
                return;
            }
            *pbuff += sprintf(
                *pbuff,
                "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%"
                ".9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%."
                "4lf,%d;",
                s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0],
                svr->rtk.sol.rr_filer[0] + svr->rtk.dr[0],
                svr->rtk.sol.rr_filer[1] + svr->rtk.dr[1],
                svr->rtk.sol.rr_filer[2] + svr->rtk.dr[2], svr->rtk.sol.enu[0], svr->rtk.sol.enu[1],
                svr->rtk.sol.enu[2], svr->rtk.sol.rr_filer[0] + svr->rtk.dr[0],
                svr->rtk.sol.rr_filer[1] + svr->rtk.dr[1],
                svr->rtk.sol.rr_filer[2] + svr->rtk.dr[2], svr->rtk.sol.enu[0], svr->rtk.sol.enu[1],
                svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1], svr->rtk.sol.vel[2],
                svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
                svr->rtk.opt.masterXyz[0], svr->rtk.opt.masterXyz[1], svr->rtk.opt.masterXyz[2],
                iniEnuFlag
            );
        }
        else
        {
            *pbuff += sprintf(
                *pbuff,
                "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%"
                ".9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%."
                "4lf,%d;",
                s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0], svr->rtk.sol.rr_filer[0],
                svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0],
                svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.sol.rr_filer[0],
                svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0],
                svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1],
                svr->rtk.sol.vel[2], svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
                svr->rtk.opt.masterXyz[0], svr->rtk.opt.masterXyz[1], svr->rtk.opt.masterXyz[2],
                iniEnuFlag
            );
        }
    }
    /* OUTPUT FFT Infomation */
    if (svr->rtk.opt.senceopt == 1)
    {
        *pbuff += sprintf(*pbuff, "fft,%.4f,%.4f;", svr->rtk.fftFrq[2], svr->rtk.fftPower[2]);
    }
    else
    {
        if ((svr->rtk.sol.time.time + ROUND(svr->rtk.sol.time.frac)) % (int)writeConfigTime == 0)
        {
            *pbuff += sprintf(*pbuff, "\n");
            if (svr->rtk.opt.masterSlaveBaseFlag == 0 && svr->rtk.opt.masterXyz[0] != 0 &&
                svr->rtk.opt.slaveXyz[0] != 0)
            {
                if (svr->rtk.dr[0] == 0.0)
                {
                    return;
                }
                *pbuff += sprintf(
                    *pbuff, "cof,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%.4f;",
                    svr->rtk.opt.masterXyz[0], svr->rtk.opt.masterXyz[1], svr->rtk.opt.masterXyz[2],
                    svr->rtk.sol.enu[0], svr->rtk.sol.enu[1], svr->rtk.sol.enu[2],
                    svr->rtk.enuWindwoIndex[0], svr->rtk.enuWindwoIndex[1],
                    svr->rtk.enuWindwoIndex[2], svr->rtk.sol.rr_filer[0] + svr->rtk.dr[0],
                    svr->rtk.sol.rr_filer[1] + svr->rtk.dr[1],
                    svr->rtk.sol.rr_filer[2] + svr->rtk.dr[2]
                );
            }
            else
            {
                *pbuff += sprintf(
                    *pbuff, "cof,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%.4f;",
                    svr->rtk.rb[0], svr->rtk.rb[1], svr->rtk.rb[2], svr->rtk.sol.enu[0],
                    svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.enuWindwoIndex[0],
                    svr->rtk.enuWindwoIndex[1], svr->rtk.enuWindwoIndex[2],
                    svr->rtk.sol.rr_filer[0], svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2]
                );
            }
        }
    }
    *pbuff += sprintf(*pbuff, "\n");
}
#else

static void outDnyResult(
    rtksvr_t* svr, char** pbuff, char* s1, unsigned char iniEnuFlag, int fixCnt
)
{
    if (svr->rtk.opt.kMode == 0)
    {
        if (svr->rtk.opt.timeIntervalSolution != 0 && fixCnt > 0)
        {
            svr->rtk.sol.stat = 1;
            trace(4, "outDnyResult set stat 1\n");
        }
        *pbuff += sprintf(
            *pbuff,
            "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%."
            "9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%.4lf,"
            "%d;",
            s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0], svr->rtk.sol.rr_original[0],
            svr->rtk.sol.rr_original[1], svr->rtk.sol.rr_original[2], svr->rtk.sol.enu_original[0],
            svr->rtk.sol.enu_original[1], svr->rtk.sol.enu_original[2], svr->rtk.sol.rr_filer[0],
            svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0],
            svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1],
            svr->rtk.sol.vel[2], svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
            svr->rtk.rb[0], svr->rtk.rb[1], svr->rtk.rb[2], iniEnuFlag
        );
    }
    else
    {
        if (svr->rtk.opt.timeIntervalSolution != 0 && fixCnt > 0)
        {
            svr->rtk.sol.stat = 1;
            trace(4, "outDnyResult set stat 1\n");
        }
        *pbuff += sprintf(
            *pbuff,
            "pos,%s,%d,%d,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%.9lf,%."
            "9lf,%.9lf,%.9lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%.4lf,"
            "%d;",
            s1, svr->rtk.sol.stat, svr->rtk.sol.ns[0], svr->rtk.sol.rr_filer[0],
            svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0],
            svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.sol.rr_filer[0],
            svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2], svr->rtk.sol.enu[0],
            svr->rtk.sol.enu[1], svr->rtk.sol.enu[2], svr->rtk.sol.vel[0], svr->rtk.sol.vel[1],
            svr->rtk.sol.vel[2], svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
            svr->rtk.rb[0], svr->rtk.rb[1], svr->rtk.rb[2], iniEnuFlag
        );
    }
    /* OUTPUT FFT Infomation */
    if (svr->rtk.opt.senceopt == 1)
    {
        *pbuff += sprintf(*pbuff, "fft,%.4f,%.4f;", svr->rtk.fftFrq[2], svr->rtk.fftPower[2]);
    }
    else
    {
        if ((svr->rtk.sol.time.time + ROUND(svr->rtk.sol.time.frac)) % (int)writeConfigTime == 0)
        {
            *pbuff += sprintf(*pbuff, "\n");
            *pbuff += sprintf(
                *pbuff, "cof,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d;", svr->rtk.rb[0],
                svr->rtk.rb[1], svr->rtk.rb[2], svr->rtk.sol.enu[0], svr->rtk.sol.enu[1],
                svr->rtk.sol.enu[2], svr->rtk.enuWindwoIndex[0], svr->rtk.enuWindwoIndex[1],
                svr->rtk.enuWindwoIndex[2]
            );
        }
    }
    *pbuff += sprintf(*pbuff, "\n");
}
#endif

extern void out_json_fail(char* idStr, char* item, stream_t* stream)
{
    char ackBuff[1024] = {0};
    sprintf(ackBuff, "ack,%s,%s,failed;\n", idStr, item);
    strwrite(stream, (uint8_t*)ackBuff, strlen(ackBuff));
}

extern int get_json_int(char* idStr, cJSON* json, char* item, stream_t* stream, int* out)
{
    cJSON* json_data_tmp;

    json_data_tmp = cJSON_GetObjectItem(json, item);

    if (json_data_tmp != NULL)
    {
        *out = json_data_tmp->valueint;
        return 0;
    }

    out_json_fail(idStr, item, stream);
    return 1;
}

extern int get_json_uint(char* idStr, cJSON* json, char* item, stream_t* stream, uint8_t* out)
{
    cJSON* json_data_tmp;

    json_data_tmp = cJSON_GetObjectItem(json, item);

    if (json_data_tmp != NULL)
    {
        *out = json_data_tmp->valueint;
        return 0;
    }

    out_json_fail(idStr, item, stream);
    return 1;
}

extern int get_json_double(char* idStr, cJSON* json, char* item, stream_t* stream, double* out)
{
    cJSON* json_data_tmp;
    json_data_tmp = cJSON_GetObjectItem(json, item);

    if (json_data_tmp != NULL)
    {
        *out = json_data_tmp->valuedouble;
        return 0;
    }
    out_json_fail(idStr, item, stream);
    return 1;
}

static int decodeConfig(rtksvr_t* svr, cfgopt_t* cfgOpt, char* buff)
{
    int    i, configflag = 1;
    cJSON* json          = NULL;
    cJSON* json_data     = NULL;
    cJSON* json_data_tmp = NULL;
    char*  str;
    double value;
    char   ackBuff[4096] = {0};
    char   idStr[64]     = {0};
    printf("%s\n", buff);
    buff = strstr(buff, "{");
    //printf("%s\n", buff);
    json = cJSON_Parse(buff);

    if (NULL == json)
    {
        printf("cJSON_Parse error:%s\n", cJSON_GetErrorPtr());
        return 0;
    }
    json_data_tmp = cJSON_GetObjectItem(json, "taskID");
    if (json_data_tmp == NULL)
    {
        printf("timeInterval json_data_tmp is null\n");

        sprintf(ackBuff, "ack,ID,failed;\n");
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        strcpy(idStr, json_data_tmp->valuestring);
    }

    // json_data_tmp = cJSON_GetObjectItem(json, "timeInterval");
    // if (json_data_tmp == NULL) {
    //     printf("timeInterval json_data_tmp is null\n");
    //     cJSON_Delete(json);
    //     sprintf(ackBuff, "ack,%s,timeInterval,failed;\n", idStr);
    //     strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    //     return 0;
    // }
    // cfgOpt->timeInterval = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "freq");
    if (json_data_tmp == NULL)
    {
        printf("freq json_data_tmp is null\n");
    }
    else
    {
        cfgOpt->freq = json_data_tmp->valueint;
        configflag   = 0;
    }

    // get_json_int(idStr, json, "iono", &svr->stream[2], &cfgOpt->iono);

    json_data_tmp = cJSON_GetObjectItem(json, "iono");
    if (json_data_tmp == NULL)
    {
        printf("iono json_data_tmp is null\n");
        sprintf(ackBuff, "ack,%s,iono,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->iono = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "trop");
    if (json_data_tmp == NULL)
    {
        printf("trop json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,trop,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->trop = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "tides");
    if (json_data_tmp == NULL)
    {
        printf("tides json_data_tmp is null\n");
        sprintf(ackBuff, "ack,%s,tides,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->tides = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "elevMin");
    if (json_data_tmp == NULL)
    {
        printf("elevMin json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,elevMin,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->elevMin = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "cn0Min");
    if (json_data_tmp == NULL)
    {
        printf("cn0Min json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,cn0Min,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->cn0Min = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "gdopThld");
    if (json_data_tmp == NULL)
    {
        printf("gdopThld json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,gdopThld,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->gdopThld = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "diffAge");
    if (json_data_tmp == NULL)
    {
        printf("diffAgeMax json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,diffAge,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->diffAgeMax = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "sys");
    if (json_data_tmp == NULL)
    {
        printf("sys json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,sys,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->sys = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "postResThld");
    if (json_data_tmp == NULL)
    {
        printf("postResThld json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,postResThld,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->postResThld = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "kMode");
    if (json_data_tmp == NULL)
    {
        printf("kMode json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,kMode,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->kMode = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "buffSize");
    if (json_data_tmp == NULL)
    {
        printf("buffSize json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,buffSize,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->buffSize = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "pcvE");
    if (json_data_tmp == NULL)
    {
        printf("pcvE json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,pcvE,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->stationPCV[0] = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "pcvN");
    if (json_data_tmp == NULL)
    {
        printf("pcvN json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,pcvN,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->stationPCV[1] = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "pcvU");
    if (json_data_tmp == NULL)
    {
        printf("pcvU json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,pcvU,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->stationPCV[2] = json_data_tmp->valuedouble;
    }

    // json_data_tmp = cJSON_GetObjectItem(json, "scene");
    // if (json_data_tmp == NULL) {
    //     printf("senceopt json_data_tmp is null\n");
    //     cJSON_Delete(json);
    //     sprintf(ackBuff, "ack,%s,scene,failed;\n", idStr);
    //     strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    //     return 0;
    // }
    // cfgOpt->senceopt = json_data_tmp->valueint;

    // json_data_tmp = cJSON_GetObjectItem(json, "smoothWindowTime");
    // if (json_data_tmp == NULL) {
    //     printf("smoothWindowsTime json_data_tmp is null\n");
    //     cJSON_Delete(json);
    //     sprintf(ackBuff, "ack,%s,smoothWindowTime,failed;\n", idStr);
    //     strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    //     return 0;
    // }
    // cfgOpt->smoothWindowsTime = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "initTime");
    if (json_data_tmp == NULL)
    {
        printf("initEnuTime json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,initTime,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->initEnuTime = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "detectSensitivity");
    if (json_data_tmp == NULL)
    {
        printf("detectSensitivity json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,detectSensitivity,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->detectSensitivity = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "typeSol");
    if (json_data_tmp == NULL)
    {
        printf("typeSol json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,typeSol,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->typeSol = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "timeIntervalSolution");
    if (json_data_tmp == NULL)
    {
        printf("timeIntervalSolution json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,timeIntervalSolution,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->timeIntervalSolution = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "minFixSat");
    if (json_data_tmp == NULL)
    {
        printf("minFixSat json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,minFixSat,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->minFixSat = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "maxDelSat");
    if (json_data_tmp == NULL)
    {
        printf("maxDelSat json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,maxDelSat,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->maxDelSat = json_data_tmp->valueint;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "gpsMask");
    if (json_data_tmp == NULL)
    {
        printf("gpsMask json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,gpsMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->gpsMask = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "qzssMask");
    if (json_data_tmp == NULL)
    {
        printf("qzsMask json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,qzssMask,failed\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->qzssMask = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "glonassMask");
    if (json_data_tmp == NULL)
    {
        printf("glonassMask json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,glonassMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->glonassMask = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "galieoMask");
    if (json_data_tmp == NULL)
    {
        printf("galieoMask json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,galieoMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->galieoMask = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "beidouMask");
    if (json_data_tmp == NULL)
    {
        printf("bdsMask json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,beidouMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->bdsMask = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "minSatRes");
    if (json_data_tmp == NULL)
    {
        printf("minSatRes json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,minSatRes,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->minSatRes = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "iggiiik0");
    if (json_data_tmp == NULL)
    {
        printf("iggiiik0 json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,iggiiik0,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->iggiiik0 = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "iggiiik1");
    if (json_data_tmp == NULL)
    {
        printf("iggiiik1 json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,iggiiik1,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        cfgOpt->iggiiik1 = json_data_tmp->valuedouble;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "param");
    if (json_data_tmp == NULL)
    {
        printf("param json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,param,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value         = str2num(str, 0, strlen(str));
            cfgOpt->param = (int)value;
        }
        else
        {
            cfgOpt->param = json_data_tmp->valueint;
        }
    }

    json_data_tmp = cJSON_GetObjectItem(json, "levelTrace");
    if (json_data_tmp == NULL)
    {
        printf("levelTrace json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,levelTrace,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value = str2num(str, 0, strlen(str));
            // level_trace = (int)value;
            level_trace = ((int)value > 7 ? 0xFF : (0xFF >> (8 - (int)value)));
        }
        else
        {
            level_trace =
                (json_data_tmp->valueint > 7 ? 0xFF : (0xFF >> (8 - json_data_tmp->valueint)));
        }
    }

    json_data_tmp = cJSON_GetObjectItem(json, "logFileSize");
    if (json_data_tmp == NULL)
    {
        printf("logFileSize json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,logFileSize,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value       = str2num(str, 0, strlen(str));
            logFileSize = value;
        }
        else
        {
            logFileSize = json_data_tmp->valuedouble;
        }
    }
    if (logFileSize <= 0)
    {
        logFileSize = 100;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "logFileNum");
    if (json_data_tmp == NULL)
    {
        printf("logFileNum json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,logFileNum,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value      = str2num(str, 0, strlen(str));
            logFileNum = value;
        }
        else
        {
            logFileNum = json_data_tmp->valueint;
        }
    }

    json_data_tmp = cJSON_GetObjectItem(json, "writeDugTIme");
    if (json_data_tmp == NULL)
    {
        printf("writeDugTIme json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,writeDugTIme,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value        = str2num(str, 0, strlen(str));
            writeDugTime = value;
        }
        else
        {
            writeDugTime = json_data_tmp->valuedouble;
        }
    }

    if (writeDugTime <= 0)
    {
        writeDugTime = 10;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "writeConfigTime");
    if (json_data_tmp == NULL)
    {
        printf("writeDugTIme json_data_tmp is null\n");

        sprintf(ackBuff, "ack,%s,writeDugTIme,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    }
    else
    {
        str = json_data_tmp->valuestring;
        if (str != NULL)
        {
            value           = str2num(str, 0, strlen(str));
            writeConfigTime = value;
        }
        else
        {
            writeConfigTime = json_data_tmp->valuedouble;
        }
    }

    if (writeConfigTime <= 0 || writeConfigTime > 60)
    {
        writeConfigTime = 15;
    }
#ifdef MULBASE
    if (configflag == 1)
    {
        json_data_tmp = cJSON_GetObjectItem(json, "masterSlaveBaseFlag");
        if (json_data_tmp == NULL)
        {
            printf("masterSlaveBaseFlag json_data_tmp is null\n");
            sprintf(ackBuff, "ack,%s,masterSlaveBaseFlag,failed;\n", idStr);
            strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        }
        else
        {
            str = json_data_tmp->valuestring;
            if (str != NULL)
            {
                value                        = str2num(str, 0, strlen(str));
                g_cfgOpt.masterSlaveBaseFlag = value;
            }
            else
            {
                g_cfgOpt.masterSlaveBaseFlag = json_data_tmp->valueint;
            }
        }
        if (g_cfgOpt.masterSlaveBaseFlag == 0 || g_cfgOpt.masterSlaveBaseFlag == 1)
        {
            svr->rtk.masterEnu[0] = svr->rtk.masterEnu[1] = svr->rtk.masterEnu[2] = 0.0;
            json_data_tmp = cJSON_GetObjectItem(json, "masterE");
            if (json_data_tmp == NULL)
            {
                printf("masterX json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterE,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    svr->rtk.masterEnu[0] = value;
                }
                else
                {
                    svr->rtk.masterEnu[0] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterN");
            if (json_data_tmp == NULL)
            {
                printf("masterY json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterN,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    svr->rtk.masterEnu[1] = value;
                }
                else
                {
                    svr->rtk.masterEnu[1] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterU");
            if (json_data_tmp == NULL)
            {
                printf("masterZ json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterZ,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    svr->rtk.masterEnu[2] = value;
                }
                else
                {
                    svr->rtk.masterEnu[2] = json_data_tmp->valuedouble;
                }
            }

            json_data_tmp = cJSON_GetObjectItem(json, "masterRx");
            if (json_data_tmp == NULL)
            {
                printf("masterX json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterRx,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    svr->rtk.masterRr[0] = value;
                }
                else
                {
                    svr->rtk.masterRr[0] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterRy");
            if (json_data_tmp == NULL)
            {
                printf("masterY json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterRy,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    svr->rtk.masterRr[1] = value;
                }
                else
                {
                    svr->rtk.masterRr[1] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterRz");
            if (json_data_tmp == NULL)
            {
                printf("masterZ json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterRz,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    svr->rtk.masterRr[2] = value;
                }
                else
                {
                    svr->rtk.masterRr[2] = json_data_tmp->valuedouble;
                }
            }
#ifdef MULBASE
            json_data_tmp = cJSON_GetObjectItem(json, "masterX");
            if (json_data_tmp == NULL)
            {
                printf("masterX json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterX,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    g_cfgOpt.masterXyz[0] = value;
                }
                else
                {
                    g_cfgOpt.masterXyz[0] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterY");
            if (json_data_tmp == NULL)
            {
                printf("masterY json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterY,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    g_cfgOpt.masterXyz[1] = value;
                }
                else
                {
                    g_cfgOpt.masterXyz[1] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "masterZ");
            if (json_data_tmp == NULL)
            {
                printf("masterZ json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,masterZ,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                 = str2num(str, 0, strlen(str));
                    g_cfgOpt.masterXyz[2] = value;
                }
                else
                {
                    g_cfgOpt.masterXyz[2] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "slaveX");
            if (json_data_tmp == NULL)
            {
                printf("slaveX json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,slaveX,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    g_cfgOpt.slaveXyz[0] = value;
                }
                else
                {
                    g_cfgOpt.slaveXyz[0] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "slaveY");
            if (json_data_tmp == NULL)
            {
                printf("slaveY json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,slaveY,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    g_cfgOpt.slaveXyz[1] = value;
                }
                else
                {
                    g_cfgOpt.slaveXyz[1] = json_data_tmp->valuedouble;
                }
            }
            //
            json_data_tmp = cJSON_GetObjectItem(json, "slaveZ");
            if (json_data_tmp == NULL)
            {
                printf("slaveZ json_data_tmp is null\n");
                sprintf(ackBuff, "ack,%s,slaveZ,failed;\n", idStr);
                strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                // g_cfgOpt.masterSlaveBaseFlag = 1;
                return;
            }
            else
            {
                str = json_data_tmp->valuestring;
                if (str != NULL)
                {
                    value                = str2num(str, 0, strlen(str));
                    g_cfgOpt.slaveXyz[2] = value;
                }
                else
                {
                    g_cfgOpt.slaveXyz[2] = json_data_tmp->valuedouble;
                }
            }
            if (g_cfgOpt.masterSlaveBaseFlag == 0)
            {
                svr->rtk.rb[0] = svr->rtk.opt.slaveXyz[0] = g_cfgOpt.slaveXyz[0];
                svr->rtk.rb[1] = svr->rtk.opt.slaveXyz[1] = g_cfgOpt.slaveXyz[1];
                svr->rtk.rb[2] = svr->rtk.opt.slaveXyz[2] = g_cfgOpt.slaveXyz[2];
                svr->rtk.opt.masterXyz[0]                 = g_cfgOpt.masterXyz[0];
                svr->rtk.opt.masterXyz[1]                 = g_cfgOpt.masterXyz[1];
                svr->rtk.opt.masterXyz[2]                 = g_cfgOpt.masterXyz[2];
                svr->rtk.sol.fixxyz[0] = svr->rtk.sol.fixxyz[1] = svr->rtk.sol.fixxyz[1] = 0.0;
                svr->rtk.sol.rr_smooth_cnt                                               = 0;
                svr->rtk.iniCnt                                                          = 0;
                svr->rtk.dr[0] = svr->rtk.dr[1] = svr->rtk.dr[2] = 0;
            }
            else
            {
                svr->rtk.rb[0] = svr->rtk.opt.masterXyz[0] = g_cfgOpt.masterXyz[0];
                svr->rtk.rb[1] = svr->rtk.opt.masterXyz[1] = g_cfgOpt.masterXyz[1];
                svr->rtk.rb[2] = svr->rtk.opt.masterXyz[2] = g_cfgOpt.masterXyz[2];
                svr->rtk.opt.slaveXyz[0] = g_cfgOpt.slaveXyz[0] = 0.0;
                svr->rtk.opt.slaveXyz[1] = g_cfgOpt.slaveXyz[1] = 0.0;
                svr->rtk.opt.slaveXyz[2] = g_cfgOpt.slaveXyz[2] = 0.0;
                svr->rtk.sol.fixxyz[0] = svr->rtk.sol.fixxyz[1] = svr->rtk.sol.fixxyz[1] = 0.0;
                svr->rtk.sol.rr_smooth_cnt                                               = 0;
                svr->rtk.iniCnt                                                          = 0;
                svr->rtk.dr[0] = svr->rtk.dr[1] = svr->rtk.dr[2] = 0;
            }
#endif
            sprintf(ackBuff, "ack,baseFlag_%s,ok;\n", idStr);
            strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
            return;
        }
    }
#endif
    setCfgOpt(g_cfgOpt, &(svr->rtk.opt));
    // setCfgOpt(g_cfgOpt, &(svr->rtkepoch.opt));

    sprintf(ackBuff, "ack,%s,ok;\n", idStr);
    strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    cJSON_Delete(json);
    return 1;
}
static int traceconfig(void)
{
    trace(2, "rtk version:%d\n", SVN_VERSION);
    trace(
        2,
        "kMode:%d freq:%d iono:%d trop:%d tides:%d sys:%d senceopt:%d "
        "detectSensitivity:%d\n",
        g_cfgOpt.kMode, g_cfgOpt.freq, g_cfgOpt.iono, g_cfgOpt.trop, g_cfgOpt.tides, g_cfgOpt.sys,
        g_cfgOpt.senceopt, g_cfgOpt.detectSensitivity
    );

    char sysstr[10];

    sys2str(g_cfgOpt.sys, sysstr);
    trace(2, "navsys, %s\n", sysstr);

    trace(
        2,
        "typeSol:%d timeIntervalSolution:%d minFixSat:%d maxDelSat:%d param:%d "
        "gpsMask:%ld qzssMask:%ld\n",
        g_cfgOpt.typeSol, g_cfgOpt.timeIntervalSolution, g_cfgOpt.minFixSat, g_cfgOpt.maxDelSat,
        g_cfgOpt.param, g_cfgOpt.gpsMask, g_cfgOpt.qzssMask
    );

    trace(
        2,
        "glonassMask:%ld galieoMask:%ld bdsMask:%ld minSatRes:%.2f "
        "iggiiik0:%.2f iggiiik1:%.2f smoothWindowsTime:%.2f\n",
        g_cfgOpt.glonassMask, g_cfgOpt.galieoMask, g_cfgOpt.bdsMask, g_cfgOpt.minSatRes,
        g_cfgOpt.iggiiik0, g_cfgOpt.iggiiik1, g_cfgOpt.smoothWindowsTime
    );

    trace(
        2,
        "initEnuTime:%.2f diffAgeMax:%d buffSize:%d cn0Min:%.2f elevMin:%.2f "
        "gdopThld:%.2f postResThld:%.2f\n",
        g_cfgOpt.initEnuTime, g_cfgOpt.diffAgeMax, g_cfgOpt.buffSize, g_cfgOpt.cn0Min,
        g_cfgOpt.elevMin, g_cfgOpt.gdopThld, g_cfgOpt.postResThld
    );

    trace(
        2,
        "timeInterval:%.2f stationPCV[0]:%.2f stationPCV[0]:%.2f "
        "stationPCV[0]:%.2f\n",
        g_cfgOpt.timeInterval, g_cfgOpt.stationPCV[0], g_cfgOpt.stationPCV[1],
        g_cfgOpt.stationPCV[2]
    );

    trace(
        2,
        "level_trace:%d logFileSize:%.2f writeConfigTime:%.2f "
        "writeDugTime:%.2f logFileNum:%d\n",
        level_trace, logFileSize, writeConfigTime, writeDugTime, logFileNum
    );
    return 0;
}
/* rtk server thread ---------------------------------------------------------*/
#ifdef WIN32
static DWORD WINAPI rtksvrthread(void* arg)
#else
static void* rtksvrthread(void* arg)
#endif
{
    int i, j, f, k, m, n, cnt, dtMinIndex, cputime, fobs[2] = {0}, fnobs, size, ouInterval,
                                                    fixCnt = 0;
    double         tt, tow, dtMin, dt[OBSBASELEN] = {0};
    unsigned char *p, *q, iniEnuFlag = 0, sys, prn;
    unsigned int   cycle = 0, tick, tick1hz = 0, iniEnuCnt = 0, epochCnt = 0;
    double         iniEnu[3], pos[3], dr[3];
    gtime_t        time;
    obsd_t         obs[MAXOBS * 2] = {0}; /* for rover and base */
    obsd_t         obsPre[MAXOBS];
    obsd_t         obsepoch[MAXOBS * 2];
    int            nobsepoch = 0;
    int            nobsPre   = 0;
    solopt_t       sopt      = solopt_default;
    rtksvr_t*      svr       = (rtksvr_t*)arg;
    sopt.posf                = 2;  // 0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
    sopt.times               = 3;  // 0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间
    sopt.outvel              = 0;
    svr->tick                = tickget();
    svr->cycle               = 1000;
    char*  buff              = (char*)(calloc(sizeof(char), DEBUG_BUFF_LEN));
    char*  pbuff;
    char   s1[32];
    qobs_t qobs;

#ifndef WIN32
    struct stat fstat = {0};
#endif

    traceconfig();
    for (cycle = 0; svr->state; cycle++)
    {
        tick = tickget();
        time = timeget();
        time.time = ROUND(time.time + time.frac);
        time.frac = 0.0;
        if (sopt.times == 3)
        {
            time = gpst2utc(time);
            time.time += 3600 * 8;
        }
        time2str(time, s1, 3);
        
        trace(2, "systime,%s\n", s1);

        ouInterval = svr->rtk.opt.timeIntervalSolution * 3600;
        if (g_cfgOpt.initEnuTime > g_cfgOpt.smoothWindowsTime)
        {
            svr->rtk.opt.initEnuTime = g_cfgOpt.initEnuTime = g_cfgOpt.smoothWindowsTime;
        }
        if (ouInterval != 0)
        {
            if (time.time % ouInterval == 0)
            {
                memset(buff, 0, DEBUG_BUFF_LEN);
                pbuff = buff;
                if (fixCnt > 0)
                {
                    if (svr->rtk.opt.typeSol == 0)
                    {
                        outDnyResult(svr, &pbuff, s1, iniEnuFlag, fixCnt);
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                    else
                    {
                        outDnyResult(svr, &pbuff, s1, iniEnuFlag, fixCnt);
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                    fixCnt = 0;
                }
                else
                {
                    if (svr->rtk.opt.typeSol == 0)
                    {
                        pbuff += sprintf(
                            pbuff,
                            "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,"
                            "%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%"
                            ".4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%d;",
                            s1, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0
                        );
                        if (svr->rtk.opt.senceopt == 1)
                        {
                            pbuff += sprintf(pbuff, "fft,0.0,0.0;");
                        }
                        pbuff += sprintf(pbuff, "\n");
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));

                        pbuff = buff;
                        sprintf(pbuff, "rtk,%s,failed,%d\n", s1, rtkReturnValue);
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                    else
                    {
                        pbuff += sprintf(
                            pbuff,
                            "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,"
                            "%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%"
                            ".4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%d;",
                            s1, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                            0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0
                        );
                        // if (svr->rtkepoch.opt.senceopt == 1) {
                        //     pbuff += sprintf(pbuff, "fft,0.0,0.0;");
                        // }
                        pbuff += sprintf(pbuff, "\n");
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));

                        pbuff = buff;
                        sprintf(pbuff, "rtk,%s,failed,%d\n", s1, rtkReturnValue);
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                }
            }
        }
#ifndef WIN32
        if (fptcp && (time.time % 30 == 0))
        {
            stat(tcpFileFath, &fstat);
            size = fstat.st_size;
            // printf("size=%d\n", size);
            if (size / 1024 > 10240)
            {
                fclose(fptcp);
                fptcp = fopen(tcpFileFath, "w");
                if (fptcp == NULL)
                {
                    trace(0xff, "open file:%s error\n", tcpFileFath);
                    return 0;
                }
            }
        }
#else
        WIN32_FIND_DATA fileInfo;
        HANDLE          hFind;
        DWORD           fileSize;
        const char*     fileName = tcpFileFath;
        hFind                    = FindFirstFile(tcpFileFath, &fileInfo);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            fileSize = fileInfo.nFileSizeLow;
        }
        FindClose(hFind);
#endif

        for (i = 0; i < 2; i++)
        {
            if (fobs[i] != 0)
            {
                continue;
            }
            memset(buff, 0, DEBUG_BUFF_LEN);
            memset(svr->buff[i], 0, BUFFSIZE);
            p = svr->buff[i] + svr->nb[i];
            q = svr->buff[i] + BUFFSIZE;

            /* read receiver raw/rtcm data from input stream */
            // streamBase = i; then?
            if ((n = strread(svr->stream + i, p, q - p)) <= 0)
            {
                // trace(0x4, "%s rec data len=%d\n", strpath[i], n);
                continue;
            }
#ifdef WIN32
            printf("i=%d nb=%d\n", i, n);
#endif
            rtksvrlock(svr);
            svr->nb[i] += n;
            n = n < BUFFSIZE - svr->npb[i] ? n : (BUFFSIZE - svr->npb[i]);
            memcpy(svr->pbuf[i] + svr->npb[i], p, n);
            svr->npb[i] += n;
            rtksvrunlock(svr);
        }
        fobs[0] = fobs[1] = 0;
        for (i = 0; i < 2; i++)
        {
            fobs[i] = decoderaw(svr, i);  // 解码rtcm data and ssr
                                          // 返回每次从流里面读取以采样间隔为单位的组数
        }
        //-----------------read config-----------------
        if ((n = strread(svr->stream + 2, buff, DEBUG_BUFF_LEN)) > 0)
        {
            char* buff2 = (char*)(calloc(sizeof(char), DEBUG_BUFF_LEN));
            strcpy(buff2, buff);
            trace(0x4, "---------decodeConfig\n");
            trace(0x4, "%s\n", buff);
            trace(0x4, "rebootFlag:%d\n", rebootFlag);
            if (strstr(buff, "ack,update ok"))
            {
                trace(0x4, "set rebootFlag 1\n");
                rebootFlag = 1;
            }
            if (n != 0)
            {
                decodeConfig(svr, &g_cfgOpt, buff2);
                traceconfig();
                memset(buff, 0, DEBUG_BUFF_LEN);
                free(buff2);
                // if (g_cfgOpt.param > 0 && g_cfgOpt.param<=24) {
                //     for (i = 0; i < 3; i++) {
                //         svr->rtk.enuWindwoIndex[i] = 0;
                //         free(svr->rtk.enuWindow[i]);
                //         svr->rtk.enuWindow[i] = NULL;
                //     }
                //     svr->rtk.opt.smoothWindowsTime = g_cfgOpt.param;
                //     svr->rtk.maxSmoothPoint = svr->rtk.opt.smoothWindowsTime * 3600.0
                //     / ROUND(svr->rtk.opt.timeInterval);

                //    for (i = 0; i < 3; i++) {
                //        if (!(svr->rtk.enuWindow[i] =
                //        (double*)calloc(svr->rtk.maxSmoothPoint, sizeof(double)))) {
                //            return 0;
                //        }
                //    }
                //}
            }
        }

        trace(2, "fobs[0]=%d fobs[1]=%d, g_nav.n, %d\n", fobs[0], fobs[1], g_nav.n);

        for (k = 0; k < fobs[1]; k++)
        {
            qobs.n = 0;
            // if (svr->obs[1][k].data[0].time.time % 15 != 0)continue;
            // printf( "k=%d base  time=%d\n", k, svr->obs[1][k].data[0].time.time);
            trace(0x10, "k=%d base  time=%d\n", k, svr->obs[1][k].data[0].time.time);
            if (g_baseObsSyncIndex >= OBSBASELEN)
            {
                g_baseObsSyncIndex = 0;
                g_baseObsBuffFull  = 1;
            }
            g_nbaseObsSync[g_baseObsSyncIndex] = 0;
            for (j = 0; j < svr->obs[1][k].n; j++)
            {
                for (f = 0; f < NFREQ; f++)
                {
                    svr->obs[1][k].data[j].LockTime[f] = 3000;
                }
                g_baseObsSync[g_baseObsSyncIndex][j] = svr->obs[1][k].data[j];
                g_nbaseObsSync[g_baseObsSyncIndex]++;
            }
            g_baseObsSyncIndex++;
        }
        for (i = 0; i < fobs[0]; i++)
        {
            // if (svr->obs[0][i].data[0].time.time % 15 != 0)continue;
            trace(0x10, "k=%d rover time=%d\n", i, svr->obs[0][i].data[0].time.time);
            n = 0;
            for (j = 0; j < MAXSAT; j++)
            {
                for (f = 0; f < NFREQ; f++)
                {
                    svr->rtk.ssat[j].SNR[f] = 0;
                }
            }
            for (j = 0; j < svr->obs[0][i].n && n < MAXOBS * 2; j++)
            {
                sys = satsys(svr->obs[0][i].data[j].sat, &prn);

                if (!(svr->rtk.opt.sys & 1) && sys == SYS_GPS)
                {
                    continue;
                }
                if (!(svr->rtk.opt.sys & 2) && sys == SYS_QZS)
                {
                    continue;
                }
                if (!(svr->rtk.opt.sys & 4) && sys == SYS_BDS)
                {
                    continue;
                }
                if (!(svr->rtk.opt.sys & 8) && sys == SYS_GAL)
                {
                    continue;
                }
                if (!(svr->rtk.opt.sys & 16) && sys == SYS_GLO)
                {
                    continue;
                }
                if (sys == SYS_GPS && svr->rtk.opt.gpsMask >= 0)
                {
                    if (!((svr->rtk.opt.gpsMask >> (prn - 1)) & 1))
                    {
                        continue;
                    }
                }
                if (sys == SYS_QZS && svr->rtk.opt.qzssMask >= 0)
                {
                    if (!((svr->rtk.opt.qzssMask >> (prn - MINPRNQZS - 1)) & 1))
                    {
                        continue;
                    }
                }
                if (sys == SYS_BDS && svr->rtk.opt.bdsMask >= 0)
                {
                    if (!((svr->rtk.opt.bdsMask >> (prn - 1)) & 1))
                    {
                        continue;
                    }
                }
                if (sys == SYS_GAL && svr->rtk.opt.galieoMask >= 0)
                {
                    if (!((svr->rtk.opt.galieoMask >> (prn - 1)) & 1))
                    {
                        continue;
                    }
                }
                if (sys == SYS_GLO && svr->rtk.opt.glonassMask >= 0)
                {
                    if (!((svr->rtk.opt.glonassMask >> (prn - 1)) & 1))
                    {
                        continue;
                    }
                }

                obs[n] = svr->obs[0][i].data[j];
                /*Set frequency*/
                if (!(svr->rtk.opt.freq & 1))
                {
                    obs[n].P[0] = obs[n].L[0] = obs[n].D[0] = 0.0;
                }
                if (!(svr->rtk.opt.freq & 2))
                {
                    obs[n].P[1] = obs[n].L[1] = obs[n].D[1] = 0.0;
                }
                if (!(svr->rtk.opt.freq & 4))
                {
                    obs[n].P[2] = obs[n].L[2] = obs[n].D[2] = 0.0;
                }
                if (!(svr->rtk.opt.freq & 8))
                {
                    obs[n].P[3] = obs[n].L[3] = obs[n].D[3] = 0.0;
                    obs[n].P[4] = obs[n].L[4] = obs[n].D[4] = 0.0;
                    obs[n].P[5] = obs[n].L[5] = obs[n].D[5] = 0.0;
                }
                for (f = 0; f < NFREQ; f++)
                {
                    obs[n].LockTime[f] = 3000;
                }
                for (k = 0; k < NFREQ; k++)
                {
                    if (obs[n].SNR[k] < svr->rtk.opt.cn0Min * 4 || obs[n].LCK[k] < 8)
                    {
                        obs[n].P[k] = obs[n].L[k] = obs[n].D[k] = 0.0;
                    }
                }
                cnt = 0;
                if (sys == SYS_BDS)
                {
                    for (f = 0; f < NFREQ; f++)
                    {
                        if (obs[n].P[f] != 0.0)
                        {
                            cnt++;
                        }
                    }
                    for (f = 0; f < NFREQ; f++)
                    {
                        if (cnt >= 2 && f >= 3)
                        {
                            obs[n].P[f] = obs[n].L[f] = 0.0;
                        }
                    }
                }
                n++;
            }
            qobs.n = 0;
            for (j = 0; j < n; j++)
            {
                qobs.data[j] = obs[j];
                qobs.n       = qobs.n + 1;
            }
            if (qobs.n > 0)
            {
                EnQueue(&Qrover, qobs);
            }

            // 收到测站数据即计算测站星空图，并发送数据
            // 避免在后面缺基站数据，不进解算，无法计算星空图（阻塞后 无法发送该数）
            for (j = 0; j < MAXSAT; j++)
            {
                svr->rtk.ssat[j].vs         = 0;
                svr->rtk.ssat[j].azel[0][0] = svr->rtk.ssat[j].azel[0][1] = 0.0;
                svr->rtk.ssat[j].azel[1][0] = svr->rtk.ssat[j].azel[1][1] = 0.0;
                svr->rtk.ssat[j].rs[0]                                    = 0.0;
                svr->rtk.ssat[j].rs[1]                                    = 0.0;
                svr->rtk.ssat[j].rs[2]                                    = 0.0;
                svr->rtk.ssat[j].SNR[0] = svr->rtk.ssat[j].SNR[1] = svr->rtk.ssat[j].SNR[2] =
                    svr->rtk.ssat[j].SNR[3] = svr->rtk.ssat[j].SNR[4] = svr->rtk.ssat[j].SNR[5] =
                        0.0;
            }
            for (j = 0; j < n; j++)
            {
                svr->rtk.ssat[obs[j].sat - 1].SNR[0] = obs[j].SNR[0];
                svr->rtk.ssat[obs[j].sat - 1].SNR[1] = obs[j].SNR[1];
                svr->rtk.ssat[obs[j].sat - 1].SNR[2] = obs[j].SNR[2];
                svr->rtk.ssat[obs[j].sat - 1].SNR[3] = obs[j].SNR[3];
                svr->rtk.ssat[obs[j].sat - 1].SNR[4] = obs[j].SNR[4];
                svr->rtk.ssat[obs[j].sat - 1].SNR[5] = obs[j].SNR[5];
            }
            if (!pntpos(0, obs, n, &svr->rtk.sol, NULL, svr->rtk.ssat, &svr->rtk.opt, 0))
            {  // 0:is ok  -1 eoror
                memset(buff, 0, DEBUG_BUFF_LEN);
                pbuff = buff;
                time  = svr->rtk.sol.time;
                if (sopt.times == 3)
                {
                    time = gpst2utc(time);
                    time.time += 3600 * 8;
                }
                time2str(time, s1, 3);
                pbuff += sprintf(pbuff, "sat,%s;", s1);
                generateSatBuf(&svr->rtk, &pbuff);
                pbuff += sprintf(pbuff, "\n");
                strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
            }
            else
            {
                memset(buff, 0, DEBUG_BUFF_LEN);
                pbuff = buff;
                pbuff += sprintf(pbuff, "sat,0;0,0,0,0,0,0,0;");
                pbuff += sprintf(pbuff, "\n");
                strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
            }
        }
        qobs.n = 0;
        while (GetHead(&Qrover))
        {
            qobs = Qrover.data[0];
            for (i = 1; i < Qrover.rear; i++)
            {
                Qrover.data[i - 1] = Qrover.data[i];
            }
            Qrover.rear--;
            n = 0;
            for (i = 0; i < qobs.n; i++)
            {
                obs[n]                               = qobs.data[i];
                svr->rtk.ssat[obs[n].sat - 1].SNR[0] = obs[n].SNR[0];
                svr->rtk.ssat[obs[n].sat - 1].SNR[1] = obs[n].SNR[1];
                svr->rtk.ssat[obs[n].sat - 1].SNR[2] = obs[n].SNR[2];
                svr->rtk.ssat[obs[n].sat - 1].SNR[3] = obs[n].SNR[3];
                svr->rtk.ssat[obs[n].sat - 1].SNR[4] = obs[n].SNR[4];
                svr->rtk.ssat[obs[n].sat - 1].SNR[5] = obs[n].SNR[5];
                n++;
            }

            //-----------------------------find the closest time--------------------
            dtMin      = 9999.9;
            dtMinIndex = -1;
            for (j = 0; j < OBSBASELEN; j++)
            {
                dt[j] = timediff(g_baseObsSync[j][0].time, obs[0].time);
                if (fabs(dt[j]) < fabs(dtMin))
                {
                    dtMin      = dt[j];
                    dtMinIndex = j;
                }
            }
            if (dtMinIndex != -1)
            {
                for (k = 0; k < g_nbaseObsSync[dtMinIndex]; k++)
                {
                    obs[n + k] = g_baseObsSync[dtMinIndex][k];
                }
                n = n + g_nbaseObsSync[dtMinIndex];
            }
            trace(
                0x10, "dt:%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", dt[0], dt[1], dt[2],
                dt[3], dt[4], dt[5], dt[6], dt[7], dt[8], dt[9]
            );

            nobsepoch = 0;
            for (k = 0; k < n; k++)
            {
                if (obs[k].rcv == 1)
                {
                    obsepoch[nobsepoch++] = obs[k];
                }
            }
            for (k = 0; k < nobsPre; k++)
            {
                obsepoch[nobsepoch]     = obsPre[k];
                obsepoch[nobsepoch].rcv = 2;
                nobsepoch++;
            }

            if (obs[0].time.time % ((int)svr->rtk.opt.timeInterval) != 0)
            {
                continue;
            }
            trace(2, "++++++rtkpos start ++++++++++++++++\n"
            rtkReturnValue = rtkpos(&svr->rtk, obs, n);
            trace(2, "++++++rtkpos end   ++++++++++++++++\n"
#ifdef MULBASE
            if (svr->rtk.opt.masterSlaveBaseFlag == 0 && svr->rtk.opt.slaveXyz[0] != 0)
            {
                if (svr->rtk.masterEnu[0] == 0.0 && svr->rtk.masterEnu[1] == 0.0 &&
                    svr->rtk.masterEnu[2] == 0.0)
                {
                    rtkReturnValue = 8;
                }
                if (svr->rtk.masterRr[0] == 0.0 && svr->rtk.masterRr[1] == 0.0 &&
                    svr->rtk.masterRr[2] == 0.0)
                {
                    rtkReturnValue = 9;
                }
                if (svr->rtk.opt.masterXyz[0] == 0.0 && svr->rtk.opt.masterXyz[1] == 0.0 &&
                    svr->rtk.opt.masterXyz[2] == 0.0)
                {
                    rtkReturnValue = 10;
                }
            }
#endif

            if (rtkReturnValue != 0)
            {
#ifdef MULBASE
                if (svr->rtk.opt.masterSlaveBaseFlag == 0 && svr->rtk.opt.slaveXyz[0] != 0)
                {
                    rtkReturnValue += 20;
                }
#endif
                svr->rtk.sol.stat = SOLQ_NONE;
                time              = svr->rtk.sol.time;
                if (sopt.times == 3)
                {
                    time = gpst2utc(time);
                    time.time += 3600 * 8;
                }
                time2str(time, s1, 3);
                /* OUTPUT POS Infomation */
                memset(buff, 0, DEBUG_BUFF_LEN);
                pbuff = buff;
                pbuff += sprintf(
                    pbuff,
                    "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%."
                    "4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,"
                    "%.4lf,%.4lf,%.4lf,%.4lf,%d;",
                    s1, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0
                );
                /* OUTPUT FFT Infomation */
                if (svr->rtk.opt.senceopt == 1)
                {
                    pbuff += sprintf(pbuff, "fft,0.0,0.0;");
                }
                pbuff += sprintf(pbuff, "\n");
                if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                {
                    strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                }

                pbuff = buff;
                sprintf(pbuff, "rtk,%s,failed,%d\n", s1, rtkReturnValue);
                if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                {
                    strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                }

                /* OUTPUT Log Infomation */
            }
            else
            {
                time = svr->rtk.sol.time;
                if (sopt.times == 3)
                {
                    time = gpst2utc(time);
                    time.time += 3600 * 8;
                }
                if (svr->rtk.enuWindwoIndex[0] * (double)svr->rtk.opt.timeInterval ==
                        (double)svr->rtk.opt.smoothWindowsTime * 3600.0 &&
                    rebootFlag == 0)
                {
                    char ackBuff[128] = {0};
                    // rebootFlag=1;
                    trace(
                        4, "enuWindwoIndex:%d,timeInterval:%.2f,smoothWindowsTime:%.2f\n",
                        svr->rtk.enuWindwoIndex[0], svr->rtk.opt.timeInterval,
                        svr->rtk.opt.smoothWindowsTime
                    );
                    sprintf(ackBuff, "ack,update origin\n");
                    strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
                }
                time2str(time, s1, 3);
                if (outsol(NULL, &svr->rtk, &svr->rtk.sol, svr->rtk.rb, &sopt))
                {
                    tt = timediff(svr->rtk.sol.time, svr->rtk.sol.time_pre);
                    if (svr->rtk.sol.rr_pre[0] != 0.0 && svr->rtk.sol.rr_pre[1] != 0.0 &&
                        svr->rtk.sol.rr_pre[2] != 0.0 && tt != 0.0)
                    {
                        for (j = 0; j < 3; j++)
                        {
                            svr->rtk.sol.vel[j] =
                                (svr->rtk.sol.rr[j] - svr->rtk.sol.rr_pre[j]) / tt;
                        }
                    }
                    if (svr->rtk.sol.vel_pre[0] != 0.0 && svr->rtk.sol.vel_pre[1] != 0.0 &&
                        svr->rtk.sol.vel_pre[2] != 0.0 && tt != 0.0)
                    {
                        for (j = 0; j < 3; j++)
                        {
                            svr->rtk.sol.acc[j] =
                                (svr->rtk.sol.vel[j] - svr->rtk.sol.vel_pre[j]) / tt;
                        }
                    }

                    // if (svr->rtk.tt != 0.0) {
                    if (svr->rtk.enuWindwoIndex[0] * (double)svr->rtk.opt.timeInterval >=
                            (svr->rtk.opt.initEnuTime * 3600.0) &&
                        svr->rtk.enuWindwoIndex[1] * (double)svr->rtk.opt.timeInterval >=
                            (svr->rtk.opt.initEnuTime * 3600.0) &&
                        svr->rtk.enuWindwoIndex[2] * (double)svr->rtk.opt.timeInterval >=
                            (svr->rtk.opt.initEnuTime * 3600.0) &&
                        iniEnuFlag == 0)
                    {
                        iniEnuFlag = 1;
                    }
                    //}
                    /* OUTPUT Pos Infomation */
                    memset(buff, 0, DEBUG_BUFF_LEN);
                    pbuff = buff;
                    outDnyResult(svr, &pbuff, s1, iniEnuFlag, 0);
                    if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                    {
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                    fixCnt++;

                    for (j = 0; j < 3; j++)
                    {
                        svr->rtk.sol.rr_pre[j]  = svr->rtk.sol.rr[j];
                        svr->rtk.sol.vel_pre[j] = svr->rtk.sol.vel[j];
                    }
                    svr->rtk.sol.time_pre = svr->rtk.sol.time;
                }
                else
                {
                    /* OUTPUT POS Infomation */
                    memset(buff, 0, DEBUG_BUFF_LEN);
                    pbuff = buff;
                    pbuff += sprintf(
                        pbuff,
                        "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%."
                        "4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.6lf,%.6lf,%."
                        "6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%.4lf,%d;",
                        s1, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0
                    );
                    pbuff += sprintf(pbuff, "\n");
                    if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                    {
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }

                    pbuff = buff;
                    sprintf(pbuff, "rtk,%s,failed,%d\n", s1, 7);
                    if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                    {
                        strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));
                    }
                }
            }

            rtksvrunlock(svr);
            if (svr->rtk.sol.stat != SOLQ_NONE)
            {
                /* adjust current time */
                tt = (int)(tickget() - tick) / 1000.0 + DTTOL;
                timeset(gpst2utc(timeadd(svr->rtk.sol.time, tt)));
            }
            /* if cpu overload, inclement obs outage counter and break */
            //            if ((int)(tickget() - tick) >= svr->cycle) {
            //                svr->prcout += fobs[0] - i - 1;
            // #if 0 /* omitted v.2.4.1 */
            //                break;
            // #endif
            //            }
            // sleepms(1000);

            // trace(4, "time=%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
            //     ntime[0], ntime[1], ntime[2], ntime[3], ntime[4],
            //     ntime[5], ntime[6], ntime[7], ntime[8], ntime[9]);
        }
        /* send null solution if no solution (1hz) */
        if (svr->rtk.sol.stat == SOLQ_NONE && (int)(tick - tick1hz) >= 1000)
        {
            tick1hz = tick;
        }

        if ((cputime = (int)(tickget() - tick)) > 0)
        {
            svr->cputime = cputime;
        }
        fobs[0] = fobs[1] = 0;
        /* sleep until next cycle */
        sleepms(svr->cycle - cputime);
        // sleepms(100);
    }
    free(buff);
    return 0;
}

void split(char* src, const char* separator, char** dest, int* num)
{
    char* pNext;
    char* p;
    int   count = 0;
    if (src == NULL || strlen(src) == 0)
    {
        return;
    }
    if (separator == NULL || strlen(separator) == 0)
    {
        return;
    }
#ifdef WIN32
    pNext = strtok_s(src, separator, &p);
#else
    pNext = strtok_r(src, separator, &p);
#endif
    while (pNext != NULL)
    {
        *dest++ = pNext;
        ++count;
#ifdef WIN32
        pNext = strtok_s(NULL, separator, &p);
#else
        pNext = strtok_r(NULL, separator, &p);
#endif
    }
    *num = count;
}

int main(int argc, char** argv)
{
    solopt_t sopt = solopt_default;
    int      i, len, rw;
    char*    cfgfile;
    char     svrBuff[1024];
    initCfgOpt(&g_cfgOpt);

    printf("version:%d\n", SVN_VERSION);
    InitQueue(&Qrover);
    svr.rtk.mpflag = 0;
    svr.rtk.mvflag = 0;
#if 1
    /*TCP Client Port Input*/
    // printf("argc=%d\n", argc);
    // if (argc < 4)
    //{
    //     printf("please input like:Rtk.out address:port address:port
    //     address:port\n"); return 0;
    // }
    // if (argc > 4) {
    //     printf("intput too many parameter\n");
    //     return 0;
    // }
    // for (i = 1; i < 4; i++) {
    //     cfgfile = argv[i];
    //     strcpy(strpath[i - 1], cfgfile);
    //     printf("%s\n", strpath[i - 1]);
    // }
    /*NTRIP Client Input*/
    // printf("argc=%d\n", argc);
    // if (argc < 4)
    //{
    //     printf("please input like:Rtk.out [user1[:passwd1]@]addr[:port]/mpoint1
    //     [user2[:passwd2]@]addr[:port]/mpoint2
    //     [user3[:passwd3]@]addr[:port]/mpoint3 \n"); return 0;
    // }
    // if (argc > 4) {
    //     printf("intput too many parameter\n");
    //     return 0;
    // }
    // for (i = 1; i < 4; i++) {
    //     cfgfile = argv[i];
    //     strcpy(strpath[i - 1], cfgfile);
    //     printf("%s\n", strpath[i - 1]);
    // }
    /* NTRIP Client and Configuration Parameters Input  */
    printf("argc=%d\n", argc);
    if (argc < 26)
    {
        if (argc == 19)  // Without Configuration Parameters
        {
            for (i = 1; i < 4; i++)
            {
                cfgfile = argv[i];
                strcpy(strpath[i - 1], cfgfile);
                printf("%s\n", strpath[i - 1]);
                strtype[i - 1] = STR_NTRIPCLI;
            }

            cfgfile                    = argv[i++];
            g_cfgOpt.smoothWindowsTime = atof(cfgfile);
            g_cfgOpt.smoothWindowsTime -= 1;
            printf("smoothWindowsTime:%.2f\n", g_cfgOpt.smoothWindowsTime);

            cfgfile               = argv[i++];
            g_cfgOpt.timeInterval = atof(cfgfile);
            printf("timeInterval: % .2f\n", g_cfgOpt.timeInterval);

            cfgfile           = argv[i++];
            g_cfgOpt.senceopt = atoi(cfgfile);
            printf("senceopt:%d\n", g_cfgOpt.senceopt);

            cfgfile            = argv[i++];
            g_cfgOpt.maxPosSat = atoi(cfgfile);
            printf("maxPosSat:%d\n", g_cfgOpt.maxPosSat);

            cfgfile        = argv[i++];
            g_cfgOpt.rb[0] = atof(cfgfile);
            cfgfile        = argv[i++];
            g_cfgOpt.rb[1] = atof(cfgfile);
            cfgfile        = argv[i++];
            g_cfgOpt.rb[2] = atof(cfgfile);
            printf("rb:%14.4f %14.4f %14.4f\n", g_cfgOpt.rb[0], g_cfgOpt.rb[1], g_cfgOpt.rb[2]);

            cfgfile               = argv[i++];
            g_cfgOpt.enuWindow[0] = atof(cfgfile);
            cfgfile               = argv[i++];
            g_cfgOpt.enuWindow[1] = atof(cfgfile);
            cfgfile               = argv[i++];
            g_cfgOpt.enuWindow[2] = atof(cfgfile);
            printf(
                "enuWindow:%14.4f %14.4f %14.4f\n", g_cfgOpt.enuWindow[0], g_cfgOpt.enuWindow[1],
                g_cfgOpt.enuWindow[2]
            );

            cfgfile                    = argv[i++];
            g_cfgOpt.enuWindowIndex[0] = atoi(cfgfile);
            cfgfile                    = argv[i++];
            g_cfgOpt.enuWindowIndex[1] = atoi(cfgfile);
            cfgfile                    = argv[i++];
            g_cfgOpt.enuWindowIndex[2] = atoi(cfgfile);
            printf(
                "enuWindowIndex:%d %d %d\n", g_cfgOpt.enuWindowIndex[0], g_cfgOpt.enuWindowIndex[1],
                g_cfgOpt.enuWindowIndex[2]
            );

            cfgfile                 = argv[i++];
            g_cfgOpt.useRtcmPosFlag = atoi(cfgfile);
            printf("useRtcmPosFlag:%d\n", g_cfgOpt.useRtcmPosFlag);

            // svr.rtk.mpflag = atoi(argv[18]);
            svr.rtk.mvflag = atoi(argv[18]);
            // cfgfile = argv[i++];
            // svr.rtk.masterRr[0] = atof(cfgfile);
            // cfgfile = argv[i++];
            // svr.rtk.masterRr[1] = atof(cfgfile);
            // cfgfile = argv[i++];
            // svr.rtk.masterRr[2] = atof(cfgfile);
            // printf("masterXyz:%.4f %.4f %.4f\n", svr.rtk.masterRr[0],
            // svr.rtk.masterRr[1], svr.rtk.masterRr[2]);

            svr.rtk.opt.smoothWindowsTime = g_cfgOpt.smoothWindowsTime;
            svr.rtk.opt.timeInterval      = g_cfgOpt.timeInterval;
            svr.rtk.opt.senceopt          = g_cfgOpt.senceopt;
            svr.rtk.opt.useRtcmPosFlag    = g_cfgOpt.useRtcmPosFlag;
        }
        else
        {
            // printf("please input like:Rtk.out [user1[:passwd1]@]addr[:port]/mpoint1
            // [user2[:passwd2]@]addr[:port]/mpoint2
            // [user3[:passwd3]@]addr[:port]/mpoint3 \
            // timeInterval freq iono trop tides elevmin cn0min gdopthld diffagemax
            // sys postresthld kmode buffsize stationPCV[3] senceopt smoothWindowsTime
            // initEnuTime\ detectSensitivity typeSol\n");

            printf(
                "please input like:Rtk.out [user1[:passwd1]@]addr[:port]/mpoint1 [user2[:passwd2]@]addr[:port]/mpoint2 [user3[:passwd3]@]addr[:port]/mpoint3 \
            timeInterval freq iono trop tides elevmin cn0min gdopthld diffagemax sys postresthld kmode buffsize stationPCV[3] senceopt smoothWindowsTime initEnuTime\
            detectSensitivity typeSol timeIntervalSolution\n"
            );
            return 0;
        }
    }
    else if (argc > 26)
    {
        printf("input too many parameter\n");
        return 0;
    }
    else  // NTRIP Client With Configuration Parameters
    {
        for (i = 1; i < 4; i++)
        {
            cfgfile = argv[i];
            strcpy(strpath[i - 1], cfgfile);
            printf("%s\n", strpath[i - 1]);
            if (strpath[i - 1][0] == ':')
            {
                strtype[i - 1] = STR_TCPSVR;
            }
            else
            {
                strtype[i - 1] = STR_NTRIPCLI;
            }
        }
        loadCfgOpt(&g_cfgOpt, argv, 3);
    }
#else
    // oFile.fpDebug = fopen("debug.log", "wb+");
#endif

    // int* strs = strtype;
    char*        paths1[]  = {strpath[0], strpath[1], strpath[2]};
    char**       paths     = paths1;
    char         port[256] = "", user[256] = {0}, passwd[256] = {0};
    char         srctbl[MAXSTRPATH] = {0}, mntpntb[256] = {0}, mntpntr[256] = {0};
    double       enuAve[3]    = {0.0};
    char         buf[1024]    = {0}, buff[1024];
    unsigned int enuAveCnt[3] = {0};
    int          m, n;
    char         buffport1[10][124] = {{0}};
    char         buffport2[10][124] = {{0}};
    decodetcppath(paths1[0], NULL, port, user, passwd, mntpntr, srctbl);
    decodetcppath(paths1[1], NULL, port, user, passwd, mntpntb, srctbl);

    m = 0;
    n = 0;
    for (i = 0; i < strlen(mntpntr); i++)
    {
        if (mntpntr[i] == '-')
        {
            m++;
            n = 0;
        }
        else
        {
            buffport1[m][n++] = mntpntr[i];
        }
    }
    m = 0;
    n = 0;
    for (i = 0; i < strlen(mntpntb); i++)
    {
        if (mntpntb[i] == '-')
        {
            m++;
            n = 0;
        }
        else
        {
            buffport2[m][n++] = mntpntb[i];
        }
    }

    if (access("./configFile", 0) != 0)
    {
        createdir("./configFile");
    }
    if (access("./configFile/tcp_log", 0) != 0)
    {
        createdir("./configFile/tcp_log");
    }
    if (access("./rtkLog", 0) != 0)
    {
        createdir("./rtkLog");
    }

    createdir("./projects");
    sprintf(svr.rtk.path, "./projects/%s/", mntpntb);
    createdir(svr.rtk.path);

    if (svr.rtk.mpflag == 1 && g_cfgOpt.enuWindowIndex[0] > 0)
    {
        svr.rtk.mpflag = 2;  // load old mp data
    }
    if (svr.rtk.mpflag != 0)
    {
        mkfpssat(&svr.rtk);
    }

    /* open cycle log module */
    char logfile[1024];
    // sprintf(logfile, "%s/rtk.log", svr.rtk.path);
    sprintf(logfile, "./rtkLog/rtk-%s-%s.log", buffport1[2], buffport2[2]);

    char tracefile[1024] = "";
    sprintf(tracefile, "%s.trace", logfile);
    traceopen(tracefile);
    printf("TRACE LEVEL: %d\n", 4);
    tracelevel(4);

    /* open pos filter */
    svr.rtk.sol.window[0].nmax = (int)2 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[1].nmax = (int)2 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[1].dely = (int)3 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[2].nmax =
        (int)svr.rtk.opt.smoothWindowsTime * 60 * 60 / svr.rtk.opt.timeInterval;

    svr.rtk.sol.window[0].thres[0] = 0.01;  // posmaxstd(0.001, 0.01);  // unit:mm
    svr.rtk.sol.window[0].thres[1] = 0.01;  // posmaxstd(0.001, 0.01);
    svr.rtk.sol.window[0].thres[2] = 0.02;  // posmaxstd(0.002, 0.02);

    svr.rtk.sol.window[1].thres[0] = 0.01;  // posmaxstd(0.001, 0.01);  // unit:mm
    svr.rtk.sol.window[1].thres[1] = 0.01;  // posmaxstd(0.001, 0.01);
    svr.rtk.sol.window[1].thres[2] = 0.02;  // posmaxstd(0.002, 0.02);

    svr.rtk.sol.wdata.nmax =
        (int)svr.rtk.opt.smoothWindowsTime * 60 * 60 / svr.rtk.opt.timeInterval + 1;
    svr.rtk.sol.wdata.mode = FIL;

    char wpospath[256];
    sprintf(wpospath, "%s/wpos.dat", svr.rtk.path);
    init_data(&svr.rtk.sol.wdata, wpospath);

    rtksvrinit(&svr);

    strinit(&svr.stream[0]);
    strinit(&svr.stream[1]);
    strinit(&svr.stream[2]);
    svr.state = 1;
    svr.tick  = tickget();

    setCfgOpt(g_cfgOpt, &(svr.rtk.opt));

    trace_flag[0]  = 1;                  // 0:定位结果
    trace_flag[1]  = svr.rtk.opt.kMode;  // 1:滤波后结果
    trace_flag[2]  = 0;                  // 2：单点定位结果
    trace_flag[3]  = 0;                  // 3：DOP
    trace_flag[4]  = 0;                  // 4：观测量信息
    trace_flag[5]  = 0;                  // 5：卫星位置
    trace_flag[6]  = 0;                  // 6：卫星残差
    trace_flag[7]  = 0;                  // 7：卫星仰角
    trace_flag[8]  = 0;                  // 8：多径
    trace_flag[9]  = 0;                  // 9：模糊度 电离层
    trace_flag[10] = 1;                  // 9：调试信息

    trace_flag[1] = 1;

    svr.rtk.opt.dynamics = 0;
    // svr.rtk.opt.maxgdop = 30.0;
    svr.rtk.opt.mode     = 2;
    svr.rtk.opt.nf       = NFREQ;
    svr.rtk.opt.tidecorr = 1;
    // svr.rtk.opt.elmin = 15.0 * D2R;
    svr.rtk.opt.maxtdiff     = 5.0;
    svr.rtk.opt.std          = 0.01;
    svr.rtk.nx               = 0;
    svr.rtk.sol.bslConstrain = 1;
#ifdef MULBASE
    svr.rtk.opt.masterSlaveBaseFlag = 1;
#endif
    if (svr.rtk.opt.dynamics == 2)
    {
        sopt.outvel = 1;
    }
    if (svr.rtk.opt.smoothWindowsTime == 0)
    {
        svr.rtk.opt.smoothWindowsTime = 24;
    }

    // SELETE_SAT_NUM = svr.rtk.opt.maxPosSat;
    NX = (3 + 2 + SELETE_SAT_NUM + SELETE_SAT_NUM * NFREQ);
    NY = NX;

    svr.rtk.x  = zeros(NX, 1);
    svr.rtk.P  = zeros(NX, NX);
    svr.rtk.xp = zeros(NX, 1);
    svr.rtk.Pp = zeros(NX, NX);
    svr.rtk.I  = zeros(NX, NX);
    svr.rtk.H  = zeros(NY, NX);
    svr.rtk.F  = zeros(NY, NX);
    svr.rtk.K  = zeros(NY, NX);
    svr.rtk.Ri = zeros(NY, 1);
    svr.rtk.Rj = zeros(NY, 1);
    svr.rtk.R  = zeros(NY, NY);
    svr.rtk.v  = zeros(NY, 1);
    iniGloLam();
    if (ROUND(svr.rtk.opt.timeInterval) != 0.0)
    {
        svr.rtk.maxSmoothPoint =
            svr.rtk.opt.smoothWindowsTime * 3600.0 / ROUND(svr.rtk.opt.timeInterval);
    }
    else
    {
        svr.rtk.maxSmoothPoint = 86400;
    }
    if (svr.rtk.opt.senceopt == 1)
    {
        svr.rtk.maxSmoothPoint = 10;
    }

    svr.rtk.cntEnuWind           = 0;
    svr.rtk.maxMedianFilterPoint = 1 * 3600 / svr.rtk.opt.timeInterval + 1;
    svr.rtk.maxMedianFilterPoint =
        svr.rtk.maxMedianFilterPoint > 3601 ? 3601 : svr.rtk.maxMedianFilterPoint;
    for (i = 0; i < 3; i++)
    {
        if (!(svr.rtk.enuWindow[i] = (double*)calloc(svr.rtk.maxSmoothPoint, sizeof(double))))
        {
            return 0;
        }
        if (!(svr.rtk.enuWindowMedian[i] =
                  (double*)calloc(svr.rtk.maxMedianFilterPoint, sizeof(double))))
        {
            return 0;
        }
        svr.rtk.enuWindowMedianShiftNum[i] = 0;
    }
    for (i = 0; i < 3; i++)
    {
        svr.rtk.rb[i] = svr.rtk.opt.rb[i];
    }

    enuAve[0]    = svr.rtk.opt.enuWindow[0];
    enuAve[1]    = svr.rtk.opt.enuWindow[1];
    enuAve[2]    = svr.rtk.opt.enuWindow[2];
    enuAveCnt[0] = svr.rtk.opt.enuWindowIndex[0];
    enuAveCnt[1] = svr.rtk.opt.enuWindowIndex[1];
    enuAveCnt[2] = svr.rtk.opt.enuWindowIndex[2];

    svr.rtk.sol.ori_ave[0] = enuAve[0];
    svr.rtk.sol.ori_ave[1] = enuAve[1];
    svr.rtk.sol.ori_ave[2] = enuAve[2];
    svr.rtk.sol.ori_var[0] = 0.02 * 0.02;
    svr.rtk.sol.ori_var[1] = 0.02 * 0.02;
    svr.rtk.sol.ori_var[2] = 0.05 * 0.05;

    if (enuAveCnt[0] > svr.rtk.maxSmoothPoint)
    {
        enuAveCnt[0] = svr.rtk.maxSmoothPoint;
    }
    if (enuAveCnt[1] > svr.rtk.maxSmoothPoint)
    {
        enuAveCnt[1] = svr.rtk.maxSmoothPoint;
    }
    if (enuAveCnt[2] > svr.rtk.maxSmoothPoint)
    {
        enuAveCnt[2] = svr.rtk.maxSmoothPoint;
    }
    svr.rtk.enuWindwoIndex[0] = enuAveCnt[0];
    svr.rtk.enuWindwoIndex[1] = enuAveCnt[1];
    svr.rtk.enuWindwoIndex[2] = enuAveCnt[2];

    if (svr.rtk.opt.senceopt == 1)
    {
        enuAveCnt[0] = enuAveCnt[1] = enuAveCnt[2] = 10;
    }
    for (i = 0; i < enuAveCnt[0]; i++)
    {
        svr.rtk.enuWindow[0][i] = enuAve[0];
    }
    svr.rtk.sum_enu[0]   = enuAve[0] * enuAveCnt[0];
    svr.rtk.sum_sqeun[0] = enuAve[0] * enuAve[0] * enuAveCnt[0];

    for (i = 0; i < enuAveCnt[1]; i++)
    {
        svr.rtk.enuWindow[1][i] = enuAve[1];
    }
    svr.rtk.sum_enu[1]   = enuAve[1] * enuAveCnt[1];
    svr.rtk.sum_sqeun[1] = enuAve[1] * enuAve[1] * enuAveCnt[1];

    for (i = 0; i < enuAveCnt[2]; i++)
    {
        svr.rtk.enuWindow[2][i] = enuAve[2];
    }
    svr.rtk.sum_enu[2]   = enuAve[2] * enuAveCnt[2];
    svr.rtk.sum_sqeun[2] = enuAve[2] * enuAve[2] * enuAveCnt[2];

    if (enuAveCnt[0] == svr.rtk.maxSmoothPoint)
    {
        rebootFlag = 1;
    }
    else
    {
        rebootFlag = 0;
    }

    svr.rtk.iniCnt = 0;

    sopt.posf    = 2;  // 0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
    sopt.times   = 3;  // 0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间
    sopt.outvel  = 0;
    sopt.outhead = 0;

    strinitcom();  // 初始化网络
    memset(configFileFath, 0, MAXSTRPATH);

    strcpy(logFileSizeName, "rtk.conf");
    fpcof = fopen(logFileSizeName, "r");

    if (fpcof == NULL)
    {
        fpcof = fopen(logFileSizeName, "w");
        if (fpcof == NULL)
        {
            printf("fopen fpcof error\n");
            return 0;
        }
        fprintf(fpcof, "logFileSize:%.0f\n", logFileSize);
        fprintf(fpcof, "writeConfigTime:%.0f\n", writeConfigTime);
        fprintf(fpcof, "writeDugTime:%.0f\n", writeDugTime);
        fprintf(fpcof, "level_trace:%d\n", level_trace);
        fflush(fpcof);
    }
    else
    {
        while (fgets(buff, 1024, fpcof))
        {
            if (!strncmp(buff, "logFileSize", strlen("logFileSize")))
            {
                sscanf(buff, "logFileSize:%lf", &logFileSize);
                printf("------------logFileSize=%.2f----------------\n", logFileSize);
            }
            if (!strncmp(buff, "writeConfigTime", strlen("writeConfigTime")))
            {
                sscanf(buff, "writeConfigTime:%lf", &writeConfigTime);
                printf("------------writeConfigTime=%.2f----------------\n", writeConfigTime);
            }
            if (!strncmp(buff, "writeDugTime", strlen("writeDugTime")))
            {
                sscanf(buff, "writeDugTime:%lf", &writeDugTime);
                printf("------------writeDugTime=%.2f----------------\n", writeDugTime);
            }
            if (!strncmp(buff, "level_trace", strlen("level_trace")))
            {
                sscanf(buff, "level_trace:%d", &m);
                printf("------------level_trace=%d----------------\n", m);
                level_trace = (m > 7 ? 0xFF : (0xFF >> (8 - m)));
            }
        }
    }
    fclose(fpcof);

    for (i = 0; i < 3; i++)
    {
        rw = i < 2 ? STR_MODE_R : STR_MODE_RW;
        if (!stropen(svr.stream + i, strtype[i], rw, paths[i]))
        {
            printf("open stream error:%s\n", paths[i]);
            return 0;
        }
    }

    for (i = 0; i < 2; i++)
    {
        init_rtcm(svr.rtcm + i);
        svr.nb[i] = svr.npb[i] = 0;
        if (!(svr.buff[i] = (unsigned char*)calloc(sizeof(unsigned char), BUFFSIZE)) ||
            !(svr.pbuf[i] = (unsigned char*)calloc(sizeof(unsigned char), BUFFSIZE)))
        {
            return 0;
        }
    }
    //----------------------malloc preBaseObs---------
    g_preBaseObsRtkNum = 0;
    /* create rtk server thread */
#ifdef WIN32
    if (!(svr.thread = CreateThread(NULL, 0, rtksvrthread, &svr, 0, NULL)))
    {
#else
    if (pthread_create(&svr.thread, NULL, rtksvrthread, &svr))
    {
#endif
        for (i = 0; i < 3; i++)
        {
            strclose(svr.stream + i);
        }
        printf("thread1 create error\n");
        return 0;
    }

#ifdef WIN32
    WaitForSingleObject(svr.thread, INFINITE);
    CloseHandle(svr.thread);
#else
    pthread_join(svr.thread, NULL);
#endif
    rtksvrfree(&svr);
    free_data(&svr.rtk.sol.wdata);

    for (i = 0; i < 3; i++)
    {
        strclose(&svr.stream[i]);
    }
    for (i = 0; i < 3; i++)
    {
        if (svr.rtk.enuWindow[i])
        {
            free(svr.rtk.enuWindow[i]);
        }
    }
    printf("rtk thread return\n");
    return 1;
}
