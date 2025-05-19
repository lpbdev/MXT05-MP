#include <stdlib.h>
#include "rtk.h"
#include "multipath.h"
#include "logmod.h"
#include "cJSON.h"

#ifndef WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#define __USE_MISC
#ifndef CRTSCTS
#define CRTSCTS  020000000000
#endif
#include <errno.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#ifdef _MSC_VER
#include"../src/include/dirent_bak.h"
#else
//#include <dirent.h>
#endif

rtksvr_t svr;
nav_t g_nav = { 0 };
myFile_t oFile = { 0 };
cfgopt_t g_cfgOpt = { 0 };
unsigned char trace_flag[64] = { 0 };
unsigned char rtkReturnValue;
FILE* fptcp = NULL;
FILE* fpcof = NULL;
char configFileFath[MAXSTRPATH] = { 0 };
char tcpFileFath[MAXSTRPATH] = { 0 };

unsigned char rtcmMode = 2;
int g_week;
char debugFile[1024] = { 0 };
char logFileSizeName[1024] = { 0 };
double logFileSize = 100;
double writeConfigTime = 1;
double writeDugTime = 1;

#define OBSBASELEN    30
obsd_t g_baseObsSync[OBSBASELEN][MAXOBS];
unsigned char g_nbaseObsSync[OBSBASELEN] = { 0 };
unsigned char g_baseObsSyncIndex = 255;
unsigned char g_baseObsBuffFull = 0;

double g_gpsLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5 };
double g_galLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ7, CLIGHT / FREQ5 };
double g_bdsLam[NFREQ] = { CLIGHT / FREQ1_CMP, CLIGHT / FREQ2_CMP, CLIGHT / FREQB2a_CMP,CLIGHT / FREQB2b_CMP,CLIGHT / FREQB1C_CMP,CLIGHT / FREQ3_CMP };

double g_gloLam[MAXPRNGLO][NFREQ] = { 0.0 };
unsigned char streamBase;
obsd_t g_preBaseObsRtk[MAXOBS];
int g_preBaseObsRtkNum;
unsigned char level_trace = 0xff;
double baseRtcmPosition[3] = { 0 };

int SELETE_SAT_NUM = 20;
int NX;
int NY;

#define BUFFSIZE 32768
#define NTRIP_MAXRSP        32768       /* max size of ntrip response */
#define NTRIP_MAXSTR        256         /* max length of mountpoint string */

#ifdef WIN32
#define dev_t               HANDLE
#define socket_t            SOCKET
typedef int socklen_t;
#else
#define dev_t               int
#define socket_t            int
#define closesocket         close
#endif



typedef struct {            /* file control type */
    FILE* fp;               /* file pointer */
    FILE* fp_tag;           /* file pointer of tag file */
    FILE* fp_tmp;           /* temporary file pointer for swap */
    FILE* fp_tag_tmp;       /* temporary file pointer of tag file for swap */
    char path[MAXSTRPATH];  /* file path */
    char openpath[MAXSTRPATH]; /* open file path */
    int mode;               /* file mode */
    int timetag;            /* time tag flag (0:off,1:on) */
    int repmode;            /* replay mode (0:master,1:slave) */
    int offset;             /* time offset (ms) for slave */
    int size_fpos;          /* file position size (bytes) */
    gtime_t time;           /* start time */
    gtime_t wtime;          /* write time */
    uint32_t tick;          /* start tick */
    uint32_t tick_f;        /* start tick in file */
    long fpos_n;            /* next file position */
    uint32_t tick_n;        /* next tick */
    double start;           /* start offset (s) */
    double speed;           /* replay speed (time factor) */
    double swapintv;        /* swap interval (hr) (0: no swap) */
    lock_t lock;            /* lock flag */
} file_t;


int strtype[3] = { STR_FILE,STR_FILE,SYS_NONE };   //STR_TCPCLI STR_TCPSVR
int format[3] = { STRFMT_RTCM3,STRFMT_RTCM3,STRFMT_RTCM3 };
char strpath[3][1024] = { "rover.txt","base.txt","" };

extern void rtksvrlock(rtksvr_t* svr) { lock(&svr->lock); }
extern void rtksvrunlock(rtksvr_t* svr) { unlock(&svr->lock); }

obsqueue_t Qrover;
int InitQueue(obsqueue_t* Q)
{
    Q->rear = -1;
    return  1;
}

int GetHead(obsqueue_t* Q)
{
    int i, j, k;
    if (Q->rear <= 0)
        return 0;
    //printf("GetHead rear=%d time=%d n=%d\n", Q->rear, Q->data[0].data[0].time.time, Q->data[0].n);
    if (g_baseObsSyncIndex == 255)
        return 0;
    if (Q->data[0].data[0].time.time > g_baseObsSync[g_baseObsSyncIndex - 1][0].time.time)
        return 0;
    return 1;
}
int EnQueue(obsqueue_t* Q, qobs_t e)
{
    int i;
    if (Q->rear == MAXQUEUESIZE || Q->rear == -1) {
        Q->rear = 0;
    }
    Q->data[Q->rear] = e;
    Q->rear++;
    //printf("EnQueue rear=%d time=%d n=%d\n", Q->rear,e.data[0].time.time, e.n);
    //printf("-----------------\n");
    //for (i = 0; i < Q->rear; i++) {
    //    printf("EnQueue time=%d\n", Q->data[i].data[0].time.time);
    //}
    //printf("*****************\n");
    return  1;
}

/* compare observation data -------------------------------------------------*/
static int cmpobs(const void* p1, const void* p2)
{
    obsd_t* q1 = (obsd_t*)p1, * q2 = (obsd_t*)p2;
    double tt = timediff(q1->time, q2->time);
    if (fabs(tt) > DTTOL) return tt < 0 ? -1 : 1;
    if (q1->rcv != q2->rcv) return (int)q1->rcv - (int)q2->rcv;
    return (int)q1->sat - (int)q2->sat;
}

static int sortobs(obs_t* obs)
{
    int i, j, n;

    //trace(3, "sortobs: nobs=%d\n", obs->n);

    if (obs->n <= 0) return 0;

    qsort(obs->data, obs->n, sizeof(obsd_t), cmpobs);

    /* delete duplicated data */
    for (i = j = 0; i < obs->n; i++) {
        if (obs->data[i].sat != obs->data[j].sat ||
            obs->data[i].rcv != obs->data[j].rcv ||
            timediff(obs->data[i].time, obs->data[j].time) != 0.0) {
            obs->data[++j] = obs->data[i];
        }
    }
    obs->n = j + 1;

    for (i = n = 0; i < obs->n; i = j, n++) {
        for (j = i + 1; j < obs->n; j++) {
            if (timediff(obs->data[j].time, obs->data[i].time) > DTTOL) break;
        }
    }
    return n;
}

static void updatesvr(rtksvr_t* svr, int ret, obs_t* obs, int index, int iobs)
{
    double pos[3], del[3] = { 0 }, dr[3];
    int i, n = 0;

    //tracet(4, "updatesvr: ret=%d sat=%2d index=%d\n", ret, sat, index);

    if (ret == 1) { /* observation data */
        if (iobs < 128) {
            for (i = 0; i < obs->n; i++) {
                //if(obs->data[i].SNR[0]<40*4)continue;
                svr->obs[index][iobs].data[n] = obs->data[i];
                svr->obs[index][iobs].data[n].rcv = index + 1;
                n++;
            }
            svr->obs[index][iobs].n = n;
            sortobs(&svr->obs[index][iobs]);
        }
        //svr->nmsg[index][0]++;
    }
    else if (ret == 5 && svr->rtk.opt.useRtcmPosFlag == 1) { /* antenna postion parameters */
        if (index == 1) {
            for (i = 0; i < 3; i++) {
                svr->rtk.rb[i] = svr->rtcm[1].sta.pos[i];
                baseRtcmPosition[i] = svr->rtcm[1].sta.pos[i];
            }
            //        /* antenna delta */
            ecef2pos(svr->rtk.rb, pos);
            if (svr->rtcm[1].sta.deltype) { /* xyz */
                del[2] = svr->rtcm[1].sta.hgt;
                enu2ecef(pos, del, dr);
                for (i = 0; i < 3; i++) {
                    svr->rtk.rb[i] += svr->rtcm[1].sta.del[i] + dr[i];
                }
            }
            else { /* enu */
                enu2ecef(pos, svr->rtcm[1].sta.del, dr);
                for (i = 0; i < 3; i++) {
                    svr->rtk.rb[i] += dr[i];
                }
            }
        }
    }
}
extern int decoderaw(rtksvr_t* svr, int index)
{
    obs_t* obs = NULL;
    int i, ret = 0, fobs = 0;
    unsigned char buff;
    svr->format[index] = STRFMT_RTCM3;
    file_t* fp_rtcmfile = (file_t*)svr->stream[index].port;
    svr->rtcm[index].obsflag = 0;
    while (!svr->rtcm[index].obsflag) {
        if (svr->format[index] == STRFMT_RTCM3) {
            svr->rtcm[index].rcv = index;
            buff = fgetc(fp_rtcmfile->fp);
            if (feof(fp_rtcmfile->fp)) return  0;
            ret = input_rtcm3(svr->rtcm + index, buff);
            obs = &svr->rtcm[index].obs;
            if (ret != 0) {
                //printf("decode rtcm3 index=%d ok\n",index);
            }
        }
        /* update rtk server */
        if (ret > 0)
            updatesvr(svr, ret, obs, index, fobs);
        /* observation data received */
        if (ret == 1) {
            if (fobs < 128) {
                fobs++;
            }
            else svr->prcout++;
        }
    }
    svr->nb[index] = 0;
    return fobs;
}

