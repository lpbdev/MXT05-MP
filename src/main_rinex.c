
#include "rtk.h"
#include"../src/include/dirent.h"
#include<direct.h >
#define MINSNR 20

obs_t obss = { 0 };          /* observation data */
static int nepoch = 0;            /* number of observation epochs */
static int iobsu = 0;            /* current rover observation data index */
static int iobsr = 0;            /* current reference observation data index */
static int aborts = 0;            /* abort status */
static int revs = 0;            /* analysis direction (0:forward,1:backward) */
static int isbs = 0;            /* current sbas message index */
static char proc_rov[64] = "";   /* rover for current processing */
static sta_t stas[MAXRCV];      /* station infomation */
static char proc_base[64] = "";   /* base station for current processing */
unsigned char trace_flag[64] = { 0 };
//----0:定位结果 	1:滤波后结果	2：单点定位结果	3：接收机自带定位		4：观测量信息		5：卫星位置
//----6：卫星残差	7：卫星仰角		8：调试信息
myFile_t oFile = { 0 };
rtksvr_t svr;
nav_t g_nav = { 0 };
rtk_t g_rtk = { 0 };
//rtk_t g_rtk_epoch = { 0 };
FILE* fptcp = NULL;
struct timeval tvl;
double start, end, ntime[10];
unsigned int g_nfloat = 0;
unsigned int g_nfix = 0;
double writeDugTime=10;
char* gpdebugBuff = NULL;
char debugBuff[DEBUG_BUFF_LEN] = { 0 };
unsigned char streamBase;
obsd_t g_baseObsSync[10][MAXOBS];
unsigned char g_nbaseObsSync[10] = { 0 };
unsigned char g_baseObsSyncIndex = 0;
unsigned char streamIndex;
double g_gpsLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5 };
double g_galLam[NFREQ] = { CLIGHT / FREQ1, CLIGHT / FREQ7, CLIGHT / FREQ5 };
double g_bdsLam[NFREQ] = { CLIGHT / FREQ1_CMP, CLIGHT / FREQ2_CMP, CLIGHT / FREQB2a_CMP,CLIGHT / FREQB2b_CMP,CLIGHT / FREQB1C_CMP,CLIGHT / FREQ3_CMP };//FREQB2a_CMP
double g_gloLam[MAXPRNGLO][NFREQ] = { 0.0 };

obsd_t g_preBaseObsRtk[MAXOBS];
int g_preBaseObsRtkNum;
obsd_t g_preBaseObsRtkLs[MAXOBS];
int g_preBaseObsRtkLsNum;
double baseRtcmPosition[3] = { 0 };
unsigned char level_trace = 0xff;
FILE* fpversion = NULL;
char configFileFath[MAXSTRPATH] = { 0 };
int SELETE_SAT_NUM = 20;
int NX;
int NY;
unsigned char rtcmMode;
int g_week;
double writeConfigTime = 1;

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

int fread_debug(rtk_t* rtk, nav_t* nav, FILE* fp, obsd_t* obs, int* nobs) {
	gtime_t time1 = { 0 }, time2 = { 0 };
	char buf[4096] = { 0 }, buf2[4096] = { 0 };
	int index, SNR[3];
	double tow, P[3], L[3], D[3], rb[3] = { 0 };
	int i, n = 0, flag = 0;
	int sat, sys, prn;
	while (fgets(buf, 1024, fp)) {

		if (buf[0] == 'b' && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'e' && buf[4] == ' ' && buf[5] == ' ' && buf[6] == 'p' && buf[7] == 'n' && buf[8] == 't')
		{
			sscanf(buf, "base  pntpos:%lf %lf %lf", &rb[0], &rb[1], &rb[2]);
			rtk->rb[0] = rb[0]; rtk->rb[1] = rb[1]; rtk->rb[2] = rb[2];
			if (flag == 1 && n > 0) {
				*nobs = n;
				return 0;
			}
		}
		if (buf[0] == 'r' && buf[1] == 'o' && buf[2] == 'v' && buf[3] == 'e' && buf[4] == 'r' && buf[5] == ' ' && buf[6] == 's' && buf[7] == 'y' && buf[8] == 's')
		{
			flag = 1;
			sscanf(buf, "rover sys=%d prn=%d %d %lf %lf %lf %d %lf %lf %lf %d", &sys, &prn, &time1.time, &P[0], &L[0], &D[0], &SNR[0], &P[1], &L[1], &D[1], &SNR[1]);
			sat = satno((unsigned char)sys, (unsigned char)prn);
			obs[n].sat = sat;
			obs[n].time = time1;
			obs[n].rcv = 1;
			for (i = 0; i < 1; i++) {
				obs[n].P[i] = P[i];
				obs[n].L[i] = L[i];
				obs[n].D[i] = D[i];
				obs[n].SNR[i] = SNR[i];
			}
			n++;
		}
		if (buf[0] == 'b' && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'e' && buf[4] == ' ' && buf[5] == ' ' && buf[6] == 's' && buf[7] == 'y' && buf[8] == 's')
		{
			flag = 1;
			sscanf(buf, "base  sys=%d prn=%d %d %lf %lf %lf %d %lf %lf %lf %d", &sys, &prn, &time2.time, &P[0], &L[0], &D[0], &SNR[0], &P[1], &L[1], &D[1], &SNR[1]);
			sat = satno(sys, prn);
			obs[n].sat = sat;
			obs[n].time = time2;
			obs[n].rcv = 2;
			for (i = 0; i < NFREQ; i++) {
				obs[n].P[i] = P[i];
				obs[n].L[i] = L[i];
				obs[n].D[i] = D[i];
				obs[n].SNR[i] = SNR[i];
			}
			n++;
		}
		if ((buf[0] == 'g' && buf[1] == 'p' && buf[2] == 's' && buf[3] == ' ' && buf[4] == 'e' && buf[5] == 'p' && buf[6] == 'h' && buf[7] == ':') ||
			(buf[0] == 'q' && buf[1] == 'z' && buf[2] == 's' && buf[3] == ' ' && buf[4] == 'e' && buf[5] == 'p' && buf[6] == 'h' && buf[7] == ':') ||
			(buf[0] == 'b' && buf[1] == 'd' && buf[2] == 's' && buf[3] == ' ' && buf[4] == 'e' && buf[5] == 'p' && buf[6] == 'h' && buf[7] == ':') ||
			(buf[0] == 'g' && buf[1] == 'a' && buf[2] == 'l' && buf[3] == ' ' && buf[4] == 'e' && buf[5] == 'p' && buf[6] == 'h' && buf[7] == ':') ||
			(buf[0] == 'g' && buf[1] == 'l' && buf[2] == 'o' && buf[3] == ' ' && buf[4] == 'e' && buf[5] == 'p' && buf[6] == 'h' && buf[7] == ':') ||
			(buf[0] == 'e' && buf[1] == 'p' && buf[2] == 'h' && buf[3] == ':')) {

			//memcpy(buf2, buf + 4, 4096 - 4);
			//sat = (unsigned char)str2num(buf2, 4, 7);
			sat = (unsigned char)str2num(buf, 4, 7);
			if (sat == 40)
				index = -1;
			sys = satsys(sat, NULL);
			if (sys == SYS_GLO) {
				index = findGephIndex(nav->geph, sat);
				if (index == -1) continue;
				sscanf(buf, "eph:%3d:%3d,%3d,%3d,%3d,%3d,%ld,%ld,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf\n",
					&nav->geph[index].sat,
					&nav->geph[index].iode,
					&nav->geph[index].frq,
					&nav->geph[index].svh,
					&nav->geph[index].sva,
					&nav->geph[index].age,
					&nav->geph[index].toe.time,
					&nav->geph[index].tof.time,
					&nav->geph[index].pos[0],
					&nav->geph[index].pos[1],
					&nav->geph[index].pos[2],
					&nav->geph[index].vel[0],
					&nav->geph[index].vel[1],
					&nav->geph[index].vel[2],
					&nav->geph[index].acc[0],
					&nav->geph[index].acc[1],
					&nav->geph[index].acc[2],
					&nav->geph[index].taun,
					&nav->geph[index].gamn,
					&nav->geph[index].dtaun);
			}
			else {
				index = findEphIndex(nav->eph, sat);
				if (index == -1) continue;
				//index = sat - 1;
				nav->eph[index].sat = sat;
				sscanf(buf, "eph:%d:%d,%d,%d,%d,%ld,%ld,%ld,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%d,%d",
					&nav->eph[index].sat,
					&nav->eph[index].iode,
					&nav->eph[index].iodc,
					&nav->eph[index].sva,
					&nav->eph[index].svh,
					&nav->eph[index].toe.time,
					&nav->eph[index].toc.time,
					&nav->eph[index].ttr.time,
					&nav->eph[index].A,
					&nav->eph[index].e,
					&nav->eph[index].i0,
					&nav->eph[index].OMG0,
					&nav->eph[index].omg,
					&nav->eph[index].M0,
					&nav->eph[index].deln,
					&nav->eph[index].OMGd,
					&nav->eph[index].idot,
					&nav->eph[index].crc,
					&nav->eph[index].crs,
					&nav->eph[index].cuc,
					&nav->eph[index].cus,
					&nav->eph[index].cic,
					&nav->eph[index].cis,
					&nav->eph[index].toes,
					&nav->eph[index].fit,
					&nav->eph[index].f0,
					&nav->eph[index].f1,
					&nav->eph[index].f2,
					&nav->eph[index].tgd[0],
					&nav->eph[index].code,
					&nav->eph[index].flag);
			}
		}
	}
	if (fp)	fclose(fp);
	return 1;
}


