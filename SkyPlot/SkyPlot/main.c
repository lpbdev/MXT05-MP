#include <stdlib.h>
#include "rtk.h"
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

rtksvr_t svr;
nav_t g_nav = { 0 };
myFile_t oFile = { 0 };
struct timeval tvl;

FILE* fptcp = NULL;
char* gpdebugBuff = NULL;
char debugBuff[DEBUG_BUFF_LEN] = { 0 };

double g_gpsLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5 };
double g_galLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ7, CLIGHT / FREQ5 };
double g_bdsLam[NFREQ] = { CLIGHT / FREQ1_CMP, CLIGHT / FREQ2_CMP, CLIGHT / FREQ3_CMP,CLIGHT / FREQB1C_CMP ,CLIGHT / FREQB2a_CMP ,CLIGHT / FREQB2b_CMP };
double g_gloLam[MAXPRNGLO][NFREQ] = { 0.0 };

unsigned char level_trace = 0xFF;
unsigned char trace_flag[64] = { 0 };
static void iniGloLam() {
	int i = 0, frq;
	for (i = 1; i <= MAXPRNGLO; i++) {
		if (i == 1 || i == 5)         frq = 1;
		else if (i == 20 || i == 24) frq = 2;
		else if (i == 19 || i == 23) frq = 3;
		else if (i == 17 || i == 21) frq = 4;
		else if (i == 3 || i == 7)  frq = 5;
		else if (i == 4 || i == 8)  frq = 6;
		else if (i == 11 || i == 15) frq = 0;
		else if (i == 12 || i == 16) frq = -1;
		else if (i == 9 || i == 13) frq = -2;
		else if (i == 18 || i == 22) frq = -3;
		else if (i == 2 || i == 6)  frq = -4;
		else if (i == 10 || i == 14) frq = -7;
		else frq = 0;
		g_gloLam[i - 1][0] = CLIGHT / (FREQ1_GLO + DFRQ1_GLO * frq);
		g_gloLam[i - 1][1] = CLIGHT / (FREQ2_GLO + DFRQ2_GLO * frq);
		g_gloLam[i - 1][2] = CLIGHT / FREQ3_GLO;
	}
}

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

typedef struct {            /* tcp control type */
	int state;              /* state (0:close,1:wait,2:connect) */
	char saddr[256];        /* address string */
	int port;               /* port */
	struct sockaddr_in addr; /* address resolved */
	socket_t sock;          /* socket descriptor */
	int tcon;               /* reconnect time (ms) (-1:never,0:now) */
	uint32_t tact;          /* data active tick */
	uint32_t tdis;          /* disconnect tick */
} tcp_t;

typedef struct {            /* tcp cilent type */
	tcp_t svr;              /* tcp server control */
	int toinact;            /* inactive timeout (ms) (0:no timeout) */
	int tirecon;            /* reconnect interval (ms) (0:no reconnect) */
} tcpcli_t;

typedef struct {            /* ntrip control type */
	int state;              /* state (0:close,1:wait,2:connect) */
	int type;               /* type (0:server,1:client) */
	int nb;                 /* response buffer size */
	char url[MAXSTRPATH];   /* url for proxy */
	char mntpnt[256];       /* mountpoint */
	char user[256];         /* user */
	char passwd[256];       /* password */
	char str[NTRIP_MAXSTR]; /* mountpoint string for server */
	uint8_t buff[NTRIP_MAXRSP]; /* response buffer */
	tcpcli_t* tcp;          /* tcp client */
} ntrip_t;

//int strtype[3] = { STR_TCPCLI,STR_TCPCLI,STR_TCPCLI };   //
//int format[3] = { STRFMT_RTCM3,STRFMT_RTCM3,STRFMT_RTCM3};
//char strpath[3][1024] = { "192.168.0.10:6006","192.168.0.10:6009",":192.168.1.232:6003" };

int strtype[2] = { STR_TCPCLI,STR_TCPCLI };   //STR_TCPCLI STR_TCPSVR
int format[2] = { STRFMT_RTCM3,STRFMT_RTCM3 };
char strpath[2][1024] = { "192.168.23.227:5000","192.168.22.115:8001"};

