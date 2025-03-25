#include"rtk.h"

#ifdef WIN32
#include <windows.h>
#endif

#define ROUND(x)    (int)floor((x)+0.5)
#define NINCOBS     262144              /* inclimental number of obs data */
//#define FILEPATHSEP '\\'
#define MAXEXFILE   1024                /* max number of expanded files */
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

typedef struct {                        /* signal index type */
	int n;                              /* number of index */
	int frq[MAXOBSTYPE];                /* signal frequency (1:L1,2:L2,...) */
	int pos[MAXOBSTYPE];                /* signal index in obs data (-1:no) */
	unsigned char pri[MAXOBSTYPE];     /* signal priority (15-0) */
	unsigned char type[MAXOBSTYPE];     /* type (0:C,1:L,2:D,3:S) */
	unsigned char code[MAXOBSTYPE];     /* obs code (CODE_L??) */
	double shift[MAXOBSTYPE];           /* phase shift (cycle) */
} sigind_t;

#define SOLF_LLH    0                   /* solution format: lat/lon/height */
#define SOLF_XYZ    1                   /* solution format: x/y/z-ecef */
#define SOLF_ENU    2                   /* solution format: e/n/u-baseline */
#define SOLF_NMEA   3                   /* solution format: NMEA-183 */
#define SOLF_STAT   4                   /* solution format: solution status */
#define SOLF_GSIF   5                   /* solution format: GSI F1/F2 */
#define COMMENTH    "%"                 /* comment line indicator for solution */
#define VER_RTKLIB  "2.4.3"             /* library version */

static const char obscodes1[] = "CLDS";    /* obs type codes */

extern double str2num(const char *s, int i, int n)
{
	double value;
	char str[256],*p=str;

	if (i<0||(int)strlen(s)<i||(int)sizeof(str)-1<n) return 0.0;
	for (s+=i;*s&&--n>=0;s++) *p++=*s=='d'||*s=='D'?'E':*s;
	*p='\0';
	return sscanf(str,"%lf",&value)==1?value:0.0;
}
static double str2num2(const char* s, int i, int n)
{
	double value;
	char str[256], * p = str;

	if (i < 0 || (int)strlen(s) < i || (int)sizeof(str) - 1 < n) return 0.0;
	for (s += i;*s && --n >= 0;s++) *p++ = *s == 'd' || *s == 'D' ? 'E' : *s;
	*p = '\0';
	sscanf(str, "%lf", &value);
	if (value > 0 && value < 255)
		return value;
	else
		return 0.0;
}
static void setstr(char *dst, const char *src, int n)
{
	char *p=dst;
	const char *q=src;
	while (*q&&q<src+n) *p++=*q++;
	*p--='\0';
	while (p>=dst&&*p==' ') *p--='\0';
}

static int str2time(const char *s, int i, int n, gtime_t *t)
{
	double ep[6];
	char str[256], *p = str;

	if (i < 0 || (int)strlen(s) < i || (int)sizeof(str) - 1 < i) return -1;
	for (s += i; *s&&--n >= 0;) *p++ = *s++;
	*p = '\0';
	if (sscanf(str, "%lf %lf %lf %lf %lf %lf", ep, ep + 1, ep + 2, ep + 3, ep + 4, ep + 5) < 6)
		return -1;
	if (ep[0] < 100.0) ep[0] += ep[0] < 80.0 ? 2000.0 : 1900.0;
	*t = epoch2time(ep);
	return 0;
}


