#include<stdio.h>
#include"rtk.h"
#include<math.h>
#include "multipath.h"

static double GRID_SIZE = 1.0;
hm_grid* HMP = NULL;

#define MP 1
#if MP
extern void set_grid_size(double gridsize) {
    if (gridsize > 0)
        GRID_SIZE = gridsize;
}
extern int init_mp(double  gridsize) {
    int n = (int)(360 / gridsize);
    int m = (int)(90 / gridsize);
    int gridnum = (n + 1) * (m + 1);
    int i, j;

    set_grid_size(gridsize);

    if (!(HMP = (hm_grid*)calloc(gridnum, sizeof(hm_grid)))) {
        return 1;
    }
    //memset(HMP, 0, gridnum*sizeof(hm_grid));
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            HMP[i * m + j].azel[0] = i * gridsize;
            HMP[i * m + j].azel[1] = j * gridsize;
        }
    }
    return 0;
}
extern void free_mp() {
    free(HMP);
}
static int update_hmp(hm_grid* hmp, const double mp) {
    int n = hmp->n;

    hmp->var = 1.0 * n / pow(n + 1, 2) * pow(mp - hmp->mean, 2) + 1.0 * n / (n + 1) * hmp->var;
    hmp->mean = hmp->mean + (mp - hmp->mean) / (n + 1);
    hmp->n++;

    return 0;
}
static int test_update_hmp() {
    double a[10] = { 1,2,3,4,5,6,7,8,9,0 };
    double b[20] = { 10,1,2,3,4,5,6,7,8,9, 10,1,2,3,4,5,6,7,8,9 };
    int i = 0;
    hm_grid hmp;

    memset(&hmp, 0, sizeof(hm_grid));
    for (i = 0; i < 10; i++) {
        update_hmp(&hmp, a[i]);
    }
    if (hmp.n != 10 ||
        fabs(hmp.mean - 4.50000) > 1E-6 ||
        fabs(hmp.var - 8.25000) > 1E-6 ||
        fabs(sqrt(hmp.var) - 2.872281) > 1E-6)
        return 1;

    memset(&hmp, 0, sizeof(hm_grid));
    for (i = 0; i < 20; i++) {
        update_hmp(&hmp, b[i]);
    }
    if (hmp.n != 20 ||
        fabs(hmp.mean - 5.50000) > 1E-6 ||
        fabs(hmp.var - 8.25000) > 1E-6 ||
        fabs(sqrt(hmp.var) - 2.872281) > 1E-6)
        return 1;

    return 0;
}
/* 计算相关系数 */
static double correlationCoefficient(double* X, double* Y, int n)
{

    double sum_X = 0, sum_Y = 0, sum_XY = 0;
    double squareSum_X = 0, squareSum_Y = 0;

    for (int i = 0; i < n; i++)
    {
        // sum of elements of array X.
        sum_X = sum_X + X[i];

        // sum of elements of array Y.
        sum_Y = sum_Y + Y[i];

        // sum of X[i] * Y[i].
        sum_XY = sum_XY + X[i] * Y[i];

        // sum of square of array elements.
        squareSum_X = squareSum_X + X[i] * X[i];
        squareSum_Y = squareSum_Y + Y[i] * Y[i];
    }

    // use formula for calculating correlation coefficient.
    float corr = (float)(n * sum_XY - sum_X * sum_Y)
        / sqrt((n * squareSum_X - sum_X * sum_X)
            * (n * squareSum_Y - sum_Y * sum_Y));

    return corr;
}
/*
* na: length of a
* nb: length of b
* n : moving correlation window length
*/
static int mov_corr(double* a, int na, double* b, int nb, double n, double* maxpr, int* maxi) {
    double pr = 0.0;
    int i = 0;

    if (na < n || nb < n) {
        /* a or b is too short array */
        return 1;
    }

    for (i = 0; i < na - n; i++) {
        pr = correlationCoefficient(a + i, b, n);
        if (fabs(*maxpr) < fabs(pr)) {
            *maxpr = pr;
            *maxi = i;
        }
    }
    return 0;
}
/*
static int get_res_seg(satres_t *satres, satseg_t *satseg, int *index) {
    int i = 0;

    satseg->nepoch = 0;
    satseg->freq = satres->freq;
    satseg->sat = satres->sat;

    if (satres->epochnum < *index) return 1;

    for (i = *index; i < satres->epochnum-1; i++) {
        if (satres->res[i+1].time - satres->res[i].time <= satres->timeintv) {
            satseg->res[satseg->nepoch] = satres->res[i];
            satseg->nepoch++;
        }
        else if (satres->res[i + 1].time - satres->res[i].time <= 2* satres->timeintv) {
            satseg->res[satseg->nepoch].res = (satres->res[i].res+ satres->res[i+1].res)/2;
            satseg->res[satseg->nepoch].time = (satres->res[i].time + satres->res[i + 1].time) / 2;
            satseg->res[satseg->nepoch].azel[0] = (satres->res[i].azel[0] + satres->res[i + 1].azel[0]) / 2;
            satseg->res[satseg->nepoch].azel[1] = (satres->res[i].azel[1] + satres->res[i + 1].azel[1]) / 2;
            satseg->nepoch++;
        }
        else {
            break;
        }
    }
    return 0;
}*/
ress_t RESS;
#if 0
extern int readres(char* filepath) {
    FILE* fp;
    int i = 0, ind = 0;
    int sat = 0, ifreq = 0;
    char satid[4];
    char buff[512];
    char daystr[32], timestr[32];
    double res[6] = { 0.0 };
    char* tok;
    double secs = 0.0;
    double ep[6] = { 0.0 };
    double azel[2], PL[2];

    RESS.res = (res_t*)calloc(sizeof(res_t), 1700000);
    RESS.maxresnum = 1700000;

    if (!(fp = fopen(filepath, "r"))) return 1;

    while (fgets(buff, 512, fp)) {
        if (!strncmp(buff, "$MP", 3)) {
            //outresult,2022 8 17 0 0 15,     15,  1,G03,50.49,33.15,0.3836,0.01,-240
            //outresult,       2022 8 17 0 0 9,9, G03,1,50.52,33.18,0.1571,0.0052,-242
            //outresult,2024/05/28 06:56:00.00, 5, 0, G05, 257.16,  47.12, 0.0070, -0.0049
            //printf("%s\n", buff);
            tok = strtok(buff, ",");
            for (i = 0; i < 9; i++) {
                tok = strtok(NULL, ",");
                switch (i) {
                case 0: {
                    sscanf(tok, "%lf ", &secs);
                    break;
                }
                case 1: {
                    sscanf(tok, "%s", satid);
                    sat = satno(satid);
                    break;
                }
                case 2: sscanf(tok, "%lf", azel); break;
                case 3: {
                    sscanf(tok, "%lf", azel + 1);
                    break;
                }
                case 4: sscanf(tok, "%d", &ifreq); break;
                case 5: sscanf(tok, "%lf", PL); break;
                case 6: sscanf(tok, "%lf", PL + 1); break;
                default:
                    continue;
                }
            }
            //if (res[3] != 0.0) {

            RESS.res[ind].data.sod = secs;// 
            RESS.res[ind].sat = sat;
            RESS.res[ind].data.res = PL[1];
            RESS.res[ind].data.azel[0] = azel[0];
            RESS.res[ind].data.azel[1] = azel[1];
            /*
            printf("%.1f \n", RESS.res[ind].sod);
            if (RESS.res[ind].sod == 1700.00) {
                printf("hello\n");
            } */
            ind++;
            RESS.resnum = ind;

            //}

            if (RESS.resnum == RESS.maxresnum) {
                printf("ERROR: too many data!\n");
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    return 0;
}
#endif
static int hmp2file(hm_grid* hmp) {
    int i, j;
    FILE* fpp;
    int n = (int)(360 / GRID_SIZE);
    int m = (int)(90 / GRID_SIZE);

    fpp = fopen("stat.csv", "w");
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++)
        {
            if ((hmp[i * m + j].n != 0) && (hmp[i * m + j].mean != 0.0)) {
                fprintf(fpp, "%f,%f,%d,%f,%f,\n", hmp[i * m + j].azel[0], hmp[i * m + j].azel[1],
                    hmp[i * m + j].n, hmp[i * m + j].mean, hmp[i * m + j].var
                );
            }
        }
    }
    fclose(fpp);
    return 0;
}
extern int res2hmp(double gridsize) {
    int i = 0, j = 0, k = 0;
    int n = (int)(360 / gridsize);
    int m = (int)(90 / gridsize);
    int azel_id[2] = { 0 };
    hm_grid* hp;

    for (i = 0; i < RESS.resnum; i++) {
        //azel_id[0] = (int)(RESS.res[i].data.azel[0] / gridsize);
        //azel_id[1] = (int)(RESS.res[i].data.azel[1] / gridsize);
        hp = HMP + m * azel_id[0] + azel_id[1];
        update_hmp(hp, RESS.res[i].data.res * 1E3);
    }

    return 0;
}
extern double get_hmp_corr(double* azel, int nmin, double minvar) {
    int n = (int)(360 / GRID_SIZE);
    int m = (int)(90 / GRID_SIZE);
    int azel_id[2] = { 0 };
    hm_grid* hp;

    if (HMP == NULL) return 0.0;

    azel_id[0] = (int)(azel[0] * R2D / GRID_SIZE);
    azel_id[1] = (int)(azel[1] * R2D / GRID_SIZE);
    hp = HMP + m * azel_id[0] + azel_id[1];

    if ((hp->n > nmin) && (hp->var <= minvar)) {
        return hp->mean / 1000.0;
    }
    return 0.0;
}

static int get_default_period(unsigned char satno) {
    // double delays[60] = { 0.0 };
    char id[4];

    satno2id(satno, id);

    if (!strncmp(id, "C01", 3) || !strncmp(id, "C02", 3) ||
        !strncmp(id, "C03", 3) || !strncmp(id, "C04", 3) ||
        !strncmp(id, "C05", 3) || !strncmp(id, "C59", 3) || !strncmp(id, "C60", 3))
        return 86400 - 240.00;
    else if (!strncmp(id, "C06", 3) || !strncmp(id, "C07", 3) || !strncmp(id, "C08", 3) ||
        !strncmp(id, "C09", 3) || !strncmp(id, "C10", 3) || !strncmp(id, "C13", 3) || !strncmp(id, "C16", 3) ||
        !strncmp(id, "C38", 3) || !strncmp(id, "C39", 3) || !strncmp(id, "C40", 3))
        return 86400 - 240.00;

    return 86400 * 7 - 1700.00;
}



extern double get_sid_corr(int satno, double sec) {
    int ind = 0, j = 0;
    double delay = 0.0;

    if (RESS.resnum == 0) return 0.0;
    delay = 0; // = get_delay(satno);
    for (ind = 0; ind < RESS.resnum; ind++) {
        if ((RESS.res[ind].sat == satno) && fabs(RESS.res[ind].data.sod - sec) < 1E-2)
            return RESS.res[ind].data.res;
    }
    return 0.0;
}
#if 0
static void test_readres() {

    char filepath[256] = "D:\\rtktest\\tree0\\result_123456\\outr_bds.csv";


    hm_grid* hmp = NULL;
    double gridsize = 0.5;
    hmp = init_mp(gridsize);

    readres(filepath);
    res2hmp(gridsize);

    int n = (int)(360 / gridsize);
    int m = (int)(90 / gridsize);

    int i, j;
    FILE* fp;

    fp = fopen("stat.csv", "w");
    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++)
        {
            if (hmp[i * m + j].n != 0) {
                fprintf(fp, "%f,%f,%d,%f,%f,\n", hmp[i * m + j].azel[0], hmp[i * m + j].azel[1],
                    hmp[i * m + j].n, hmp[i * m + j].mean, hmp[i * m + j].var
                );
            }
        }
    }
    fclose(fp);
    free(hmp);
}