//int format[3] = { STRFMT_RTCM3,STRFMT_RTCM3,STRFMT_RTCM3 };
//int strtype[3] = { STR_NTRIPCLI,STR_NTRIPCLI,STR_NTRIPCLI }; //NTRIP Client
//char strpath[3][1024] = { "na5100:pw5100@139.224.245.115:5101/mt-rover-5100","na5074:pw5074@139.224.245.115:5101/mt-base-5074-5100","na57745773:pw57745773@101.132.195.26:5101/mt-data-57745773" };

extern void rtksvrlock(rtksvr_t* svr) { lock(&svr->lock); }
extern void rtksvrunlock(rtksvr_t* svr) { unlock(&svr->lock); }

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

static void updatesvr(rtksvr_t* svr, int ret, obs_t* obs, int iobs)
{
	double pos[3], del[3] = { 0 }, dr[3];
	int i, n = 0;

	//tracet(4, "updatesvr: ret=%d sat=%2d index=%d\n", ret, sat, index);

	if (ret == 1) { /* observation data */
		if (iobs < 128) {
			for (i = 0; i < obs->n; i++) {
				//if(obs->data[i].SNR[0]<40*4)continue;
				svr->obs[iobs].data[n] = obs->data[i];
				svr->obs[iobs].data[n].rcv = 0 + 1;
				n++;
			}
			svr->obs[iobs].n = n;
			sortobs(&svr->obs[iobs]);
		}
		//svr->nmsg[index][0]++;
	}
}

