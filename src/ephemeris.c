#include <math.h>
#include "rtk.h"

#define STD_BRDCCLK 30.0  /* error of broadcast clock (m) */
#define RTOL_KEPLER 1E-13 /* relative tolerance for Kepler equation */

#define RE_GLO 6378136.0      /* radius of earth (m)            ref [2] */
#define MU_GPS 3.9860050E14   /* gravitational constant         ref [1] */
#define MU_GLO 3.9860044E14   /* gravitational constant         ref [2] */
#define MU_GAL 3.986004418E14 /* earth gravitational constant   ref [7] */
#define MU_CMP 3.986004418E14 /* earth gravitational constant   ref [9] */
#define J2_GLO 1.0826257E-3   /* 2nd zonal harmonic of geopot   ref [2] */

#define OMGE_GLO 7.292115E-5     /* earth angular velocity (rad/s) ref [2] */
#define OMGE_GAL 7.2921151467E-5 /* earth angular velocity (rad/s) ref [7] */
#define OMGE_CMP 7.292115E-5     /* earth angular velocity (rad/s) ref [9] */

#define ERREPH_GLO 5.0    /* error of glonass ephemeris (m) */
#define TSTEP 60.0        /* integration step glonass ephemeris (s) */
#define RTOL_KEPLER 1E-13 /* relative tolerance for Kepler equation */

#define SIN_5 -0.0871557427476582  /* sin(-5.0 deg) */
#define COS_5 0.9961946980917456   /* cos(-5.0 deg) */
#define MAX_ITER_KEPLER 30         /* max number of iteration of Kelpler */
#define MAXECORSSR 10.0            /* max orbit correction of ssr (m) */
#define MAXCCORSSR (1E-6 * CLIGHT) /* max clock correction of ssr (m) */
#define MAXAGESSR_HRCLK 10.0       /* max age of ssr high-rate clock (s) */
#define MAXAGESSR 1800.0           /* max age of ssr orbit and clock (s) */
#define DEFURASSR 0.15             /* default accurary of ssr corr (m) */
// satellite clock does not include relativity correction and tdg
static double var_uraeph(unsigned char sys, int ura)
{
    const double ura_value[] = {2.0,  2.8,   4.0,   5.7,   8,      11.3,   16.0,   32.0,
                                64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0, 4096.0, 8192.0};
    if (sys == SYS_GAL)
    { /* galileo sisa (ref [7] 5.1.11) */
        if (ura <= 49)
        {
            return SQR(ura * 0.01);
        }
        if (ura <= 74)
        {
            return SQR(0.5 + (ura - 50) * 0.02);
        }
        if (ura <= 99)
        {
            return SQR(1.0 + (ura - 75) * 0.04);
        }
        if (ura <= 125)
        {
            return SQR(2.0 + (ura - 100) * 0.16);
        }
        return SQR(500.0);
    }
    else
    { /* gps ura (ref [1] 20.3.3.3.1.1) */
        return ura < 0 || 14 < ura ? SQR(6144.0) : SQR(ura_value[ura]);
    }
}
/* -- double eph2clk(gtime_t time,const eph_t *eph) --------------------------------------
 *
 * Description    : broadcast ephemeris to sat clock bias
 * Parameters    : time    I    time by sat clock
 *                            eph        I    broadcast ephemeris
 * Return        : sat clock bias
 */