static void free_rtcm(rtcm_t* rtcm)
{

    /* free memory for observation and ephemeris buffer */
    free(rtcm->obs.data); rtcm->obs.data = NULL; rtcm->obs.n = 0;
}

extern int init_rtcm(rtcm_t* rtcm)
{
    gtime_t time0 = { 0 };
    obsd_t data0 = { { 0 } };
    int i;

    rtcm->staid = rtcm->stah = rtcm->seqno = rtcm->outtype = 0;
    rtcm->time = rtcm->time_s = time0;
    rtcm->sta.name[0] = rtcm->sta.marker[0] = '\0';
    rtcm->sta.antdes[0] = rtcm->sta.antsno[0] = '\0';
    rtcm->sta.rectype[0] = rtcm->sta.recver[0] = rtcm->sta.recsno[0] = '\0';
    rtcm->sta.antsetup = rtcm->sta.itrf = rtcm->sta.deltype = 0;
    for (i = 0; i < 3; i++) {
        rtcm->sta.pos[i] = rtcm->sta.del[i] = 0.0;
    }
    rtcm->sta.hgt = 0.0;
    //rtcm->dgps = NULL;
    //for (i = 0; i<MAXSAT; i++) {
    //    rtcm->ssr[i] = ssr0;
    //}
    rtcm->msg[0] = rtcm->msgtype[0] = rtcm->opt[0] = '\0';
    rtcm->obsflag = rtcm->ephsat = 0;

    rtcm->nbyte = rtcm->nbit = rtcm->len = 0;
    rtcm->word = 0;

    rtcm->obs.data = NULL;

    /* reallocate memory for observation and ephemris buffer */
    if (!(rtcm->obs.data = (obsd_t*)calloc(sizeof(obsd_t) , MAXOBS))) {
        free_rtcm(rtcm);
        return 0;
    }
    rtcm->obs.n = 0;
    for (i = 0; i < MAXOBS; i++) rtcm->obs.data[i] = data0;
    return 1;
}
static void rtksvrfree(rtksvr_t* svr)
{
    int i, j;
    if (g_nav.eph)
        free(g_nav.eph);
    if (g_nav.geph)
        free(g_nav.geph);
    for (i = 0; i < 2; i++) {
        free(svr->buff[i]);
        free(svr->pbuf[i]);
        free_rtcm(&svr->rtcm[i]);
    }
    for (i = 0; i < 2; i++) for (j = 0; j < 128; j++) {
        free(svr->obs[i][j].data);
    }
    free(svr->rtk.x); free(svr->rtk.P); free(svr->rtk.xp); free(svr->rtk.Pp); free(svr->rtk.I); free(svr->rtk.H);
    free(svr->rtk.F); free(svr->rtk.K); free(svr->rtk.Ri); free(svr->rtk.Rj); free(svr->rtk.R); free(svr->rtk.v);
    //rtkfree(&svr->rtk);
}
extern int rtksvrinit(rtksvr_t* svr)
{
    eph_t  eph0 = { 0,-1,-1 };
    geph_t geph0 = { 0,-1 };
    int i, j;
    for (i = 0; i < 2; i++) svr->format[i] = 0;
    for (i = 0; i < 2; i++) svr->nb[i] = 0;

    printf("calloc eph_t %d\n",sizeof(eph_t)*MAXSAT);
    printf("calloc obsd_t %d\n",sizeof(obsd_t)*MAXOBS*2*128);
    printf("calloc double %d\n",sizeof(double)*MAXSAT*3*MAXSAT*3);


    if (!(g_nav.eph = (eph_t*)calloc(sizeof(eph_t) , MAXSAT)) || !(g_nav.geph = (geph_t*)calloc(sizeof(geph_t) , NSATGLO))) {
        ////trace(1, "rtksvrinit: calloc error\n");
        return 0;
    }

    g_nav.n = MAXSAT;
    g_nav.ng = NSATGLO;
    svr->navsel = 1;

    for (i = 0; i < 2; i++) for (j = 0; j < 128; j++) {
        if (!(svr->obs[i][j].data = (obsd_t*)calloc(sizeof(obsd_t), MAXOBS))) {
            printf("rtksvrinit: calloc error\n");
            return 0;
        }
    }
    for (i = 0; i < 2; i++) {
        memset(svr->rtcm + i, 0, sizeof(rtcm_t));
    }
    initlock(&svr->lock);
    return 1;
}
//Initialize Configuration Option for User Setting
static void initCfgOpt(cfgopt_t* opt)
{
    opt->kMode = 1;   /* Kinematic Calculation Mode Smoothed Real-Time Kinematic */
    opt->freq = 15;    /* L1+L2+L5=1+2+4  7:all freq*/
    opt->iono = 0;    /* disable Ionosphere Correction */
    opt->trop = 0;    /* enable Troposphere Correction */
    opt->tides = 0;   /* disable Tides Correction */
    opt->sys = 31;    /* GPS+QZS+BDS+GAL+GLO 0001 1111*/
    opt->cn0Min = 25;    /* C/N0 Cut-off */
    opt->diffAgeMax = 30; /* Max Age of Diff , unit:s */
    opt->buffSize = 4; /*Buffer Size Default:4kB */
    opt->elevMin = 15;   /* Elevation Cut-off */
    opt->gdopThld = 4.0; /* GDOP Threshold */
    opt->postResThld = 0.02; /* Posterior Residual Threshold*/
    opt->timeInterval = 15.0;   /* Time Interval 1s */
    opt->stationPCV[0] = 0.0;
    opt->stationPCV[1] = 0.0;
    opt->stationPCV[2] = 0.0;
    opt->senceopt = 0;
    opt->initEnuTime = 1;
    opt->smoothWindowsTime = 12;
    opt->detectSensitivity = 10;
    opt->typeSol = 0;
    opt->timeIntervalSolution = 0;

    opt->minFixSat = 10;
    opt->maxDelSat = 5;
    opt->minSatRes = 0.02;
    opt->gpsMask = -1;
    opt->qzssMask = -1;
    opt->glonassMask = -1;
    opt->galieoMask = -1;
    opt->bdsMask = -1;
    opt->iggiiik0 = 1.5;
    opt->iggiiik1 = 3.0;
}

