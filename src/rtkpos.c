#include "rtk.h"
#define VAR_POS SQR(30.0) /* initial variance of receiver pos (m^2) */
#define VAR_VEL SQR(10.0)
#define VAR_ACC SQR(10.0)
#define MAX_ION_STD 0.5
#define VAR_GRA SQR(0.001) /* initial variance of gradient (m^2) */
#define SQRT(x) ((x) < 0.0 ? 0.0 : sqrt(x))
static unsigned char ilterCout;
static double        stdvMax;
static double        K0, K1;

static double       baseXyz[3];
static unsigned int smoothCnt = 0;
extern double       baseRtcmPosition[3];

static unsigned char halfSlip;
static unsigned char halfSlipCnt;
static unsigned char fixErrorCnt  = 0;
static unsigned char largeShift   = 0;
static unsigned char rejectcount  = 0;
static unsigned char baseXyzError = 0;
static double        varerr_gamit(
           int sat, int sys, double el, double bl, double dt, int f, const prcopt_t* opt
       )
{
    double a, b, c = 0 * bl / 1E4, d = CLIGHT * 0 * dt;
    double sinel;
    a = 0.003;
    b = 0.003;
    // SNR = isnr[sat - 1][f / nf] / 4;
    // if (el * R2D > 45) {
    //     if (SNR < 40) el = 15 * D2R;
    // }
    sinel = sin(el);
    return 2.0 * (a * a + b * b / sinel / sinel + c * c) + d * d;
}
/* initialize state and covariance -------------------------------------------*/
static void initx(rtk_t* rtk, double xi, double var, int i)
{
    int j;
    rtk->x[i] = xi;
    for (j = 0; j < rtk->nx; j++)
    {
        rtk->P[i + j * rtk->nx] = rtk->P[j + i * rtk->nx] = ((i == j) ? var : 0.0);
    }
}
static void initxp(rtk_t* rtk, double xi, double var, int i)
{
    int j;
    rtk->xp[i] = xi;
    for (j = 0; j < rtk->nx; j++)
    {
        rtk->Pp[i + j * rtk->nx] = rtk->Pp[j + i * rtk->nx] = ((i == j) ? var : 0.0);
    }
}
static void getWavelength(unsigned char sys, unsigned char prn, double* lam)
{
    int k = 0;
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
}
extern double baseline(const double* ru, const double* rb, double* dr)
{
    int i;
    for (i = 0; i < 3; i++)
    {
        dr[i] = ru[i] - rb[i];
    }
    return norm(dr, 3);
}
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
static void setbitu(unsigned char* buff, int pos, int len, unsigned int data)
{
    unsigned int mask = 1u << (len - 1);
    int          i;
    if (len <= 0 || 32 < len)
    {
        return;
    }
    for (i = pos; i < pos + len; i++, mask >>= 1)
    {
        if (data & mask)
        {
            buff[i / 8] |= 1u << (7 - i % 8);
        }
        else
        {
            buff[i / 8] &= ~(1u << (7 - i % 8));
        }
    }
}
static void detslp_ll(rtk_t* rtk, const obsd_t* obs, int i, int rcv)
{
    int           f, slip, LLI;
    unsigned char sat = obs[i].sat;
    for (f = 0; f < rtk->opt.nf; f++)
    {
        // if (obs[i].L[f] == 0.0 ||fabs(timediff(obs[i].time, rtk->ssat[sat -
        // 1].pt[rcv - 1][f])) < DTTOL) {
        if (obs[i].L[f] == 0.0 || rtk->tt < DTTOL)
        {
            continue;
        }
        /* restore previous LLI */
        if (rcv == 1)
        {
            LLI = getbitu(&rtk->ssat[sat - 1].slip[f], 0, 2); /* rover */
        }
        else
        {
            LLI = getbitu(&rtk->ssat[sat - 1].slip[f], 2, 2); /* base  */
        }
        /* detect slip by cycle slip flag in LLI */
        if (obs[i].LLI[f] & 1)
        {
            trace(
                0x08, "slip detected forward  (sat=%2d rcv=%d F=%d LLI=%x)\n", sat, rcv, f + 1,
                obs[i].LLI[f]
            );
            // printf("slip detected forward  (sat=%2d rcv=%d F=%d LLI=%x)\n", sat,
            // rcv, f + 1, obs[i].LLI[f]);
        }

        slip = obs[i].LLI[f];

        /* detect slip by parity unknown flag transition in LLI */
        if (((LLI & 2) && !(obs[i].LLI[f] & 2)) || (!(LLI & 2) && (obs[i].LLI[f] & 2)))
        {
            trace(
                0x08, "slip detected half-cyc (sat=%2d rcv=%d F=%d LLI=%x->%x)\n", sat, rcv, f + 1,
                LLI, obs[i].LLI[f]
            );
            // printf("slip detected half-cyc (sat=%2d rcv=%d F=%d LLI=%x->%x)\n",
            // sat, rcv, f + 1, LLI, obs[i].LLI[f]);
            slip |= 1;
        }

        /* save current LLI */
        if (rcv == 1)
        {
            setbitu(&rtk->ssat[sat - 1].slip[f], 0, 2, obs[i].LLI[f]);
        }
        else
        {
            setbitu(&rtk->ssat[sat - 1].slip[f], 2, 2, obs[i].LLI[f]);
        }

        /* save slip and half-cycle valid flag */

        rtk->ssat[sat - 1].slip[f] |= (unsigned char)slip;
        rtk->ssat[sat - 1].half[f] = (obs[i].LLI[f] & 2) ? 0 : 1;
    }
}
static double sdobs(const obsd_t* obs, int i, int j, int f)
{
    double pi = f < NFREQ ? obs[i].L[f] : obs[i].P[f - NFREQ];
    double pj = f < NFREQ ? obs[j].L[f] : obs[j].P[f - NFREQ];
    return pi == 0.0 || pj == 0.0 ? 0.0 : (pi - pj + 0.000001);
}
/* single-differenced geometry-free linear combination of phase --------------*/
static double gfobs(const obsd_t* obs, int index, int i, int j, const double* lam)
{
    double pi = sdobs(obs, i, j, 0) * lam[0], pj = sdobs(obs, i, j, index) * lam[index];
    return pi == 0.0 || pj == 0.0 ? 0.0 : pi - pj;
}

/* temporal update of position/velocity/acceleration -------------------------*/
static void udpos(rtk_t* rtk, double tt)
{
    int     i, j, nx, *ix;
    double *F, *P, *FP, *x, *xp, pos[3], Q[9 * 9] = {0}, Qv[9], var = 0.0, xcc;
    /* initialize position for first epoch */

    if (rtk->sol.fixxyz[0] != 0.0 && rtk->sol.rr_smooth_cnt > 10)
    {
        var = SQR(0.1);
    }
    else
    {
        var = 900;
    }
    if (rtk->opt.mode == PMODE_DGPS)
    {
        for (i = 0; i < 3; i++)
        {
            rtk->x[i] = rtk->sol.rr[i];
            for (j = 0; j < 3; j++)
            {
                rtk->P[i + j * 3] = rtk->P[j + i * 3] = ((i == j) ? VAR_POS : 0.0);
            }
        }
        return;
    }

    if ((norm(rtk->x, 3) <= 0.0))
    {
        for (i = 0; i < 3; i++)
        {
            initx(rtk, rtk->sol.rr[i], VAR_POS, i);
        }
        if (rtk->opt.dynamics == 1)
        {
            for (i = 3; i < 6; i++)
            {
                initx(rtk, rtk->sol.rr[i], VAR_VEL, i);  // initx(rtk, 1E-3, VAR_VEL, i);
            }
        }
        if (rtk->opt.dynamics == 2)
        {
            for (i = 3; i < 6; i++)
            {
                initx(rtk, rtk->sol.rr[i], VAR_VEL, i);
            }
            for (i = 6; i < 9; i++)
            {
                initx(rtk, 1E-6, VAR_ACC, i);
            }
        }
    }
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            if (i == j)
            {
                if (i < 2)
                {
                    if (rtk->P[i + j * rtk->nx] < SQR(0.001))
                    {
                        rtk->P[i + j * rtk->nx] = SQR(0.00003);
                    }
                }
                else
                {
                    if (rtk->P[i + j * rtk->nx] < SQR(0.001))
                    {
                        rtk->P[i + j * rtk->nx] = SQR(0.00008);
                    }
                }
            }
            else
            {
                rtk->P[i + j * rtk->nx] = 0.0;
            }
        }
    }

    /* static mode */
    if (rtk->opt.mode == PMODE_STATIC)
    {
        return;
    }
    /* kinmatic mode without dynamics */
    // if (rtk->sol.rr_smooth[0] != 0 && !rtk->opt.dynamics) {
    //     for (i = 0; i < 3; i++) initx(rtk, rtk->sol.rr[i], 0.2, i);
    //     return;
    // }
    if (!rtk->opt.dynamics)
    {
        /*if (norm(rtk->xp, 3) > 0.0)
            for (i = 0; i < 3; i++)  rtk->sol.rr[i] = rtk->xp[i];*/
        for (i = 0; i < 3; i++)
        {
            initx(rtk, rtk->sol.rr[i], var, i);
        }
        return;
    }
    /* check variance of estimated postion */
    for (i = 0; i < 3; i++)
    {
        var += rtk->P[i + i * rtk->nx];
    }
    var /= 3.0;
    if (var > VAR_POS)
    {
        /* reset position with large variance */
        for (i = 0; i < 3; i++)
        {
            initx(rtk, rtk->sol.rr[i], VAR_POS, i);
        }
        for (i = 3; i < 6; i++)
        {
            initx(rtk, rtk->sol.rr[i], VAR_VEL, i);
        }
        if (rtk->opt.dynamics == 2)
        {
            for (i = 6; i < 9; i++)
            {
                initx(rtk, 1E-6, VAR_ACC, i);
            }
        }
        return;
    }
    /* generate valid state index */
    ix = imat(rtk->nx, 1);
    nx = 0;
    if (rtk->opt.dynamics == 1)
    {
        for (i = 0; i < 6; i++)
        {
            ix[nx++] = i;
        }
        for (i = nx = 6; i < rtk->nx; i++)
        {
            if (rtk->x[i] != 0.0 && rtk->P[i + i * rtk->nx] > 0.0)
            {
                ix[nx++] = i;
            }
        }
    }
    if (rtk->opt.dynamics == 2)
    {
        for (i = 0; i < 9; i++)
        {
            ix[nx++] = i;
        }
        for (i = nx = 9; i < rtk->nx; i++)
        {
            if (rtk->x[i] != 0.0 && rtk->P[i + i * rtk->nx] > 0.0)
            {
                ix[nx++] = i;
            }
        }
    }
    /* state transition of position/velocity/acceleration */
    F  = eye(nx);
    P  = mat(nx, nx);
    FP = mat(nx, nx);
    x  = mat(nx, 1);
    xp = mat(nx, 1);
    if (rtk->opt.dynamics == 1)
    {
        for (i = 0; i < 3; i++)
        {
            F[i + (i + 3) * nx] = tt;
        }
    }
    if (rtk->opt.dynamics == 2)
    {
        for (i = 0; i < 6; i++)
        {
            F[i + (i + 3) * nx] = tt;
        }
        for (i = 0; i < 3; i++)
        {
            F[i + (i + 6) * nx] = SQR(tt) / 2.0;
        }
    }
    for (i = 0; i < nx; i++)
    {
        x[i] = rtk->x[ix[i]];
        for (j = 0; j < nx; j++)
        {
            P[i + j * nx] = rtk->P[ix[i] + ix[j] * rtk->nx];
        }
    }
    /* x=F*x, P=F*P*F+Q */
    matmul("NN", nx, 1, nx, 1.0, F, x, 0.0, xp);
    matmul("NN", nx, nx, nx, 1.0, F, P, 0.0, FP);
    matmul("NT", nx, nx, nx, 1.0, FP, F, 0.0, P);
    for (i = 0; i < nx; i++)
    {
        rtk->x[ix[i]] = xp[i];
        for (j = 0; j < nx; j++)
        {
            rtk->P[ix[i] + ix[j] * rtk->nx] = P[i + j * nx];
        }
    }
    /* process noise added to only vel */
    if (rtk->opt.dynamics == 1)
    {
        // Q[0]=Q[4]=Q[8]=SQR(rtk->opt.prn[3])*fabs(tt);
        Q[0] = Q[4] = Q[8] = SQR(0.1) * fabs(tt);
        Q[8]               = SQR(0.2) * fabs(tt);
        ecef2pos(rtk->x, pos);
        covecef(pos, Q, Qv);
        for (i = 0; i < 3; i++)
        {
            for (j = 0; j < 3; j++)
            {
                rtk->P[i + 3 + (j + 3) * rtk->nx] += Qv[i + j * 3];
            }
        }
    }
    if (rtk->opt.dynamics == 2)
    {
        for (i = 0; i < 3; i++)
        {
            for (j = 0; j < 3; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i == j) ? tt * tt * tt * tt / 20 : 0.0);
            }
        }
        for (i = 3; i < 6; i++)
        {
            for (j = 3; j < 6; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i == j) ? tt * tt / 3 : 0.0);
            }
        }
        for (i = 6; i < 9; i++)
        {
            for (j = 6; j < 9; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i == j) ? 1.0 : 0.0);
            }
        }
        for (i = 3; i < 6; i++)
        {
            for (j = 0; j < 3; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i - 3 == j) ? tt * tt * tt / 8 : 0.0);
            }
        }
        for (i = 6; i < 9; i++)
        {
            for (j = 0; j < 3; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i - 6 == j) ? tt * tt / 6 : 0.0);
            }
        }
        for (i = 6; i < 9; i++)
        {
            for (j = 3; j < 6; j++)
            {
                Q[i + j * 9] = Q[j + i * 9] = ((i - 3 == j) ? tt / 2 : 0.0);
            }
        }
        xcc = norm(rtk->x + 6, 3);
        // sig = (4 - PI) / PI * (10 - xcc) * (10 - xcc) * 2 * tt / 100;
        for (i = 0; i < 9; i++)
        {
            for (j = 0; j < 9; j++)
            {
                Q[i + j * 9] = Q[i + j * 9] * SQR(0.1);
            }
        }
        for (i = 0; i < 9; i++)
        {
            for (j = 0; j < 9; j++)
            {
                rtk->P[i + j * rtk->nx] += Q[i + j * 9];
            }
        }
    }

    free(ix);
    free(F);
    free(P);
    free(FP);
    free(x);
    free(xp);
}

static void udion(
    rtk_t* rtk, const obsd_t* obs, const unsigned char* iu, const unsigned char* ir, double tt,
    double bl, const unsigned char* sat, unsigned char ns
)
{
    gtime_t time;
    double  el, fact, ep[6];
    int     i, j = rtk->np + rtk->nt;
    time = rtk->sol.time;
    time.time += 8 * 3600;
    time = gpst2utc(time);
    time2epoch(rtk->sol.time, ep);

    if (0)
    {
        for (i = 0; i < ns; i++)
        {
            trace(0x10, "ini ion:sat:%3d %f\n", sat[i], rtk->opt.std);
            initx(rtk, 1E-6, SQR(0.2), j);
            // initx(rtk, 1E-6, SQR(0.03), j);
            // initx(rtk, 1E-6, SQR(rtk->opt.std * bl / 1E4), j);
            j++;
        }
    }
    else
    {
        for (i = 0; i < ns; i++)
        {
            // if (rtk->x[j] == 0.0 || rtk->sol.stat == 0 || (rtk->sol.stat != 1 &&
            // rtk->sol.stat != 2) || rtk->sol.ratio < 1.3) { if (rtk->x[j] == 0.0 ||
            // rtk->sol.ratio < 1.3 || (rtk->sol.stat != 1 && rtk->sol.stat != 2)) {
            if (rtk->x[j] == 0.0 ||
                (fabs(timediff(rtk->ssat[sat[i] - 1].ddionTime, rtk->sol.time)) > 60))
            {
                // if (rtk->x[j] == 0.0) {
                rtk->ssat[sat[i] - 1].ionIndexCnt = 0;
                trace(0x10, "ini ion:sat:%3d %f\n", sat[i], rtk->opt.std);
                // initx(rtk, 1E-6, SQR(0.2), j);
                initx(rtk, 1E-6, SQR(rtk->opt.std), j);
                j++;
            }
            else
            {
                el   = rtk->ssat[sat[i] - 1].azel[0][1];
                fact = cos(el);
                // if(tt> rtk->sol.age)
                // if(ep[3]>11 && ep[3] > 20)
                rtk->P[j + j * rtk->nx] += SQR(0.005 * fact) * rtk->opt.timeInterval;
                // else
                //     rtk->P[j + j * rtk->nx] += SQR(0.0001 * fact);
                // else
                //     rtk->P[j + j * rtk->nx] += SQR(0.0001 * fact) *
                //     fabs(rtk->sol.age);
                j++;
            }
        }
    }
}

static void udtrop(rtk_t* rtk, double tt, double bl)
{
    int i, j;
    if (0)
    {
        for (i = 0; i < 2; i++)
        {
            j = rtk->np + i;
            // if (rtk->x[j] == 0.0)
            initx(rtk, 0.15, SQR(0.001), j); /* initial zwd */
                                             // else
                                             //     rtk->P[j + j * rtk->nx] += SQR(0.001) * tt;
        }
    }
    else
    {
        for (i = 0; i < 2; i++)
        {
            j = rtk->np + i;
            // if (rtk->x[j] == 0.0||rtk->sol.ratio<1.3) {
            if (rtk->x[j] == 0.0)
            {
                initx(rtk, 0.15, SQR(0.001), j); /* initial zwd */
            }
            else
            {
                // if(tt>rtk->sol.age)
                rtk->P[j + j * rtk->nx] += SQR(0.0001) * rtk->opt.timeInterval;
                // else
                //     rtk->P[j + j * rtk->nx] += SQR(0.00001) * rtk->sol.age;
            }
        }
    }
}

#define MAXACC 10.0 /* max accel for doppler slip detection (m/s^2) */
/* detect cycle slip by doppler and phase difference -------------------------*/
static void detslp_dop(rtk_t* rtk, const obsd_t* obs, int i, int rcv)
{
    /* detection with doppler disabled because of clock-jump issue (v.2.3.0) */
    unsigned char k, f, sys, prn, sat = obs[i].sat;
    double        tt = rtk->tt, dph, dpt, lam[NFREQ], thres;

    sys = satsys(sat, &prn);
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

    for (f = 0; f < rtk->opt.nf; f++)
    {
        if (obs[i].L[f] == 0.0 || obs[i].D[f] == 0.0 ||
            rtk->ssat[obs[i].sat - 1].ph[rcv - 1][f] == 0.0)
        {
            continue;
        }
        if (rtk->tt < DTTOL)
        {
            continue;
        }
        if ((lam[f]) <= 0.0)
        {
            continue;
        }

        /* cycle slip threshold (cycle) */
        thres = MAXACC * tt * tt / 2.0 / lam[f] + 1.0 * fabs(tt) * 2.0;

        /* phase difference and doppler x time (cycle) */
        dph = obs[i].L[f] - rtk->ssat[obs[i].sat - 1].ph[rcv - 1][f];
        dpt = -obs[i].D[f] * tt;

        if (fabs(dph - dpt) <= thres)
        {
            continue;
        }

        rtk->ssat[sat - 1].slip[f] |= 1;

        trace(
            0x08, "detslp_dop detected (sat=%2d rcv=%d L%d=%.3f %.3f thres=%.3f)\n", sat, rcv,
            f + 1, dph, dpt, thres
        );
    }
}