double eph2clk(gtime_t time, const eph_t* eph)
{
    double t;
    int    i;
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
    int    i, j = -1;
    tmax = MAXDTOE + 1.0;
    tmin = tmax + 1.0;

    for (i = 0; i < nav->n; i++)
    {
        if (nav->eph[i].sat != sat)
        {
            continue;
        }
        if (iode >= 0 && nav->eph[i].iode != iode)
        {
            continue;
        }
        if ((t = fabs(timediff(nav->eph[i].toe, time))) > tmax)
        {
            continue;
        }
        if (iode >= 0)
        {
            return nav->eph + i;
        }
        if (t <= tmin) /* choose eph that has toe closest to time */
        {
            j    = i;
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
    int    i, j = -1;

    // //trace(4,"selgeph : time=%s sat=%2d iode=%2d\n",time_str(time,3),sat,iode);

    for (i = 0; i < nav->ng; i++)
    {
        if (nav->geph[i].sat != sat)
        {
            continue;
        }
        if (iode >= 0 && nav->geph[i].iode != iode)
        {
            continue;
        }
        if ((t = fabs(timediff(nav->geph[i].toe, time))) > tmax)
        {
            continue;
        }
        if (iode >= 0)
        {
            return nav->geph + i;
        }
        if (t <= tmin)
        {
            j    = i;
            tmin = t;
        } /* toe closest to time */
    }
    if (iode >= 0 || j < 0)
    {
        ////trace(3,"no glonass ephemeris  : %s sat=%2d iode=%2d\n",time_str(time,0),
        //      sat,iode);
        return NULL;
    }
    return nav->geph + j;
}
extern void eph2pos(gtime_t time, const eph_t* eph, double* rs, double* dts, double* var)
{
    double tk, M, E, Ek, sinE, cosE, u, r, i, O, sin2u, cos2u, x, y, sinO, cosO, cosi, mu, omge;
    double xg, yg, zg, sino, coso;
    int    n;
    unsigned char sys, prn;
    ////trace(4, "eph2pos : time=%s sat=%2d\n", time_str(time, 3), eph->sat);

    if (eph->A <= 0.0)
    {
        rs[0] = rs[1] = rs[2] = *dts = *var = 0.0;
        return;
    }
    tk = timediff(time, eph->toe);

    switch ((sys = satsys(eph->sat, &prn)))
    {
        case SYS_GAL:
            mu   = MU_GAL;
            omge = OMGE_GAL;
            break;
        case SYS_BDS:
            mu   = MU_CMP;
            omge = OMGE_CMP;
            break;
        default:
            mu   = MU_GPS;
            omge = OMGE;
            break;
    }
    M = eph->M0 + (sqrt(mu / (eph->A * eph->A * eph->A)) + eph->deln) * tk;

    for (n = 0, E = M, Ek = 0.0; fabs(E - Ek) > RTOL_KEPLER && n < MAX_ITER_KEPLER; n++)
    {
        Ek = E;
        E -= (E - eph->e * sin(E) - M) / (1.0 - eph->e * cos(E));
    }
    if (n >= MAX_ITER_KEPLER)
    {
        ////trace(2, "eph2pos: kepler iteration overflow sat=%2d\n", eph->sat);
        return;
    }
    sinE = sin(E);
    cosE = cos(E);

    ////trace(4, "kepler: sat=%2d e=%8.5f n=%2d del=%10.3e\n", eph->sat, eph->e, n, E - Ek);

    u     = atan2(sqrt(1.0 - eph->e * eph->e) * sinE, cosE - eph->e) + eph->omg;
    r     = eph->A * (1.0 - eph->e * cosE);
    i     = eph->i0 + eph->idot * tk;
    sin2u = sin(2.0 * u);
    cos2u = cos(2.0 * u);
    u += eph->cus * sin2u + eph->cuc * cos2u;
    r += eph->crs * sin2u + eph->crc * cos2u;
    i += eph->cis * sin2u + eph->cic * cos2u;
    x    = r * cos(u);
    y    = r * sin(u);
    cosi = cos(i);

    /* beidou geo satellite (ref [9]) */
    if (sys == SYS_BDS && (prn <= 5 || prn >= 59))
    {
        O     = eph->OMG0 + eph->OMGd * tk - omge * eph->toes;
        sinO  = sin(O);
        cosO  = cos(O);
        xg    = x * cosO - y * cosi * sinO;
        yg    = x * sinO + y * cosi * cosO;
        zg    = y * sin(i);
        sino  = sin(omge * tk);
        coso  = cos(omge * tk);
        rs[0] = xg * coso + yg * sino * COS_5 + zg * sino * SIN_5;
        rs[1] = -xg * sino + yg * coso * COS_5 + zg * coso * SIN_5;
        rs[2] = -yg * SIN_5 + zg * COS_5;
    }
    else
    {
        O     = eph->OMG0 + (eph->OMGd - omge) * tk - omge * eph->toes;
        sinO  = sin(O);
        cosO  = cos(O);
        rs[0] = x * cosO - y * cosi * sinO;
        rs[1] = x * sinO + y * cosi * cosO;
        rs[2] = y * sin(i);
    }

    tk   = timediff(time, eph->toc);
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

    if (r2 <= 0.0)
    {
        xdot[0] = xdot[1] = xdot[2] = xdot[3] = xdot[4] = xdot[5] = 0.0;
        return;
    }
    /* ref [2] A.3.1.2 with bug fix for xdot[4],xdot[5] */
    a       = 1.5 * J2_GLO * MU_GLO * SQR(RE_GLO) / r2 / r3; /* 3/2*J2*mu*Ae^2/r^5 */
    b       = 5.0 * x[2] * x[2] / r2;                        /* 5*z^2/r^2 */
    c       = -MU_GLO / r3 - a * (1.0 - b);                  /* -mu/r^3-a(1-b) */
    xdot[0] = x[3];
    xdot[1] = x[4];
    xdot[2] = x[5];
    xdot[3] = (c + omg2) * x[0] + 2.0 * OMGE_GLO * x[4] + acc[0];
    xdot[4] = (c + omg2) * x[1] - 2.0 * OMGE_GLO * x[3] + acc[1];
    xdot[5] = (c - 2.0 * a) * x[2] + acc[2];
}
/* glonass position and velocity by numerical integration --------------------*/
static void glorbit(double t, double* x, const double* acc)
{
    double k1[6], k2[6], k3[6], k4[6], w[6];
    int    i;

    deq(x, k1, acc);
    for (i = 0; i < 6; i++)
    {
        w[i] = x[i] + k1[i] * t / 2.0;
    }
    deq(w, k2, acc);
    for (i = 0; i < 6; i++)
    {
        w[i] = x[i] + k2[i] * t / 2.0;
    }
    deq(w, k3, acc);
    for (i = 0; i < 6; i++)
    {
        w[i] = x[i] + k3[i] * t;
    }
    deq(w, k4, acc);
    for (i = 0; i < 6; i++)
    {
        x[i] += (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]) * t / 6.0;
    }
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
    int    i;

    // //trace(4,"geph2clk: time=%s sat=%2d\n",time_str(time,3),geph->sat);

    t = timediff(time, geph->toe);

    for (i = 0; i < 2; i++)
    {
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
extern void geph2pos(gtime_t time, const geph_t* geph, double* rs, double* dts, double* var)
{
    double t, tt, x[6];
    int    i;

    ////trace(4,"geph2pos: time=%s sat=%2d\n",time_str(time,3),geph->sat);

    t = timediff(time, geph->toe);

    *dts = -geph->taun + geph->gamn * t;

    for (i = 0; i < 3; i++)
    {
        x[i]     = geph->pos[i];
        x[i + 3] = geph->vel[i];
    }
    for (tt = t < 0.0 ? -TSTEP : TSTEP; fabs(t) > 1E-9; t -= tt)
    {
        if (fabs(t) < TSTEP)
        {
            tt = t;
        }
        glorbit(tt, x, geph->acc);
    }
    for (i = 0; i < 3; i++)
    {
        rs[i] = x[i];
    }

    *var = SQR(ERREPH_GLO);
}

/* satellite position and clock by broadcast ephemeris -----------------------*/
static int ephpos(
    gtime_t time, gtime_t teph, unsigned char sat, const nav_t* nav, int iode, double* rs,
    double* dts, double* var, int* svh
)
{
    eph_t*        eph;
    geph_t*       geph;
    double        rst[3], dtst[1], tt = 1E-3;
    int           i;
    unsigned char sys, prn;
    sys  = satsys(sat, &prn);
    *svh = -1;

    if (sys == SYS_GPS || sys == SYS_GAL || sys == SYS_QZS || sys == SYS_BDS)
    {
        if (!(eph = seleph(teph, sat, iode, nav)))
        {
            return 0;
        }
        eph2pos(time, eph, rs, dts, var);
        time = timeadd(time, tt);
        eph2pos(time, eph, rst, dtst, var);
        *svh = eph->svh;
    }
    else if (sys == SYS_GLO)
    {
        if (!(geph = selgeph(teph, sat, iode, nav)))
        {
            return 0;
        }
        geph2pos(time, geph, rs, dts, var);
        time = timeadd(time, tt);
        geph2pos(time, geph, rst, dtst, var);
        *svh = geph->svh;
    }
    /* satellite velocity and clock drift by differential approx */
    for (i = 0; i < 3; i++)
    {
        rs[i + 3] = (rst[i] - rs[i]) / tt;
    }
    dts[1] = (dtst[0] - dts[0]) / tt;

    return 1;
}
/* satellite clock with broadcast ephemeris ----------------------------------*/
static int ephclk(gtime_t time, gtime_t teph, unsigned char sat, const nav_t* nav, double* dts)
{
    eph_t*        eph;
    geph_t*       geph;
    unsigned char sys;
    sys = satsys(sat, NULL);
    if (sys == SYS_GPS || sys == SYS_GAL || sys == SYS_QZS || sys == SYS_BDS)
    {
        if (!(eph = seleph(teph, sat, -1, nav)))
        {
            return 0;
        }
        *dts = eph2clk(time, eph);
    }
    else if (sys == SYS_GLO)
    {
        if (!(geph = selgeph(teph, sat, -1, nav)))
        {
            return 0;
        }
        *dts = geph2clk(time, geph);
    }
    else
    {
        return 0;
    }

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
static int satpos(
    gtime_t time, gtime_t teph, int sat, int ephopt, const nav_t* nav, double* rs, double* dts,
    double* var, int* svh
)
{
    *svh = 0;
    switch (ephopt)
    {
            // return ephpos(time,teph,sat,nav,-1,rs,dts,var,svh,msg);
        case EPHOPT_BRDC:
            return ephpos(time, teph, sat, nav, -1, rs, dts, var, svh);
    }
    *svh = -1;
    return 0;
}

static int searchAvalidPsr(obsd_t* obs)
{
    int f, index = -1;
    // for (f = NFREQ-1; f >= 0; f--) {
    //     if (obs->P[f] != 0.0) {
    //         index = f;
    //         break;
    //     }
    // }
    for (f = 0; f < NFREQ; f++)
    {
        if (obs->P[f] != 0.0)
        {
            index = f;
            break;
        }
    }
    char id[4];
    satno2id(obs->sat, id);
    // trace(2,"searchAvalidPsr, %d,%s,\n",index,id);
    return index;
}

extern void satposs(
    gtime_t teph, obsd_t* obs, int n, int ephopt, double* rs, double* dts, double* var, int* svh
)
{
    gtime_t       time[MAXOBS * 2] = {{0}};
    double        dt, P;
    int           i, j, index;
    unsigned char sys, prn;
    for (i = 0; (i < n) && (i < MAXOBS * 2); i++)
    {
        for (j = 0; j < 6; j++)
        {
            rs[j + i * 6] = 0.0;
        }
        for (j = 0; j < 2; j++)
        {
            dts[j + i * 2] = 0.0;
        }
        var[i]                   = 0.0;
        svh[i]                   = 0;
        obs[i].pvtAvalidPsrIndex = -1;
        index                    = searchAvalidPsr(&obs[i]);
        if (index == -1)
        {
            continue;
        }
        obs[i].pvtAvalidPsrIndex = index;
        P                        = obs[i].P[index];
        // if (P <=0.0)    continue;
        sys = satsys(obs[i].sat, &prn);
        /* transmission time by satellite clock */
        time[i] = timeadd(obs[i].time, -P / CLIGHT);
        /* satellite clock bias by broadcast ephemeris */
        if (!ephclk(time[i], teph, obs[i].sat, &g_nav, &dt))
        {
            // trace(0x04, "no satellite clock sys=%d prn=%d\n", sys, prn);
            continue;
        }
        time[i] = timeadd(time[i], -dt);

        /* satellite position and clock at transmission time */
        if (!satpos(
                time[i], teph, obs[i].sat, ephopt, &g_nav, rs + i * 6, dts + i * 2, var + i, svh + i
            ))
        {
            // trace(0x04, "satellite position error sat=%d\n", obs[i].sat);
            continue;
        }
        // if no precise clock available, use broadcast clock instead
        if (dts[i * 2] == 0.0)
        {
            dts[i * 2] = dt;
            *var       = SQR(STD_BRDCCLK);
        }
    }
}

extern void assignSatBias(rtk_t* rtk, gtime_t teph, gtime_t tepb, obsd_tmp_t* obs_tmp, int ns)
{
    unsigned char sat, baseSat, sys, prn, sati, frqi;
    int           i, j, k, m, f, svh2;
    double        rs1[6], dts1[2], rs2[6], dts2[2], P1, P2, L1, L2, dt;
    double        var1, var2, svh1, baseBias, ddbias, resBias;
    double        e[3], r1, r2, pos[3], lam[NFREQ], y1, y2;
    double        zhd, azel[2], zazel[] = {0.0, 90.0 * D2R};
    double        P_base_sat[NSYS][NFREQ] = {0};
    double        L_base_sat[NSYS][NFREQ] = {0};
    double        y_b[NSYS][NFREQ]        = {0};
    gtime_t       time;

    for (i = 0; i < ns; i++)
    {
        for (j = 0; j < NFREQ; j++)
        {
            sat = obs_tmp[i].sat;
            sys = satsys(sat, &prn);
            switch (sys)
            {
                case SYS_GPS:
                    m = 0;
                    break;
                case SYS_GLO:
                    m = 1;
                    break;
                case SYS_GAL:
                    m = 2;
                    break;
                case SYS_BDS:
                    m = 3;
                    break;
                case SYS_QZS:
                    m = 0;
                    break;
            }
            if (rtk->base_prn[m][j] != sat)
            {
                continue;
            }
            if (rtk->base_prn[m][j] == 0)
            {
                continue;
            }
            P1 = obs_tmp[i].P[j];
            P2 = obs_tmp[i + ns].P[j];
            L1 = obs_tmp[i].L[j];
            L2 = obs_tmp[i + ns].L[j];
            // if (rtk->ssat[sat - 1].vs != 1) continue;
            if (P1 == 0.0 || P2 == 0.0 || L1 == 0.0 || L2 == 0.0)
            {
                continue;
            }
            time = timeadd(teph, -P1 / CLIGHT);
            if (!ephclk(time, teph, sat, &g_nav, &dt))
            {
                trace(0x04, "assignSatBias no satellite clock sys=%d prn=%d\n", sys, prn);
                continue;
            }
            time = timeadd(time, -dt);
            if (!satpos(time, teph, sat, EPHOPT_BRDC, &g_nav, rs1, dts1, &var1, &svh2))
            {
                trace(0x04, "assignSatBias satellite position error sat=%d\n", sat);
                continue;
            }
            r1 = geodist(rs1, rtk->sol.rr, e);
            r1 -= CLIGHT * dts1[0];
            if (rtk->opt.mode != 4)
            {
                ecef2pos(rtk->sol.rr, pos);
                satazel(pos, e, azel);
                zhd = tropmodel(teph, pos, zazel, 0.0);
                r1 += tropmapf(teph, pos, azel, NULL) * zhd;
            }

            time = timeadd(tepb, -P2 / CLIGHT);
            if (!ephclk(time, tepb, sat, &g_nav, &dt))
            {
                trace(0x04, "assignSatBias no satellite clock sys=%d prn=%d\n", sys, prn);
                continue;
            }
            time = timeadd(time, -dt);
            if (!satpos(time, tepb, sat, EPHOPT_BRDC, &g_nav, rs2, dts2, &var2, &svh2))
            {
                trace(0x04, "assignSatBias satellite position error sat=%d\n", sat);
                continue;
            }
            r2 = geodist(rs2, rtk->rb, e);
            r2 -= CLIGHT * dts2[0];
            if (rtk->opt.mode != 4)
            {
                ecef2pos(rtk->rb, pos);
                satazel(pos, e, azel);
                zhd = tropmodel(tepb, pos, zazel, 0.0);
                r2 += tropmapf(tepb, pos, azel, NULL) * zhd;
            }

            if (sys == SYS_GPS || sys == SYS_QZS)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_gpsLam[k];
                }
            }
            else if (sys == SYS_GLO)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_gloLam[prn - 1][k];
                }
            }
            else if (sys == SYS_GAL)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_galLam[k];
                }
            }
            else
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_bdsLam[k];
                }
            }

            y1        = L1 * lam[j] - r1;
            y2        = L2 * lam[j] - r2;
            y_b[m][j] = y_b[m][j] = y1 - y2;
        }
    }

    for (i = 0; i < ns; i++)
    {
        for (j = 0; j < NFREQ; j++)
        {
            P1  = obs_tmp[i].P[j];
            P2  = obs_tmp[i + ns].P[j];
            L1  = obs_tmp[i].L[j];
            L2  = obs_tmp[i + ns].L[j];
            sat = obs_tmp[i].sat;
            sys = satsys(sat, &prn);
            // if (rtk->ssat[sat - 1].vs != 1) continue;
            if (P1 == 0.0 || P2 == 0.0 || L1 == 0.0 || L2 == 0.0)
            {
                continue;
            }
            time = timeadd(teph, -P1 / CLIGHT);
            if (!ephclk(time, teph, sat, &g_nav, &dt))
            {
                trace(0x04, "assignSatBias no satellite clock sys=%d prn=%d\n", sys, prn);
                continue;
            }
            time = timeadd(time, -dt);
            if (!satpos(time, teph, sat, EPHOPT_BRDC, &g_nav, rs1, dts1, &var1, &svh2))
            {
                trace(0x04, "assignSatBias satellite position error sat=%d\n", sat);
                continue;
            }
            r1 = geodist(rs1, rtk->sol.rr, e);
            r1 -= CLIGHT * dts1[0];
            if (rtk->opt.mode != 4)
            {
                ecef2pos(rtk->sol.rr, pos);
                satazel(pos, e, azel);
                zhd = tropmodel(teph, pos, zazel, 0.0);
                r1 += tropmapf(teph, pos, azel, NULL) * zhd;
            }
            time = timeadd(tepb, -P2 / CLIGHT);
            if (!ephclk(time, tepb, sat, &g_nav, &dt))
            {
                trace(0x04, "assignSatBias no satellite clock sys=%d prn=%d\n", sys, prn);
                continue;
            }
            time = timeadd(time, -dt);
            if (!satpos(time, tepb, sat, EPHOPT_BRDC, &g_nav, rs2, dts2, &var2, &svh2))
            {
                trace(0x04, "assignSatBias satellite position error sat=%d\n", sat);
                continue;
            }
            r2 = geodist(rs2, rtk->rb, e);
            r2 -= CLIGHT * dts2[0];
            if (rtk->opt.mode != 4)
            {
                ecef2pos(rtk->rb, pos);
                satazel(pos, e, azel);
                zhd = tropmodel(tepb, pos, zazel, 0.0);
                r2 += tropmapf(tepb, pos, azel, NULL) * zhd;
            }

            if (sys == SYS_GPS || sys == SYS_QZS)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_gpsLam[k];
                }
            }
            else if (sys == SYS_GLO)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_gloLam[prn - 1][k];
                }
            }
            else if (sys == SYS_GAL)
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_galLam[k];
                }
            }
            else
            {
                for (k = 0; k < NFREQ; k++)
                {
                    lam[k] = g_bdsLam[k];
                }
            }

            y1 = L1 * lam[j] - r1;
            y2 = L2 * lam[j] - r2;

            switch (sys)
            {
                case SYS_GPS:
                    m = 0;
                    break;
                case SYS_GLO:
                    m = 1;
                    break;
                case SYS_GAL:
                    m = 2;
                    break;
                case SYS_BDS:
                    m = 3;
                    break;
                case SYS_QZS:
                    m = 0;
                    break;
            }
            if (rtk->base_prn[m][j] == 0)
            {
                continue;
            }
            baseSat = rtk->base_prn[m][j];
            if (baseSat == sat || baseSat == 0)
            {
                continue;
            }
            k = -1;
            for (k = 0; k < rtk->nx - rtk->np - rtk->nt - rtk->ni; k++)
            {
                sati = rtk->nxRecordSat[k];
                frqi = rtk->nxRecordFrq[k];
                if (frqi != j)
                {
                    continue;
                }
                if (sati == baseSat)
                {
                    break;
                }
            }
            if (k == -1)
            {
                continue;
            }

            //trace(
            //    3, "nxRecord,assignSatBias(), k, %d, xIndex, %d\n", rtk->ssat[sati - 1].xIndex[f]
            //);

            k                                 = rtk->np - rtk->nt - rtk->ni + k;
            baseBias                          = rtk->xp[k];
            rtk->ssat[baseSat - 1].fix_amb[j] = baseBias;
            // if (sat == 40) {
            //     printf("y_b=%.2f y1=%.2f y2=%.2f\n", y_b[m][j], y1, y2);
            // }
            ddbias                        = (y_b[m][j] - (y1 - y2)) / lam[j];
            resBias                       = ddbias - ROUND(ddbias);
            rtk->ssat[sat - 1].resBias[j] = resBias;
            if (fabs(resBias) > 0.3)
            {
                trace(0x04, "sys=%3d prn=%3d res bias:%.2f\n", sys, prn, resBias);
                continue;
            }
            // printf("sat1=%3d sat2=%3d sys=%3d prn=%3d  bias1:%10.2f base=%10.2f ddbias=%10d\n",
            //     sat, baseSat,sys, prn, baseBias - ROUND(ddbias), baseBias, ROUND(ddbias));
            rtk->ssat[sat - 1].fix_amb[j] = baseBias - ROUND(ddbias);
        }
    }
    // printf("-------------\n");
}

