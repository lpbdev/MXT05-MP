#include"rtk.h"
#include "multipath.h"

#define SQRT(x)    ((x)<0.0?0.0:sqrt(x))
static double ori_min[3], ori_max[3];

static double varerr_gamit(unsigned char sat, unsigned char sys, double el, double bl, int f, const prcopt_t* opt)
{
    double a, b, c = 0 * bl / 1E4;
    double sinel;
    a = 0.003;
    b = 0.003;
    sinel = sin(el);
    return 2.0 * (a * a + b * b / sinel / sinel + c * c);
}

// static double varrL(const obsd_t* obs, double el, double bl, int f, const prcopt_t* opt)
// {
//     double a, b, c = 0 * bl / 1E4;
//     double sinel, var;
//     a = 0.003;
//     b = 0.003;
//     sinel = sin(el);
//     var = 2.0 * (a * a + b * b / sinel / sinel + c * c);

//     //if (obs->LockTime[f] < 3000)
//     //    var = var + (3000 - obs->LockTime[f])/16000.0;
//     if (obs->SNR[f] / 4.0 > 40)
//         var *= 1.0;
//     else if (obs->SNR[f] / 4.0 > 30)
//         var *= 5;
//     else if (obs->SNR[f] / 4.0 > 10)
//         var *= 2.0;
//     else
//         var *= 5.0;

//     if (obs->LockTime[f] < 1000) {
//         var *= 2.0;
//     }
//     //return 2.0 * (a * a + b * b / sinel / sinel + c * c);
//     return 1;
// }

static double baseline(const double* ru, const double* rb, double* dr)
{
    int i;
    for (i = 0; i < 3; i++) dr[i] = ru[i] - rb[i];
    return norm(dr, 3);
}


/* select common satellites between rover and reference station --------------*/
static int selsat(const obsd_t* obs, int nu, int nr, int* sat, int* iu, int* ir)
{
    int i, j, k = 0, tmp;
    for (i = 0; i < nu; i++) {
        tmp = (obs[i].sat);
        for (j = nu; j < nu + nr; j++) {
            if (tmp == (obs[j].sat)) { /* elevation at base station */
                sat[k] = tmp; iu[k] = i; ir[k++] = j;
                break;
            }
        }
    }
    return k;
}

