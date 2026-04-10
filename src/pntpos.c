
#include "rtk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXITR 10     /* max number of iteration for point pos */
#define ERR_ION 5.0   /* ionospheric delay std (m) */
#define ERR_TROP 3.0  /* tropspheric delay std (m) */
#define ERR_SAAS 0.3  /* saastamoinen model error std (m) */
#define ERR_BRDCI 0.5 /* broadcast iono model error factor */
#define ERR_CBIAS 0.1 /* code bias error std (m) */
#define REL_HUMI 0.7  /* relative humidity for saastamoinen model */
#define NXSPP 7

static int    SysCnt = 0;
static double varerr(const prcopt_t* opt, double el, int sys)
{
    double fact, varr;
    fact = sys == SYS_GLO ? EFACT_GLO : (sys == SYS_BDS ? EFACT_SBS : EFACT_GPS);
    varr = SQR(100) * (SQR(0.003) + SQR(0.003) / sin(el));
    return SQR(fact) * varr;
}
#if 1
static double gettgd(gtime_t time, int sat)
{
    int i;
    for (i = 0; i < g_nav.n; i++)
    {
        if (g_nav.eph[i].sat != sat)
        {
            continue;
        }
        return CLIGHT * g_nav.eph[i].tgd[0];
    }
    return 0.0;
}
#else

static double gettgd(gtime_t time, int sat)
{
    GloSatEphInfo* gssat = NULL;
    SatEphInfo*    ssat  = NULL;
    unsigned char  sys, prn;
    sys = satsys(sat, &prn);
    if (sys == SYS_GLO)
    {
        return 0.0;
    }
    else
    {
        ssat = ssatpt_mcu(&m_pvteph, sat);
        if (ssat == NULL)
        {
            return 0.0;
        }
        else
        {
            return CLIGHT * ssat->eph.tgd[0];
        }
    }
    return 0.0;
}

#endif
// const double chi_square[]={10.83,13.82,16.27,18.47,20.51};//0.1%
static int solval(
    double* v, int nv, double* azel, int n, int* vsat, double* dop, const prcopt_t* opt
)
{
    int    i, ns;
    double tmp, azels[2 * MAXOBS];
    // chi square test
    if (nv > NXSPP)
    {
        tmp = dot(v, v, nv);
        if (tmp >= chisqr[nv - NXSPP - 1])
        {
            trace(
                0x04, "chi square test failed  nv=%d tem=%lf  chisqr=%lf\n", nv, tmp,
                chisqr[nv - NXSPP - 1]
            );
            return -2;
        }
    }
    for (i = 0, ns = 0; i < n; i++)
    {
        if (vsat[i])
        {
            azels[2 * ns]     = azel[2 * i];
            azels[2 * ns + 1] = azel[2 * i + 1];
            ns++;
        }
    }
    // GDOP test: azels contains only valid sat {az,el}
    dops(ns, azels, opt->elmin, dop);
    if ((dop[0] <= 0) || (dop[0] > opt->maxgdop))
    {
        trace(0x04, "GDOP error:%f,ns %d\n", dop[0], ns);
        return -1;
    }
    return 0;
}
extern int ionocorr(
    gtime_t time, int sat, const double* pos, const double* azel, int ionoopt, double* ion,
    double* var
)
{
    double ion_gps[] = {/* 2004/1/1 */
                        0.1118E-07, -0.7451E-08, -0.5961E-07, 0.1192E-06,
                        0.1167E+06, -0.2294E+06, -0.1311E+06, 0.1049E+07
    };
    /* GPS broadcast ionosphere model */
    if (ionoopt == IONOOPT_BRDC)
    {
        *ion = ionmodel(time, ion_gps, pos, azel);
        *var = SQR(*ion * ERR_BRDCI);
        return 1;
    }
    *ion = 0.0;
    *var = ionoopt == IONOOPT_OFF ? SQR(ERR_ION) : 0.0;
    return 1;
}
extern int tropcorr(
    gtime_t time, const double* pos, const double* azel, int tropopt, double* trp, double* var
)
{
    /* Saastamoinen model */
    if (tropopt == TROPOPT_SAAS || tropopt == TROPOPT_EST || tropopt == TROPOPT_ESTG)
    {
        *trp = tropmodel(time, pos, azel, REL_HUMI);
        *var = SQR(ERR_SAAS / (sin(azel[1]) + 0.1));
        return 1;
    }
    /* no correction */
    *trp = 0.0;
    *var = tropopt == TROPOPT_OFF ? SQR(ERR_TROP) : 0.0;
    return 1;
}