static void convcode(double ver, unsigned char sys, const char *str, char *type)
{
	strcpy(type,"   ");

	if      (!strcmp(str,"P1")) { /* ver.2.11 GPS L1PY,GLO L2P */
		if      (sys==SYS_GPS) sprintf(type,"%c1W",'C');
		else if (sys==SYS_GLO) sprintf(type,"%c1P",'C');
	}
	else if (!strcmp(str,"P2")) { /* ver.2.11 GPS L2PY,GLO L2P */
		if      (sys==SYS_GPS) sprintf(type,"%c2W",'C');
		else if (sys==SYS_GLO) sprintf(type,"%c2P",'C');
	}
	else if (!strcmp(str,"C1")) { /* ver.2.11 GPS L1C,GLO L1C/A */
		if      (ver>=2.12) ; /* reject C1 for 2.12 */
		else if (sys==SYS_GPS) sprintf(type,"%c1C",'C');
		else if (sys==SYS_GLO) sprintf(type,"%c1C",'C');
		else if (sys==SYS_GAL) sprintf(type,"%c1X",'C'); /* ver.2.12 */
		else if (sys==SYS_QZS) sprintf(type,"%c1C",'C');
		else if (sys==SYS_SBS) sprintf(type,"%c1C",'C');
	}
	else if (!strcmp(str,"C2")) {
		if (sys==SYS_GPS) {
			if (ver>=2.12) sprintf(type,"%c2W",'C'); /* L2P(Y) */
			else           sprintf(type,"%c2X",'C'); /* L2C */
		}
		else if (sys==SYS_GLO) sprintf(type,"%c2C",'C');
		else if (sys==SYS_QZS) sprintf(type,"%c2X",'C');
		else if (sys==SYS_BDS) sprintf(type,"%c1X",'C'); /* ver.2.12 B1 */
	}
	else if (ver>=2.12&&str[1]=='A') { /* ver.2.12 L1C/A */
		if      (sys==SYS_GPS) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_GLO) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_SBS) sprintf(type,"%c1C",str[0]);
	}
	else if (ver>=2.12&&str[1]=='B') { /* ver.2.12 GPS L1C */
		if      (sys==SYS_GPS) sprintf(type,"%c1X",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c1X",str[0]);
	}
	else if (ver>=2.12&&str[1]=='C') { /* ver.2.12 GPS L2C */
		if      (sys==SYS_GPS) sprintf(type,"%c2X",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c2X",str[0]);
	}
	else if (ver>=2.12&&str[1]=='D') { /* ver.2.12 GLO L2C/A */
		if      (sys==SYS_GLO) sprintf(type,"%c2C",str[0]);
	}
	else if (ver>=2.12&&str[1]=='1') { /* ver.2.12 GPS L1PY,GLO L1P */
		if      (sys==SYS_GPS) sprintf(type,"%c1W",str[0]);
		else if (sys==SYS_GLO) sprintf(type,"%c1P",str[0]);
		else if (sys==SYS_GAL) sprintf(type,"%c1X",str[0]); /* tentative */
		else if (sys==SYS_BDS) sprintf(type,"%c1X",str[0]); /* extension */
	}
	else if (ver<2.12&&str[1]=='1') {
		if      (sys==SYS_GPS) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_GLO) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_GAL) sprintf(type,"%c1X",str[0]); /* tentative */
		else if (sys==SYS_QZS) sprintf(type,"%c1C",str[0]);
		else if (sys==SYS_SBS) sprintf(type,"%c1C",str[0]);
	}
	else if (str[1]=='2') {
		if      (sys==SYS_GPS) sprintf(type,"%c2W",str[0]);
		else if (sys==SYS_GLO) sprintf(type,"%c2P",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c2X",str[0]);
		else if (sys==SYS_BDS) sprintf(type,"%c1X",str[0]); /* ver.2.12 B1 */
	}
	else if (str[1]=='5') {
		if      (sys==SYS_GPS) sprintf(type,"%c5X",str[0]);
		else if (sys==SYS_GAL) sprintf(type,"%c5X",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c5X",str[0]);
		else if (sys==SYS_SBS) sprintf(type,"%c5X",str[0]);
	}
	else if (str[1]=='6') {
		if      (sys==SYS_GAL) sprintf(type,"%c6X",str[0]);
		else if (sys==SYS_QZS) sprintf(type,"%c6X",str[0]);
		else if (sys==SYS_BDS) sprintf(type,"%c6X",str[0]); /* ver.2.12 B3 */
	}
	else if (str[1]=='7') {
		if      (sys==SYS_GAL) sprintf(type,"%c7X",str[0]);
		else if (sys==SYS_BDS) sprintf(type,"%c7X",str[0]); /* ver.2.12 B2 */
	}
	else if (str[1]=='8') {
		if      (sys==SYS_GAL) sprintf(type,"%c8X",str[0]);
	}
	// //trace(3,"convcode: ver=%.2f sys=%2d type= %s -> %s\n",ver,sys,str,type);
}

extern int satid2no(const char *id)
{
	int sys,prn;
	char code;

	if (sscanf(id,"%d",&prn)==1) {
		if      (MINPRNGPS<=prn&&prn<=MAXPRNGPS) sys=SYS_GPS;
		else if (MINPRNSBS<=prn&&prn<=MAXPRNSBS) sys=SYS_SBS;
		else if (MINPRNQZS<=prn&&prn<=MAXPRNQZS) sys=SYS_QZS;
		else return 0;
		return satno(sys,prn);
	}
	if (sscanf(id,"%c%d",&code,&prn)<2) return 0;

	switch (code) {
	case 'G': sys=SYS_GPS; prn+=MINPRNGPS-1; break;
	case 'R': sys=SYS_GLO; prn+=MINPRNGLO-1; break;
	case 'E': sys=SYS_GAL; prn+=MINPRNGAL-1; break;
	case 'J': sys=SYS_QZS; prn+=MINPRNQZS-1; break;
	case 'C': sys=SYS_BDS; prn+=MINPRNBDS-1; break;
	case 'L': sys=SYS_LEO; prn+=MINPRNLEO-1; break;
	case 'S': sys=SYS_SBS; prn+=100; break;
	default: return 0;
	}
	return satno(sys,prn);
}

/* decode obs epoch ----------------------------------------------------------*/
extern int decode_obsepoch(FILE *fp, char *buff, double ver, gtime_t *time,
	int *flag, int *sats)
{
	int i, j, n;
	char satid[8] = "";

	////trace(4, "decode_obsepoch: ver=%.2f\n", ver);

	if (ver <= 2.99) { /* ver.2 */
		if ((n = (int)str2num(buff, 29, 3)) <= 0) return 0;

		/* epoch flag: 3:new site,4:header info,5:external event */
		*flag = (int)str2num(buff, 28, 1);

		if (3 <= *flag&&*flag <= 5) return n;

		if (str2time(buff, 0, 26, time)) {
			////trace(2, "rinex obs invalid epoch: epoch=%26.26s\n", buff);
			return 0;
		}
		for (i = 0, j = 32; i < n; i++, j += 3) {
			if (j >= 68) {
				if (!fgets(buff, MAXRNXLEN, fp)) break;
				j = 32;
			}
			if (i < MAXOBS) {
				strncpy(satid, buff + j, 3);
				sats[i] = satid2no(satid);
			}
		}
	}
	else { /* ver.3 */
		if ((n = (int)str2num(buff, 32, 3)) <= 0) return 0;

		*flag = (int)str2num(buff, 31, 1);

		if (3 <= *flag&&*flag <= 5) return n;

		if (buff[0] != '>' || str2time(buff, 1, 28, time)) {
			////trace(2, "rinex obs invalid epoch: epoch=%29.29s\n", buff);
			return 0;
		}
	}
	////trace(4, "decode_obsepoch: time=%s flag=%d\n", time_str(*time, 3), *flag);
	return n;
}

/* set system mask -----------------------------------------------------------*/
static int set_sysmask(const char *opt)
{
	const char *p;
	int mask = SYS_NONE;

	if (!(p = strstr(opt, "-SYS="))) return SYS_ALL;

	for (p += 5; *p&&*p != ' '; p++) {
		switch (*p) {
		case 'G': mask |= SYS_GPS; break;
		case 'R': mask |= SYS_GLO; break;
		case 'E': mask |= SYS_GAL; break;
		case 'J': mask |= SYS_QZS; break;
		case 'C': mask |= SYS_BDS; break;
		case 'S': mask |= SYS_SBS; break;
		}
	}
	return mask;
}


/* set signal index ----------------------------------------------------------*/
static void set_index(double ver, unsigned char sys, const char *opt,
	char tobs[MAXOBSTYPE][4], sigind_t *ind)
{
	const char *p;
	char str[8], *optstr = "";
	double shift;
	int i, j, k, n;
	for (i = n = 0; *tobs[i]; i++, n++) {
		ind->code[i] = obs2code(tobs[i] + 1, ind->frq + i);
		ind->type[i] = (p = strchr(obscodes1, tobs[i][0])) ? (int)(p - obscodes1) : 0;
		ind->pri[i] = getcodepri(sys, ind->code[i], opt);
		ind->pos[i] = -1;

		/* frequency index for beidou */
		//if (sys == SYS_BDS) {
		//	if (ind->frq[i] == 2 && ind->code[i] != 56) ind->frq[i] = 1; /* B1 */
		//	else if (ind->frq[i] == 5) ind->frq[i] = 2; /* B2 */
		//	else if (ind->frq[i] == 4) ind->frq[i] = 3; /* B3 */
		//	else if (ind->code[i] == 56 || ind->code[i] == 2) ind->frq[i] = 4; /* B1C */
		//	else if (ind->code[i] == 57 || ind->code[i] == 59) ind->frq[i] = 5; /* B2a*/
		//	else if (ind->code[i] == 58) ind->frq[i] = 6; /* B2b*/
		//}
		if (sys == SYS_BDS) {
			//if (ind->frq[i] == 5) ind->frq[i] = 2; /* B2 */
			//else if (ind->frq[i] == 4) ind->frq[i] = 3; /* B3 */
			//if (ind->frq[i] == 2 && ind->code[i] != 56) ind->frq[i] = 1; /* B1 */
			if (ind->code[i] == CODE_L1I)
				ind->frq[i] = 1; /* B1 */
			else if (ind->code[i] == CODE_L7I)
				ind->frq[i] = 2; /* B2 */
			else if (ind->code[i] == CODE_L5D)
				ind->frq[i] = 3; /* B2a*/
			else if (ind->code[i] == CODE_L7D)
				ind->frq[i] = 4; /* B2b*/
			else if (ind->code[i] == CODE_L1D)
				ind->frq[i] = 5; /* B1C */
			else if (ind->code[i] == CODE_L6I)
				ind->frq[i] = 6; /* B3 */
			else {
				ind->code[i] = 0;
				ind->frq[i] = -1;
			}
		}
		if(sys == SYS_GAL) {
			if (ind->code[i]== CODE_L5Q) ind->frq[i] = 3; /* E5A */
			else if (ind->code[i] == CODE_L7Q) ind->frq[i] = 2; /* E5B */
		}
	}
	/* parse phase shift options */
	switch (sys) {
	case SYS_GPS: optstr = "-GL%2s=%lf"; break;
	case SYS_GLO: optstr = "-RL%2s=%lf"; break;
	case SYS_GAL: optstr = "-EL%2s=%lf"; break;
	case SYS_QZS: optstr = "-JL%2s=%lf"; break;
	case SYS_SBS: optstr = "-SL%2s=%lf"; break;
	case SYS_BDS: optstr = "-CL%2s=%lf"; break;
	}
	for (p = opt; p && (p = strchr(p, '-')); p++) {
		if (sscanf(p, optstr, str, &shift) < 2) continue;
		for (i = 0; i < n; i++) {
			if (strcmp(code2obs(ind->code[i], NULL), str)) continue;
			ind->shift[i] = shift;
			////trace(2, "phase shift: sys=%2d tobs=%s shift=%.3f\n", sys,
			//	tobs[i], shift);
		}
	}
	/* assign index for highest priority code */
	for (i = 0; i < NFREQ; i++) {
		for (j = 0, k = -1; j < n; j++) {
			if (ind->frq[j] == i + 1 && ind->pri[j] && (k<0 || ind->pri[j]>ind->pri[k])) {
				k = j;
			}
		}
		if (k < 0) continue;

		for (j = 0; j < n; j++) {
			if (ind->code[j] == ind->code[k]) 
				ind->pos[j] = i;
		}
	}
	/* assign index of extended obs data */
	for (i = 0; i < 0; i++) {
		for (j = 0; j < n; j++) {
			if (ind->code[j] && ind->pri[j] && ind->pos[j] < 0) break;
		}
		if (j >= n) break;

		for (k = 0; k < n; k++) {
			if (ind->code[k] == ind->code[j]) ind->pos[k] = NFREQ + i;
		}
	}
	for (i = 0; i < n; i++) {
		if (!ind->code[i] || !ind->pri[i] || ind->pos[i] >= 0) continue;
		////trace(4, "reject obs type: sys=%2d, obs=%s\n", sys, tobs[i]);
	}
	ind->n = n;

#if 0 /* for debug */
	for (i = 0; i < n; i++) {
		//trace(2, "set_index: sys=%2d,tobs=%s code=%2d pri=%2d frq=%d pos=%d shift=%5.2f\n",
			sys, tobs[i], ind->code[i], ind->pri[i], ind->frq[i], ind->pos[i],
			ind->shift[i]);
	}
#endif
}


/* decode obs data -----------------------------------------------------------*/
static int decode_obsdata(FILE *fp, char *buff, double ver, int mask,
	sigind_t *index, obsd_t *obs)
{
	sigind_t *ind;
	double val[MAXOBSTYPE] = { 0 };
	unsigned char lli[MAXOBSTYPE] = { 0 };
	char satid[8] = "";
	int i, j, n, m, sys,stat = 1, p[MAXOBSTYPE], k[16], l[16];
	////trace(4, "decode_obsdata: ver=%.2f\n", ver);

	if (ver > 2.99) { /* ver.3 */
		strncpy(satid, buff, 3);
		obs->sat = (unsigned char)satid2no(satid);
	}
	if (!obs->sat) {
		////trace(4, "decode_obsdata: unsupported sat sat=%s\n", satid);
		stat = 0;
	}
	else {
		sys=satsys(obs->sat, NULL);
		if(sys==SYS_NONE)
			stat = 0;
	}
	/* read obs data fields */
	switch (satsys(obs->sat, NULL)) {
	case SYS_GLO: ind = index + 1; break;
	case SYS_GAL: ind = index + 2; break;
	case SYS_QZS: ind = index + 3; break;
	case SYS_SBS: ind = index + 4; break;
	case SYS_BDS: ind = index + 5; break;
	default:      ind = index; break;
	}
	for (i = 0, j = ver <= 2.99 ? 0 : 3; i < ind->n; i++, j += 16) {

		if (ver <= 2.99&&j >= 80) { /* ver.2 */
			if (!fgets(buff, MAXRNXLEN, fp)) break;
			j = 0;
		}
		if (stat) {
			val[i] = str2num(buff, j, 14) + ind->shift[i];
			lli[i] = (unsigned char)str2num(buff, j + 14, 1) & 3;
		}
	}
	if (!stat) return 0;

	for (i = 0; i < NFREQ; i++) {
		obs->P[i] = obs->L[i] = 0.0; obs->D[i] = 0.0f;
		obs->SNR[i] = obs->LLI[i] = obs->code[i] = 0;
	}
	/* assign position in obs data */
	for (i = n = m = 0; i < ind->n; i++) {

		p[i] = ver <= 2.11 ? ind->frq[i] - 1 : ind->pos[i];

		if (ind->type[i] == 0 && p[i] == 0) k[n++] = i; /* C1? index */
		if (ind->type[i] == 0 && p[i] == 1) l[m++] = i; /* C2? index */
	}
	if (ver <= 2.11) {

		/* if multiple codes (C1/P1,C2/P2), select higher priority */
		if (n >= 2) {
			if (val[k[0]] == 0.0&&val[k[1]] == 0.0) {
				p[k[0]] = -1; p[k[1]] = -1;
			}
			else if (val[k[0]] != 0.0&&val[k[1]] == 0.0) {
				p[k[0]] = 0; p[k[1]] = -1;
			}
			else if (val[k[0]] == 0.0&&val[k[1]] != 0.0) {
				p[k[0]] = -1; p[k[1]] = 0;
			}
			else if (ind->pri[k[1]] > ind->pri[k[0]]) {
				p[k[1]] = 0; p[k[0]] = 0 < 1 ? -1 : NFREQ;
			}
			else {
				p[k[0]] = 0; p[k[1]] = 0 < 1 ? -1 : NFREQ;
			}
		}
		if (m >= 2) {
			if (val[l[0]] == 0.0&&val[l[1]] == 0.0) {
				p[l[0]] = -1; p[l[1]] = -1;
			}
			else if (val[l[0]] != 0.0&&val[l[1]] == 0.0) {
				p[l[0]] = 1; p[l[1]] = -1;
			}
			else if (val[l[0]] == 0.0&&val[l[1]] != 0.0) {
				p[l[0]] = -1; p[l[1]] = 1;
			}
			else if (ind->pri[l[1]] > ind->pri[l[0]]) {
				p[l[1]] = 1; p[l[0]] = 0 < 2 ? -1 : NFREQ + 1;
			}
			else {
				p[l[0]] = 1; p[l[1]] = 0 < 2 ? -1 : NFREQ + 1;
			}
		}
	}
	/* save obs data */
	for (i = 0; i < ind->n; i++) {
		if (p[i] < 0 || val[i] == 0.0) continue;
		switch (ind->type[i]) {
		case 0: obs->P[p[i]] = val[i]; obs->code[p[i]] = ind->code[i]; break;
		case 1: obs->L[p[i]] = val[i]; obs->LLI[p[i]] = lli[i];       break;
		case 2: obs->D[p[i]] = (float)val[i];                        break;
		case 3: obs->SNR[p[i]] = (unsigned char)(val[i] * 4.0 + 0.5);    break;
		}
	}
	////trace(4, "decode_obsdata: time=%s sat=%2d\n", time_str(obs->time, 0), obs->sat);
	return 1;
}