static int rescode(rtk_t* rtk, int post, const obsd_t* obs, int n, int nu, const int* svh, double* rs, double* dts,
    double* H, double* v, double* var_sat, double* var, double* x, unsigned char* exc, double* lsqraim,
    const prcopt_t* opt, unsigned char ns, unsigned char* sat, unsigned char* iu, unsigned char* ir,
    double* stdv, unsigned char* nvSatMask)
{
    unsigned char sys, prni, prnj;
    int i, j, k, m, f, flag, nf = opt->nf, base_sat, nv = 0;
    double pos1[3], e1[3], eb1[3], rr1[3];
    double pos2[3], e2[3], eb2[3], rr2[3];
    double Lb1, Lb2, L1, L2, rb1, rb2, r1, r2, zhd;
    double bl, dr[3], amb, * Hi = NULL, zazel[] = { 0.0,90.0 * D2R };;
    double Ri, Rj, differHeight, lami[NFREQ], lamj[NFREQ], C;
    int sat1, sat2, index1 = 0, index2 = 0;
    double C1, C2, ion1, ion2;
    for (i = 0; i < 3; i++) lsqraim[i] = 0;
    for (i = 0; i < 3; i++) rr1[i] = x[i];
    for (i = 0; i < 3; i++) rr2[i] = rtk->rb[i];
    //printf("x=%14.4lf y=%14.4lf z=%14.4lf\n", rr2[0], rr2[1], rr2[2]);

    ecef2pos(rr1, pos1);
    ecef2pos(rr2, pos2);

    differHeight = fabs(pos2[2] - pos1[2]);
    for (i = 0; i < MAXSAT; i++) {
        for (f = 0; f < NFREQ; f++) {
            rtk->ssat[i].vsat[f] = 0;
        }
    }
    bl = baseline(x, opt->rb, dr);
    //dt = timediff(obs[0].time, obs[nu].time);

    flag = 0;
    for (i = 0; i < MAXSAT; i++) {
        if (rtk->ssat[i].fix_amb[0] != 9999.9) {
            flag = 1;
            break;
        }
    }
    if (flag == 1)
        nf = 1;

    for (m = 0; m < NSYS; m++) {
        for (f = 0; f < nf; f++) {
            base_sat = rtk->basePrnLsq[m][f];
            for (i = -1, j = 0; j < ns; j++) {
                if (sat[j] == base_sat) {
                    i = j;
                    break;
                }
            }
            for (k = 0; k < nu; k++) {
                if (obs[k].sat == base_sat) {
                    index1 = k;
                    break;
                }
            }
            for (k = nu; k < n; k++) {
                if (obs[k].sat == base_sat) {
                    index2 = k;
                    break;
                }
            }
            if (i == -1) continue;
            if (sat[i] == 0) continue;
            if (exc[sat[i] - 1] == 1)continue;
            if (rtk->ssat[sat[j] - 1].vs != 1) continue;
            if (rtk->ssat[sat[i] - 1].fix_amb[f] == 9999.9) continue;
            if ((rtk->ssat[sat[i] - 1].slip[f] & 1) || (rtk->ssat[sat[i] - 1].slip[f] & 2)) {
                rtk->ssat[sat[i] - 1].fix_amb[f] = 9999.9;
                continue;
            }
            satsys(sat[i], &prni);

            if ((rb1 = geodist(rs + index1 * 6, rr1, eb1)) <= 0)    continue;  //参考星至接收机几何距离
            if ((rb2 = geodist(rs + index2 * 6, rr2, eb2)) <= 0)    continue;  //参考星至基准站几何距离
            rb1 -= CLIGHT * dts[index1 * 2];
            rb2 -= CLIGHT * dts[index2 * 2];

            sat1 = sat[i];
            //if (differHeight > 15 || bl>1.1E4) {
            if (rtk->opt.mode != 4) {
                zhd = tropmodel(obs[0].time, pos1, zazel, 0.0);
                rb1 += tropmapf(obs[0].time, pos1, rtk->ssat[sat1 - 1].azel[0], NULL) * zhd;

                zhd = tropmodel(obs[0].time, pos2, zazel, 0.0);
                rb2 += tropmapf(obs[0].time, pos2, rtk->ssat[sat1 - 1].azel[1], NULL) * zhd;
            }

            for (j = 0; j < ns; j++) {
                //vsat[j+f*ns] = 0;azel[j * 2] = azel[j * 2 + 1] = resp[j+ f * ns] = 0.0;

                if (i == j) continue;
                if (svh[j])    continue;
                if (exc[sat[j] - 1] == 1)continue;
                if (rtk->ssat[sat[j] - 1].vs != 1) continue;
                sys = satsys(sat[j], &prnj);
                if (sys == SYS_NONE) continue;
                if (!test_sys(sys, m))    continue;
                if (obs[iu[i]].L[f] == 0.0 || obs[ir[i]].L[f] == 0.0 || obs[iu[j]].L[f] == 0.0 || obs[ir[j]].L[f] == 0.0) continue;
                for (k = 0; k < nu; k++) {
                    if (obs[k].sat == sat[j]) {
                        index1 = k;
                        break;
                    }
                }
                for (k = nu; k < n; k++) {
                    if (obs[k].sat == sat[j]) {
                        index2 = k;
                        break;
                    }
                }
                if (rtk->ssat[sat[j] - 1].fix_amb[f] == 9999.9) continue;
                if ((r1 = geodist(rs + index1 * 6, rr1, e1)) <= 0)    continue; //非参考星至接收机几何距离
                if ((r2 = geodist(rs + index2 * 6, rr2, e2)) <= 0)    continue; //非参考星至基准站几何距离
                r1 -= CLIGHT * dts[index1 * 2];
                r2 -= CLIGHT * dts[index2 * 2];

                sat2 = sat[j];
                //satazel(pos1, e1, rtk->ssat[sat2 - 1].azel[0]);
                //satazel(pos2, e2, rtk->ssat[sat2 - 1].azel[1]);

                //if (differHeight > 15 || bl>1.1E4) {
                if (rtk->opt.mode != 4) {
                    zhd = tropmodel(obs[0].time, pos1, zazel, 0.0);
                    r1 += tropmapf(obs[0].time, pos1, rtk->ssat[sat2 - 1].azel[0], NULL) * zhd;

                    zhd = tropmodel(obs[0].time, pos2, zazel, 0.0);
                    r2 += tropmapf(obs[0].time, pos2, rtk->ssat[sat2 - 1].azel[1], NULL) * zhd;
                }

                if (sys == SYS_GPS || sys == SYS_QZS) {
                    for (k = 0; k < NFREQ; k++) lami[k] = lamj[k] = g_gpsLam[k];
                }
                else if (sys == SYS_GLO) {
                    for (k = 0; k < NFREQ; k++) lami[k] = g_gloLam[prni - 1][k];
                    for (k = 0; k < NFREQ; k++) lamj[k] = g_gloLam[prnj - 1][k];
                }
                else if (sys == SYS_GAL) {
                    for (k = 0; k < NFREQ; k++) lami[k] = lamj[k] = g_galLam[k];
                }
                else {
                    for (k = 0; k < NFREQ; k++) lami[k] = lamj[k] = g_bdsLam[k];
                }
                flag = 0;
                for (k = 0; k < rtk->nsLsq[f]; k++) {
                    if (sat[j] == rtk->satLsq[f][k]) {
                        ion1 = rtk->ssat[sat[i] - 1].fix_ion;
                        ion2 = rtk->ssat[sat[j] - 1].fix_ion;
                        amb = rtk->ssat[sat[i] - 1].fix_amb[f] * lami[f] - rtk->ssat[sat[j] - 1].fix_amb[f] * lamj[f];
                        flag = 1;
                        break;
                    }
                }
                if (flag == 0) continue;

                Lb1 = obs[iu[i]].L[f] * lami[f] - rb1;
                Lb2 = obs[ir[i]].L[f] * lami[f] - rb2;
                L1 = obs[iu[j]].L[f] * lamj[f] - r1;
                L2 = obs[ir[j]].L[f] * lamj[f] - r2;

                C = SQR(lamj[f % nf] / lamj[0]) * (f / nf == 0 ? -1.0 : 1.0);
                if (post == 0) {
                    rtk->ssat[sat[j] - 1].ddlcru = L1 - L2;
                    rtk->ssat[sat[i] - 1].ddlcru = Lb1 - Lb2;
                }
                //v[nv] = Lb1 - Lb2 - (L1 - L2) - (rtk->ssat[sat[i] - 1].ddl- rtk->ssat[sat[j] - 1].ddl);
                //if (fabs(v[nv]) > 0.02) continue;
                v[nv] = Lb1 - Lb2 - (L1 - L2) - amb - 0 * (rtk->ssat[sat[j] - 1].ddion - rtk->ssat[sat[j] - 1].ddtrp);

                double corr_j = 0.0;
                char tstr[64];
                time2str(obs[0].time, tstr,1);
                char id[4];
                satno2id(obs[j].sat, id);
                if (rtk->mpflag == 1 && f==0) {

                    gtime_t ctime = obs[0].time;
                    double epochlen  = get_sid_T(sat[j], obs[0].time, &g_nav);
                    trace(2, "$EPH, %s,%s, %.2f,%d\n", tstr,id, epochlen,(int)(epochlen / rtk->opt.timeInterval));
                    ctime.time = ctime.time - ((int)(epochlen / rtk->opt.timeInterval)) * rtk->opt.timeInterval;
                    int offset2 = calOffset(obs[j].sat, ctime, (int)rtk->opt.timeInterval);

                    corr_j = getDat(rtk->ssat[obs[j].sat].fp_ssat, offset2);

                    if (post == 0 && fabs(v[nv])<0.02) {
                        resdata_t data;
                        data.sod = obs[0].time.time + obs[0].time.frac;
                        data.res = v[nv];
#if 0
                        satres_add(rtk->ssat[sat[j] - 1].satres, &data);
#endif
                        int offset = calOffset(sat[j], obs[0].time, rtk->opt.timeInterval);

                        writeDat(rtk->ssat[obs[j].sat].fp_ssat, offset, data.res);


                        trace(2, "RES0, %s, %s, %d, %.4f,%d\n",tstr, id,f,v[nv],obs[j].sat);
                    }
                }

                v[nv] += -corr_j;
                if(post==0){
                    trace(2, "RES1, %s, %s, %d, %.4f, %d\n",tstr, id,f, v[nv],obs[j].sat);
                }

                //if (post == 0)
                //    printf("*****f=%d v=%10.2f sat1=%3d sat2=%3d N1=%10.2f N2=%10.2f N1-N2=%10.2f\n",
                //        f,v[nv],sat[j], sat[i], rtk->ssat[sat[j] - 1].fix_amb[f], rtk->ssat[sat[i] - 1].fix_amb[f], rtk->ssat[sat[j] - 1].fix_amb[f] - rtk->ssat[sat[i] - 1].fix_amb[f]);

                if (sys == SYS_GLO) {
                    C1 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);
                    C2 = SQR(lamj[f % nf] / lamj[0]) * (f / nf == 0 ? -1.0 : 1.0);
                }
                else
                    C1 = C2 = SQR(lami[f % nf] / lami[0]) * (f / nf == 0 ? -1.0 : 1.0);

                if (opt->ionoopt == IONOOPT_EST) {
                    v[nv] -= C1 * ion1 - C2 * ion2;
                    //printf("C=%lf ion1=%lf ion2=%lf\n", C, ion1, ion2);
                }
                if (opt->tropopt == TROPOPT_EST) {
                    v[nv] -= rtk->ssat[sat[j] - 1].fix_trop;
                }
                if (H) Hi = H + nv * 3;
                if (H) {
                    for (k = 0; k < 3; k++) {
                        Hi[k] = -eb1[k] + e1[k];
                    }
                }
                rtk->ssat[sat[j] - 1].vsat[f] = 1;

                //Ri = varerr_gamit(sat[i], sys, rtk->ssat[sat1 - 1].azel[0][1], bl,f,opt);
                //Rj = varerr_gamit(sat[j], sys, rtk->ssat[sat2 - 1].azel[0][1], bl,f,opt);

                Ri = varrL(&obs[iu[j]], rtk->ssat[sat1 - 1].azel[0][1], bl, f, opt);
                Rj = varrL(&obs[iu[j]], rtk->ssat[sat2 - 1].azel[0][1], bl, f, opt);

                var[nv] = rtk->ssat[sat1 - 1].Ri + rtk->ssat[sat2 - 1].Ri;
                stdv[nv] = fabs(v[nv]) / sqrt(var[nv]);

                if (sys == SYS_BDS && prnj <= 5) {
                  var[nv] *= 10;
                }

                nvSatMask[nv] = sat[j];
                if (stdv[nv] > lsqraim[2])
                {
                    lsqraim[0] = sat[j];
                    lsqraim[1] = fabs(v[nv]);
                    lsqraim[2] = stdv[nv];
                }
                nv++;
            }
        }
    }
    //printf("----------------------------\n");
    return nv;
}
static int pvtAvaildSatCnt(rtk_t* rtk) {
    int i, n = 0;
    for (i = 0; i < MAXSAT; i++) {
        if (rtk->ssat[i].vsat[0] == 1 || rtk->ssat[i].vsat[1] == 1 || rtk->ssat[i].vsat[2] == 1)
            n++;
    }
    return n;
}


