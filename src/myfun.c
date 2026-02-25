#include "rtk.h"

#define REJ_RES_COUNT 60
#define THRES_REJECT_RES 0.02
#define REJ_RES_MAX_CNT 10

extern int findEphIndex(eph_t* eph, unsigned char sat)
{
    unsigned char i;

    for (i = 0; i < MAXEPH; i++)
    {
        if (eph[i].sat == sat || eph[i].sat == 0)
        {
            return i;
        }
    }
    return -1;
}
extern int findGephIndex(geph_t* geph, unsigned char sat)
{
    unsigned char i;

    for (i = 0; i < MAXGEPH; i++)
    {
        if (geph[i].sat == sat || geph[i].sat == 0)
        {
            return i;
        }
    }
    return -1;
}
extern double L_LP(
    double i, double j, double k, double f1, double f2, double f5, const double* Pi,
    const double* Pj
)
{
    double P1, P2, P5;

    if ((i && (!Pi[0] || !Pj[0])) || (j && (!Pi[1] || !Pj[1])) || (k && (!Pi[2] || !Pj[2])))
    {
        return 0.0;
    }
    P1 = Pi[0] - Pj[0];
    P2 = Pi[1] - Pj[1];
    P5 = Pi[2] - Pj[2];
    return (i * f1 * P1 + j * f2 * P2 + k * f5 * P5) / (i * f1 + j * f2 + k * f5);
}
extern double L_LC(
    double i, double j, double k, double f1, double f2, double f5, const double* Li,
    const double* Lj
)
{
    double L[3];
    if ((i && (!Li[0] || !Lj[0])) || (j && (!Li[1] || !Lj[1])) || (k && (!Li[2] || !Lj[2])))
    {
        return 0.0;
    }
    L[0] = (CLIGHT / f1) * (Li[0] - Lj[0]);
    L[1] = (CLIGHT / f2) * (Li[1] - Lj[1]);
    L[2] = (CLIGHT) / f5 * (Li[2] - Lj[2]);

    return (i * f1 * L[0] + j * f2 * L[1] + k * f5 * L[2]) / (i * f1 + j * f2 + k * f5);
}

extern double lam_LC(double i, double j, double k, double f1, double f2, double f5)
{
    if ((i == 0 && j == 0 && k == 0) || (f1 == 0.0 && f2 == 0.0 && f5 == 0.0))
    {
        return 0.0;
    }
    return CLIGHT / (i * f1 + j * f2 + k * f5);
}
extern double L_LP2(double i, double j, double k, double f1, double f2, double f5, const double* Pi)
{
    double P1, P2, P5;

    if ((i && !Pi[0]) || (j && !Pi[1]) || (k && !Pi[2]))
    {
        return 0.0;
    }
    P1 = Pi[0];
    P2 = Pi[1];
    P5 = Pi[2];
    return (i * f1 * P1 + j * f2 * P2 + k * f5 * P5) / (i * f1 + j * f2 + k * f5);
}
extern double L_LC2(double i, double j, double k, double f1, double f2, double f5, const double* Li)
{
    double L[3];
    if ((i && !Li[0]) || (j && !Li[1]) || (k && !Li[2]))
    {
        return 0.0;
    }
    L[0] = (CLIGHT / f1) * (Li[0]);
    L[1] = (CLIGHT / f2) * (Li[1]);
    L[2] = (CLIGHT) / f5 * (Li[2]);

    return (i * f1 * L[0] + j * f2 * L[1] + k * f5 * L[2]) / (i * f1 + j * f2 + k * f5);
}

extern double var_LC(double i, double j, double k, double f1, double f2, double f5, double sig)
{
    double test = (SQR(i * f1) + SQR(j * f2) + SQR(k * f5)) / SQR(i * f1 + j * f2 + k * f5);
    return test * SQR(sig);
}
extern double var_LCion(
    double i, double j, double k, double f1, double f2, double f5, double bl, double el
)
{
    double fator = (SQR(f1)) * (i / f1 + j / f2 + k / f5) / (i * f1 + j * f2 + k * f5);
    double var   = SQR(0.03 * bl / 1E4);
    var          = fabs(fator) * var;
    return var;
}
extern double SD_var(double var, double el)
{
    double sinel = sin(el);
    return 2.0 * (var + var / sinel / sinel);
}
extern int preBaseObsRTK(
    rtk_t* rtk, obsd_t* obs, unsigned char* nu1, unsigned char* nr1, int* n1, int flag
)
{
    int           i, stat = 0;
    unsigned char nu = (*nu1);
    unsigned char nr = (*nr1);
    int           n  = (*n1);
    double        tt;

    tt = fabs(timediff(obs[0].time, g_preBaseObsRtk[0].time));
    if ((nr < g_preBaseObsRtkNum - 4 && tt < rtk->opt.maxtdiff && rtk->opt.mode != 4) || flag == 1)
    {
        rtk->sol.age = tt;
        trace(0x02, "nr=%d pre=%d tt=%4.2f flag=%d\n", nr, g_preBaseObsRtkNum, tt, flag);
        if (tt < rtk->opt.maxtdiff)
        {
            for (i = 0; i < g_preBaseObsRtkNum; i++)
            {
                obs[nu + i]                       = g_preBaseObsRtk[i];
                rtk->ssat[obs[nu + i].sat - 1].vs = 1;
            }
            for (i = 0; i < 3; i++)
            {
                rtk->rb[i] = rtk->prb[i];
            }
            nr   = g_preBaseObsRtkNum;
            n    = nr + nu;
            stat = 1;
        }
        else
        {
            trace(0x02, "preBaseObs dt more than maxdiff\n");
        }
    }
    else
    {
        g_preBaseObsRtkNum = nr;
        for (i = 0; i < 3; i++)
        {
            rtk->prb[i] = rtk->rb[i];
        }
        for (i = 0; i < nr; i++)
        {
            g_preBaseObsRtk[i] = obs[nu + i];
        }
    }
    (*nu1) = nu;
    (*nr1) = nr;
    (*n1)  = n;
    return stat;
}

#if 0
extern void findMaxRes(rtk_t *rtk, unsigned char *sat, int ns) {
    unsigned char j, f, k, reject_sat = 0, nf = rtk->opt.ionoopt == IONOOPT_IFLC ? 1 : rtk->opt.nf;
    double vmax = 0, threshold, dr[3], bl;

    for (j = 0; j < ns; j++) {
        for (f = 0; f < nf; f++) {
            if (!rtk->ssat[sat[j] - 1].vsat[f])
                continue;
            threshold = THRES_REJECT_RES; // THRES_REJECT_RES;
            if (rtk->opt.ionoopt != IONOOPT_EST) {
                bl = baseline(rtk->x, rtk->rb, dr);
                if (bl > 1000) {
                    k = bl / 1000;
                    threshold += 0.01 * k;
                }
            }
            if (rtk->ssat[sat[j] - 1].azel[0][1] * R2D < 30)
                threshold += 0.02;
            if (fabs(rtk->ssat[sat[j] - 1].resc[f]) > threshold && fabs(rtk->ssat[sat[j] - 1].resc[f]) > vmax) {
                vmax = fabs(rtk->ssat[sat[j] - 1].resc[f]);
                reject_sat = sat[j];
            }
        }
        // if (rtk->opt.ionoopt == IONOOPT_EST) {
        //    if (fabs(rtk->ssat[sat[j] - 1].dion) > 0.2) {
        //        rtk->ssat[sat[j] - 1].rejRes = 1;
        //        rtk->ssat[sat[j] - 1].resCnt++;
        //    }
        // }
    }
    if (reject_sat > 0) {
        for (j = 0; j < ns; j++) {
            if (sat[j] == reject_sat)
                rtk->ssat[sat[j] - 1].resMaxCnt++;
            else
                rtk->ssat[sat[j] - 1].resMaxCnt = 0;
        }
        if (rtk->ssat[reject_sat - 1].resMaxCnt >= REJ_RES_MAX_CNT) {
            rtk->ssat[reject_sat - 1].rejRes = 1;
            rtk->ssat[reject_sat - 1].resCnt++;
            trace(0x04, "reject_sat=%d vmax=%f threshold=%f\n", reject_sat, vmax, threshold);
        }
    }
}
#endif
///* detect res ------------------------------------------------*/
extern void detectionRes(rtk_t* rtk, obsd_t* obs, int n)
{
    unsigned char i, j, f, nf = rtk->opt.nf, sat, flag = 0;

    for (i = 0; i < n; i++)
    {
        sat = obs[i].sat;
        if (rtk->ssat[sat - 1].rejRes == 1 || rtk->ssat[sat - 1].slip_cout[0] >= 3 ||
            rtk->ssat[sat - 1].slip_cout[1] >= 3 || rtk->ssat[sat - 1].slip_cout[2] >= 3)
        {
            if (rtk->ssat[sat - 1].timeCout == REJ_RES_COUNT * rtk->ssat[sat - 1].resCnt)
            {
                rtk->ssat[sat - 1].rejRes           = 0;
                rtk->ssat[sat - 1].resMaxCnt        = 0;
                rtk->ssat[sat - 1].timeCout         = 0;
                rtk->ssat[sat - 1].slip_cout[0]     = rtk->ssat[sat - 1].slip_cout[1] =
                    rtk->ssat[sat - 1].slip_cout[2] = 0;
                trace(0x04, "recover sat=%d by time cout end\n", sat);
            }
            else
            {
                rtk->ssat[sat - 1].rejRes = 1;
                rtk->ssat[sat - 1].timeCout++;
                trace(
                    0x04, "delete:sat=%3d el=%4.2lf rejRes=%d timeCout=%d\n", sat,
                    rtk->ssat[sat - 1].azel[0][1] * R2D, rtk->ssat[sat - 1].rejRes,
                    rtk->ssat[sat - 1].timeCout
                );
            }
        }
    }
    trace(0x04, "ionIndexCnt:\n");
    for (i = 1; i <= MAXSAT; i++)
    {
        for (j = 0; j < n; j++)
        {
            flag = 0;
            if (obs[j].sat == i)
            {
                flag = 1;
                break;
            }
        }
        if (flag == 0)
        {
            rtk->ssat[i - 1].rejRes      = 0;
            rtk->ssat[i - 1].resCnt      = 0;
            rtk->ssat[i - 1].ionIndexCnt = 0;
            for (f = 0; f < nf; f++)
            {
                rtk->ssat[i - 1].gf[f]    = 0.0;
                rtk->ssat[i - 1].ph[0][f] = 0.0;
                rtk->ssat[i - 1].ph[1][f] = 0.0;
            }
            rtk->ssat[i - 1].timeCout  = 0;
            rtk->ssat[i - 1].resMaxCnt = 0;
        }
        else
        {
            trace(0x04, "%d:%d ", i, rtk->ssat[i - 1].ionIndexCnt);
        }
    }
    trace(0x04, "\n");
}