/* read rinex obs data body --------------------------------------------------*/
extern int readrnxobsb(FILE* fp, const char* opt, double ver, int* tsys,
	char tobs[][MAXOBSTYPE][4], int* flag, obsd_t* data, sta_t* sta, int* lineCount, int maxLine)
{
	gtime_t time = { 0 };
	sigind_t index[7] = { {0} };
	char buff[MAXRNXLEN];
	int i = 0, n = 0, nsat = 0, sats[MAXOBS] = { 0 }, mask;
	int percent10 = maxLine / 10;
	int percent1 = maxLine / 100;
	int cout = percent10 > 0 ? 1 : 0;
	int percent;
	/* set system mask */
	mask = set_sysmask(opt);

	/* set signal index */
	set_index(ver, SYS_GPS, opt, tobs[0], index);
	set_index(ver, SYS_GLO, opt, tobs[1], index + 1);
	set_index(ver, SYS_GAL, opt, tobs[2], index + 2);
	set_index(ver, SYS_QZS, opt, tobs[3], index + 3);
	set_index(ver, SYS_SBS, opt, tobs[4], index + 4);
	set_index(ver, SYS_BDS, opt, tobs[5], index + 5);

	while (fgets(buff, MAXRNXLEN, fp)) {
		*lineCount = *lineCount + 1;
		if (cout) {
			if (*lineCount % percent10 == percent10 - 1)
			{
				percent = ROUND((*lineCount / (double)maxLine) * 100);
				if (percent % percent10 != 0 && percent % 10 != 0)
					percent++;
				printf("%d%% ", percent);
			}
		}
		/* decode obs epoch */
		if (i == 0) {
			if ((nsat = decode_obsepoch(fp, buff, ver, &time, flag, sats)) <= 0) {
				continue;
			}
		}
		else if (*flag <= 2 || *flag == 6) {

			data[n].time = time;
			data[n].sat = (unsigned char)sats[i - 1];

			/* decode obs data */
			if (decode_obsdata(fp, buff, ver, mask, index, data + n) && n < MAXOBS) n++;
		}
		else if (*flag == 3 || *flag == 4) { /* new site or header info follows */

			/* decode obs header */
			decode_obsh(fp, buff, ver, tsys, tobs, NULL, sta);
		}
		if (++i > nsat)
			return n;
	}
	return -1;
}
/* save slips ----------------------------------------------------------------*/
static void saveslips(unsigned char slips[][NFREQ], obsd_t *data)
{
	int i;
	for (i = 0; i < NFREQ; i++) {
		if (data->LLI[i] & 1) slips[data->sat - 1][i] |= LLI_SLIP;
	}
}
static int screent(gtime_t time, gtime_t ts, gtime_t te, double tint)
{
	return (tint <= 0.0 || fmod(time2gpst(time, NULL) + DTTOL, tint) <= DTTOL * 2.0) &&
		(ts.time == 0 || timediff(time, ts) >= -DTTOL) &&
		(te.time == 0 || timediff(time, te) < DTTOL);
}
/* restore slips -------------------------------------------------------------*/
static void restslips(unsigned char slips[][NFREQ], obsd_t *data)
{
	int i;
	for (i = 0; i < NFREQ; i++) {
		if (slips[data->sat - 1][i] & 1) 
			data->LLI[i] |= LLI_SLIP;
		slips[data->sat - 1][i] = 0;
	}
}

/* add obs data --------------------------------------------------------------*/
static int addobsdata(obs_t *obs, const obsd_t *data)
{
	obsd_t *obs_data;
	if (obs->nmax <= obs->n) {
		if (obs->nmax <= 0) 
			obs->nmax = NINCOBS; 
		else 
			obs->nmax *= 2;
		if (!(obs_data = (obsd_t *)realloc(obs->data, sizeof(obsd_t)*obs->nmax))) {
			printf( "addobsdata: memalloc error n=%dx%d\n", sizeof(obsd_t), obs->nmax);
			free(obs->data); obs->data = NULL; obs->n = obs->nmax = 0;
			exit(-1);
		}
		obs->data = obs_data;
	}
	obs->data[obs->n++] = *data;
	return 1;
}

static int trim_obs(obs_t* obs, gtime_t ts, gtime_t te, int navsys) {

    int i = 0;
    int ix = 0;
    double TMAX = 1.0; /* 1s */
    int sys = 0;

    for (i = 0; i < obs->n; i++) {
        sys = satsys(obs->data[i].sat, NULL);
        if (!(sys & navsys)) continue;

        if ((ts.time != 0) && (timediff(ts, obs->data[i].time) > TMAX)) continue;
        if ((te.time != 0) && (timediff(obs->data[i].time, te) > TMAX)) continue;


        obs->data[ix] = obs->data[i];
        ix++;

    }
    obs->n = ix;
    return 0;
}