static double varrLsq(const prcopt_t* opt, const obsd_t* obs, int f, double el)
{
    double        VarMeas = 3, VarLockTime, VarMp, SatEl, TotalVar;
    unsigned char sys, prn;
    sys = satsys(obs->sat, &prn);
    /* var(measurement) */
    if (obs->SNR[f] / 4.0 > 48)
    {
        VarMeas = 1.0;
    }
    else if (obs->SNR[f] / 4.0 > 44)
    {
        VarMeas = 5 / pow(2.0, (obs->SNR[f] / 4.0 - 48) / 1);
    }
    else if (obs->SNR[f] / 4.0 > 40)
    {
        VarMeas = 10 / pow(2.0, (obs->SNR[f] / 4.0 - 44) / 2);
    }
    else if (obs->SNR[f] / 4.0 > 30)
    {
        VarMeas = 40 / pow(2.0, (obs->SNR[f] / 4.0 - 39) / 3);
    }
    else if (obs->SNR[f] / 4.0 > 22)
    {
        VarMeas = 403.17 / pow(2.0, (obs->SNR[f] / 4.0 - 28) / 6);
    }
    else if (obs->SNR[f] / 4.0 > 10)
    {
        VarMeas = 1140.34 / pow(2.0, (obs->SNR[f] / 4.0 - 16) / 12);
    }
    else
    {
        VarMeas = 1612.68;
    }
    /* var(LockTime) */
    VarLockTime = 0;
    // if (obs->LockTime[f] < 3000)
    //    VarLockTime = (3000 - obs->LockTime[f]);
    /* var(Mp) */
    VarMp = 0;

    // SatEl = el * R2D;
    // if (SatEl < 30 && SatEl > 0)
    //    VarMp = (90 - SatEl) * 300 / (obs->SNR[f] / 4.0);
    VarMp    = varerr(opt, el, sys);
    TotalVar = VarMeas + VarLockTime + VarMp;
    // if (sys == SYS_BDS && prn <= 5) TotalVar *= 10;
    return TotalVar;
}
/*
Compute residual and other components for LSE
args:    int *svh                I        SV health (-1: ephemeris unavailable,!=0 unhealthy sat)
int n                        I        number of observation data
double *rs            I        sat pos/vel (ecef), size n
double *dts            I        sat clock bias
double *x                I        receiver pos/clock bias
double *azel         O        {az,el} (rad), size 2n
double *H                O        design matrix
double *v                O        residual of all obs data
double *resp        O   residual of valid obs data
double *var            O        residual error variance
double *nv            O        number of valid sat
int *vsat                O        valid sat flag
*/

