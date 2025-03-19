#include <math.h>
#include "rtk.h"

#define STD_BRDCCLK 30.0          /* error of broadcast clock (m) */
#define RTOL_KEPLER 1E-13         /* relative tolerance for Kepler equation */

#define RE_GLO   6378136.0        /* radius of earth (m)            ref [2] */
#define MU_GPS   3.9860050E14     /* gravitational constant         ref [1] */
#define MU_GLO   3.9860044E14     /* gravitational constant         ref [2] */
#define MU_GAL   3.986004418E14   /* earth gravitational constant   ref [7] */
#define MU_CMP   3.986004418E14   /* earth gravitational constant   ref [9] */
#define J2_GLO   1.0826257E-3     /* 2nd zonal harmonic of geopot   ref [2] */

#define OMGE_GLO 7.292115E-5      /* earth angular velocity (rad/s) ref [2] */
#define OMGE_GAL 7.2921151467E-5  /* earth angular velocity (rad/s) ref [7] */
#define OMGE_CMP 7.292115E-5      /* earth angular velocity (rad/s) ref [9] */


#define ERREPH_GLO 5.0            /* error of glonass ephemeris (m) */
#define TSTEP    60.0             /* integration step glonass ephemeris (s) */
#define RTOL_KEPLER 1E-13         /* relative tolerance for Kepler equation */

#define SIN_5 -0.0871557427476582 /* sin(-5.0 deg) */
#define COS_5  0.9961946980917456 /* cos(-5.0 deg) */
#define MAX_ITER_KEPLER 30        /* max number of iteration of Kelpler */
#define MAXECORSSR 10.0           /* max orbit correction of ssr (m) */
#define MAXCCORSSR (1E-6*CLIGHT)  /* max clock correction of ssr (m) */
#define MAXAGESSR_HRCLK 10.0      /* max age of ssr high-rate clock (s) */
#define MAXAGESSR 1800.0            /* max age of ssr orbit and clock (s) */
#define DEFURASSR 0.15            /* default accurary of ssr corr (m) */
//satellite clock does not include relativity correction and tdg
static double var_uraeph(unsigned char sys, int ura)
{
	const double ura_value[] = {
		2.0,2.8,4.0,5.7,8,11.3,16.0,32.0,64.0,128.0,256.0,512.0,1024.0,
		2048.0,4096.0,8192.0
	};
	if (sys == SYS_GAL) { /* galileo sisa (ref [7] 5.1.11) */
		if (ura <= 49) return SQR(ura * 0.01);
		if (ura <= 74) return SQR(0.5 + (ura - 50) * 0.02);
		if (ura <= 99) return SQR(1.0 + (ura - 75) * 0.04);
		if (ura <= 125) return SQR(2.0 + (ura - 100) * 0.16);
		return SQR(500.0);
	}
	else { /* gps ura (ref [1] 20.3.3.3.1.1) */
		return ura < 0 || 14 < ura ? SQR(6144.0) : SQR(ura_value[ura]);
	}
}
/* -- double eph2clk(gtime_t time,const eph_t *eph) --------------------------------------
*
* Description	: broadcast ephemeris to sat clock bias
* Parameters	: time	I	time by sat clock
*							eph		I	broadcast ephemeris
* Return		: sat clock bias
*/
double eph2clk(gtime_t time, const eph_t* eph)
{
	double t;
	int i;
	t = timediff(time, eph->toc);
	for (i = 0; i < 2; i++)
	{
		t -= eph->f0 + eph->f1 * t + eph->f2 * t * t;
	}
	return eph->f0 + eph->f1 * t + eph->f2 * t * t;
}