static int test_mov_corr() {
    double a[10] = { 0,0,0,4,5,6,7,0,0 };
    double b[4] = { 9,8,7,6 };
    double maxpr = 0.0;
    int maxi = -1;
    mov_corr(a, 10, b, 4, 4, &maxpr, &maxi);
    printf("%f, %d\n", maxpr, maxi);
}
static int test_correlation_Pearson() {
    double pr;

    double a[10] = { 1,2,3,4,5,6,7,8,9 };
    double b[10] = { 9,8,7,6,5,4,3,2,1 };

    pr = correlationCoefficient(a, b, 9);

    if (fabs(pr - (-1)) > 1E-6) return 1;

    double X[5] = { 15, 18, 21, 24, 27 };
    double Y[5] = { 25, 25, 27, 31, 32 };


    pr = correlationCoefficient(X, Y, 5);
    if (fabs(pr - 0.95346259) > 1E-6) {
        //printf("hello\n");
        return 1;
    }

    return 0;
}

#endif

extern int satres_init(satres_t* satres, int nmax) {
    satres->n = 0;
    satres->nmax = nmax;

    if (!(satres->data = (resdata_t*)calloc(nmax, sizeof(resdata_t)))) {
        return -1;
    }
    return 0;
}

extern int satres_insert(satres_t* satres, resdata_t* data) {
    int minNum = 0, i = 0, inum = 0; /* */
    int minIndex = 0;
    int sIndex = -1; /* start index */
    resdata_t tmpdata = { 0 };

    minNum = satres->n <= satres->nmax ? satres->n : satres->nmax;
    /* find the insert pos */
    for (i = 0; i <= minNum; i++) {
        minIndex = (satres->n - i - 1 + satres->nmax) % satres->nmax;
        //printf("i=%d, minIndex= %d\n",i, minIndex);
        if (satres->data[minIndex].sod < 1E-5) break;
        if (satres->data[minIndex].sod < data->sod) {
            sIndex = satres->n - i;
            break;
        }
    }

    if (sIndex == -1) return -1;

    for (i = sIndex; i < satres->n + 1; i++) {
        inum = i % satres->nmax;
        tmpdata = satres->data[inum];
        satres->data[inum] = *data;
        *data = tmpdata;
    }
    satres->n++;
    return 0;
}