/* set select flag ------------------------------------------------*/
extern int selectSatFlag(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr)
{
    unsigned char i, j, f, nf = rtk->opt.nf, flag, sys, prn, quickSelSatFlag = 0, index1 = 0,
                           index2 = 0;
    unsigned char ngps = 0, nglo = 0, ngal = 0, nbds = 0, nqzs = 0;
    for (i = 0; i < MAXSAT; i++)
    {
        if (rtk->ssat[i].vs != 1)
        {
            continue;
        }
        sys = satsys(i + 1, &prn);
        if (rtk->ssat[i].azel[0][1] < rtk->opt.elmin || rtk->ssat[i].azel[1][1] < rtk->opt.elmin)
        {
            // trace(0x08, "sys=%3d prn=%3d el1=%.2f el2=%lf\n", sys,prn, rtk->ssat[i].azel[0][1] *
            // R2D, rtk->ssat[i].azel[1][1] * R2D);
            continue;
        }
        flag = 0;
        for (j = 0; j < nu; j++)
        {
            if (obs[j].sat == i + 1)
            {
                index1 = j;
            }
        }
        for (j = nu; j < nu + nr; j++)
        {
            if (obs[j].sat == i + 1)
            {
                index2 = j;
            }
        }
        for (f = 0; f < nf; f++)
        {
            if (obs[index1].P[f] != 0.0 && obs[index1].L[f] != 0.0 && obs[index2].P[f] != 0.0 &&
                obs[index2].L[f] != 0.0)
            {
                flag = 1;
                break;
            }
        }
        if (flag == 0)
        {
            trace(0x04, "reset sat:%3d vs\n", i + 1);
            rtk->ssat[i].vs = 0;
            continue;
        }
        // if (rtk->ssat[i].rejFloat[0] == 1 && rtk->ssat[i].rejFloat[1] == 1 &&
        // rtk->ssat[i].rejFloat[2] == 1) continue;
        sys = satsys(i + 1, NULL);
        if (sys == SYS_GPS)
        {
            ngps++;
        }
        else if (sys == SYS_GLO)
        {
            nglo++;
        }
        else if (sys == SYS_GAL)
        {
            ngal++;
        }
        else if (sys == SYS_BDS)
        {
            nbds++;
        }
        else
        {
            nqzs++;
        }
        // printf("%3d ", i+1);
    }
    // printf("\n");
    trace(0x02, "ngps=%d nglo=%d ngal=%d nbds=%d nqzs=%d\n", ngps, nglo, ngal, nbds, nqzs);
    if (ngps + ngal + nbds + nqzs > SELETE_SAT_NUM)
    {
        quickSelSatFlag = 1;
    }
    return quickSelSatFlag;
    if (nbds >= SELETE_SAT_NUM)
    {
        if (nbds > SELETE_SAT_NUM)
        {
            quickSelSatFlag = 1;
        }
        for (i = 0; i < MAXSAT; i++)
        {
            sys = satsys(i + 1, NULL);
            if (sys != SYS_BDS)
            {
                rtk->ssat[i].vs = 0;
            }
        }
    }
    else if ((ngps + nbds + nqzs) >= SELETE_SAT_NUM)
    {
        if ((nbds + ngps + nqzs) > SELETE_SAT_NUM)
        {
            quickSelSatFlag = 1;
        }
        for (i = 0; i < MAXSAT; i++)
        {
            sys = satsys(i + 1, NULL);
            if (sys == SYS_GAL || sys == SYS_GLO)
            {
                rtk->ssat[i].vs = 0;
            }
        }
    }
    else if ((ngps + nbds + ngal + nqzs) >= SELETE_SAT_NUM)
    {
        if ((ngps + nbds + ngal + nqzs) > SELETE_SAT_NUM)
        {
            quickSelSatFlag = 1;
        }
        for (i = 0; i < MAXSAT; i++)
        {
            sys = satsys(i + 1, NULL);
            if (sys == SYS_GLO)
            {
                rtk->ssat[i].vs = 0;
            }
        }
    }
    else if ((ngps + nbds + ngal + nglo + nqzs) >= SELETE_SAT_NUM)
    {
        if ((ngps + nbds + ngal + nglo + nqzs) > SELETE_SAT_NUM)
        {
            quickSelSatFlag = 1;
        }
    }
    return quickSelSatFlag;
}