static void udbias(
    rtk_t* rtk, double tt, const obsd_t* obs, const unsigned char* sat, const unsigned char* iu,
    const unsigned char* ir, unsigned char ns, double* r
)
{
    double        cp, pr, bias, lam = 0.0, var, offset[NFREQ] = {0};
    int           i, f, nx2, slip, nf = rtk->opt.nf, offsetCnt[NFREQ] = {0};
    int           nsat[NSYS][NFREQ], nslip[NSYS][NFREQ];
    unsigned char sys, prn;
    for (i = 0; i < NSYS; i++)
    {
        for (f = 0; f < NFREQ; f++)
        {
            nsat[i][f]  = 0;
            nslip[i][f] = 0;
        }
    }
    nx2 = rtk->np + rtk->nt + rtk->ni;
    for (f = 0; f < nf; f++)
    {
        /* estimate approximate phase-bias by phase - code */
        offsetCnt[f] = 0.0;
        for (i = 0; i < ns; i++)
        {
            sys = satsys(sat[i], &prn);
            if (sys == SYS_GPS || sys == SYS_QZS)
            {
                lam = g_gpsLam[f];
            }
            else if (sys == SYS_GLO)
            {
                lam = g_gloLam[prn - 1][f];
            }
            else if (sys == SYS_GAL)
            {
                lam = g_galLam[f];
            }
            else if (sys == SYS_BDS)
            {
                lam = g_bdsLam[f];
            }
            else
            {
                lam = g_gpsLam[f];
            }
            cp = sdobs(obs, iu[i], ir[i], f); /* cycle */
            pr = sdobs(obs, iu[i], ir[i], f + NFREQ);
            if (cp == 0.0 || pr == 0.0 || lam <= 0.0)
            {
                continue;
            }

            slip = rtk->ssat[sat[i] - 1].slip[f];
            sys  = satsys(sat[i], NULL);
            if (sys == SYS_GPS || sys == SYS_QZS)
            {
                nsat[0][f]++;
            }
            else if (sys == SYS_GLO)
            {
                nsat[1][f]++;
            }
            else if (sys == SYS_GAL)
            {
                nsat[2][f]++;
            }
            else if (sys == SYS_BDS)
            {
                nsat[3][f]++;
            }
            if (slip & 1)
            {
                rtk->x[nx2] = 0.0;
                if (sys == SYS_GPS || sys == SYS_QZS)
                {
                    nslip[0][f]++;
                }
                else if (sys == SYS_GLO)
                {
                    nslip[1][f]++;
                }
                else if (sys == SYS_GAL)
                {
                    nslip[2][f]++;
                }
                else if (sys == SYS_BDS)
                {
                    nslip[3][f]++;
                }
            }
            if (baseXyzError == 0)
            {
                pr = r[iu[i]] - r[ir[i]];
            }
            bias                           = cp - pr / lam;
            rtk->ssat[sat[i] - 1].fbias[f] = bias;
            if (rtk->x[nx2] != 0.0)
            {
                offset[f] += bias - rtk->x[nx2];
                offsetCnt[f]++;
            }

            nx2++;
        }
    }
    nx2 = rtk->np + rtk->nt + rtk->ni;
    for (f = 0; f < nf; f++)
    {
        for (i = 0; i < ns; i++)
        {
            sys = satsys(sat[i], &prn);
            if (sys == SYS_GPS || sys == SYS_QZS)
            {
                lam = g_gpsLam[f];
            }
            else if (sys == SYS_GLO)
            {
                lam = g_gloLam[prn - 1][f];
            }
            else if (sys == SYS_GAL)
            {
                lam = g_galLam[f];
            }
            else if (sys == SYS_BDS)
            {
                lam = g_bdsLam[f];
            }
            else
            {
                lam = g_gpsLam[f];
            }

            cp = sdobs(obs, iu[i], ir[i], f); /* cycle */
            pr = sdobs(obs, iu[i], ir[i], f + NFREQ);
            if (cp == 0.0 || pr == 0.0 || lam <= 0.0)
            {
                continue;
            }

            slip = rtk->ssat[sat[i] - 1].slip[f];
            sys  = satsys(sat[i], NULL);
            if (baseXyzError == 0)
            {
                pr = r[iu[i]] - r[ir[i]];
            }
            bias = cp - pr / lam;
            if (rtk->opt.ionoopt == IONOOPT_EST)
            {
                if (rtk->x[nx2] == 0.0)
                {
                    var = 900;
                    initx(rtk, bias, var, nx2);
                    trace(
                        4,
                        "ini bias:sat:sys=%3d prn=%3d f:%2d x=%7.2f stat=%2d "
                        "ratio=%7.2f value:%7.2f el=%7.2f\n",
                        sys, prn, f, rtk->x[nx2], rtk->sol.stat, rtk->sol.ratio, bias,
                        rtk->ssat[sat[i] - 1].azel[0][1] * R2D
                    );
                    if (f == 0)
                    {
                        rtk->ssat[sat[i] - 1].ionIndexCnt    = 0;
                        rtk->ssat[sat[i] - 1].ddionTime.time = 0;
                    }
                }
                else
                {
                    if (offsetCnt[f] > 0)
                    {
                        rtk->x[nx2] += offset[f] / offsetCnt[f];
                    }
                    if (f == 0)
                    {
                        rtk->ssat[sat[i] - 1].ionIndexCnt++;
                        rtk->ssat[sat[i] - 1].ddionTime = rtk->sol.time;
                    }
                }
            }
            else
            {
                if (rtk->x[nx2] == 0.0 || rtk->sol.stat == 0 ||
                    (rtk->sol.stat != 1 && rtk->sol.stat != 2 && rtk->sol.stat != 4) ||
                    rtk->sol.ratio < 1.3)
                {
                    var = 900;
                    initx(rtk, bias, var, nx2);
                    trace(
                        4,
                        "ini bias:sat:sys=%3d prn=%3d f:%2d x=%7.2f stat=%2d "
                        "ratio=%7.2f value:%7.2f el=%7.2f\n",
                        sys, prn, f, rtk->x[nx2], rtk->sol.stat, rtk->sol.ratio, bias,
                        rtk->ssat[sat[i] - 1].azel[0][1] * R2D
                    );
                }
                else
                {
                    if (offsetCnt[f] > 0)
                    {
                        rtk->x[nx2] += offset[f] / offsetCnt[f];
                    }
                }
            }
            nx2++;
        }
    }
    // printf("ratio=%.2f\n", rtk->sol.ratio);
    for (i = 0; i < NSYS; i++)
    {
        for (f = 0; f < nf; f++)
        {
            // if (nslip[i][f] >= nsat[i][f] / 2) {
            if (nslip[i][f] == nsat[i][f] && nslip[i][f] != 0)
            {
                rtk->allSlipFlag[i][f] = 1;
                trace(4, "nslip[%d][%d]=%d,nsat[%d][%d]=%d\n", i, f, nslip[i][f], i, f, nsat[i][f]);
            }
        }
    }
}

/* temporal update of states
 * --------------------------------------------------*/
static void udstate(
    rtk_t* rtk, const obsd_t* obs, const unsigned char* sat, const unsigned char* iu,
    const unsigned char* ir, unsigned char ns, double* r
)
{
    double tt = rtk->tt, bl, dr[3];

    /* temporal update of position/velocity/acceleration */
    udpos(rtk, tt);

    bl = baseline(rtk->x, rtk->rb, dr);
#if 0
    if (rtk->nfloat > 3 && rtk->opt.ionoopt != IONOOPT_EST) {
        rtk->sol.stat = 0;
        rtk->opt.std += 0.01;
        rtk->nfloat = 0;
        if (rtk->opt.std >= MAX_ION_STD) rtk->opt.std = 0.01;
    }
#endif
    /* temporal update of tropospheric parameters */
    if (rtk->opt.tropopt == TROPOPT_EST)
    {
        udtrop(rtk, tt, bl);
    }
    /* temporal update of ionospheric parameters */
    if (rtk->opt.ionoopt == IONOOPT_EST)
    {
        udion(rtk, obs, iu, ir, tt, bl, sat, ns);
    }
    if (rtk->opt.mode > PMODE_DGPS)
    {
        udbias(rtk, tt, obs, sat, iu, ir, ns, r);
    }
}

static void ddcov(const int* nb, int n, const double* Ri, const double* Rj, int nv, double* R)
{
    int i, j, k = 0, b;

    for (i = 0; i < nv * nv; i++)
    {
        R[i] = 0.0;
    }
    for (b = 0; b < n; k += nb[b++])
    {
        for (i = 0; i < nb[b]; i++)
        {
            for (j = 0; j < nb[b]; j++)
            {
                R[k + i + (k + j) * nv] = Ri[k + i] + (i == j ? Rj[k + i] : 0.0);
            }
        }
    }
}

/* single-differenced measurement error variance -----------------------------*/
extern double varrL(const obsd_t* obs, double el, double bl, int f, const prcopt_t* opt)
{
    double a, b, c = 0 * bl / 1E4;
    double sinel, var;
    a = 0.003;
    b = 0.003;

    sinel = sin(el);
    var   = 2.0 * (a * a + b * b / sinel / sinel + c * c);
    // if (obs->LockTime[f] < 3000)
    //     var = var + (3000 - obs->LockTime[f])/16000.0;

    // double snr = obs->SNR[f] / 4.0 ;
    // double minSNR=30.0, maxSNR=50.0;
    // double normSNR = (snr-minSNR)/(maxSNR-minSNR);

    // var *= (-6.2*normSNR+7.2);
    // if (snr > 45)
    //     var *= 1.0;
    // else if (snr > 40)
    //     var *= 1.0;
    // else if (snr > 35)
    //     var *= 5.0;
    // else if (snr > 30)
    //     var *= 10.0;
    // else if (snr > 10)
    //     var *= 100.0;
    // else
    //     var *= 100.0;

    // printf("var, %10.8f, snr, %.2f\n", var,snr);
    // if (obs->LockTime[f] < 1000) {
    //     var *= 2.0;
    // }

    // if(isIGSO(obs->sat)==1){
    //     var *= 1.0;
    // }else if(isMEO(obs->sat)==1){
    //     var *= 1.0;
    // }else if(isGEO(obs->sat)==1){
    //     var *= 10.0;
    // }
    // return 2.0 * (a * a + b * b / sinel / sinel + c * c);
    return var;
}

/* UD (undifferenced) phase/code residual for satellite ----------------------*/
static void zdres_sat(
    int base, double r, const obsd_t* obs, const double* azel, const prcopt_t* opt, double* y
)
{
    int           i, k;
    unsigned char sys, prn;
    int           nf = opt->nf;
    double        lam[NFREQ];
    sys = satsys(obs->sat, &prn);
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

    for (i = 0; i < nf; i++)
    {
        if (lam[i] == 0.0)
        {
            continue;
        }
        if (obs->L[i] == 0.0 && obs->P[i] == 0.0)
        {
            continue;
        }
        if (obs->L[i] != 0.0)
        {
            y[i] = obs->L[i] * lam[i] - r;
        }
        if (obs->P[i] != 0.0)
        {
            y[i + nf] = obs->P[i] - r;
        }
    }
}
/* test navi system (m=0:gps/qzs/sbs,1:bds) ----------------------*/
extern int test_sys(int sys, int m)
{
    switch (sys)
    {
        case SYS_GPS:
            return m == 0;
        case SYS_GLO:
            return m == 1;
        case SYS_GAL:
            return m == 2;
        case SYS_BDS:
            return m == 3;
        case SYS_QZS:
            return m == 0;
    }
    return 0;
}
static int validobsP(int i, int j, int f, int nf, double* y)
{
    /* if no phase observable, psudorange is also unusable */
    return y[f + i * nf * 2] != 0.0 && y[f + j * nf * 2] != 0.0;
}
/* test valid observation data -----------------------------------------------*/
static int validobsLP(int i, int j, int f, int nf, double* y)
{
    /* if no phase observable, psudorange is also unusable */
    return y[f + i * nf * 2] != 0.0 && y[f + j * nf * 2] != 0.0 &&
           (f < nf || (y[f - nf + i * nf * 2] != 0.0 && y[f - nf + j * nf * 2] != 0.0));
}
/* UD (undifferenced) phase/code residuals -----------------------------------*/
static int zdres(
    rtk_t* rtk, int base, const obsd_t* obs, int n, const double* rs, const double* dts,
    const int* svh, const double* rr, const prcopt_t* opt, int index, double* y, double* e,
    double* azel, double* rdist
)
{
    double r, rr_[3], pos[3], disp[3];
    double zhd, zazel[] = {0.0, 90.0 * D2R};
    int    i, sat, nf = rtk->opt.nf;
    for (i = 0; i < 2 * nf * n; i++)
    {
        y[i] = 0.0;
    }

    if (sos3(rr) <= 0.0)
    {
        return -1;  // no receiver pos
    }
    for (i = 0; i < 3; i++)
    {
        rr_[i] = rr[i];
    }

    if (opt->tidecorr)
    {
        tidedisp(gpst2utc(obs[0].time), rr_, opt->tidecorr, &g_nav.erp, opt->odisp[base], disp);
        for (i = 0; i < 3; i++)
        {
            rr_[i] += disp[i];
        }
    }
    ecef2pos(rr_, pos);
    for (i = 0; i < n; i++)
    {
        sat = obs[i].sat;
        r   = geodist(rs + i * 6, rr_, e + i * 3);
        satazel(pos, e + i * 3, azel + i * 2);
        rtk->ssat[sat - 1].azel[base][0] = azel[2 * i];
        rtk->ssat[sat - 1].azel[base][1] = azel[2 * i + 1];

        // satellite clock-bias
        r -= CLIGHT * dts[i * 2];

        /* troposphere delay model (hydrostatic) */
        if (rtk->opt.mode != 4)
        {
            zhd = tropmodel(obs[0].time, pos, zazel, 0.0);
            r += tropmapf(obs[i].time, pos, azel + i * 2, NULL) * zhd;
        }
        rdist[i]                      = r;
        rtk->ssat[sat - 1].dist[base] = r;

        /* undifferenced phase/code residual for satellite */
        zdres_sat(base, r, obs + i, azel + i * 2, opt, y + i * nf * 2);
    }
    return 0;
}

static int ddmat(rtk_t* rtk, double* D, int* fixSatNum)
{
    int           i, j, k, m, f, nb = 0, na = rtk->np + rtk->nt + rtk->ni, nf = rtk->opt.nf;
    unsigned char sati, frqi, sys;
    unsigned char satFlag[MAXSAT] = {0};
    unsigned char satFix[MAXSAT]  = {0};
    int           flag, ns = 0;
    for (i = 0; i < MAXSAT; i++)
    {
        for (j = 0; j < NFREQ; j++)
        {
            rtk->ssat[i].fix[j] = 0;
        }
    }
    for (i = 0; i < na; i++)
    {
        D[i + i * rtk->nx] = 1.0;
    }
    for (f = 0, k = na; f < nf; f++)
    {
        for (m = 0; m < NSYS; m++)
        {
            flag = 0;
            for (i = k; i < k + rtk->nx; i++)
            {
                sati = rtk->nxRecordSat[i - k];
                frqi = rtk->nxRecordFrq[i - k];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (sati != rtk->base_prn[m][f])
                {
                    continue;
                }
                rtk->ssat[sati - 1].fix[f] = 2; /* fix */
                satFix[sati - 1]           = 1;
                flag                       = 1;
                break;
            }
            if (flag == 0)
            {
                continue;
            }
            for (j = k; j < k + rtk->nx; j++)
            {
                sati = rtk->nxRecordSat[j - k];
                frqi = rtk->nxRecordFrq[j - k];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (i == j || rtk->x[j] == 0.0 || !test_sys(sys, m) || !rtk->ssat[sati - 1].vsat[f])
                {
                    continue;
                }
                if (!(rtk->ssat[sati - 1].slip[f] & 2))
                {
                    D[i + (na + nb) * rtk->nx] = 1.0;
                    D[j + (na + nb) * rtk->nx] = -1.0;
                    rtk->ssat[sati - 1].fix[f] = 2; /* fix */
                    // printf("sati=%d f=%d fix=2\n", sati, f);
                    satFlag[sati - 1] = 1;
                    satFix[sati - 1]  = 1;
                    nb++;
                }
                else
                {
                    rtk->ssat[sati - 1].fix[f] = 1;
                    // printf("sati=%d f=%d fix=1\n", sati, f);
                    trace(0x10, "sati=%d f=%d fix=1\n", sati, f);
                }
            }
        }
    }
    for (i = 0; i < MAXSAT; i++)
    {
        if (satFlag[i] == 1)
        {
            (*fixSatNum)++;
        }
        if (satFix[i] == 1)
        {
            ns++;
        }
    }
    rtk->sol.ns[1] = ns;
    trace(2, "ddmat, fisSatNum, %d, ns, %d\n", *fixSatNum, ns);
    return nb;
}

static void restamb(rtk_t* rtk, const double* bias, int nb)
{
    int i, n, f = 0, m, indexb, index[MAXSAT], nv = 0, nf = rtk->opt.nf,
              xa = rtk->np + rtk->nt + rtk->ni;
    int sati, frqi, sys;

    for (f = 0; f < nf; f++)
    {
        for (m = 0; m < NSYS; m++)
        {
            for (n = i = 0; i < rtk->nx; i++)
            {
                sati = rtk->nxRecordSat[i];
                frqi = rtk->nxRecordFrq[i];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (!test_sys(sys, m) || rtk->ssat[sati - 1].fix[f] != 2)
                {
                    continue;
                }
                index[n++] = i;
            }
            indexb = -1;
            for (i = 0; i < rtk->nx; i++)
            {
                sati = rtk->nxRecordSat[i];
                frqi = rtk->nxRecordFrq[i];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (sati != rtk->base_prn[m][f])
                {
                    continue;
                }
                indexb = i;
                // trace(
                //     2, "restamb, nxRecord, indexb, %d, xIndex, %d\n", indexb,
                //     rtk->ssat[sati - 1].xIndex[frqi]
                //);
                break;
            }
            if (indexb == -1)
            {
                continue;
            }
            if (n >= 2)
            {
                for (i = 0; i < n; i++)
                {
                    if (index[i] == indexb)
                    {
                        continue;
                    }
                    rtk->xp[index[i] + xa] = rtk->xp[indexb + xa] - bias[nv];
                    nv++;
                }
            }
        }
    }
}

/* hold integer ambiguity ----------------------------------------------------*/
static void holdamb(rtk_t* rtk, const double* xa)
{
    int           i, n, m, f, info, indexb, index[MAXSAT], nb = rtk->na, nv = 0, nf = rtk->opt.nf;
    unsigned char sati, frqi, sys;

    trace(2, "holdamb, nb, %d\n", nb);
    memset(rtk->v, 0, sizeof(double) * NY);
    memset(rtk->R, 0, sizeof(double) * NY * NY);
    memset(rtk->H, 0, sizeof(double) * NY * NX);

    for (f = 0; f < nf; f++)
    {
        for (m = 0; m < NSYS; m++)
        {
            for (n = i = 0; i < nb; i++)
            {
                sati = rtk->nxRecordSat[i];
                frqi = rtk->nxRecordFrq[i];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (!test_sys(sys, m) || rtk->ssat[sati - 1].fix[frqi] != 2)
                {
                    continue;
                }
                index[n++]             = i;
                rtk->ssat[sati].fix[f] = 3; /* hold */
                                            // printf("sati=%d f=%d fix=3\n", sati, f);
            }
            /* constraint to fixed ambiguity */
            indexb = -1;
            for (i = 0; i < nb; i++)
            {
                sati = rtk->nxRecordSat[i];
                frqi = rtk->nxRecordFrq[i];
                sys  = satsys(sati, NULL);
                if (sati == 0 || f != frqi)
                {
                    continue;
                }
                if (sati != rtk->base_prn[m][f])
                {
                    continue;
                }
                indexb = i;
                // trace(2, "holdamb, i, %d, xIndex, %d\n", i, );
                break;
            }
            if (indexb == -1)
            {
                continue;
            }
            for (i = 0; i < n; i++)
            {
                if (index[i] == indexb)
                {
                    continue;
                }
                rtk->v[nv] = (xa[indexb] - xa[index[i]]) - (rtk->x[indexb] - rtk->x[index[i]]);
                rtk->H[indexb + nv * rtk->nx]   = 1.0;
                rtk->H[index[i] + nv * rtk->nx] = -1.0;
                nv++;
            }
        }
    }
    if (nv > 0)
    {
        for (i = 0; i < nv; i++)
        {
            rtk->R[i + i * nv] = 0.001;
        }
        /* update states with constraints */
        // if ((info = filter(rtk->x, rtk->P, H, v, R, rtk->nx, nv))) {
        if ((info = filter(
                 rtk, rtk->xp, rtk->Pp, rtk->H, rtk->v, rtk->R, rtk->nx, nv, rtk->x, rtk->P
             )))
        {
            trace(0x02, "filter error (info=%d)\n", info);
        }
    }
}