/* decode obs header ---------------------------------------------------------*/
extern void decode_obsh(FILE *fp, char *buff, double ver, int *tsys,
	char tobs[][MAXOBSTYPE][4], nav_t *nav, sta_t *sta)
{
	/* default codes for unknown code */
	const char *defcodes[]={
		"CWX    ",  /* GPS: L125____ */
		"CC     ",  /* GLO: L12_____ */
		"X XXXX ",  /* GAL: L1_5678_ */
		"CXXX   ",  /* QZS: L1256___ */
		"C X    ",  /* SBS: L1_5____ */
		"X  XX  ",  /* BDS: L1__67__ */
		"  A   A"   /* IRN: L__5___9 */
	};
	double del[3];
	int i,j,k,n,nt,fcn;
	int prn;
	const char *p;
	char *label=buff+60,str[4];
	char test;

	if      (strstr(label,"MARKER NAME"         )) {
		if (sta) setstr(sta->name,buff,60);
	}
	else if (strstr(label,"MARKER NUMBER"       )) { /* opt */
		if (sta) setstr(sta->marker,buff,20);
	}
	else if (strstr(label,"MARKER TYPE"         )) ; /* ver.3 */
	else if (strstr(label,"OBSERVER / AGENCY"   )) ;
	else if (strstr(label,"REC # / TYPE / VERS" )) {
		if (sta) 
		{
			setstr(sta->recsno, buff,   20);
			setstr(sta->rectype,buff+20,20);
			setstr(sta->recver, buff+40,20);
		}
	}
	else if (strstr(label,"ANT # / TYPE"        )) {
		if (sta) {
			setstr(sta->antsno,buff   ,20);
			setstr(sta->antdes,buff+20,20);
		}
	}
	else if (strstr(label,"APPROX POSITION XYZ" )) {
		if (sta) {
			for (i=0,j=0;i<3;i++,j+=14) sta->pos[i]=str2num(buff,j,14);
		}
	}
	else if (strstr(label,"ANTENNA: DELTA H/E/N")) {
		if (sta) {
			for (i=0,j=0;i<3;i++,j+=14) del[i]=str2num(buff,j,14);
			sta->del[2]=del[0]; /* h */
			sta->del[0]=del[1]; /* e */
			sta->del[1]=del[2]; /* n */
		}
	}
	else if (strstr(label,"ANTENNA: DELTA X/Y/Z")) ; /* opt ver.3 */
	else if (strstr(label,"ANTENNA: PHASECENTER")) ; /* opt ver.3 */
	else if (strstr(label,"ANTENNA: B.SIGHT XYZ")) ; /* opt ver.3 */
	else if (strstr(label,"ANTENNA: ZERODIR AZI")) ; /* opt ver.3 */
	else if (strstr(label,"ANTENNA: ZERODIR XYZ")) ; /* opt ver.3 */
	else if (strstr(label,"CENTER OF MASS: XYZ" )) ; /* opt ver.3 */
	else if (strstr(label,"SYS / # / OBS TYPES" )) { /* ver.3 */
		if (!(p=strchr(syscodes,buff[0]))) {
			// //trace(2,"invalid system code: sys=%c\n",buff[0]);
			return;
		}
		i=(int)(p-syscodes);
		n=(int)str2num(buff,3,3);
		for (j=nt=0,k=7;j<n;j++,k+=4) {
			if (k>58) {
				if (!fgets(buff,MAXRNXLEN,fp)) break;
				k=7;
			}
			if (nt<MAXOBSTYPE-1) setstr(tobs[i][nt++],buff+k,3);
		}
		*tobs[i][nt]='\0';

		/* change beidou B1 code: 3.02 draft -> 3.02/3.03 */
		if (i==5) {
			for (j=0;j<nt;j++) if (tobs[i][j][1]=='2') tobs[i][j][1]='1';
		}
		/* if unknown code in ver.3, set default code */
		for (j=0;j<nt;j++) {
			test=tobs[i][j][2];
			if (tobs[i][j][2]) continue;
			if (!(p=strchr(frqcodes,tobs[i][j][1]))) continue;
			tobs[i][j][2]=defcodes[i][(int)(p-frqcodes)];
		}
	}
	else if (strstr(label,"WAVELENGTH FACT L1/2")) ; /* opt ver.2 */
	else if (strstr(label,"# / TYPES OF OBSERV" )) { /* ver.2 */
		n=(int)str2num(buff,0,6);
		for (i=nt=0,j=10;i<n;i++,j+=6) {
			if (j>58) {
				if (!fgets(buff,MAXRNXLEN,fp)) break;
				j=10;
			}
			if (nt>=MAXOBSTYPE-1) continue;
			if (ver<=2.99) {
				setstr(str,buff+j,2);
				convcode(ver,SYS_GPS,str,tobs[0][nt]);
				convcode(ver,SYS_GLO,str,tobs[1][nt]);
				convcode(ver,SYS_GAL,str,tobs[2][nt]);
				convcode(ver,SYS_QZS,str,tobs[3][nt]);
				convcode(ver,SYS_SBS,str,tobs[4][nt]);
				convcode(ver,SYS_BDS,str,tobs[5][nt]);
			}
			nt++;
		}
		*tobs[0][nt]='\0';
	}
	else if (strstr(label,"SIGNAL STRENGTH UNIT")) ; /* opt ver.3 */
	else if (strstr(label,"INTERVAL"            )) ; /* opt */
	else if (strstr(label,"TIME OF FIRST OBS"   )) {
		if      (!strncmp(buff+48,"GPS",3)) *tsys=TSYS_GPS;
		else if (!strncmp(buff+48,"GLO",3)) *tsys=TSYS_UTC;
		else if (!strncmp(buff+48,"GAL",3)) *tsys=TSYS_GAL;
		else if (!strncmp(buff+48,"QZS",3)) *tsys=TSYS_QZS; /* ver.3.02 */
		else if (!strncmp(buff+48,"BDT",3)) *tsys=TSYS_CMP; /* ver.3.02 */
		else if (!strncmp(buff+48,"IRN",3)) *tsys=TSYS_IRN; /* ver.3.03 */
	}
	else if (strstr(label,"TIME OF LAST OBS"    )) ; /* opt */
	else if (strstr(label,"RCV CLOCK OFFS APPL" )) ; /* opt */
	else if (strstr(label,"SYS / DCBS APPLIED"  )) ; /* opt ver.3 */
	else if (strstr(label,"SYS / PCVS APPLIED"  )) ; /* opt ver.3 */
	else if (strstr(label,"SYS / SCALE FACTOR"  )) ; /* opt ver.3 */
	else if (strstr(label,"SYS / PHASE SHIFTS"  )) ; /* ver.3.01 */
	else if (strstr(label,"GLONASS SLOT / FRQ #")) { /* ver.3.02 */
		if (nav) {
			for (i=0,p=buff+4;i<8;i++,p+=8) {
				if (sscanf(p,"R%2d %2d",&prn,&fcn)<2) continue;
				//if (1<=prn&&prn<=MAXPRNGLO) nav->glo_fcn[prn-1]=fcn+8;
			}
		}
	}
	//else if (strstr(label,"GLONASS COD/PHS/BIS" )) { /* ver.3.02 */
	//	if (nav) {
	//		for (i=0,p=buff;i<4;i++,p+=13) {
	//			if      (strncmp(p+1,"C1C",3)) nav->glo_cpbias[0]=str2num(p,5,8);
	//			else if (strncmp(p+1,"C1P",3)) nav->glo_cpbias[1]=str2num(p,5,8);
	//			else if (strncmp(p+1,"C2C",3)) nav->glo_cpbias[2]=str2num(p,5,8);
	//			else if (strncmp(p+1,"C2P",3)) nav->glo_cpbias[3]=str2num(p,5,8);
	//		}
	//	}
	//}
	//else if (strstr(label,"LEAP SECONDS"        )) { /* opt */
	//	if (nav) nav->leaps=(int)str2num(buff,0,6);
	//}
	else if (strstr(label,"# OF SALTELLITES"    )) { /* opt */
		/* skip */ ;
	}
	else if (strstr(label,"PRN / # OF OBS"      )) { /* opt */
		/* skip */ ;
	}
}


extern int readrnxh(FILE* fp, double* ver, char* type, unsigned char* sys, int* tsys,
	char tobs[][MAXOBSTYPE][4], nav_t* nav, sta_t* sta, int* lineCount, int maxLine)
{
	double bias;
	char buff[MAXRNXLEN], * label = buff + 60;
	unsigned char sat;
	int i = 0, block = 0;
	int percent10 = maxLine / 10;
	int percent, percent1 = maxLine / 100;
	int cout = percent10 > 0 ? 1 : 0;
	*ver = 2.10; *type = ' '; *sys = SYS_GPS;

	while (fgets(buff, MAXRNXLEN, fp)) {
		*lineCount = *lineCount + 1;
		if (cout) {
			if (*lineCount % percent10 == percent10 - 1)
			{
				percent = ROUND((*lineCount / (double)maxLine) * 100);
				if (percent % percent10 != 0 && percent % 10 != 0)
					percent++;
				printf("%d%% ", percent);
			}
		}
		if (strlen(buff) <= 60) continue;

		else if (strstr(label, "RINEX VERSION / TYPE")) {
			*ver = str2num(buff, 0, 9);

			*type = *(buff + 20);
			/* satellite system */
			switch (*type) {
			case ' ':
			case 'N': *sys = SYS_GPS;  *tsys = TSYS_GPS; break;
			case 'G': *sys = SYS_GLO;  *tsys = TSYS_UTC; break;
			case 'E': *sys = SYS_GAL;  *tsys = TSYS_GAL; break; /* v.2.12 */
			case 'S': *sys = SYS_SBS;  *tsys = TSYS_GPS; break;
			case 'J': *sys = SYS_QZS;  *tsys = TSYS_QZS; break; /* v.3.02 */
			case 'C': *sys = SYS_BDS;  *tsys = TSYS_CMP; break; /* v.2.12 */
			case 'M': *sys = SYS_NONE; *tsys = TSYS_GPS; break; /* mixed */
			default:
				break;
			}
			continue;

		}
		else if (strstr(label, "PGM / RUN BY / DATE"))
			continue;
		else if (strstr(label, "COMMENT")) { /* opt */

			/* read cnes wl satellite fractional bias */
			if (strstr(buff, "WIDELANE SATELLITE FRACTIONAL BIASES") ||
				strstr(buff, "WIDELANE SATELLITE FRACTIONNAL BIASES")) {
				block = 1;
			}
			else if (block) {
				/* cnes/cls grg clock */
				if (!strncmp(buff, "WL", 2) && (sat = satid2no(buff + 3)) &&
					sscanf(buff + 40, "%lf", &bias) == 1) {
				}
				/* cnes ppp-wizard clock */
				else if ((sat = satid2no(buff + 1)) && sscanf(buff + 6, "%lf", &bias) == 1) {
				}
			}
			continue;
		}
		/* file type */
		switch (*type) {
		case 'O': decode_obsh(fp, buff, *ver, tsys, tobs, nav, sta); break;
			// case 'N': decode_navh (buff,nav); break;
			//case 'G': decode_gnavh(buff,nav); break;
			//case 'H': decode_hnavh(buff,nav); break;
			// case 'J': decode_navh (buff,nav); break; /* extension */
			//case 'L': decode_navh (buff,nav); break; /* extension */
		}
		if (strstr(label, "END OF HEADER")) 
			return 1;

		if (++i >= MAXPOSHEAD && *type == ' ') break; /* no rinex file */
	}
	return 0;
}

/* read rinex obs ------------------------------------------------------------*/
extern int readrnxobs(FILE* fp, gtime_t ts, gtime_t te, double tint, const char* opt,
	int rcv, double ver, int* tsys, char tobs[][MAXOBSTYPE][4], obs_t* obs, sta_t* sta, int* lineCount, int maxLine)
{
	obsd_t* data;
	unsigned char slips[MAXSAT][NFREQ] = { {0} };
	int i, n, flag = 0, stat = 0;

	////trace(4, "readrnxobs: rcv=%d ver=%.2f tsys=%d\n", rcv, ver, tsys);

	if (!obs || rcv > MAXRCV) return 0;

	if (!(data = (obsd_t*)malloc(sizeof(obsd_t) * MAXOBS))) return 0;

	/* read rinex obs data body */
	while ((n = readrnxobsb(fp, opt, ver, tsys, tobs, &flag, data, sta, lineCount, maxLine)) >= 0 && stat >= 0) {

		for (i = 0; i < n; i++) {

			/* utc -> gpst */
			if (*tsys == TSYS_UTC)
				data[i].time = utc2gpst(data[i].time);

			/* save cycle-slip */
			saveslips(slips, data + i);
		}
		/* screen data by time */
		if (n > 0 && !screent(data[0].time, ts, te, tint))
			continue;

		for (i = 0; i < n; i++) {

			/* restore cycle-slip */
			restslips(slips, data + i);

			data[i].rcv = (unsigned char)rcv;

			/* save obs data */
			if ((stat = addobsdata(obs, data + i)) < 0) break;
		}
	}
	////trace(4, "readrnxobs: nobs=%d stat=%d\n", obs->n, stat);

	free(data);

	return stat;
}

static const double ura_eph[] = {         /* ura values (ref [3] 20.3.3.3.1.1) */
	2.4,3.4,4.85,6.85,9.65,13.65,24.0,48.0,96.0,192.0,384.0,768.0,1536.0,
	3072.0,6144.0,0.0
};

static const double bdt0[] = { 2006,1, 1,0,0,0 }; /* beidou time reference */

//--------------------------------------------------------------------------------
/* adjust time considering week handover -------------------------------------*/
static gtime_t adjweek(gtime_t t, gtime_t t0)
{
	double tt = timediff(t, t0);
	if (tt < -302400.0) return timeadd(t, 604800.0);
	if (tt > 302400.0) return timeadd(t, -604800.0);
	return t;
}