extern int isGEO(unsigned char sat)
{
    char id[4];
    satno2id(sat, id);

    if (!strncmp(id, "C01", 3) || !strncmp(id, "C02", 3) || !strncmp(id, "C03", 3) ||
        !strncmp(id, "C04", 3) || !strncmp(id, "C05", 3) || !strncmp(id, "C59", 3) ||
        !strncmp(id, "C60", 3))
    {
        return 1;
    }
    return 0;
}
extern int isIGSO(unsigned char sat)
{
    char id[4];
    satno2id(sat, id);

    if (!strncmp(id, "C06", 3) || !strncmp(id, "C07", 3) || !strncmp(id, "C08", 3) ||
        !strncmp(id, "C09", 3) || !strncmp(id, "C10", 3) || !strncmp(id, "C13", 3) ||
        !strncmp(id, "C16", 3) || !strncmp(id, "C38", 3) || !strncmp(id, "C39", 3) ||
        !strncmp(id, "C40", 3))
    {
        return 1;
    }
    return 0;
}

extern int isMEO(unsigned char sat)
{
    // double delays[60] = { 0.0 };
    char id[4];

    satno2id(sat, id);

    if (!strncmp(id, "C01", 3) || !strncmp(id, "C02", 3) || !strncmp(id, "C03", 3) ||
        !strncmp(id, "C04", 3) || !strncmp(id, "C05", 3) || !strncmp(id, "C59", 3) ||
        !strncmp(id, "C60", 3))
    {
        return 0;
    }
    else if (!strncmp(id, "C06", 3) || !strncmp(id, "C07", 3) || !strncmp(id, "C08", 3) ||
             !strncmp(id, "C09", 3) || !strncmp(id, "C10", 3) || !strncmp(id, "C13", 3) ||
             !strncmp(id, "C16", 3) || !strncmp(id, "C38", 3) || !strncmp(id, "C39", 3) ||
             !strncmp(id, "C40", 3))
    {
        return 0;
    }

    return 1;
}