/* resolve integer ambiguity by LAMBDA ---------------------------------------*/
extern int resamb_LAMBDA(rtk_t* rtk, int lcopt)
{
    int i, j, k = 0, m = 0, n = 0, fixSat = 0, ny, nb, info, nx = rtk->nx,
              na = rtk->np + rtk->nt + rtk->ni;
    double *D, *DP, *y, *Qy, *b, *db, *Qb, *Qab, *QQ, *bias, *Pp, s[2], thresar, ratio;
    // char buff[10240] = { 0 };
    // char *p = buff;

    if (rtk->opt.mode <= PMODE_DGPS)
    {
        return 0;
    }
    if (lcopt == 0)
    {
        rtk->sol.ratio = 0.0;
        nx             = rtk->nx;
    }
    /* single to double-difference transformation matrix (D') */
    // D = zeros(nx, nx);
    memset(rtk->I, 0, sizeof(double) * NX * NX);
    D = rtk->I;
    if (lcopt == 0)
    {
        if ((nb = ddmat(rtk, D, &fixSat)) <= 0)
        {
            trace(2, "no valid double-difference\n");
            // free(D);
            return 0;
        }
    }
    trace(0x10, "lambda fix sat:%d\n", fixSat);
    if (fixSat < 3)
    {
        trace(2, "lambda fix sat:%d\n", fixSat);
        // free(D);
        return 0;
    }
    rtk->sol.ratio = 0.0;
    // trace(4, "ddmat sat\n");

    ny = na + nb;
    // y = zeros(ny, 1);
    // Qy = zeros(ny, ny);
    // DP = zeros(ny, nx);
    b  = mat(nb, 2);
    Pp = zeros(na, na);
    db = mat(nb, 1);
    // Qb = mat(nb, nb);
    // Qab = mat(na, nb);
    // QQ = mat(na, nb);
    bias = mat(nx, 1);

    // double K[NY * NX];
    // double F[NY * NX];
    // double I[NX * NX];
    // double H[NY * NX];
    // double Ri[NY];
    // double Rj[NY];
    // double R[NY * NY];//41k
    y  = rtk->Ri;
    Qy = rtk->R;
    Qb = rtk->K;
    DP = rtk->H;

    // if ((!y) || (!Qy) || (!DP) || (!b) || (!db) || (!Qb) || (!Qab) || (!QQ) ||
    // (!bias) || (!Pp))
    //{
    //     trace(1, "resamb_LAMBDA mat error\n");
    //     return 0;
    // }
    if (lcopt == 0)
    {
        for (i = 0; i < na; i++)
        {
            for (j = 0; j < nx; j++)
            {
                DP[i + j * ny] = rtk->Pp[i + j * nx];
            }
        }
        for (i = 0, k = 0; i < na; i++)
        {
            y[k++] = rtk->xp[i];
        }

        for (i = na; i < ny; i++)
        {
            for (j = 0; j < nx; j++)
            {
                if (D[j + i * nx] == 1.0)
                {
                    m = j;
                }
                if (D[j + i * nx] == -1.0)
                {
                    n = j;
                }
            }
            if (m == 0 || n == 0)
            {
                break;
            }
            else
            {
                y[k++] = rtk->xp[m] - rtk->xp[n];
                for (j = 0; j < nx; j++)
                {
                    DP[i + j * ny] = rtk->Pp[m + j * nx] - rtk->Pp[n + j * nx];
                }
                m = n = 0;
            }
        }
        for (i = 0; i < na; i++)
        {
            for (j = 0; j < ny; j++)
            {
                Qy[j + i * ny] = DP[j + i * ny];
            }
        }
        for (i = na; i < ny; i++)
        {
            for (j = 0; j < nx; j++)
            {
                if (D[j + i * nx] == 1.0)
                {
                    m = j;
                }
                if (D[j + i * nx] == -1.0)
                {
                    n = j;
                }
            }
            if (m == 0 || n == 0)
            {
                break;
            }
            else
            {
                for (j = 0; j < ny; j++)
                {
                    Qy[j + i * ny] = Qy[i + j * ny] = DP[j + m * ny] - DP[j + n * ny];
                }
                m = n = 0;
            }
        }

        /* phase-bias covariance (Qb) and real-parameters to bias covariance (Qab)
         */
        for (i = 0; i < nb; i++)
        {
            for (j = 0; j < nb; j++)
            {
                Qb[i + j * nb] = Qy[na + i + (na + j) * ny];
            }
        }
    }

#if 0
    char buff[40690] = { 0 };
    char* p = buff;
    p += sprintf(p, "*********D************");
    for (i = 0; i < nx * nx; i++)
    {
        if (i % nx == 0)  p += sprintf(p, "\n ");
        p += sprintf(p, "%3.0f ", D[i]);
    }
    p += sprintf(p, "\n ");

    fprintf(fptest, "*********y************\n");
    for (i = 0; i < ny; i++)    p += sprintf(p, "%14.4lf", y[i]);
    p += sprintf(p, "\n");
    p += sprintf(p, "*********Qy************");
    for (i = 0; i < ny * ny; i++)
    {
        if (i % ny == 0)  p += sprintf(p, "\n ");
        p += sprintf(p, "%10.4lf", Qy[i]);
    }
    p += sprintf(p, "\n ");


    p += sprintf(p, "*********Qb************\n");
    for (i = 0; i < nb * nb; i++) {
        if (i % nb == 0) p += sprintf(p, "\n ");
        p += sprintf(p, "%10.4lf", Qb[i]);
    }
    p += sprintf(p, "\n ");
    fprintf(fptest, buff);
    fflush(fptest);
#endif

    // info=plambda(y + na, Qb, nb, 2, b,s, 0.999);
    info = lambda(rtk, nb, 2, y + na, Qb, b, s, lcopt);
    if (info == 0 || info == -2 || info == -3)
    {
        // if (!(info = lambda(nb, 2, y + na, Qb, b, s, msg))) {
        ratio = s[0] > 0 ? (float)(s[1] / s[0]) : 0.0f;
        if (ratio > 999.9)
        {
            ratio = 999.9f;
        }
        if (lcopt == 0)
        {
            rtk->sol.ratio = ratio;
        }
        // printf("ratio=%f\n", rtk->sol.ratio);
        if (fixSat < 6)
        {
            thresar = 5.0;
        }
        else if (fixSat >= 6 && fixSat < 10)
        {
            thresar = 3.0;
        }
        else if (fixSat >= 10 && fixSat < 20)
        {
            thresar = 2.5;
        }
        else
        {
            thresar = 2.0;
        }
        // thresar = 1.5;
        if (lcopt == 3 || lcopt == 4)
        {
            thresar = 3.0;
        }
        // if (rtk->opt.ionoopt == IONOOPT_EST && fixSat>10 && rtk->sol.fixxyz[0] !=
        // 0.0) {
        //     thresar = 2.0;
        // }
        // if (lcopt == 4) {
        //     thresar = 1.3;
        // }
        // thresar = 1.0;
        trace(0x02, "lambda ratio=%.1f\n", ratio);
        // if (nb < 5) rtk->sol.ratio = 0.0;
        if ((s[0] <= 0.0 || (ratio >= thresar)) && info != -2 && info != -3)
        {
            for (i = 0; i < na; i++)
            {
                if (lcopt == 0)
                {
                    for (j = 0; j < na; j++)
                    {
                        Pp[i + j * na] = rtk->Pp[i + j * nx];
                    }
                }
            }
            for (i = 0; i < nb; i++)
            {
                bias[i] = b[i];
                y[na + i] -= b[i];
            }
            if (!matinv(Qb, nb))
            {
                memset(rtk->F, 0, sizeof(double) * NY * NX);
                memset(rtk->I, 0, sizeof(double) * NX * NX);
                Qab = rtk->F;
                QQ  = rtk->I;
                for (i = 0; i < na; i++)
                {
                    for (j = 0; j < nb; j++)
                    {
                        Qab[i + j * na] = Qy[i + (na + j) * ny];  //??Qab???
                    }
                }

                if (lcopt == 0)
                {
                    matmul("NN", nb, 1, nb, 1.0, Qb, y + na, 0.0, db);
                    matmul("NN", na, 1, nb, -1.0, Qab, db, 1.0, rtk->xp);
                    /* transform float to fixed solution (xa=xa-Qab*Qb\(b0-b)) */
                    /* covariance of fixed solution (Qa=Qa-Qab*Qb^-1*Qab') */
                    matmul("NN", na, nb, nb, 1.0, Qab, Qb, 0.0, QQ);
                    matmul("NT", na, na, nb, -1.0, QQ, Qab, 1.0, Pp);
                    for (i = 0; i < 3; i++)
                    {
                        for (j = 0; j < 3; j++)
                        {
                            rtk->Pp[i + j * nx] = Pp[i + j * na];
                        }
                    }
                    for (i = 0; i < 3; i++)
                    {
                        for (j = 0; j < 3; j++)
                        {
                            rtk->Pp[i + j * nx] = Pp[i + j * na];
                        }
                    }
                    restamb(rtk, bias, nb);
                }
            }
            else
            {
                nb = 0;
            }
        }
        else
        {
            nb = 0;
        }
    }
    else
    {
        rtk->sol.ratio = 0.0;
        trace(0x02, "lambda error (info=%d)\n", info);
    }
    // free(D);
    // free(y);
    // free(Qy);
    // free(DP);
    free(b);
    free(db);
    // free(Qb);
    // free(Qab);
    // free(QQ);
    free(bias);
    free(Pp);

    // y = rtk->Ri;
    // Qy = rtk->R;
    // DP = rtk->H;
    // Qb = rtk->K;
    // Qab = rtk->F;
    // QQ = rtk->I;
    if (info == -1)
    {
        return -1;
    }
    return nb; /* number of ambiguities */
}

static int scanFloatState(rtk_t* rtk, unsigned char* floatSat, unsigned char* floatSatFrq)
{
    unsigned char i, j, sati, frqi, satj, frqj, flag, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char floatCnt = 0;
    if (rtk->nxFixNx <= 5)
    {
        return 0;
    }
    for (i = na; i < rtk->nx; i++)
    {
        sati = rtk->nxRecordSat[i - na];
        frqi = rtk->nxRecordFrq[i - na];
        if (rtk->ssat[sati - 1].vsat[frqi] == 0)
        {
            continue;
        }
        flag = 0;
        for (j = 0; j < rtk->nxFixNx; j++)
        {
            satj = rtk->nxFixSat[j];
            frqj = rtk->nxFixFrq[j];
            if (sati == satj && frqi == frqj)
            {
                if (rtk->ssat[satj - 1].slip[frqj] & 1)
                {
                    break;
                }
                // if (rtk->ssat[satj - 1].slip[frqj] & 2) break;
                flag = 1;
                break;
            }
        }
        if (flag == 0)
        {
            trace(
                0x02, "AR failed by sat=%3d f=%2d el=%7.2f %7.2f\n", sati, frqi,
                rtk->ssat[sati - 1].azel[0][1] * R2D, rtk->ssat[sati - 1].azel[1][1] * R2D
            );
            initxp(rtk, 0, SQR(30), i);
            if (rtk->opt.ionoopt == IONOOPT_EST)
            {
                for (j = 0; j < rtk->ns; j++)
                {
                    if (rtk->nsSat[j] == sati)
                    {
                        initxp(rtk, 0, 0, (j + rtk->np + rtk->nt));
                        trace(0x10, "initxp ion sat=%d i=%d\n", sati, j);
                    }
                }
            }
            floatSat[floatCnt]    = sati;
            floatSatFrq[floatCnt] = frqi;
            floatCnt++;
        }
    }
    return floatCnt;
}

static void rtkFloatState(rtk_t* rtk, unsigned char vsat[][NFREQ])
{
    unsigned char floatSat[3 + 2 + 40 + 40 * NFREQ]    = {0};
    unsigned char floatSatFrq[3 + 2 + 40 + 40 * NFREQ] = {0};
    unsigned char i, j, k;
    double        ratio;
    int           lambdaReturn;
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);

    k = scanFloatState(rtk, floatSat, floatSatFrq);

    if (k > 0 && rtk->nxFixNx > 0)
    {
        trace(0x02, "rtkFloatState\n");
        rtk->rejSatCnt = k;
        ratio          = k / (double)rtk->na;
        if (k >= 1 && rtk->opt.ionoopt == IONOOPT_EST)
        {
            trace(0x10, "rejcnt:%d na:%d ratio=%f\n", k, rtk->na, ratio);
        }

        if (ratio > 0.5)
        {
            trace(0x02, "delete ratio too large:%f and reset\n", ratio);
            rtk->opt.std   = 0.01;
            rtk->sol.stat  = 0;
            rtk->fix_state = 0;
            return;
        }
        for (i = 0; i < k; i++)
        {
            rtk->ssat[floatSat[i] - 1].vsat[floatSatFrq[i]] = 0;
        }
        lambdaReturn = resamb_LAMBDA(rtk, 0);
        if (lambdaReturn == -1)
        {
            rtk->sol.stat = 0;
        }
        if (lambdaReturn > 1)
        {
            rtk->sol.stat = SOLQ_FIX;
            if (checkFixP(rtk, 0))
            {
                matcpy(rtk->x, rtk->xp, NX, 1);
                matcpy(rtk->P, rtk->Pp, NX, NX);
            }
            else
            {
                if (rtk->nfix >= 10)
                {
                    holdamb(rtk, rtk->xp);
                }
            }
        }
        else
        {
            matcpy(rtk->xp, rtk->x, NX, 1);
            matcpy(rtk->Pp, rtk->P, NX, NX);
            for (i = 0; i < MAXSAT; i++)
            {
                for (j = 0; j < NFREQ; j++)
                {
                    rtk->ssat[i].vsat[j] = vsat[i][j];
                }
            }
        }
    }
}
#if 0
static void rtkARdeleteOneSat(rtk_t* rtk, unsigned char* sat, int n, unsigned char vsat[][NFREQ]) {
    int i, j, k, lambdaReturn, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char sati = 0;
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);

    for (k = 0; k < n; k++) {
        for (j = 0; j < NFREQ; j++) {
            if (sat[k] == rtk->base_prn[0][j] || sat[k] == rtk->base_prn[1][j] ||
                sat[k] == rtk->base_prn[2][j] || sat[k] == rtk->base_prn[3][j]) {
                continue;
            }
            rtk->ssat[sat[k] - 1].vsat[j] = 0;
        }
        lambdaReturn = resamb_LAMBDA(rtk, 0);
        if (lambdaReturn == -1) rtk->sol.stat = 0;
        if (lambdaReturn > 1) {
            for (i = na; i < rtk->nx; i++) {
                sati = rtk->nxRecordSat[i - na];
                if (sat[k] == sati) {
                    initxp(rtk, 0, 0, i);
                }
            }
            if (rtk->opt.ionoopt == IONOOPT_EST) {
                for (i = 0; i < rtk->ns; i++) {
                    if (rtk->nsSat[i] == sat[k]) {
                        initxp(rtk, 0, 0, (i + rtk->np + rtk->nt));
                        trace(0x10, "initxp ion sat=%d i=%d\n", sati, i);
                    }
                }
            }
            rtk->sol.stat = SOLQ_FIX;
            if (checkFixP(rtk, 0)) {
                matcpy(rtk->x, rtk->xp, NX, 1);
                matcpy(rtk->P, rtk->Pp, NX, NX);
            }
            else {
                /* hold integer ambiguity */
                if (rtk->nfix >= 10)     holdamb(rtk, rtk->xp);
                break;
            }
        }
        else {
            matcpy(rtk->xp, rtk->x, NX, 1);
            matcpy(rtk->Pp, rtk->P, NX, NX);

            for (i = 0; i < MAXSAT; i++) {
                for (j = 0; j < NFREQ; j++) {
                    rtk->ssat[i].vsat[j] = vsat[i][j];
                }
            }
        }
    }
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);
}
static void rtkARdeleteTwoSat(rtk_t* rtk, unsigned char* sat, int n, unsigned char vsat[][NFREQ]) {
    int i, j, k, lambdaReturn, flag, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char sati = 0, sys, prn;
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);
    for (k = 0; k < n && k + 1 < n; k += 2) {
        for (j = 0; j < NFREQ; j++) {
            if (sat[k] != rtk->base_prn[0][j] && sat[k] != rtk->base_prn[1][j] &&
                sat[k] != rtk->base_prn[2][j] && sat[k] != rtk->base_prn[3][j]) {
                rtk->ssat[sat[k] - 1].vsat[j] = 0;
            }
            if (sat[k + 1] != rtk->base_prn[0][j] && sat[k + 1] != rtk->base_prn[1][j] &&
                sat[k + 1] != rtk->base_prn[2][j] && sat[k + 1] != rtk->base_prn[3][j]) {
                rtk->ssat[sat[k + 1] - 1].vsat[j] = 0;
            }
        }
        lambdaReturn = resamb_LAMBDA(rtk, 0);
        if (lambdaReturn == -1) rtk->sol.stat = 0;
        if (lambdaReturn > 1) {
            for (i = na; i < rtk->nx; i++) {
                sati = rtk->nxRecordSat[i - na];
                flag = 0;
                for (j = 0; j < NFREQ; j++) {
                    if (sati == rtk->base_prn[0][j] || sati == rtk->base_prn[1][j] ||
                        sati == rtk->base_prn[2][j] || sati == rtk->base_prn[3][j]) {
                        flag = 1;
                        break;
                    }
                }
                if (flag == 1) continue;
                if (sat[k] == sati || sat[k + 1] == sati) {
                    sys = satsys(sati, &prn);
                    initxp(rtk, 0, 0, i);
                    for (j = 0; j < NFREQ; j++)
                        rtk->ssat[sati - 1].vsat[j] = 0;
                }
            }
            if (rtk->opt.ionoopt == IONOOPT_EST) {
                for (i = 0; i < rtk->ns; i++) {
                    flag = 0;
                    for (j = 0; j < NFREQ; j++) {
                        if (rtk->nsSat[i] == rtk->base_prn[0][j] || rtk->nsSat[i] == rtk->base_prn[1][j] ||
                            rtk->nsSat[i] == rtk->base_prn[2][j] || rtk->nsSat[i] == rtk->base_prn[3][j]) {
                            flag = 1;
                            break;
                        }
                    }
                    if (flag == 1) continue;
                    if (rtk->nsSat[i] == sat[k] || rtk->nsSat[i] == sat[k + 1]) {
                        sys = satsys(rtk->nsSat[i], &prn);
                        initxp(rtk, 0, 0, (i + rtk->np + rtk->nt));
                        trace(0x10, "initxp ion sys=%3d prn=%3d i=%d\n", sys, prn, i);
                    }
                }
            }
            rtk->sol.stat = SOLQ_FIX;
            if (checkFixP(rtk, 0)) {
                matcpy(rtk->x, rtk->xp, NX, 1);
                matcpy(rtk->P, rtk->Pp, NX, NX);
            }
            else {
                /* hold integer ambiguity */
                if (rtk->nfix >= 10)     holdamb(rtk, rtk->xp);
                break;
            }
        }
        else {
            matcpy(rtk->xp, rtk->x, NX, 1);
            matcpy(rtk->Pp, rtk->P, NX, NX);

            for (i = 0; i < MAXSAT; i++) {
                for (j = 0; j < NFREQ; j++) {
                    rtk->ssat[i].vsat[j] = vsat[i][j];
                }
            }
        }
    }
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);
}
static void rtkARdeleteBD3(rtk_t* rtk, unsigned char* sat, int n, unsigned char vsat[][NFREQ]) {
    int i, j, k, lambdaReturn;
    unsigned char sati, frqi, prni, sysi;
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);

    for (k = 0; k < n; k++) {
        sysi = satsys(sat[k], &prni);
        if (sysi == SYS_BDS && prni > 19) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[sat[k] - 1].vsat[j] = 0;
            }
        }
    }
    for (i = 0; i < rtk->na; i++) {
        sati = rtk->nxRecordSat[i];
        frqi = rtk->nxRecordFrq[i];
        sysi = satsys(sati, &prni);
        if (sysi == SYS_BDS && prni > 19) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[sati - 1].vsat[frqi] = 0;
            }
            if (rtk->opt.ionoopt == IONOOPT_EST) {
                for (j = 0; j < rtk->ns; j++) {
                    if (rtk->nsSat[j] == sati) {
                        initxp(rtk, 0, 0, (j + rtk->np + rtk->nt));
                        trace(0x10, "initxp ion sat=%d i=%d\n", sati, j);
                    }
                }
            }
        }
    }

    lambdaReturn = resamb_LAMBDA(rtk, 0);
    if (lambdaReturn == -1) rtk->sol.stat = 0;
    if (lambdaReturn > 1) {
        rtk->sol.stat = SOLQ_FIX;
        if (checkFixP(rtk, 0)) {
            matcpy(rtk->x, rtk->xp, NX, 1);
            matcpy(rtk->P, rtk->Pp, NX, NX);
        }
        else {
            /* hold integer ambiguity */
            if (rtk->nfix >= 10)     holdamb(rtk, rtk->xp);
        }
    }
    else {
        matcpy(rtk->xp, rtk->x, NX, 1);
        matcpy(rtk->Pp, rtk->P, NX, NX);
        for (i = 0; i < MAXSAT; i++) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[i].vsat[j] = vsat[i][j];
            }
        }
    }
}
static void rtkARdeleteSys(rtk_t* rtk, int sys, unsigned char* sat, int n, unsigned char vsat[][NFREQ]) {
    int i, j, k, lambdaReturn;
    unsigned char sysi, sati, frqi;
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);

    for (k = 0; k < n; k++) {
        sysi = satsys(sat[k], NULL);
        if (sysi & sys) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[sat[k] - 1].vsat[j] = 0;
            }
        }
    }
    for (i = 0; i < rtk->na; i++) {
        sati = rtk->nxRecordSat[i];
        frqi = rtk->nxRecordFrq[i];
        sysi = satsys(sati, NULL);
        if (sysi & sys) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[sati - 1].vsat[frqi] = 0;
            }
            if (rtk->opt.ionoopt == IONOOPT_EST) {
                for (j = 0; j < rtk->ns; j++) {
                    if (rtk->nsSat[j] == sati) {
                        initxp(rtk, 0, 0, (j + rtk->np + rtk->nt));
                        trace(0x10, "initxp ion sat=%d i=%d\n", sati, j);
                    }
                }
            }
        }
    }

    lambdaReturn = resamb_LAMBDA(rtk, 0);
    if (lambdaReturn == -1) rtk->sol.stat = 0;
    if (lambdaReturn > 1) {
        rtk->sol.stat = SOLQ_FIX;
        if (checkFixP(rtk, 0)) {
            matcpy(rtk->x, rtk->xp, NX, 1);
            matcpy(rtk->P, rtk->Pp, NX, NX);
        }
        else {
            /* hold integer ambiguity */
            if (rtk->nfix >= 10)     holdamb(rtk, rtk->xp);
        }
    }
    else {
        matcpy(rtk->xp, rtk->x, NX, 1);
        matcpy(rtk->Pp, rtk->P, NX, NX);
        for (i = 0; i < MAXSAT; i++) {
            for (j = 0; j < NFREQ; j++) {
                rtk->ssat[i].vsat[j] = vsat[i][j];
            }
        }
    }
}
#endif