static eph_t* seleph(gtime_t time, int sat, int iode, const nav_t* nav)
{
	double t, tmin, tmax;
	int i, j = -1;
	tmax = MAXDTOE + 1.0;
	tmin = tmax + 1.0;

	for (i = 0; i < nav->n; i++)
	{
		if (nav->eph[i].sat != sat) continue;
		if (iode >= 0 && nav->eph[i].iode != iode)continue;
		if ((t = fabs(timediff(nav->eph[i].toe, time))) > tmax) continue;
		if (iode >= 0) return nav->eph + i;
		if (t <= tmin)/* choose eph that has toe closest to time */
		{
			j = i;
			tmin = t;
		}
	}
	if (iode >= 0 || j < 0)
	{
		//*msg += sprintf(*msg,"seleph5\n");
		return NULL;
	}
	return nav->eph + j;
}

/* select glonass ephememeris ------------------------------------------------*/
static geph_t* selgeph(gtime_t time, int sat, int iode, const nav_t* nav)
{
	double t, tmax = MAXDTOE_GLO, tmin = tmax + 1.0;
	int i, j = -1;

	// //trace(4,"selgeph : time=%s sat=%2d iode=%2d\n",time_str(time,3),sat,iode);

	for (i = 0; i < nav->ng; i++) {
		if (nav->geph[i].sat != sat) continue;
		if (iode >= 0 && nav->geph[i].iode != iode) continue;
		if ((t = fabs(timediff(nav->geph[i].toe, time))) > tmax) continue;
		if (iode >= 0) return nav->geph + i;
		if (t <= tmin) { j = i; tmin = t; } /* toe closest to time */
	}
	if (iode >= 0 || j < 0) {
		////trace(3,"no glonass ephemeris  : %s sat=%2d iode=%2d\n",time_str(time,0),
		//      sat,iode);
		return NULL;
	}
	return nav->geph + j;
}
extern void eph2pos(gtime_t time, const eph_t* eph, double* rs, double* dts,
	double* var)
{
	double tk, M, E, Ek, sinE, cosE, u, r, i, O, sin2u, cos2u, x, y, sinO, cosO, cosi, mu, omge;
	double xg, yg, zg, sino, coso;
	int n;
	unsigned char sys, prn;
	////trace(4, "eph2pos : time=%s sat=%2d\n", time_str(time, 3), eph->sat);

	if (eph->A <= 0.0) {
		rs[0] = rs[1] = rs[2] = *dts = *var = 0.0;
		return;
	}
	tk = timediff(time, eph->toe);

	switch ((sys = satsys(eph->sat, &prn))) {
	case SYS_GAL: mu = MU_GAL; omge = OMGE_GAL; break;
	case SYS_BDS: mu = MU_CMP; omge = OMGE_CMP; break;
	default:      mu = MU_GPS; omge = OMGE;     break;
	}
	M = eph->M0 + (sqrt(mu / (eph->A * eph->A * eph->A)) + eph->deln) * tk;

	for (n = 0, E = M, Ek = 0.0; fabs(E - Ek) > RTOL_KEPLER && n < MAX_ITER_KEPLER; n++) {
		Ek = E; E -= (E - eph->e * sin(E) - M) / (1.0 - eph->e * cos(E));
	}
	if (n >= MAX_ITER_KEPLER) {
		////trace(2, "eph2pos: kepler iteration overflow sat=%2d\n", eph->sat);
		return;
	}
	sinE = sin(E); cosE = cos(E);

	////trace(4, "kepler: sat=%2d e=%8.5f n=%2d del=%10.3e\n", eph->sat, eph->e, n, E - Ek);

	u = atan2(sqrt(1.0 - eph->e * eph->e) * sinE, cosE - eph->e) + eph->omg;
	r = eph->A * (1.0 - eph->e * cosE);
	i = eph->i0 + eph->idot * tk;
	sin2u = sin(2.0 * u); cos2u = cos(2.0 * u);
	u += eph->cus * sin2u + eph->cuc * cos2u;
	r += eph->crs * sin2u + eph->crc * cos2u;
	i += eph->cis * sin2u + eph->cic * cos2u;
	x = r * cos(u); y = r * sin(u); cosi = cos(i);

	/* beidou geo satellite (ref [9]) */
	if (sys == SYS_BDS && (prn <= 5 || prn >= 59)) {
		O = eph->OMG0 + eph->OMGd * tk - omge * eph->toes;
		sinO = sin(O); cosO = cos(O);
		xg = x * cosO - y * cosi * sinO;
		yg = x * sinO + y * cosi * cosO;
		zg = y * sin(i);
		sino = sin(omge * tk); coso = cos(omge * tk);
		rs[0] = xg * coso + yg * sino * COS_5 + zg * sino * SIN_5;
		rs[1] = -xg * sino + yg * coso * COS_5 + zg * coso * SIN_5;
		rs[2] = -yg * SIN_5 + zg * COS_5;
	}
	else {
		O = eph->OMG0 + (eph->OMGd - omge) * tk - omge * eph->toes;
		sinO = sin(O); cosO = cos(O);
		rs[0] = x * cosO - y * cosi * sinO;
		rs[1] = x * sinO + y * cosi * cosO;
		rs[2] = y * sin(i);
	}

	tk = timediff(time, eph->toc);
	*dts = eph->f0 + eph->f1 * tk + eph->f2 * tk * tk;
	/* relativity correction */
	*dts -= 2.0 * sqrt(mu * eph->A) * eph->e * sinE / SQR(CLIGHT);
	/* position and clock error variance */
	*var = var_uraeph(sys, eph->sva);
}
/* glonass orbit differential equations --------------------------------------*/
static void deq(const double* x, double* xdot, const double* acc)
{
	double a, b, c, r2 = dot(x, x, 3), r3 = r2 * sqrt(r2), omg2 = SQR(OMGE_GLO);

	if (r2 <= 0.0) {
		xdot[0] = xdot[1] = xdot[2] = xdot[3] = xdot[4] = xdot[5] = 0.0;
		return;
	}
	/* ref [2] A.3.1.2 with bug fix for xdot[4],xdot[5] */
	a = 1.5 * J2_GLO * MU_GLO * SQR(RE_GLO) / r2 / r3; /* 3/2*J2*mu*Ae^2/r^5 */
	b = 5.0 * x[2] * x[2] / r2;                    /* 5*z^2/r^2 */
	c = -MU_GLO / r3 - a * (1.0 - b);                /* -mu/r^3-a(1-b) */
	xdot[0] = x[3]; xdot[1] = x[4]; xdot[2] = x[5];
	xdot[3] = (c + omg2) * x[0] + 2.0 * OMGE_GLO * x[4] + acc[0];
	xdot[4] = (c + omg2) * x[1] - 2.0 * OMGE_GLO * x[3] + acc[1];
	xdot[5] = (c - 2.0 * a) * x[2] + acc[2];
}
/* glonass position and velocity by numerical integration --------------------*/
static void glorbit(double t, double* x, const double* acc)
{
	double k1[6], k2[6], k3[6], k4[6], w[6];
	int i;

	deq(x, k1, acc); for (i = 0; i < 6; i++) w[i] = x[i] + k1[i] * t / 2.0;
	deq(w, k2, acc); for (i = 0; i < 6; i++) w[i] = x[i] + k2[i] * t / 2.0;
	deq(w, k3, acc); for (i = 0; i < 6; i++) w[i] = x[i] + k3[i] * t;
	deq(w, k4, acc);
	for (i = 0; i < 6; i++) x[i] += (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]) * t / 6.0;
}
/* glonass ephemeris to satellite clock bias -----------------------------------
* compute satellite clock bias with glonass ephemeris
* args   : gtime_t time     I   time by satellite clock (gpst)
*          geph_t *geph     I   glonass ephemeris
* return : satellite clock bias (s)
* notes  : see ref [2]
*-----------------------------------------------------------------------------*/
extern double geph2clk(gtime_t time, const geph_t* geph)
{
	double t;
	int i;

	// //trace(4,"geph2clk: time=%s sat=%2d\n",time_str(time,3),geph->sat);

	t = timediff(time, geph->toe);

	for (i = 0; i < 2; i++) {
		t -= -geph->taun + geph->gamn * t;
	}
	return -geph->taun + geph->gamn * t;
}
/* glonass ephemeris to satellite position and clock bias ----------------------
* compute satellite position and clock bias with glonass ephemeris
* args   : gtime_t time     I   time (gpst)
*          geph_t *geph     I   glonass ephemeris
*          double *rs       O   satellite position {x,y,z} (ecef) (m)
*          double *dts      O   satellite clock bias (s)
*          double *var      O   satellite position and clock variance (m^2)
* return : none
* notes  : see ref [2]
*-----------------------------------------------------------------------------*/
extern void geph2pos(gtime_t time, const geph_t* geph, double* rs, double* dts,
	double* var)
{
	double t, tt, x[6];
	int i;

	////trace(4,"geph2pos: time=%s sat=%2d\n",time_str(time,3),geph->sat);

	t = timediff(time, geph->toe);

	*dts = -geph->taun + geph->gamn * t;

	for (i = 0; i < 3; i++) {
		x[i] = geph->pos[i];
		x[i + 3] = geph->vel[i];
	}
	for (tt = t < 0.0 ? -TSTEP : TSTEP; fabs(t) > 1E-9; t -= tt) {
		if (fabs(t) < TSTEP) tt = t;
		glorbit(tt, x, geph->acc);
	}
	for (i = 0; i < 3; i++) rs[i] = x[i];

	*var = SQR(ERREPH_GLO);
}