/* show message and check break ----------------------------------------------*/
static int checkbrk(const char* format, ...)
{
	va_list arg;
	char buff[1024], * p = buff;
	if (!*format) return showmsg("");
	va_start(arg, format);
	p += vsprintf(p, format, arg);
	va_end(arg);
	if (*proc_rov && *proc_base) sprintf(p, " (%s-%s)", proc_rov, proc_base);
	else if (*proc_rov) sprintf(p, " (%s)", proc_rov);
	else if (*proc_base) sprintf(p, " (%s)", proc_base);
	return showmsg(buff);
}

static int nextobsb(const obs_t* obs, int* i, int rcv)
{
	double tt;
	int n;

	for (; *i >= 0; (*i)--) if (obs->data[*i].rcv == rcv) break;
	for (n = 0; *i - n >= 0; n++) {
		tt = timediff(obs->data[*i - n].time, obs->data[*i].time);
		if (obs->data[*i - n].rcv != rcv || tt < -DTTOL) break;
	}
	return n;
}

/* search next observation data index ----------------------------------------*/
static int nextobsf(const obs_t* obs, int* i, int rcv)
{
	double tt;
	int n;

	for (; *i < obs->n; (*i)++) if (obs->data[*i].rcv == rcv) break;
	for (n = 0; *i + n < obs->n; n++) {
		tt = timediff(obs->data[*i + n].time, obs->data[*i].time);
		if (obs->data[*i + n].rcv != rcv || tt > DTTOL) break;
	}
	return n;
}