static int rescode(
    int post, sol_t* sol, const obsd_t* obs, const int* svh, int n, double* rs, double* dts,
    double* azel, double* H, double* v, double* var_sat, double* var, double* resp, double* x,
    int* vsat, int* exc, double* lsqraim, const prcopt_t* opt, int* ns, double tt
)
{
    unsigned char prn, nvSatMask[MAXSAT] = {0};
    int           i, j, k, index, sys, nv = 0, mask[NSYS] = {0};
    double        pos[3], e[3], rr[3];
    double        P, ion, dtr, trop, var_P, var_ion, var_trop, r, lam[NFREQ];
    double        stdv = 0.0;
    for (i = 0; i < 3; i++)
    {
        rr[i] = x[i];
    }
    ecef2pos(rr, pos);
    lsqraim[1] = 0.0;
    lsqraim[2] = 0.0;
    // printf("%14.4f %14.4f %14.4f\n", pos[0], pos[1], pos[2]);
    for (i = *ns = 0; i < n; i++)
    {
        vsat[i]     = 0;
        azel[i * 2] = azel[i * 2 + 1] = resp[i] = 0.0;
        if (svh[i])
        {
            continue;
        }
        if (exc[obs[i].sat - 1] == 1)
        {
            continue;
        }
        sys = satsys(obs[i].sat, &prn);
        if (sys == SYS_NONE)
        {
            continue;
        }
        /* reject duplicated observation data */
        if (i < n - 1 && i < MAXOBS - 1 && obs[i].sat == obs[i + 1].sat)
        {
            i++;
            continue;
        }
        if ((r = geodist(rs + i * 6, rr, e)) <= 0)
        {
            continue;
        }
        if (satazel(pos, e, azel + i * 2) < opt->elmin)
        {
            continue;
        }
        /* psudorange with code bias correction */
        index = obs[i].pvtAvalidPsrIndex;
        P     = obs[i].P[index];

        if (index == 0)
        {
            P -= gettgd(obs[i].time, obs[i].sat);
        }
        var_P = SQR(ERR_CBIAS);

        switch (sys)
        {
            case SYS_GPS:
                dtr = x[3];
                break;
            case SYS_QZS:
                dtr = x[3];
                break;
            case SYS_GLO:
                dtr = x[4];
                break;
            case SYS_GAL:
                dtr = x[5];
                break;
            case SYS_BDS:
                dtr = x[6];
                break;
            // case SYS_QZS: dtr = x[7]; break;
            default:
                return 0;
        }
        // ionosheric corrections: check opt for sbas|broadcast model
        ionocorr(obs[i].time, obs[i].sat, pos, azel + i * 2, opt->ionoopt, &ion, &var_ion);
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

        if (lam[0] == 0.0)
        {
            continue;
        }
        ion *= SQR(lam[0] / lam[index]);
        tropcorr(obs[i].time, pos, azel + 2 * i, opt->tropopt, &trop, &var_trop);

        v[nv] = P - (r + ion + trop + dtr - CLIGHT * dts[i * 2]);
        /* design matrix */
        for (j = 0; j < NXSPP; j++)
        {
            H[j + nv * NXSPP] = j < 3 ? -e[j] : (j == 3 ? 0.0 : 0.0);
        }
        /* time system and receiver bias offset correction */

        if (sys == SYS_GPS || sys == SYS_QZS)
        {
            H[3 + nv * NXSPP] = 1.0;
            mask[0]           = 1;
        }
        else if (sys == SYS_GLO)
        {
            H[4 + nv * NXSPP] = 1.0;
            mask[1]           = 1;
        }
        else if (sys == SYS_GAL)
        {
            H[5 + nv * NXSPP] = 1.0;
            mask[2]           = 1;
        }
        else if (sys == SYS_BDS)
        {
            H[6 + nv * NXSPP] = 1.0;
            mask[3]           = 1;
        }
        // else if (sys == SYS_QZS) { H[7 + nv * NXSPP] = 1.0; mask[4] = 1; }
        else
        {
            mask[0] = 1;
        }

        vsat[i] = 1;
        resp[i] = v[nv];
        var[nv] =
            varrLsq(opt, &obs[i], index, azel[1 + i * 2]) + var_sat[i] + var_ion + var_trop + var_P;
        // var[nv] = varerr_snr(opt, &obs[i], index, azel[1 + i * 2])+var_sat[i] + var_ion +
        // var_trop + var_P; var[nv] = varerr(opt, azel[1 + i * 2], sys) + var_sat[i] + var_ion +
        // var_trop + var_P; if (post == 0) {
        stdv = fabs(v[nv]) / sqrt(var[nv]);
        if (stdv > lsqraim[2])
        {
            lsqraim[0] = obs[i].sat;
            lsqraim[1] = fabs(v[nv]);
            lsqraim[2] = stdv;
        }
        //}
        nvSatMask[nv] = obs[i].sat;
        // if (obs[i].rcv==1)
        //    printf("rcv=%d sys=%3d prn=%3d v[%3d]=%14.3f var=%14.3f P=%14.3f r=%14.3f ion=%5.2f
        //    trop=%5.2f dtr=%14.3f dts=%14.3f\n",
        //        obs[i].rcv,sys, prn, nv, v[nv],var[nv], P, r, ion, trop, dtr, CLIGHT*dts[2 * i]);
        nv++;
        (*ns)++;
    }
    // printf("-------------------------\n");
    index = 0;
    for (i = 0; i < nv; i++)
    {
        sys = satsys(nvSatMask[i], &prn);
        switch (sys)
        {
            case SYS_GPS:
                dtr = x[3];
                break;
            case SYS_QZS:
                dtr = x[3];
                break;
            case SYS_GLO:
                dtr = x[4];
                break;
            case SYS_GAL:
                dtr = x[5];
                break;
            case SYS_BDS:
                dtr = x[6];
                break;
        }
    }
    if (index == 1)
    {
        return -1;
    }
    SysCnt = 0;
    if (index == 1)
    {
        return -1;
    }
    for (i = 0; i < NSYS; i++)
    {
        if (mask[i])
        {
            SysCnt++;
            continue;
        }
        v[nv] = 0.0;
        for (j = 0; j < NXSPP; j++)
        {
            H[j + nv * NXSPP] = j == i + 3 ? 1.0 : 0.0;
        }
        var[nv] = 0.01;
        nv++;
    }
    // printf("-------------------------------------\n");
    return nv;
}