static void loadCfgOpt(cfgopt_t* cfgOpt, char** argv, int i)
{
    char* cfgfile;
    cfgfile = argv[i++];

    cfgfile = argv[i++];
    cfgOpt->timeInterval = atof(cfgfile);
    printf("timeInterval:%.2f\n", cfgOpt->timeInterval);

    cfgfile = argv[i++];
    cfgOpt->freq = atoi(cfgfile);
    printf("freq:%d\n", cfgOpt->freq);

    cfgfile = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("iono:%d\n", cfgOpt->iono);
    cfgfile = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("trop:%d\n", cfgOpt->trop);
    cfgfile = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("tides:%d\n", cfgOpt->tides);

    cfgfile = argv[i++];
    cfgOpt->elevMin = atof(cfgfile);
    printf("elevMin:%.2f\n", cfgOpt->elevMin);

    cfgfile = argv[i++];
    cfgOpt->cn0Min = atof(cfgfile);
    printf("cn0Min:%.2f\n", cfgOpt->cn0Min);

    cfgfile = argv[i++];
    cfgOpt->gdopThld = atof(cfgfile);
    printf("gdopThld:%.2f\n", cfgOpt->gdopThld);

    cfgfile = argv[i++];
    cfgOpt->diffAgeMax = atoi(cfgfile);
    printf("diffAgeMax:%d\n", cfgOpt->diffAgeMax);

    cfgfile = argv[i++];
    cfgOpt->sys = atoi(cfgfile);
    printf("sys:%d\n", cfgOpt->sys);


    cfgfile = argv[i++];
    cfgOpt->postResThld = atof(cfgfile);
    printf("postResThld:%.2f\n", cfgOpt->postResThld);

    cfgfile = argv[i++];
    cfgOpt->kMode = atoi(cfgfile);
    printf("kMode:%d\n", cfgOpt->kMode);

    cfgfile = argv[i++];
    cfgOpt->buffSize = atoi(cfgfile);
    printf("buffSize:%d\n", cfgOpt->buffSize);

    cfgfile = argv[i++];
    cfgOpt->stationPCV[0] = atoi(cfgfile);
    cfgfile = argv[i++];
    cfgOpt->stationPCV[1] = atoi(cfgfile);
    cfgfile = argv[i++];
    cfgOpt->stationPCV[2] = atoi(cfgfile);
    printf("stationPCV[0]=%.2f stationPCV[1]=%.2f stationPCV[2]=%.2f\n", cfgOpt->stationPCV[0], cfgOpt->stationPCV[1], cfgOpt->stationPCV[2]);

    cfgfile = argv[i++];
    cfgOpt->senceopt = atoi(cfgfile);
    printf("senceopt=%d\n", cfgOpt->senceopt);

    cfgfile = argv[i++];
    cfgOpt->smoothWindowsTime = atof(cfgfile);
    printf("smoothWindowsTime=%.2f\n", cfgOpt->smoothWindowsTime);

    cfgfile = argv[i++];
    cfgOpt->initEnuTime = atof(cfgfile);
    printf("initEnuTime=%.2f\n", cfgOpt->initEnuTime);

    cfgfile = argv[i++];
    cfgOpt->detectSensitivity = atoi(cfgfile);
    printf("detectSensitivity=%d\n", cfgOpt->detectSensitivity);

    cfgfile = argv[i++];
    cfgOpt->typeSol = atoi(cfgfile);
    printf("typeSol=%d\n", cfgOpt->typeSol);

    cfgfile = argv[i++];
    cfgOpt->timeIntervalSolution = atoi(cfgfile);
    printf("timeIntervalSolution=%d\n", cfgOpt->timeIntervalSolution);

}

//Set Configuration Option for User Setting to Processing Option
static void setCfgOpt(cfgopt_t cfgOpt, prcopt_t* procOpt)
{
    procOpt->kMode = cfgOpt.kMode;
    procOpt->freq = cfgOpt.freq;

    procOpt->ioncfg = cfgOpt.iono;
    procOpt->trocfg = cfgOpt.trop;

    if (cfgOpt.iono == 0)
        procOpt->ionoopt = IONOOPT_BRDC;
    else if (cfgOpt.iono == 1)
        procOpt->ionoopt = IONOOPT_EST;
    else
        procOpt->ionoopt = IONOOPT_BRDC;

    if (cfgOpt.trop == 0)
        procOpt->tropopt = TROPOPT_SAAS;
    else if (cfgOpt.trop == 1)
        procOpt->tropopt = TROPOPT_EST;
    else
        procOpt->ionoopt = TROPOPT_SAAS;

    procOpt->tidecorr = cfgOpt.tides;
    procOpt->sys = cfgOpt.sys;
    procOpt->cn0Min = cfgOpt.cn0Min;
    procOpt->buffSize = cfgOpt.buffSize * 1024;
    procOpt->elmin = cfgOpt.elevMin * D2R;
    procOpt->maxtdiff = cfgOpt.diffAgeMax;
    procOpt->maxgdop = cfgOpt.gdopThld;
    procOpt->postResThld = cfgOpt.postResThld;
    procOpt->senceopt = cfgOpt.senceopt;
    procOpt->timeInterval = cfgOpt.timeInterval;
    procOpt->smoothWindowsTime = cfgOpt.smoothWindowsTime;
    procOpt->initEnuTime = cfgOpt.initEnuTime;
    procOpt->detectSensitivity = cfgOpt.detectSensitivity;
    procOpt->typeSol = cfgOpt.typeSol;
    procOpt->timeIntervalSolution = cfgOpt.timeIntervalSolution;

    procOpt->minFixSat = cfgOpt.minFixSat;
    procOpt->maxDelSat = cfgOpt.maxDelSat;
    procOpt->minSatRes = cfgOpt.minSatRes;
    procOpt->gpsMask = cfgOpt.gpsMask;
    procOpt->qzssMask = cfgOpt.qzssMask;
    procOpt->glonassMask = cfgOpt.glonassMask;
    procOpt->galieoMask = cfgOpt.galieoMask;
    procOpt->bdsMask = cfgOpt.bdsMask;
    procOpt->iggiiik0 = cfgOpt.iggiiik0;
    procOpt->iggiiik1 = cfgOpt.iggiiik1;
    //time interval
}