extern void quickSelSat(rtk_t* rtk, obsd_t* obs, unsigned char nu, unsigned char nr)
{
    unsigned char i, j, f, complete, sigFreqNum = 0, sat, sys, prn, elMoreThan60 = 0,
                                     elLessThan60 = 0;
    double        tmp, k, azl[MAXOBS] = {0}, deltAzl[MAXOBS] = {0}, el, el1, el2;
    unsigned char sat1[MAXOBS] = {0}, sat2[MAXOBS] = {0};
    unsigned char index1 = 0, index2 = 0;
    unsigned char mulFreq[MAXSAT] = {0}, sigFreqSat[MAXSAT] = {0};
    double elGPS[MAXOBS] = {0}, elBDS[MAXOBS] = {0}, elGAL[MAXOBS] = {0}, elGLO[MAXOBS] = {0},
           elQZS[MAXOBS]           = {0};
    unsigned char elGPSsat[MAXOBS] = {0}, elBDSsat[MAXOBS] = {0}, elGALsat[MAXOBS] = {0},
                  elGLOsat[MAXOBS] = {0}, elQZSsat[MAXOBS] = {0};
    unsigned char elGPSIndex = 0, elBDSIndex = 0, elGALIndex = 0, elGLOIndex = 0, elQZSIndex = 0;
    double        thresel = 60.0;
    int           group;
    //----find every sys max el sat,record the num of el more than 60, less than 60----
    for (i = 0; i < nu; i++)
    {
        sat = obs[i].sat;
        sys = satsys(sat, &prn);
        if (rtk->ssat[sat - 1].vs != 1)
        {
            continue;
        }
        // if (rtk->ssat[sat - 1].rejRes == 1) continue;
        // if (rtk->ssat[sat - 1].rejFloat[0] == 1 && rtk->ssat[sat - 1].rejFloat[1] == 1 &&
        // rtk->ssat[sat - 1].rejFloat[2] == 1) continue;
        if (rtk->ssat[sat - 1].azel[0][1] < rtk->opt.elmin)
        {
            continue;
        }
        if (rtk->ssat[sat - 1].azel[1][1] < rtk->opt.elmin)
        {
            continue;
        }
        el = rtk->ssat[sat - 1].azel[0][1] * R2D;
        if (sys == SYS_GPS)
        {
            elGPS[elGPSIndex]      = el;
            elGPSsat[elGPSIndex++] = sat;
        }
        if (sys == SYS_GLO)
        {
            elGLO[elGLOIndex]      = el;
            elGLOsat[elGLOIndex++] = sat;
        }
        else if (sys == SYS_GAL)
        {
            elGAL[elGALIndex]      = el;
            elGALsat[elGALIndex++] = sat;
        }
        else if (sys == SYS_BDS)
        {
            elBDS[elBDSIndex]      = el;
            elBDSsat[elBDSIndex++] = sat;
        }
        else if (sys == SYS_QZS)
        {
            elQZS[elQZSIndex]      = el;
            elQZSsat[elQZSIndex++] = sat;
        }
        if (el >= thresel)
        {
            elMoreThan60++;
        }
        else
        {
            elLessThan60++;
        }
    }
    //------------el sort---------------
    for (i = 0; i < elGPSIndex; i++)
    {
        for (j = i + 1; j < elGPSIndex; j++)
        {
            if (elGPS[i] < elGPS[j])
            {
                tmp      = elGPS[j];
                elGPS[j] = elGPS[i];
                elGPS[i] = tmp;

                sat         = elGPSsat[j];
                elGPSsat[j] = elGPSsat[i];
                elGPSsat[i] = sat;
            }
        }
    }
    for (i = 0; i < elGLOIndex; i++)
    {
        for (j = i + 1; j < elGLOIndex; j++)
        {
            if (elGLO[i] < elGLO[j])
            {
                tmp      = elGLO[j];
                elGLO[j] = elGLO[i];
                elGLO[i] = tmp;

                sat         = elGLOsat[j];
                elGLOsat[j] = elGLOsat[i];
                elGLOsat[i] = sat;
            }
        }
    }
    for (i = 0; i < elBDSIndex; i++)
    {
        for (j = i + 1; j < elBDSIndex; j++)
        {
            if (elBDS[i] < elBDS[j])
            {
                tmp      = elBDS[j];
                elBDS[j] = elBDS[i];
                elBDS[i] = tmp;

                sat         = elBDSsat[j];
                elBDSsat[j] = elBDSsat[i];
                elBDSsat[i] = sat;
            }
        }
    }
    for (i = 0; i < elGALIndex; i++)
    {
        for (j = i + 1; j < elGALIndex; j++)
        {
            if (elGAL[i] < elGAL[j])
            {
                tmp      = elGAL[j];
                elGAL[j] = elGAL[i];
                elGAL[i] = tmp;

                sat         = elGALsat[j];
                elGALsat[j] = elGALsat[i];
                elGALsat[i] = sat;
            }
        }
    }
    for (i = 0; i < elQZSIndex; i++)
    {
        for (j = i + 1; j < elQZSIndex; j++)
        {
            if (elQZS[i] < elQZS[j])
            {
                tmp      = elQZS[j];
                elQZS[j] = elQZS[i];
                elQZS[i] = tmp;

                sat         = elQZSsat[j];
                elQZSsat[j] = elQZSsat[i];
                elQZSsat[i] = sat;
            }
        }
    }
    //----record the num of sigle freq and mul freq----
    for (i = 0; i < nu; i++)
    {
        sat = obs[i].sat;
        sys = satsys(sat, &prn);
        if (rtk->ssat[sat - 1].vs != 1)
        {
            // trace(16, "delete vs sys=%d prn=%d\n", sys, prn);
            continue;
        }
        // if (rtk->ssat[sat - 1].rejRes == 1)     continue;
        // if (rtk->ssat[sat - 1].rejFloat[0] == 1 && rtk->ssat[sat - 1].rejFloat[1] == 1 &&
        // rtk->ssat[sat - 1].rejFloat[2] == 1) continue;
        if (rtk->ssat[sat - 1].azel[0][1] < rtk->opt.elmin)
        {
            continue;
        }
        if (rtk->ssat[sat - 1].azel[1][1] < rtk->opt.elmin)
        {
            continue;
        }
        // if (sat == elGPSsat[0] || sat == elGPSsat[1]) continue;
        // if (sat == elGLOsat[0] || sat == elGLOsat[1]) continue;
        // if (sat == elGALsat[0] || sat == elGALsat[1]) continue;
        // if (sat == elBDSsat[0] || sat == elBDSsat[1]) continue;
        // if (sat == elQZSsat[0] || sat == elQZSsat[1]) continue;
        complete = 0;
        for (f = 0; f < rtk->opt.nf; f++)
        {
            if (obs[i].P[f] != 0.0 && obs[i].L[f] != 0.0)
            {
                complete++;
            }
        }
        if (complete >= 2)
        {
            mulFreq[sat - 1] = complete;
        }
        else
        {
            sigFreqSat[sigFreqNum++] = sat;
        }
    }

    for (i = nu; i < nu + nr; i++)
    {
        sat      = obs[i].sat;
        complete = 0;
        if (mulFreq[sat - 1] == 0)
        {
            continue;
        }
        for (f = 0; f < rtk->opt.nf; f++)
        {
            if (obs[i].P[f] != 0.0 && obs[i].L[f] != 0.0)
            {
                complete++;
            }
        }
        if (complete < 2)
        {
            mulFreq[sat - 1]         = 0;
            sigFreqSat[sigFreqNum++] = sat;
        }
    }

    group = elLessThan60 + elMoreThan60 - SELETE_SAT_NUM;
    trace(16, "group=%d %d %d sigFreqNum=%d\n", group, elLessThan60, elMoreThan60, sigFreqNum);

    //-----------if need to reject, prior single frq fix amb is zero---
    if (group >= sigFreqNum)
    {
        for (i = 0; i < sigFreqNum; i++)
        {
            if (rtk->ssat[sigFreqSat[i] - 1].fix_amb[0] == 9999.9)
            {
                rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel = 1;
                group--;
            }
        }
    }
    else if (group > 0)
    {
        for (i = 0; i < sigFreqNum; i++)
        {
            if (rtk->ssat[sigFreqSat[i] - 1].fix_amb[0] == 9999.9)
            {
                rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel = 1;
                group--;
            }
            if (group == 0)
            {
                break;
            }
        }
    }
    if (group <= 0)
    {
        return;
    }
    for (i = 0; i < MAXSAT; i++)
    {
        if (mulFreq[i] == 0)
        {
            continue;
        }
        if (rtk->ssat[i].fix_amb[0] == 9999.9)
        {
            rtk->ssat[i].quickSelSatDel = 1;
            group--;
        }
        if (group == 0)
        {
            break;
        }
    }
    if (group <= 0)
    {
        return;
    }
    //-----------if need to reject, prior single frq---
    if (group >= sigFreqNum)
    {
        for (i = 0; i < sigFreqNum; i++)
        {
            if (rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel == 1)
            {
                continue;
            }
            rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel = 1;
            group--;
        }
    }
    else if (group > 0)
    {
        for (i = 0; i < sigFreqNum; i++)
        {
            if (rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel == 1)
            {
                continue;
            }
            rtk->ssat[sigFreqSat[i] - 1].quickSelSatDel = 1;
            group--;
            if (group == 0)
            {
                break;
            }
        }
    }
    if (group <= 0)
    {
        return;
    }

    //-------record the rest of satellite azl and sat number----
    for (i = 0; i < nu; i++)
    {
        sat = obs[i].sat;
        sys = satsys(sat, &prn);
        if (rtk->ssat[sat - 1].vs != 1)
        {
            continue;
        }
        // if (rtk->ssat[sat - 1].rejRes == 1)     continue;
        if (rtk->ssat[sat - 1].quickSelSatDel == 1)
        {
            continue;
        }
        // if (rtk->ssat[sat - 1].rejFloat[0] == 1 && rtk->ssat[sat - 1].rejFloat[1] == 1 &&
        // rtk->ssat[sat - 1].rejFloat[2] == 1) continue;
        if (rtk->ssat[sat - 1].azel[0][1] < rtk->opt.elmin)
        {
            continue;
        }
        if (rtk->ssat[sat - 1].azel[1][1] < rtk->opt.elmin)
        {
            continue;
        }
        sat1[index1]  = sat;
        azl[index1++] = rtk->ssat[sat - 1].azel[0][0] * R2D;
    }

    //---------------azl sort------------------
    for (i = 0; i < index1; i++)
    {
        for (j = i + 1; j < index1; j++)
        {
            if (azl[i] > azl[j])
            {
                tmp    = azl[j];
                azl[j] = azl[i];
                azl[i] = tmp;

                sat     = sat1[j];
                sat1[j] = sat1[i];
                sat1[i] = sat;
            }
        }
    }
    for (i = 0; i < index1; i++)
    {
        sat2[i] = sat1[i];
    }
    //---------------difference-------------
    for (i = j = 0; i + 1 < index1; i += 2, j++)
    {
        deltAzl[index2++] = azl[i + 1] - azl[i];
    }
    //---------------dazl sort--------------------
    for (i = 0; i < index2; i++)
    {
        for (j = i + 1; j < index2; j++)
        {
            if (deltAzl[i] > deltAzl[j])
            {
                tmp        = deltAzl[j];
                deltAzl[j] = deltAzl[i];
                deltAzl[i] = tmp;

                // printf("i=%d,j=%d\n", i, j);

                // printf("swap %d %d\n", 2 * j, 2 * i);
                sat         = sat2[2 * j];
                sat2[2 * j] = sat2[2 * i];
                sat2[2 * i] = sat;

                // printf("swap %d %d\n", 2 * j + 1, 2 * i + 1);
                sat             = sat2[2 * j + 1];
                sat2[2 * j + 1] = sat2[2 * i + 1];
                sat2[2 * i + 1] = sat;
            }
        }
    }

    if (group > 0)
    {
        i = 0;
        while (group > 0)
        {
            k = elMoreThan60 / (double)elLessThan60;
            if (sat2[2 * i] > MAXSAT || sat2[2 * i] < 1)
            {
                if (i == 0)
                {
                    return;
                }
                i = 0;
                continue;
            }
            if (sat2[2 * i + 1] > MAXSAT || sat2[2 * i + 1] < 1)
            {
                i = 0;
                continue;
            }
            if (rtk->ssat[sat2[2 * i] - 1].quickSelSatDel == 1 &&
                rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel == 0)
            {
                rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel = 1;
                sys                                           = satsys(sat2[2 * i + 1], &prn);
                trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                group--;
                i++;
                continue;
            }
            if (rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel == 1 &&
                rtk->ssat[sat2[2 * i] - 1].quickSelSatDel == 0)
            {
                rtk->ssat[sat2[2 * i] - 1].quickSelSatDel = 1;
                sys                                       = satsys(sat2[2 * i], &prn);
                trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                group--;
                i++;
                continue;
            }
            if (k < 0.3333)
            {
                el1 = rtk->ssat[sat2[2 * i] - 1].azel[0][1] * R2D;
                el2 = rtk->ssat[sat2[2 * i + 1] - 1].azel[0][1] * R2D;
                if ((mulFreq[sat2[2 * i] - 1]) == (mulFreq[sat2[2 * i + 1] - 1]))
                {
                    if (el1 < el2)
                    {
                        sys = satsys(sat2[2 * i], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                        rtk->ssat[sat2[2 * i] - 1].quickSelSatDel = 1;
                        if (el1 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                    }
                    else
                    {
                        sys = satsys(sat2[2 * i + 1], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                        rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel = 1;
                        if (el2 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                    }
                }
                else
                {
                    if (mulFreq[sat2[2 * i] - 1] <= mulFreq[sat2[2 * i + 1] - 1])
                    {
                        rtk->ssat[sat2[2 * i] - 1].quickSelSatDel = 1;
                        if (el1 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                    else
                    {
                        rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel = 1;
                        if (el2 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i + 1], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                }
            }
            else
            {
                el1 = rtk->ssat[sat2[2 * i] - 1].azel[0][1] * R2D;
                el2 = rtk->ssat[sat2[2 * i + 1] - 1].azel[0][1] * R2D;
                if ((mulFreq[sat2[2 * i] - 1]) == (mulFreq[sat2[2 * i + 1] - 1]))
                {
                    if (el1 > el2)
                    {
                        rtk->ssat[sat2[2 * i] - 1].quickSelSatDel = 1;
                        if (el1 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                    else
                    {
                        rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel = 1;
                        if (el2 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i + 1], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                }
                else
                {
                    if (mulFreq[sat2[2 * i] - 1] <= mulFreq[sat2[2 * i + 1] - 1])
                    {
                        rtk->ssat[sat2[2 * i] - 1].quickSelSatDel = 1;
                        if (el1 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                    else
                    {
                        rtk->ssat[sat2[2 * i + 1] - 1].quickSelSatDel = 1;
                        if (el2 >= thresel)
                        {
                            elMoreThan60--;
                        }
                        else
                        {
                            elLessThan60--;
                        }
                        sys = satsys(sat2[2 * i + 1], &prn);
                        trace(16, "quickSelSatDel sys=%3d prn=%3d\n", sys, prn);
                    }
                }
            }
            group--;
            i++;
        }
    }
}

extern int selsatRTK(
    rtk_t* rtk, const obsd_t* obs, unsigned char nu, unsigned char nr, const prcopt_t* opt,
    unsigned char* sat, unsigned char* iu, unsigned char* ir, unsigned char elFlag
)
{
    unsigned char i, j, k = 0, f, flag, nf = rtk->opt.nf;
    char          id[4];
    for (i = 0; i < nu; i++)
    {
        satno2id(obs[i].sat, id);
        if (rtk->ssat[obs[i].sat - 1].vs != 1)
        {
            trace(2, "sel,0, %s\n", id);
            continue;
        }
        if (rtk->ssat[obs[i].sat - 1].quickSelSatDel == 1)
        {
            trace(2, "sel,1, %s\n", id);
            continue;
        }
        if (rtk->ssat[obs[i].sat - 1].rejRes == 1)
        {
            trace(2, "sel,2, %s\n", id);
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
                        obs[j].L[f] != 0.0)
                    {
                        trace(2, "sel,5, %s,%d,%d\n", id, f, nf);

                        flag = 1;
                        break;
                    }
                }
                if (flag == 0)
                {
                    trace(2, "sel,3, %s,%d\n", id, nf);
                    continue;
                }
                if (elFlag == 1)
                {
                    if (rtk->ssat[obs[i].sat - 1].azel[0][1] * R2D <= 30.0 &&
                        rtk->ssat[obs[i].sat - 1].azel[1][1] * R2D <= 30.0)
                    {
                        trace(2, "sel,4, %s\n", id);
                        continue;
                    }
                }
                trace(
                    2, "sel,6,azel,%.2f, %.2f, %.2f\n", rtk->ssat[obs[i].sat - 1].azel[0][1],
                    rtk->ssat[obs[i].sat - 1].azel[1][1], opt->elmin
                );
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

/* select common satellites between rover and reference station --------------*/
extern void gloFlag(rtk_t* rtk, const obsd_t* obs, unsigned char nu, unsigned char nr)
{
    unsigned char nglo = 0, nsatExcGlo = 0;
    unsigned char i, j, f, flag, nf = rtk->opt.nf;
    unsigned char sys;
    for (i = 0; i < nu; i++)
    {
        if (rtk->ssat[obs[i].sat - 1].vs != 1)
        {
            continue;
        }
        // if (rtk->ssat[obs[i].sat - 1].rejRes == 1) continue;
        sys = satsys(obs[i].sat, NULL);
        for (j = nu; j < nu + nr; j++)
        {
            if (obs[i].sat == obs[j].sat)
            { /* elevation at base station */
                flag = 0;
                for (f = 0; f < nf; f++)
                {
                    if (obs[i].P[f] != 0.0 && obs[j].P[f] != 0.0 && obs[i].L[f] != 0.0 &&
                        obs[j].L[f] != 0.0)
                    {
                        flag = 1;
                        break;
                    }
                }
                if (flag == 0)
                {
                    continue;
                }
                if (rtk->ssat[obs[i].sat - 1].azel[0][1] >= rtk->opt.elmin &&
                    rtk->ssat[obs[i].sat - 1].azel[1][1] >= rtk->opt.elmin)
                {
                    if (sys == SYS_GLO)
                    {
                        nglo++;
                    }
                    else
                    {
                        nsatExcGlo++;
                    }
                }
                break;
            }
        }
    }
    if (nsatExcGlo > 7 && nglo > 0)
    {
        for (i = 0; i < nu; i++)
        {
            sys = satsys(obs[i].sat, NULL);
            if (sys == SYS_GLO)
            {
                rtk->ssat[obs[i].sat - 1].vs = 0;
            }
        }
    }
}

/* select common satellites between rover and reference station --------------*/
extern int selsatDGPS(
    rtk_t* rtk, const obsd_t* obs, int n, const prcopt_t* opt, unsigned char* sat,
    unsigned char* iu, unsigned char* ir
)
{
    unsigned char i, j, k = 0, nu, nr, f, flag, nf = rtk->opt.nf;
    for (i = 0; i < n; i++)
    {
        if (obs[i].rcv != 1)
        {
            break;
        }
    }
    nu = i;  // number of rover observations
    nr = n - nu;

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
        for (j = nu; j < nu + nr; j++)
        {
            if (obs[i].sat == obs[j].sat)
            { /* elevation at base station */
                flag = 0;
                for (f = 0; f < nf; f++)
                {
                    if (obs[i].P[f] != 0.0 && obs[j].P[f] != 0.0)
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
static void assignAmbX(rtk_t* rtk, int lcopt)
{
    unsigned char i, j, na = rtk->np + rtk->nt + rtk->ni,
                        naPre = rtk->npPre + rtk->ntPre + rtk->niPre;
    unsigned char sati, frqi, satj, frqj;
    if (lcopt == 0)
    {
        for (i = na; i < rtk->nx; i++)
        {
            sati = rtk->nxRecordSat[i - na];
            frqi = rtk->nxRecordFrq[i - na];
            for (j = naPre; j < rtk->nxPre; j++)
            {
                satj = rtk->nxRecordSatPre[j - naPre];
                frqj = rtk->nxRecordFrqPre[j - naPre];
                if (sati == satj && frqi == frqj)
                {
                    rtk->x[i] = rtk->xp[j];
                    break;
                }
            }
        }
    }
}

static int findPreAmbIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char frqi1, unsigned char satj1, unsigned char frqj1,
    unsigned char* row, unsigned char* column, int lcopt
)
{
    unsigned char i, j, na = rtk->npPre + rtk->ntPre + rtk->niPre;
    unsigned char sati2, frqi2;
    unsigned char satj2, frqj2;
    if (lcopt == 0)
    {
        for (i = na; i < rtk->nxPre; i++)
        {
            sati2 = rtk->nxRecordSatPre[i - na];
            frqi2 = rtk->nxRecordFrqPre[i - na];
            for (j = i + 1; j < rtk->nxPre; j++)
            {
                satj2 = rtk->nxRecordSatPre[j - na];
                frqj2 = rtk->nxRecordFrqPre[j - na];
                if (sati2 == sati1 && satj2 == satj1 && frqi2 == frqi1 && frqj2 == frqj1)
                {
                    *row    = i;
                    *column = j;

                    // trace(2,"findPreAmbIndex,nxRecord, i,%d,j,%d, xIndex, %d,%d\n", i,j,
                    // rtk->ssat[sati1-1].xIndex[frqi1],
                    // rtk->ssat[satj1-1].xIndex[frqj1]
                    // );
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void assignAmbP(rtk_t* rtk, int lcopt)
{
    unsigned char i, ii, jj, j, row, column, row2, column2, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char sati, frqi, satj, frqj;
    if (lcopt == 0)
    {
        for (i = na; i < rtk->nx; i++)
        {
            sati = rtk->nxRecordSat[i - na];
            frqi = rtk->nxRecordFrq[i - na];
            for (j = i + 1; j < rtk->nx; j++)
            {
                row = column = 0;
                satj         = rtk->nxRecordSat[j - na];
                frqj         = rtk->nxRecordFrq[j - na];
                if (findPreAmbIndex(rtk, sati, frqi, satj, frqj, &row, &column, lcopt))
                {
                    rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                    rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                    rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                    rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
                    trace(
                        3,
                        "assignAmbP, i, %d, j, %d, ii, %10.4f,   jj, %10.4f,ij, %10.4f,ji, "
                        "%10.4f\n",
                        i, j, rtk->P[i + rtk->nx * i], rtk->P[j + rtk->nx * j],
                        rtk->P[i + rtk->nx * j], rtk->P[j + rtk->nx * i]
                    );
                }
            }
        }
    }
}
static int trpIonIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char* row, unsigned char* column, unsigned char j
)
{
    unsigned char i, ni = rtk->npPre + rtk->ntPre;
    unsigned char sati2;
    for (i = ni; i < rtk->nsPre + ni; i++)
    {
        sati2 = rtk->nsSatPre[i - ni];
        if (sati2 == sati1)
        {
            *row    = i;
            *column = j;
            return 1;
        }
    }
    return 0;
}

static int tropAmbIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char frqi1, unsigned char* row, unsigned char* column,
    unsigned char j
)
{
    unsigned char i, na = rtk->npPre + rtk->ntPre + rtk->niPre;
    unsigned char sati2, frqi2;
    for (i = na; i < rtk->nxPre; i++)
    {
        sati2 = rtk->nxRecordSatPre[i - na];
        frqi2 = rtk->nxRecordFrqPre[i - na];
        if (sati2 == sati1 && frqi2 == frqi1)
        {
            *row    = i;
            *column = j;
            return 1;
        }
    }
    return 0;
}

static void assignTrp(rtk_t* rtk)
{
    unsigned char i, j;
    unsigned char row, column, na = rtk->np + rtk->nt + rtk->ni, ni = rtk->np + rtk->nt;
    unsigned char sati, frqi;
    if (rtk->nt == rtk->ntPre)
    {
        rtk->x[rtk->np]     = rtk->xp[rtk->np];
        rtk->x[rtk->np + 1] = rtk->xp[rtk->np + 1];
    }
    //----------------------trp-trp----------------------
    rtk->P[rtk->np + rtk->np * rtk->nx] = rtk->Pp[rtk->npPre + rtk->npPre * rtk->nxPre];
    rtk->P[(rtk->np + 1) + (rtk->np + 1) * rtk->nx] =
        rtk->Pp[(rtk->npPre + 1) + (rtk->npPre + 1) * rtk->nxPre];
    rtk->P[rtk->np + (rtk->np + 1) * rtk->nx] = rtk->Pp[rtk->npPre + (rtk->npPre + 1) * rtk->nxPre];
    rtk->P[(rtk->np + 1) + rtk->np * rtk->nx] = rtk->Pp[(rtk->npPre + 1) + rtk->npPre * rtk->nxPre];
    //----------------------trp-amp----------------------
    for (j = rtk->np; j < rtk->np + 2; j++)
    {
        for (i = na; i < rtk->nx; i++)
        {
            sati = rtk->nxRecordSat[i - na];
            frqi = rtk->nxRecordFrq[i - na];
            row = column = 0;
            if (tropAmbIndex(rtk, sati, frqi, &row, &column, j))
            {
                rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
            }
        }
    }
    //----------------------trp-ion----------------------
    if (rtk->opt.ionoopt == IONOOPT_EST && rtk->niPre != 0)
    {
        for (j = rtk->np; j < rtk->np + 2; j++)
        {
            for (i = ni; i < rtk->ns + ni; i++)
            {
                sati = rtk->nsSat[i - ni];
                row = column = 0;
                if (trpIonIndex(rtk, sati, &row, &column, j))
                {
                    rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                    rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                    rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                    rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
                }
            }
        }
    }
}

static int ionIonIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char satj1, unsigned char* row, unsigned char* column
)
{
    unsigned char i, j, ni = rtk->npPre + rtk->ntPre;
    unsigned char sati2, satj2;
    for (i = ni; i < rtk->nsPre + ni; i++)
    {
        sati2 = rtk->nsSatPre[i - ni];
        for (j = i + 1; j < rtk->nsPre + ni; j++)
        {
            satj2 = rtk->nsSatPre[j - ni];
            if (sati2 == sati1 && satj2 == satj1)
            {
                *row    = i;
                *column = j;
                return 1;
            }
        }
    }
    return 0;
}
static int ionAmbIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char frqi1, unsigned char* row, unsigned char* column,
    unsigned char ionSat1
)
{
    unsigned char i, j, ni = rtk->npPre + rtk->ntPre, na = rtk->npPre + rtk->ntPre + rtk->niPre;
    ;
    unsigned char sati2, frqi2, ionSat2;

    for (i = ni; i < rtk->nsPre + ni; i++)
    {
        ionSat2 = rtk->nsSatPre[i - ni];
        for (j = na; j < rtk->nxPre; j++)
        {
            sati2 = rtk->nxRecordSatPre[j - na];
            frqi2 = rtk->nxRecordFrqPre[j - na];
            if (sati2 == sati1 && frqi2 == frqi1 && ionSat2 == ionSat1)
            {
                *row    = i;
                *column = j;
                return 1;
            }
        }
    }
    return 0;
}
static int ionTrpIndex(
    rtk_t* rtk, unsigned char sati1, unsigned char* row, unsigned char* column, unsigned char j
)
{
    unsigned char i, ni = rtk->npPre + rtk->ntPre;
    unsigned char sati2;
    for (i = ni; i < rtk->nsPre + ni; i++)
    {
        sati2 = rtk->nsSatPre[i - ni];
        if (sati2 == sati1)
        {
            *row    = i;
            *column = j;
            return 1;
        }
    }
    return 0;
}

static void assignIon(rtk_t* rtk)
{
    int           k;
    unsigned char i, j, sati, satj, frqi, ni = rtk->np + rtk->nt, na = rtk->np + rtk->nt + rtk->ni;
    unsigned char row, column;
    unsigned char ionSat1;
    for (i = 0; i < rtk->ns; i++)
    {
        sati = rtk->nsSat[i];
        k    = -1;
        for (j = 0; j < rtk->nsPre; j++)
        {
            if (sati == rtk->nsSatPre[j])
            {
                k = j;
                break;
            }
        }
        if (k != -1)
        {
            rtk->x[i + rtk->np + rtk->nt] = rtk->xp[j + rtk->npPre + rtk->ntPre];
        }
    }
    //----------------------ion-ion----------------------
    for (i = ni; i < rtk->ns + ni; i++)
    {
        sati = rtk->nsSat[i - ni];
        for (j = i + 1; j < rtk->ns + ni; j++)
        {
            satj = rtk->nsSat[j - ni];
            row = column = 0;
            if (ionIonIndex(rtk, sati, satj, &row, &column))
            {
                rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
                // printf("P[%d,%d]=Pp[%d,%d]\n", i, j, row, column);
            }
        }
    }
    //----------------------ion-amb----------------------
    for (i = ni; i < rtk->ns + ni; i++)
    {
        ionSat1 = rtk->nsSat[i - ni];
        for (j = na; j < rtk->nx; j++)
        {
            sati = rtk->nxRecordSat[j - na];
            frqi = rtk->nxRecordFrq[j - na];
            row = column = 0;
            if (ionAmbIndex(rtk, sati, frqi, &row, &column, ionSat1))
            {
                rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
            }
        }
    }
    //----------------------ion-trp----------------------
    if (rtk->opt.tropopt == TROPOPT_EST && rtk->ntPre != 0)
    {
        for (i = ni; i < rtk->ns + ni; i++)
        {
            sati = rtk->nsSat[i - ni];
            for (j = rtk->np; j < rtk->np + 2; j++)
            {
                row = column = 0;
                if (ionTrpIndex(rtk, sati, &row, &column, j))
                {
                    rtk->P[i + rtk->nx * j] = rtk->Pp[row + rtk->nxPre * column];
                    rtk->P[j + rtk->nx * i] = rtk->Pp[column + rtk->nxPre * row];
                    rtk->P[i + rtk->nx * i] = rtk->Pp[row + rtk->nxPre * row];
                    rtk->P[j + rtk->nx * j] = rtk->Pp[column + rtk->nxPre * column];
                }
            }
        }
    }
}

extern int checkFixP(rtk_t* rtk, int lcopt)
{
    int    i;
    double check_p;
    if (lcopt == 0)
    {
        for (i = 0; i < 3; i++)
        {
            check_p = sqrt(fabs(rtk->Pp[i + i * rtk->nx]));
            if (check_p > 0.2)
            {
                trace(2, "FixP too large=%lf\n", check_p);
                rtk->sol.stat  = SOLQ_FLOAT;
                rtk->sol.ratio = 0.0;
                return 1;
            }
        }
    }
    return 0;
}
extern int checkFloatP(rtk_t* rtk, int lcopt)
{
    int    i;
    double check_p;
    if (lcopt == 0)
    {
        for (i = 0; i < 3; i++)
        {
            check_p = sqrt(fabs(rtk->Pp[i + i * rtk->nx]));
            if (check_p > 15.0)
            {
                trace(8, "float P too large=%lf\n", check_p);
                rtk->sol.stat  = SOLQ_NONE;
                rtk->sol.ratio = 0.0;
                return 1;
            }
        }
    }
    return 0;
}
extern int obsScan(rtk_t* rtk, const prcopt_t* popt, obsd_t* obs, const int nobs)
{
    unsigned char sati, nsat, sat[MAXOBS] = {0}, iu[MAXOBS] = {0}, ir[MAXOBS] = {0};
    unsigned char n, n1, n2, murFrq, sigFrq, nu, nr, f, ns, nx = 0, nxWL = 0, nf = rtk->opt.nf, sys,
                                                            prn;
    double pos1[3], pos2[3], dr[3], bl, differHeight;
    int    i, j;
    nsat = 0;
    if (rtk->sol.stat != 1)
    {
        for (i = 0; i < nobs; i++)
        {
            if (obs[i].rcv != 1)
            {
                break;
            }
            sati = obs[i].sat;
            if (rtk->ssat[sati - 1].quickSelSatDel == 1)
            {
                continue;
            }
            if (rtk->ssat[sati - 1].vs != 1)
            {
                continue;
            }
            if (rtk->ssat[sati - 1].azel[0][1] * R2D > 30.0 &&
                rtk->ssat[sati - 1].azel[1][1] * R2D > 30.0)
            {
                nsat++;
            }
        }
    }
    if (nsat > 10)
    {
        rtk->fix30flag = 1;
    }
    else
    {
        rtk->fix30flag = 0;
    }
    rtk->fix30flag = 0;

    rtk->nxPre = rtk->nx;
    rtk->npPre = rtk->np;
    rtk->ntPre = rtk->nt;
    rtk->niPre = rtk->ni;
    rtk->naPre = rtk->na;

    rtk->nsPre = rtk->ns;
    for (i = 0; i < rtk->ns; i++)
    {
        rtk->nsSatPre[i] = rtk->nsSat[i];
    }

    for (i = 0; i < NX; i++)
    {
        rtk->nxRecordSatPre[i] = rtk->nxRecordSat[i];
        rtk->nxRecordFrqPre[i] = rtk->nxRecordFrq[i];
        rtk->nxRecordSat[i]    = 0;
        rtk->nxRecordFrq[i]    = 0;
    }

    for (i = 0; i < nobs; i++)
    {
        if (obs[i].rcv != 1)
        {
            break;
        }
    }
    nu = i;  // number of rover observations
    nr = nobs - nu;

    ns = selsatRTK(rtk, obs, nu, nr, &rtk->opt, sat, iu, ir, rtk->fix30flag);
    // trace(2, "obsScan, selsatRTK, %d\n",ns);
    if (ns > SELETE_SAT_NUM)
    {
        trace(2, "error ns=%d ,SELETE_SAT_NUM, %d\n", ns, SELETE_SAT_NUM);
        return 0;
    }
    murFrq = 0;
    sigFrq = 0;
    for (i = 0; i < ns; i++)
    {
        n1 = 0;
        n2 = 0;
        for (f = 0; f < rtk->opt.nf; f++)
        {
            if (obs[iu[i]].L[f] != 0.0)
            {
                n1++;
            }
        }
        for (f = 0; f < rtk->opt.nf; f++)
        {
            if (obs[ir[i]].L[f] != 0.0)
            {
                n2++;
            }
        }
        if (n1 >= 2 && n2 >= 2)
        {
            murFrq++;
        }
        else
        {
            sigFrq++;
        }
    }
    ecef2pos(rtk->sol.rr, pos1);
    ecef2pos(rtk->rb, pos2);
    bl                    = baseline(rtk->sol.rr, rtk->rb, dr);
    rtk->opt.bl           = bl;
    differHeight          = fabs(pos1[2] - pos2[2]);
    rtk->opt.differHeight = differHeight;
    rtk->ns               = ns;
    for (i = 0; i < ns; i++)
    {
        rtk->nsSat[i] = sat[i];
    }

    // rtk->opt.ionoopt = IONOOPT_BRDC;
    // rtk->opt.tropopt = TROPOPT_SAAS;
    if (rtk->opt.bl > BSLTHRESHOLD && rtk->opt.mode != 3)
    {
        rtk->opt.ionoopt = IONOOPT_EST;
    }
    else
    {
        rtk->opt.ionoopt = IONOOPT_BRDC;
    }
    // if (rtk->opt.bl > 5E3 && rtk->opt.mode != 3 || differHeight > 200)
    rtk->opt.tropopt = TROPOPT_EST;
    // else
    rtk->opt.tropopt = TROPOPT_SAAS;
    rtk->opt.tropopt = TROPOPT_SAAS;
    // rtk->opt.ionoopt = IONOOPT_EST;
    //    rtk->opt.ionoopt = IONOOPT_BRDC;
    for (f = 0; f < nf; f++)
    {
        for (i = 0; i < ns; i++)
        {
            if (obs[iu[i]].P[f] != 0.0 && obs[iu[i]].L[f] != 0.0 && obs[ir[i]].P[f] != 0.0 &&
                obs[ir[i]].L[f] != 0.0)
            {
                rtk->nxRecordSat[nx]                    = obs[iu[i]].sat;
                rtk->nxRecordFrq[nx]                    = f;
                rtk->ssat[obs[iu[i]].sat - 1].xIndex[f] = nx;

                nx++;
            }
        }
    }

#if 0
    for (i = 0; i < ns; i++) {
        sati = obs[iu[i]].sat;
        sys = satsys(sati, &prn);
        if (sys == SYS_BDS && prn >= 19) {
            if (obs[iu[i]].P[0] != 0.0 && obs[iu[i]].L[0] != 0.0 && obs[ir[i]].P[0] != 0.0 && obs[ir[i]].L[0] != 0.0 &&
                obs[iu[i]].P[2] != 0.0 && obs[iu[i]].L[2] != 0.0 && obs[ir[i]].P[2] != 0.0 && obs[ir[i]].L[2] != 0.0) {
                nxWL++;
                if (3 + nxWL >= NXLC) {
                    trace(0x04, "nxWL too more:%d\n", nxWL);
                    break;
                }
            }
        } else {
            if (obs[iu[i]].P[0] != 0.0 && obs[iu[i]].L[0] != 0.0 && obs[ir[i]].P[0] != 0.0 && obs[ir[i]].L[0] != 0.0 &&
                obs[iu[i]].P[1] != 0.0 && obs[iu[i]].L[1] != 0.0 && obs[ir[i]].P[1] != 0.0 && obs[ir[i]].L[1] != 0.0) {
                nxWL++;
                if (3 + nxWL >= NXLC) {
                    trace(0x04, "nxWL too more:%d\n", nxWL);
                    break;
                }
            }
        }
    }
#endif
    n = 0;
    for (i = 0; i < nobs; i++)
    {
        for (j = 0; j < ns; j++)
        {
            // trace(2, "obsscan, i,j,obs[i].sat, sat[j], %d,%d,%d,%d\n",i,j,obs[i].sat, sat[j] );
            if (obs[i].sat == sat[j])
            {
                obs[n++] = obs[i];
                break;
            }
        }
    }

    // trace(2,"obsScan,WTF,n,%d,nobs,%d,ns,%d,i,%d,j,%d\n",n,nobs,ns,i,j);

    if (rtk->opt.dynamics == 0)
    {
        rtk->np = 3;
    }
    else if (rtk->opt.dynamics == 1)
    {
        rtk->np = 6;
    }
    else
    {
        rtk->opt.dynamics = 2;
        rtk->np           = 9;
    }
    if (rtk->opt.tropopt == TROPOPT_EST)
    {
        rtk->nt = 2;
    }
    else
    {
        rtk->opt.tropopt = TROPOPT_SAAS;
        rtk->nt          = 0;
    }
    if (rtk->opt.ionoopt == IONOOPT_EST)
    {
        rtk->ni = ns;
    }
    else
    {
        rtk->opt.ionoopt = IONOOPT_BRDC;
        rtk->ni          = 0;
    }
    trace(
        0x02, "opt:%d %d %d %d %d %f %.2f %.2f\n", rtk->opt.ionoopt, rtk->opt.tropopt, ns, murFrq,
        sigFrq, rtk->opt.std, bl, differHeight
    );
    if (rtk->opt.bl > BSLTHRESHOLD)
    {
        if (murFrq < 10)
        {
            trace(2, "obsScan, murFrq, %d\n", murFrq);
            return 0;
        }
    }
    rtk->na = nx;
    rtk->nx = rtk->np + rtk->nt + rtk->ni + rtk->na;

    memset(rtk->x, 0, sizeof(double) * NX);
    memset(rtk->P, 0, sizeof(double) * NX * NX);

    assignAmbX(rtk, 0);
    assignAmbP(rtk, 0);
    if (rtk->opt.tropopt == TROPOPT_EST && rtk->ntPre != 0)
    {
        assignTrp(rtk);
    }
    if (rtk->opt.ionoopt == IONOOPT_EST && rtk->niPre != 0)
    {
        assignIon(rtk);
    }

    return n;
}

extern int rtksvrinit(rtksvr_t* svr)
{
    eph_t  eph0  = {0, -1, -1};
    geph_t geph0 = {0, -1};
    int    i, j;
    for (i = 0; i < 2; i++)
    {
        svr->format[i] = 0;
    }
    for (i = 0; i < 2; i++)
    {
        svr->nb[i] = 0;
    }

    printf("calloc eph_t %d\n", sizeof(eph_t) * MAXSAT);
    printf("calloc obsd_t %d\n", sizeof(obsd_t) * MAXOBS * 2 * 128);
    printf("calloc double %d\n", sizeof(double) * MAXSAT * 3 * MAXSAT * 3);

    if (!(g_nav.eph = (eph_t*)calloc(sizeof(eph_t), MAXSAT)) ||
        !(g_nav.geph = (geph_t*)calloc(sizeof(geph_t), NSATGLO)))
    {
        ////trace(1, "rtksvrinit: calloc error\n");
        return 0;
    }

    g_nav.n     = MAXSAT;
    g_nav.ng    = NSATGLO;
    svr->navsel = 1;

    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 128; j++)
        {
            if (!(svr->obs[i][j].data = (obsd_t*)calloc(sizeof(obsd_t), MAXOBS)))
            {
                printf("rtksvrinit: calloc error\n");
                return 0;
            }
        }
    }
    for (i = 0; i < 2; i++)
    {
        memset(svr->rtcm + i, 0, sizeof(rtcm_t));
    }
    initlock(&svr->lock);
    return 1;
}

extern void rtksvrfree(rtksvr_t* svr)
{
    int i, j;
    if (g_nav.eph)
    {
        free(g_nav.eph);
    }
    if (g_nav.geph)
    {
        free(g_nav.geph);
    }
    for (i = 0; i < 2; i++)
    {
        free(svr->buff[i]);
        free(svr->pbuf[i]);
        free_rtcm(&svr->rtcm[i]);
    }
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 128; j++)
        {
            free(svr->obs[i][j].data);
        }
    }
    free(svr->rtk.x);
    free(svr->rtk.P);
    free(svr->rtk.xp);
    free(svr->rtk.Pp);
    free(svr->rtk.I);
    free(svr->rtk.H);
    free(svr->rtk.F);
    free(svr->rtk.K);
    free(svr->rtk.Ri);
    free(svr->rtk.Rj);
    free(svr->rtk.R);
    free(svr->rtk.v);
    // rtkfree(&svr->rtk);
}

extern void free_rtcm(rtcm_t* rtcm)
{
    /* free memory for observation and ephemeris buffer */
    free(rtcm->obs.data);
    rtcm->obs.data = NULL;
    rtcm->obs.n    = 0;
}

extern int init_rtcm(rtcm_t* rtcm)
{
    gtime_t time0 = {0};
    obsd_t  data0 = {{0}};
    int     i;

    rtcm->staid = rtcm->stah = rtcm->seqno = rtcm->outtype = 0;
    rtcm->time = rtcm->time_s = time0;
    rtcm->sta.name[0] = rtcm->sta.marker[0] = '\0';
    rtcm->sta.antdes[0] = rtcm->sta.antsno[0] = '\0';
    rtcm->sta.rectype[0] = rtcm->sta.recver[0] = rtcm->sta.recsno[0] = '\0';
    rtcm->sta.antsetup = rtcm->sta.itrf = rtcm->sta.deltype = 0;
    for (i = 0; i < 3; i++)
    {
        rtcm->sta.pos[i] = rtcm->sta.del[i] = 0.0;
    }
    rtcm->sta.hgt = 0.0;
    // rtcm->dgps = NULL;
    // for (i = 0; i<MAXSAT; i++) {
    //     rtcm->ssr[i] = ssr0;
    // }
    rtcm->msg[0] = rtcm->msgtype[0] = rtcm->opt[0] = '\0';
    rtcm->obsflag = rtcm->ephsat = 0;

    rtcm->nbyte = rtcm->nbit = rtcm->len = 0;
    rtcm->word                           = 0;

    rtcm->obs.data = NULL;

    /* reallocate memory for observation and ephemris buffer */
    if (!(rtcm->obs.data = (obsd_t*)calloc(sizeof(obsd_t), MAXOBS)))
    {
        free_rtcm(rtcm);
        return 0;
    }
    rtcm->obs.n = 0;
    for (i = 0; i < MAXOBS; i++)
    {
        rtcm->obs.data[i] = data0;
    }
    return 1;
}

extern void generateSatBuf(rtk_t* rtk, char** p)
{
    unsigned char i, j;
    unsigned char sys;
    unsigned char prn;
    double        azel[2]    = {0.0};
    double        CN0[NFREQ] = {0.0};
    double        r          = 0.0;
    double        e[3]       = {0.0};
    double        pos[3]     = {0.0};
    int           qzscnt     = 0;
    //*p += sprintf(*p, "sat,");

    for (i = 0; i < MAXSAT; i++)
    {
        if (rtk->ssat[i].rs[0] != 0)
        {
            sys     = satsys(i + 1, &prn);
            azel[1] = rtk->ssat[i].azel[0][0] * R2D;
            azel[0] = rtk->ssat[i].azel[0][1] * R2D;
            for (j = 0; j < NFREQ; j++)
            {
                CN0[j] = rtk->ssat[i].SNR[j] / 4.0;
            }

            if (CN0[0] != 0 || CN0[1] != 0 || CN0[2] != 0 || CN0[3] != 0 || CN0[4] != 0 ||
                CN0[5] != 0)
            {
                if (azel[0] == 0 || azel[1] == 0)
                {
                    if ((r = geodist(rtk->ssat[i].rs, rtk->sol.rr, e)) <= 0)
                    {
                        continue;
                    }
                    ecef2pos(rtk->sol.rr, pos);
                    if (satazel(pos, e, azel) < rtk->opt.elmin)
                    {
                        continue;
                    }
                    azel[0] *= R2D;
                    azel[1] *= R2D;
                }
                // sat,卫星类型,卫星号,仰角,方位角,信噪比;repeated /r/n
                if (sys == SYS_GPS)
                {
                    *p += sprintf(
                        *p, "0,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1],
                        CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]
                    );
                }
                else if (sys == SYS_QZS)
                {
                    qzscnt++;
                    *p += sprintf(
                        *p, "1,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1],
                        CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]
                    );
                }
                else if (sys == SYS_BDS)
                {
                    *p += sprintf(
                        *p, "2,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1],
                        CN0[0], CN0[1], CN0[5], CN0[4], CN0[2], CN0[3]
                    );
                }
                else if (sys == SYS_GAL)
                {
                    *p += sprintf(
                        *p, "3,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1],
                        CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]
                    );
                }
                else if (sys == SYS_GLO)
                {
                    *p += sprintf(
                        *p, "4,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f;", prn, azel[0], azel[1],
                        CN0[0], CN0[1], CN0[2], CN0[3], CN0[4], CN0[5]
                    );
                }
            }
        }
    }
    if (qzscnt == 0)
    {
        *p += sprintf(
            *p, "dtr,0:%.3f,1:%.3f,2:%.3f,3:%.3f,4:%.3f", rtk->sol.dtr[0] * CLIGHT, 0.0,
            rtk->sol.dtr[1] * CLIGHT, rtk->sol.dtr[2] * CLIGHT, rtk->sol.dtr[3] * CLIGHT
        );
    }
    else
    {
        *p += sprintf(
            *p, "dtr,0:%.3f,1:%.3f,2:%.3f,3:%.3f,4:%.3f", rtk->sol.dtr[0] * CLIGHT,
            rtk->sol.dtr[0] * CLIGHT, rtk->sol.dtr[1] * CLIGHT, rtk->sol.dtr[2] * CLIGHT,
            rtk->sol.dtr[3] * CLIGHT
        );
    }
    //*p += sprintf(*p, "\n");
    return;
}

// Initialize Configuration Option for User Setting
extern void initCfgOpt(cfgopt_t* opt)
{
    opt->kMode                = 1;    /* Kinematic Calculation Mode Smoothed Real-Time Kinematic */
    opt->freq                 = 15;   /* L1+L2+L5=1+2+4  7:all freq*/
    opt->iono                 = 0;    /* disable Ionosphere Correction */
    opt->trop                 = 0;    /* enable Troposphere Correction */
    opt->tides                = 0;    /* disable Tides Correction */
    opt->sys                  = 31;   /* GPS+QZS+BDS+GAL+GLO 0001 1111*/
    opt->cn0Min               = 30;   /* C/N0 Cut-off */
    opt->diffAgeMax           = 30;   /* Max Age of Diff , unit:s */
    opt->buffSize             = 4;    /*Buffer Size Default:4kB */
    opt->elevMin              = 15;   /* Elevation Cut-off */
    opt->gdopThld             = 4.0;  /* GDOP Threshold */
    opt->postResThld          = 0.02; /* Posterior Residual Threshold*/
    opt->timeInterval         = 15.0; /* Time Interval 1s */
    opt->stationPCV[0]        = 0.0;
    opt->stationPCV[1]        = 0.0;
    opt->stationPCV[2]        = 0.0;
    opt->senceopt             = 0;
    opt->initEnuTime          = 1;
    opt->smoothWindowsTime    = 12;
    opt->detectSensitivity    = 10;
    opt->typeSol              = 0;
    opt->timeIntervalSolution = 0;

    opt->minFixSat      = 5;
    opt->maxDelSat      = 5;
    opt->minSatRes      = 0.02;
    opt->gpsMask        = -1;
    opt->qzssMask       = -1;
    opt->glonassMask    = -1;
    opt->galieoMask     = -1;
    opt->bdsMask        = -1;
    opt->iggiiik0       = 1.5;
    opt->iggiiik1       = 3.0;
    opt->param          = 0;
    opt->maxPosSat      = 40;
    opt->useRtcmPosFlag = 0;
}

extern void loadCfgOpt(cfgopt_t* cfgOpt, char** argv, int i)
{
    char* cfgfile;
    cfgfile = argv[i++];

    cfgfile              = argv[i++];
    cfgOpt->timeInterval = atof(cfgfile);
    printf("timeInterval:%.2f\n", cfgOpt->timeInterval);

    cfgfile      = argv[i++];
    cfgOpt->freq = atoi(cfgfile);
    printf("freq:%d\n", cfgOpt->freq);

    cfgfile      = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("iono:%d\n", cfgOpt->iono);
    cfgfile      = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("trop:%d\n", cfgOpt->trop);
    cfgfile      = argv[i++];
    cfgOpt->iono = atoi(cfgfile);
    printf("tides:%d\n", cfgOpt->tides);

    cfgfile         = argv[i++];
    cfgOpt->elevMin = atof(cfgfile);
    printf("elevMin:%.2f\n", cfgOpt->elevMin);

    cfgfile        = argv[i++];
    cfgOpt->cn0Min = atof(cfgfile);
    printf("cn0Min:%.2f\n", cfgOpt->cn0Min);

    cfgfile          = argv[i++];
    cfgOpt->gdopThld = atof(cfgfile);
    printf("gdopThld:%.2f\n", cfgOpt->gdopThld);

    cfgfile            = argv[i++];
    cfgOpt->diffAgeMax = atoi(cfgfile);
    printf("diffAgeMax:%d\n", cfgOpt->diffAgeMax);

    cfgfile     = argv[i++];
    cfgOpt->sys = atoi(cfgfile);
    printf("sys:%d\n", cfgOpt->sys);

    cfgfile             = argv[i++];
    cfgOpt->postResThld = atof(cfgfile);
    printf("postResThld:%.2f\n", cfgOpt->postResThld);

    cfgfile       = argv[i++];
    cfgOpt->kMode = atoi(cfgfile);
    printf("kMode:%d\n", cfgOpt->kMode);

    cfgfile          = argv[i++];
    cfgOpt->buffSize = atoi(cfgfile);
    printf("buffSize:%d\n", cfgOpt->buffSize);

    cfgfile               = argv[i++];
    cfgOpt->stationPCV[0] = atoi(cfgfile);
    cfgfile               = argv[i++];
    cfgOpt->stationPCV[1] = atoi(cfgfile);
    cfgfile               = argv[i++];
    cfgOpt->stationPCV[2] = atoi(cfgfile);
    printf(
        "stationPCV[0]=%.2f stationPCV[1]=%.2f stationPCV[2]=%.2f\n", cfgOpt->stationPCV[0],
        cfgOpt->stationPCV[1], cfgOpt->stationPCV[2]
    );

    cfgfile          = argv[i++];
    cfgOpt->senceopt = atoi(cfgfile);
    printf("senceopt=%d\n", cfgOpt->senceopt);

    cfgfile                   = argv[i++];
    cfgOpt->smoothWindowsTime = atof(cfgfile);
    printf("smoothWindowsTime=%.2f\n", cfgOpt->smoothWindowsTime);

    cfgfile             = argv[i++];
    cfgOpt->initEnuTime = atof(cfgfile);
    printf("initEnuTime=%.2f\n", cfgOpt->initEnuTime);

    cfgfile                   = argv[i++];
    cfgOpt->detectSensitivity = atoi(cfgfile);
    printf("detectSensitivity=%d\n", cfgOpt->detectSensitivity);

    cfgfile         = argv[i++];
    cfgOpt->typeSol = atoi(cfgfile);
    printf("typeSol=%d\n", cfgOpt->typeSol);

    cfgfile                      = argv[i++];
    cfgOpt->timeIntervalSolution = atoi(cfgfile);
    printf("timeIntervalSolution=%d\n", cfgOpt->timeIntervalSolution);
}

// Set Configuration Option for User Setting to Processing Option
extern void setCfgOpt(cfgopt_t cfgOpt, prcopt_t* procOpt)
{
    procOpt->kMode = cfgOpt.kMode;
    procOpt->freq  = cfgOpt.freq;

    procOpt->ioncfg = cfgOpt.iono;
    procOpt->trocfg = cfgOpt.trop;

    if (cfgOpt.iono == 0)
    {
        procOpt->ionoopt = IONOOPT_BRDC;
    }
    else if (cfgOpt.iono == 1)
    {
        procOpt->ionoopt = IONOOPT_EST;
    }
    else
    {
        procOpt->ionoopt = IONOOPT_BRDC;
    }

    if (cfgOpt.trop == 0)
    {
        procOpt->tropopt = TROPOPT_SAAS;
    }
    else if (cfgOpt.trop == 1)
    {
        procOpt->tropopt = TROPOPT_EST;
    }
    else
    {
        procOpt->ionoopt = TROPOPT_SAAS;
    }

    procOpt->tidecorr             = cfgOpt.tides;
    procOpt->sys                  = cfgOpt.sys;
    procOpt->cn0Min               = cfgOpt.cn0Min;
    procOpt->buffSize             = cfgOpt.buffSize * 1024;
    procOpt->elmin                = cfgOpt.elevMin * D2R;
    procOpt->maxtdiff             = cfgOpt.diffAgeMax;
    procOpt->maxgdop              = cfgOpt.gdopThld;
    procOpt->postResThld          = cfgOpt.postResThld;
    procOpt->senceopt             = cfgOpt.senceopt;
    procOpt->timeInterval         = cfgOpt.timeInterval;
    procOpt->smoothWindowsTime    = cfgOpt.smoothWindowsTime;
    procOpt->initEnuTime          = cfgOpt.initEnuTime;
    procOpt->detectSensitivity    = cfgOpt.detectSensitivity;
    procOpt->typeSol              = cfgOpt.typeSol;
    procOpt->timeIntervalSolution = cfgOpt.timeIntervalSolution;

    procOpt->minFixSat         = cfgOpt.minFixSat;
    procOpt->maxDelSat         = cfgOpt.maxDelSat;
    procOpt->minSatRes         = cfgOpt.minSatRes;
    procOpt->gpsMask           = cfgOpt.gpsMask;
    procOpt->qzssMask          = cfgOpt.qzssMask;
    procOpt->glonassMask       = cfgOpt.glonassMask;
    procOpt->galieoMask        = cfgOpt.galieoMask;
    procOpt->bdsMask           = cfgOpt.bdsMask;
    procOpt->iggiiik0          = cfgOpt.iggiiik0;
    procOpt->iggiiik1          = cfgOpt.iggiiik1;
    procOpt->param             = cfgOpt.param;
    procOpt->maxPosSat         = cfgOpt.maxPosSat;
    procOpt->rb[0]             = cfgOpt.rb[0];
    procOpt->rb[1]             = cfgOpt.rb[1];
    procOpt->rb[2]             = cfgOpt.rb[2];
    procOpt->enuWindow[0]      = cfgOpt.enuWindow[0];
    procOpt->enuWindow[1]      = cfgOpt.enuWindow[1];
    procOpt->enuWindow[2]      = cfgOpt.enuWindow[2];
    procOpt->enuWindowIndex[0] = cfgOpt.enuWindowIndex[0];
    procOpt->enuWindowIndex[1] = cfgOpt.enuWindowIndex[1];
    procOpt->enuWindowIndex[2] = cfgOpt.enuWindowIndex[2];
#ifdef MULBASE
    procOpt->masterSlaveBaseFlag = cfgOpt.masterSlaveBaseFlag;
    procOpt->masterXyz[0]        = cfgOpt.masterXyz[0];
    procOpt->masterXyz[1]        = cfgOpt.masterXyz[1];
    procOpt->masterXyz[2]        = cfgOpt.masterXyz[2];
    procOpt->slaveXyz[0]         = cfgOpt.slaveXyz[0];
    procOpt->slaveXyz[1]         = cfgOpt.slaveXyz[1];
    procOpt->slaveXyz[2]         = cfgOpt.slaveXyz[2];
#endif
    // time interval
}