//static gtime_t bdt2time(int week, double sec)
//{
//	gtime_t t = epoch2time(bdt0);
//
//	if (sec < -1E9 || 1E9 < sec) sec = 0.0;
//	t.time += (time_t)86400 * 7 * week + (int)sec;
//	t.frac = sec - (int)sec;
//	return t;
//}

static double time2sec(gtime_t time, gtime_t *day)
{
    double ep[6],sec;
    time2epoch(time,ep);
    sec=ep[3]*3600.0+ep[4]*60.0+ep[5];
    ep[3]=ep[4]=ep[5]=0.0;
    *day=epoch2time(ep);
    return sec;
}


/* ura value (m) to ura index ------------------------------------------------*/
static int uraindex(double value)
{
	int i;
	for (i = 0; i < 15; i++) if (ura_eph[i] >= value) break;
	return i;
}
/* galileo sisa value (m) to sisa index --------------------------------------*/
static int sisa_index(double value)
{
	if (value < 0.0 || value>6.0) return 255; /* unknown or NAPA */
	else if (value <= 0.5) return (int)(value / 0.01);
	else if (value <= 1.0) return (int)((value - 0.5) / 0.02) + 50;
	else if (value <= 2.0) return (int)((value - 1.0) / 0.04) + 75;
	return ((int)(value - 2.0) / 0.16) + 100;
}
/* decode ephemeris ----------------------------------------------------------*/
static int decode_eph(double ver, unsigned char sat, gtime_t toc, const double *data,
	eph_t *eph)
{
	eph_t eph0 = { 0 };
	unsigned char sys;

	////trace(4, "decode_eph: ver=%.2f sat=%2d\n", ver, sat);

	sys = satsys(sat, NULL);

	//if (!(sys&(SYS_GPS | SYS_GAL | SYS_QZS | SYS_BDS))) {
	if (sys==SYS_NONE) {
		////trace(3, "ephemeris error: invalid satellite sat=%2d\n", sat);
		return 0;
	}
	*eph = eph0;

	eph->sat = sat;
	eph->toc = toc;

	eph->f0 = data[0];
	eph->f1 = data[1];
	eph->f2 = data[2];

	eph->A = SQR(data[10]); eph->e = data[8]; eph->i0 = data[15]; eph->OMG0 = data[13];
	eph->omg = data[17]; eph->M0 = data[6]; eph->deln = data[5]; eph->OMGd = data[18];
	eph->idot = data[19]; eph->crc = data[16]; eph->crs = data[4]; eph->cuc = data[7];
	eph->cus = data[9]; eph->cic = data[12]; eph->cis = data[14];

	if (sys == SYS_GPS || sys == SYS_QZS) {
		eph->iode = (int)data[3];      /* IODE */
		eph->iodc = (int)data[26];      /* IODC */
		eph->toes = data[11];      /* toe (s) in gps week */
		eph->week = (int)data[21];      /* gps week */
		eph->toe = adjweek(gpst2time(eph->week, data[11]), toc);
		eph->ttr = adjweek(gpst2time(eph->week, data[27]), toc);

		eph->code = (int)data[20];      /* GPS: codes on L2 ch */
		eph->svh = (int)data[24];      /* sv health */
		eph->sva = uraindex(data[23]);  /* ura (m->index) */
		eph->flag = (int)data[22];      /* GPS: L2 P data flag */

		eph->tgd[0] = data[25];      /* TGD */
		if (sys == SYS_GPS) {
			eph->fit = data[28];        /* fit interval (h) */
		}
		else {
			eph->fit = data[28] == 0.0 ? 1.0 : 2.0; /* fit interval (0:1h,1:>2h) */
		}
	}
	else if (sys == SYS_GAL) { /* GAL ver.3 */
		eph->iode = (int)data[3];      /* IODnav */
		eph->toes = data[11];      /* toe (s) in galileo week */
		eph->week = (int)data[21];      /* gal week = gps week */
		eph->toe = adjweek(gpst2time(eph->week, data[11]), toc);
		eph->ttr = adjweek(gpst2time(eph->week, data[27]), toc);

		eph->code = (int)data[20];      /* data sources */
		/* bit 0 set: I/NAV E1-B */
		/* bit 1 set: F/NAV E5a-I */
		/* bit 2 set: F/NAV E5b-I */
		/* bit 8 set: af0-af2 toc are for E5a.E1 */
		/* bit 9 set: af0-af2 toc are for E5b.E1 */
		eph->svh = (int)data[24];      /* sv health */
		/* bit     0: E1B DVS */
		/* bit   1-2: E1B HS */
		/* bit     3: E5a DVS */
		/* bit   4-5: E5a HS */
		/* bit     6: E5b DVS */
		/* bit   7-8: E5b HS */
		eph->sva = sisa_index(data[23]); /* ura (m->index) */
		//eph->sva = uraindex(data[23]); /* ura (m->index) */
		eph->tgd[0] = data[25];      /* BGD E5a/E1 */
		eph->tgd[1] = data[26];      /* BGD E5b/E1 */
	}
	else if (sys == SYS_BDS) { /* BeiDou v.3.02 */
		eph->toc = bdt2gpst(eph->toc);  /* bdt -> gpst */
		eph->iode = (int)data[3];      /* AODE */
		eph->iodc = (int)data[28];      /* AODC */
		eph->toes = data[11];      /* toe (s) in bdt week */
		eph->week = (int)data[21];      /* bdt week */
		eph->toe = bdt2gpst(bdt2time(eph->week, data[11])); /* bdt -> gpst */
		eph->ttr = bdt2gpst(bdt2time(eph->week, data[27])); /* bdt -> gpst */
		eph->toe = adjweek(eph->toe, toc);
		eph->ttr = adjweek(eph->ttr, toc);

		eph->svh = (int)data[24];      /* satH1 */
		eph->sva = uraindex(data[23]);  /* ura (m->index) */

		eph->tgd[0] = data[25];      /* TGD1 B1/B3 */
		eph->tgd[1] = data[26];      /* TGD2 B2/B3 */
	}
	//else if (sys == SYS_IRN) { /* IRNSS v.3.03 */
	//	eph->iode = (int)data[3];      /* IODEC */
	//	eph->toes = data[11];      /* toe (s) in irnss week */
	//	eph->week = (int)data[21];      /* irnss week */
	//	eph->toe = adjweek(gpst2time(eph->week, data[11]), toc);
	//	eph->ttr = adjweek(gpst2time(eph->week, data[27]), toc);
	//	eph->svh = (int)data[24];      /* sv health */
	//	eph->sva = uraindex(data[23]);  /* ura (m->index) */
	//	eph->tgd[0] = data[25];      /* TGD */
	//}
	if (eph->iode < 0 || 1023 < eph->iode) {
		////trace(2, "rinex nav invalid: sat=%2d iode=%d\n", sat, eph->iode);
	}
	if (eph->iodc < 0 || 1023 < eph->iodc) {
		////trace(2, "rinex nav invalid: sat=%2d iodc=%d\n", sat, eph->iodc);
	}
	return 1;
}

/* adjust time considering week handover -------------------------------------*/
static gtime_t adjday(gtime_t t, gtime_t t0)
{
	double tt = timediff(t, t0);
	if (tt < -43200.0) return timeadd(t, 86400.0);
	if (tt > 43200.0) return timeadd(t, -86400.0);
	return t;
}

/* decode glonass ephemeris --------------------------------------------------*/
static int decode_geph(double ver, unsigned char sat, gtime_t toc, double *data,
	geph_t *geph)
{
	geph_t geph0 = { 0 };
	gtime_t tof;
	double tow, tod;
	int week, dow;

	////trace(4, "decode_geph: ver=%.2f sat=%2d\n", ver, sat);

	if (satsys(sat, NULL) != SYS_GLO) {
		////trace(3, "glonass ephemeris error: invalid satellite sat=%2d\n", sat);
		return 0;
	}
	*geph = geph0;

	geph->sat = sat;

	/* toc rounded by 15 min in utc */
	tow = time2gpst(toc, &week);
	toc = gpst2time(week, floor((tow + 450.0) / 900.0) * 900);
	dow = (int)floor(tow / 86400.0);

	/* time of frame in utc */
	tod = ver <= 2.99 ? data[2] : fmod(data[2], 86400.0); /* tod (v.2), tow (v.3) in utc */
	tof = gpst2time(week, tod + dow * 86400.0);
	tof = adjday(tof, toc);

	geph->toe = utc2gpst(toc);   /* toc (gpst) */
	geph->tof = utc2gpst(tof);   /* tof (gpst) */

	/* iode = tb (7bit), tb =index of UTC+3H within current day */
	geph->iode = (int)(fmod(tow + 10800.0, 86400.0) / 900.0 + 0.5);

	geph->taun = -data[0];       /* -taun */
	geph->gamn = data[1];       /* +gamman */

	geph->pos[0] = data[3] * 1E3; geph->pos[1] = data[7] * 1E3; geph->pos[2] = data[11] * 1E3;
	geph->vel[0] = data[4] * 1E3; geph->vel[1] = data[8] * 1E3; geph->vel[2] = data[12] * 1E3;
	geph->acc[0] = data[5] * 1E3; geph->acc[1] = data[9] * 1E3; geph->acc[2] = data[13] * 1E3;

	geph->svh = (int)data[6];
	geph->frq = (int)data[10];
	geph->age = (int)data[14];

	/* some receiver output >128 for minus frequency number */
	if (geph->frq > 128) geph->frq -= 256;

	if (geph->frq < MINFREQ_GLO || MAXFREQ_GLO < geph->frq) {
		////trace(2, "rinex gnav invalid freq: sat=%2d fn=%d\n", sat, geph->frq);
	}
	return 1;
}