static void rtkAR(rtk_t* rtk, obsd_t* obs, unsigned char* sat, int n)
{
    int i, j, k, m, f, sati, frqi, lambdaReturn, nf = rtk->opt.nf, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char vsat[MAXSAT][NFREQ], sys, prn;
    double        ratio = 0.0;
    for (i = 0; i < MAXSAT; i++)
    {
        for (j = 0; j < NFREQ; j++)
        {
            vsat[i][j] = rtk->ssat[i].vsat[j];
        }
    }
    matcpy(rtk->x, rtk->xp, NX, 1);
    matcpy(rtk->P, rtk->Pp, NX, NX);

    lambdaReturn = resamb_LAMBDA(rtk, 0);

    if (lambdaReturn == -1)
    {
        rtk->sol.stat = 0;
    }
    if (lambdaReturn > 1)
    {
        /* hold integer ambiguity */
        rtk->sol.stat = SOLQ_FIX;
        // if (checkFixP(rtk)) {
        matcpy(rtk->x, rtk->xp, NX, 1);
        matcpy(rtk->P, rtk->Pp, NX, NX);
        holdamb(rtk, rtk->xp);
        //}
        // else {
        //    if (rtk->nfix >= 10) holdamb(rtk, rtk->xp);
        //}
    }
    else
    {
        matcpy(rtk->xp, rtk->x, NX, 1);
        matcpy(rtk->Pp, rtk->P, NX, NX);
    }
    if (rtk->sol.stat == SOLQ_FLOAT)
    {
        ratio = rtk->sol.ratio;
        if (rtk->nxFixNx > 0)
        {
            rtkFloatState(rtk, vsat);
        }
#if 0
        if (rtk->sol.stat == SOLQ_FLOAT && rtk->base_prn[1][0] != 0) {
            trace(8, "rtkARdelete GLO\n");
            rtkARdeleteSys(rtk, SYS_GLO, sat, n, vsat);
        }
        //if (rtk->sol.stat == SOLQ_FLOAT && rtk->ns > 5 && rtk->ns > 10) {
        //    trace(8, "rtkARdeleteOneSat\n");
        //    rtkARdeleteOneSat(rtk, sat, n, vsat);
        //}
        if (rtk->sol.stat == SOLQ_FLOAT && rtk->ns > 10) {
            trace(8, "rtkARdeleteTwoSat\n");
            rtkARdeleteTwoSat(rtk, sat, n, vsat);
        }
        //if (rtk->sol.stat == SOLQ_FLOAT && rtk->ns > 10) {
        //    trace(8, "rtkARdeleteBD3\n");
        //    rtkARdeleteBD3(rtk, sat, n, vsat);
        //}
#endif
        // if (rtk->fix30flag == 0) {
        //     if (rtk->sol.stat == SOLQ_FLOAT&&rtk->base_prn[1][0]!=0) {
        //         trace(4, "rtkARdelete GLO\n");
        //         rtkARdeleteSys(rtk, SYS_GLO, sat, n, vsat);
        //     }
        //     if (rtk->sol.stat == SOLQ_FLOAT && rtk->base_prn[0][0] != 0) {
        //         trace(4, "rtkARdelete GPS\n");
        //         rtkARdeleteSys(rtk, SYS_GPS, sat, n, vsat);
        //     }
        //     if (rtk->sol.stat == SOLQ_FLOAT && rtk->base_prn[3][0] != 0) {
        //         trace(4, "rtkARdelete BDS\n");
        //         rtkARdeleteSys(rtk, SYS_BDS, sat, n, vsat);
        //     }
        // }
    }

    if (rtk->sol.stat == SOLQ_FLOAT)
    {
        rtk->nfix = 0;
        checkFloatP(rtk, 0);
    }
    if (rtk->sol.stat != SOLQ_FIX)
    {
        rtk->nxFixNx = 0;
    }

    for (i = 0; i < NSYS; i++)
    {
        // if (i == 0)    trace(0x04, "GPS BASE PRN:");
        // else if (i == 1)    trace(0x04, "GLO BASE PRN:");
        // else if (i == 2)    trace(0x04, "GAL BASE PRN:");
        // else  trace(0x04, "BDS BASE PRN:");
        // for (j = 0; j < NFREQ; j++) {
        //     sys = satsys(rtk->base_prn[i][j], &prn);
        //     trace(0x04, "%3d ", prn);
        // }
        // trace(0x04, "\n");
    }
    j = -1;
    for (i = na; i < rtk->nx; i++)
    {
        sati = rtk->nxRecordSat[i - na];
        frqi = rtk->nxRecordFrq[i - na];
        sys  = satsys(sati, &prn);
        j    = frqi;
    }
    // trace(0x04, "\n");

    /* save solution status */
    if (rtk->sol.stat == SOLQ_FIX)
    {
        for (i = 0; i < 3; i++)
        {
            rtk->sol.rr[i] = rtk->xp[i];
            rtk->sol.qr[i] = (float)rtk->Pp[i + i * rtk->nx];
        }
        for (i = 0; i < rtk->nx; i++)
        {
            rtk->x[i] = rtk->xp[i];
        }
        rtk->sol.qr[3] = (float)rtk->Pp[1];
        rtk->sol.qr[4] = (float)rtk->Pp[1 + 2 * rtk->nx];
        rtk->sol.qr[5] = (float)rtk->Pp[2];

        // rtk->sol.ns[1] = 0;
        k = 0;
        j = -1;
        for (i = na; i < rtk->nx; i++)
        {
            sati = rtk->nxRecordSat[i - na];
            frqi = rtk->nxRecordFrq[i - na];
            sys  = satsys(sati, &prn);
            if (rtk->ssat[sati - 1].slip[frqi] & 2)
            {
                continue;
            }
            if (rtk->ssat[sati - 1].vsat[frqi] == 0)
            {
                continue;
            }
            if (rtk->xp[i] != 0.0)
            {
                rtk->nxFixSat[k] = sati;
                rtk->nxFixFrq[k] = frqi;
                k++;
                j = frqi;
            }
            // if (frqi == 0) rtk->sol.ns[1]++;
        }
        // trace(0x04, "\n");
        rtk->nxFixNx = k;
        if (rtk->opt.dynamics)
        { /* velocity and covariance */
            for (i = 3; i < 6; i++)
            {
                rtk->sol.rr[i]     = rtk->xp[i];
                rtk->sol.qv[i - 3] = (float)rtk->Pp[i + i * rtk->na];
            }
            if (rtk->opt.dynamics == 2)
            {
                for (i = 6; i < 9; i++)
                {
                    rtk->sol.rr[i] = rtk->xp[i];
                }
            }
            rtk->sol.qv[3] = (float)rtk->Pp[4 + 3 * rtk->na];
            rtk->sol.qv[4] = (float)rtk->Pp[5 + 4 * rtk->na];
            rtk->sol.qv[5] = (float)rtk->Pp[5 + 3 * rtk->na];
        }
        rtk->nfloat = 0;
        rtk->nfix++;
    }
    else if (rtk->sol.stat == SOLQ_FLOAT)
    {
        for (i = 0; i < 3; i++)
        {
            rtk->sol.rr[i] = rtk->xp[i];
            rtk->sol.qr[i] = (float)rtk->Pp[i + i * rtk->nx];
        }
        rtk->sol.qr[3] = (float)rtk->Pp[1];
        rtk->sol.qr[4] = (float)rtk->Pp[1 + 2 * rtk->nx];
        rtk->sol.qr[5] = (float)rtk->Pp[2];

        if (rtk->opt.dynamics)
        { /* velocity and covariance */
            for (i = 3; i < 6; i++)
            {
                rtk->sol.rr[i]     = rtk->xp[i];
                rtk->sol.qv[i - 3] = (float)rtk->Pp[i + i * rtk->na];
            }
            if (rtk->opt.dynamics == 2)
            {
                for (i = 6; i < 9; i++)
                {
                    rtk->sol.rr[i] = rtk->xp[i];
                }
            }
            rtk->sol.qv[3] = (float)rtk->Pp[4 + 3 * rtk->nx];
            rtk->sol.qv[4] = (float)rtk->Pp[5 + 4 * rtk->nx];
            rtk->sol.qv[5] = (float)rtk->Pp[5 + 3 * rtk->nx];
        }
        rtk->nfix = 0;
        rtk->nfloat++;
        rtk->sol.ratio = ratio;
        for (j = 0; j < rtk->opt.nf; j++)
        {
            // trace(0x04, "f:%d float sat:", j);
            for (i = 0; i < MAXSAT; i++)
            {
                if (rtk->ssat[i].vsat[j] == 0)
                {
                    continue;
                }
                // trace(0x04, "%3d ", i + 1);
            }
            // trace(0x04, "\n");
        }
    }
    else
    {
        rtk->sol.stat = SOLQ_NONE;
        rtk->nfix     = 0;
        rtk->nfloat   = 0;
    }
    for (i = 0; i < MAXSAT; i++)
    {
        for (j = 0; j < nf; j++)
        {
            if (rtk->ssat[i].fix[j] == 2 && rtk->sol.stat != SOLQ_FIX)
            {
                rtk->ssat[i].fix[j] = 1;
            }
        }
    }
    if (rtk->sol.stat != SOLQ_NONE)
    {
        matcpy(rtk->x, rtk->xp, NX, 1);
        matcpy(rtk->P, rtk->Pp, NX, NX);
    }
    else
    {
        memset(rtk->x, 0, sizeof(double) * NX);
        memset(rtk->P, 0, sizeof(double) * NX * NX);
        memset(rtk->xp, 0, sizeof(double) * NX);
        memset(rtk->Pp, 0, sizeof(double) * NX * NX);
    }
#if 1
    unsigned char satj, frqj;
    if (rtk->sol.stat == SOLQ_FIX)
    {
        k = rtk->np + rtk->nt + rtk->ni;
        for (i = 0; i < MAXSAT; i++)
        {
            for (j = 0; j < NFREQ; j++)
            {
                rtk->ssat[i].fix_amb[j] = 9999.9;
            }
        }
        for (i = 0; i < NSYS; i++)
        {
            for (j = 0; j < NFREQ; j++)
            {
                rtk->basePrnLsq[i][j] = rtk->base_prn[i][j];
            }
        }
        for (j = 0; j < NFREQ; j++)
        {
            rtk->nsLsq[j] = 0;
            for (i = 0; i < MAXOBS; i++)
            {
                rtk->satLsq[j][i] = 0;
            }
        }
        for (j = 0; j < rtk->nxFixNx; j++)
        {
            satj                                  = rtk->nxFixSat[j];
            frqj                                  = rtk->nxFixFrq[j];
            rtk->satLsq[frqj][rtk->nsLsq[frqj]++] = satj;

            if (rtk->opt.ionoopt == IONOOPT_EST)
            {
                rtk->ssat[satj - 1].fix_ion = rtk->ssat[satj - 1].dion;
            }
            else
            {
                rtk->ssat[satj - 1].fix_ion = 0.0;
            }
            if (rtk->opt.tropopt == TROPOPT_EST)
            {
                rtk->ssat[satj - 1].fix_trop = rtk->ssat[satj - 1].ddtrp;
            }
            else
            {
                rtk->ssat[satj - 1].fix_trop = 0.0;
            }

            for (i = 0; i < rtk->nx - k; i++)
            {
                sati = rtk->nxRecordSat[i];
                frqi = rtk->nxRecordFrq[i];
                if (sati == satj && frqi == frqj)
                {
                    rtk->ssat[sati - 1].fix_amb[frqi] = rtk->xp[i + k];
                }
            }
        }
        int    base_sat, ns = n, nu = n;
        int    index1, index2;
        double amb, lam[NFREQ] = {0};
        for (m = 0; m < NSYS; m++)
        {
            for (f = 0; f < NFREQ; f++)
            {
                base_sat = rtk->basePrnLsq[m][f];
                for (i = -1, j = 0; j < ns; j++)
                {
                    if (sat[j] == base_sat)
                    {
                        i = j;
                        break;
                    }
                }
                if (i == -1)
                {
                    continue;
                }
                for (j = 0; j < ns; j++)
                {
                    sys = satsys(sat[j], &prn);
                    if (!test_sys(sys, m))
                    {
                        continue;
                    }
                    if (sat[j] == 0 || sat[j] == sat[i])
                    {
                        continue;
                    }
                    if (rtk->ssat[sat[j] - 1].fix_amb[f] == 9999.9)
                    {
                        continue;
                    }
                    if ((rtk->ssat[sat[j] - 1].slip[f] & 1) || (rtk->ssat[sat[j] - 1].slip[f] & 2))
                    {
                        rtk->ssat[sat[j] - 1].fix_amb[f] = 9999.9;
                        continue;
                    }
                    rtk->ssat[sat[j] - 1].ddAmb[f] =
                        rtk->ssat[sat[i] - 1].fix_amb[f] - rtk->ssat[sat[j] - 1].fix_amb[f];
                }
            }
        }
        int    m, f;
        double dL1, dLb1, ddL1, dL2, dLb2, ddL2, ddN1, ddN2;
        double ddion, f1, f2, f3, f4, f5, f6;
        double dr, drb, ddr, ddtrp;
        for (m = 0; m < NSYS; m++)
        {
            base_sat = rtk->basePrnLsq[m][0];
            for (i = -1, j = 0; j < ns; j++)
            {
                sys = satsys(sat[j], &prn);
                if (!test_sys(sys, m))
                {
                    continue;
                }
                if (sat[j] == base_sat)
                {
                    i = j;
                    break;
                }
            }
            if (i == -1)
            {
                continue;
            }
            for (j = 0; j < ns; j++)
            {
                sys = satsys(sat[j], &prn);
                getWavelength(sys, prn, lam);
                if (!test_sys(sys, m))
                {
                    continue;
                }
                if (sat[j] == 0 || sat[j] == sat[i])
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].fix_amb[0] == 9999.9)
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].ph[0][0] == 0.0 || rtk->ssat[sat[j] - 1].ph[1][0] == 0.0)
                {
                    continue;
                }
                index1 = 0;
                index2 = 0;
                for (f = 1; f < NFREQ; f++)
                {
                    if (rtk->ssat[sat[j] - 1].ph[0][f] != 0.0 &&
                        rtk->ssat[sat[j] - 1].ph[1][f] != 0.0)
                    {
                        index2 = f;
                        break;
                    }
                }
                if (index2 == 0)
                {
                    continue;
                }
                dL1  = rtk->ssat[sat[j] - 1].ph[0][0] - rtk->ssat[sat[j] - 1].ph[1][0];
                dLb1 = rtk->ssat[sat[i] - 1].ph[0][0] - rtk->ssat[sat[i] - 1].ph[1][0];
                ddL1 = (dLb1 - dL1) * lam[0];
                dL2  = rtk->ssat[sat[j] - 1].ph[0][index2] - rtk->ssat[sat[j] - 1].ph[1][index2];
                dLb2 = rtk->ssat[sat[i] - 1].ph[0][index2] - rtk->ssat[sat[i] - 1].ph[1][index2];
                if (rtk->ssat[sat[i] - 1].ph[0][index2] == 0 ||
                    rtk->ssat[sat[i] - 1].ph[1][index2] == 0)
                {
                    rtk->ssat[sat[j] - 1].ddion = 0.0;
                    continue;
                }
                ddL2  = (dLb2 - dL2) * lam[index2];
                ddN1  = rtk->ssat[sat[j] - 1].ddAmb[0];
                ddN2  = rtk->ssat[sat[j] - 1].ddAmb[index2];
                f1    = CLIGHT / lam[0];
                f2    = CLIGHT / lam[index2];
                ddion = (SQR(f2) / (SQR(f1) - SQR(f2))) *
                        (ddL1 - ddL2 - (lam[0] * ddN1 - lam[index2] * ddN2));
                rtk->ssat[sat[j] - 1].ddion     = ddion;
                rtk->ssat[sat[j] - 1].ddionTime = rtk->sol.time;
                rtk->ssat[sat[j] - 1].ddtrp     = ddion;
                // printf("sat=%3d ion=%10.3f\n", sat[j], ddion);
                dr    = rtk->ssat[sat[j] - 1].dist[0] - rtk->ssat[sat[j] - 1].dist[1];
                drb   = rtk->ssat[sat[i] - 1].dist[0] - rtk->ssat[sat[i] - 1].dist[1];
                ddr   = drb - dr;
                ddtrp = (SQR(f1) / (SQR(f1) - SQR(f2))) * (ddL1 - lam[0] * ddN1) -
                        (SQR(f2) / (SQR(f1) - SQR(f2))) * (ddL2 - lam[index2] * ddN2) - ddr;
                rtk->ssat[sat[j] - 1].ddtrp = ddtrp;
            }
        }
        rtk->fix_state = 1;
    }
    else
    {
        rtk->fix_state = 0;
    }