/* satellite position and clock by broadcast ephemeris -----------------------*/
static int ephpos(gtime_t time, gtime_t teph, unsigned char sat, const nav_t* nav,
	int iode, double* rs, double* dts, double* var, int* svh)
{
	eph_t* eph;
	geph_t* geph;
	double rst[3], dtst[1], tt = 1E-3;
	int i;
	unsigned char sys, prn;
	sys = satsys(sat, &prn);
	*svh = -1;

	if (sys == SYS_GPS || sys == SYS_GAL || sys == SYS_QZS || sys == SYS_BDS) {
		if (!(eph = seleph(teph, sat, iode, nav))) return 0;
		eph2pos(time, eph, rs, dts, var);
		time = timeadd(time, tt);
		eph2pos(time, eph, rst, dtst, var);
		*svh = eph->svh;
	}
	else if (sys == SYS_GLO) {
		if (!(geph = selgeph(teph, sat, iode, nav))) return 0;
		geph2pos(time, geph, rs, dts, var);
		time = timeadd(time, tt);
		geph2pos(time, geph, rst, dtst, var);
		*svh = geph->svh;
	}
	/* satellite velocity and clock drift by differential approx */
	for (i = 0; i < 3; i++) rs[i + 3] = (rst[i] - rs[i]) / tt;
	dts[1] = (dtst[0] - dts[0]) / tt;

	return 1;
}
/* satellite clock with broadcast ephemeris ----------------------------------*/
static int ephclk(gtime_t time, gtime_t teph, unsigned char sat, const nav_t* nav, double* dts)
{
	eph_t* eph;
	geph_t* geph;
	unsigned char sys;
	sys = satsys(sat, NULL);
	if (sys == SYS_GPS || sys == SYS_GAL || sys == SYS_QZS || sys == SYS_BDS) {
		if (!(eph = seleph(teph, sat, -1, nav))) return 0;
		*dts = eph2clk(time, eph);
	}
	else if (sys == SYS_GLO) {
		if (!(geph = selgeph(teph, sat, -1, nav))) return 0;
		*dts = geph2clk(time, geph);
	}
	else return 0;

	return 1;
}