/* read rinex navigation data body -------------------------------------------*/
static int readrnxnavb(FILE* fp, const char* opt, double ver, unsigned char sys,
	int* type, eph_t* eph, geph_t* geph, int* lineCount, int maxLine)
{
	gtime_t toc;
	double data[64];
	unsigned char prn, sat=0;
	int i = 0, j, sp = 3, mask;
	char buff[MAXRNXLEN], id[8] = "", * p;
	int percent10 = maxLine / 10;
	int percent, percent1 = maxLine / 100;
	int cout = percent10 > 0 ? 1 : 0;
	////trace(4, "readrnxnavb: ver=%.2f sys=%d\n", ver, sys);

	/* set system mask */
	mask = set_sysmask(opt);

	while (fgets(buff, MAXRNXLEN, fp)) {
		*lineCount = *lineCount + 1;
		if (cout) {
			if (*lineCount % percent10 == percent10 - 1)
			{
				percent = ROUND((*lineCount / (double)maxLine) * 100);
				if (percent % percent10 != 0 && percent % 10 != 0)
					percent++;
				printf("%d%% ", percent);
			}
		}
		if (i == 0) {

			/* decode satellite field */
			//if (ver >= 3.0 || sys == SYS_GAL || sys == SYS_QZS) { /* ver.3 or GAL/QZS */
			if (ver >= 3.0) { /* ver.3 or GAL/QZS */
				strncpy(id, buff, 3);
				sat = satid2no(id);
				sp = 4;
				if (ver >= 3.0)
					sys = satsys(sat, NULL);
			}
			else {
				prn = (unsigned char)str2num(buff, 0, 2);

				if (sys == SYS_SBS) {
					sat = satno(SYS_SBS, prn + 100);
				}
				else if (sys == SYS_GLO) {
					sat = satno(SYS_GLO, prn);
				}
				else if (sys == SYS_GAL) {
					sat = satno(SYS_GAL, prn);
				}
				else if (sys == SYS_BDS) {
					sat = satno(SYS_BDS, prn);
				}
				else if (93 <= prn && prn <= 97) { /* extension */
					sat = satno(SYS_QZS, prn + 100);
				}
				else sat = satno(SYS_GPS, prn);
			}
			/* decode toc field */
			if (str2time(buff + sp, 0, 19, &toc)) {
				////trace(2, "rinex nav toc error: %23.23s\n", buff);
				return 0;
			}
			/* decode data fields */
			for (j = 0, p = buff + sp + 19; j < 3; j++, p += 19) {
				data[i++] = str2num(p, 0, 19);
			}
		}
		else {
			/* decode data fields */
			for (j = 0, p = buff + sp; j < 4; j++, p += 19) {
				data[i++] = str2num(p, 0, 19);
			}
			/* decode ephemeris */
			if (sys == SYS_GLO && i >= 15) {
				if (!(mask & sys)) return 0;
				*type = 1;
				return decode_geph(ver, sat, toc, data, geph);
			}
			//else if (sys == SYS_SBS && i >= 15) {
			//	if (!(mask&sys)) return 0;
			//	*type = 2;
			//	return decode_seph(ver, sat, toc, data, seph);
			//}
			else if (i >= 31) {
				//if (!(mask & sys)) return 0;
				if (sys==SYS_NONE) return 0;
				*type = 0;
				return decode_eph(ver, sat, toc, data, eph);
			}
		}
	}
	return -1;
}

extern int findGephIndex2(geph_t* geph, unsigned char sat) {
	int i;

	for (i = 0; i < MAXGEPH; i++) {
		if (geph[i].sat == 0) {
			return i;
		}
	}
	return -1;
}
static int add_geph(nav_t *nav, const geph_t *geph)
{
	int index;
	index = findGephIndex2(nav->geph, geph->sat);
	if (index == -1) {
		printf("add_geph error\n");
		return 0;
	}
	nav->geph[index] = *geph;
	return 1;
}


static int findEphIndex2(eph_t* eph, unsigned char sat) {
	int i;

	for (i = 0; i < MAXEPH; i++) {
		if (eph[i].sat == 0) {
			return i;
		}
	}
	return -1;
}
/* add ephemeris to navigation data ------------------------------------------*/
static int add_eph(nav_t *nav, const eph_t *eph)
{
	int index;
	index = findEphIndex2(nav->eph, eph->sat);
	if (index == -1) {
		printf("add_eph error\n");
		return 0;
	}
	nav->eph[index] = *eph;
	return 1;
}

/* read rinex nav/gnav/geo nav -----------------------------------------------*/
extern int readrnxnav(FILE* fp, const char* opt, double ver, unsigned char sys, nav_t* nav, int* lineCount, int maxLine)
{
	eph_t eph;
	geph_t geph;
	int stat, type;

	////trace(3, "readrnxnav: ver=%.2f sys=%d\n", ver, sys);

	if (!nav) return 0;

	/* read rinex navigation data body */
	while ((stat = readrnxnavb(fp, opt, ver, sys, &type, &eph, &geph, lineCount, maxLine)) >= 0) {

		/* add ephemeris to navigation data */
		if (stat) {
			switch (type) {
			case 1: stat = add_geph(nav, &geph); break;
			//case 2: stat = add_seph(nav, &seph); break;
			default: stat = add_eph(nav, &eph); break;
			}
			if (!stat) return 0;
		}
	}
	return nav->n > 0 || nav->ng > 0;
}


/* satellite carrier wave length -----------------------------------------------
* get satellite carrier wave lengths
* args   : int    sat       I   satellite number
*          int    frq       I   frequency index (0:L1,1:L2,2:L5/3,...)
*          nav_t  *nav      I   navigation messages
* return : carrier wave length (m) (0.0: error)
*-----------------------------------------------------------------------------*/

extern double satwavelen(unsigned char sat, int frq, const nav_t *nav)
{
	const double freq_glo[] = { FREQ1_GLO,FREQ2_GLO };
	const double dfrq_glo[] = { DFRQ1_GLO,DFRQ2_GLO };
	unsigned char i, sys = satsys(sat, NULL);

	if (sys == SYS_GLO) {
		if (0 <= frq && frq <= 1) {
			for (i = 0; i < nav->ng; i++) {
				if (nav->geph[i].sat != sat) continue;
				return CLIGHT / (freq_glo[frq] + dfrq_glo[frq] * nav->geph[i].frq);
			}
		}
		else if (frq == 2) { /* L3 */
			return CLIGHT / FREQ3_GLO;
		}
	}
	else if (sys == SYS_BDS) {
		if (frq == 0) return CLIGHT / FREQ1_CMP; /* B1 */
		else if (frq == 1) return CLIGHT / FREQ2_CMP; /* B2 */
		else if (frq == 2) return CLIGHT / FREQ3_CMP; /* B3 */
	}
	else {
		if (frq == 0) return CLIGHT / FREQ1; /* L1/E1 */
		else if (frq == 1) return CLIGHT / FREQ2; /* L2 */
		else if (frq == 2) return CLIGHT / FREQ5; /* L5/E5a */
		else if (frq == 3) return CLIGHT / FREQ6; /* L6/LEX */
		else if (frq == 4) return CLIGHT / FREQ7; /* E5b */
		else if (frq == 5) return CLIGHT / FREQ8; /* E5a+b */
		else if (frq == 6) return CLIGHT / FREQ9; /* S */
	}
	return 0.0;
}


extern int outprcopts(unsigned char *buff, const prcopt_t *opt)
{
	const unsigned char sys[] = { SYS_GPS,SYS_GLO,SYS_GAL,SYS_QZS,SYS_BDS,SYS_SBS,0 };
	const char *s1[] = { "single","dgps","kinematic","static","moving-base","fixed",
		"ppp-kinematic","ppp-static","ppp-fixed","" };
	const char *s2[] = { "L1","L1+L2","L1+L2+L5","L1+L2+L5+L6","L1+L2+L5+L6+L7",
		"L1+L2+L5+L6+L7+L8","" };
	const char *s3[] = { "forward","backward","combined" };
	const char *s4[] = { "off","broadcast","sbas","iono-free","estimation",
		"ionex tec","qzs","lex","vtec_sf","vtec_ef","gtec","" };
	const char *s5[] = { "off","saastamoinen","sbas","est ztd","est ztd+grad","" };
	const char *s6[] = { "broadcast","precise","broadcast+sbas","broadcast+ssr apc",
		"broadcast+ssr com","qzss lex","" };
	const char *s7[] = { "gps","glonass","galileo","qzss","beidou","irnss","sbas","" };
	const char *s8[] = { "off","continuous","instantaneous","fix and hold","" };
	const char *s9[] = { "off","on","auto calib","external calib","" };
	int i;
	char *p = (char *)buff;

	////trace(3, "outprcopts:\n");

	p += sprintf(p, "%s pos mode  : %s\n", COMMENTH, s1[opt->mode]);

	/*if (PMODE_DGPS <= opt->mode&&opt->mode <= PMODE_FIXED) {
		p += sprintf(p, "%s freqs     : %s\n", COMMENTH, s2[opt->nf - 1]);
	}
	if (opt->mode > PMODE_SINGLE) {
		p += sprintf(p, "%s solution  : %s\n", COMMENTH, s3[opt->soltype]);
	}
	p += sprintf(p, "%s elev mask : %.1f deg\n", COMMENTH, opt->elmin*R2D);
	if (opt->mode > PMODE_SINGLE) {
		p += sprintf(p, "%s dynamics  : %s\n", COMMENTH, opt->dynamics ? "on" : "off");
		p += sprintf(p, "%s tidecorr  : %s\n", COMMENTH, opt->tidecorr ? "on" : "off");
	}
	if (opt->mode <= PMODE_FIXED) {
		p += sprintf(p, "%s ionos opt : %s\n", COMMENTH, s4[opt->ionoopt]);
	}
	p += sprintf(p, "%s tropo opt : %s\n", COMMENTH, s5[opt->tropopt]);
	p += sprintf(p, "%s ephemeris : %s\n", COMMENTH, s6[opt->sateph]);
	if (opt->navsys != SYS_GPS) {
		p += sprintf(p, "%s navi sys  :", COMMENTH);
		for (i = 0; sys[i]; i++) {
			if (opt->navsys&sys[i]) p += sprintf(p, " %s", s7[i]);
		}
		p += sprintf(p, "\n");
	}
	if (PMODE_KINEMA <= opt->mode&&opt->mode <= PMODE_FIXED) {
		p += sprintf(p, "%s amb res   : %s\n", COMMENTH, s8[opt->modear]);
		if (opt->navsys&SYS_GLO) {
			p += sprintf(p, "%s amb glo   : %s\n", COMMENTH, s9[opt->glomodear]);
		}
		if (opt->thresar[0] > 0.0) {
			p += sprintf(p, "%s val thres : %.1f\n", COMMENTH, opt->thresar[0]);
		}
	}
	if (opt->mode == PMODE_MOVEB && opt->baseline[0] > 0.0) {
		p += sprintf(p, "%s baseline  : %.4f %.4f m\n", COMMENTH,
			opt->baseline[0], opt->baseline[1]);
	}
	for (i = 0; i < 2; i++) {
		if (opt->mode == PMODE_SINGLE || (i >= 1 && opt->mode > PMODE_FIXED)) continue;
		p += sprintf(p, "%s antenna%d  : %-21s (%7.4f %7.4f %7.4f)\n", COMMENTH,
			i + 1, opt->anttype[i], opt->antdel[i][0], opt->antdel[i][1],
			opt->antdel[i][2]);
	}*/
	return p - (char *)buff;
}