#endif
}

static double prectrop(
    rtk_t* rtk, const double* pos, int r, const double* azel, const prcopt_t* opt, const double* x,
    double* dtdx
)
{
    double m_w = 0.0;
    int    i   = rtk->np + r;
    /* wet mapping function */
    tropmapf(rtk->sol.time, pos, azel, &m_w);
    dtdx[0] = m_w;
    return m_w * x[i];
}

static int check_res(
    rtk_t* rtk, const obsd_t* obs, const double* x, unsigned char* sat, double* y,
    unsigned char* iu, unsigned char* ir, int ns, double* azel
)
{
    unsigned char i, j, k, nv = 0, m, index1, index2, sati, satj, frqi, frqj, flag, f, sysi, sysj,
                           prni, prnj, nf = rtk->opt.nf;
    double lami[NFREQ], lamj[NFREQ], thres, C1, C2, posu[3], posr[3];
    int    nvCar = 0, na = rtk->np + rtk->ni + rtk->nt;

    trace(2, "checkres(), na, %d\n", na);

    double *tropu, *tropr;
    double *dtdxu, *dtdxr;

    double        v[MAXOBS]        = {0};
    unsigned char sortVsat[MAXOBS] = {0};
    int           vi               = 0;

    tropu = zeros(SELETE_SAT_NUM, 1);
    tropr = zeros(SELETE_SAT_NUM, 1);
    dtdxu = zeros(SELETE_SAT_NUM, 1);
    dtdxr = zeros(SELETE_SAT_NUM, 1);

    ecef2pos(x, posu);
    ecef2pos(rtk->rb, posr);
    if (rtk->opt.tropopt == TROPOPT_EST)
    {
        for (i = 0; i < ns; i++)
        {
            tropu[i] = prectrop(rtk, posu, 0, azel + iu[i] * 2, &rtk->opt, x, dtdxu + i);
            tropr[i] = prectrop(rtk, posr, 1, azel + ir[i] * 2, &rtk->opt, x, dtdxr + i);
        }
    }

    rtk->sumPostCarV = 0.0;
    for (m = 0; m < NSYS; m++)
    {
        for (f = 0; f < nf; f++)
        {
            for (i = 0; i < ns; i++)
            {
                if (rtk->base_prn[m][f] == sat[i])
                {
                    flag = 1;
                    break;
                }
                else
                {
                    flag = 0;
                }
            }
            /* make double difference */
            for (j = 0; j < ns && flag == 1; j++)
            {
                if (i == j)
                {
                    continue;
                }
                sysi = satsys(sat[i], &prni);
                sysj = satsys(sat[j], &prnj);
                if (!test_sys(sysj, m))
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].vs != 1)
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].slip[f] & 1)
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].slip[f] & 2)
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].vsat[f] == 0)
                {
                    continue;
                }
                if (!validobsLP(iu[j], ir[j], f, nf, y))
                {
                    continue;
                }

                if (sysi == SYS_GPS || sysi == SYS_QZS)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_gpsLam[k];
                    }
                }
                else if (sysi == SYS_GLO)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = g_gloLam[prni - 1][k];
                    }
                    for (k = 0; k < NFREQ; k++)
                    {
                        lamj[k] = g_gloLam[prnj - 1][k];
                    }
                }
                else if (sysi == SYS_GAL)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_galLam[k];
                    }
                }
                else
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_bdsLam[k];
                    }
                }

                /* double-differenced residual */
                rtk->v[nv] = (y[f + iu[i] * nf * 2] - y[f + ir[i] * nf * 2]) -
                             (y[f + iu[j] * nf * 2] - y[f + ir[j] * nf * 2]);

                if (rtk->opt.tropopt == TROPOPT_EST)
                {
                    rtk->v[nv] -= (tropu[i] - tropu[j]) - (tropr[i] - tropr[j]);
                    rtk->ssat[sat[j] - 1].ddtrp = (tropu[i] - tropu[j]) - (tropr[i] - tropr[j]);
                }
                if (sysj == SYS_GLO)
                {
                    C1 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);
                    C2 = SQR(lamj[f % nf] / lamj[0]) * (f / nf == 0 ? -1.0 : 1.0);
                }
                else
                {
                    C1 = C2 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);
                }

                if (rtk->opt.ionoopt == IONOOPT_EST)
                {
                    rtk->v[nv] -= C1 * x[rtk->np + rtk->nt + i] - C2 * x[rtk->np + rtk->nt + j];
                }
                for (index1 = na; index1 < rtk->nx; index1++)
                {
                    sati = rtk->nxRecordSat[index1 - na];
                    frqi = rtk->nxRecordFrq[index1 - na];
                    if (frqi != f)
                    {
                        continue;
                    }
                    if (sati == sat[i])
                    {
                        break;
                    }
                }
                for (index2 = na; index2 < rtk->nx; index2++)
                {
                    satj = rtk->nxRecordSat[index2 - na];
                    frqj = rtk->nxRecordFrq[index2 - na];

                    if (frqj != f)
                    {
                        continue;
                    }
                    if (satj == sat[j])
                    {
                        break;
                    }
                }
                //trace(
                //    3, "nxRecord, check_res(), index1,%d, index2, %d, %d,%d\n", index1, index2,
                //    rtk->ssat[sat[i] - 1].xIndex[f], rtk->ssat[sat[j] - 1].xIndex[f]
                //);
                rtk->v[nv] -= (lami[f] * x[index1] - lami[f] * x[index2]);
                rtk->v[nv] -= (lami[f] - lamj[f]) * x[index2];
                // if (rtk->opt.nf == 1) {
                //     if (rtk->ssat[sat[j] - 1].azel[0][1] * R2D > 30)
                //         thres = lami[f] / 4;
                //     else
                //         thres = lami[f] / 2;
                // }
                // else {
                //     thres = lami[f];
                // }
                // thres = lami[f] * 0.24;
                rtk->Ri[nv] = varrL(&obs[iu[i]], azel[1 + iu[i] * 2], 0, f, &rtk->opt);

                if (f == 0)
                {
                    v[vi]        = fabs(rtk->v[nv]);
                    sortVsat[vi] = sat[j];
                    vi++;
                }
                if (fabs(rtk->v[nv]) > 0.09)
                {
                    rtk->ssat[sortVsat[i] - 1].vs = 0;
                    trace(
                        2, "fix error v=%lf el=%lf\r\n", rtk->v[nv],
                        rtk->ssat[sat[j] - 1].azel[0][1] * R2D
                    );
                    for (k = na; k < rtk->nx; k++)
                    {
                        if (rtk->nxRecordSat[k - na] == sat[j])
                        {
                            initx(rtk, 0, 0, k);
                            initxp(rtk, 0, 0, k);
                        }
                    }
                    if (rtk->opt.ionoopt == IONOOPT_EST)
                    {
                        for (k = 0; k < rtk->ns; k++)
                        {
                            if (rtk->nsSat[k] == sat[j])
                            {
                                initx(rtk, 0, 0, (k + rtk->np + rtk->nt));
                                initxp(rtk, 0, 0, (k + rtk->np + rtk->nt));
                            }
                        }
                    }
                    continue;
                }
                // if (fabs(rtk->v[nv]) > 0.2) {
                //     trace(2, "***fix error v=%lf el=%lf\r\n", rtk->v[nv],
                //     rtk->ssat[sat[j] - 1].azel[0][1] * R2D); rtk->fix_state = 0;
                //     return 1;
                // }
                // if (fabs(rtk->v[nv]) > 0.02) {
                // rtk->ssat[sat[j] - 1].vs = 0;
                //}
                // printf("sys=%3d prn=%3d index1=%d index2=%d v[%2d]=%.4lf\n", sysj,
                // prnj,nv, index1, index2,rtk->v[nv]);
                rtk->sumPostCarV += fabs(rtk->v[nv]);
                nvCar++;

                nv++;
            }
        }
    }
    // double tmp;
    // for (i = 0; i < vi; i++) {
    //     for (j = i + 1; j < vi; j++) {
    //         if (v[i] < v[j]) {
    //             tmp = v[j];
    //             v[j] = v[i];
    //             v[i] = tmp;

    //            satj = sortVsat[j];
    //            sortVsat[j] = sortVsat[i];
    //            sortVsat[i] = satj;
    //        }
    //    }
    //}
    // for (i = 0; i < vi; i++) {
    //    if (v[i] > 0.01) {
    //        rtk->ssat[sortVsat[i] - 1].vs = 0;
    //    }
    //}

    if (nvCar != 0)
    {
        rtk->sumPostCarV = rtk->sumPostCarV / nvCar;
        trace(0x04, "sumPostCarV=%.3f\n", rtk->sumPostCarV);
    }

    free(tropu);
    free(tropr);
    free(dtdxu);
    free(dtdxr);
    return 0;
}
static double ionmapf(const double* pos, const double* azel)
{
    if (pos[2] >= HION)
    {
        return 1.0;
    }
    return 1.0 / cos(asin((RE_WGS84 + pos[2]) / (RE_WGS84 + HION) * sin(PI / 2.0 - azel[1])));
}
static int Rweight(int vcnt)
{
    if (vcnt <= 30)
    {
        return 10;
    }
    else if (vcnt <= 60)
    {
        return 8;
    }
    else if (vcnt < 120)
    {
        return 4;
    }
    else
    {
        return 1;
    }
}
static int ddres(
    int post, rtk_t* rtk, const obsd_t* obs, double dt, const double* x, const double* P,
    const unsigned char* sat, double* y, double* e, double* azel, const unsigned char* iu,
    const unsigned char* ir, int ns, double* standard_v, unsigned char* robust, int* vflg
)
{
    prcopt_t* opt = &rtk->opt;
    double    bl, dr[3], posu[3], posr[3];
    ;
    double lami[NFREQ], lamj[NFREQ], C1, C2, *Hi = NULL;
    // double *tropu = NULL, * tropr = NULL, * dtdxu = NULL, * dtdxr = NULL;
    double *tropu, *tropr;
    double *dtdxu, *dtdxr;
    int i, j, k, sati, frqi, satj, frqj, index1, index2, m, f, nv = 0, nb[NFREQ * 4 * 2 + 2] = {0},
                                                               b = 0, sysi, sysj, nf = rtk->opt.nf;
    int           vmax_sat = 1, stat = 1, vmaxj = 0, type, freq, na = rtk->np + rtk->nt + rtk->ni;
    double        p1, p2, vmax = 0.0;
    unsigned char prni, prnj;
    char*         stype = "";
    bl                  = baseline(x, rtk->rb, dr);
    ecef2pos(x, posu);
    ecef2pos(rtk->rb, posr);
    sysi = sysj = SYS_GPS;

    tropu = zeros(SELETE_SAT_NUM, 1);
    tropr = zeros(SELETE_SAT_NUM, 1);
    dtdxu = zeros(SELETE_SAT_NUM, 1);
    dtdxr = zeros(SELETE_SAT_NUM, 1);

    if (rtk->opt.tropopt == TROPOPT_EST)
    {
        for (i = 0; i < ns; i++)
        {
            tropu[i] = prectrop(rtk, posu, 0, azel + iu[i] * 2, opt, x, dtdxu + i);
            tropr[i] = prectrop(rtk, posr, 1, azel + ir[i] * 2, opt, x, dtdxr + i);
        }
    }
    for (i = 0; i < MAXSAT; i++)
    {
        for (j = 0; j < NFREQ; j++)
        {
            rtk->ssat[i].vsat[j] = 0;
            // rtk->ssat[i].resc[j] = 0.0;
        }
    }
    trace(2, "ddres, opt->mode, %d, PMODE_DGPS, %d\n", opt->mode, PMODE_DGPS);
    for (f = opt->mode > PMODE_DGPS ? 0 : nf; f < nf * 2; f++)
    {
        // if (rtk->nx == nv)break;
        // if (f >= nf) break;
        if (nv >= NY)
        {
            break;
        }
        for (m = 0; m < NSYS; m++)
        {
            // if (rtk->nx == nv)break;
            if (nv >= NY)
            {
                break;
            }
            /* search reference satellite with highest elevation */
            for (i = -1, j = 0; j < ns; j++)
            {
                sysi = satsys(sat[j], &prni);
                if (!test_sys(sysi, m))
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].vs != 1)
                {
                    continue;
                }
                // if (rtk->ssat[sat[j] - 1].useCnt[f%nf] < 1 &&
                // findUseCntFlag(rtk,f%nf)) continue;
                if (opt->mode > PMODE_DGPS)
                {
                    if (rtk->allSlipFlag[m][0 % nf] == 0)
                    {
                        if (rtk->ssat[sat[j] - 1].slip[f % nf] & 1)
                        {
                            continue;
                        }
                        if (rtk->ssat[sat[j] - 1].slip[f % nf] & 2)
                        {
                            continue;
                        }
                    }
                    if (!validobsLP(iu[j], ir[j], f, nf, y))
                    {
                        continue;
                    }
                }
                else
                {
                    if (!validobsP(iu[j], ir[j], f, nf, y))
                    {
                        continue;
                    }
                }

                if (f > 0)
                {
                    if (rtk->base_prn[m][0] == sat[j])
                    {
                        i = j;
                        break;
                    }
                }
                if (i < 0 || azel[1 + iu[j] * 2] >= azel[1 + iu[i] * 2])
                {
                    i = j;
                }
            }
            if (i < 0)
            {
                continue;
            }
            if (f < nf)
            {
                rtk->base_prn[m][f] = sat[i];
            }
            /* make double difference */
            for (j = 0; j < ns; j++)
            {
                sysi = satsys(sat[i], &prni);
                sysj = satsys(sat[j], &prnj);
                if (!test_sys(sysj, m))
                {
                    continue;
                }
                if (rtk->ssat[sat[j] - 1].vs != 1)
                {
                    continue;
                }
                if (opt->mode > PMODE_DGPS)
                {
                    // if (rtk->ssat[sat[j] - 1].slip[f % nf] & 1) continue;
                    if (rtk->ssat[sat[j] - 1].slip[f % nf] & 2)
                    {
                        continue;
                    }
                    if (!validobsLP(iu[j], ir[j], f, nf, y))
                    {
                        continue;
                    }
                }
                else
                {
                    if (!validobsP(iu[j], ir[j], f, nf, y))
                    {
                        continue;
                    }
                }
                if (i == j)
                {
                    continue;
                }

                if (sysi == SYS_GPS || sysi == SYS_QZS)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_gpsLam[k];
                    }
                }
                else if (sysi == SYS_GLO)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = g_gloLam[prni - 1][k];
                    }
                    for (k = 0; k < NFREQ; k++)
                    {
                        lamj[k] = g_gloLam[prnj - 1][k];
                    }
                }
                else if (sysi == SYS_GAL)
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_galLam[k];
                    }
                }
                else
                {
                    for (k = 0; k < NFREQ; k++)
                    {
                        lami[k] = lamj[k] = g_bdsLam[k];
                    }
                }
                if (f < nf)
                {
                    if (lami[f] == 0.0 || lamj[f] == 0.0)
                    {
                        continue;
                    }
                }
                if (rtk->opt.mode == PMODE_DGPS)
                {
                    Hi = rtk->H + nv * 3;
                }
                else
                {
                    Hi = rtk->H + nv * rtk->nx;
                }
                /* double-differenced residual */
                rtk->v[nv] = (y[f + iu[i] * nf * 2] - y[f + ir[i] * nf * 2]) -
                             (y[f + iu[j] * nf * 2] - y[f + ir[j] * nf * 2]);

                for (k = 0; k < 3; k++)
                {
                    Hi[k] = -e[k + iu[i] * 3] + e[k + iu[j] * 3];
                }
                /* double-differenced tropospheric delay term */
                if (opt->tropopt == TROPOPT_EST)
                {
                    rtk->v[nv] -= (tropu[i] - tropu[j]) - (tropr[i] - tropr[j]);
                    rtk->ssat[sat[j] - 1].ddtrp = (tropu[i] - tropu[j]) - (tropr[i] - tropr[j]);
                    for (k = 0; k < 1; k++)
                    {
                        Hi[rtk->np + k]     = (dtdxu[k + i] - dtdxu[k + j]);
                        Hi[rtk->np + k + 1] = -(dtdxr[k + i] - dtdxr[k + j]);
                    }
                }
                if (sysj == SYS_GLO)
                {
                    C1 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);
                    C2 = SQR(lamj[f % nf] / lamj[0]) * (f / nf == 0 ? -1.0 : 1.0);
                }
                else
                {
                    C1 = C2 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);
                }

                if (opt->ionoopt == IONOOPT_EST)
                {
                    rtk->v[nv] -= C1 * x[rtk->np + rtk->nt + i] - C2 * x[rtk->np + rtk->nt + j];
                    Hi[rtk->np + rtk->nt + i]  = C1;
                    Hi[rtk->np + rtk->nt + j]  = -C2;
                    rtk->ssat[sat[i] - 1].dion = x[rtk->np + rtk->nt + i];
                    rtk->ssat[sat[j] - 1].dion = x[rtk->np + rtk->nt + j];
                }
                /* double-differenced phase-bias term */
                if (f < nf)
                {
                    // rtk->v[nv] -= lami * rtk->ssat[sat[i] - 1].dbias[f] - lamj *
                    // rtk->ssat[sat[j] - 1].dbias[f];
                    for (index1 = na; index1 < rtk->nx; index1++)
                    {
                        sati = rtk->nxRecordSat[index1 - na];
                        frqi = rtk->nxRecordFrq[index1 - na];
                        if (frqi != f)
                        {
                            continue;
                        }
                        if (sati == sat[i])
                        {
                            break;
                        }
                    }
                    for (index2 = na; index2 < rtk->nx; index2++)
                    {
                        satj = rtk->nxRecordSat[index2 - na];
                        frqj = rtk->nxRecordFrq[index2 - na];

                        if (frqj != f)
                        {
                            continue;
                        }
                        if (satj == sat[j])
                        {
                            break;
                        }
                    }
                    rtk->v[nv] -= (lami[f] * x[index1] - lami[f] * x[index2]);
                    rtk->v[nv] -= (lami[f] - lamj[f]) * x[index2];
                    if (rtk->opt.bl <= BSLTHRESHOLD && baseXyzError == 0)
                    {
                        if (post == 0 && f < nf && rtk->sol.fixxyz[0] != 0.0 &&
                            rtk->sol.rr_smooth_cnt > 10)
                        {
                            // rtk->ssat[sat[j] - 1].resc[f] = x[index1] - x[index2] -
                            // ROUND(x[index1] - x[index2]); rtk->ssat[sat[j] - 1].resc[f] =
                            // (x[index1] - x[index2]);
                            rtk->ssat[sat[j] - 1].resc[f] =
                                rtk->ssat[sat[i] - 1].fbias[f] - rtk->ssat[sat[j] - 1].fbias[f] -
                                ROUND(
                                    rtk->ssat[sat[i] - 1].fbias[f] - rtk->ssat[sat[j] - 1].fbias[f]
                                );

                            // trace(0x04, "outlier rejected half slip(sat=%3d-%3d %s%d v=%.3f
                            // snr=%.2f %.2f)\n",
                            //     sat[i], sat[j], f < nf ? "L" : "P", f% nf + 1,
                            //     fabs(rtk->ssat[sat[j] - 1].resc[f]), obs[iu[i]].SNR[f % nf]
                            //     / 4.0, obs[iu[j]].SNR[f % nf] / 4.0);

                            if (fabs(rtk->ssat[sat[j] - 1].resc[f]) > 0.35 &&
                                fabs(rtk->ssat[sat[j] - 1].resc[f]) < 0.75)
                            {
                                memset(rtk->Ri, 0, sizeof(double) * NY);
                                memset(rtk->Rj, 0, sizeof(double) * NY);
                                trace(
                                    0x04,
                                    "outlier rejected half slip(sat=%3d-%3d %s%d v=%.3f "
                                    "snr=%.2f %.2f)\n",
                                    sat[i], sat[j], f < nf ? "L" : "P", f % nf + 1,
                                    fabs(rtk->ssat[sat[j] - 1].resc[f]),
                                    obs[iu[i]].SNR[f % nf] / 4.0, obs[iu[j]].SNR[f % nf] / 4.0
                                );
                                if (f == 0)
                                {
                                    for (k = na; k < rtk->nx; k++)
                                    {
                                        if (rtk->nxRecordSat[k - na] == sat[j])
                                        {
                                            trace(
                                                2, "ddres, k, %d, xIndex, %d\n",
                                                rtk->ssat[sat[j] - 1].xIndex[f]
                                            );
                                            initx(rtk, 0, 0, k);
                                        }
                                    }
                                    if (rtk->opt.ionoopt == IONOOPT_EST)
                                    {
                                        for (k = 0; k < rtk->ns; k++)
                                        {
                                            if (rtk->nsSat[k] == sat[j])
                                            {
                                                initx(rtk, 0, 0, (k + rtk->np + rtk->nt));
                                                // printf("inixp j;%d\n", (j + rtk->np + rtk->nt));
                                            }
                                        }
                                    }
                                    rtk->ssat[sat[j] - 1].vs = 0;
                                    halfSlip++;
                                }
                                else
                                {
                                    for (k = na; k < rtk->nx; k++)
                                    {
                                        if (rtk->nxRecordSat[k - na] == sat[j] &&
                                            rtk->nxRecordFrq[k - na] == sat[j])
                                        {
                                            initx(rtk, 0, 0, k);
                                        }
                                    }
                                    rtk->ssat[sat[j] - 1].slip[f] = 3;
                                }
                                rtk->sol.ilterCout = 0;
                                free(tropu);
                                free(tropr);
                                free(dtdxu);
                                free(dtdxr);
                                return -2;
                            }
                        }
                    }
                    Hi[index1] = lami[f];
                    Hi[index2] = -lami[f];
                }
                if (post == 0 && fabs(rtk->v[nv]) > 50.0)
                {
                    rtk->ssat[sat[j] - 1].vs = 0;
                    memset(rtk->Ri, 0, sizeof(double) * NY);
                    memset(rtk->Rj, 0, sizeof(double) * NY);
                    trace(
                        0x04, "outlier rejected (sat=%3d-%3d %s%d v=%.3f)\n", sat[i], sat[j],
                        f < nf ? "L" : "P", f % nf + 1, rtk->v[nv]
                    );
                    for (k = na; k < rtk->nx; k++)
                    {
                        if (rtk->nxRecordSat[k - na] == sat[j])
                        {
                            initx(rtk, 0, 0, k);
                        }
                    }
                    if (rtk->opt.ionoopt == IONOOPT_EST)
                    {
                        for (k = 0; k < rtk->ns; k++)
                        {
                            if (rtk->nsSat[k] == sat[j])
                            {
                                initx(rtk, 0, 0, (k + rtk->np + rtk->nt));
                                // printf("inixp j;%d\n", (j + rtk->np + rtk->nt));
                            }
                        }
                    }
                    free(tropu);
                    free(tropr);
                    free(dtdxu);
                    free(dtdxr);
                    if (rtk->sol.rr_smooth_cnt > 10)
                    {
                        rejectcount++;
                    }
                    return -2;
                }
                //if (f < nf && post == 0)
                //{
                //    trace(
                //        2,
                //        "f=%d nv=%3d sys=%3d prn=%3d x[%3d]=%7.2f x[%3d]=%7.2f "
                //        "dbias=%7.2f v[%2d]=%10.4lf\n",
                //        f, nv, sysj, prnj, index1, x[index1], index1, x[index2],
                //        x[index1] - x[index2], nv, rtk->v[nv]
                //    );
                //}
                if (!post && robust[nv] == 0)
                {
                    rtk->Rj[nv] = varrL(&obs[iu[j]], azel[1 + iu[j] * 2], bl, f, opt);
                    // rtk->Rj[nv] = varerr_gamit(sat[j], sysj, azel[1 + iu[j] * 2], bl,
                    // dt, f, opt);
                    if (f >= nf)
                    {
                        rtk->Rj[nv] = rtk->Rj[nv] * SQR(500);
                    }
                }
                rtk->Ri[nv] = varrL(&obs[iu[i]], azel[1 + iu[i] * 2], bl, f, opt);
                // rtk->Ri[nv] = varerr_gamit(sat[i], sysi, azel[1 + iu[i] * 2], bl, dt,
                // f, opt);
                //  if (f >= nf)  rtk->Ri[nv] = rtk->Ri[nv] * SQR(500);
                if (f == 0)
                {
                    rtk->ssat[sat[j] - 1].Ri = rtk->Rj[nv];
                    rtk->ssat[sat[i] - 1].Ri = rtk->Ri[nv];
                }