extern int satres_add(satres_t* satres, resdata_t* data) {
    int index = 0;
    if (fabs(data->res) > 0.05) return 0;
    //printf("satres_add sod=%f, res=%f\n", data->sod, data->res);
    if (satres->n == 0 || data->sod > satres->data[(satres->n - 1) % satres->nmax].sod) {
        index = satres->n % satres->nmax;
        satres->data[index] = *data;
        satres->n++;
    }
    else if (data->sod < satres->data[(satres->n - 1) % satres->nmax].sod) {
        satres_insert(satres, data);
    }
    return 0;
}

#if 0
/* search satres from oldest record */
extern double satres_search(satres_t* satres, double isod) {
    int i = 0;
    int minIndex = 0;

    for (i = 0; i <= satres->nmax; i++) {
        minIndex = (satres->n + i + satres->nmax) % satres->nmax;
        if (satres->data[minIndex].sod < 1E-5) break;
        //printf("minIndex=%d\n",minIndex);
        if (satres->data[minIndex].sod == isod) {
            //if(satres->data[minIndex].res!=0.0)
            //    printf("i=%d\n", i);
            return satres->data[minIndex].res;
        }
    }

    return 0.0;
}
#else
/* search satres from oldest record */
extern double satres_search(satres_t* satres, double isod) {
    int i = 0;
    int minIndex = 0;
    double sum = 0.0;
    int nsum = 0;

    for (i = 0; i <= satres->nmax; i++) {
        minIndex = i;
        if (fabs(satres->data[minIndex].sod - isod)<=1.0) {
            //if (satres->data[minIndex].res != 0.0)
            //    printf("i=%d\n", i);
            sum += satres->data[minIndex].res;
            nsum++;
        }
    }
    return nsum==0?0.0:sum/nsum;
}
#endif