/* input obs data, navigation messages and sbas correction -------------------*/
static int inputobs(obsd_t* obs, int solq, const prcopt_t* popt)
{
	gtime_t time = { 0 };
	int i, nu, nr, n = 0;

	////trace(3, "infunc  : revs=%d iobsu=%d iobsr=%d isbs=%d\n", revs, iobsu, iobsr, isbs);

	if (0 <= iobsu && iobsu < obss.n) {
		settime((time = obss.data[iobsu].time));
		if (checkbrk("processing : %s Q=%d", time_str(time, 0), solq)) {
			aborts = 1; showmsg("aborted"); return -1;
		}
	}
	if (!revs) { /* input forward data */
		if ((nu = nextobsf(&obss, &iobsu, 1)) <= 0) return -1;
		//if (popt->intpref) {
		if (1) {
			for (; (nr = nextobsf(&obss, &iobsr, 2)) > 0; iobsr += nr)
				if (timediff(obss.data[iobsr].time, obss.data[iobsu].time) > -DTTOL)
					break;
		}
		else {
			for (i = iobsr; (nr = nextobsf(&obss, &i, 2)) > 0; iobsr = i, i += nr)
				if (timediff(obss.data[i].time, obss.data[iobsu].time) > DTTOL)
					break;
		}
		nr = nextobsf(&obss, &iobsr, 2);
		if (nr <= 0) {
			nr = nextobsf(&obss, &iobsr, 2);
		}
		for (i = 0; i < nu && n < MAXOBS * 2; i++) obs[n++] = obss.data[iobsu + i];
		for (i = 0; i < nr && n < MAXOBS * 2; i++) obs[n++] = obss.data[iobsr + i];
		iobsu += nu;
	}

	return n;
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

	////trace(3, "sortobs: nobs=%d\n", obs->n);

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
/* compare glonass ephemeris -------------------------------------------------*/
static int cmpgeph(const void* p1, const void* p2)
{
	geph_t* q1 = (geph_t*)p1, * q2 = (geph_t*)p2;
	return q1->tof.time != q2->tof.time ? (int)(q1->tof.time - q2->tof.time) :
		(q1->toe.time != q2->toe.time ? (int)(q1->toe.time - q2->toe.time) :
			q1->sat - q2->sat);
}
/* compare ephemeris ---------------------------------------------------------*/
static int cmpeph(const void* p1, const void* p2)
{
	eph_t* q1 = (eph_t*)p1, * q2 = (eph_t*)p2;
	return q1->ttr.time != q2->ttr.time ? (int)(q1->ttr.time - q2->ttr.time) :
		(q1->toe.time != q2->toe.time ? (int)(q1->toe.time - q2->toe.time) :
			q1->sat - q2->sat);
}
/* sort and unique ephemeris -------------------------------------------------*/
static void uniqeph(nav_t* nav)
{
	int i, j;

	////trace(3, "uniqeph: n=%d\n", nav->n);

	if (nav->n <= 0) return;

	qsort(nav->eph, nav->n, sizeof(eph_t), cmpeph);

	for (i = 1, j = 0; i < nav->n; i++) {
		if (nav->eph[i].sat != nav->eph[j].sat ||
			nav->eph[i].iode != nav->eph[j].iode) {
			nav->eph[++j] = nav->eph[i];
		}
	}
	nav->n = j + 1;
	////trace(4, "uniqeph: n=%d\n", nav->n);
}
/* sort and unique glonass ephemeris -----------------------------------------*/
static void uniqgeph(nav_t* nav)
{
	int i, j;

	////trace(3, "uniqgeph: ng=%d\n", nav->ng);

	if (nav->ng <= 0) return;

	qsort(nav->geph, nav->ng, sizeof(geph_t), cmpgeph);

	for (i = j = 0; i < nav->ng; i++) {
		if (nav->geph[i].sat != nav->geph[j].sat ||
			nav->geph[i].toe.time != nav->geph[j].toe.time ||
			nav->geph[i].svh != nav->geph[j].svh) {
			nav->geph[++j] = nav->geph[i];
		}
	}
	nav->ng = j + 1;

	////trace(4, "uniqgeph: ng=%d\n", nav->ng);
}
/* set antenna parameters ----------------------------------------------------*/
static void setpcv(gtime_t time, prcopt_t* popt, nav_t* nav, const pcvs_t* pcvs,
	const pcvs_t* pcvr, const sta_t* sta)
{
	//pcv_t* pcv;
	//double pos[3], del[3];
	//int i, j, mode = PMODE_DGPS <= popt->mode && popt->mode <= PMODE_FIXED;
	//char id[64];

	///* set satellite antenna parameters */
	//for (i = 0;i < MAXSAT;i++) {
	//	if (!(satsys(i + 1, NULL) & popt->navsys)) continue;
	//	if (!(pcv = searchpcv(i + 1, "", time, pcvs))) {
	//		satno2id(i + 1, id);
	//		continue;
	//	}
	//	nav->pcvs[i] = *pcv;
	//}
	//for (i = 0;i < (mode ? 2 : 1);i++) {
	//	if (!strcmp(popt->anttype[i], "*")) { /* set by station parameters */
	//		strcpy(popt->anttype[i], sta[i].antdes);
	//		if (sta[i].deltype == 1) { /* xyz */
	//			if (norm(sta[i].pos, 3) > 0.0) {
	//				ecef2pos(sta[i].pos, pos);
	//				ecef2enu(pos, sta[i].del, del);
	//				for (j = 0;j < 3;j++) popt->antdel[i][j] = del[j];
	//			}
	//		}
	//		else { /* enu */
	//			for (j = 0;j < 3;j++) popt->antdel[i][j] = stas[i].del[j];
	//		}
	//	}
	//	if (!(pcv = searchpcv(0, popt->anttype[i], time, pcvr))) {
	//		*popt->anttype[i] = '\0';
	//		continue;
	//	}
	//	strcpy(popt->anttype[i], pcv->type);
	//}
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

static void loadCfgOpt(cfgopt_t* cfgOpt, char** argv, int i, char* fileDir, char* outDir)
{
	char* cfgfile;
	cfgfile = argv[i++];

	cfgfile = argv[i++];
	strcpy(fileDir, cfgfile);

	cfgfile = argv[i++];
	strcpy(outDir, cfgfile);

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

	//cfgfile = argv[i++];
	//cfgOpt->timeIntervalSolution = atoi(cfgfile);
	//printf("timeIntervalSolution=%d\n", cfgOpt->timeIntervalSolution);
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
	//procOpt->timeIntervalSolution = cfgOpt.timeIntervalSolution;
	//time interval
}


FILE* fptest;
//#define RTKDEBUG
int main(int argc, char** argv)
{
	SELETE_SAT_NUM = 25;
	NX = (3 + 2 + SELETE_SAT_NUM + SELETE_SAT_NUM * NFREQ);
	NY = NX;
	gtime_t ts = { 0 }, te = { 0 };
	solopt_t sopt = solopt_default;
	filopt_t fopt = { 0 };
	double ver, dt[10], dtMin, rb[3] = { 0 };
	int i, j, k, f, n, nu, nr, cnt, dtMinIndex, tsys, nobs, coutFileLine[6] = { 0 }, readFileLine[6] = { 0 }, num,rtkReturnValue;
	unsigned char sys, prn;
	char* revbuf[64] = { 0 };
	gpdebugBuff = debugBuff;
	eph_t  eph0 = { 0,-1,-1 };
	geph_t geph0 = { 0,-1 };
	sta_t sta = { 0 };
	char* cfgfile;
	cfgopt_t cfgOpt = { 0 };

	obsd_t obs[MAXOBS * 2]; /* for rover and base */
	FILE* fp[6];
	pcvs_t pcvss = { 0 };        /* receiver antenna parameters */
	DIR* dir;
	struct dirent* file;
	char type, filepath[MAXSTRPATH] = { '\0' }, * ext;
	char buf[4096], buff[4096], sep = (char)FILEPATHSEP, s[64] = { 0 }, tobs[NUMSYS][MAXOBSTYPE][4] = { { "" } };
	int indexFile = 0, handObsCount = 0;
	int percent = 0, percent10 = 0, percent1 = 0, cout = 0;
	char outDir[MAXSTRPATH] = "", outfile[2][MAXSTRPATH] = {""}, outFileName[MAXSTRPATH];
	char infile[6][MAXSTRPATH], fileDir[MAXSTRPATH];
	char optsys[] = "SingleBDS", lsqOrkalman[] = "lsq_segMid_Rinex";
	//char optsys[] = "SingleBDS", lsqOrkalman[] = "kalman_segMid_tmp_smth6_Rinex";
	//char optsys[] = "SingleBDS", lsqOrkalman[] = "lsq_detect2_Rinex";
	//char optsys[] = "SingleBDS", lsqOrkalman[] = "lsq_Rinex";

	//char infileDir[MAXSTRPATH] = "F:\\data\\2cm";//1Hz
	//char infileDir[MAXSTRPATH] = "F:\\data\\8mm";//1Hz
	//char infileDir[MAXSTRPATH] = "F:\\data\\5mm";//1Hzk
	//
	//char infileDir[MAXSTRPATH] = "F:\\data\\BDS\\206FL_BD_louding";
	//char infileDir[MAXSTRPATH] = "F:\\data\\BDS\\LouDing";
	//char infileDir[MAXSTRPATH] = "F:\\data\\BDS\\ShuiKu";
	//char infileDir[MAXSTRPATH] = "F:\\data\\BDS\\ShuYin";
	char infileDir[MAXSTRPATH] = "D:\\rtktest\\03-01";
	//char infileDir[MAXSTRPATH] = "F:\\data\\20240705\\SingleBDS_MRD\\21100100001208_21100100001195\\rinex";//单北斗静态前后端解算超MRD需求
	//char infileDir[MAXSTRPATH] = "F:\\data\\20240705\\2cm_1cm\\21100100001208_2k1100100001603";//单北斗静态前后端解算超MRD需求
	//char infileDir[MAXSTRPATH] = "F:\\data\\20240708\\SingleBDS_2cm\\rinex";//单北斗静态前后端解算超MRD需求

	if (argc == 2)
		strcpy(fileDir, argv[1]);
	else
		strcpy(fileDir, infileDir);
	printf("fileDir %s\n", fileDir);
	char* p = buf;
	double pos[3], dr[3], r[3];
	char addr[256] = "", port[256] = "", user[256] = { 0 }, passwd[256] = { 0 };
	char mntpnt[256] = { 0 }, srctbl[MAXSTRPATH] = { 0 };
	double enuAve[3] = { 0.0 };
	unsigned int enuAveCnt[3] = { 0 };
	int startFlag = 0, endFlag = 0;


	fptest = fopen("test.log", "w");
	g_nav.n = MAXEPH;
	g_nav.ng = MAXGEPH;
	g_nav.eph = (eph_t*)malloc(sizeof(eph_t) * MAXEPH);
	g_nav.geph = (geph_t*)malloc(sizeof(geph_t) * MAXGEPH);


	for (i = 0; i < MAXEPH; i++) {
		g_nav.eph[i] = eph0;
	}
	for (i = 0; i < MAXGEPH; i++) {
		g_nav.geph[i] = geph0;
	}
	g_rtk.x = zeros(NX, 1);
	g_rtk.P = zeros(NX, NX);
	g_rtk.xp = zeros(NX, 1);
	g_rtk.Pp = zeros(NX, NX);

	g_rtk.I = zeros(NX, NX);
	g_rtk.H = zeros(NY, NX);
	g_rtk.F = zeros(NY, NX);
	g_rtk.K = zeros(NY, NX);
	g_rtk.Ri = zeros(NY, 1);
	g_rtk.Rj = zeros(NY, 1);
	g_rtk.R = zeros(NY, NY);
	g_rtk.v = zeros(NY, 1);

	//g_rtk_epoch.x = zeros(NX, 1);
	//g_rtk_epoch.P = zeros(NX, NX);
	//g_rtk_epoch.xp = zeros(NX, 1);
	//g_rtk_epoch.Pp = zeros(NX, NX);
	//g_rtk_epoch.I = zeros(NX, NX);
	//g_rtk_epoch.H = zeros(NY, NX);
	//g_rtk_epoch.F = zeros(NY, NX);
	//g_rtk_epoch.K = zeros(NY, NX);
	//g_rtk_epoch.Ri = zeros(NY, 1);
	//g_rtk_epoch.Rj = zeros(NY, 1);
	//g_rtk_epoch.R = zeros(NY, NY);
	//g_rtk_epoch.v = zeros(NY, 1);

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

#if 0
	printf("argc=%d\n", argc);
	if (argc < 24)
	{
		printf("please input like:Rtk.out timeInterval freq iono trop tides elevmin cn0min gdopthld diffagemax sys\
		postresthld kmode buffsize stationPCV[3] senceopt smoothWindowsTime initEnuTime detectSensitivity typeSol\n");
		return 0;
	}
	else if (argc > 24)
	{
		printf("input too many parameter\n");
		return 0;
	}
	else   //NTRIP Client With Configuration Parameters
	{
		loadCfgOpt(&cfgOpt, argv, 3 - 3, fileDir, outDir);
	}
	setCfgOpt(cfgOpt, &(g_rtk.opt));
	iniGloLam();
	g_rtk.opt.dynamics = 0;
	g_rtk.opt.mode = 2;
	g_rtk.opt.nf = NFREQ;
	g_rtk.nx = 0;
	g_rtk.opt.maxtdiff = 30;
	g_rtk.opt.std = 0.01;
	g_rtk.sol.bslConstrain = 1;
	if (g_rtk.opt.dynamics == 2) sopt.outvel = 1;
	g_rtk.opt.mode = 2;
	g_rtk.opt.minFixSat = 10;
	g_rtk.opt.maxDelSat = 5;
	g_rtk.opt.minSatRes = 0.02;
	g_rtk.opt.gpsMask = 0xffffffffffffffff;
	g_rtk.opt.qzssMask = 0xffffffffffffffff;
	g_rtk.opt.glonassMask = 0xffffffffffffffff;
	g_rtk.opt.galieoMask = 0xffffffffffffffff;
	g_rtk.opt.bdsMask = 0xffffffffffffffff;
	g_rtk.opt.iggiiik0 = 1.5;
	g_rtk.opt.iggiiik1 = 3.0;

	setCfgOpt(cfgOpt, &(g_rtk_epoch.opt));
	iniGloLam();
	g_rtk_epoch.opt.dynamics = 0;
	g_rtk_epoch.opt.mode = 2;
	g_rtk_epoch.opt.nf = NFREQ;
	g_rtk_epoch.nx = 0;
	g_rtk_epoch.opt.maxtdiff = 30;
	g_rtk_epoch.opt.std = 0.01;
	g_rtk_epoch.sol.bslConstrain = 1;
	if (g_rtk_epoch.opt.dynamics == 2) sopt.outvel = 1;
	g_rtk_epoch.opt.mode = 3;
	//g_rtk_epoch.opt.maxgdop = 30.0;
	//g_rtk.opt.maxgdop = 30.0;
	g_rtk_epoch.opt.minFixSat = 10;
	g_rtk_epoch.opt.maxDelSat = 5;
	g_rtk_epoch.opt.minSatRes = 0.02;
	g_rtk_epoch.opt.gpsMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.qzssMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.glonassMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.galieoMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.bdsMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.iggiiik0 = 1.5;
	g_rtk_epoch.opt.iggiiik1 = 3.0;

#else
	g_rtk.opt.senceopt = 0;
	iniGloLam();
	g_rtk.opt.dynamics = 0;
	g_rtk.opt.maxgdop = 30.0;
	g_rtk.opt.nf = NFREQ;
	g_rtk.opt.sys = 0xff;
	g_rtk.opt.freq = 0xff;
	g_rtk.opt.postResThld = 2;
	g_rtk.opt.cn0Min = 33;
	g_rtk.nx = 0;
	g_rtk.opt.elmin = 15.0 * D2R;
	g_rtk.opt.maxtdiff = 5;
	g_rtk.opt.std = 0.01;
	g_rtk.sol.bslConstrain = 1;
	g_rtk.opt.timeInterval = 15.0;
	g_rtk.opt.initEnuTime = 11;
	g_rtk.opt.smoothWindowsTime = 11;
	g_rtk.opt.mode = 2;
	if (g_rtk.opt.dynamics == 2) sopt.outvel = 1;
	g_rtk.opt.detectSensitivity = 10;
	g_rtk.opt.typeSol = 0;
	g_rtk.opt.minFixSat = 10;
	g_rtk.opt.maxDelSat = 5;
	g_rtk.opt.minSatRes = 0.02;
	g_rtk.opt.gpsMask = 0xffffffffffffffff;
	g_rtk.opt.qzssMask = 0xffffffffffffffff;
	g_rtk.opt.glonassMask = 0xffffffffffffffff;
	g_rtk.opt.galieoMask = 0xffffffffffffffff;
	g_rtk.opt.bdsMask = -1;
	g_rtk.opt.iggiiik0 = 1.5;
	g_rtk.opt.iggiiik1 = 3.0;

	sprintf(outDir, "%s%c%s_%d", fileDir, sep, "result", SVN_VERSION);

	iniGloLam();
	/*g_rtk_epoch.opt.senceopt = 0;
	g_rtk_epoch.opt.dynamics = 0;
	g_rtk_epoch.opt.maxgdop = 30.0;
	g_rtk_epoch.opt.nf = NFREQ;
	g_rtk_epoch.opt.sys = 0xff;
	g_rtk_epoch.opt.freq = 0xff;
	g_rtk_epoch.opt.postResThld = 2;
	g_rtk_epoch.opt.cn0Min = 33;
	g_rtk_epoch.nx = 0;
	g_rtk_epoch.opt.elmin = 15.0 * D2R;
	g_rtk_epoch.opt.std = 0.01;
	g_rtk_epoch.sol.bslConstrain = 1;
	g_rtk_epoch.opt.timeInterval = g_rtk.opt.timeInterval;
	g_rtk_epoch.opt.maxtdiff = g_rtk.opt.timeInterval + 1.0;
	g_rtk_epoch.opt.initEnuTime = 0;
	g_rtk_epoch.opt.smoothWindowsTime = 12;
	g_rtk_epoch.opt.mode = 3;
	g_rtk_epoch.opt.typeSol = 0;
	g_rtk_epoch.opt.minFixSat = 10;
	g_rtk_epoch.opt.maxDelSat = 5;
	g_rtk_epoch.opt.minSatRes = 0.02;
	g_rtk_epoch.opt.gpsMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.qzssMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.glonassMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.galieoMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.bdsMask = 0xffffffffffffffff;
	g_rtk_epoch.opt.iggiiik0 = 1.5;
	g_rtk_epoch.opt.iggiiik1 = 3.0;
	if (g_rtk_epoch.opt.dynamics == 2) sopt.outvel = 1;*/
	//sprintf(outDir, "%s%c%s", fileDir, sep, "result");
#endif

	if (ROUND(g_rtk.opt.timeInterval) != 0.0) {
		g_rtk.maxSmoothPoint = g_rtk.opt.smoothWindowsTime * 3600.0 / ROUND(g_rtk.opt.timeInterval);
	}
	else {
		g_rtk.maxSmoothPoint = 86400;
	}
	if (g_rtk.opt.senceopt == 1) {
		g_rtk.maxSmoothPoint = 10;
	}
	g_rtk.cntEnuWind = 0;
	g_rtk.maxMedianFilterPoint = 1 * 3600 / g_rtk.opt.timeInterval + 1;
	g_rtk.maxMedianFilterPoint = g_rtk.maxMedianFilterPoint > 3601 ? 3601 : g_rtk.maxMedianFilterPoint;
	for (i = 0; i < 3; i++) {
		if (!(g_rtk.enuWindow[i] = (double*)calloc(g_rtk.maxSmoothPoint, sizeof(double)))) {
			return 0;
		}
		if (!(g_rtk.enuWindowMedian[i] = (double*)calloc(g_rtk.maxMedianFilterPoint, sizeof(double)))) {
			return 0;
		}
		g_rtk.enuWindowMedianShiftNum[i] = 0;
	}

	/*if (!(g_rtk.eWindow = (double*)calloc(g_rtk.maxMedianFilterPoint, sizeof(double)))) {
		return 0;
	}
	if (!(g_rtk.nWindow = (double*)calloc(g_rtk.maxMedianFilterPoint, sizeof(double)))) {
		return 0;
	}
	if (!(g_rtk.uWindow = (double*)calloc(g_rtk.maxMedianFilterPoint, sizeof(double)))) {
		return 0;
	}*/
	//if (ROUND(g_rtk_epoch.opt.timeInterval) != 0.0) {
	//	g_rtk_epoch.maxSmoothPoint = g_rtk_epoch.opt.smoothWindowsTime * 3600.0 / ROUND(g_rtk_epoch.opt.timeInterval);
	//}
	//else {
	//	g_rtk_epoch.maxSmoothPoint = 86400;
	//}
	//if (g_rtk_epoch.opt.senceopt == 1) {
	//	g_rtk_epoch.maxSmoothPoint = 10;
	//}
	//for (i = 0; i < 3; i++) {
	//	if (!(g_rtk_epoch.enuWindow[i] = (double*)calloc(g_rtk_epoch.maxSmoothPoint, sizeof(double)))) {
	//		return 0;
	//	}
	//}
	g_rtk.iniCnt = g_rtk.maxSmoothPoint;

	if (access("./configFile", 0) != 0)
		mkdir("./configFile");
	decodetcppath("test.log", NULL, port, user, passwd, mntpnt, srctbl);
	strcpy(configFileFath, "./configFile/config.log");
	//fpversion = fopen(configFileFath, "r");
	//if (fpversion == NULL) {
	//	//printf("open file:%s error\n", configFileFath);
	//	fpversion = fopen(configFileFath, "w");
	//	if (fpversion == NULL) {
	//		printf("open file:%s error\n", configFileFath);
	//		return 0;
	//	}	
	//}
	fpversion = fopen(configFileFath, "w");
	if (fpversion == NULL) {
		printf("open file:%s error\n", configFileFath);
		return 0;
	}
	else {
		i = 0;
		while (fgets(buf, 1024, fpversion)) {
			strcpy(buff, buf);
			split(buff, " ", revbuf, &num);
			if (num == 8) {
				printf("enu ini:%s\n", buf);
				i = sscanf(buf, "%d %lf %lf %lf %u %u %u %d",
					&startFlag, &enuAve[0], &enuAve[1], &enuAve[2], &enuAveCnt[0], &enuAveCnt[1], &enuAveCnt[2], &endFlag);
				if (i != 0 && enuAve[0] != 0.0 && enuAve[1] != 0.0 && enuAve[2] != 0.0 && enuAveCnt[0] != 0.0 && enuAveCnt[1] != 0.0 && enuAveCnt[2] != 0.0
					&& startFlag == 1234 && endFlag == 5678) {
					g_rtk.sol.ori_ave[0] = enuAve[0];
					g_rtk.sol.ori_ave[1] = enuAve[1];
					g_rtk.sol.ori_ave[2] = enuAve[2];
					g_rtk.sol.ori_var[0] = 0.02 * 0.02;
					g_rtk.sol.ori_var[1] = 0.02 * 0.02;
					g_rtk.sol.ori_var[2] = 0.05 * 0.05;

					g_rtk.enuWindwoIndex[0] = enuAveCnt[0];
					g_rtk.enuWindwoIndex[1] = enuAveCnt[1];
					g_rtk.enuWindwoIndex[2] = enuAveCnt[2];

					if (g_rtk.opt.senceopt == 1) {
						enuAveCnt[0] = enuAveCnt[1] = enuAveCnt[2] = 10;
					}
					for (i = 0; i < enuAveCnt[0]; i++) {
						g_rtk.enuWindow[0][i] = enuAve[0];
					}
					g_rtk.sum_enu[0] = enuAve[0] * enuAveCnt[0];
					g_rtk.sum_sqeun[0] = enuAve[0] * enuAve[0] * enuAveCnt[0];

					for (i = 0; i < enuAveCnt[1]; i++) {
						g_rtk.enuWindow[1][i] = enuAve[1];
					}
					g_rtk.sum_enu[1] = enuAve[1] * enuAveCnt[1];
					g_rtk.sum_sqeun[1] = enuAve[1] * enuAve[1] * enuAveCnt[1];

					for (i = 0; i < enuAveCnt[2]; i++) {
						g_rtk.enuWindow[2][i] = enuAve[2];
					}
					g_rtk.sum_enu[2] = enuAve[2] * enuAveCnt[2];
					g_rtk.sum_sqeun[2] = enuAve[2] * enuAve[2] * enuAveCnt[2];					
					startFlag = 0; endFlag = 0;
				}
			}
			if (num == 5) {
				printf("rb ini:%s\n", buf);
				i = sscanf(buf, "%d %lf %lf %lf %d",
					&startFlag, &rb[0], &rb[1], &rb[2], &endFlag);
				if (i != 0 && startFlag == 1234 && endFlag == 5678) {
					for (i = 0; i < 3; i++) g_rtk.rb[i] = rb[i];
				}
			}
		}
	}
	if (g_rtk.sol.ori_ave[0] != 0.0 && g_rtk.sol.ori_ave[1] != 0.0 && g_rtk.sol.ori_ave[2] != 0.0 &&
		g_rtk.rb[0] != 0.0 && g_rtk.rb[1] != 0.0 && g_rtk.rb[2] != 0.0) {
		ecef2pos(g_rtk.rb, pos);
		enu2ecef(pos, g_rtk.sol.ori_ave, dr);
		for (i = 0; i < 3; i++) {
			r[i] = g_rtk.rb[i];
			r[i] += dr[i];
		}
		//for (i = 0; i < 3; i++) g_rtk_epoch.staticFixXyz[i] = r[i];
		//for (i = 0; i < 3; i++) g_rtk_epoch.xp[i] = r[i];

		//for (i = 0; i < 3; i++) {
		//	for (j = 0; j < 3; j++) {
		//		if (i == j)
		//			g_rtk_epoch.staticFixP[i + j * 3] = 0.25;
		//		else
		//			g_rtk_epoch.staticFixP[i + j * 3] = 0.0;
		//	}
		//}
	}

	obss.n = 0; obss.nmax = 1024;
	obss.data = (obsd_t*)malloc(sizeof(obsd_t) * obss.nmax);

	sopt.posf = 2; //0:SOLF_LLH  1:SOLF_XYZ  2:SOLF_ENU  3:SOLF_NMEA 4 SOLF_ORI
	sopt.times = 0;//0:GPS时间 1：UTC  2：TIMES_JST  3：北京时间  

	//g_rtk.rb[0] = -2286256.2312; g_rtk.rb[1] = 5003469.9486; g_rtk.rb[2] = 3217157.5013;
	//g_rtk.rb[0] = -1932696.5311; g_rtk.rb[1] = 5112579.3509; g_rtk.rb[2] = 3277660.1313;
	//g_rtk_epoch.rb[0] = -1932696.5311; g_rtk_epoch.rb[1] = 5112579.3509; g_rtk_epoch.rb[2] = 3277660.1313;

	//g_rtk.rb[0] = -2286279.7002; g_rtk.rb[1] = 5003468.2113; g_rtk.rb[2] = 3217155.2669;
	//g_rtk_epoch.rb[0] = -2286279.7002; g_rtk_epoch.rb[1] = 5003468.2113; g_rtk_epoch.rb[2] = 3217155.2669;
	if (access(outDir, 0) != 0)
		mkdir(outDir);

	sprintf(outfile[0], "%s%c%s_%s_%s.pos", outDir, sep, optsys,"rtk",lsqOrkalman);
	sprintf(outfile[1], "%s%c%s_%s_%s.pos", outDir, sep, optsys,"filter", lsqOrkalman);

	if (!(dir = opendir(fileDir))) {
		printf("ERROR: open obsdir failed, please check it!\n");
		system("pause");
		return -1;
	}
	while ((file = readdir(dir)) != NULL) {
		if (strncmp(file->d_name, ".", 1) == 0) continue;
		if (!(ext = strrchr(file->d_name, '.'))) continue;
		if (!strstr(ext, "20o") && !strstr(ext, "21o") && !strstr(ext, "o") && !strstr(ext, "22o")) continue;
		sprintf(filepath, "%s%c%s", fileDir, sep, file->d_name);
		if (indexFile > 2) {
			printf("obs file too more\n");
			closedir(dir);
			system("pause");
			return -1;
		}
		//-------------------------------------------------------------------------------------------------------------
		if (strncmp(file->d_name, "rover", 4) == 0 || strncmp(file->d_name, "novatel", 4) == 0 || strncmp(file->d_name, "ROVER", 4) == 0) {
			strcpy(infile[0], filepath);
			indexFile++;
		}
		if (strncmp(file->d_name, "base", 4) == 0 || strncmp(file->d_name, "BASE", 4) == 0) {
			strcpy(infile[1], filepath);
			indexFile++;
		}
		//strcpy(infile[indexFile++], filepath);
	}
	if (indexFile <= 1) {
		printf("obs file too less\n");
		closedir(dir);
		system("pause");
		return -1;
	}
	closedir(dir);

	//--------------------------------------
	if (!(dir = opendir(fileDir))) {
		printf("ERROR: open obsdir failed, please check it!\n");
		return -1;
	}
	while ((file = readdir(dir)) != NULL) {
		if (strncmp(file->d_name, ".", 1) == 0) continue;
		if (!(ext = strrchr(file->d_name, '.'))) continue;
		if (!strstr(ext, "20n") && !strstr(ext, "21n") && !strstr(ext, "n")) continue;
		sprintf(filepath, "%s%c%s", fileDir, sep, file->d_name);

		if (indexFile >= 6) {
			printf("nav file too more\n");
			closedir(dir);
			system("pause");
			return -1;
		}
		strcpy(infile[indexFile++], filepath);

	}
	if (indexFile <= 2) {
		printf("nav file too less\n");
		closedir(dir);
		system("pause");
		return -1;
	}
	closedir(dir);
	if (access(outDir, 0) != 0)
		mkdir(outDir);

	for (j = 0; j < indexFile; j++) {
		fp[j] = fopen(infile[j], "r");
		if (!(fp[j])) {
			printf("fopen file:%s error!\n", infile[j]);
			system("pause");
			return -1;
		}
	}
	for (i = 0; i < indexFile; i++) {
		printf("get file:%s line...:", infile[i]);
		while (fgets(buf, 4096, fp[i]) != NULL)
			coutFileLine[i]++;
		printf("%d\n", coutFileLine[i]);
		rewind(fp[i]);
	}

	for (i = 0; i < indexFile; i++) {
		printf("READ FILE %s:", infile[i]);
		if (!readrnxh(fp[i], &ver, &type, &sys, &tsys, tobs, &g_nav, &sta, &readFileLine[i], coutFileLine[i])) return 0;
		/* read rinex body */
		switch (type)
		{
		case 'O': readrnxobs(fp[i], ts, te, 0, "ALL", i + 1, ver, &tsys, tobs, &obss, &sta, &readFileLine[i], coutFileLine[i]); break;
		case 'N': readrnxnav(fp[i], "ALL", ver, sys, &g_nav, &readFileLine[i], coutFileLine[i]); break;
		case 'C': readrnxnav(fp[i], "ALL", ver, sys, &g_nav, &readFileLine[i], coutFileLine[i]); break;
		case 'R': readrnxnav(fp[i], "ALL", ver, sys, &g_nav, &readFileLine[i], coutFileLine[i]); break;
		case 'E': readrnxnav(fp[i], "ALL", ver, sys, &g_nav, &readFileLine[i], coutFileLine[i]); break;
		}
		printf("\n");
	}
	printf("\n");
	printf("sort observation data\n");
	/* sort observation data */
	nepoch = sortobs(&obss);
	/* delete duplicated ephemeris */

	/* write header to output file */
	for (i = 0; i < 2; i++) {
		/* write header to output file */
		if (!outhead(outfile[i], infile, indexFile, &g_rtk.opt, &sopt)) {
			//freeobsnav(&obss, &navs);
			return 0;
		}
		oFile.fpOut[i] = fopen(outfile[i], "a");
		if (!oFile.fpOut[i]) return -1;
	}

	//sprintf(outFileName, "%s%c%s", outDir, sep, "trace_mat.log");
	fptest = fopen("test.log", "w");
	if (!fptest)	return -1;


	if (trace_flag[2] == 1) {
		sprintf(outFileName, "%s%c%s", outDir, sep, "spppos_r.log");
		oFile.spppos_r = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "spppos_b.log");
		oFile.spppos_b = fopen(outFileName, "w");
		if (!(oFile.spppos_r && oFile.spppos_b))
			return -1;
	}
	if (trace_flag[3] == 1) {
		sprintf(outFileName, "%s%c%s", outDir, sep, "pdop1.log");
		oFile.pdop1 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "pdop2.log");
		oFile.pdop2 = fopen(outFileName, "w");
		if (!oFile.pdop1 || !oFile.pdop2) return -1;
	}
	//if (trace_flag[4] == 1) {
	//	sprintf(outFileName, "%s%c%s%c%s", fileDir, sep, "result", sep, "P1.log");
	//	oFile.P1 = fopen(outFileName, "w");
	//	sprintf(outFileName, "%s%c%s%c%s", fileDir, sep, "result", sep, "P2.log");
	//	oFile.P2 = fopen(outFileName, "w");
	//	sprintf(outFileName, "%s%c%s%c%s", fileDir, sep, "result", sep, "L1.log");
	//	oFile.L1 = fopen(outFileName, "w");
	//	sprintf(outFileName, "%s%c%s%c%s", fileDir, sep, "result", sep, "L2.log");
	//	oFile.L2 = fopen(outFileName, "w");
	//	if (!(oFile.P1 && oFile.P2 && oFile.L1 && oFile.L2)) return -1;
	//}
	if (trace_flag[6] == 1) {
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp.log");
		oFile.resp = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc1.log");
		oFile.resc1 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp1.log");
		oFile.resp1 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc2.log");
		oFile.resc2 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp2.log");
		oFile.resp2 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc3.log");
		oFile.resc3 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp3.log");
		oFile.resp3 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc4.log");
		oFile.resc4 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc5.log");
		oFile.resc5 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resc6.log");
		oFile.resc6 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp4.log");
		oFile.resp4 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp5.log");
		oFile.resp5 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "resp6.log");
		oFile.resp6 = fopen(outFileName, "w");
		if (!(oFile.resp && oFile.resc1 && oFile.resp1 && oFile.resc2 && oFile.resp2 && oFile.resc3 && oFile.resp3
			&& oFile.resc4 && oFile.resc5 && oFile.resc6 && oFile.resp4 && oFile.resp5 && oFile.resp6)) return -1;
	}
	if (trace_flag[7] == 1) {
		sprintf(outFileName, "%s%c%s", outDir, sep, "elev.log");
		oFile.elev = fopen(outFileName, "w");
		if (!oFile.elev) return -1;
	}
	if (trace_flag[8] == 1) {
		sprintf(outFileName, "%s%c%s", outDir, sep, "mp1.log");
		oFile.mp1 = fopen(outFileName, "w");
		sprintf(outFileName, "%s%c%s", outDir, sep, "mp2.log");
		oFile.mp2 = fopen(outFileName, "w");
		if (!oFile.mp1 || !oFile.mp2) return -1;
		//oFile.gf=fopen("./result/gf.log", "w");    //GF周跳探测量
		//oFile.mw=fopen("./result/mw.log", "w");	 //MW周跳探测量	
		//oFile.lp=fopen("./result/lp.log", "w");    //载波减伪距法周跳探测量
		//if (!oFile.gf||!oFile.mw||!oFile.lp) return -1;
	}
	if (trace_flag[9] == 1) {
		//sprintf(outFileName, "%s%c%s", outDir, sep, "resAmb1.log");
		//oFile.ambN1 = fopen(outFileName, "w");
		//sprintf(outFileName, "%s%c%s", outDir, sep, "resAmb2.log");
		//oFile.ambN2 = fopen(outFileName, "w");
		//sprintf(outFileName, "%s%c%s", outDir, sep, "resAmb3.log");
		//oFile.ambN3 = fopen(outFileName, "w");
		//if (!oFile.ambN1 || !oFile.ambN2 || !oFile.ambN3) return -1;
		sprintf(outFileName, "%s%c%s", outDir, sep, "ion.log");
		oFile.ion = fopen(outFileName, "w");
		if (!oFile.ion) return -1;
		//sprintf(outFileName, "%s%c%s%c%s", fileDir, sep, "result", sep, "trop.log");
		//oFile.trop = fopen(outFileName, "w");
		//if (!oFile.trop) return -1;
	}
	if (trace_flag[10] == 1) {
		//sprintf(outFileName, "%s%c%s", outDir, sep, "debug.log");
		sprintf(outFileName, "%s%c%s_%s_%s.log", outDir, sep, optsys,"debug",lsqOrkalman);
		//sprintf(outFileName, "%s%c%s", outDir, sep, "AllSYS_debug_lsq.log");
		oFile.fpDebug = fopen(outFileName, "w");
		if (!oFile.fpDebug) return -1;
	}

	percent10 = nepoch / 10;
	percent1 = nepoch / 100;
	cout = percent10 > 0 ? 1 : 0;
	printf("handle data:\n");

	iobsu = 0;
	while ((nobs = inputobs(obs, g_rtk.sol.stat, &g_rtk.opt)) >= 0) {
		handObsCount++;
		if (cout) {
			if (handObsCount % percent10 == 0)
			{
				percent = ROUND((handObsCount / (double)nepoch) * 100);
				if (percent % percent10 != 0 && percent % 10 != 0)
					percent++;
				printf("%d%% ", percent);
			}
		}
		/* exclude satellites */

		for (i = n = 0; i < nobs; i++) {
			sys = satsys(obs[i].sat, &prn);
			if (!(g_rtk.opt.sys & 1) && sys == SYS_GPS)continue;
			if (!(g_rtk.opt.sys & 2) && sys == SYS_QZS)continue;
			if (!(g_rtk.opt.sys & 4) && sys == SYS_BDS)continue;
			if (!(g_rtk.opt.sys & 8) && sys == SYS_GAL)continue;
			if (!(g_rtk.opt.sys & 16) && sys == SYS_GLO)continue;
			if (sys == SYS_GPS && g_rtk.opt.gpsMask>=0) {
				if (!((g_rtk.opt.gpsMask >> (prn - 1)) & 1)) continue;
			}
			if (sys == SYS_QZS && g_rtk.opt.qzssMask>=0) {
				if (!((g_rtk.opt.qzssMask >> (prn - MINPRNQZS- 1)) & 1)) continue;
			}
			if (sys == SYS_BDS && g_rtk.opt.bdsMask >= 0) {
				if (!((g_rtk.opt.bdsMask >> (prn - 1)) & 1)) continue;
			}
			if (sys == SYS_GAL && g_rtk.opt.galieoMask >= 0) {
				if (!((g_rtk.opt.galieoMask >> (prn - 1)) & 1)) continue;
			}
			if (sys == SYS_GLO && g_rtk.opt.glonassMask >= 0) {
				if (!((g_rtk.opt.glonassMask >> (prn - 1)) & 1)) continue;
			}
			if (sys == SYS_QZS) continue;
			if (sys == SYS_GPS) continue;
			if (sys == SYS_GAL) continue;
			if (sys == SYS_GLO) continue;
			//if (sys != SYS_BDS) continue;
			//if (prn<5) continue;
			//if (sys == SYS_BDS && prn <= 5) continue;
			//g_rtk.opt.freq = 1;
	
			/*Set frequency*/
			if (!(g_rtk.opt.freq & 1))
			{
				obs[i].P[0] = obs[i].L[0] = obs[i].D[0] = 0.0;
			}
			//else {
			//	if (obs[i].P[0] == 0.0 || obs[i].L[0] == 0.0 || obs[i].SNR[0] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//}
			if (!(g_rtk.opt.freq & 2))
			{
				obs[i].P[1] = obs[i].L[1] = obs[i].D[1] = 0.0;
			}
			//else {
			//	if (obs[i].P[1] == 0.0 || obs[i].L[1] == 0.0 || obs[i].SNR[1] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//}
			if (!(g_rtk.opt.freq & 4))
			{
				obs[i].P[2] = obs[i].L[2] = obs[i].D[2] = 0.0;
			}
			//else {
			//	if (obs[i].P[2] == 0.0 || obs[i].L[2] == 0.0 || obs[i].SNR[2] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//}
			if (!(g_rtk.opt.freq & 8))
			{
				obs[i].P[3] = obs[i].L[3] = obs[i].D[3] = 0.0;
				obs[i].P[4] = obs[i].L[4] = obs[i].D[4] = 0.0;
				obs[i].P[5] = obs[i].L[5] = obs[i].D[5] = 0.0;
			}
			//else {
			//	if (obs[i].P[3] == 0.0 || obs[i].L[3] == 0.0 || obs[i].SNR[3] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//	if (obs[i].P[4] == 0.0 || obs[i].L[4] == 0.0 || obs[i].SNR[4] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//	if (obs[i].P[5] == 0.0 || obs[i].L[5] == 0.0 || obs[i].SNR[5] < g_rtk.opt.cn0Min * 4)
			//		continue;
			//}
			cnt = 0;
			if (sys == SYS_BDS) {
				for (f = 0; f < NFREQ; f++) {
					if (obs[i].P[f] != 0.0) cnt++;
				}
				for (f = 0; f < NFREQ; f++) {
					if (cnt >= 2 && f >= 3)
						obs[i].P[f] = obs[i].L[f] = 0.0;
				}
			}
			for (f = 0; f < NFREQ; f++)
				obs[i].LockTime[f] = 3000;
			obs[n++] = obs[i];
		}
		for (i = 0; i < n; i++) {
			if (obs[i].rcv != 1)
				break;
		}
		nu = i;
		nr = n - nu;
		if (g_baseObsSyncIndex >= 10)
			g_baseObsSyncIndex = 0;
		g_nbaseObsSync[g_baseObsSyncIndex] = 0;
		for (i = nu; i < nu + nr; i++) {
			g_baseObsSync[g_baseObsSyncIndex][i - nu] = obs[i];
			g_nbaseObsSync[g_baseObsSyncIndex]++;
		}
		g_baseObsSyncIndex++;
		//-----------------------------find the closest time--------------------
		dtMin = 9999.9;
		dtMinIndex = -1;
		if (nu > 0) {
			for (j = 0; j < 10; j++) {
				dt[j] = timediff(g_baseObsSync[j][0].time, obs[0].time);
				if (fabs(dt[j]) < fabs(dtMin)) {
					dtMin = dt[j];
					dtMinIndex = j;
				}
			}
			if (dtMinIndex != -1) {
				for (i = 0; i < g_nbaseObsSync[dtMinIndex]; i++) {
					obs[nu + i] = g_baseObsSync[dtMinIndex][i];
				}
				n = nu + g_nbaseObsSync[dtMinIndex];
			}
		}
		if (n <= 0) continue;
		trace(4, "------------rtk dynamics-------------\n");
		if (rtkpos(&g_rtk, obs, n)) {
			g_rtk.sol.stat = SOLQ_NONE;
		}
		else {
			outsol(oFile.fpOut[1], &g_rtk, &g_rtk.sol, g_rtk.rb, &sopt);
			outResult(&g_rtk, &sopt);
		}
		//printf("%s\n", debugBuff);
		if (oFile.fpDebug)	fprintf(oFile.fpDebug, "%s\n", debugBuff);
		gpdebugBuff = debugBuff;

	}
	if (percent == 90)
		printf("%d%% ", 100);
	printf("\n");
	free(g_nav.eph); free(g_nav.geph); free(obss.data);
	free(g_rtk.x); free(g_rtk.P); free(g_rtk.xp); free(g_rtk.Pp);
	free(g_rtk.I); free(g_rtk.H); free(g_rtk.F); free(g_rtk.K);
	free(g_rtk.Ri); free(g_rtk.Rj); free(g_rtk.R); free(g_rtk.v);


	//free(g_rtk_epoch.x); free(g_rtk_epoch.P); free(g_rtk_epoch.xp); free(g_rtk_epoch.Pp);
	//free(g_rtk_epoch.I); free(g_rtk_epoch.H); free(g_rtk_epoch.F); free(g_rtk_epoch.K);
	//free(g_rtk_epoch.Ri); free(g_rtk_epoch.Rj); free(g_rtk_epoch.R); free(g_rtk_epoch.v);

	return 0;
}