static int estpos(
    const obsd_t* obs, int n, int* svh, double* rs, double* dts, double* resp, double* var_sat,
    double* azel, int* vsat, int* exc, double* lsqraim, const prcopt_t* opt, sol_t* sol, double tt
)
{
    int     i, j, k, index, ns0 = 0, info, ns = 0, post = 0, stat = -1, nv = 0;
    double  x[NXSPP] = {0}, dx[NXSPP], Q[NXSPP * NXSPP];
    double *H, *v, *var, sig, dop[4];
    double  B[MAXOBS * NXSPP] = {0}, A[MAXOBS * NXSPP];
    double  factor[MAXOBS]    = {0};
    double  r1, r2;
    H   = mat(NXSPP, n + 4);
    v   = mat(n + 4, 1);
    var = mat(n + 4, 1);
    if ((!H) || (!v) || (!var))
    {
        trace(0x01, "estpos mat allocate error");
        free(H);
        free(v);
        free(var);
        return -1;
    }
    for (i = 0; i < 3; i++)
    {
        x[i] = sol->rr[i];
    }
    if (tt < 2.0)
    {
        for (j = 0; j < NSYS; j++)
        {
            x[j + 3] = sol->dtr[j] * CLIGHT;
        }
    }
    for (i = 0; i < MAXITR; i++)
    {
        nv = rescode(
            post, sol, obs, svh, n, rs, dts, azel, H, v, var_sat, var, resp, x, vsat, exc, lsqraim,
            opt, &ns, tt
        );
        if (i == 0)
        {
            ns0 = ns;
        }
        if (nv == -1)
        {
            i = 0;
            continue;
        }
        if (nv < NXSPP + 1)
        {
            trace(0x02, "lack of valid sat:%d %d\n", ns, ns0);
            free(H);
            free(v);
            free(var);
            if (ns0 > 5 && lsqraim[0] > 0)
            {
                sol->ns[0] = ns0;
                return -2;
            }
            else
            {
                return -1;
            }
        }
        /* weight by variance */
        for (j = 0; j < nv; j++)
        {
            sig = sqrt(var[j]);
            v[j] /= sig;
            for (k = 0; k < NXSPP; k++)
            {
                H[k + j * NXSPP] /= sig;
            }
        }
        if ((info = lsq(H, v, NXSPP, nv, dx, Q)))
        {
            trace(0x02, "lsq error\n");
            free(H);
            free(v);
            free(var);
            return -1;
        }

        for (j = 0; j < NXSPP; j++)
        {
            x[j] += dx[j];
        }
        if (norm(dx, NXSPP) < 1E-3)
        {
            stat = solval(v, nv, azel, n, vsat, dop, opt);
            if (stat == -2)
            {
                sol->ns[0] = ns0;
                free(H);
                free(v);
                free(var);
                return stat;
            }

            for (j = 0; j < 3; j++)
            {
                sol->qr[j] = (float)Q[j + j * NXSPP];
            }
            // for (j = 0; j < 3; j++) {
            //    if (fabs(sol->qr[j]) > 400) {
            //        free(H); free(v); free(var);
            //        trace(4, "spp P too large=%f\n", sol->qr[j]);
            //        return -2;
            //    }
            // }
            sol->time = timeadd(obs[0].time, -x[3] / CLIGHT);
            for (j = 0; j < NSYS; j++)
            {
                sol->dtr[j] = x[j + 3] / CLIGHT; /* receiver clock bias (s) */
            }
            trace(0x10, "dtr=%.2f %.2f %.2f %.2f\n", x[3], x[4], x[5], x[6]);
            for (j = 0; j < 6; j++)
            {
                sol->rr[j] = j < 3 ? x[j] : 0.0;
            }
            for (j = 0; j < 3; j++)
            {
                sol->qr[j] = (float)Q[j + j * NXSPP];
            }
            sol->qr[3] = (float)Q[1];         /* cov xy */
            sol->qr[4] = (float)Q[2 + NXSPP]; /* cov yz */
            sol->qr[5] = (float)Q[2];         /* cov zx */

            sol->dop[0][0] = dop[0];
            sol->dop[0][1] = dop[1];
            sol->dop[0][2] = dop[2];
            sol->dop[0][3] = dop[3];
            sol->type      = 0;
            sol->ns[0]     = ns;
            free(H);
            free(v);
            free(var);
            return stat;
        }
        post++;
    }
    if (i >= MAXITR)
    {
        trace(0x02, "estpos diverged ns=%d norm dx=%f\n", nv, norm(dx, NXSPP));
    }
    sol->ns[0] = ns0;
    free(H);
    free(v);
    free(var);
    return -2;
}