#if 0
                if (rtk->rsat[sat[i] - 1].vfcnt[f] < 120 || rtk->bsat[sat[i] - 1].vfcnt[f] < 120)
                {
                    rtk->Ri[nv] *= Rweight(rtk->rsat[sat[i] - 1].vfcnt[f]) *
                                   Rweight(rtk->bsat[sat[i] - 1].vfcnt[f]);
                }
                if (rtk->rsat[sat[j] - 1].vfcnt[f] < 120 || rtk->bsat[sat[j] - 1].vfcnt[f] < 120)
                {
                    rtk->Rj[nv] *= Rweight(rtk->rsat[sat[j] - 1].vfcnt[f]) *
                                   Rweight(rtk->bsat[sat[j] - 1].vfcnt[f]);
                }
#endif 

                if (f < nf && rtk->ssat[sat[j] - 1].resc[f] == 0.0)
                {
                    rtk->ssat[sat[j] - 1].resc[f] =
                        rtk->ssat[sat[i] - 1].fbias[f] - rtk->ssat[sat[j] - 1].fbias[f] -
                        ROUND(rtk->ssat[sat[i] - 1].fbias[f] - rtk->ssat[sat[j] - 1].fbias[f]);
                }

                /* set valid data flags */
                if (opt->mode > PMODE_DGPS)
                {
                    if (f < nf)
                    {
                        rtk->ssat[sat[i] - 1].vsat[f] = rtk->ssat[sat[j] - 1].vsat[f] = 1;
                    }
                }
                else
                {
                    rtk->ssat[sat[i] - 1].vsat[f - nf] = rtk->ssat[sat[j] - 1].vsat[f - nf] = 1;
                }
                vflg[nv++] = (sat[i] << 16) | (sat[j] << 8) | ((f < nf ? 0 : 1) << 4) | (f % nf);
                nb[b]++;
                if (nv >= NY)
                {
                    break;
                }
            }
            if (nb[b] != 0)
            {
                b++;
            }
        }
    }

    trace(
        2, "replos, m, %d, nf, %d, ns,%d,opt->nf, %d, nv,%d,f,%d,NY,%d\n", m, nf, ns, opt->nf, nv,
        f, NY
    );

    if (rtk->opt.bl <= BSLTHRESHOLD)
    {
        K0 = 1.5;
        K1 = 3.0;
    }
    else
    {
        K0 = 1.5;
        K1 = 3.0;
    }
    K0 = rtk->opt.iggiiik0;
    K1 = rtk->opt.iggiiik1;

    if (rtk->opt.bl > BSLTHRESHOLD)
    {
        for (j = 0; j < nv && post > 0; j++)
        {
            standard_v[j] = sqrt(rtk->v[j] * rtk->v[j] / (rtk->Ri[j] + rtk->Rj[j]));
            if (standard_v[j] < K0)
            {
                continue;
            }
            if (standard_v[j] > vmax)
            {
                vmax     = standard_v[j];
                vmax_sat = (vflg[j] >> 8) & 0xFF;
                vmaxj    = j;
            }
            stat = 0;
        }
        if (post > 0 && stat == 0)
        {
            j     = vmaxj;
            type  = (vflg[j] >> 4) & 0xF;
            freq  = vflg[j] & 0xF;
            stype = type == 0 ? "L" : (type == 1 ? "L" : "C");
            if (standard_v[j] > K0)
            {
                robust[j] = 1;
                vmax      = standard_v[j];
                stdvMax   = vmax;
                if (vmax <= K0)
                {
                    rtk->Rj[j] = rtk->Rj[j];
                }
                else if (vmax > K0 && vmax <= K1)
                {
                    p1         = 1 / sqrt(rtk->Rj[j]);
                    p2         = p1 * (K0 / vmax) * SQR(((K1 - vmax) / (K1 - K0)));
                    rtk->Rj[j] = SQR((1 / p2));
                    trace(
                        0x04, "post=%d robust1 sat=%d %s%d vmax=%f\n", post, vmax_sat, stype,
                        freq + 1, vmax
                    );
                }
                else if (vmax > 2 * K1)
                {
                    rtk->ssat[vmax_sat - 1].vs = 0;
                    // memset(rtk->Ri, 0, sizeof(rtk->Ri));
                    // memset(rtk->Rj, 0, sizeof(rtk->Rj));
                    memset(rtk->Ri, 0, sizeof(double) * NY);
                    memset(rtk->Rj, 0, sizeof(double) * NY);
                    trace(0x04, "outlier rejected sat=%d stdv=%lf\n", vmax_sat, vmax);
                    // if (rtk->opt.tropopt == TROPOPT_EST) {
                    //     free(tropu); free(tropr);free(dtdxu);free(dtdxr);
                    // }
                    for (i = rtk->np + rtk->nt + rtk->ni; i < rtk->nx; i++)
                    {
                        if (rtk->nxRecordSat[i - (rtk->np + rtk->nt + rtk->ni)] == vmax_sat)
                        {
                            initx(rtk, 0, 0, i);
                        }
                    }
                    if (rtk->opt.ionoopt == IONOOPT_EST)
                    {
                        for (j = 0; j < rtk->ns; j++)
                        {
                            if (rtk->nsSat[j] == vmax_sat)
                            {
                                initx(rtk, 0, 0, (j + rtk->np + rtk->nt));
                                // printf("inixp j;%d\n", (j + rtk->np + rtk->nt));
                            }
                        }
                    }
                    free(tropu);
                    free(tropr);
                    free(dtdxu);
                    free(dtdxr);
                    return -2;
                }
                else
                {
                    rtk->Rj[j] = 10E8;
                    trace(
                        0x04, "post=%d robust2 sat=%d %s%d vmax=%f\n", post, vmax_sat, stype,
                        freq + 1, vmax
                    );
                }
            }
        }
    }
    else
    {
        for (j = 0; j < nv && post > 0; j++)
        {
            standard_v[j] = sqrt(rtk->v[j] * rtk->v[j] / (rtk->Ri[j] + rtk->Rj[j]));
            if (standard_v[j] < K0)
            {
                continue;
            }
            stat = 0;
        }
        if (post > 0 && stat == 0)
        {
            for (j = 0; j < nv; j++)
            {
                if (standard_v[j] > 2 * K1 && standard_v[j] > vmax)
                {
                    vmax     = standard_v[j];
                    vmaxj    = j;
                    vmax_sat = (vflg[j] >> 8) & 0xFF;
                    type     = (vflg[j] >> 4) & 0xF;
                    freq     = vflg[j] & 0xF;
                    stype    = type == 0 ? "L" : (type == 1 ? "L" : "C");
                }
            }
            if (vmax != 0)
            {
                rtk->ssat[vmax_sat - 1].vs = 0;
                trace(
                    4, "outlier rejected sat=%d v=%.2f std=%.3f stdv=%.2f\n", vmax_sat,
                    rtk->v[vmaxj], sqrt(rtk->Ri[vmaxj]), vmax
                );

                memset(rtk->Ri, 0, sizeof(double) * NY);
                memset(rtk->Rj, 0, sizeof(double) * NY);
                for (i = rtk->np + rtk->nt + rtk->ni; i < rtk->nx; i++)
                {
                    if (rtk->nxRecordSat[i - (rtk->np + rtk->nt + rtk->ni)] == vmax_sat)
                    {
                        initx(rtk, 0, 0, i);
                    }
                }
                if (rtk->opt.ionoopt == IONOOPT_EST)
                {
                    for (j = 0; j < rtk->ns; j++)
                    {
                        if (rtk->nsSat[j] == vmax_sat)
                        {
                            initx(rtk, 0, 0, (j + rtk->np + rtk->nt));
                            // printf("inixp j;%d\n", (j + rtk->np + rtk->nt));
                        }
                    }
                }
                free(tropu);
                free(tropr);
                free(dtdxu);
                free(dtdxr);
                return -2;
            }
            if (vmax == 0)
            {
                for (j = 0; j < nv; j++)
                {
                    if (standard_v[j] > K0)
                    {
                        robust[j] = 1;
                        vmax      = standard_v[j];
                        vmax_sat  = (vflg[j] >> 8) & 0xFF;
                        type      = (vflg[j] >> 4) & 0xF;
                        freq      = vflg[j] & 0xF;
                        stype     = type == 0 ? "L" : "C";
                        if (vmax > stdvMax)
                        {
                            stdvMax = vmax;
                        }
                        if (vmax <= K0)
                        {
                            rtk->Rj[j] = rtk->Rj[j];
                        }
                        else if (vmax > K0 && vmax <= K1)
                        {
                            p1         = 1 / sqrt(rtk->Rj[j]);
                            p2         = p1 * (K0 / vmax) * SQR(((K1 - vmax) / (K1 - K0)));
                            rtk->Rj[j] = SQR((1 / p2));
                            trace(
                                8, "post=%d robust1 sat=%d %s%d v=%.2f std=%.3f stdv=%.2f\n", post,
                                vmax_sat, stype, freq + 1, rtk->v[j], sqrt(rtk->Ri[j]), vmax
                            );
                        }
                        else
                        {
                            rtk->Rj[j] = 10E8;
                            trace(
                                8, "post=%d robust2 sat=%d %s%d v=%.2f std=%.3f stdv=%.2f\n", post,
                                vmax_sat, stype, freq + 1, rtk->v[j], sqrt(rtk->Ri[j]), vmax
                            );
                        }
                    }
                }
            }
        }
    }
    // if (rtk->opt.tropopt == TROPOPT_EST) {
    //     free(tropu); free(tropr);free(dtdxu);free(dtdxr);
    // }
    /* double-differenced measurement error covariance */
    ddcov(nb, b, rtk->Ri, rtk->Rj, nv, rtk->R);
    /* end of system loop */
    free(tropu);
    free(tropr);
    free(dtdxu);
    free(dtdxr);
    return post ? stat : nv;
}