extern void outprcopt(FILE *fp, const prcopt_t *opt)
{
	unsigned char buff[8191 + 1];
	int n;

	////trace(3, "outprcopt:\n");

	if ((n = outprcopts(buff, opt)) > 0) {
		fwrite(buff, n, 1, fp);
	}
}

/* output reference position -------------------------------------------------*/
static void outrpos(FILE *fp, const double *r, const solopt_t *opt)
{
	double pos[3], dms1[3], dms2[3];
	const char *sep = opt->sep;

	////trace(3, "outrpos :\n");

	if (opt->posf == SOLF_LLH || opt->posf == SOLF_ENU) {
		ecef2pos(r, pos);
		if (opt->degf) {
			deg2dms(pos[0] * R2D, dms1, 5);
			deg2dms(pos[1] * R2D, dms2, 5);
			fprintf(fp, "%3.0f%s%02.0f%s%08.5f%s%4.0f%s%02.0f%s%08.5f%s%10.4f",
				dms1[0], sep, dms1[1], sep, dms1[2], sep, dms2[0], sep, dms2[1],
				sep, dms2[2], sep, pos[2]);
		}
		else {
			fprintf(fp, "%13.9f%s%14.9f%s%10.4f", pos[0] * R2D, sep, pos[1] * R2D,
				sep, pos[2]);
		}
	}
	else if (opt->posf == SOLF_XYZ) {
		fprintf(fp, "%14.4f%s%14.4f%s%14.4f", r[0], sep, r[1], sep, r[2]);
	}
}

/* solution option to field separator ----------------------------------------*/
static const char *opt2sep(const solopt_t *opt)
{
	if (!*opt->sep) return " ";
	else if (!strcmp(opt->sep, "\\t")) return "\t";
	return opt->sep;
}

static int outsolheads(unsigned char *buff, const solopt_t *opt)
{
	const char *s1[] = { "WGS84","Tokyo" }, *s2[] = { "ellipsoidal","geodetic" };
	const char *s3[] = { "GPST","UTC ","JST ","BJT" }, *sep = opt2sep(opt);
	char *p = (char *)buff;
	int timeu = opt->timeu < 0 ? 0 : (opt->timeu > 20 ? 20 : opt->timeu);

	////trace(3, "outsolheads:\n");

	if (opt->posf == SOLF_NMEA || opt->posf == SOLF_STAT || opt->posf == SOLF_GSIF) {
		return 0;
	}
	if (opt->outhead) {
		p += sprintf(p, "%s (", COMMENTH);
		if (opt->posf == SOLF_XYZ) p += sprintf(p, "x/y/z-ecef=WGS84");
		else if (opt->posf == SOLF_ENU) p += sprintf(p, "e/n/u-baseline=WGS84");
		else p += sprintf(p, "lat/lon/height=%s/%s", s1[opt->datum], s2[opt->height]);
		p += sprintf(p, ",Q=1:fix,2:float,3:sbas,4:dgps,5:single,6:ppp,ns=# of satellites)\n");
	}
	p += sprintf(p, "%s  %-*s%s", COMMENTH, (opt->timef ? 16 : 8) + timeu + 1, s3[opt->times], sep);

	if (opt->posf == SOLF_LLH) { /* lat/lon/hgt */
		if (opt->degf) {
			p += sprintf(p, "%16s%s%16s%s%10s%s%3s%s%3s%s%8s%s%8s%s%8s%s%8s%s%8s%s%8s%s%6s%s%6s",
				"latitude(d'\")", sep, "longitude(d'\")", sep, "height(m)", sep,
				"Q", sep, "ns", sep, "sdn(m)", sep, "sde(m)", sep, "sdu(m)", sep,
				"sdne(m)", sep, "sdeu(m)", sep, "sdue(m)", sep, "age(s)", sep, "ratio");
		}
		else {
			p += sprintf(p, "%14s%s%14s%s%10s%s%3s%s%3s%s%8s%s%8s%s%8s%s%8s%s%8s%s%8s%s%6s%s%6s",
				"latitude(deg)", sep, "longitude(deg)", sep, "height(m)", sep,
				"Q", sep, "ns", sep, "sdn(m)", sep, "sde(m)", sep, "sdu(m)", sep,
				"sdne(m)", sep, "sdeu(m)", sep, "sdun(m)", sep, "age(s)", sep, "ratio");
		}
		if (opt->outvel) {
			p += sprintf(p, "%s%10s%s%10s%s%10s%s%9s%s%8s%s%8s%s%8s%s%8s%s%8s",
				sep, "vn(m/s)", sep, "ve(m/s)", sep, "vu(m/s)", sep, "sdvn", sep,
				"sdve", sep, "sdvu", sep, "sdvne", sep, "sdveu", sep, "sdvun");
		}
	}
	else if (opt->posf == SOLF_XYZ) { /* x/y/z-ecef */
		p += sprintf(p, "%14s%s%14s%s%14s%s%3s%s%3s%s%8s%s%8s%s%8s%s%8s%s%8s%s%8s%s%6s%s%6s",
			"x-ecef(m)", sep, "y-ecef(m)", sep, "z-ecef(m)", sep, "Q", sep, "ns", sep,
			"sdx(m)", sep, "sdy(m)", sep, "sdz(m)", sep, "sdxy(m)", sep,
			"sdyz(m)", sep, "sdzx(m)", sep, "age(s)", sep, "ratio");

		if (opt->outvel) {
			p += sprintf(p, "%s%10s%s%10s%s%10s%s%9s%s%8s%s%8s%s%8s%s%8s%s%8s",
				sep, "vx(m/s)", sep, "vy(m/s)", sep, "vz(m/s)", sep, "sdvx", sep,
				"sdvy", sep, "sdvz", sep, "sdvxy", sep, "sdvyz", sep, "sdvzx");
		}
	}
	else if (opt->posf == SOLF_ENU) { /* e/n/u-baseline */
		p += sprintf(p, "%14s%s%14s%s%14s%s%3s%s%3s%s%8s%s%8s%s%8s%s%8s%s%8s%s%8s%s%6s%s%6s",
			"e-baseline(m)", sep, "n-baseline(m)", sep, "u-baseline(m)", sep,
			"Q", sep, "ns", sep, "sde(m)", sep, "sdn(m)", sep, "sdu(m)", sep,
			"sden(m)", sep, "sdnu(m)", sep, "sdue(m)", sep, "age(s)", sep, "ratio");
	}
	p += sprintf(p, "\n");
	return p - (char *)buff;
}

static void outsolhead(FILE *fp, const solopt_t *opt)
{
	unsigned char buff[8191 + 1];
	int n;

	////trace(3, "outsolhead:\n");

	if ((n = outsolheads(buff, opt)) > 0) {
		fwrite(buff, n, 1, fp);
	}
}