static void generateSatBuf(rtk_t* rtk, char** p)
{
    unsigned char i, j;
    unsigned char sys;
    unsigned char prn;
    double azel[2] = { 0.0 };
    double CN0[NFREQ] = { 0.0 };
    double r = 0.0;
    double e[3] = { 0.0 };
    double pos[3] = { 0.0 };
    //*p += sprintf(*p, "sat,");

    for (i = 0; i < MAXSAT; i++)
    {
        if (rtk->ssat[i].rs[0] != 0)
        {
            sys = satsys(i + 1, &prn);
            azel[1] = rtk->ssat[i].azel[0][0] * R2D;
            azel[0] = rtk->ssat[i].azel[0][1] * R2D;
            for (j = 0; j < NFREQ; j++)
            {
                CN0[j] = rtk->ssat[i].SNR[j] / 4.0;
            }

            if (CN0[0] != 0 || CN0[1] != 0 || CN0[2] != 0 || CN0[3] != 0 || CN0[4] != 0 || CN0[5] != 0)
            {
                if (azel[0] == 0 || azel[1] == 0)
                {
                    if ((r = geodist(rtk->ssat[i].rs, rtk->sol.rr, e)) <= 0)  continue;
                    ecef2pos(rtk->sol.rr, pos);
                    if (satazel(pos, e, azel) < rtk->opt.elmin)  continue;
                    azel[0] *= R2D;
                    azel[1] *= R2D;
                }
                //sat,卫星类型,卫星号,仰角,方位角,信噪比;repeated /r/n
                if (sys == SYS_GPS)
                {
                    *p += sprintf(*p, "0,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1], CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]);
                }
                else if (sys == SYS_QZS)
                {
                    *p += sprintf(*p, "1,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1], CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]);
                }
                else if (sys == SYS_BDS)
                {
                    *p += sprintf(*p, "2,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1], CN0[0], CN0[1], CN0[5], CN0[4], CN0[2], CN0[3]);
                }
                else if (sys == SYS_GAL)
                {
                    *p += sprintf(*p, "3,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1], CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]);
                }
                else if (sys == SYS_GLO)
                {
                    *p += sprintf(*p, "4,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1], CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]);
                }
            }
        }
    }
    //*p += sprintf(*p, "\n");
    return;
}

static void outDnyResult(rtksvr_t* svr, char** pbuff, char* s1, unsigned char iniEnuFlag) {
    if (svr->rtk.opt.kMode == 0) {
        *pbuff += sprintf(*pbuff, "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%.4lf,%d;",
            s1,
            svr->rtk.sol.stat,
            svr->rtk.sol.ns[0],
            svr->rtk.sol.rr_original[0], svr->rtk.sol.rr_original[1], svr->rtk.sol.rr_original[2],
            svr->rtk.sol.enu_original[0], svr->rtk.sol.enu_original[1], svr->rtk.sol.enu_original[2],
            svr->rtk.sol.rr_filer[0], svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2],
            svr->rtk.sol.enu[0], svr->rtk.sol.enu[1], svr->rtk.sol.enu[2],
            svr->rtk.sol.vel[0], svr->rtk.sol.vel[1], svr->rtk.sol.vel[2],
            svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
            svr->rtk.rb[0], svr->rtk.rb[1], svr->rtk.rb[2],
            iniEnuFlag);
    }
    else {
        *pbuff += sprintf(*pbuff, "pos,%s,%d,%d,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.4lf,%.4lf,%.4lf,%d;",
            s1,
            svr->rtk.sol.stat, svr->rtk.sol.ns[0],
            svr->rtk.sol.rr_filer[0], svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2],
            svr->rtk.sol.enu[0], svr->rtk.sol.enu[1], svr->rtk.sol.enu[2],
            svr->rtk.sol.rr_filer[0], svr->rtk.sol.rr_filer[1], svr->rtk.sol.rr_filer[2],
            svr->rtk.sol.enu[0], svr->rtk.sol.enu[1], svr->rtk.sol.enu[2],
            svr->rtk.sol.vel[0], svr->rtk.sol.vel[1], svr->rtk.sol.vel[2],
            svr->rtk.sol.acc[0], svr->rtk.sol.acc[1], svr->rtk.sol.acc[2],
            svr->rtk.rb[0], svr->rtk.rb[1], svr->rtk.rb[2],
            iniEnuFlag);
    }
    /* OUTPUT FFT Infomation */
    if (svr->rtk.opt.senceopt == 1) {
        *pbuff += sprintf(*pbuff, "fft,%.4f,%.4f;", svr->rtk.fftFrq[2], svr->rtk.fftPower[2]);
    }
    *pbuff += sprintf(*pbuff, "\n");
}
static int decodeConfig(rtksvr_t* svr, cfgopt_t* cfgOpt, char* buff) {

    int i;
    cJSON* json = NULL;
    cJSON* json_data = NULL;
    cJSON* json_data_tmp = NULL;
    char ackBuff[4096] = { 0 };
    char idStr[64] = { 0 };
    //printf("%s\n", buff);
    buff = strstr(buff, "{");
    printf("%s\n", buff);
    json = cJSON_Parse(buff);

    if (NULL == json)
    {
        printf("cJSON_Parse error:%s\n", cJSON_GetErrorPtr());
        return 0;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "taskID");
    if (json_data_tmp == NULL) {
        printf("timeInterval json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,ID,failed;\n");
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    strcpy(idStr, json_data_tmp->valuestring);


    //json_data_tmp = cJSON_GetObjectItem(json, "timeInterval");
    //if (json_data_tmp == NULL) {
    //    printf("timeInterval json_data_tmp is null\n");
    //    cJSON_Delete(json);
    //    sprintf(ackBuff, "ack,%s,timeInterval,failed;\n", idStr);
    //    strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    //    return 0;
    //}
    //cfgOpt->timeInterval = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "freq");
    if (json_data_tmp == NULL) {
        printf("freq json_data_tmp is null\n");
        cJSON_Delete(json);
        return 0;
    }
    cfgOpt->freq = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "iono");
    if (json_data_tmp == NULL) {
        printf("iono json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,iono,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->iono = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "trop");
    if (json_data_tmp == NULL) {
        printf("trop json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,trop,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->trop = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "tides");
    if (json_data_tmp == NULL) {
        printf("tides json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,tides,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->tides = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "elevMin");
    if (json_data_tmp == NULL) {
        printf("elevMin json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,elevMin,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->elevMin = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "cn0Min");
    if (json_data_tmp == NULL) {
        printf("cn0Min json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,cn0Min,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->cn0Min = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "gdopThld");
    if (json_data_tmp == NULL) {
        printf("gdopThld json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,gdopThld,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->gdopThld = json_data_tmp->valuedouble;


    json_data_tmp = cJSON_GetObjectItem(json, "diffAge");
    if (json_data_tmp == NULL) {
        printf("diffAgeMax json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,diffAge,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->diffAgeMax = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "sys");
    if (json_data_tmp == NULL) {
        printf("sys json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,sys,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->sys = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "postResThld");
    if (json_data_tmp == NULL) {
        printf("postResThld json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,postResThld,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->postResThld = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "kMode");
    if (json_data_tmp == NULL) {
        printf("kMode json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,kMode,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->kMode = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "buffSize");
    if (json_data_tmp == NULL) {
        printf("buffSize json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,buffSize,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->buffSize = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "pcvE");
    if (json_data_tmp == NULL) {
        printf("pcvE json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,pcvE,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->stationPCV[0] = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "pcvN");
    if (json_data_tmp == NULL) {
        printf("pcvN json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,pcvN,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->stationPCV[1] = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "pcvU");
    if (json_data_tmp == NULL) {
        printf("pcvU json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,pcvU,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->stationPCV[2] = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "scene");
    if (json_data_tmp == NULL) {
        printf("senceopt json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,scene,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->senceopt = json_data_tmp->valueint;

    //json_data_tmp = cJSON_GetObjectItem(json, "smoothWindowTime");
    //if (json_data_tmp == NULL) {
    //    printf("smoothWindowsTime json_data_tmp is null\n");
    //    cJSON_Delete(json);
    //    sprintf(ackBuff, "ack,%s,smoothWindowTime,failed;\n", idStr);
    //    strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    //    return 0;
    //}
    //cfgOpt->smoothWindowsTime = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "initTime");
    if (json_data_tmp == NULL) {
        printf("initEnuTime json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,initTime,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->initEnuTime = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "detectSensitivity");
    if (json_data_tmp == NULL) {
        printf("detectSensitivity json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,detectSensitivity,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->detectSensitivity = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "typeSol");
    if (json_data_tmp == NULL) {
        printf("typeSol json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,typeSol,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->typeSol = json_data_tmp->valueint;


    json_data_tmp = cJSON_GetObjectItem(json, "timeIntervalSolution");
    if (json_data_tmp == NULL) {
        printf("timeIntervalSolution json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,timeIntervalSolution,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->timeIntervalSolution = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "minFixSat");
    if (json_data_tmp == NULL) {
        printf("minFixSat json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,minFixSat,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->minFixSat = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "maxDelSat");
    if (json_data_tmp == NULL) {
        printf("maxDelSat json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,maxDelSat,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->maxDelSat = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "gpsMask");
    if (json_data_tmp == NULL) {
        printf("gpsMask json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,gpsMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->gpsMask = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "qzssMask");
    if (json_data_tmp == NULL) {
        printf("qzsMask json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,qzssMask,failed\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->qzssMask = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "glonassMask");
    if (json_data_tmp == NULL) {
        printf("glonassMask json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,glonassMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->glonassMask = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "galieoMask");
    if (json_data_tmp == NULL) {
        printf("galieoMask json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,galieoMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->galieoMask = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "beidouMask");
    if (json_data_tmp == NULL) {
        printf("bdsMask json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,beidouMask,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->bdsMask = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "minSatRes");
    if (json_data_tmp == NULL) {
        printf("minSatRes json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,minSatRes,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->minSatRes = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "iggiiik0");
    if (json_data_tmp == NULL) {
        printf("iggiiik0 json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,iggiiik0,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->iggiiik0 = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "iggiiik1");
    if (json_data_tmp == NULL) {
        printf("iggiiik1 json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,iggiiik1,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    cfgOpt->iggiiik1 = json_data_tmp->valuedouble;

    json_data_tmp = cJSON_GetObjectItem(json, "levelTrace");
    if (json_data_tmp == NULL) {
        printf("levelTrace json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,levelTrace,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    level_trace = json_data_tmp->valueint;

    json_data_tmp = cJSON_GetObjectItem(json, "logFileSize");
    if (json_data_tmp == NULL) {
        printf("logFileSize json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,logFileSize,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    logFileSize = json_data_tmp->valueint;
    if (logFileSize <= 0) {
        logFileSize = 100;
    }

    json_data_tmp = cJSON_GetObjectItem(json, "writeDugTIme");
    if (json_data_tmp == NULL) {
        printf("writeDugTIme json_data_tmp is null\n");
        cJSON_Delete(json);
        sprintf(ackBuff, "ack,%s,writeDugTIme,failed;\n", idStr);
        strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
        return 0;
    }
    writeDugTime = json_data_tmp->valueint;
    if (writeDugTime <= 0) {
        writeDugTime = 10;
    }

    setCfgOpt(g_cfgOpt, &(svr->rtk.opt));

    sprintf(ackBuff, "ack,%s,ok;\n", idStr);
    strwrite(&svr->stream[2], (uint8_t*)ackBuff, strlen(ackBuff));
    cJSON_Delete(json);
    return 1;
}
/* rtk server thread ---------------------------------------------------------*/
#ifdef WIN32
static DWORD WINAPI rtksvrthread(void* arg)
#else
static void* rtksvrthread(void* arg)
#endif
{
    int i, j, f, k, m, n, cnt, dtMinIndex, flag = 0, cputime, fobs[2] = { 0 }, fnobs, state1, state2, size, ouInterval, fixCnt = 0;
    double tt, tow, dtMin, dt[OBSBASELEN] = { 0 };
    unsigned char* p, * q, iniEnuFlag = 0, sys, prn;
    unsigned int cycle = 0, tick, tick1hz = 0, iniEnuCnt = 0, epochCnt = 0;
    double iniEnu[3], pos[3], dr[3];
    gtime_t time;
    obsd_t obs[MAXOBS * 2] = { 0 }; /* for rover and base */
    obsd_t obsPre[MAXOBS];
    obsd_t obsepoch[MAXOBS * 2];
    int nobsepoch = 0;
    int nobsPre = 0;
    solopt_t sopt = solopt_default;
    rtksvr_t* svr = (rtksvr_t*)arg;
    sopt.posf = 2; //0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
    sopt.times = 0;//0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间
    sopt.outvel = 0;
    svr->tick = tickget();
    svr->cycle = 1000;
    char* buff = (char*)(calloc(sizeof(char), DEBUG_BUFF_LEN));
    char* pbuff;
    char s1[32];
    gtime_t obstime = { 0 };
    qobs_t qobs;


#ifndef WIN32
    struct stat fstat = { 0 };
#endif
    for (cycle = 0; svr->state; cycle++)
    {
        tick = tickget();
#ifdef _MSC_VER  // Redundant Code
        WIN32_FIND_DATA fileInfo;
        HANDLE hFind;
        DWORD fileSize;
        const char* fileName = tcpFileFath;
        hFind = FindFirstFile(tcpFileFath, &fileInfo);
        if (hFind != INVALID_HANDLE_VALUE)
            fileSize = fileInfo.nFileSizeLow;
        FindClose(hFind);
#endif
        FILE* fp_rover = ((file_t*)svr->stream[0].port)->fp;
        FILE* fp_base = ((file_t*)svr->stream[1].port)->fp;
        fobs[0] = fobs[1] = 0;
        while (!feof(fp_rover) || !feof(fp_base)) {
            for (i = 0; i < 2; i++)
            {   
                // printf("stream path: %s\n",svr->stream[1].path);
                fobs[i] = decoderaw(svr, i);//解码rtcm data and ssr  返回每次从流里面读取以采样间隔为单位的组数
            }
            while (svr->rtcm[0].time.time != svr->rtcm[1].time.time)
            {
                if (feof(fp_rover) || feof(fp_base)) break;
                if (svr->rtcm[0].time.time > svr->rtcm[1].time.time)
                {
                    fobs[1] = decoderaw(svr, 1);
                }
                else
                {
                    fobs[0] = decoderaw(svr, 0);
                }
            }
            break;

        }
        if (feof(fp_rover)) {
            free(buff);
            return 0;
        }
        for (k = 0; k < fobs[1]; k++) {
            qobs.n = 0;
            //if (svr->obs[1][k].data[0].time.time % 15 != 0)continue;
            //printf( "k=%d base  time=%d\n", k, svr->obs[1][k].data[0].time.time);
            trace(0x10, "k=%d base  time=%d\n", k, svr->obs[1][k].data[0].time.time);
            if (g_baseObsSyncIndex >= OBSBASELEN) {
                g_baseObsSyncIndex = 0;
                g_baseObsBuffFull = 1;
            }
            g_nbaseObsSync[g_baseObsSyncIndex] = 0;
            for (j = 0; j < svr->obs[1][k].n; j++) {
                for (f = 0; f < NFREQ; f++)svr->obs[1][k].data[j].LockTime[f] = 3000;
                g_baseObsSync[g_baseObsSyncIndex][j] = svr->obs[1][k].data[j];
                g_nbaseObsSync[g_baseObsSyncIndex]++;
            }
            g_baseObsSyncIndex++;
        }
        trace(2,"svr->rtk.opt.cn0Min*4, %d\n",svr->rtk.opt.cn0Min*4);
        for (i = 0; i < fobs[0]; i++) {
            //if (svr->obs[0][i].data[0].time.time % 15 != 0)continue;
            trace(0x10, "k=%d rover time=%d\n", i, svr->obs[0][i].data[0].time.time);
            n = 0;
            for (j = 0; j < MAXSAT; j++) {
                for (f = 0; f < NFREQ; f++)    svr->rtk.ssat[j].SNR[f] = 0;
            }
            for (j = 0; j < svr->obs[0][i].n && n < MAXOBS * 2; j++) {

                sys = satsys(svr->obs[0][i].data[j].sat, &prn);
                if (sys != SYS_BDS) continue;
                if (!(svr->rtk.opt.sys & 1) && sys == SYS_GPS)continue;
                if (!(svr->rtk.opt.sys & 2) && sys == SYS_QZS)continue;
                if (!(svr->rtk.opt.sys & 4) && sys == SYS_BDS)continue;
                if (!(svr->rtk.opt.sys & 8) && sys == SYS_GAL)continue;
                if (!(svr->rtk.opt.sys & 16) && sys == SYS_GLO)continue;
                // if (sys == SYS_BDS && (prn <= 5 || prn==59 || prn==60)) continue;
                if (sys == SYS_GAL) continue;
                if (sys == SYS_GPS && svr->rtk.opt.gpsMask >= 0) {
                    if (!((svr->rtk.opt.gpsMask >> (prn - 1)) & 1)) continue;
                }
                if (sys == SYS_QZS && svr->rtk.opt.qzssMask >= 0) {
                    if (!((svr->rtk.opt.qzssMask >> (prn - MINPRNQZS - 1)) & 1)) continue;
                }
                if (sys == SYS_BDS && svr->rtk.opt.bdsMask >= 0) {
                    if (!((svr->rtk.opt.bdsMask >> (prn - 1)) & 1)) continue;
                }
                if (sys == SYS_GAL && svr->rtk.opt.galieoMask >= 0) {
                    if (!((svr->rtk.opt.galieoMask >> (prn - 1)) & 1)) continue;
                }
                if (sys == SYS_GLO && svr->rtk.opt.glonassMask >= 0) {
                    if (!((svr->rtk.opt.glonassMask >> (prn - 1)) & 1)) continue;
                }

                // svr->rtk.opt.freq=1;
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
                for (f = 0; f < NFREQ; f++)     obs[n].LockTime[f] = 3000;
                for (k = 0; k < NFREQ; k++)
                {
                    if (obs[n].SNR[k] < svr->rtk.opt.cn0Min * 4)
                    {
                        obs[n].P[k] = obs[n].L[k] = obs[n].D[k] = 0.0;
                    }
                }
                cnt = 0;
                if (sys == SYS_BDS) {
                    for (f = 0; f < NFREQ; f++) {
                        if (obs[n].P[f] != 0.0) cnt++;
                    }
                    for (f = 0; f < NFREQ; f++) {
                        if (cnt >= 2 && f >= 3)
                            obs[n].P[f] = obs[n].L[f] = 0.0;
                    }
                }
                n++;
            }
            qobs.n = 0;
            for (j = 0; j < n; j++) {
                qobs.data[j] = obs[j];
                qobs.n = qobs.n + 1;
            }
            if (qobs.n > 0)
                EnQueue(&Qrover, qobs);

            //收到测站数据即计算测站星空图，并发送数据 避免在后面缺基站数据，不进解算，无法计算星空图（阻塞后 无法发送该数）
            for (j = 0; j < MAXSAT; j++) {
                svr->rtk.ssat[j].vs = 0;
                svr->rtk.ssat[j].azel[0][0] = svr->rtk.ssat[j].azel[0][1] = 0.0;
                svr->rtk.ssat[j].azel[1][0] = svr->rtk.ssat[j].azel[1][1] = 0.0;
                svr->rtk.ssat[j].rs[0] = 0.0;
                svr->rtk.ssat[j].rs[1] = 0.0;
                svr->rtk.ssat[j].rs[2] = 0.0;
                svr->rtk.ssat[j].SNR[0] = svr->rtk.ssat[j].SNR[1] = svr->rtk.ssat[j].SNR[2] =
                    svr->rtk.ssat[j].SNR[3] = svr->rtk.ssat[j].SNR[4] = svr->rtk.ssat[j].SNR[5] = 0.0;
            }
            for (j = 0; j < n; j++) {
                svr->rtk.ssat[obs[j].sat - 1].SNR[0] = obs[j].SNR[0];
                svr->rtk.ssat[obs[j].sat - 1].SNR[1] = obs[j].SNR[1];
                svr->rtk.ssat[obs[j].sat - 1].SNR[2] = obs[j].SNR[2];
                svr->rtk.ssat[obs[j].sat - 1].SNR[3] = obs[j].SNR[3];
                svr->rtk.ssat[obs[j].sat - 1].SNR[4] = obs[j].SNR[4];
                svr->rtk.ssat[obs[j].sat - 1].SNR[5] = obs[j].SNR[5];
            }
        }
        qobs.n = 0;
        while (GetHead(&Qrover)) {
            qobs = Qrover.data[0];
            for (i = 1; i < Qrover.rear; i++) {
                Qrover.data[i - 1] = Qrover.data[i];
            }
            Qrover.rear--;
            n = 0;
            for (i = 0; i < qobs.n; i++) {
                obs[n] = qobs.data[i];
                svr->rtk.ssat[obs[n].sat - 1].SNR[0] = obs[n].SNR[0];
                svr->rtk.ssat[obs[n].sat - 1].SNR[1] = obs[n].SNR[1];
                svr->rtk.ssat[obs[n].sat - 1].SNR[2] = obs[n].SNR[2];
                svr->rtk.ssat[obs[n].sat - 1].SNR[3] = obs[n].SNR[3];
                svr->rtk.ssat[obs[n].sat - 1].SNR[4] = obs[n].SNR[4];
                svr->rtk.ssat[obs[n].sat - 1].SNR[5] = obs[n].SNR[5];


                n++;
            }

            //-----------------------------find the closest time--------------------
            dtMin = 9999.9;
            dtMinIndex = -1;
            for (j = 0; j < OBSBASELEN; j++) {
                dt[j] = timediff(g_baseObsSync[j][0].time, obs[0].time);
                if (fabs(dt[j]) < fabs(dtMin)) {
                    dtMin = dt[j];
                    dtMinIndex = j;
                }
            }
            if (dtMinIndex != -1) {
                for (k = 0; k < g_nbaseObsSync[dtMinIndex]; k++) {
                    obs[n + k] = g_baseObsSync[dtMinIndex][k];
                }
                n = n + g_nbaseObsSync[dtMinIndex];
            }
            trace(0x10, "dt:%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", dt[0], dt[1], dt[2], dt[3], dt[4], dt[5], dt[6], dt[7], dt[8], dt[9]);


            nobsepoch = 0;
            for (k = 0; k < n; k++) {
                if (obs[k].rcv == 1) {
                    obsepoch[nobsepoch++] = obs[k];
                }
            }
            for (k = 0; k < nobsPre; k++) {
                obsepoch[nobsepoch] = obsPre[k];
                obsepoch[nobsepoch].rcv = 2;
                nobsepoch++;
            }
                    
            nobsPre = 0;
            for (k = 0; k < n; k++) {
                if (obs[k].rcv == 1) {
                    nobsPre++;
                    obsPre[k] = obs[k];
                }
            }


            if (obs[0].time.time % (int)svr->rtk.opt.timeInterval != 0) {
                continue;
            }
            // printf("mak%ld, %.0f;\n", obs[0].time.time, svr->rtk.opt.timeInterval);

            trace(0xff, "------------rtk dynamics-------------\n");
            
            trace(2, "kalman start \n");
            rtkReturnValue = rtkpos(&svr->rtk, obs, n);
            trace(2, "kalman end \n");
            time = svr->rtk.sol.time;
            if (sopt.times == 3) {
                time = gpst2utc(time);
                time.time += 3600 * 8;
            }
            time2str(time, s1, 3);
            if (outsol(oFile.fpOut[1], &svr->rtk, &svr->rtk.sol, svr->rtk.rb, &sopt)) {
                tt = timediff(svr->rtk.sol.time, svr->rtk.sol.time_pre);
                if (svr->rtk.sol.rr_pre[0] != 0.0 && svr->rtk.sol.rr_pre[1] != 0.0 && svr->rtk.sol.rr_pre[2] != 0.0 && tt != 0.0) {
                    for (j = 0; j < 3; j++) {
                        svr->rtk.sol.vel[j] = (svr->rtk.sol.rr[j] - svr->rtk.sol.rr_pre[j]) / tt;
                    }
                }
                if (svr->rtk.sol.vel_pre[0] != 0.0 && svr->rtk.sol.vel_pre[1] != 0.0 && svr->rtk.sol.vel_pre[2] != 0.0 && tt != 0.0) {
                    for (j = 0; j < 3; j++) {
                        svr->rtk.sol.acc[j] = (svr->rtk.sol.vel[j] - svr->rtk.sol.vel_pre[j]) / tt;
                    }
                }

                if (svr->rtk.tt != 0.0) {
                    if (svr->rtk.enuWindwoIndex[0] * (double)svr->rtk.opt.timeInterval >= (svr->rtk.opt.initEnuTime * 3600.0) &&
                        svr->rtk.enuWindwoIndex[1] * (double)svr->rtk.opt.timeInterval >= (svr->rtk.opt.initEnuTime * 3600.0) &&
                        svr->rtk.enuWindwoIndex[2] * (double)svr->rtk.opt.timeInterval >= (svr->rtk.opt.initEnuTime * 3600.0) && iniEnuFlag == 0)
                        iniEnuFlag = 1;
                }
                /* OUTPUT Pos Infomation */
                memset(buff, 0, DEBUG_BUFF_LEN);
                pbuff = buff;
                outDnyResult(svr, &pbuff, s1, iniEnuFlag);
                if (svr->rtk.opt.typeSol == 0 && svr->rtk.opt.timeIntervalSolution == 0)
                    strwrite(&svr->stream[2], (uint8_t*)buff, strlen(buff));


                for (j = 0; j < 3; j++) {
                    svr->rtk.sol.rr_pre[j] = svr->rtk.sol.rr[j];
                    svr->rtk.sol.vel_pre[j] = svr->rtk.sol.vel[j];
                }
                svr->rtk.sol.time_pre = svr->rtk.sol.time;
                outResult(&svr->rtk, &sopt);

            }


            rtksvrunlock(svr);
            if (svr->rtk.sol.stat != SOLQ_NONE) {
                /* adjust current time */
                tt = (int)(tickget() - tick) / 1000.0 + DTTOL;
                timeset(gpst2utc(timeadd(svr->rtk.sol.time, tt)));
            }
            /* if cpu overload, inclement obs outage counter and break */
//            if ((int)(tickget() - tick) >= svr->cycle) {
//                svr->prcout += fobs[0] - i - 1;
//#if 0 /* omitted v.2.4.1 */
//                break;
//#endif
//            }
            //sleepms(1000);
#ifdef TIME_OUTPUT
            gettimeofday(&tvl, NULL);
            end = tvl.tv_sec * 1000.0 + tvl.tv_usec / 1000.0;
            ntime[9] = end - start;
#endif


        }
        /* send null solution if no solution (1hz) */
        if (svr->rtk.sol.stat == SOLQ_NONE && (int)(tick - tick1hz) >= 1000) {
            tick1hz = tick;
        }

        if ((cputime = (int)(tickget() - tick)) > 0) {
            svr->cputime = cputime;
        }
        fobs[0] = fobs[1] = 0;
        /* sleep until next cycle */
        //sleepms(svr->cycle - cputime);
        //sleepms(100);
    }
    free(buff);
    return 0;
}


void split(char* src, const char* separator, char** dest, int* num) {
    char* pNext;
    char* p;
    int count = 0;
    if (src == NULL || strlen(src) == 0)
        return;
    if (separator == NULL || strlen(separator) == 0)
        return;
#ifdef WIN32
    pNext = strtok_s(src, separator, &p);
#else
    pNext = strtok_r(src, separator, &p);
#endif
    while (pNext != NULL) {
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
extern FILE* fp_trace;     /* file pointer of trace */
int main(int argc, char** argv)
{
    solopt_t sopt = solopt_default;
    int i, len, rw;
    char* cfgfile;
    initCfgOpt(&g_cfgOpt);
    setCfgOpt(g_cfgOpt, &(svr.rtk.opt));

    printf("version:%d\n", SVN_VERSION);
    InitQueue(&Qrover);
    int trace_level=0;

    //int* strs = strtype;
    char* paths1[] = { strpath[0],strpath[1],strpath[2] };
    char** paths = paths1;
    char addr[256] = "", port[256] = "", user[256] = { 0 }, passwd[256] = { 0 };
    double enuAve[3] = { 0.0 };
    unsigned int enuAveCnt[3] = { 0 };
    int m, n, startFlag = 0, endFlag = 0, num = 0;
    char* revbuf[64] = { 0 };
    double rb[3] = { 0 };
    char infile[6][MAXSTRPATH], fileDir[MAXSTRPATH] = "D:\\rtktest\\kunchi", projname[128];

    m = 0; n = 0;


    svr.rtk.opt.timeInterval = 1.0;
    svr.rtk.opt.smoothWindowsTime = 1;
    svr.rtk.mpflag = 0;
#if 1
    int j=0; gtime_t ts, te;
    double es[] = { 2000,1,1,0,0,0 }, ee[] = { 2000,12,31,23,59,59 };
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-out") && i + 1 < argc) {
            strcpy(fileDir, argv[++i]);
        } else if (!strcmp(argv[i], "-fr") && i + 1 < argc) {
            strcpy(strpath[0], argv[++i]);
        } else if (!strcmp(argv[i], "-fb") && i + 1 < argc) {
            strcpy(strpath[1], argv[++i]);
        } else if (!strcmp(argv[i], "-name") && i + 1 < argc) {
            strcpy(projname, argv[++i]);
        } else if (!strcmp(argv[i], "-refb") && i + 1 < argc) {
            for (j = 0; j < 3; j++){
                svr.rtk.opt.rb[j] = atof(argv[++i]);
            }
        } else if (!strcmp(argv[i], "-refr") && i + 1 < argc) {
            for (j = 0; j < 3; j++){
                svr.rtk.opt.ru[j] = atof(argv[++i]);
            }
        } else if (!strcmp(argv[i], "-ts") && i + 2 < argc) {
            sscanf(argv[++i], "%lf/%lf/%lf", es, es + 1, es + 2);
            sscanf(argv[++i], "%lf:%lf:%lf", es + 3, es + 4, es + 5);
            ts = epoch2time(es);
        } else if (!strcmp(argv[i], "-te") && i + 2 < argc) {
            sscanf(argv[++i], "%lf/%lf/%lf", ee, ee + 1, ee + 2);
            sscanf(argv[++i], "%lf:%lf:%lf", ee + 3, ee + 4, ee + 5);
            te = epoch2time(ee);
        } else if (!strcmp(argv[i], "-ti") && i + 1 < argc) {
            svr.rtk.opt.timeInterval = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-x") && i + 1 < argc) {
            trace_level = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-st") && i + 1 < argc) {
            svr.rtk.opt.smoothWindowsTime = atof(argv[++i]);
        }else if (!strcmp(argv[i], "-mp") ) {
            svr.rtk.mpflag =1;
        }
        
    }
#endif

    rtksvrinit(&svr);

    svr.rtk.x = zeros(NX, 1);
    svr.rtk.P = zeros(NX, NX);
    svr.rtk.xp = zeros(NX, 1);
    svr.rtk.Pp = zeros(NX, NX);
    svr.rtk.I = zeros(NX, NX);
    svr.rtk.H = zeros(NY, NX);
    svr.rtk.F = zeros(NY, NX);
    svr.rtk.K = zeros(NY, NX);
    svr.rtk.Ri = zeros(NY, 1);
    svr.rtk.Rj = zeros(NY, 1);
    svr.rtk.R = zeros(NY, NY);
    svr.rtk.v = zeros(NY, 1);


    strinit(&svr.stream[0]);
    strinit(&svr.stream[1]);
    strinit(&svr.stream[2]);
    svr.state = 1;
    svr.tick = tickget();

    svr.rtk.opt.dynamics = 0;
    svr.rtk.opt.maxgdop = 30.0;
    svr.rtk.opt.mode = 2;
    svr.rtk.opt.nf = NFREQ;
    svr.rtk.opt.tidecorr = 1;
    svr.rtk.opt.elmin = 15.0 * D2R;
    svr.rtk.opt.maxtdiff = 5.0;
    svr.rtk.opt.std = 0.01;
    svr.rtk.nx = 0;
    svr.rtk.sol.bslConstrain = 1;
    if (svr.rtk.opt.dynamics == 2) sopt.outvel = 1;
    if (svr.rtk.opt.smoothWindowsTime == 0) svr.rtk.opt.smoothWindowsTime = 24;
    svr.rtk.opt.initEnuTime = 1;
    SELETE_SAT_NUM = 40;
    NX = (3 + 2 + SELETE_SAT_NUM + SELETE_SAT_NUM * NFREQ);
    NY = NX;

    svr.rtk.x = zeros(NX, 1);
    svr.rtk.P = zeros(NX, NX);
    svr.rtk.xp = zeros(NX, 1);
    svr.rtk.Pp = zeros(NX, NX);
    svr.rtk.I = zeros(NX, NX);
    svr.rtk.H = zeros(NY, NX);
    svr.rtk.F = zeros(NY, NX);
    svr.rtk.K = zeros(NY, NX);
    svr.rtk.Ri = zeros(NY, 1);
    svr.rtk.Rj = zeros(NY, 1);
    svr.rtk.R = zeros(NY, NY);
    svr.rtk.v = zeros(NY, 1);


    trace_flag[0] = 1; //0:定位结果
    trace_flag[1] = 1; //1:滤波后结果
    trace_flag[2] = 0; //2：单点定位结果
    trace_flag[3] = 0; //3：DOP
    trace_flag[4] = 0; //4：观测量信息
    trace_flag[5] = 0; //5：卫星位置
    trace_flag[6] = 0; //6：卫星残差
    trace_flag[7] = 0; //7：卫星仰角
    trace_flag[8] = 0; //8：多径
    trace_flag[9] = 0; //9：模糊度 电离层
    trace_flag[10] = 1;//9：调试信息

    if (ROUND(svr.rtk.opt.timeInterval) != 0.0) {
        svr.rtk.maxSmoothPoint = svr.rtk.opt.smoothWindowsTime * 3600.0 / ROUND(svr.rtk.opt.timeInterval);
    }
    else {
        svr.rtk.maxSmoothPoint = 86400;
    }
    if (svr.rtk.opt.senceopt == 1) {
        svr.rtk.maxSmoothPoint = 10;
    }

    svr.rtk.cntEnuWind = 0;
    svr.rtk.maxMedianFilterPoint = 1 * 3600 / svr.rtk.opt.timeInterval + 1;
    svr.rtk.maxMedianFilterPoint = svr.rtk.maxMedianFilterPoint > 3601 ? 3601 : svr.rtk.maxMedianFilterPoint;
    for (i = 0; i < 3; i++) {
        if (!(svr.rtk.enuWindow[i] = (double*)calloc(svr.rtk.maxSmoothPoint, sizeof(double)))) {
            return 0;
        }
        if (!(svr.rtk.enuWindowMedian[i] = (double*)calloc(svr.rtk.maxMedianFilterPoint, sizeof(double)))) {
            return 0;
        }
        svr.rtk.enuWindowMedianShiftNum[i] = 0;
    }


    svr.rtk.iniCnt = 0;

    sopt.posf = 2; //0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
    sopt.times = 3;//0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间
    sopt.outvel = 0;
    sopt.outhead = 0;

    strinitcom();//初始化网络
    memset(configFileFath, 0, MAXSTRPATH);

    char outDir[1024], outFileName[1024], outfile[2][1024], * ext;
    char sep = (char)FILEPATHSEP;

#if 0
    struct dirent* file;
    DIR* dir;
    if (!(dir = opendir(fileDir))) {
        printf("ERROR: open obsdir failed: %s\n",fileDir);
        return -1;
    }
    while ((file = readdir(dir)) != NULL) {
        if (strncmp(file->d_name, ".", 1) == 0) continue;
        if (!(ext = strrchr(file->d_name, '.'))) continue;
        if (!strstr(file->d_name, "rover") && !strstr(file->d_name, "base")) continue;
        if (strstr(file->d_name, "rover"))
            sprintf(strpath[0], "%s%c%s", fileDir, sep, file->d_name);
        else
            sprintf(strpath[1], "%s%c%s", fileDir, sep, file->d_name);
    }
#endif

    sprintf(outDir, "%s%cresult_%s_%d_%d", fileDir, sep, projname, SVN_VERSION,svr.rtk.mpflag);
    if (access(outDir, 0) != 0)
        createdir(outDir);
    sprintf(outfile[0], "%s%c%s", outDir, sep, "rtk.pos");
    sprintf(outfile[1], "%s%c%s", outDir, sep, "filter.pos");

    sprintf(svr.rtk.path, "%s/dats", outDir);
    createdir(svr.rtk.path);
    if (svr.rtk.mpflag == 1){
        mkfpssat(&svr.rtk);
    }

    char logfile[1024];
    sprintf(logfile, "%s/rtk.log", svr.rtk.path);
    logopen(logfile, 0); // 1M log  for test


    /* open pos filter */
    svr.rtk.sol.window[0].nmax = (int)2 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[1].nmax = (int)2 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[1].dely = (int)3 * 60 / svr.rtk.opt.timeInterval;
    svr.rtk.sol.window[2].nmax = (int)svr.rtk.opt.smoothWindowsTime * 60 * 60 / svr.rtk.opt.timeInterval;

    svr.rtk.sol.window[0].thres[0]= posmaxstd(0.001, 0.01);  // unit:mm
    svr.rtk.sol.window[0].thres[1]= posmaxstd(0.001, 0.01);
    svr.rtk.sol.window[0].thres[2]= posmaxstd(0.002, 0.02);

    svr.rtk.sol.window[1].thres[0]= posmaxstd(0.001, 0.01);  // unit:mm
    svr.rtk.sol.window[1].thres[1]= posmaxstd(0.001, 0.01);
    svr.rtk.sol.window[1].thres[2]= posmaxstd(0.002, 0.02);

    printf("%f %f %f\n",svr.rtk.sol.window[1].thres[0],svr.rtk.sol.window[1].thres[1],svr.rtk.sol.window[1].thres[2]);

    svr.rtk.sol.wdata.nmax = (int)svr.rtk.opt.smoothWindowsTime * 60 * 60 / svr.rtk.opt.timeInterval + 1;
    svr.rtk.sol.wdata.mode = FIL;

    char wpospath[256];
    sprintf(wpospath, "%s/wpos.dat", svr.rtk.path);
    init_data(&svr.rtk.sol.wdata, wpospath);

    for (i = 0; i < 2; i++) {
        /* write header to output file */
        if (!outhead(outfile[i], infile, i, &svr.rtk.opt, &sopt)) {
            //freeobsnav(&obss, &navs);
            return 0;
        }
        oFile.fpOut[i] = fopen(outfile[i], "a");
        if (!oFile.fpOut[i]) return -1;
    }
    sprintf(outFileName, "%s%c%s", outDir, sep, "debug.log");
    if (trace_level > 0) {
        printf("TRACE FILE : %s\n",outFileName);
        traceopen(outFileName);
        printf("TRACE LEVEL: %d\n",trace_level);
        tracelevel(trace_level);
    }

    for (i = 0; i < 3; i++)
    {
        rw = i < 2 ? STR_MODE_R : STR_MODE_RW;
        if (!stropen(svr.stream + i, strtype[i], rw, paths[i])) {
            printf("open stream error:%s\n", paths[i]);
            return 0;
        }
    }
    if (trace_flag[6] == 1) {
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp.ion");
        oFile.resp = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc1.ion");
        oFile.resc1 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp1.ion");
        oFile.resp1 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc2.ion");
        oFile.resc2 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp2.ion");
        oFile.resp2 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc3.ion");
        oFile.resc3 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp3.ion");
        oFile.resp3 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc4.ion");
        oFile.resc4 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc5.ion");
        oFile.resc5 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resc6.ion");
        oFile.resc6 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp4.ion");
        oFile.resp4 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp5.ion");
        oFile.resp5 = fopen(outFileName, "w");
        sprintf(outFileName, "%s%c%s", outDir, sep, "resp6.ion");
        oFile.resp6 = fopen(outFileName, "w");
        if (!(oFile.resp && oFile.resc1 && oFile.resp1 && oFile.resc2 && oFile.resp2 && oFile.resc3 && oFile.resp3
            && oFile.resc4 && oFile.resc5 && oFile.resc6 && oFile.resp4 && oFile.resp5 && oFile.resp6)) return -1;
    }
    for (i = 0; i < 2; i++)
    {
        init_rtcm(svr.rtcm + i);
        svr.nb[i] = svr.npb[i] = 0;
        if (!(svr.buff[i] = (unsigned char*)calloc(sizeof(unsigned char), BUFFSIZE)) || !(svr.pbuf[i] = (unsigned char*)calloc (sizeof(unsigned char),BUFFSIZE))) {
            return 0;
        }
    }

    enuAve[0] = svr.rtk.opt.enuWindow[0];
    enuAve[1] = svr.rtk.opt.enuWindow[1];
    enuAve[2] = svr.rtk.opt.enuWindow[2];
    enuAveCnt[0] = svr.rtk.opt.enuWindowIndex[0];
    enuAveCnt[1] = svr.rtk.opt.enuWindowIndex[1];
    enuAveCnt[2] = svr.rtk.opt.enuWindowIndex[2];

    if (enuAveCnt[0] > svr.rtk.maxSmoothPoint) {
        enuAveCnt[0] = svr.rtk.maxSmoothPoint;
    }
    if (enuAveCnt[1] > svr.rtk.maxSmoothPoint) {
        enuAveCnt[1] = svr.rtk.maxSmoothPoint;
    }
    if (enuAveCnt[2] > svr.rtk.maxSmoothPoint) {
        enuAveCnt[2] = svr.rtk.maxSmoothPoint;
    }
    svr.rtk.enuWindwoIndex[0] = enuAveCnt[0];
    svr.rtk.enuWindwoIndex[1] = enuAveCnt[1];
    svr.rtk.enuWindwoIndex[2] = enuAveCnt[2];

    if (svr.rtk.opt.senceopt == 1) {
        enuAveCnt[0] = enuAveCnt[1] = enuAveCnt[2] = 10;
    }
    for (i = 0; i < enuAveCnt[0]; i++) {
        svr.rtk.enuWindow[0][i] = enuAve[0];
    }
    svr.rtk.sum_enu[0] = enuAve[0] * enuAveCnt[0];
    svr.rtk.sum_sqeun[0] = enuAve[0] * enuAve[0] * enuAveCnt[0];

    for (i = 0; i < enuAveCnt[1]; i++) {
        svr.rtk.enuWindow[1][i] = enuAve[1];
    }
    svr.rtk.sum_enu[1] = enuAve[1] * enuAveCnt[1];
    svr.rtk.sum_sqeun[1] = enuAve[1] * enuAve[1] * enuAveCnt[1];

    for (i = 0; i < enuAveCnt[2]; i++) {
        svr.rtk.enuWindow[2][i] = enuAve[2];
    }
    svr.rtk.sum_enu[2] = enuAve[2] * enuAveCnt[2];
    svr.rtk.sum_sqeun[2] = enuAve[2] * enuAve[2] * enuAveCnt[2];
    
    g_preBaseObsRtkNum = 0;
    /* create rtk server thread */
#ifdef WIN32
    if (!(svr.thread = CreateThread(NULL, 0, rtksvrthread, &svr, 0, NULL))) {
#else
    if (pthread_create(&svr.thread, NULL, rtksvrthread, &svr)) {
#endif
        for (i = 0; i < 3; i++) strclose(svr.stream + i);
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
        strclose(&svr.stream[i]);
    for (i = 0; i < 3; i++) {
        free(svr.rtk.enuWindow[i]);
        free(svr.rtk.enuWindowMedian[i]);
    }
    printf("rtk thread return\n");
    return 1;
    }