extern int rtkLsq(rtk_t* rtk, const obsd_t* obs, int n, int nu, const int* svh, double* rs, double* dts, double* var_sat,
    double* lsqraim, unsigned char ns, unsigned char* sat, unsigned char* iu, unsigned char* ir, unsigned char* exc)
{
    unsigned char i, j, k, f, info, sys, prn, nsobs = 0, nbase = 0, post = 0, nv = 0, respMaxSat = 0;
    int nf = rtk->opt.nf;
    double x[3] = { 0 }, dx[3], Q[3 * 3], dr = 0.0;
    double* H, * v, * var, sig;
    double *stdv, stdvMax = 0.0;
    unsigned char *nvSatMask;

    H = mat(3, ns * nf); v = mat(ns * nf, 1); var = mat(ns * nf, 1);
    stdv = zeros(SELETE_SAT_NUM * NFREQ, 1);
    nvSatMask = (uint8_t *)(calloc(sizeof(uint8_t), SELETE_SAT_NUM));
    // nvSatMask = zerosChar(SELETE_SAT_NUM * NFREQ, 1);

    if ((!H) || (!v) || (!var))
    {
        trace(0x01, "estpos mat allocate error");
        free(H); free(v); free(var); free(stdv); free(nvSatMask);
        return -1;
    }
    for (i = 0; i < NSYS; i++) {
        for (f = 0; f < rtk->opt.nf; f++) {
            if (rtk->basePrnLsq[i][f] != 0) {
                nbase++;
                break;
            }
        }
    }
    //for (i = 0; i < 3; i++)
    //    dr = dr + (rtk->sol.rr_ref[i] - rtk->sol.rr[i]) * (rtk->sol.rr_ref[i] - rtk->sol.rr[i]);
    //if (sqrt(dr) > 0.1)
    //{
    //    for (i = 0; i < 3; i++)        rtk->sol.rr_ref[i] = rtk->sol.rr[i];
    //}
    //for (i = 0; i < 3; i++)        x[i] = rtk->sol.rr_ref[i];

    for (i = 0; i < 3; i++)        x[i] = rtk->sol.rr[i];
    for (i = 0; i < 10; i++) {
        nv = rescode(rtk, post, obs, n, nu, svh, rs, dts, H, v, var_sat, var, x, exc, lsqraim, &rtk->opt, ns, sat, iu, ir, stdv, nvSatMask);
        nsobs = pvtAvaildSatCnt(rtk);
        if (nsobs < 5) {
            trace(0x02, "rtk lsq lack of valid sat:%d\n", nsobs + nbase);
            free(H); free(v); free(var); free(stdv); free(nvSatMask);
            return -1;
        }
        /* weight by variance */
        for (j = 0; j < nv; j++) {
            sig = sqrt(var[j]);
            v[j] /= sig;
            for (k = 0; k < 3; k++) H[k + j * 3] /= sig;
        }
        if ((info = lsq(H, v, 3, nv, dx, Q))) {
            trace(0x02, "rtk lsq error\n");
            free(H); free(v); free(var); free(stdv); free(nvSatMask);
            return -1;
        }
        for (j = 0; j < 3; j++) x[j] += dx[j];
        if (norm(dx, 3) < 1E-3) {
            for (k = 0; k < nv; k++) {
                if (stdv[k] > stdvMax) {
                    stdvMax = stdv[k];
                    respMaxSat = nvSatMask[k];
                }
            }
            if (stdvMax > 6.0) {
                exc[respMaxSat - 1] = 1;
                rtk->sol.ns[1] = nsobs + nbase;
                sys = satsys(respMaxSat, &prn);
                lsqraim[0] = respMaxSat;
                lsqraim[2] = stdvMax;
                trace(0x04, "rtk lsq resc reject sys=%d prn=%d stdvMax=%lf\n", sys, prn, stdvMax);
                logmsg(0x04, "rtk lsq resc reject sys=%d prn=%d stdvMax=%lf\n", sys, prn, stdvMax);

                free(H); free(v); free(var); free(stdv); free(nvSatMask);
                return -2;
            }
            trace(0x02, "rtk lsq pos:%14.4lf %14.4lf %14.4lf\n", x[0], x[1], x[2]);
            logmsg(0x02, "rtk lsq pos:%14.4lf %14.4lf %14.4lf\n", x[0], x[1], x[2]);
            rtk->sol.ns[1] = nsobs + nbase;
            for (j = 0; j < 3; j++) rtk->sol.rr[j] = j < 3 ? x[j] : 0.0;
            for (j = 0; j < 3; j++) rtk->sol.qr[j] = (float)Q[j + j * 3];
            rtk->sol.qr[3] = (float)Q[1];    /* cov xy */
            rtk->sol.qr[4] = (float)Q[5]; /* cov yz */
            rtk->sol.qr[5] = (float)Q[2];    /* cov zx */
            rtk->sol.stat = 1;
            //rtk->sol.age = timediff(obs[0].time, obs[nu].time);
            for (j = 0; j < 3; j++) rtk->sol.rr_lsq[j] = rtk->sol.rr[j];
            rtk->sol.nsLsq = rtk->sol.ns[1];
            free(H); free(v); free(var); free(stdv); free(nvSatMask);
            return 0;
        }
        post++;
    }
    if (i >= 10) {
        trace(0x04, "rtk lsq diverged ns=%d norm dx=%f\n", nv, norm(dx, 3));
    }
    rtk->sol.ns[1] = nsobs + nbase;
    free(H); free(v); free(var); free(stdv); free(nvSatMask);
    return -2;
}