/* output header -------------------------------------------------------------*/
extern void outheader(FILE *fp, char file[][MAXSTRPATH], int n, const prcopt_t *popt,
	const solopt_t *sopt)
{
	if (sopt->posf == SOLF_NMEA || sopt->posf == SOLF_STAT) {
		return;
	}
	if (sopt->outopt) {
		outprcopt(fp, popt);
	}
	if (PMODE_DGPS <= popt->mode&&popt->mode <= PMODE_FIXED && popt->mode != PMODE_MOVEB) {
		fprintf(fp, "%s ref pos   :", COMMENTH);
		outrpos(fp, popt->rb, sopt);
		fprintf(fp, "\n");
	}
	if (sopt->outhead || sopt->outopt) fprintf(fp, "%s\n", COMMENTH);

	outsolhead(fp, sopt);
}
extern int expath(const char *path, char *paths[], int nmax)
{
    int i,j,n=0;
    char tmp[1024];
#ifdef WIN32
    WIN32_FIND_DATA file;
    HANDLE h;
    char dir[1024]="",*p;
    
    if ((p=strrchr(path,'\\'))) {
        strncpy(dir,path,p-path+1); dir[p-path+1]='\0';
    }
    if ((h=FindFirstFile((LPCTSTR)path,&file))==INVALID_HANDLE_VALUE) {
        strcpy(paths[0],path);
        return 1;
    }
    sprintf(paths[n++],"%s%s",dir,file.cFileName);
    while (FindNextFile(h,&file)&&n<nmax) {
        if (file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        sprintf(paths[n++],"%s%s",dir,file.cFileName);
    }
    FindClose(h);
#else
    struct dirent *d;
    DIR *dp;
    const char *file=path;
    char dir[1024]="",s1[1024],s2[1024],*p,*q,*r;
    
    ////trace(3,"expath  : path=%s nmax=%d\n",path,nmax);
    
    if ((p=strrchr(path,'/'))||(p=strrchr(path,'\\'))) {
        file=p+1; strncpy(dir,path,p-path+1); dir[p-path+1]='\0';
    }
    if (!(dp=opendir(*dir?dir:"."))) return 0;
    while ((d=readdir(dp))) {
        if (*(d->d_name)=='.') continue;
        sprintf(s1,"^%s$",d->d_name);
        sprintf(s2,"^%s$",file);
        for (p=s1;*p;p++) *p=(char)tolower((int)*p);
        for (p=s2;*p;p++) *p=(char)tolower((int)*p);
        
        for (p=s1,q=strtok_r(s2,"*",&r);q;q=strtok_r(NULL,"*",&r)) {
            if ((p=strstr(p,q))) p+=strlen(q); else break;
        }
        if (p&&n<nmax) sprintf(paths[n++],"%s%s",dir,d->d_name);
    }
    closedir(dp);
#endif
    /* sort paths in alphabetical order */
    for (i=0;i<n-1;i++) {
        for (j=i+1;j<n;j++) {
            if (strcmp(paths[i],paths[j])>0) {
                strcpy(tmp,paths[i]);
                strcpy(paths[i],paths[j]);
                strcpy(paths[j],tmp);
            }
        }
    }
    
    return n;
}
extern void createdir(const char *path)
{
	char buff[1024], *p;

	//trace(2, "createdir: path=%s\n", path);

	strcpy(buff, path);
	//if (!(p = strrchr(buff, FILEPATHSEP))) return;
	//*p = '\0';

#ifdef WIN32
    mkdir(buff);
#else
	mkdir(buff, 0777);
#endif
}
/* write header to output file -----------------------------------------------*/
extern int outhead(const char *outfile, char infile[][MAXSTRPATH], int n,
	const prcopt_t *popt, const solopt_t *sopt)
{
	FILE *fp = stdout;

	////trace(3, "outhead: outfile=%s n=%d\n", outfile, n);

	if (*outfile) {
		//createdir(outfile);
		if (!(fp = fopen(outfile, "w"))) {
			showmsg("error : open output file %s", outfile);
			return 0;
		}
	}
	/* output header */
	outheader(fp, infile, n, popt, sopt);

	if (*outfile) fclose(fp);

	return 1;
}

static void init_sta(sta_t *sta)
{
    int i;
    *sta->name   ='\0';
    *sta->marker ='\0';
    *sta->antdes ='\0';
    *sta->antsno ='\0';
    *sta->rectype='\0';
    *sta->recver ='\0';
    *sta->recsno ='\0';
    sta->antsetup=sta->itrf=sta->deltype=0;
    for (i=0;i<3;i++) sta->pos[i]=0.0;
    for (i=0;i<3;i++) sta->del[i]=0.0;
    sta->hgt=0.0;
}

static int uncompress(const char *file, char *uncfile)
{
    int stat=0;
    char *p,cmd[2048]="",tmpfile[1024]="",buff[1024],*fname,*dir="";
    
   
    strcpy(tmpfile,file);
    if (!(p=strrchr(tmpfile,'.'))) return 0;
    
    /* uncompress by gzip */
    if (!strcmp(p,".z"  )||!strcmp(p,".Z"  )||
        !strcmp(p,".gz" )||!strcmp(p,".GZ" )||
        !strcmp(p,".zip")||!strcmp(p,".ZIP")) {
        
        strcpy(uncfile,tmpfile); uncfile[p-tmpfile]='\0';
        sprintf(cmd,"gzip -f -d -c \"%s\" > \"%s\"",tmpfile,uncfile);
        
        //if (execcmd(cmd)) {
        //    remove(uncfile);
        //    return -1;
        //}
        strcpy(tmpfile,uncfile);
        stat=1;
    }
    /* extract tar file */
    if ((p=strrchr(tmpfile,'.'))&&!strcmp(p,".tar")) {
        
        strcpy(uncfile,tmpfile); uncfile[p-tmpfile]='\0';
        strcpy(buff,tmpfile);
        fname=buff;
#ifdef WIN32
        if ((p=strrchr(buff,'\\'))) {
            *p='\0'; dir=fname; fname=p+1;
        }
        sprintf(cmd,"set PATH=%%CD%%;%%PATH%% & cd /D \"%s\" & tar -xf \"%s\"",
                dir,fname);
#else
        if ((p=strrchr(buff,'/'))) {
            *p='\0'; dir=fname; fname=p+1;
        }
        sprintf(cmd,"tar -C \"%s\" -xf \"%s\"",dir,tmpfile);
#endif
        //if (execcmd(cmd)) {
        //    if (stat) remove(tmpfile);
        //    return -1;
        //}
        if (stat) remove(tmpfile);
        stat=1;
    }
    /* extract hatanaka-compressed file by cnx2rnx */
    else if ((p=strrchr(tmpfile,'.'))&&strlen(p)>3&&(*(p+3)=='d'||*(p+3)=='D')) {
        
        strcpy(uncfile,tmpfile);
        uncfile[p-tmpfile+3]=*(p+3)=='D'?'O':'o';
        sprintf(cmd,"crx2rnx < \"%s\" > \"%s\"",tmpfile,uncfile);
        
        //if (execcmd(cmd)) {
        //    remove(uncfile);
        //    if (stat) remove(tmpfile);
        //    return -1;
        //}
        if (stat) remove(tmpfile);
        stat=1;
    }
    ////trace(3,"uncompress: stat=%d\n",stat);
    return stat;
}

/* read rinex clock ----------------------------------------------------------*/
static int readrnxclk(FILE *fp, const char *opt, int index, nav_t *nav)
{
 //   pclk_t *nav_pclk;
 //   gtime_t time;
 //   double data[2];
 //   int i,j,mask;
 //   unsigned char sat;
 //   char buff[MAXRNXLEN],satid[8]="";
 //  
 //   
 //   if (!nav) return 0;
 //   
 //   /* set system mask */
 //   mask=set_sysmask(opt);
 //   
 //   while (fgets(buff,sizeof(buff),fp)) 
	//{
	//	//printf("%s", buff);
 //       
 //       if (str2time(buff,8,26,&time)) {
 //          // //trace(2,"rinex clk invalid epoch: %34.34s\n",buff);
 //           continue;
 //       }
 //       strncpy(satid,buff+3,4);
 //       
 //       /* only read AS (satellite clock) record */
 //       if (strncmp(buff,"AS",2)||!(sat=satid2no(satid))) 
	//		continue;
 //       
 //       if (!(satsys(sat,NULL)&mask)) continue;
 //       
 //       for (i=0,j=40;i<2;i++,j+=20) data[i]=str2num(buff,j,19);
 //       
 //       if (nav->nc>=nav->ncmax) {
 //           nav->ncmax+=1024;
 //           if (!(nav_pclk=(pclk_t *)realloc(nav->pclk,sizeof(pclk_t)*(nav->ncmax)))) {
 //               free(nav->pclk); nav->pclk=NULL; nav->nc=nav->ncmax=0;
 //               return -1;
 //           }
 //           nav->pclk=nav_pclk;
 //       }
 //       if (nav->nc<=0||fabs(timediff(time,nav->pclk[nav->nc-1].time))>1E-9) {
 //           nav->nc++;
 //           nav->pclk[nav->nc-1].time =time;
 //           nav->pclk[nav->nc-1].index=index;
 //           for (i=0;i<MAXSAT;i++) {
 //               nav->pclk[nav->nc-1].clk[i][0]=0.0;
 //               nav->pclk[nav->nc-1].std[i][0]=0.0f;
 //           }
 //       }
 //       nav->pclk[nav->nc-1].clk[sat-1][0]=data[0];
 //       nav->pclk[nav->nc-1].std[sat-1][0]=(float)data[1];
 //   }
 //   return nav->nc>0;
}
/* read rinex file -----------------------------------------------------------*/
static int readrnxfp(FILE *fp, gtime_t ts, gtime_t te, double tint,
                     const char *opt, int flag, int index, char *type,
                     obs_t *obs, nav_t *nav, sta_t *sta)
{
    double ver;
    int tsys;
	unsigned char sys;
	int num = 0;
    char tobs[NUMSYS][MAXOBSTYPE][4]={{""}};
    
    /* read rinex header */
    if (!readrnxh(fp,&ver,type,&sys,&tsys,tobs,nav,sta,&num,0)) return 0;
    
    /* flag=0:except for clock,1:clock */
    if ((!flag&&*type=='C')||(flag&&*type!='C')) return 0;
    
    /* read rinex body */
    switch (*type) {
        //case 'O': return readrnxobs(fp,ts,te,tint,opt,index,ver,tsys,tobs,obs);
        //case 'N': return readrnxnav(fp,opt,ver,sys    ,nav);
        //case 'G': return readrnxnav(fp,opt,ver,SYS_GLO,nav);
        //case 'H': return readrnxnav(fp,opt,ver,SYS_SBS,nav);
        //case 'J': return readrnxnav(fp,opt,ver,SYS_QZS,nav); /* extension */
       // case 'L': return readrnxnav(fp,opt,ver,SYS_GAL,nav); /* extension */
        case 'C': return readrnxclk(fp,opt,index,nav);
    }
   
    return 0;
}
static int readrnxfile(const char *file, gtime_t ts, gtime_t te, double tint,
                       const char *opt, int flag, int index, char *type,
                       obs_t *obs, nav_t *nav, sta_t *sta)
{
    FILE *fp;
    int cstat,stat;
    char tmpfile[1024];
    
    if (sta) init_sta(sta);
    
    /* uncompress file */
    if ((cstat=uncompress(file,tmpfile))<0) {
        ////trace(2,"rinex file uncompact error: %s\n",file);
        return 0;
    }
    if (!(fp=fopen(cstat?tmpfile:file,"r"))) {
       // //trace(2,"rinex file open error: %s\n",cstat?tmpfile:file);
        return 0;
    }
    /* read rinex file */
    stat=readrnxfp(fp,ts,te,tint,opt,flag,index,type,obs,nav,sta);
    
    fclose(fp);
    
    /* delete temporary file */
    if (cstat) remove(tmpfile);
    
    return stat;
}


#define WIN_DLL
#ifdef WIN_DLL
extern int showmsg(char *format, ...) { return 0; }
extern void settspan(gtime_t ts, gtime_t te) {}
extern void settime(gtime_t time) {}
#endif