/* update one sat time series statictals */
static int sat_ts_update(const obsd_t* obsd, satts_t* satts, double tint)
{
    int     i, j, freq;
    int     m, n;
    satts_t satts0 = {0.0};

    double tt = 0.0;

    tt = timediff(obsd->time, satts->data[0].time);

    if (tt > 0 && fabs(tt - tint) < 10E-3)
    {
        satts->data[2] = satts->data[1];
        satts->data[1] = satts->data[0];
    }
    else
    {
        /* shift data */
        *satts = satts0;
    }
    satts->data[0] = *obsd;

    satts->vcnt++;
    satts->nfreq = 0;
    /* reset valid count */
    for (i = 0; i < NFREQ + NEXOBS; i++)
    {
        if (obsd->L[i] != 0.0 && obsd->P[i] != 0.0 && obsd->SNR[i] != 0.0)
        {
            satts->vfcnt[i]++;
            satts->nfreq++;
        }
        satts->L3d[i] = 0.0;
        satts->P3d[i] = 0.0;
        satts->D3d[i] = 0.0;
        satts->S3d[i] = 0.0;
        if (satts->vfcnt[i] < 3)
        {
            continue;
        }
        satts->L3d[i] = satts->data[0].L[i] + satts->data[2].L[i] - 2 * satts->data[1].L[i];
        satts->P3d[i] = satts->data[0].P[i] + satts->data[2].P[i] - 2 * satts->data[1].P[i];
        satts->D3d[i] = satts->data[0].D[i] + satts->data[2].D[i] - 2 * satts->data[1].D[i];
        satts->S3d[i] = satts->data[0].SNR[i] - satts->data[1].SNR[i];  // SNR 只用两点差分
    }

    /* update time series max freq */
    if (satts->nfreq > satts->maxfreq)
    {
        satts->maxfreq = satts->nfreq;
    }

    return 0;
}
/* relative positioning ------------------------------------------------------*/
extern int relpos(
    rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr, unsigned char* sat,
    unsigned char* iu, unsigned char* ir, double* rs, double* dts, double* var, int* svh
)
{
    int           i, f, n = nu + nr, nv, vflg[5 + 40 + 40 * NFREQ], info;
    unsigned char ns, nf = rtk->opt.nf;
    unsigned char robust[5 + 40 + 40 * NFREQ] = {0};
    gtime_t       time                        = obs[0].time;
    double *      r, *azel;
    double *      y, *e;
    double        dt = timediff(time, obs[nu].time);
    double *      standard_v, sigma;

    azel       = zeros(2 * SELETE_SAT_NUM * 2, 1);
    y          = zeros(NFREQ * 2 * SELETE_SAT_NUM * 2, 1);
    e          = zeros(3 * SELETE_SAT_NUM * 2, 1);
    standard_v = zeros(NY, 1);
    r          = zeros(SELETE_SAT_NUM * 2, 1);

    for (i = 0; i < NSYS; i++)
    {
        for (f = 0; f < NFREQ * 2; f++)
        {
            rtk->base_prn_fix[i][f] = 0;
        }
    }
    ns = nu;

    for (i = 0; i < nu; i++)
    {
        // if(obs[i].sat!=6) continue;
        // printf("sat,%d,%d,%.3f, %.3f, %.3d\n",obs[i].rcv, obs[i].sat,obs[i].L[0],
        // obs[i].P[0], obs[i].SNR[0]);
        sat_ts_update(&obs[i], &rtk->rsat[obs[i].sat - 1], rtk->opt.timeInterval);
    }

    for (i = nu; i < n; i++)
    {
        sat_ts_update(&obs[i], &rtk->bsat[obs[i].sat - 1], rtk->opt.timeInterval);
    }

    /* undifferenced residuals for base station */
    if (zdres(
            rtk, 1, obs + nu, nr, rs + nu * 6, dts + nu * 2, svh + nu, rtk->rb, &rtk->opt, 1,
            y + nu * nf * 2, e + nu * 3, azel + nu * 2, r + nu
        ))
    {
        trace(0x02, "initial base station position error\n");
        free(y);
        free(e);
        free(azel);
        free(standard_v);
        free(r);
        rtk->sol.stat = SOLQ_NONE;
        return 1;
    }
    if (zdres(rtk, 0, obs, nu, rs, dts, svh, rtk->sol.rr, &rtk->opt, 0, y, e, azel, r))
    {
        free(y);
        free(e);
        free(azel);
        free(standard_v);
        free(r);
        trace(0x02, "initial rover station position error\n");
        rtk->sol.stat = SOLQ_NONE;
        return 1;
    }
    for (i = 0; i < NY; i++)
    {
        robust[i] = 0;
    }
    /* temporal update of states */
    udstate(rtk, obs, sat, iu, ir, ns, r);

    trace(
        2,
        "kalman xyz before while, %14.4f %14.4f %14.4f rb, %14.4f, %14.4f, "
        "%14.4f \n",
        rtk->x[0], rtk->x[1], rtk->x[2], rtk->rb[0], rtk->rb[1], rtk->rb[2]
    );
    trace(2, "kalman rtk->x0 =         ");
    tracemat(2, rtk->x, 1, 6, 14, 4);

    rtk->sol.stat      = rtk->opt.mode <= PMODE_DGPS ? SOLQ_DGPS : SOLQ_FLOAT;
    ilterCout          = 0;
    rtk->sol.ilterCout = 0;
    halfSlip           = 0;
    rejectcount        = 0;
    while (1)
    {
        trace(2, "kalman rtk->x, in while, ");
        tracemat(2, rtk->x, 1, 6, 14, 4);

        stdvMax = 0.0;
        /* undifferenced residuals for rover */
        matcpy(rtk->xp, rtk->x, NX, 1);
        matcpy(rtk->Pp, rtk->P, NX, NX);

        ilterCout++;
        rtk->sol.ilterCout++;

        if (ilterCout == 1)
        {
            for (i = 0; i < NY; i++)
            {
                robust[i] = 0;
            }
        }
        for (i = 0; i < NSYS; i++)
        {
            for (f = 0; f < NFREQ * 2; f++)
            {
                rtk->base_prn[i][f] = 0;
            }
        }

        if (rtk->sol.ilterCout > 5)
        {
            trace(2, "ilterCout too more\n");
            rtk->sol.stat = SOLQ_NONE;
            for (i = 0; i < NSYS; i++)
            {
                for (f = 0; f < NFREQ * 2; f++)
                {
                    rtk->base_prn[i][f] = 0;
                }
            }
            break;
        }
        if (zdres(rtk, 0, obs, nu, rs, dts, svh, rtk->x, &rtk->opt, 0, y, e, azel, r))
        {
            trace(0x02, "rover initial position error\n");
            rtk->sol.stat = SOLQ_NONE;
            break;
        }

        memset(rtk->H, 0, sizeof(double) * NX * NY);

        nv = ddres(
            0, rtk, obs, dt, rtk->x, rtk->P, sat, y, e, azel, iu, ir, ns, standard_v, robust, vflg
        );
        if (nv == -2)
        {
            continue;
        }
        if (nv < 4)
        {
            trace(0x02, "nv=%d\n", nv);
            rtk->sol.stat = SOLQ_NONE;
            break;
        }

        trace(2, "relpos, nv,%d, ns,%d, \n", nv, ns);
        trace(2, "relpos rtk->v\n");
        tracemat(2, rtk->v, 1, nv, 8, 4);
        /* kalman filter measurement update */
        if (rtk->opt.mode == PMODE_DGPS)
        {
            if ((info =
                     filter(rtk, rtk->x, rtk->P, rtk->H, rtk->v, rtk->R, 3, nv, rtk->xp, rtk->Pp)))
            {
                trace(0x02, "filter error (info=%d)\n", info);
                rtk->sol.stat = SOLQ_NONE;
                break;
            }
        }
        else
        {
            if ((info = filter(
                     rtk, rtk->x, rtk->P, rtk->H, rtk->v, rtk->R, rtk->nx, nv, rtk->xp, rtk->Pp
                 )))
            {
                trace(0x02, "filter error (info=%d)\n", info);
                rtk->sol.stat = SOLQ_NONE;
                break;
            }
        }

        // trace(2,"rtk->Pp\n");
        // tracemat(2,rtk->Pp, rtk->nx,rtk->nx,8,4);
        // trace(2, "CANT %d,%d, rtk->nx,%d\n", SELETE_SAT_NUM, NX,rtk->nx);
        /* float solution undifferenced residuals for rover */
        if (zdres(rtk, 0, obs, nu, rs, dts, svh, rtk->xp, &rtk->opt, 0, y, e, azel, r))
        {
            rtk->sol.stat = SOLQ_NONE;
            trace(0x02, "rover initial position error\n");
            break;
        }
        nv = ddres(
            1, rtk, obs, dt, rtk->xp, rtk->Pp, sat, y, e, azel, iu, ir, ns, standard_v, robust, vflg
        );

        if (nv == -2)
        {
            ilterCout = 0;
            memset(standard_v, 0, sizeof(standard_v));
            memset(rtk->v, 0, sizeof(rtk->v));
            memset(rtk->R, 0, sizeof(rtk->R));
            continue;
        }
        if (ilterCout < 5 && nv == 0)
        {
            continue;
        }
        else
        {
            trace(0x04, "stdvMax=%f inter=%d:%d\n", stdvMax, ilterCout, rtk->sol.ilterCout);
            /* update ambiguity control struct */
            rtk->sol.ns[1] = 0;
            for (i = 0; i < ns; i++)
            {
                for (f = 0; f < rtk->opt.nf; f++)
                {
                    if (rtk->ssat[sat[i] - 1].vsat[f] == 1)
                    {
                        rtk->sol.ns[1]++;
                        break;
                    }
                }
            }
            /* lack of valid satellites */
            if (rtk->sol.ns[1] < 4)
            {
                trace(0x02, "**********ns=%d*********\n", rtk->sol.ns[1]);
                rtk->sol.stat = SOLQ_NONE;
                break;
            }
            else
            {
                // rtk->sol.stat = SOLQ_FLOAT;
                break;
            }
        }
    }

    char ts[64];
    time2str(rtk->sol.time, ts, 3);
    trace(
        2,
        "kalman xyz after while , %s, %14.4f, %14.4f, %14.4f, %d,%d, %14.4f, "
        "%14.4f, %14.4f,\n",
        ts, rtk->x[0], rtk->x[1], rtk->x[2], rtk->sol.ns[1], ilterCout, rtk->rb[0], rtk->rb[1],
        rtk->rb[2]
    );

    // double enu3[3];
    // char s[64];
    // time2str(time, s,2);
    // pos2enu(rtk->rb, &rtk->sol, enu3);

    // trace(2, "ENU3 : %s, %14.4lf, %14.4lf,%14.4lf,\n",  s,enu3[0], enu3[1],
    // enu3[2]); trace(2, "ENU3 : %s, %14.4lf, %14.4lf,%14.4lf,\n",  s,enu3[0],
    // enu3[1], enu3[2]); trace(2, "ENU3 : %s, %14.4lf, %14.4lf,%14.4lf,\n",
    // s,enu3[0], enu3[1], enu3[2]);

    // findMaxRes(rtk, sat, ns);

    if (rtk->sol.stat == SOLQ_FLOAT)
    {
        rtkAR(rtk, obs, sat, ns);
    }

    trace(
        2,
        "kalman xyz rtkAR , %s, %14.4f, %14.4f, %14.4f, %d,%d, %14.4f, %14.4f, "
        "%14.4f, %14.4f, %14.4f, %14.4f,\n",
        ts, rtk->x[0], rtk->x[1], rtk->x[2], rtk->sol.ns[1], ilterCout, rtk->rb[0], rtk->rb[1],
        rtk->rb[2], rtk->sol.rr[0], rtk->sol.rr[1], rtk->sol.rr[2]
    );

    n = 0;
    if (rtk->opt.ionoopt == IONOOPT_EST)
    {
        for (i = 0; i < MAXSAT; i++)
        {
            if (rtk->ssat[i].ionIndexCnt > 5400.0 / rtk->opt.timeInterval)
            {
                n++;
            }
        }
        trace(0x02, "out float sol:n:%d\n", n);
        if (n > 10)
        {
            rtk->sol.stat = SOLQ_FIX;
            rtk->nfix     = 10;
        }
    }

    if (rtk->sol.fixxyz[0] != 0.0 && rtk->sol.rr_smooth_cnt > 10)
    {
        if (rejectcount > 5)
        {
            largeShift++;
        }
        else
        {
            largeShift = 0;
        }
        if (halfSlip > 5)
        {
            halfSlipCnt++;
        }
        else
        {
            halfSlipCnt = 0;
        }
        if (halfSlipCnt >= 10 || largeShift > 10)
        {
            rtk->sol.fixxyz[0]     = 0.0;
            rtk->sol.fixxyz[1]     = 0.0;
            rtk->sol.fixxyz[2]     = 0.0;
            rtk->sol.rr_smooth_cnt = 0;
        }
    }
    trace(
        0x02, "halfSlip:%d halfSlipCnt:%d rejectcount:%d halfSlipCnt:%d\n", halfSlip, halfSlipCnt,
        rejectcount, largeShift
    );
    if (rtk->sol.stat == SOLQ_FIX)
    {
        double enu3[3];
        char   s[64];
        time2str(rtk->sol.time, s, 3);
        pos2enu(rtk->rb, rtk->x, enu3);
        trace(2, "kalman enu,  %s, %14.4lf, %14.4lf,%14.4lf,\n", s, enu3[0], enu3[1], enu3[2]);
        // trace(2, "kalman xyz, %14.4f, %14.4f, %14.4f
        // \n",rtk->x[0],rtk->x[1],rtk->x[2]);
        if (rtk->sol.fixxyz[0] != 0.0 && rtk->sol.rr_smooth_cnt > 10)
        {
            if (fabs(rtk->x[0] - rtk->sol.fixxyz[0]) > 0.2 ||
                fabs(rtk->x[1] - rtk->sol.fixxyz[1]) > 0.2 ||
                fabs(rtk->x[2] - rtk->sol.fixxyz[2]) > 0.2)
            {
                if (rtk->fixCheckCnt > 10)
                {
                    rtk->sol.fixxyz[0]     = 0.0;
                    rtk->sol.fixxyz[1]     = 0.0;
                    rtk->sol.fixxyz[2]     = 0.0;
                    rtk->sol.rr_smooth_cnt = 0;
                    rtk->fixCheckCnt       = 0;
                    largeShift             = 0;
                    trace(0x02, "clear rr_smooth_cnt\n");
                }
                else
                {
                    rtk->fixCheckCnt++;
                }
            }
            else
            {
                for (i = 0; i < 3; i++)
                {
                    rtk->x[i] = rtk->sol.fixxyz[i];
                }
                rtk->fixCheckCnt = 0;
            }
        }
        else
        {
            rtk->fixCheckCnt = 0;
        }
        if (!zdres(rtk, 0, obs, nu, rs, dts, svh, rtk->x, &rtk->opt, 0, y, e, azel, r))
        {
            info = check_res(rtk, obs, rtk->x, sat, y, iu, ir, ns, azel);
            if (info == 1)
            {
                rtk->sol.stat = SOLQ_NONE;
                trace(0x02, "fix error!\n");
            }
        }
        else
        {
            rtk->sol.stat = SOLQ_NONE;
        }
    }

    free(y);
    free(e);
    free(azel);
    free(standard_v);
    free(r);
    for (i = 0; i < NSYS; i++)
    {
        for (f = 0; f < NFREQ * 2; f++)
        {
            rtk->base_prn_fix[i][f] = rtk->base_prn[i][f];
        }
    }
    return rtk->sol.stat == SOLQ_NONE ? 1 : 0;
}
extern void resetRtk(rtk_t* rtk, int stat)
{
    int i;
    // rtk->opt.mode = PMODE_KINEMA;
    rtk->sol.stat  = stat;
    rtk->sol.ratio = 0.0;
    rtk->nxFixNx   = 0;
    rtk->nfix      = 0;
    rtk->opt.std   = 0.01;
    for (i = 1; i <= MAXSAT; i++)
    {
        rtk->ssat[i - 1].rejRes    = 0;
        rtk->ssat[i - 1].resCnt    = 0;
        rtk->ssat[i - 1].timeCout  = 0;
        rtk->ssat[i - 1].resMaxCnt = 0;
    }
    memset(rtk->xp, 0, sizeof(double) * NX);
    memset(rtk->Pp, 0, sizeof(double) * NX * NX);
    memset(rtk->x, 0, sizeof(double) * NX);
    memset(rtk->P, 0, sizeof(double) * NX * NX);
}

extern int getSatNum(
    rtk_t* rtk, const obsd_t* obs, unsigned char nu, unsigned char nr, const prcopt_t* opt,
    unsigned char* sat, unsigned char* iu, unsigned char* ir, double elMin, int snrMin
)
{
    unsigned char i, j, k = 0, f, flag, nf = rtk->opt.nf;
    for (i = 0; i < nu; i++)
    {
        if (rtk->ssat[obs[i].sat - 1].vs != 1)
        {
            continue;
        }
        if (rtk->ssat[obs[i].sat - 1].quickSelSatDel == 1)
        {
            continue;
        }
        if (rtk->ssat[obs[i].sat - 1].azel[0][1] < elMin ||
            rtk->ssat[obs[i].sat - 1].azel[1][1] < elMin)
        {
            continue;
        }
        for (j = nu; j < nu + nr; j++)
        {
            if (obs[i].sat == obs[j].sat)
            { /* elevation at base station */
                flag = 0;
                for (f = 0; f < nf; f++)
                {
                    if (obs[i].P[f] != 0.0 && obs[j].P[f] != 0.0 && obs[i].L[f] != 0.0 &&
                        obs[j].L[f] != 0.0 && obs[i].SNR[f] > snrMin && obs[j].SNR[f] > snrMin)
                    {
                        flag = 1;
                        break;
                    }
                }
                if (flag == 0)
                {
                    continue;
                }
                if (rtk->ssat[obs[i].sat - 1].azel[0][1] >= opt->elmin &&
                    rtk->ssat[obs[i].sat - 1].azel[1][1] >= opt->elmin)
                {
                    sat[k]  = obs[i].sat;
                    iu[k]   = i;
                    ir[k++] = j;
                }
                break;
            }
        }
    }
    return k;
}
#if 1
/* geometry-free phase measurement -------------------------------------------*/
static double gfmeas1(const obsd_t* obs, double* lam, int index1, int index2)
{
    int i = index1, j = index2;
    if (lam[i] == 0.0 || lam[j] == 0.0 || obs->L[i] == 0.0 || obs->L[j] == 0.0)
    {
        return 0.0;
    }
    return (lam[j] * obs->L[j] - lam[i] * obs->L[i]) / lam[i];
}
static double gfmeas0(
    rtk_t* rtk, unsigned char sat, unsigned char rcv, double* lam, int index1, int index2
)
{
    int i = index1, j = index2;
    if (lam[i] == 0.0 || lam[j] == 0.0 || rtk->ssat[sat - 1].ph[rcv - 1][i] == 0.0 ||
        rtk->ssat[sat - 1].ph[rcv - 1][j] == 0.0)
    {
        return 0.0;
    }
    return (lam[j] * rtk->ssat[sat - 1].ph[rcv - 1][j] -
            lam[i] * rtk->ssat[sat - 1].ph[rcv - 1][i]) /
           lam[i];
}

static void detslp_gf(rtk_t* rtk, const obsd_t* obs, unsigned char n, int index1, int index2)
{
    double        g0, g1, thres, age, C, ep[6], lam[NFREQ];
    unsigned char i, j, k, sys, prn;
    time2epoch(obs[0].time, ep);
    for (i = 0; i < n; i++)
    {
        // if (rtk->ssat[obs[i].sat - 1].azel[obs[i].rcv - 1][1] * R2D < 10.0)
        // continue;
        sys = satsys(obs[i].sat, &prn);
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

        if ((g1 = gfmeas1(obs + i, lam, index1, index2)) == 0.0)
        {
            continue;
        }
        g0 = gfmeas0(rtk, obs[i].sat, obs[i].rcv, lam, index1, index2);
        // g0 = rtk->ssat[obs[i].sat - 1].gf[obs[i].rcv - 1][index2];
        // rtk->ssat[obs[i].sat - 1].gf[obs[i].rcv - 1][index2] = g1;
        if (g0 == 0.0)
        {
            continue;
        }

        age = rtk->tt;
        C   = SQR(lam[index2]) / SQR(lam[index1]);
        if (fabs(age) > 300)
        {
            thres = sqrt(2 * (1 + C)) * 0.01 * 5.9 * fabs(age) / 2;
        }
        else if (fabs(age) < 1)
        {
            thres = sqrt(2 * (1 + C)) * 0.01 * 5.9;
        }
        else
        {
            thres = sqrt(2 * (1 + C)) * 0.01 * 5.9 * fabs(age);
        }
        thres = 1.0;
        if (fabs(g1 - g0) > thres && fabs(age) < 7200.0)
        {
            trace(
                0x08, "%04.0f %02.0f %02.0f %02.0f %02.0f %02.0f\t", ep[0], ep[1], ep[2], ep[3],
                ep[4], ep[5]
            );
            trace(
                0x08,
                "L%dL%d\trcv=%-4d sys=%-4d prn=%-4d age=%-7.2lf el=%-7.2f "
                "thres=%-7.2lf value=%-10.2lf",
                index1 + 1, index2 + 1, obs[i].rcv, sys, prn, age,
                rtk->ssat[obs[i].sat - 1].azel[obs[i].rcv - 1][1] * R2D, thres, fabs(g1 - g0)
            );
            trace(
                0x08, "L[%d]=%14.3lf L[%d]=%14.3lf\n", index1, obs[i].L[index1], index2,
                obs[i].L[index2]
            );
            for (j = 0; j < rtk->opt.nf; j++)
            {
                rtk->ssat[obs[i].sat - 1].slip[j] = 1;
            }
        }
    }
}