/* satellite position and clock ------------------------------------------------
* compute satellite position, velocity and clock
* args   : gtime_t time     I   time (gpst)
*          gtime_t teph     I   time to select ephemeris (gpst)
*          int    sat       I   satellite number
*          nav_t  *nav      I   navigation data
*          int    ephopt    I   ephemeris option (EPHOPT_???)
*          double *rs       O   sat position and velocity (ecef)
*                               {x,y,z,vx,vy,vz} (m|m/s)
*          double *dts      O   sat clock {bias,drift} (s|s/s)
*          double *var      O   sat position and clock error variance (m^2)
*          int    *svh      O   sat health flag (-1:correction not available)
* return : status (1:ok,0:error)
* notes  : satellite position is referenced to antenna phase center
*          satellite clock does not include code bias correction (tgd or bgd)
*-----------------------------------------------------------------------------*/
static int satpos(gtime_t time, gtime_t teph, int sat,
	int ephopt, const nav_t* nav, double* rs, double* dts, double* var, int* svh)
{
	*svh = 0;
	switch (ephopt) {
		//return ephpos(time,teph,sat,nav,-1,rs,dts,var,svh,msg);
	case EPHOPT_BRDC: return ephpos(time, teph, sat, nav, -1, rs, dts, var, svh);
	}
	*svh = -1;
	return 0;
}