extern int invalidBDS(unsigned char sat)
{
    int isat = 0;
    int i    = 0;

    char* invalidBDSList[17] = {"C31", "C47", "C48", "C49", "C50", "C51", "C52", "C53", "C54",
                                "C55", "C56", "C57", "C58", "C61", "C62", "C63", "C64"};

    for (i = 0; i < 17; i++)
    {
        isat = satid2no(invalidBDSList[i]);
        if (isat == sat)
        {
            return 1;
        }
    }

    return 0;
}
static int periodK(unsigned char sat)
{
    unsigned char sys, prn;
    int           K = 0;

    switch ((sys = satsys(sat, &prn)))
    {
        case SYS_GAL:
            K = 17;
            break;
        case SYS_BDS:
            K = isMEO(sat) == 0 ? 1 : 13;
            break;
        case SYS_GPS:
            K = 2;
            break;
        default:
            K = 0;
            break;
    }
    return K;
}
/* return period days */
extern int periodDay(unsigned char sat)
{
    unsigned char sys, prn;
    int           K = 0;

    switch ((sys = satsys(sat, &prn)))
    {
        case SYS_GAL:
            K = 10;
            break;
        case SYS_BDS:
            K = isMEO(sat) == 0 ? 1 : 7;
            break;
        case SYS_GPS:
            K = 1;
            break;
        default:
            K = 0;
            break;
    }
    return K;
}
/* Calc period by eph data;
 * $$n = \sqrt{GM} /a^{\frac{3}{2}}+\delta n;$$
 * $$T=2*\pi*k/n$$
 **/
static double get_eph_period(eph_t* eph)
{
    // double A = 5153.65531;
    // double deln = 4.249105564E-9;

    // printf("%f\n", 4 * SC2RAD / (sqrt(MUY) / (A * A * A) + deln));
    /* 8615.380 */
    unsigned char sys, prn;
    double        mu;
    int           k = 0;

    switch ((sys = satsys(eph->sat, &prn)))
    {
        case SYS_GAL:
            mu = MU_GAL;
            break;
        case SYS_BDS:
            mu = MU_CMP;
            break;
        default:
            mu = MU_GPS;
            break;
    }
    k = periodK(eph->sat);

    return (2 * k * SC2RAD) / (sqrt(mu / (eph->A * eph->A * eph->A)) + eph->deln);
}

extern double get_sid_T(unsigned char sat, gtime_t teph, const nav_t* nav)
{
    eph_t* eph;
    int    sys;

    sys = satsys(sat, NULL);

    if (sys == SYS_GPS || sys == SYS_GAL || sys == SYS_QZS || sys == SYS_BDS)
    {
        if (!(eph = seleph(teph, sat, -1, nav)))
        {
            return 0.0;
        }
        else
        {
            return get_eph_period(eph);
        }
    }
    return 0.0;
}