extern void detectSlip(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr)
{
    unsigned char i, f, n = nu + nr;

    for (i = 0; i < nu; i++)
    {
        for (f = 0; f < NFREQ; f++)
        {
            rtk->ssat[obs[i].sat - 1].slip[f] &= 0xFC;
        }
    }
    if (rtk->opt.nf >= 2)
    {
        detslp_gf(rtk, obs, nu, 0, 1);
    }
    if (rtk->opt.nf >= 3)
    {
        detslp_gf(rtk, obs, nu, 0, 2);
    }
    if (rtk->opt.nf >= 4)
    {
        detslp_gf(rtk, obs, nu, 0, 3);
    }
    if (rtk->opt.nf >= 5)
    {
        detslp_gf(rtk, obs, nu, 0, 4);
    }
    if (rtk->opt.nf >= 6)
    {
        detslp_gf(rtk, obs, nu, 0, 5);
    }
    if (nr > 0)
    {
        if (rtk->opt.nf >= 2)
        {
            detslp_gf(rtk, obs + nu, nr, 0, 1);
        }
        if (rtk->opt.nf >= 3)
        {
            detslp_gf(rtk, obs + nu, nr, 0, 2);
        }
        if (rtk->opt.nf >= 4)
        {
            detslp_gf(rtk, obs + nu, nr, 0, 3);
        }
        if (rtk->opt.nf >= 5)
        {
            detslp_gf(rtk, obs + nu, nr, 0, 4);
        }
        if (rtk->opt.nf >= 6)
        {
            detslp_gf(rtk, obs + nu, nr, 0, 5);
        }
    }
    for (i = 0; i < nu; i++)
    {
        detslp_ll(rtk, obs, i, 1);
        // detslp_dop(rtk, obs, i, 1);
        /* update half-cycle valid flag */
        for (f = 0; f < NFREQ; f++)
        {
            rtk->ssat[obs[i].sat - 1].half[f] = !(obs[i].LLI[f] & 2);
        }
    }
    if (nr > 0)
    {
        for (i = nu; i < nu + nr; i++)
        {
            detslp_ll(rtk, obs, i, 2);
            // detslp_dop(rtk, obs, i, 2);
            /* update half-cycle valid flag */
            for (f = 0; f < NFREQ; f++)
            {
                rtk->ssat[obs[i].sat - 1].half[f] = !(obs[i].LLI[f] & 2);
            }
        }
    }
    for (i = 0; i < n; i++)
    {
        for (f = 0; f < NFREQ; f++)
        {
            if (obs[i].L[f] == 0.0)
            {
                continue;
            }
            rtk->ssat[obs[i].sat - 1].ph[obs[i].rcv - 1][f] = obs[i].L[f];
        }
    }
    for (i = 0; i < MAXSAT; i++)
    {
        for (f = 0; f < NFREQ; f++)
        {
            if ((rtk->ssat[i].slip[f] & 1) || (rtk->ssat[i].slip[f] & 2))
            {
                rtk->ssat[i].fix_amb[f] = 9999.9;
            }
        }
    }
}
#endif
void static SmoothBasePosion(rtk_t* rtk)
{
    int i;
    if (rtk->rb[0] == 0.0 || rtk->rb[1] == 0.0 || rtk->rb[2] == 0.0)
    {
        // if (smoothCnt * rtk->tt < 300) {
        for (i = 0; i < 3; i++)
        {
            baseXyz[i] = (baseXyz[i] * smoothCnt + rtk->solb.rr[i]) / (smoothCnt + 1);
        }
        smoothCnt++;
        for (i = 0; i < 3; i++)
        {
            rtk->rb[i] = baseXyz[i];
        }
        //}
    }
}

/* precise positioning
 * ---------------------------------------------------------*/
extern int rtkpos(rtk_t* rtk, obsd_t* obs, int n)
{
    gtime_t       time = {0};
    unsigned char i, j, f, flag, m, nu, nr, ns, ns_tmp, sys, prn, returnValue = 0;
    unsigned char sat[MAXOBS] = {0}, iu[MAXOBS] = {0}, ir[MAXOBS] = {0}, exc[MAXSAT] = {0};
    double *      rs, *dts;
    double        lsqraim[3] = {0};
    double*       var;
    int           svh[40 * 2], lsqstat = -1;

    rs  = zeros(6 * SELETE_SAT_NUM * 2, 1);
    dts = zeros(2 * SELETE_SAT_NUM * 2, 1);
    var = zeros(SELETE_SAT_NUM * 2, 1);

    char ts1[32], ts2[32];
    time2str(obs[0].time, ts1, 3);
    time2str(obs[n - 1].time, ts2, 3);

    printf("t1,%s\nt2,%s\n", ts1, ts2);

    trace(2, "t1,%s\nt2,%s\n", ts1, ts2);

    for (i = 0; i < MAXSAT; i++)
    {
        rtk->ssat[i].vs             = 0;
        rtk->ssat[i].quickSelSatDel = 0;
        rtk->ssat[i].azel[0][0] = rtk->ssat[i].azel[0][1] = 0.0;
        rtk->ssat[i].azel[1][0] = rtk->ssat[i].azel[1][1] = 0.0;
        rtk->ssat[i].rs[0]                                = 0.0;
        rtk->ssat[i].rs[1]                                = 0.0;
        rtk->ssat[i].rs[2]                                = 0.0;
        rtk->ssat[i].fixWL                                = 0;
        rtk->ssat[i].fixNL                                = 0;
        for (f = 0; f < NFREQ; f++)
        {
            rtk->ssat[i].ddAmb[f] = 9999.9;
            rtk->ssat[i].resc[f]  = 0.0;
        }
    }
    if (rtk->rejSatCnt == 0)
    {
        rtk->noRejectSatCnt++;
    }
    else
    {
        rtk->noRejectSatCnt = 0;
    }
    if (rtk->opt.ionoopt == IONOOPT_EST && rtk->sol.stat == SOLQ_FIX)
    {
        if (rtk->noRejectSatCnt > 20 && rtk->opt.std != 0.01)
        {
            trace(0x10, "reset std\n");
            rtk->noRejectSatCnt = 0;
            rtk->opt.std        = 0.01;
        }
    }

    rtk->rejSatCnt = 0;
    for (m = 0; m < NSYS; m++)
    {
        for (i = 0; i < NFREQ; i++)
        {
            rtk->allSlipFlag[m][i] = 0;
        }
    }

    for (i = 0; i < n; i++)
    {
        if (obs[i].rcv != 1)
        {
            break;
        }
    }
    nu = i;  // number of rover observations
    nr = n - nu;

    trace(2, "nu,%d nr,%d\n", nu, nr);

    if (nu < 4)
    {
        resetRtk(rtk, SOLQ_NONE);
        free(rs);
        free(dts);
        free(var);
        trace(2, "no rover data:%d\n", nu);
        return 1;
    }

    if (nr > 0)
    {
        trace(0xff, "base  time=%ld\n", obs[nu].time.time);
    }

    time = rtk->sol.time;

    detectSlip(rtk, obs, nu, nr);
    if (time.time != 0)
    {
        rtk->tt = timediff(obs[0].time, time);
        time    = rtk->sol.time_pre;
        if (rtk->fs == 0.0 && time.time != 0)
        {
            rtk->fs = timediff(obs[0].time, time);
        }
    }

    for (i = 0; i < 3; i++)
    {
        if (fabs(rtk->tt) > 600 && fabs(rtk->tt) != 0.0)
        {
            trace(0x04, "Warnning tt=%.2f\n", rtk->tt);
            if (i == 2)
            {
                rtk->sol.ori_var[i] = 0.1 * 0.1;
            }
            else
            {
                rtk->sol.ori_var[i] = 0.1 * 0.1;
            }
        }
    }
    if (pntpos(0, obs, nu, &rtk->sol, NULL, rtk->ssat, &rtk->opt, rtk->tt))
    {  // 0:is ok  -1 eoror
        resetRtk(rtk, SOLQ_NONE);
        trace(0x02, "rover position error\n");
        for (i = 0; i < 6; i++)
        {
            rtk->sol.rr[i] = 0.0;
        }
        free(rs);
        free(dts);
        free(var);
        return 2;
    }
    trace(
        0x04, "%-23s:%14.4f %14.4f %14.4f\n", "rover spp", rtk->sol.rr[0], rtk->sol.rr[1], rtk->sol.rr[2]
    );
    trace(2, "kalman rtk->x, 00, ");
    tracemat(2, rtk->x, 1, 6, 14, 4);

    if (rtk->opt.mode == PMODE_MOVEB || rtk->opt.mode == PMODE_KINEMA ||
        rtk->opt.mode == PMODE_STATIC)
    { /*  moving baseline */
        /* estimate position/velocity of base station */
        if (pntpos(1, obs + nu, nr, &rtk->solb, NULL, rtk->ssat, &rtk->opt, rtk->tt))
        {  // 0:is ok  -1 eoror
            trace(0x02, "base station position error\n");
            preBaseObsRTK(rtk, obs, &nu, &nr, &n, 1);
            if (nr <= 4 || rtk->opt.mode == PMODE_MOVEB)
            {
                resetRtk(rtk, SOLQ_SINGLE);
                for (i = 0; i < 6; i++)
                {
                    rtk->solb.rr[i] = 0.0;
                }
                free(rs);
                free(dts);
                free(var);
                return 3;
            }
            else
            {
                if (pntpos(1, obs + nu, nr, &rtk->solb, NULL, rtk->ssat, &rtk->opt, rtk->tt))
                {  // 0:is ok  -1 eoror
                    trace(0x02, "base station position error2\n");
                    rtk->sol.stat = SOLQ_SINGLE;
                    for (i = 0; i < 6; i++)
                    {
                        rtk->solb.rr[i] = 0.0;
                    }
                    free(rs);
                    free(dts);
                    free(var);
                    return 3;
                }
            }
        }
        // smooth base position
        trace(
            0x02,
            "base  pntpos:%14.4lf %14.4lf %14.4lf %14.4lf %14.4lf %14.4lf "
            "baseXyzError=%d\n",
            rtk->rb[0], rtk->rb[1], rtk->rb[2], rtk->solb.rr[0], rtk->solb.rr[1], rtk->solb.rr[2],
            baseXyzError
        );
        // if(baseRtcmPosition[0]==0.0 && baseRtcmPosition[1] == 0.0 &&
        // baseRtcmPosition[2] == 0.0)
        if (rtk->opt.useRtcmPosFlag == 0)
        {
            SmoothBasePosion(rtk);
        }
        if ((fabs(rtk->rb[0] - rtk->solb.rr[0]) > 100) ||
            (fabs(rtk->rb[1] - rtk->solb.rr[1]) > 100) ||
            (fabs(rtk->rb[2] - rtk->solb.rr[2]) > 100) && rtk->opt.useRtcmPosFlag == 0)
        {
            baseXyzError++;
            if (baseXyzError > 10)
            {
                rtk->rb[0]             = rtk->solb.rr[0];
                rtk->rb[1]             = rtk->solb.rr[1];
                rtk->rb[2]             = rtk->solb.rr[2];
                rtk->sol.fixxyz[0]     = 0.0;
                rtk->sol.fixxyz[1]     = 0.0;
                rtk->sol.fixxyz[2]     = 0.0;
                rtk->sol.rr_smooth_cnt = 0;
            }
            free(rs);
            free(dts);
            free(var);
            return 3;
        }
        else
        {
            baseXyzError = 0;
        }

        if ((fabs(rtk->rb[0] - rtk->solb.rr[0]) > 30) ||
            (fabs(rtk->rb[1] - rtk->solb.rr[1]) > 30) ||
            (fabs(rtk->rb[2] - rtk->solb.rr[2]) > 30) && rtk->opt.useRtcmPosFlag == 0)
        {
            baseXyzError++;
        }
        else
        {
            baseXyzError = 0;
        }

        if (rtk->opt.mode == PMODE_KINEMA || rtk->opt.mode == PMODE_STATIC)
        {
            rtk->sol.age = (float)timediff(rtk->sol.time, rtk->solb.time);
            if (fabs(rtk->sol.age) > rtk->opt.maxtdiff)
            {
                trace(
                    0x02, "age=%f\t sol.time=%d\t solb.time=%d\t \n", rtk->sol.age,
                    (int)rtk->sol.time.time, (int)rtk->solb.time.time
                );
                resetRtk(rtk, SOLQ_SINGLE);
                free(rs);
                free(dts);
                free(var);
                return 4;
            }
            if (preBaseObsRTK(rtk, obs, &nu, &nr, &n, 0))
            {
                if (pntpos(1, obs + nu, nr, &rtk->solb, NULL, rtk->ssat, &rtk->opt, rtk->tt))
                {  // 0:is ok  -1 eoror
                    trace(0x02, "base station position error3\n");
                    resetRtk(rtk, SOLQ_SINGLE);
                    for (i = 0; i < 6; i++)
                    {
                        rtk->solb.rr[i] = 0.0;
                    }
                    free(rs);
                    free(dts);
                    free(var);
                    return 3;
                }
            }
        }
        else
        {
            rtk->sol.age = (float)timediff(rtk->sol.time, rtk->solb.time);
            if (fabs(rtk->sol.age) > TTOL_MOVEB)
            {
                trace(
                    0x02, "age=%f\t sol.time=%d\t solb.time=%d\t \n", rtk->sol.age,
                    (int)rtk->sol.time.time, (int)rtk->solb.time.time
                );
                resetRtk(rtk, SOLQ_SINGLE);
                free(rs);
                free(dts);
                free(var);
                return 4;
            }
            for (i = 0; i < 3; i++)
            {
                rtk->rb[i] = rtk->solb.rr[i];
            }
        }
    }
    trace(2, "kalman rtk->x, 01, ");
    tracemat(2, rtk->x, 1, 6, 14, 4);

    ns = getSatNum(rtk, obs, nu, nr, &rtk->opt, sat, iu, ir, 15.0 * D2R, 0.0);

    char s1[64];
    time2str(rtk->sol.time, s1, 2);
    trace(2, "getSatNum, ns, n,nr,nu, %s, %d,%d, %d,%d\n", s1, ns, n, nr, nu);

    // gloFlag(rtk, obs, nu, nr);
    // if (selectSatFlag(rtk, obs, nu, nr))
    //     quickSelSat(rtk, obs, nu, nr);

    detectionRes(rtk, obs, nu);
    ns = obsScan(rtk, &rtk->opt, obs, n);
    trace(2, "obsScan, ns, %d,n, %d\n", ns, n);
    nu = nr = ns / 2;
    ns      = selsatRTK(rtk, obs, nu, nr, &rtk->opt, sat, iu, ir, 0);
    trace(2, "selsatRTK, ns, %d\n", ns);
    if (ns < 5)
    {
        trace(0x02, "warnning rtk ns=%d\n", ns);
        resetRtk(rtk, SOLQ_SINGLE);
        free(rs);
        free(dts);
        free(var);
        return 5;
    }
    trace(0x04, "sat:");
    for (i = 0; i < ns; i++)
    {
        sys = satsys(sat[i], &prn);
        trace(0x04, "(%d:%d:%2.0f) ", sys, prn, rtk->ssat[sat[i] - 1].azel[0][1] * R2D);
    }
    trace(0x04, "\n");

    satposs(obs[0].time, obs, 2 * ns, rtk->opt.sateph, rs, dts, var, svh);

    if (rtk->sol.fixxyz[0] != 0.0 && rtk->sol.rr_smooth_cnt > 10)
    {
        for (i = 0; i < 3; i++)
        {
            rtk->sol.rr[i] = rtk->sol.fixxyz[i];
        }
    }

    trace(2, "kalman rtk->x, in while, ");
    tracemat(2, rtk->x, 1, 6, 14, 4);

    rtk->sol.stat = SOLQ_FLOAT;
    relpos(rtk, obs, nu, nr, sat, iu, ir, rs, dts, var, svh);
    trace(0x04, "rtk lsq ns=%3d nsPre=%3d\n", rtk->sol.nsLsq, rtk->sol.nsLsqPre);
    // if (0) {
    trace(2, "kalman rtk->x, 03, ");
    tracemat(2, rtk->x, 1, 6, 14, 4);

    if (rtk->opt.mode == 2 && rtk->sol.stat == SOLQ_FIX && rtk->opt.ionoopt !=
    IONOOPT_EST) {
        rtk->sol.rr_lsq[0] = rtk->sol.rr_lsq[1] = rtk->sol.rr_lsq[2] = 0.0;
        rtk->sol.nsLsq = 0;
        int delSat = 0;
        if (rtk->fix_state == 1) {
            while (1) {
                lsqstat = rtkLsq(rtk, obs, 2 * ns, nu, svh, rs, dts, var,
                lsqraim, ns, sat, iu, ir, exc); if (lsqstat == -2 &&
                rtk->sol.ns[1] > 5 && lsqraim[0] > 0) {
                    delSat++;
                    exc[(int)lsqraim[0] - 1] = 1;
                    sys = satsys((int)lsqraim[0], &prn);
                    rtk->ssat[(int)lsqraim[0] - 1].vs = 0;
                    trace(0x04, "rtkls reject sys=%d prn=%d el=%lf\n", sys,
                    prn, rtk->ssat[(int)lsqraim[0] - 1].azel[0][1] * R2D); for
                    (i = 0; i < NFREQ; i++) {
                        rtk->ssat[(int)lsqraim[0] - 1].fix_amb[i] = 9999.9;
                    }
                    for (i = 0; i < 3; i++) {
                        lsqraim[i] = 0.0;
                    }
                } else {
                    rtk->sol.ns[1] = rtk->sol.nsLsq;
                    break;
                }
            }
            if (lsqstat != 0)
                rtk->fix_state = 0;
        }
        trace(2, "kalman rtk->x, 04, ");         tracemat(2, rtk->x,1,6,14,4);

        if (rtk->sol.rr_lsq[0] != 0.0 && rtk->sol.rr_lsq[1] != 0.0 &&
        rtk->sol.rr_lsq[2] != 0.0 && (rtk->sol.nsLsq - rtk->sol.nsLsqPre) >=
        -5) {
            rtk->sol.rr[0] = rtk->sol.rr_lsq[0];
            rtk->sol.rr[1] = rtk->sol.rr_lsq[1];
            rtk->sol.rr[2] = rtk->sol.rr_lsq[2];
            rtk->sol.nsLsqPre = rtk->sol.nsLsq;
            rtk->sol.stat = SOLQ_FIX;
        }
        else {
            rtk->sol.nsLsqPre = 0;
            rtk->sol.stat = SOLQ_FLOAT;
        }
        delSat = rtk->opt.maxDelSat;
        if (delSat > rtk->opt.maxDelSat) {
            trace(0x04, "rtkls reject nsat;%d maxDelSat:%d\n", delSat,
            rtk->opt.maxDelSat); rtk->sol.stat = SOLQ_FLOAT;
        }
    }
    trace(2, "kalman rtk->x, 05, ");         tracemat(2, rtk->x,1,6,14,4);

    if (rtk->sol.stat == SOLQ_FIX)
    {
        if (rtk->sol.fixxyz[0] != 0)
        {
            if ((fabs(rtk->sol.fixxyz[0] - rtk->sol.rr[0]) > 0.2 ||
                 fabs(rtk->sol.fixxyz[1] - rtk->sol.rr[1]) > 0.2 ||
                 fabs(rtk->sol.fixxyz[2] - rtk->sol.rr[2]) > 0.2) &&
                rtk->sol.rr_smooth_cnt > (int)((3600.0 - 1) / rtk->opt.timeInterval))
            {
                rtk->sol.stat = SOLQ_FLOAT;
                fixErrorCnt++;
                if (fixErrorCnt > 10)
                {
                    rtk->sol.fixxyz[0]     = 0.0;
                    rtk->sol.fixxyz[1]     = 0.0;
                    rtk->sol.fixxyz[2]     = 0.0;
                    rtk->sol.rr_smooth_cnt = 0;
                    rtk->sol.ratio         = 0.0;
                    trace(0x04, "error fixErrorCnt:%d\n", fixErrorCnt);
                }
                trace(0x04, "warnning fixErrorCnt:%d\n", fixErrorCnt);
                rtk->sol.stat = SOLQ_FLOAT;
            }
            else
            {
                fixErrorCnt = 0;
            }
        }
    }
    if (rtk->fixCheckCnt > 0)
    {
        rtk->sol.stat = SOLQ_FLOAT;
    }
    for (i = 0; i < MAXSAT; i++)
    {
        rtk->ssat[i].ddl = rtk->ssat[i].ddlcru;
    }
    if (rtk->sol.stat == SOLQ_NONE)
    {
        resetRtk(rtk, SOLQ_SINGLE);
        free(rs);
        free(dts);
        free(var);
        return 6;
    }
    if (rtk->sol.stat == SOLQ_FLOAT)
    {
        free(rs);
        free(dts);
        free(var);
        return 0;
    }
    free(rs);
    free(dts);
    free(var);
    return 0;
}