static int searchAvalidPsr(obsd_t* obs) {
	int f, index = -1;
	//for (f = NFREQ-1; f >= 0; f--) {
	//	if (obs->P[f] != 0.0) {
	//		index = f;
	//		break;
	//	}
	//}
	for (f = 0; f < NFREQ; f++) {
		if (obs->P[f] != 0.0) {
			index = f;
			break;
		}
	}
	return index;
}

extern void satposs(gtime_t teph, obsd_t* obs, int n,
	int ephopt, double* rs, double* dts, double* var, int* svh)
{
	gtime_t time[MAXOBS * 2] = { {0} };
	double dt, P;
	int i, j, index;
	unsigned char sys, prn;
	for (i = 0; (i < n) && (i < MAXOBS * 2); i++)
	{
		for (j = 0; j < 6; j++) rs[j + i * 6] = 0.0;
		for (j = 0; j < 2; j++) dts[j + i * 2] = 0.0;
		var[i] = 0.0; svh[i] = 0;
		obs[i].pvtAvalidPsrIndex = -1;
		index = searchAvalidPsr(&obs[i]);
		if (index == -1) continue;
		obs[i].pvtAvalidPsrIndex = index;
		P = obs[i].P[index];
		//if (P <=0.0)	continue;
		sys = satsys(obs[i].sat, &prn);
		/* transmission time by satellite clock */
		time[i] = timeadd(obs[i].time, -P / CLIGHT);
		/* satellite clock bias by broadcast ephemeris */
		if (!ephclk(time[i], teph, obs[i].sat, &g_nav, &dt)) {
			//trace(0x04, "no satellite clock sys=%d prn=%d\n", sys, prn);
			continue;
		}
		time[i] = timeadd(time[i], -dt);

		/* satellite position and clock at transmission time */
		if (!satpos(time[i], teph, obs[i].sat, ephopt, &g_nav, rs + i * 6, dts + i * 2, var + i, svh + i)) {
			//trace(0x04, "satellite position error sat=%d\n", obs[i].sat);
			continue;
		}
		//if no precise clock available, use broadcast clock instead
		if (dts[i * 2] == 0.0)
		{
			dts[i * 2] = dt;
			*var = SQR(STD_BRDCCLK);
		}
	}
	if (trace_flag[5] == 1) {
		for (i = 0; i < n && i < 2 * MAXOBS; i++)
		{
			trace(0x10, "%s sat=%2d rs=%13.3f %13.3f %13.3f dts=%12.3f var=%7.3f svh=%02X\n",
				time_str(time[i], 6), obs[i].sat, rs[i * 6], rs[1 + i * 6], rs[2 + i * 6], dts[i * 2] * 1E9, var[i], svh[i]);
			printf("%s sat=%2d rs=%13.3f %13.3f %13.3f dts=%12.3f var=%7.3f svh=%02X\n",
				time_str(time[i], 6), obs[i].sat, rs[i * 6], rs[1 + i * 6], rs[2 + i * 6], dts[i * 2] * 1E9, var[i], svh[i]);
		}
	}
}