static int print_satres(satres_t* satres) {
    int i = 0;
    int minIndex = 0;

    //for (i = 0; i < satres->nmax; i++) {
    //    minIndex = (satres->n - i + satres->nmax) % satres->nmax;
    //    if (satres->data[minIndex].sod < 1E-5) continue;
    //    printf("index = %02d, sod = %03.2f, res = %5.4f\n", minIndex, satres->data[minIndex].sod, satres->data[minIndex].res);
    //}

    for (i = 0; i < satres->nmax; i++) {
        //minIndex = (satres->n - i + satres->nmax) % satres->nmax;
        if (satres->data[i].sod < 1E-5) continue;
        printf("index = %02d, sod = %03.2f, res = %5.4f\n", i, satres->data[i].sod, satres->data[i].res);
    }

    return 0;
}


static int test_satres_add() {
    satres_t test_res;
    int nmax = 20;
    int i = 0;
    resdata_t data;

    /* test 00 */
    satres_init(&test_res, nmax);
    if ((test_res.nmax == nmax) && (test_res.data[nmax].sod < 1E-5)) {
        printf("satres_init(): OK\n");
    }
    else {
        printf("satres_init(): NO\n");
    }

    /* test unfull list */
    for (i = 0; i < 25; i++) {
        if (i == 18) continue;
        if (i == 22) continue;
        data.sod = i + 1;
        data.res = 0.001 * data.sod;
        satres_add(&test_res, &data);
    }

    printf("satres n = %d, nmax = %d\n", test_res.n, test_res.nmax);
    print_satres(&test_res);

    i = 22;
    data.sod = i + 1;
    data.res = 0.001 * data.sod;
    satres_add(&test_res, &data);

    printf("satres n = %d, nmax = %d\n", test_res.n, test_res.nmax);
    print_satres(&test_res);

    /* test satres_search */
    double res = 0.0;
    res = satres_search(&test_res, 8.0);

    printf("res= %f\n", res);

    return 0;
}