/* doppler residuals ---------------------------------------------------------*/
static int resdop(
    const obsd_t* obs, int n, const double* rs, const double* dts, const double* rr,
    const double* x, const double* azel, const int* vsat, double* v, double* H
)
{
    double        lam, rate, pos[3], E[9], a[3], e[3], vs[3], cosel;
    int           i, j, nv = 0, sys;
    unsigned char prn;
    ecef2pos(rr, pos);
    xyz2enu(pos, E);
    for (i = 0; i < n && i < MAXOBS; i++)
    {
        sys = satsys(obs[i].sat, &prn);
        if (sys == SYS_GPS || sys == SYS_QZS)
        {
            lam = g_gpsLam[0];
        }
        else if (sys == SYS_GLO)
        {
            lam = g_gloLam[prn - 1][0];
        }
        else if (sys == SYS_GAL)
        {
            lam = g_galLam[0];
        }
        else
        {
            lam = g_bdsLam[0];
        }
        if (obs[i].D[0] == 0.0 || lam == 0.0 || !vsat[i] || norm(rs + 3 + i * 6, 3) <= 0.0)
        {
            continue;
        }
        /* line-of-sight vector in ecef */
        cosel = cos(azel[1 + i * 2]);
        a[0]  = sin(azel[i * 2]) * cosel;
        a[1]  = cos(azel[i * 2]) * cosel;
        a[2]  = sin(azel[1 + i * 2]);
        matmul("TN", 3, 1, 3, 1.0, E, a, 0.0, e);

        for (j = 0; j < 3; j++)
        {
            vs[j] = rs[j + 3 + i * 6];
        }

        rate = dot(vs, e, 3) + OMGE / CLIGHT *
                                   (rs[4 + i * 6] * rr[0] + rs[1 + i * 6] * x[0] -
                                    rs[3 + i * 6] * rr[1] - rs[i * 6] * x[1]);

        /* doppler residual */
        v[nv] = -lam * obs[i].D[0] - (rate + x[3] - CLIGHT * dts[1 + i * 2]);

        /* design matrix */
        for (j = 0; j < 4; j++)
        {
            H[j + nv * 4] = j < 3 ? -e[j] : 1.0;
        }

        nv++;
    }
    return nv;
}
/* estimate receiver velocity ------------------------------------------------*/
// static int estvel(const obsd_t* obs, int n, const double* rs, const double* dts, const prcopt_t*
// opt, sol_t* sol,
//    const double* azel, const int* vsat)
//{
//    double x[4] = { 0 }, dx[4], Q[16], * v, * H;
//    int i, j, nv;
//
//    v = mat(n, 1); H = mat(4, n);
//
//    /* doppler residuals */
//    if ((nv = resdop(obs, n, rs, dts, sol->rr, x, azel, vsat, v, H)) < 4) {
//        sol->rr[3] = sol->rr[4] = sol->rr[5] = 0.0;
//        free(v); free(H);
//        return 0;
//    }
//
//    /* least square estimation */
//    if (lsq(H, v, 4, nv, dx, Q)) {
//        sol->rr[3] = sol->rr[4] = sol->rr[5] = 0.0;
//        free(v); free(H);
//        return 0;
//    }
//    for (j = 0; j < 4; j++) x[j] = dx[j];
//    for (i = 0; i < 3; i++) sol->rr[i + 3] = x[i];
//    printf("%lf %lf %lf\n", x[0], x[1],x[2]);
//    free(v); free(H);
//    return 1;
// }
static void estvel(
    const obsd_t* obs, int n, const double* rs, const double* dts, const prcopt_t* opt, sol_t* sol,
    const double* azel, const int* vsat
)
{
    double x[4] = {0}, dx[4], Q[16], *v, *H;
    int    i, j, nv;

    // trace(3, "estvel  : n=%d\n", n);

    v = mat(n, 1);
    H = mat(4, n);

    for (i = 0; i < MAXITR; i++)
    {
        /* doppler residuals */
        if ((nv = resdop(obs, n, rs, dts, sol->rr, x, azel, vsat, v, H)) < 4)
        {
            break;
        }
        /* least square estimation */
        if (lsq(H, v, 4, nv, dx, Q))
        {
            break;
        }

        for (j = 0; j < 4; j++)
        {
            x[j] += dx[j];
        }

        if (norm(dx, 4) < 1E-6)
        {
            for (i = 0; i < 3; i++)
            {
                sol->rr[i + 3] = x[i];
            }
            break;
        }
    }
    free(v);
    free(H);
}