extern int decoderaw(rtksvr_t* svr)
{
	obs_t* obs = NULL;
	int i, ret = 0, fobs = 0;
	rtksvrlock(svr);
	for (i = 0; i < svr->nb; i++) {
		svr->rtcm.rcv = 0;
		ret = input_rtcm3(&svr->rtcm, svr->buff[i]);
		obs = &svr->rtcm.obs;
		if (ret != 0) {
			//printf("decode rtcm3 index=%d ok\n",index);
		}	
		/* update rtk server */
		if (ret > 0)
			updatesvr(svr, ret, obs, fobs);
		/* observation data received */
		if (ret == 1) {
			if (fobs < 128) {
				fobs++;
			}
			else svr->prcout++;
		}
	}
	svr->nb = 0;
	rtksvrunlock(svr);
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
	rtcm->msg[0] = rtcm->msgtype[0] = rtcm->opt[0] = '\0';
	rtcm->obsflag = rtcm->ephsat = 0;

	rtcm->nbyte = rtcm->nbit = rtcm->len = 0;
	rtcm->word = 0;

	rtcm->obs.data = NULL;

	/* reallocate memory for observation and ephemris buffer */
	if (!(rtcm->obs.data = (obsd_t*)malloc(sizeof(obsd_t) * MAXOBS))) {
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

	free(svr->buff);
	free(svr->pbuf);
	free_rtcm(&svr->rtcm);
	
	for (j = 0; j < 128; j++) {
		free(svr->obs[j].data);
	}
}
extern int rtksvrinit(rtksvr_t* svr)
{
	eph_t  eph0 = { 0,-1,-1 };
	geph_t geph0 = { 0,-1 };
	int i, j;
	for (i = 0; i < 2; i++) svr->format[i] = 0;
	svr->nb = 0;

	if (!(g_nav.eph = (eph_t*)malloc(sizeof(eph_t) * MAXSAT)) || !(g_nav.geph = (geph_t*)malloc(sizeof(geph_t) * NSATGLO))) {
		////trace(1, "rtksvrinit: malloc error\n");
		return 0;
	}
	for (i = 0; i < MAXSAT; i++) g_nav.eph[i] = eph0;
	for (i = 0; i < NSATGLO; i++) g_nav.geph[i] = geph0;
	g_nav.n = MAXSAT;
	g_nav.ng = NSATGLO;
	svr->navsel = 1;

	for (j = 0; j < 128; j++) {
		if (!(svr->obs[j].data = (obsd_t*)malloc(sizeof(obsd_t) * MAXOBS))) {
			printf("rtksvrinit: malloc error\n");
			return 0;
		}
	}
	memset(&svr->rtcm, 0, sizeof(rtcm_t));
	initlock(&svr->lock);
	return 1;
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
	int qzscnt = 0;
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
					qzscnt++;
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
	if (qzscnt == 0) {
		*p += sprintf(*p, "dtr,0:%.3f,1:%.3f,2:%.3f,3:%.3f,4:%.3f",
			rtk->sol.dtr[0] * CLIGHT, 0.0, rtk->sol.dtr[1] * CLIGHT, rtk->sol.dtr[2] * CLIGHT, rtk->sol.dtr[3] * CLIGHT);
	}
	else {
		*p += sprintf(*p, "dtr,0:%.3f,1:%.3f,2:%.3f,3:%.3f,4:%.3f",
			rtk->sol.dtr[0] * CLIGHT, rtk->sol.dtr[0] * CLIGHT, rtk->sol.dtr[1] * CLIGHT, rtk->sol.dtr[2] * CLIGHT, rtk->sol.dtr[3] * CLIGHT);
	}
	//*p += sprintf(*p, "\n");
	return;
}

/* rtk server thread ---------------------------------------------------------*/
#ifdef WIN32
static DWORD WINAPI rtksvrthread(void* arg)
#else
static void* rtksvrthread(void* arg)
#endif
{
	int i, j, f, k, m, n, cnt, dtMinIndex, flag = 0, cputime, fobs= 0, fnobs, state1, state2, size;
	double tt, tow, dtMin;
	unsigned char* p, * q, iniEnuFlag = 0, sys, prn;
	unsigned int cycle = 0, tick, tick1hz = 0, iniEnuCnt = 0, epochCnt = 0;
	double iniEnu[3], pos[3], dr[3];
	gtime_t time;
	solopt_t sopt = solopt_default;
	rtksvr_t* svr = (rtksvr_t*)arg;
	sopt.posf = 2; //0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
	sopt.times = 3;//0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间  
	sopt.outvel = 0;
	svr->tick = tickget();
	svr->cycle = 100;
	char* buff = (char*)(calloc(sizeof(char), DEBUG_BUFF_LEN));
	char* pbuff;
	char s1[32];
	gtime_t obstime = { 0 };
	ntrip_t* ntrip;
	qobs_t qobs;
	char s[64] = { 0 };

#ifndef WIN32	
	struct stat fstat = { 0 };
#endif

	for (cycle = 0; svr->state; cycle++)
	{
		tick = tickget();
		if (fobs != 0) continue;
		memset(buff, 0, DEBUG_BUFF_LEN);
		memset(svr->buff, 0, BUFFSIZE);
		p = svr->buff + svr->nb; q = svr->buff + BUFFSIZE;

		if (strtype == STR_NTRIPCLI) {
			ntrip = (ntrip_t*)svr->stream[1].port;
			state1 = ntrip->tcp->svr.state;
			if (flag < 10) {
				sprintf(buff, "pid,%d\n", getpid());
				strwrite(&svr->stream[1], (unsigned char*)buff, strlen(buff));
				state2 = ntrip->tcp->svr.state;
				if (state1 == 2 && state2 == 2) {
					printf("%s\n", buff);
					flag++;
				}
			}		
		}
		/* read receiver raw/rtcm data from input stream */
		if ((n = strread(svr->stream, p, q - p)) <= 0)	continue;
		rtksvrlock(svr);
		svr->nb += n;
		n = n < BUFFSIZE - svr->npb ? n : (BUFFSIZE - svr->npb);
		memcpy(svr->pbuf + svr->npb, p, n);
		svr->npb += n;
		rtksvrunlock(svr);
		
		fobs=  0;	
		fobs = decoderaw(svr);//解码rtcm data and ssr  返回每次从流里面读取以采样间隔为单位的组数		

		for (k = 0; k < fobs; k++) {
			qobs.n = 0;
			//if (svr->obs[1][k].data[0].time.time % 15 != 0)continue;
			//printf( "k=%d base  time=%d\n", k, svr->obs[1][k].data[0].time.time);
			//trace(4, "k=%d base  time=%d\n", k, svr->obs[k].data[0].time.time);
			//计算基站星空图数据
			for (j = 0; j < svr->obs[k].n; j++) {
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[0] = svr->obs[k].data[j].SNR[0];
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[1] = svr->obs[k].data[j].SNR[1];
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[2] = svr->obs[k].data[j].SNR[2];
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[3] = svr->obs[k].data[j].SNR[3];
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[4] = svr->obs[k].data[j].SNR[4];
				svr->rtk.ssat[svr->obs[k].data[j].sat - 1].SNR[5] = svr->obs[k].data[j].SNR[5];
			}
			if (!pntpos(1, svr->obs[k].data, svr->obs[k].n, &svr->rtk.sol, NULL, svr->rtk.ssat, &svr->rtk.opt, 0)) {
				memset(buff, 0, DEBUG_BUFF_LEN);
				pbuff = buff;
				time = svr->rtk.sol.time;
				if (sopt.times == 3) {
					time = gpst2utc(time);
					time.time += 3600 * 8;
				}
				time2str(time, s1, 3);
				pbuff += sprintf(pbuff, "sat,%s;", s1);
				generateSatBuf(&svr->rtk, &pbuff);
				pbuff += sprintf(pbuff, "\n");
				strwrite(&svr->stream[1], (uint8_t*)buff, strlen(buff));
			}
		}
		fobs = 0;
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

int main(int argc, char** argv)
{
	solopt_t sopt = solopt_default;
	int i, rw;
	char* paths1[2];

#if 1
	if (argc != 3) {
		printf("intput 3 parameter\n");
		return 0;
	}	
	strtype[0] = STR_NTRIPCLI;
	strtype[1] = STR_NTRIPCLI;
	paths1[0] = argv[1];
	paths1[1] = argv[2];
	printf("argv = %s\n", argv[1]);
	printf("argv = %s\n", argv[2]);

#else
	paths1[0] = strpath[0];
	paths1[1] = strpath[1];

#endif
	char** paths = paths1;
	char addr[256] = "", port[256] = "", user[256] = { 0 }, passwd[256] = { 0 };
	char mntpnt[256] = { 0 }, srctbl[MAXSTRPATH] = { 0 };
	double enuAve[3] = { 0.0 };
	char buf[1024] = { 0 };
	unsigned int enuAveCnt[3] = { 0 };
	int startFlag = 0, endFlag = 0, num = 0;
	char* revbuf[64] = { 0 };
	double rb[3] = { 0 };

	gpdebugBuff = debugBuff;
	rtksvrinit(&svr);


	strinit(&svr.stream[0]);
	strinit(&svr.stream[1]);

	svr.state = 1;
	svr.tick = tickget();

	svr.rtk.opt.mode = 2;
	svr.rtk.opt.elmin = 10.0 * D2R;

	sopt.posf = 2; //0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
	sopt.times = 3;//0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间  
	sopt.outvel = 0;
	sopt.outhead = 0;
	iniGloLam();;
	strinitcom();//初始化网络
	for (i = 0; i < 2; i++)
	{
		rw = i < 1 ? STR_MODE_R : STR_MODE_W;
		if (!stropen(svr.stream + i, strtype[i], rw, paths[i])) {
			printf("open stream error:%s\n", paths[i]);
			return 0;
		}
	}

	init_rtcm(&svr.rtcm);
	svr.nb = svr.npb = 0;
	if (!(svr.buff = (unsigned char*)malloc(BUFFSIZE)) || !(svr.pbuf = (unsigned char*)malloc(BUFFSIZE))) {
		return 0;
	}
	/* create rtk server thread */
#ifdef WIN32
	if (!(svr.thread = CreateThread(NULL, 0, rtksvrthread, &svr, 0, NULL))) {
#else
	if (pthread_create(&svr.thread, NULL, rtksvrthread, &svr)) {
#endif
		for (i = 0; i < 2; i++) strclose(svr.stream + i);
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
	for (i = 0; i < 2; i++)
		strclose(&svr.stream[i]);
	printf("rtk thread return\n");
	return 1;
	}