extern int test_mp_main() {
    // get_delay(41);
    //test_update_hmp();
    //test_correlation_Pearson();
 //   test_mov_corr();
 //   test_readres();
    test_satres_add();
}

extern int initDat(FILE* fp, int npoint) {
    double* resdat = NULL;
    int i = 0;
    if (npoint <= 0) return 1;
    if (!(resdat = (double*)calloc(npoint, sizeof(double)))) {
        return 1;
    }
    else {
        fwrite(resdat, sizeof(double), npoint, fp);
        free(resdat);
        resdat = NULL;
    }
    return 0;
}
extern double getDat(FILE* fp, int offset) {
    double value = 0.0;
    fseek(fp, sizeof(double) * offset, SEEK_SET);
    fread(&value, sizeof(double), 1, fp);
    //fseek(fp , 0L, SEEK_SET);
    return value;
}
extern int writeDat(FILE* fp, int offset, double value) {
    double tmp = value;
    if (fp == NULL) {
        return 1;
    }
    fseek(fp, sizeof(double) * offset, SEEK_SET);
    fwrite(&tmp, sizeof(double), 1, fp);
    //fseek(fp, 0L, SEEK_SET);
    return 0;
}

extern int calOffset(unsigned char sat, gtime_t ctime, int intv) {
    int pday = 0;

    pday = periodDay(sat);
    int offset = (int)((ctime.time%(86400*pday))/intv);
    //printf("pday=%02d, offset=%d\n", pday, offset);

    char id[4];
    satno2id(sat,id);
    trace(2, "$SAT, %s, %ld, %d\n", id, ctime.time, offset);
    return offset;
}

extern int mkfpssat(rtk_t *rtk) {
    int i = 0,j=0;
    int nmax = 0.0;

    if(rtk->mpflag==0) return 0;
    for (i = 0; i < MAXSAT; i++) {
        nmax = (int)periodDay(i) * 86400 / rtk->opt.timeInterval;
#if 0
        for (j = 0; j < NFREQ; j++) {
            rtk->ssat[i].satres[j].nmax = nmax;
            rtk->ssat[i].satres[j].data = (resdata_t*)calloc(sizeof(resdata_t), nmax);
        }
#endif
        if (invalidBDS(i) == 1) {
            rtk->ssat[i].fp_ssat = NULL;
            continue;
        }

        char satdatfile[1024];
        char id[4];
        satno2id(i, id);

        // sprintf(satdatfile, "%s/sat%03d.dat\0", rtk->path,i);
        sprintf(satdatfile, "%s/sat%s.dat", rtk->path, id);

        if (rtk->mpflag == 1) { //create new MP cache data
            if ((rtk->ssat[i].fp_ssat = fopen(satdatfile, "wb+")) == NULL)
            {
                printf("Fail to open file: %s\n",satdatfile);
                return 1;
            }
            else {
                initDat(rtk->ssat[i].fp_ssat, nmax);
            }
        }
        else if (rtk->mpflag == 2) { // load old MP cache data
            if ((rtk->ssat[i].fp_ssat = fopen(satdatfile, "rb+")) == NULL)
            {
                printf("Fail to load file: %s\n",satdatfile);
                return 1;
            }
        }
    }
    return 0;
}

#else
extern int readres(char* filepath) { return 0; }
extern int init_mp(double  gridsize) { return 0; }
extern int res2hmp(double gridsize) { return 0; }
extern int satres_add(satres_t* satres, resdata_t* data) { return 0; }
extern double satres_search(satres_t* satres, double isod) { return 0.0; }
extern void free_mp() {}
extern int initDat(FILE* fp, int npoint) {}
extern double getDat(FILE* fp, int offset) {}
extern int writeDat(FILE* fp, int offset, double value) {}
extern int mkfpssat(rtk_t* rtk) {}
#endif 