static void update_state(
    const prcopt_t* opt, int base, int n, obsd_t* obs, ssat_t* ssat, int* vsat, double* resp,
    double* azel_
)
{
    int           i, j, k, index;
    unsigned char sys, prn;
    double        lam[NFREQ];
    for (i = 0; i < n; i++)
    {
        sys = satsys(obs[i].sat, &prn);
        if (sys == SYS_QZS || sys == SYS_GLO)
        {
            ssat[obs[i].sat - 1].vs = -1;
            continue;
        }

        if (sys == SYS_GPS || sys == SYS_QZS)
        {
            for (j = 0; j < NFREQ; j++)
            {
                lam[j] = g_gpsLam[j];
            }
        }
        else if (sys == SYS_GLO)
        {
            for (j = 0; j < NFREQ; j++)
            {
                lam[j] = g_gloLam[prn - 1][j];
            }
        }
        else if (sys == SYS_GAL)
        {
            for (j = 0; j < NFREQ; j++)
            {
                lam[j] = g_galLam[j];
            }
        }
        else
        {
            for (j = 0; j < NFREQ; j++)
            {
                lam[j] = g_bdsLam[j];
            }
        }

        if (base == 0)
        {
            ssat[obs[i].sat - 1].azel[0][0] = azel_[i * 2];
            ssat[obs[i].sat - 1].azel[0][1] = azel_[1 + i * 2];
            index                           = obs[i].pvtAvalidPsrIndex;
            if (index == -1)
            {
                continue;
            }
            if (!vsat[i])
            {
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            if (obs[i].P[index] == 0.0 || obs[i].L[index] == 0.0)
            {
                ssat[obs[i].sat - 1].vs = -1;
            }

            for (j = 0; j < NFREQ; j++) {
                if (obs[i].SNR[j] < 33 * 4) {
                    obs[i].P[j] = obs[i].L[j] = 0;
                }
            }

            if ((ssat[obs[i].sat - 1].slip[0] & 1) || (ssat[obs[i].sat - 1].slip[0] & 2))
            {
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            if (fabs(obs[i].P[index] - obs[i].L[index] * lam[index]) > 1000)
            {
                trace(
                    0x04, "spp reject sys=%3d prn=%3d P[%d]=%14.3lf L[%d]=%14.3f P-L=%14.3f\n", sys,
                    prn, index, obs[i].P[index], index, obs[i].L[index],
                    obs[i].P[index] - obs[i].L[index] * lam[index]
                );
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            k = 0;
            for (j = 0; j < NFREQ; j++)
            {
                if (obs[i].P[j] != 0.0 && obs[i].L[j] != 0.0)
                {
                    k++;
                }
            }
            // if (k >= 2)
            //    ssat[obs[i].sat - 1].freqs = 1;
            // else
            //    ssat[obs[i].sat - 1].freqs = 0;
            if (opt->bl > BSLTHRESHOLD && k < 2)
            {
                for (j = 0; j < NFREQ; j++)
                {
                    obs[i].P[j] = obs[i].L[j] = 0.0;
                }
                ssat[obs[i].sat - 1].vs          = -1;
                ssat[obs[i].sat - 1].ionIndexCnt = 0;
                continue;
            }
            ssat[obs[i].sat - 1].vs = 1;
        }
        else
        {
            ssat[obs[i].sat - 1].azel[1][0] = azel_[i * 2];
            ssat[obs[i].sat - 1].azel[1][1] = azel_[1 + i * 2];
            if (ssat[obs[i].sat - 1].vs == -1)
            {
                continue;  // check rover is not ok
            }
            index = obs[i].pvtAvalidPsrIndex;
            if (index == -1)
            {
                continue;
            }
            if (!vsat[i])
            {
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            if (obs[i].P[index] == 0.0 || obs[i].L[index] == 0.0)
            {
                ssat[obs[i].sat - 1].vs = -1;
            }

            for (j = 0; j < NFREQ; j++) {
                if (obs[i].SNR[j] < 33 * 4) {
                    obs[i].P[j] = obs[i].L[j] = 0;
                }
            }

            if ((ssat[obs[i].sat - 1].slip[0] & 1) || (ssat[obs[i].sat - 1].slip[0] & 2))
            {
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            if (fabs(obs[i].P[index] - obs[i].L[index] * lam[index]) > 1000)
            {
                trace(
                    0x04, "spp reject sys=%3d prn=%3d P[%d]=%14.3lf L[%d]=%14.3f P-L=%14.3f\n", sys,
                    prn, index, obs[i].P[index], index, obs[i].L[index],
                    obs[i].P[index] - obs[i].L[index] * lam[index]
                );
                ssat[obs[i].sat - 1].vs = -1;
                continue;
            }
            k = 0;
            for (j = 0; j < NFREQ; j++)
            {
                if (obs[i].P[j] != 0.0 && obs[i].L[j] != 0.0)
                {
                    k++;
                }
            }
            // if (k >= 2)
            //    ssat[obs[i].sat - 1].freqs = 1;
            // else
            //    ssat[obs[i].sat - 1].freqs = 0;
            if (opt->bl > BSLTHRESHOLD && k < 2)
            {
                for (j = 0; j < NFREQ; j++)
                {
                    obs[i].P[j] = obs[i].L[j] = 0.0;
                }
                ssat[obs[i].sat - 1].vs          = -1;
                ssat[obs[i].sat - 1].ionIndexCnt = 0;
                continue;
            }
            ssat[obs[i].sat - 1].vs = 1;
        }
    }
}

extern int pntpos(
    int base, obsd_t* obs, int n, sol_t* sol, double* azel, ssat_t* ssat, const prcopt_t* opt,
    double tt
)
{
    prcopt_t      opt_        = *opt;
    int           exc[MAXSAT] = {0}, vsat[MAXOBS] = {0}, svh[MAXOBS] = {0};
    unsigned char i, j, sys, sat, prn, respMaxSat = 0, ephcnt = 0;
    int           nv = 0, stat = -1, info;
    double *      rs, *dts, *var_sat, *azel_, *resp;
    double        lsqraim[3]     = {0};
    double        respMax        = 0;
    unsigned char robust[MAXSAT] = {0};

    SysCnt = 0;

    if (n <= 0)
    {
        trace(0x01, "no obs data\n");
        return 1;
    }
    sol->time = obs[0].time;
    // sol->time_pre = obs[0].time;
    rs      = mat(6, n);
    dts     = mat(2, n);
    var_sat = mat(1, n);
    azel_   = zeros(2, n);
    resp    = mat(1, n);

    if ((!rs) || (!dts) || (!var_sat) || (!azel_) || (!resp))
    {
        trace(0x01, "pntpos mat allocate error\n");
        free(rs);
        free(dts);
        free(var_sat);
        free(azel_);
        free(resp);
        return 1;
    }
    opt_.sateph = EPHOPT_BRDC;    // #define EPHOPT_SBAS 2                   /* ephemeris option:
                                  // broadcast + SBAS */
    opt_.ionoopt = IONOOPT_BRDC;  // #define IONOOPT_SBAS 2                  /* ionosphere option:
                                  // SBAS model */
    opt_.tropopt = TROPOPT_SAAS;  // #define TROPOPT_SBAS 2                  /* troposphere option:
                                  // SBAS model */

    satposs(sol->time, obs, n, opt_.sateph, rs, dts, var_sat, svh);

    // save rs for azel cal
    for (i = 0; i < n; i++)
    {
        if (base == 0)
        {
            ssat[obs[i].sat - 1].rs[0] = rs[i * 6];
            ssat[obs[i].sat - 1].rs[1] = rs[i * 6 + 1];
            ssat[obs[i].sat - 1].rs[2] = rs[i * 6 + 2];
        }
    }

    for (i = 0; i < n; i++)
    {
        if (rs[i * 6] == 0)
        {
            exc[obs[i].sat - 1] = 1;
        }
        else
        {
            ephcnt++;
        }
    }

    trace(2, "n:%d ephcnt:%d\n", n, ephcnt);

    if (2 * ephcnt < 1 * n)
    {
        trace(2, "pntpos eph sat too less, ephcnt,%d,n,%d\n", ephcnt, n);
        free(rs);
        free(dts);
        free(var_sat);
        free(azel_);
        free(resp);
        return -1;
    }
    while (1)
    {
        stat =
            estpos(obs, n, svh, rs, dts, resp, var_sat, azel_, vsat, exc, lsqraim, &opt_, sol, tt);
        // printf("-----------------------------------\n");
        if (stat == -2 && (sol->ns[0] > (5 + SysCnt)) && lsqraim[0] >= 1)
        {
            sat          = (int)lsqraim[0];
            exc[sat - 1] = 1;
            sys          = satsys(sat, &prn);
            trace(
                0x04, "rcv:%d spp reject sys=%d prn=%d v=%lf stdv=%lf\n", base + 1, sys, prn,
                lsqraim[1], lsqraim[2]
            );
            for (i = 0; i < 3; i++)
            {
                lsqraim[i] = 0.0;
            }
        }
        else if (stat == 0 && (sol->ns[0] > (5 + SysCnt)))
        {
            for (i = 0; i < n; i++)
            {
                if (fabs(resp[i]) > fabs(respMax))
                {
                    respMax    = resp[i];
                    respMaxSat = obs[i].sat;
                }
            }
            if (fabs(respMax) > 30.0 && respMaxSat >= 1)
            {
                exc[respMaxSat - 1] = 1;
                sys                 = satsys(respMaxSat, &prn);
                trace(0x04, "spp resp reject sys=%d prn=%d respMax=%lf\n", sys, prn, fabs(respMax));
                respMax    = 0;
                respMaxSat = 0;
                for (i = 0; i < 3; i++)
                {
                    lsqraim[i] = 0.0;
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
    if (stat == 0 && ssat)
    {
        update_state(opt, base, n, obs, ssat, vsat, resp, azel_);
    }
    /* estimate receiver velocity with doppler */
    if (!stat)
    {
        estvel(obs, n, rs, dts, &opt_, sol, azel_, vsat);
    }
    free(rs);
    free(dts);
    free(var_sat);
    free(azel_);
    free(resp);
    return stat;
}
