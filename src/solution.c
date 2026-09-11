#include "BandPassFilter.h"
#include "fft.h"
#include "rtk.h"
#include "types.h"

#define SQRT(x) ((x) < 0.0 ? 0.0 : sqrt(x))
static FILE*        fp_geoid = NULL; /* geoid file pointer */
static const double range[4];        /* embedded geoid area range {W,E,S,N} (deg) */
static const float  geoid[361][181]; /* embedded geoid heights (m) (lon x lat) */
#define GEOID_EMBEDDED 0             /* geoid model: embedded geoid */
#define GEOID_EGM96_M150 1           /* geoid model: EGM96 15x15" */
#define GEOID_EGM2008_M25 2          /* geoid model: EGM2008 2.5x2.5" */
#define GEOID_EGM2008_M10 3          /* geoid model: EGM2008 1.0x1.0" */
#define GEOID_GSI2000_M15 4          /* geoid model: GSI geoid 2000 1.0x1.5" */
#define GEOID_RAF09 5                /* geoid model: IGN RAF09 for France 1.5"x2" */
#define TIMES_UTC 1                  /* time system: utc */
#define TIMES_JST 2                  /* time system: jst */
#define SLIP_WINDOW 1
static int          model_geoid   = GEOID_EMBEDDED; /* geoid model */
static unsigned int bslErrorCount = 0;
static double       ori_pre[3];
static double       enuInit[3]    = {0.0};
static U8           unusualEnuCnt = 0;
static S32          count         = 0, idx;
static S32          data_in_I = 0, data_in_Q = 0;
static FLOAT        data;

static U8    FFTDectTh = 3;
static U32   FFTMag, FFTMagMean, FFTMagMax = 0, FFTMagMaxIdx = 0;
static FLOAT Fs = 1.0, FFTDectFreq = 0;

static compx_12bit FFTInBuf[3][FFT_LEN]      = {0};
static compx_12bit FFTOutBufTemp[3][FFT_LEN] = {0};
static compx_12bit FFTOutBuf[3][FFT_LEN]     = {0};

static BPF       BandPassFilter;
static FLOAT     data_out_I = 0, data_out_Q = 0;
extern double    writeConfigTime;
static int       fixEnuErrorCheck = 0;
long int         cntdrift         = 0;
static const int solq_nmea[] =
    {/* nmea quality flags to rtklib sol quality */
     /* nmea 0183 v.2.3 quality flags: */
     /*  0=invalid, 1=gps fix (sps), 2=dgps fix, 3=pps fix, 4=rtk, 5=float rtk
      */
     /*  6=estimated (dead reckoning), 7=manual input, 8=simulation */

     SOLQ_NONE,  SOLQ_SINGLE, SOLQ_DGPS, SOLQ_PPP,  SOLQ_FIX,
     SOLQ_FLOAT, SOLQ_DR,     SOLQ_NONE, SOLQ_NONE, SOLQ_NONE
};
extern char configFileFath[MAXSTRPATH];
#if 1
void quick3WaySort(double* a, int* index, int left, int right)
{
    if (left > right)
    {
        return;
    }
    int    lt  = left;
    int    i   = left + 1;
    int    gt  = right;
    double tem = a[left], tmp;
    int    tmpIndex;
    while (i <= gt)
    {
        if (a[i] < tem)
        {
            tmp   = a[i];
            a[i]  = a[lt];
            a[lt] = tmp;

            tmpIndex  = index[i];
            index[i]  = index[lt];
            index[lt] = tmpIndex;

            lt++;
            i++;
        }
        else if (a[i] > tem)
        {
            tmp   = a[i];
            a[i]  = a[gt];
            a[gt] = tmp;

            tmpIndex  = index[i];
            index[i]  = index[gt];
            index[gt] = tmpIndex;

            gt--;
        }
        else
        {
            i++;
        }
    }
    quick3WaySort(a, index, left, lt - 1);
    quick3WaySort(a, index, gt + 1, right);
}
static void medianFilter(double* enu, rtk_t* rtk)
{
    int i = 0, j = 0, k = 0, index, mid = 0, segment, symboldenu[3], leveldenu[3], pickPoint = 3;
    int maxpoint = rtk->maxMedianFilterPoint;
    double *enuwindow[3], tmp, denu[3], sumENU[3], sumSQENU[3], stdENU[3], aveENU[3];
    int*    sortIndex = NULL;
    index             = rtk->cntEnuWind;
    // if(rtk->opt.timeInterval!=1){

    //}else if(rtk->opt.timeInterval==1){
    //    if(rtk->sol.time.time %15 ==0){
    //        rtk->enuWindowMedian[0][index % maxpoint] = enu[0];
    //        rtk->enuWindowMedian[1][index % maxpoint] = enu[1];
    //        rtk->enuWindowMedian[2][index++ % maxpoint] = enu[2];
    //    }
    //}
    rtk->enuWindowMedian[0][index % maxpoint]   = enu[0];
    rtk->enuWindowMedian[1][index % maxpoint]   = enu[1];
    rtk->enuWindowMedian[2][index++ % maxpoint] = enu[2];
    rtk->cntEnuWind                             = index;
    if (cntdrift > 0)
    {
        cntdrift--;
        return;
    }
    for (i = 0; i < 3; i++)
    {
        if (!(enuwindow[i] = (double*)calloc(maxpoint, sizeof(double))))
        {
            return;
        }
    }
    sortIndex = (int*)calloc(maxpoint, sizeof(int));
    if (!sortIndex)
    {
        for (i = 0; i < 3; i++)
        {
            free(enuwindow[i]);
        }
        return;
    }
    if (index > (maxpoint - 1))
    {
        sumENU[0] = sumENU[1] = sumENU[2] = 0.0;
        sumSQENU[0] = sumSQENU[1] = sumSQENU[2] = 0.0;
        for (i = 0; i < maxpoint; i++)
        {
            enuwindow[0][i] = rtk->enuWindowMedian[0][i];
            enuwindow[1][i] = rtk->enuWindowMedian[1][i];
            enuwindow[2][i] = rtk->enuWindowMedian[2][i];
            sumENU[0]       = sumENU[0] + enuwindow[0][i];
            sumENU[1]       = sumENU[1] + enuwindow[1][i];
            sumENU[2]       = sumENU[2] + enuwindow[2][i];
            sumSQENU[0]     = sumSQENU[0] + enuwindow[0][i] * enuwindow[0][i];
            sumSQENU[1]     = sumSQENU[1] + enuwindow[1][i] * enuwindow[1][i];
            sumSQENU[2]     = sumSQENU[2] + enuwindow[2][i] * enuwindow[2][i];
        }
        // rtk->stdEnu[k] = sqrt((rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) / rtk->maxSmoothPoint) /
        // (rtk->maxSmoothPoint - 1));
        stdENU[0] = sqrt((sumSQENU[0] - sumENU[0] * sumENU[0] / maxpoint) / (maxpoint - 1));
        stdENU[1] = sqrt((sumSQENU[1] - sumENU[1] * sumENU[1] / maxpoint) / (maxpoint - 1));
        stdENU[2] = sqrt((sumSQENU[2] - sumENU[2] * sumENU[2] / maxpoint) / (maxpoint - 1));
        // trace(4, "$$medianFilter stdenu: %.4f; %.4f; %.4f;\n", stdENU[0], stdENU[1], stdENU[2]);
        // trace(4, "$$medianFilter aveenu: %.4f; %.4f; %.4f;\n", sumENU[0] / maxpoint, sumENU[1] /
        // maxpoint, sumENU[2] / maxpoint);

        // trace(4, "$$big window stdenu: %.4f; %.4f; %.4f;\n", rtk->stdEnu[0], rtk->stdEnu[1],
        // rtk->stdEnu[2]); trace(4, "$$big window aveenu: %.4f; %.4f; %.4f;\n", rtk->aveEnu[0],
        // rtk->aveEnu[1], rtk->aveEnu[2]);
        //  用冒泡法对数组进行排序
        // #if 0
        //         for (i = 0; i < maxpoint - 1; i++)
        //         {
        //             for (j = 0; j < maxpoint - 1 - i; j++)
        //             {
        //                 for (k = 0; k < 3; k++)
        //                 {
        //
        //                     if (enuwindow[k][j] > enuwindow[k][j + 1])
        //                     {
        //                         // 互换
        //                         tmp = enuwindow[k][j];
        //                         enuwindow[k][j] = enuwindow[k][j + 1];
        //                         enuwindow[k][j + 1] = tmp;
        //                     }
        //                 }
        //             }
        //         }
        // #endif
        for (k = 0; k < 3; k++)
        {
            // 复制数据到enuwindow
            for (i = 0; i < maxpoint; i++)
            {
                enuwindow[k][i] = rtk->enuWindowMedian[k][i];
                sortIndex[i]    = i;
            }
            quick3WaySort(enuwindow[k], sortIndex, 0, maxpoint - 1);
        }

        mid     = (maxpoint - 1) / 2;
        segment = (maxpoint - 1) / 10;
        for (i = 0; i < 3; i++)
        {
            denu[i]       = enu[i] - rtk->aveEnu[i];
            leveldenu[i]  = (int)(fabs(denu[i]) / rtk->stdEnu[i]);
            leveldenu[i]  = leveldenu[i] > 3 ? 3 : leveldenu[i];
            symboldenu[i] = denu[i] > 0 ? -1 : 1;
            // 可能位移
            if (rtk->enuWindowMedianShiftNum[i] == 0 &&
                (enuwindow[i][pickPoint] > rtk->aveEnu[i] ||
                 enuwindow[i][maxpoint - pickPoint] < rtk->aveEnu[i]))
            {
                rtk->enuWindowMedianShiftNum[i] = rtk->maxSmoothPoint;
            }
            else
            {
                if ((enuwindow[i][mid - segment] <= rtk->aveEnu[i] &&
                     enuwindow[i][mid + segment] >= rtk->aveEnu[i]))
                {
                    rtk->enuWindowMedianShiftNum[i] = 0;
                }
                else
                {
                    rtk->enuWindowMedianShiftNum[i]--;
                }
            }
            if (rtk->enuWindowMedianShiftNum[i] < 0)
            {
                rtk->enuWindowMedianShiftNum[i] = 0;
            }
            // trace(4, "%s medianFilter-%d: cnt= %5d;\n", rtk->s, i,
            // rtk->enuWindowMedianShiftNum[i]);
            /*if (leveldenu[i] > 1 &&
                (enuwindow[i][3]< rtk->aveEnu[0] && enuwindow[i][maxpoint - 3] > rtk->aveEnu[0])) {
                trace(4, "medianFilter-E:%.3f -> ", enu[0]);
                enu[0] = enuwindow[i][mid + symboldenu[0] * leveldenu[0] * segment];
                trace(4, "%.3f \n", enu[0]);
            }*/
            if (fabs(denu[i]) > rtk->stdEnu[i] && (rtk->enuWindwoIndex[i] < rtk->maxSmoothPoint ||
                                                   rtk->enuWindowMedianShiftNum[i] == 0))
            {
                enu[i] = enuwindow[i][mid + symboldenu[i] * leveldenu[i] * segment];
            }
        }
    }
    for (i = 0; i < 3; i++)
    {
        free(enuwindow[i]);
    }
    free(sortIndex);
}
#else
static void medianFilter(double* enu, rtk_t* rtk)
{
    int i = 0, j = 0, k = 0, index, mid = 0, segment, symboldenu[3], leveldenu[3], pickPoint = 3;
    int maxpoint = rtk->maxMedianFilterPoint;
    double *enuwindow[3], tmp, denu[3], sumENU[3], sumSQENU[3], stdENU[3], aveENU[3];
    index                                       = rtk->cntEnuWind;
    rtk->enuWindowMedian[0][index % maxpoint]   = enu[0];
    rtk->enuWindowMedian[1][index % maxpoint]   = enu[1];
    rtk->enuWindowMedian[2][index++ % maxpoint] = enu[2];
    rtk->cntEnuWind                             = index;
    if (cntdrift > 0)
    {
        cntdrift--;
        return;
    }
    for (i = 0; i < 3; i++)
    {
        if (!(enuwindow[i] = (double*)calloc(maxpoint, sizeof(double))))
        {
            return;
        }
    }
    if (index > (maxpoint - 1))
    {
        sumENU[0] = sumENU[1] = sumENU[2] = 0.0;
        sumSQENU[0] = sumSQENU[1] = sumSQENU[2] = 0.0;
        for (i = 0; i < maxpoint; i++)
        {
            enuwindow[0][i] = rtk->enuWindowMedian[0][i];
            enuwindow[1][i] = rtk->enuWindowMedian[1][i];
            enuwindow[2][i] = rtk->enuWindowMedian[2][i];
            sumENU[0]       = sumENU[0] + enuwindow[0][i];
            sumENU[1]       = sumENU[1] + enuwindow[1][i];
            sumENU[2]       = sumENU[2] + enuwindow[2][i];
            sumSQENU[0]     = sumSQENU[0] + enuwindow[0][i] * enuwindow[0][i];
            sumSQENU[1]     = sumSQENU[1] + enuwindow[1][i] * enuwindow[1][i];
            sumSQENU[2]     = sumSQENU[2] + enuwindow[2][i] * enuwindow[2][i];
        }
        // rtk->stdEnu[k] = sqrt((rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) /
        // rtk->maxSmoothPoint) / (rtk->maxSmoothPoint - 1));
        stdENU[0] = sqrt((sumSQENU[0] - sumENU[0] * sumENU[0] / maxpoint) / (maxpoint - 1));
        stdENU[1] = sqrt((sumSQENU[1] - sumENU[1] * sumENU[1] / maxpoint) / (maxpoint - 1));
        stdENU[2] = sqrt((sumSQENU[2] - sumENU[2] * sumENU[2] / maxpoint) / (maxpoint - 1));
        // trace(4, "$$medianFilter stdenu: %.4f; %.4f; %.4f;\n", stdENU[0],
        // stdENU[1], stdENU[2]); trace(4, "$$medianFilter aveenu: %.4f; %.4f;
        // %.4f;\n", sumENU[0] / maxpoint, sumENU[1] / maxpoint, sumENU[2] /
        // maxpoint);

        // trace(4, "$$big window stdenu: %.4f; %.4f; %.4f;\n", rtk->stdEnu[0],
        // rtk->stdEnu[1], rtk->stdEnu[2]); trace(4, "$$big window aveenu: %.4f;
        // %.4f; %.4f;\n", rtk->aveEnu[0], rtk->aveEnu[1], rtk->aveEnu[2]);
        //  用冒泡法对数组进行排序
        for (i = 0; i < maxpoint - 1; i++)
        {
            for (j = 0; j < maxpoint - 1 - i; j++)
            {
                for (k = 0; k < 3; k++)
                {
                    if (enuwindow[k][j] > enuwindow[k][j + 1])
                    {
                        // 互换
                        tmp                 = enuwindow[k][j];
                        enuwindow[k][j]     = enuwindow[k][j + 1];
                        enuwindow[k][j + 1] = tmp;
                    }
                }
            }
        }
        mid     = (maxpoint - 1) / 2;
        segment = (maxpoint - 1) / 10;
        for (i = 0; i < 3; i++)
        {
            denu[i]       = enu[i] - rtk->aveEnu[i];
            leveldenu[i]  = (int)(fabs(denu[i]) / rtk->stdEnu[i]);
            leveldenu[i]  = leveldenu[i] > 3 ? 3 : leveldenu[i];
            symboldenu[i] = denu[i] > 0 ? -1 : 1;
            // 可能位移
            if (rtk->enuWindowMedianShiftNum[i] == 0 &&
                (enuwindow[i][pickPoint] > rtk->aveEnu[i] ||
                 enuwindow[i][maxpoint - pickPoint] < rtk->aveEnu[i]))
            {
                rtk->enuWindowMedianShiftNum[i] = rtk->maxSmoothPoint;
            }
            else
            {
                if ((enuwindow[i][mid - segment] <= rtk->aveEnu[i] &&
                     enuwindow[i][mid + segment] >= rtk->aveEnu[i]))
                {
                    rtk->enuWindowMedianShiftNum[i] = 0;
                }
                else
                {
                    rtk->enuWindowMedianShiftNum[i]--;
                }
            }
            if (rtk->enuWindowMedianShiftNum[i] < 0)
            {
                rtk->enuWindowMedianShiftNum[i] = 0;
            }
            if (fabs(denu[i]) > rtk->stdEnu[i] && (rtk->enuWindwoIndex[i] < rtk->maxSmoothPoint ||
                                                   rtk->enuWindowMedianShiftNum[i] == 0))
            {
                enu[i] = enuwindow[i][mid + symboldenu[i] * leveldenu[i] * segment];
            }
        }
    }
    for (i = 0; i < 3; i++)
    {
        free(enuwindow[i]);
    }
}
#endif
/* bilinear interpolation ----------------------------------------------------*/
static double interpb(const double* y, double a, double b)
{
    return y[0] * (1.0 - a) * (1.0 - b) + y[1] * a * (1.0 - b) + y[2] * (1.0 - a) * b +
           y[3] * a * b;
}
/* embedded geoid model ------------------------------------------------------*/
static double geoidh_emb(const double* pos)
{
    const double dlon = 1.0, dlat = 1.0;
    double       a, b, y[4];
    int          i1, i2, j1, j2;

    if (pos[1] < range[0] || range[1] < pos[1] || pos[0] < range[2] || range[3] < pos[0])
    {
        ////trace(2, "out of geoid model range: lat=%.3f lon=%.3f\n", pos[0],
        /// pos[1]);
        return 0.0;
    }
    a  = (pos[1] - range[0]) / dlon;
    b  = (pos[0] - range[2]) / dlat;
    i1 = (int)a;
    a -= i1;
    i2 = i1 < 360 ? i1 + 1 : i1;
    j1 = (int)b;
    b -= j1;
    j2   = j1 < 180 ? j1 + 1 : j1;
    y[0] = geoid[i1][j1];
    y[1] = geoid[i2][j1];
    y[2] = geoid[i1][j2];
    y[3] = geoid[i2][j2];
    return interpb(y, a, b);
}
/* get 2 byte signed integer from file ---------------------------------------*/
static short fget2b(FILE* fp, long off)
{
    unsigned char v[2];
    if (fseek(fp, off, SEEK_SET) == EOF || fread(v, 2, 1, fp) < 1)
    {
        ////trace(2, "geoid data file range error: off=%ld\n", off);
    }
    return ((short)v[0] << 8) + v[1]; /* big-endian */
}
/* egm96 15x15" model --------------------------------------------------------*/
static double geoidh_egm96(const double* pos)
{
    const double lon0 = 0.0, lat0 = 90.0, dlon = 15.0 / 60.0, dlat = -15.0 / 60.0;
    const int    nlon = 1440, nlat = 721;
    double       a, b, y[4];
    long         i1, i2, j1, j2;

    if (!fp_geoid)
    {
        return 0.0;
    }

    a  = (pos[1] - lon0) / dlon;
    b  = (pos[0] - lat0) / dlat;
    i1 = (long)a;
    a -= i1;
    i2 = i1 < nlon - 1 ? i1 + 1 : 0;
    j1 = (long)b;
    b -= j1;
    j2   = j1 < nlat - 1 ? j1 + 1 : j1;
    y[0] = fget2b(fp_geoid, 2L * (i1 + j1 * nlon)) * 0.01;
    y[1] = fget2b(fp_geoid, 2L * (i2 + j1 * nlon)) * 0.01;
    y[2] = fget2b(fp_geoid, 2L * (i1 + j2 * nlon)) * 0.01;
    y[3] = fget2b(fp_geoid, 2L * (i2 + j2 * nlon)) * 0.01;
    return interpb(y, a, b);
}

/* get 4byte float from file -------------------------------------------------*/
static float fget4f(FILE* fp, long off)
{
    float v = 0.0;
    if (fseek(fp, off, SEEK_SET) == EOF || fread(&v, 4, 1, fp) < 1)
    {
        ////trace(2, "geoid data file range error: off=%ld\n", off);
    }
    return v; /* small-endian */
}

static double geoidh_egm08(const double* pos, int model)
{
    const double lon0 = 0.0, lat0 = 90.0;
    double       dlon, dlat;
    double       a, b, y[4];
    long         i1, i2, j1, j2;
    int          nlon, nlat;

    if (!fp_geoid)
    {
        return 0.0;
    }

    if (model == GEOID_EGM2008_M25)
    { /* 2.5 x 2.5" grid */
        dlon = 2.5 / 60.0;
        dlat = -2.5 / 60.0;
        nlon = 8640;
        nlat = 4321;
    }
    else
    { /* 1 x 1" grid */
        dlon = 1.0 / 60.0;
        dlat = -1.0 / 60.0;
        nlon = 21600;
        nlat = 10801;
    }
    a  = (pos[1] - lon0) / dlon;
    b  = (pos[0] - lat0) / dlat;
    i1 = (long)a;
    a -= i1;
    i2 = i1 < nlon - 1 ? i1 + 1 : 0;
    j1 = (long)b;
    b -= j1;
    j2 = j1 < nlat - 1 ? j1 + 1 : j1;

    /* notes: 4byte-zeros are inserted at first and last field of a record */
    /*        for current geid data files */
    /* http://earth-info.nga.mil/GandG/wgs84/gravitymod/egm2008/egm08_wgs84.html
     */
    /* (1) Und_min1x1_egm2008_isw=82_WGS84_TideFree_SE.gz */
    /* (2) Und_min2.5x2.5_egm2008_isw=82_WGS84_TideFree_SE.gz */
#if 0
    /* not zero-inserted */
    y[0] = fget4f(fp_geoid, 4L * (i1 + j1 * (nlon)));
    y[1] = fget4f(fp_geoid, 4L * (i2 + j1 * (nlon)));
    y[2] = fget4f(fp_geoid, 4L * (i1 + j2 * (nlon)));
    y[3] = fget4f(fp_geoid, 4L * (i2 + j2 * (nlon)));
#else
    /* zero-inserted version (2009/12/10) */
    y[0] = fget4f(fp_geoid, 4L * (i1 + j1 * (nlon + 2) + 1));
    y[1] = fget4f(fp_geoid, 4L * (i2 + j1 * (nlon + 2) + 1));
    y[2] = fget4f(fp_geoid, 4L * (i1 + j2 * (nlon + 2) + 1));
    y[3] = fget4f(fp_geoid, 4L * (i2 + j2 * (nlon + 2) + 1));
#endif
    return interpb(y, a, b);
}

/* get gsi geoid data --------------------------------------------------------*/
static double fgetgsi(FILE* fp, int nlon, int nlat, int i, int j)
{
    const int nf = 28, wf = 9, nl = nf * wf + 2, nr = (nlon - 1) / nf + 1;
    double    v;
    long      off      = nl + j * nr * nl + i / nf * nl + i % nf * wf;
    char      buff[16] = "";

    if (fseek(fp, off, SEEK_SET) == EOF || fread(buff, wf, 1, fp) < 1)
    {
        ////trace(2, "out of range for gsi geoid: i=%d j=%d\n", i, j);
        return 0.0;
    }
    if (sscanf(buff, "%lf", &v) < 1)
    {
        ////trace(2, "gsi geoid data format error: i=%d j=%d buff=%s\n", i, j,
        /// buff);
        return 0.0;
    }
    return v;
}

/* gsi geoid 2000 1.0x1.5" model ---------------------------------------------*/
static double geoidh_gsi(const double* pos)
{
    const double lon0 = 120.0, lon1 = 150.0, lat0 = 20.0, lat1 = 50.0;
    const double dlon = 1.5 / 60.0, dlat = 1.0 / 60.0;
    const int    nlon = 1201, nlat = 1801;
    double       a, b, y[4];
    int          i1, i2, j1, j2;

    if (!fp_geoid || pos[1] < lon0 || lon1 < pos[1] || pos[0] < lat0 || lat1 < pos[0])
    {
        ////trace(2, "out of range for gsi geoid: lat=%.3f lon=%.3f\n", pos[0],
        /// pos[1]);
        return 0.0;
    }
    a  = (pos[1] - lon0) / dlon;
    b  = (pos[0] - lat0) / dlat;
    i1 = (int)a;
    a -= i1;
    i2 = i1 < nlon - 1 ? i1 + 1 : i1;
    j1 = (int)b;
    b -= j1;
    j2   = j1 < nlat - 1 ? j1 + 1 : j1;
    y[0] = fgetgsi(fp_geoid, nlon, nlat, i1, j1);
    y[1] = fgetgsi(fp_geoid, nlon, nlat, i2, j1);
    y[2] = fgetgsi(fp_geoid, nlon, nlat, i1, j2);
    y[3] = fgetgsi(fp_geoid, nlon, nlat, i2, j2);
    if (y[0] == 999.0 || y[1] == 999.0 || y[2] == 999.0 || y[3] == 999.0)
    {
        ////trace(2, "geoidh_gsi: data outage (lat=%.3f lon=%.3f)\n", pos[0],
        /// pos[1]);
        return 0.0;
    }
    return interpb(y, a, b);
}

/* std-dev of soltuion -------------------------------------------------------*/
static double sol_std(const sol_t* sol)
{
    /* approximate as max std-dev of 3-axis std-devs */
    if (sol->qr[0] > sol->qr[1] && sol->qr[0] > sol->qr[2])
    {
        return SQRT(sol->qr[0]);
    }
    if (sol->qr[1] > sol->qr[2])
    {
        return SQRT(sol->qr[1]);
    }
    return SQRT(sol->qr[2]);
}
static const char* opt2sep(const solopt_t* opt)
{
    return ",";
    if (!*opt->sep)
    {
        return ",";
    }
    else if (!strcmp(opt->sep, "\\t"))
    {
        return "\t";
    }
    return opt->sep;
}

static double geoidh(const double* pos)
{
    double posd[2], h;

    posd[1] = pos[1] * R2D;
    posd[0] = pos[0] * R2D;
    if (posd[1] < 0.0)
    {
        posd[1] += 360.0;
    }

    if (posd[1] < 0.0 || 360.0 - 1E-12 < posd[1] || posd[0] < -90.0 || 90.0 < posd[0])
    {
        ////trace(2, "out of range for geoid model: lat=%.3f lon=%.3f\n", posd[0],
        /// posd[1]);
        return 0.0;
    }
    switch (model_geoid)
    {
        case GEOID_EMBEDDED:
            h = geoidh_emb(posd);
            break;
        case GEOID_EGM96_M150:
            h = geoidh_egm96(posd);
            break;
        case GEOID_EGM2008_M25:
            h = geoidh_egm08(posd, model_geoid);
            break;
        case GEOID_EGM2008_M10:
            h = geoidh_egm08(posd, model_geoid);
            break;
        case GEOID_GSI2000_M15:
            h = geoidh_gsi(posd);
            break;
        default:
            return 0.0;
    }
    if (fabs(h) > 200.0)
    {
        ////trace(2, "invalid geoid model: lat=%.3f lon=%.3f h=%.3f\n", posd[0],
        /// posd[1], h);
        return 0.0;
    }
    return h;
}

/* solution to covariance ----------------------------------------------------*/
static void soltocov(const sol_t* sol, double* P)
{
    P[0] = sol->qr[0];        /* xx or ee */
    P[4] = sol->qr[1];        /* yy or nn */
    P[8] = sol->qr[2];        /* zz or uu */
    P[1] = P[3] = sol->qr[3]; /* xy or en */
    P[5] = P[7] = sol->qr[4]; /* yz or nu */
    P[2] = P[6] = sol->qr[5]; /* zx or ue */
}

static int screent(gtime_t time, gtime_t ts, gtime_t te, double tint)
{
    return (tint <= 0.0 || fmod(time2gpst(time, NULL) + DTTOL, tint) <= DTTOL * 2.0) &&
           (ts.time == 0 || timediff(time, ts) >= -DTTOL) &&
           (te.time == 0 || timediff(time, te) < DTTOL);
}
/* sqrt of covariance --------------------------------------------------------*/
static double sqvar(double covar) { return covar < 0.0 ? -sqrt(-covar) : sqrt(covar); }

static int outenu_dynamic(
    unsigned char* buff, const char* s, rtk_t* rtk, sol_t* sol, const double* rb,
    const solopt_t* opt
)
{
    double        pos[3], rr[3], rr_kalman[3], enu[3], enu2[3], P[9], Q[9];
    int           i, j, k, n, cnt, ns, flag[3] = {0}, shiftCnt = 60, dynWinCnt = 0;
    const char*   sep = opt2sep(opt);
    char*         p   = (char*)buff;
    double        dr[3], r[3];
    unsigned char detectSensitivity = rtk->opt.detectSensitivity + 2;
    double        precent, thres;
    double        var;

    if (rtk->opt.timeInterval < 5)
    {
        shiftCnt  = 60;
        precent   = 0.95;
        dynWinCnt = 60;
    }
    else if (rtk->opt.timeInterval < 15)
    {
        shiftCnt  = 60;
        precent   = 0.95;
        dynWinCnt = 60;
    }
    else
    {
        shiftCnt  = 60;
        precent   = 0.80;
        dynWinCnt = 30;
    }

    trace(2, "detectSensitivity=%d param=%d\n", detectSensitivity, rtk->opt.param);
    for (i = 0; i < 3; i++)
    {
        rr[i] = sol->rr[i] - rb[i];
    }

    ns = sol->stat == PMODE_SINGLE ? sol->ns[0] : sol->ns[1];
    ecef2pos(sol->rr, pos);   // 大地坐标转站心坐标
    soltocov(sol, P);         // 得到XYZ方向状态协方差
    covenu(pos, P, Q);        // 将XYZ方向协方差转到ENU方向方差
    ecef2enu(pos, rr, enu2);  // 大地坐标转站心坐标
    ecef2enu(pos, rr, enu);   // 大地坐标转站心坐标

    trace(
        2, "ENU2 ,%s,%14.4lf, %14.4lf,%14.4lf,%d,%14.4lf,%14.4lf,%14.4lf\n", s, enu2[0], enu2[1],
        enu2[2], sol->stat, rtk->sol.fixxyz[0], rtk->sol.fixxyz[1], rtk->sol.fixxyz[2]

    );

    trace(
        4, "rr:%14.4lf %14.4lf %14.4lf %14.4lf %14.4lf %14.4lf\n", sol->rr[0], sol->rr[1],
        sol->rr[2], enu2[0], enu2[1], enu2[2]
    );

    trace(2, "minFixSat=%d,%f\n", rtk->opt.minFixSat, rtk->opt.maxgdop);
    if (sol->stat != SOLQ_FIX || rtk->sol.ns[1] < rtk->opt.minFixSat)
    {
        trace(
            2, "warnning:stat=%d nfix=%d ns[0]=%d ns[1]=%d sumPostCarV=%.2f,stat,%d,ns1,%d\n",
            sol->stat, rtk->nfix, rtk->sol.ns[0], rtk->sol.ns[1], rtk->sumPostCarV, sol->stat,
            rtk->sol.ns[1]
        );
        trace(
            2,
            "warnning:%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%"
            "s%8.4f%s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
            s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]),
            sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]),
            sep, sol->age, sep, sol->ratio
        );
        if (sol->stat == SOLQ_FIX)
        {
            for (i = 0; i < 3; i++)
            {
                sol->thresCnt1[i] = 0;
                sol->thresCnt2[i] = 0;
                sol->enu_sum[i]   = 0.0;
            }
            rtk->sol.nsFixPre = rtk->sol.ns[1];
        }
        return 0;
    }
    // if (fixEnuErrorCheck <= 3 && rtk->enuWindwoIndex[0] > 0)
    // {
    //     if (fabs(rtk->enuWindow[0][rtk->enuWindwoIndex[0] - 1] - enu[0]) > 0.5 ||
    //         fabs(rtk->enuWindow[1][rtk->enuWindwoIndex[1] - 1] - enu[1]) > 0.5 ||
    //         fabs(rtk->enuWindow[2][rtk->enuWindwoIndex[2] - 1] - enu[2]) > 0.5)
    //     {
    //         sol->stat  = 0;
    //         sol->ratio = 0.0;
    //         fixEnuErrorCheck++;
    //         trace(2, "fixEnuErrorCheck:%d\n", fixEnuErrorCheck);
    //         return 0;
    //     }
    //     else
    //     {
    //         fixEnuErrorCheck = 0;
    //     }
    // }
    if (rtk->opt.detectSensitivity == 1)
    {
        /*
        if (rtk->aveEnu[0] != 0)
        {
            if (fabs(enu[0] - rtk->aveEnu[0]) > 0.5)
            {
                shiftCnt = 60 - (ROUND(fabs(enu[0] - rtk->aveEnu[0]) / 0.5)) * 10;
            }
            else if (fabs(enu[1] - rtk->aveEnu[1]) > 0.5)
            {
                shiftCnt = 60 - (ROUND(fabs(enu[1] - rtk->aveEnu[1]) / 0.5)) * 10;
            }
            else if (fabs(enu[2] - rtk->aveEnu[2]) > 0.5)
            {
                shiftCnt = 60 - (ROUND(fabs(enu[2] - rtk->aveEnu[2]) / 0.5)) * 10;
            }
            if (shiftCnt < 0)
            {
                shiftCnt = 10;
            }

            shiftCnt = 60;
        }
            */
    }
    else
    {
        medianFilter(enu, rtk);
    }

    // for (i = 0; i < 3; i++) enu2[i] = enu[i];
#ifdef MULBASE
    if (rtk->opt.masterSlaveBaseFlag == 0 && rtk->opt.masterXyz[0] != 0 &&
        rtk->opt.slaveXyz[0] != 0)
    {
        trace(
            4,
            "masterXyz:%.4f %.4f %.4f slaveXyz:%.4f %.4f %.4f masterEnu:%.4f "
            "%.4f %.4f\n",
            rtk->opt.masterXyz[0], rtk->opt.masterXyz[1], rtk->opt.masterXyz[2],
            rtk->opt.slaveXyz[0], rtk->opt.slaveXyz[1], rtk->opt.slaveXyz[2], rtk->masterEnu[0],
            rtk->masterEnu[1], rtk->masterEnu[2]
        );
        trace(4, "use slave base,enu:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);
        if (rtk->masterEnu[0] == 0.0 && rtk->masterEnu[1] == 0.0 && rtk->masterEnu[2] == 0.0)
        {
            trace(4, "masterEnu is zero\n");
            return 0;
        }

        if (rtk->slaveAvCnt > (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval))
        {
            rtk->slaveAvCnt = (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval);
        }
        for (i = 0; i < 3; i++)
        {
            rtk->slaveAveEnu[i] =
                (rtk->slaveAveEnu[i] * rtk->slaveAvCnt + enu2[i]) / (rtk->slaveAvCnt + 1);
        }
        rtk->slaveAvCnt++;

        delenu[0] = rtk->masterEnu[0] - rtk->slaveAveEnu[0];
        delenu[1] = rtk->masterEnu[1] - rtk->slaveAveEnu[1];
        delenu[2] = rtk->masterEnu[2] - rtk->slaveAveEnu[2];

        trace(4, "use slave base,denu:%.4f %.4f %.4f\n", delenu[0], delenu[1], delenu[2]);

        trace(4, "enu_1:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);

        for (i = 0; i < 3; i++)
        {
            enu2[i] = enu[i] = enu[i] + delenu[i];
        }
        trace(4, "enu_2:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);
    }
#endif
    // printf("%14.4lf %14.4lf %14.4lf\n", rtk->sol.fixxyz[0], rtk->sol.fixxyz[1],
    // rtk->sol.fixxyz[2]);
    if (rtk->fixCheckCnt == 0)
    {
        // for (i = 0; i < 3; i++)
        //     sol->fixxyz[i] = rtk->sol.rr[i];
        if (sol->rr_smooth_cnt > (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval))
        {
            sol->rr_smooth_cnt = (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval);
        }
        for (i = 0; i < 3; i++)
        {
            rtk->sol.fixxyz[i] =
                (sol->fixxyz[i] * sol->rr_smooth_cnt + rtk->sol.rr[i]) / (sol->rr_smooth_cnt + 1);
        }
        sol->rr_smooth_cnt++;
    }

    if (rtk->mvflag == 1)
    {
        for (i = 0; i < 3; i++)
        {
            if (sol->window[0].jumpflag[i] == 2 || sol->acc_warn[i] == 1)
            {
                sol->window[2].n[i] = 0;  // sol->window[0].n;

                sol->window[2].ave[i]   = sol->window[2].ave[i] + sol->jump[i];
                sol->window[2].var[i]   = 0.0;  // sol->window[0].var[i];
                sol->window[2].std[i]   = 0.0;  // sol->window[0].std[i];
                sol->window[2].sumX2[i] = 0.0;  // sol->window[0].sumX2[i];
                trace(2, "wjump,%d,%.4f\n", sol->window[0].jumpflag[i], sol->window[2].ave[i]);
            }
        }
    }

    if (rtk->mvflag == 1)
    {
        update_data(&rtk->sol.wdata, enu2);
        update_wind(&rtk->sol.window[0], &rtk->sol.wdata);
        update_wind(&rtk->sol.window[1], &rtk->sol.wdata);
        update_wind(&rtk->sol.window[2], &rtk->sol.wdata);

        calcjump(sol->window, sol->jump, sol->tmpjump, 3);

        trace(
            2, "window0, %s, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f,%d,%d,%d\n", s,
            sol->window[0].ave[0], sol->window[0].ave[1], sol->window[0].ave[2],
            sol->window[0].std[0], sol->window[0].std[1], sol->window[0].std[2],
            sol->window[0].n[0], sol->window[0].n[1], sol->window[0].n[2]
        );

        trace(
            2, "window1, %s, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f,%d,%d,%d\n", s,
            sol->window[1].ave[0], sol->window[1].ave[1], sol->window[1].ave[2],
            sol->window[1].std[0], sol->window[1].std[1], sol->window[1].std[2],
            sol->window[1].n[0], sol->window[1].n[1], sol->window[1].n[2]
        );

        trace(
            2, "window2, %s, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f,%d,%d,%d\n", s,
            sol->window[2].ave[0], sol->window[2].ave[1], sol->window[2].ave[2],
            sol->window[2].std[0], sol->window[2].std[1], sol->window[2].std[2],
            sol->window[2].n[0], sol->window[2].n[1], sol->window[2].n[2]
        );

        // logmsg(2, "window3, %s, %d, %d, %d, %.4f, %.4f, %.4f,%.4f, %.4f,
        // %.4f,\n", s,
        //        //
        //        sol->jump[0]+sol->window[2].ave[0],sol->jump[1]+sol->window[2].ave[0],sol->jump[2]+sol->window[2].ave[0],
        //        sol->window[0].jumpflag[0], sol->window[0].jumpflag[1],
        //        sol->window[0].jumpflag[2], sol->jump[0], sol->jump[1],
        //        sol->jump[2], sol->tmpjump[0], sol->tmpjump[1], sol->tmpjump[2]);
    }

    trace(2, "enu             :%10.4f %10.4f %10.4f\n", enu2[0], enu2[1], enu2[2]);
    trace(
        2, "aveEnu          :%10.4f %10.4f %10.4f\n", rtk->aveEnu[0], rtk->aveEnu[1], rtk->aveEnu[2]
    );
    trace(
        2, "stdEnu          :%10.4f %10.4f %10.4f\n", rtk->stdEnu[0], rtk->stdEnu[1], rtk->stdEnu[2]
    );

    if (rtk->opt.detectSensitivity == 10)
    {
        if (rtk->aveEnu[0] != 0.0 && rtk->enuWindwoIndex[0] > 3600.0 * 1 / rtk->opt.timeInterval)
        {
            detectSensitivity = 4;
        }
    }
    rtk->sol.nsFixPre = rtk->sol.ns[1];

    trace(2, "iniCnt:%d shiftCnt=%d\n", rtk->iniCnt, shiftCnt);
    if (rtk->iniCnt < rtk->initmax + 1)
    {
        rtk->iniCnt++;
    }

    for (k = 0; k < 3; k++)
    {
        if (rtk->enuWindwoIndex[k] >= rtk->maxSmoothPoint)
        {  // 窗口满
            if (rtk->aveEnu[k] != 0.0 &&
                fabs(enu[k] - rtk->aveEnu[k]) > detectSensitivity * rtk->stdEnu[k])
            {
                sol->thresCnt1[k]++;
                trace(0x02, "warnning a exceptional potin:%d\n", k);
                if (sol->thresCnt1[k] < shiftCnt / 2.0 && rtk->opt.detectSensitivity != 10)
                {
                    trace(0x02, "changed enu:%d\n", k);
                    enu[k] = rtk->aveEnu[k] + 0.0001;
                    ;
                }
                sol->enu_sum[k] += enu2[k];
            }
            else
            {
                if (sol->thresCnt1[k] > 0)
                {
                    sol->thresCnt2[k]++;
                    sol->enu_sum[k] += enu2[k];
                }
                else
                {
                    sol->thresCnt2[k] = 0;
                }
            }
            if ((sol->thresCnt2[k] - sol->thresCnt1[k] >= 15) && (sol->thresCnt1[k] <= 5))
            {
                sol->thresCnt1[k] = 0;
                sol->thresCnt2[k] = 0;
                sol->enu_sum[k]   = 0.0;
            }
            trace(
                2, "thresCnt,k=%d, %d,%d,%.4f,%.4f\n", k, sol->thresCnt1[k], sol->thresCnt2[k],
                fabs(enu[k] - rtk->aveEnu[k]), detectSensitivity * rtk->stdEnu[k]
            );

            if (sol->thresCnt1[k] + sol->thresCnt2[k] >= shiftCnt)
            {
                rtk->sol.enu_shift[k] =
                    sol->enu_sum[k] / (sol->thresCnt1[k] + sol->thresCnt2[k]) - rtk->aveEnu[k];
                if (fabs(rtk->sol.enu_shift[k]) > 0.03 && rtk->opt.timeInterval >= 15)
                {
                    precent = 0.5;
                }
                if ((sol->thresCnt1[k] > shiftCnt * precent && rtk->opt.detectSensitivity != 10) ||
                    (fabs(enu[k] - rtk->aveEnu[k]) > 0.5 && fabs(rtk->aveEnu[k]) != 0.0 &&
                     rtk->opt.detectSensitivity != 10))
                {
                    // rtk->sol.enu_shift[k] = sol->enu_sum[k] / (sol->thresCnt1[k] +
                    // sol->thresCnt2[k]) - rtk->aveEnu[k];
                    trace(2, "warnning has detect shift:%.2f;k=%d\n", rtk->sol.enu_shift[k], k);
                    if (k == 0 || k == 1)
                    {
                        if (fabs(rtk->sol.enu_shift[k]) > 0.01)
                        {
                            rtk->sum_enu[k]   = 0;
                            rtk->sum_sqeun[k] = 0;
                            for (i = 0; i < rtk->maxSmoothPoint; i++)
                            {
                                if (fabs(enu[k] - rtk->aveEnu[k]) > 1)
                                {
                                    rtk->enuWindow[k][i] = rtk->aveEnu[k] + rtk->sol.enu_shift[k];
                                    rtk->stdEnu[k]       = fabs(rtk->sol.enu_shift[k]);
                                    cntdrift             = rtk->maxMedianFilterPoint;
                                }
                                else
                                {
                                    rtk->enuWindow[k][i] += rtk->sol.enu_shift[k];
                                }
                                rtk->sum_enu[k] = rtk->sum_enu[k] + rtk->enuWindow[k][i];
                                rtk->sum_sqeun[k] =
                                    rtk->sum_sqeun[k] + rtk->enuWindow[k][i] * rtk->enuWindow[k][i];
                            }
                            // if ((int)rtk->ep[3] - 2 < 0)
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2 + 24];
                            // else
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2];
                        }
                        else
                        {
                            rtk->sol.enu_shift[k] = 0.0;
                        }
                    }
                    else
                    {
                        if (fabs(rtk->sol.enu_shift[k]) > 0.03)
                        {
                            rtk->sum_enu[k]   = 0;
                            rtk->sum_sqeun[k] = 0;
                            for (i = 0; i < rtk->maxSmoothPoint; i++)
                            {
                                rtk->enuWindow[k][i] += rtk->sol.enu_shift[k];
                                rtk->sum_enu[k] = rtk->sum_enu[k] + rtk->enuWindow[k][i];
                                rtk->sum_sqeun[k] =
                                    rtk->sum_sqeun[k] + rtk->enuWindow[k][i] * rtk->enuWindow[k][i];
                            }
                            // if ((int)rtk->ep[3] - 2 < 0)
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2 + 24];
                            // else
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2];
                        }
                        else
                        {
                            rtk->sol.enu_shift[k] = 0.0;
                        }
                    }
                }
                sol->thresCnt1[k] = 0;
                sol->thresCnt2[k] = 0;
                sol->enu_sum[k]   = 0.0;
            }
            rtk->sum_enu[k] = rtk->sum_enu[k] - rtk->enuWindow[k][rtk->delpoint[k]] + enu[k];
            rtk->sum_sqeun[k] =
                rtk->sum_sqeun[k] - SQR(rtk->enuWindow[k][rtk->delpoint[k]]) + enu[k] * enu[k];

            rtk->aveEnu[k] = rtk->sum_enu[k] / rtk->maxSmoothPoint;

            var = (rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) / rtk->maxSmoothPoint) /
                  (rtk->maxSmoothPoint - 1);
            if (var < 0)
            {
                rtk->stdEnu[k] = 1E-7;
            }
            else
            {
                rtk->stdEnu[k] = sqrt(
                    (rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) / rtk->maxSmoothPoint) /
                    (rtk->maxSmoothPoint - 1)
                );
            }

            rtk->enuWindow[k][rtk->delpoint[k]] = enu[k];
            rtk->delpoint[k]                    = (++rtk->delpoint[k]) % rtk->maxSmoothPoint;
        }
        else
        {  // 窗口没满
            if (rtk->aveEnu[k] != 0.0 &&
                    fabs(enu[k] - rtk->aveEnu[k]) > detectSensitivity * rtk->stdEnu[k] ||
                (fabs(enu[k] - rtk->aveEnu[k]) > 0.5 && fabs(rtk->aveEnu[k]) != 0.0 &&
                 rtk->opt.detectSensitivity != 10))
            {
                sol->thresCnt1[k]++;
                trace(0x02, "warnning a exceptional potin:%d\n", k);
                if (sol->thresCnt1[k] < shiftCnt / 2.0 && rtk->opt.detectSensitivity != 10)
                {
                    trace(0x02, "changed enu:%d\n", k);
                    enu[k] = rtk->aveEnu[k] + 0.0001;
                }
                sol->enu_sum[k] += enu2[k];
            }
            else
            {
                if (sol->thresCnt1[k] > 0)
                {
                    sol->thresCnt2[k]++;
                    sol->enu_sum[k] += enu2[k];
                }
                else
                {
                    sol->thresCnt2[k] = 0;
                }
            }
            if ((sol->thresCnt2[k] - sol->thresCnt1[k] >= 15) && (sol->thresCnt1[k] <= 5))
            {
                sol->thresCnt1[k] = 0;
                sol->enu_sum[k]   = 0.0;
            }
            if (sol->thresCnt1[k] + sol->thresCnt2[k] >= shiftCnt)
            {
                rtk->sol.enu_shift[k] =
                    sol->enu_sum[k] / (sol->thresCnt1[k] + sol->thresCnt2[k]) - rtk->aveEnu[k];
                if (fabs(rtk->sol.enu_shift[k]) > 0.03 && rtk->opt.timeInterval >= 15)
                {
                    precent = 0.5;
                }
                if (sol->thresCnt1[k] > shiftCnt * precent && rtk->opt.detectSensitivity != 10 ||
                    (fabs(enu[k] - rtk->aveEnu[k]) > 0.5 && fabs(rtk->aveEnu[k]) != 0.0 &&
                     rtk->opt.detectSensitivity != 10))
                {
                    // rtk->sol.enu_shift[k] = sol->enu_sum[k] / (sol->thresCnt1[k] +
                    // sol->thresCnt2[k]) - rtk->aveEnu[k];
                    if (k == 0 || k == 1)
                    {
                        if (fabs(rtk->sol.enu_shift[k]) > 0.01)
                        {
                            rtk->sum_enu[k]   = 0;
                            rtk->sum_sqeun[k] = 0;
                            for (i = 0; i < rtk->enuWindwoIndex[k]; i++)
                            {
                                rtk->enuWindow[k][i] += rtk->sol.enu_shift[k];
                                rtk->sum_enu[k] = rtk->sum_enu[k] + rtk->enuWindow[k][i];
                                rtk->sum_sqeun[k] =
                                    rtk->sum_sqeun[k] + rtk->enuWindow[k][i] * rtk->enuWindow[k][i];
                            }
                            // if ((int)rtk->ep[3] - 2 < 0)
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2 + 24];
                            // else
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2];
                        }
                        else
                        {
                            rtk->sol.enu_shift[k] = 0.0;
                        }
                    }
                    else
                    {
                        if (fabs(rtk->sol.enu_shift[k]) > 0.03)
                        {
                            rtk->sum_enu[k]   = 0;
                            rtk->sum_sqeun[k] = 0;
                            for (i = 0; i < rtk->enuWindwoIndex[k]; i++)
                            {
                                rtk->enuWindow[k][i] += rtk->sol.enu_shift[k];
                                rtk->sum_enu[k] = rtk->sum_enu[k] + rtk->enuWindow[k][i];
                                rtk->sum_sqeun[k] =
                                    rtk->sum_sqeun[k] + rtk->enuWindow[k][i] * rtk->enuWindow[k][i];
                            }
                            // if ((int)rtk->ep[3] - 2 < 0)
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2 + 24];
                            // else
                            //     rtk->stdEnu[k] = rtk->stdEnuHour[k][(int)rtk->ep[3] - 2];
                        }
                        else
                        {
                            rtk->sol.enu_shift[k] = 0.0;
                        }
                    }
                }
                sol->thresCnt1[k] = 0;
                sol->thresCnt2[k] = 0;
                sol->enu_sum[k]   = 0.0;
            }
            rtk->sum_enu[k]   = rtk->sum_enu[k] + enu[k];
            rtk->sum_sqeun[k] = rtk->sum_sqeun[k] + enu[k] * enu[k];
            rtk->aveEnu[k]    = rtk->sum_enu[k] / (rtk->enuWindwoIndex[k] + 1);
            rtk->enuWindow[k][rtk->enuWindwoIndex[k]] = enu[k];
            rtk->enuWindwoIndex[k]++;
            if (rtk->enuWindwoIndex[k] == 1)
            {
                rtk->stdEnu[k] = 0;
            }
            else
            {
                var = (rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) / rtk->maxSmoothPoint) /
                      (rtk->maxSmoothPoint - 1);
                if (var < 0)
                {
                    rtk->stdEnu[k] = 1E-7;
                }
                else
                {
                    rtk->stdEnu[k] = sqrt(
                        (rtk->sum_sqeun[k] - SQR(rtk->sum_enu[k]) / rtk->enuWindwoIndex[k]) /
                        (rtk->enuWindwoIndex[k] - 1)
                    );
                }
            }
        }
    }

    if (sol->stat == SOLQ_FIX)
    {
        sol->fixCnt++;
    }
    else
    {
        sol->floatCnt++;
    }
    //--------------------------------original-----------------------------------
    for (i = 0; i < 3; i++)
    {
        sol->enu_original[i] = enu2[i];
    }
    ecef2pos(rtk->rb, pos);
    enu2ecef(pos, enu2, dr);
    for (i = 0; i < 3; i++)
    {
        r[i] = rtk->rb[i];
        r[i] += dr[i];
    }
    for (i = 0; i < 3; i++)
    {
        sol->rr_original[i] = r[i];
    }

    //-----------------------------------smooth--------------------------------
    // if (trace_flag[1] == 1) {

    if (rtk->mvflag == 0)
    {
        for (i = 0; i < 3; i++)
        {
            enu2[i] = rtk->aveEnu[i];
            // enu2[i] = enu2[i] - (rtk->aveEnu[i] - rtk->sol.ori_ave[i]);
        }
    }
    else
    {
        for (i = 0; i < 3; i++)
        {
            // rtk->enuWindwoIndex[i]=sol->window[2].n[i];
            //  enu2[i] = rtk->aveEnu[i];
            //  enu2[i] = enu2[i] - (sol->window[2].ave[i] - rtk->sol.ori_ave[i]);
            enu2[i] = sol->window[2].ave[i];
        }
    }

    for (i = 0; i < 3; i++)
    {
        sol->enu[i] = enu2[i];
    }
    ecef2pos(rtk->rb, pos);
    enu2ecef(pos, enu2, dr);
    for (i = 0; i < 3; i++)
    {
        r[i] = rtk->rb[i];
        r[i] += dr[i];
    }
    for (i = 0; i < 3; i++)
    {
        sol->rr[i] = r[i];
    }
    for (i = 0; i < 3; i++)
    {
        sol->rr_filer[i] = r[i];
    }

#ifdef MULBASE
    if (rtk->opt.masterXyz[0] == 0)
    {
        rtk->opt.masterXyz[0] = rtk->rb[0];
        rtk->opt.masterXyz[1] = rtk->rb[1];
        rtk->opt.masterXyz[2] = rtk->rb[2];
    }
    if (rtk->opt.masterSlaveBaseFlag == 0 && rtk->opt.masterXyz[0] != 0 &&
        rtk->opt.slaveXyz[0] != 0 && rtk->dr[0] == 0.0)
    {
        rtk->dr[0] = rtk->masterRr[0] - sol->rr_filer[0];
        rtk->dr[1] = rtk->masterRr[1] - sol->rr_filer[1];
        rtk->dr[2] = rtk->masterRr[2] - sol->rr_filer[2];
    }
#endif
    // printf("rr_original:%14.4lf %14.4lf %14.4lf\n", sol->rr_original[0],
    // sol->rr_original[1], sol->rr_original[2]); printf("rr_filer:%14.4lf %14.4lf
    // %14.4lf\n", sol->rr_filer[0], sol->rr_filer[1], sol->rr_filer[2]);

    trace(
        2, "%14.4lf %14.4lf %14.4lf %u %u %u\n", rtk->stdEnu[0], rtk->stdEnu[1], rtk->stdEnu[2],
        rtk->enuWindwoIndex[0], rtk->enuWindwoIndex[1], rtk->enuWindwoIndex[2]
    );
    trace(2, "rtk->tt=%.2f maxSmoothPoint=%d\n", rtk->tt, rtk->maxSmoothPoint);
    trace(2, "cnt1:  %02d  %02d  %02d\n", sol->thresCnt1[0], sol->thresCnt1[1], sol->thresCnt1[2]);
    trace(2, "cnt2:  %02d  %02d  %02d\n", sol->thresCnt2[0], sol->thresCnt2[1], sol->thresCnt2[2]);

    trace(
        0x02,
        "enu1:%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
        "s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
        s, sep, sol->enu_original[0], sep, sol->enu_original[1], sep, sol->enu_original[2], sep,
        sol->stat, sep, ns, sep, SQRT(Q[0]), sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]),
        sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep, sol->age, sep, sol->ratio
    );

    trace(
        0x02,
        "enu2:%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
        "s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio
    );

    p += sprintf(
        p,
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
        "s%8.4f%s%6.2f%s%6.1f%s%.4f%s%.4f%s%.4f%s%.0f%s%.0f%s%.0f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio, sep, rtk->fftFrq[0], sep, rtk->fftFrq[1], sep, rtk->fftFrq[2],
        sep, rtk->fftPower[0], sep, rtk->fftPower[1], sep, rtk->fftPower[2]
    );

    printf(
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8."
        "4f%s%8.4f%s%6.2f%s%6.1f\n",
        s, sep, sol->enu_original[0], sep, sol->enu_original[1], sep, sol->enu_original[2], sep,
        sol->stat, sep, ns, sep, SQRT(Q[0]), sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]),
        sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep, sol->age, sep, sol->ratio
    );
    if (oFile.fpOut[0])
    {
        fprintf(
            oFile.fpOut[0],
            "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
            "s%8.4f%s%8.4f%s%6.2f%s%6.1f,%4.2f,%4.2f,%4.2f,\n",
            s, sep, sol->enu_original[0], sep, sol->enu_original[1], sep, sol->enu_original[2], sep,
            sol->stat, sep, ns, sep, SQRT(Q[0]), sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]),
            sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep, sol->age, sep, sol->ratio, sol->dop[0][0],
            sol->dop[0][1], sol->dop[0][2]
        );
    }
    return p - (char*)buff;
}

static int outenu2(
    unsigned char* buff, const char* s, rtk_t* rtk, sol_t* sol, const double* rb,
    const solopt_t* opt
)
{
    double      pos[3], rr[3], enu2[3], P[9], Q[9], weight1, weight2;
    int         i, k, index, ns, flag[3] = {0};
    const char* sep = opt2sep(opt);
    char*       p   = (char*)buff;
    double      var0, var1, thres0[3] = {0}, thres[3] = {0.02, 0.02, 0.02};
    double      dr[3], r[3], delenu[3];
    double      sumx, sumy, sumz, errorX, errorY, errorZ, truePoint;
    int         thresCnt = 3;
    for (i = 0; i < 3; i++)
    {
        rr[i] = sol->rr[i] - rb[i];
    }
    ns = sol->stat == PMODE_SINGLE ? sol->ns[0] : sol->ns[1];

    trace(2, "outenu2(): \n");
    ecef2pos(sol->rr, pos);
    soltocov(sol, P);         // 得到XYZ方向状态协方差
    covenu(pos, P, Q);        // 将XYZ方向协方差转到ENU方向方差
    ecef2enu(pos, rr, enu2);  // 大地坐标转站心坐标

    if (sol->stat != SOLQ_FIX || rtk->sol.ns[1] < rtk->opt.minFixSat)
    {
        trace(
            0x4, "warnning:stat=%d nfix=%d ns[0]=%d ns[1]=%d sumPostCarV=%.2f\n", sol->stat,
            rtk->nfix, rtk->sol.ns[0], rtk->sol.ns[1], rtk->sumPostCarV
        );
        trace(
            0x4,
            "warnning:%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%"
            "s%8.4f%s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
            s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]),
            sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]),
            sep, sol->age, sep, sol->ratio
        );
        return 0;
    }
#ifdef MULBASE
    if (rtk->opt.masterSlaveBaseFlag == 0 && rtk->opt.masterXyz[0] != 0 &&
        rtk->opt.slaveXyz[0] != 0)
    {
        trace(
            4,
            "masterXyz:%.4f %.4f %.4f slaveXyz:%.4f %.4f %.4f masterEnu:%.4f "
            "%.4f %.4f\n",
            rtk->opt.masterXyz[0], rtk->opt.masterXyz[1], rtk->opt.masterXyz[2],
            rtk->opt.slaveXyz[0], rtk->opt.slaveXyz[1], rtk->opt.slaveXyz[2], rtk->masterEnu[0],
            rtk->masterEnu[1], rtk->masterEnu[2]
        );
        trace(4, "use slave base,enu:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);
        if (rtk->masterEnu[0] == 0.0 && rtk->masterEnu[1] == 0.0 && rtk->masterEnu[2] == 0.0)
        {
            trace(4, "masterEnu is zero\n");
            return 0;
        }
        thresCnt = 10;
        if (rtk->slaveAvCnt > (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval))
        {
            rtk->slaveAvCnt = (int)((4 * 3600.0 - 1) / rtk->opt.timeInterval);
        }
        for (i = 0; i < 3; i++)
        {
            rtk->slaveAveEnu[i] =
                (rtk->slaveAveEnu[i] * rtk->slaveAvCnt + enu2[i]) / (rtk->slaveAvCnt + 1);
        }
        rtk->slaveAvCnt++;

        delenu[0] = rtk->masterEnu[0] - rtk->slaveAveEnu[0];
        delenu[1] = rtk->masterEnu[1] - rtk->slaveAveEnu[1];
        delenu[2] = rtk->masterEnu[2] - rtk->slaveAveEnu[2];

        enuInit[0] = rtk->masterEnu[0];
        enuInit[1] = rtk->masterEnu[1];
        enuInit[2] = rtk->masterEnu[2];

        trace(4, "use slave base,denu:%.4f %.4f %.4f\n", delenu[0], delenu[1], delenu[2]);

        trace(4, "enu_1:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);

        for (i = 0; i < 3; i++)
        {
            enu2[i] = enu2[i] + delenu[i];
        }
        trace(4, "enu_2:%.4f %.4f %.4f\n", enu2[0], enu2[1], enu2[2]);
    }
#endif
    for (i = 0; i < 3; i++)
    {
        if (enuInit[i] == 0.0)
        {
            continue;
        }
        if (i == 2)
        {
            if (fabs(enu2[i] - enuInit[i]) > 0.5 && unusualEnuCnt < thresCnt)
            {
                unusualEnuCnt++;
                trace(
                    0x04, "warnning enu:%.2f %.2f %.2f enuIni:%.2f %.2f %.2f\n", enu2[i], enu2[i],
                    enu2[i], enuInit[i], enuInit[i], enuInit[i]
                );
                return 0;
            }
        }
        else
        {
            if (fabs(enu2[i] - enuInit[i]) > 0.1 && unusualEnuCnt < thresCnt)
            {
                unusualEnuCnt++;
                trace(
                    0x04, "warnning enu:%.2f %.2f %.2f enuIni:%.2f %.2f %.2f\n", enu2[i], enu2[i],
                    enu2[i], enuInit[i], enuInit[i], enuInit[i]
                );
                return 0;
            }
        }
    }
    if (unusualEnuCnt >= thresCnt)
    {
        enuInit[0] = enuInit[1] = enuInit[2] = 0.0;
        rtk->enuWindwoIndex[0] = rtk->enuWindwoIndex[1] = rtk->enuWindwoIndex[2] = 0;
    }
    unusualEnuCnt = 0;
    /*
        if (rtk->sol.ns[1] <= SELETE_SAT_NUM && rtk->rejSatCnt == 0) {
            if (rtk->satMapNfix[rtk->sol.ns[1] - 1] < rtk->nfix) {
                rtk->satMapEnu[rtk->sol.ns[1] - 1][0] = enu2[0];
                rtk->satMapEnu[rtk->sol.ns[1] - 1][1] = enu2[1];
                rtk->satMapEnu[rtk->sol.ns[1] - 1][2] = enu2[2];
                rtk->satMapNfix[rtk->sol.ns[1] - 1] = rtk->nfix;
                if (rtk->satMapNfix[rtk->sol.ns[1] - 1] > 3600.0) {
                    rtk->satMapNfix[rtk->sol.ns[1] - 1] = 3600.0;
                }
            }
        }
        index = -1;
        for (i = SELETE_SAT_NUM - 1; i >= 0; i--) {
            if (rtk->satMapEnu[i][0] != 0.0) {
                index = i;
                break;
            }
        }
        if (index != -1) {
            if (rtk->nfix >= 3600) rtk->nfix = 3600;
            weight1 = (rtk->nfix / 3600.0);
            weight2 = (SQR(rtk->sol.ns[1]) * SQR(rtk->sol.ns[1]) / SQR(400.0));
            for (i = 0; i < 3; i++) {
                enu2[i] = 0.5 * (weight1 * enu2[i] + (1 - weight1) *
       rtk->satMapEnu[index][i]) + 0.5 * (weight2 * enu2[i] + (1 - weight2) *
       rtk->satMapEnu[index][i]);
            }
            trace(0x04, "nfix=%d ns=%d weight1=%.2f weight2=%.2f enu=%.2f %.2f
       %.2f satMapEnu=%.2f %.2f %.2f\n", rtk->nfix, index + 1, weight1, weight2,
       rtk->satMapEnu[index][0], rtk->satMapEnu[index][1],
       rtk->satMapEnu[index][2], enu2[0], enu2[1], enu2[2]);
        }
        trace(0x02, "maxSmoothPoint=%d\n", rtk->maxSmoothPoint);
        if (sol->stat == SOLQ_FIX) {
            if (rtk->xyzWindwoIndex >= rtk->maxSmoothPoint) {
                for (i = 1; i < rtk->maxSmoothPoint; i++) {
                    rtk->enuWindow[0][i - 1] = rtk->enuWindow[0][i];
                    rtk->enuWindow[1][i - 1] = rtk->enuWindow[1][i];
                    rtk->enuWindow[2][i - 1] = rtk->enuWindow[2][i];
                }
                rtk->enuWindow[0][(int)rtk->maxSmoothPoint - 1] = enu2[0];
                rtk->enuWindow[1][(int)rtk->maxSmoothPoint - 1] = enu2[1];
                rtk->enuWindow[2][(int)rtk->maxSmoothPoint - 1] = enu2[2];

                sumx = 0.0; sumy = 0.0; sumz = 0.0;
                for (i = 0; i < rtk->maxSmoothPoint; i++) {
                    sumx += rtk->enuWindow[0][i];
                    sumy += rtk->enuWindow[1][i];
                    sumz += rtk->enuWindow[2][i];
                }
                rtk->aveXyz[0] = sumx / rtk->maxSmoothPoint;
                rtk->aveXyz[1] = sumy / rtk->maxSmoothPoint;
                rtk->aveXyz[2] = sumz / rtk->maxSmoothPoint;

                //for (i = 0; i < rtk->maxSmoothPoint; i++) {
                //    errorX = rtk->enuWindow[0][i] - rtk->aveXyz[0];
                //    errorY = rtk->enuWindow[1][i] - rtk->aveXyz[1];
                //    errorZ = rtk->enuWindow[2][i] - rtk->aveXyz[2];
                //    rtk->xyzError[i] = sqrt(SQR(errorX) + SQR(errorY) +
       SQR(errorZ));
                //    rtk->xyzErrorSortIndex[i] = i;
                //}
                //quick3WaySort(rtk->xyzError, rtk->xyzErrorSortIndex, 0,
       rtk->maxSmoothPoint - 1);
                //truePoint = ROUND(rtk->maxSmoothPoint * 0.7);

                //sumx = 0.0; sumy = 0.0; sumz = 0.0;
                //for (i = 0; i < truePoint; i++) {
                //    index = rtk->xyzErrorSortIndex[i];
                //    rtk->xyzWindowTure[0][i] = rtk->enuWindow[0][index];
                //    rtk->xyzWindowTure[1][i] = rtk->enuWindow[1][index];
                //    rtk->xyzWindowTure[2][i] = rtk->enuWindow[2][index];

                //    sumx += rtk->xyzWindowTure[0][i];
                //    sumy += rtk->xyzWindowTure[1][i];
                //    sumz += rtk->xyzWindowTure[2][i];
                //}
                //rtk->aveXyz[0] = sumx / truePoint;
                //rtk->aveXyz[1] = sumy / truePoint;
                //rtk->aveXyz[2] = sumz / truePoint;
            }
            else {
                index = rtk->xyzWindwoIndex;
                for (i = 0; i < 3; i++) {
                    rtk->enuWindow[i][index] = enu2[i];
                }
                rtk->xyzWindwoIndex++;
                //return 0;
                sumx = 0.0; sumy = 0.0; sumz = 0.0;
                for (i = 0; i < rtk->xyzWindwoIndex; i++) {
                    sumx += rtk->enuWindow[0][i];
                    sumy += rtk->enuWindow[1][i];
                    sumz += rtk->enuWindow[2][i];
                }
                rtk->aveXyz[0] = sumx / rtk->xyzWindwoIndex;
                rtk->aveXyz[1] = sumy / rtk->xyzWindwoIndex;
                rtk->aveXyz[2] = sumz / rtk->xyzWindwoIndex;
            }
        }
        else
            return 0;
        enu2[0] = rtk->aveXyz[0];
        enu2[1] = rtk->aveXyz[1];
        enu2[2] = rtk->aveXyz[2];
    */
    if (sol->stat == SOLQ_FIX)
    {
        sol->fixCnt++;
    }
    else
    {
        sol->floatCnt++;
    }

    for (i = 0; i < 3; i++)
    {
        sol->ori_ave[i] =
            (sol->ori_ave[i] * rtk->enuWindwoIndex[i] + enu2[i]) / (rtk->enuWindwoIndex[i] + 1);
        if (rtk->enuWindwoIndex[i] <= 600)
        {
            rtk->enuWindwoIndex[i]++;
        }
        if (rtk->enuWindwoIndex[i] > 600 && enuInit[i] == 0.0)
        {
            enuInit[i] = sol->ori_ave[i];
        }
    }

    rtk->fftFrq[0] = rtk->fftFrq[1] = rtk->fftFrq[2] = 0.0;
    rtk->fftPower[0] = rtk->fftPower[1] = rtk->fftPower[2] = 0.0;
    //---------------------------------fft--------------------------------------
    // if (enuInit[0] == 0.0 || enuInit[1] == 0.0 || enuInit[2] == 0.0) return 0;
    for (k = 0; k < 3; k++)
    {
        if (enuInit[0] != 0.0 || enuInit[1] != 0.0 || enuInit[2] != 0.0)
        {
            data      = enu2[k] - enuInit[k];
            data_in_I = data * 10000;
            // FFT data in
            for (i = 0; i < FFT_LEN - 1; i++)
            {
                FFTInBuf[k][i].real = FFTInBuf[k][i + 1].real;
                FFTInBuf[k][i].imag = FFTInBuf[k][i + 1].imag;
            }
            FFTInBuf[k][i].real = data_in_I;
            FFTInBuf[k][i].imag = 0;
            Saturate12bit(FFTInBuf[k][i].real);
            Saturate12bit(FFTInBuf[k][i].imag);

            // FFT process
            fft_256_12b12b(FFTInBuf[k], FFTOutBufTemp[k]);

            // FFT reverse & calculate FFT mean
            FFTMag = 0;
            for (i = 0; i < FFT_LEN; i++)
            {
                idx                    = bit_reverse(i, FFT_LEN_BITWIDTH);
                FFTOutBuf[k][idx].real = FFTOutBufTemp[k][i].real;
                FFTOutBuf[k][idx].imag = FFTOutBufTemp[k][i].imag;
                FFTMag += calc_mag(FFTOutBuf[k][idx]);
            }
            FFTMagMean = (FFTMag >> FFT_LEN_BITWIDTH);
            // find max FFT bin, compare with threshold, output frequency
            FFTMagMax = 0;
            for (i = 0; i < FFT_LEN / 2; i++)
            {
                FFTMag = calc_mag(FFTOutBuf[k][i]);
                if (FFTMagMax < FFTMag)
                {
                    FFTMagMax    = FFTMag;
                    FFTMagMaxIdx = i;
                }
            }
            if (FFTMagMax > (FFTMagMean * FFTDectTh))
            {
                Fs          = rtk->fs;
                FFTDectFreq = Fs / FFT_LEN * FFTMagMaxIdx;
                trace(
                    0x10,
                    "k=%d FFT %d, FFTMagMean %d, FFTMagMax %d, FFTMagMaxIdx %d, "
                    "FFTDectFreq %f\n",
                    k, count, FFTMagMean, FFTMagMax, FFTMagMaxIdx, FFTDectFreq
                );
                rtk->fftFrq[k]   = FFTDectFreq;
                rtk->fftPower[k] = FFTMagMax;
                // printf("k=%d FFT %d, FFTMagMean %d, FFTMagMax %d, FFTMagMaxIdx %d,
                // FFTDectFreq %f\n", k, count, FFTMagMean, FFTMagMax, FFTMagMaxIdx,
                // FFTDectFreq);
            }
            else
            {
                trace(0x10, "k=%d FFT %d, FFTMagMean %d, No Freq dect!\n", k, count, (FFTMag >> 8));
            }
            // FFT result output
            // for (i = 0; i < FFT_LEN; i++)
            //{
            //    printf("I-%d %d, Q-%d %d\n", i, FFTOutBuf[i].real, i,
            //    FFTOutBuf[i].imag);
            //}
            // count++;
            // BandPassFilter & output
            BandPassFilterProcess(&BandPassFilter, &data_in_I, &data_in_Q);
            data_out_I = (data_in_I >> (BPF_COEF_BITWIDTH - 1)) / 10000.0;
            data_out_Q = (data_in_Q >> (BPF_COEF_BITWIDTH - 1)) / 10000.0;
            // if (k == 2)
            //     printf("I-%d %f, Q-%d %f\n", count, data_out_I, count, data_out_Q);
            //------------------------------------------------------------------------
        }
    }

    //--------------------------------original-----------------------------------
    for (i = 0; i < 3; i++)
    {
        sol->enu_original[i] = enu2[i];
    }
    ecef2pos(rtk->rb, pos);
    enu2ecef(pos, enu2, dr);
    // printf("rr:%14.4lf %14.4lf %14.4lf\n", sol->rr[0], sol->rr[1], sol->rr[2]);
    for (i = 0; i < 3; i++)
    {
        r[i] = rtk->rb[i];
        r[i] += dr[i];
    }
    printf("r:%14.4lf %14.4lf %14.4lf\n", r[0], r[1], r[2]);
    for (i = 0; i < 3; i++)
    {
        sol->rr_original[i] = r[i];
    }

    for (i = 0; i < 3; i++)
    {
        sol->enu[i] = enu2[i];
    }
    ecef2pos(rtk->rb, pos);
    enu2ecef(pos, enu2, dr);
    for (i = 0; i < 3; i++)
    {
        r[i] = rtk->rb[i];
        r[i] += dr[i];
    }
    for (i = 0; i < 3; i++)
    {
        sol->rr[i] = r[i];
    }
    for (i = 0; i < 3; i++)
    {
        sol->rr_filer[i] = r[i];
    }
#ifdef MULBASE
    if (rtk->opt.masterXyz[0] == 0)
    {
        rtk->opt.masterXyz[0] = rtk->rb[0];
        rtk->opt.masterXyz[1] = rtk->rb[1];
        rtk->opt.masterXyz[2] = rtk->rb[2];
    }
    if (rtk->opt.masterSlaveBaseFlag == 0 && rtk->opt.masterXyz[0] != 0 &&
        rtk->opt.slaveXyz[0] != 0 && rtk->dr[0] == 0.0)
    {
        rtk->dr[0] = rtk->masterRr[0] - sol->rr_filer[0];
        rtk->dr[1] = rtk->masterRr[1] - sol->rr_filer[1];
        rtk->dr[2] = rtk->masterRr[2] - sol->rr_filer[2];
    }
#endif
    // trace(4,"%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
    //     s, sep, sol->enu_original[0], sep, sol->enu_original[1], sep,
    //     sol->enu_original[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
    //     SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep,
    //     sqvar(Q[2]), sep, sol->age, sep, sol->ratio);

    // p += sprintf(p,
    // "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%6.2f%s%6.1f%s%.3f%s%.3f\n",
    //     s, sep, sol->enu_original[0] - enuInit[0], sep, sol->enu_original[1] -
    //     enuInit[1], sep, sol->enu_original[2] - enuInit[2], sep, sol->stat,
    //     sep, ns, sep, SQRT(Q[0]), sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep,
    //     sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep, sol->age, sep,
    //     sol->ratio,sep, rtk->fftFrq,sep, rtk->fftPower);

    trace(
        0x02,
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8."
        "4f%s%8.4f%s%6.2f%s%6.1f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio
    );

    p += sprintf(
        p,
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
        "s%8.4f%s%6.2f%s%6.1f%s%.4f%s%.4f%s%.4f%s%.0f%s%.0f%s%.0f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio, sep, rtk->fftFrq[0], sep, rtk->fftFrq[1], sep, rtk->fftFrq[2],
        sep, rtk->fftPower[0], sep, rtk->fftPower[1], sep, rtk->fftPower[2]
    );
    return p - (char*)buff;
}

extern void pos2enu(double* rb, double* rr, double* enu)
{
    int    i = 0;
    double pos[3], r[3], P[9], Q[9];

    for (i = 0; i < 3; i++)
    {
        r[i] = rr[i] - rb[i];
    }
    ecef2pos(rr, pos);
    ecef2enu(pos, r, enu);  // 大地坐标转站心坐标
}
static int outenuSstatic(
    unsigned char* buff, const char* s, rtk_t* rtk, sol_t* sol, const double* rb,
    const solopt_t* opt
)
{
    double      pos[3], rr[3], enu2[3], P[9], Q[9];
    int         i, k, ns, flag[3] = {0}, nfix;
    const char* sep = opt2sep(opt);
    char*       p   = (char*)buff;
    double      var0, var1, thres0[3] = {0}, thres[3] = {0.02, 0.02, 0.02};
    double      dr[3], r[3];

    for (i = 0; i < 3; i++)
    {
        rr[i] = sol->rr[i] - rb[i];
    }

    ns = sol->stat == PMODE_SINGLE ? sol->ns[0] : sol->ns[1];

    ecef2pos(sol->rr, pos);
    soltocov(sol, P);         // 得到XYZ方向状态协方差
    covenu(pos, P, Q);        // 将XYZ方向协方差转到ENU方向方差
    ecef2enu(pos, rr, enu2);  // 大地坐标转站心坐标

    nfix = 1;
    if (rtk->nfix < nfix || sol->stat != SOLQ_FIX || rtk->sol.ns[1] < 10)
    {
        trace(
            0x04, "warnning:stat=%d nfix=%d ns[0]=%d ns[1]=%d sumPostCarV=%.2f\n", sol->stat,
            rtk->nfix, rtk->sol.ns[0], rtk->sol.ns[1], rtk->sumPostCarV
        );
        trace(
            0x04,
            "warnning:%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%"
            "s%8.4f%s%8.4f%s%8.4f%s%6.2f%s%6.1f\n",
            s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]),
            sep, SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]),
            sep, sol->age, sep, sol->ratio
        );
        return 0;
    }
    rtk->sol.enu[0] = enu2[0];
    rtk->sol.enu[1] = enu2[1];
    rtk->sol.enu[2] = enu2[2];
    if (sol->stat == SOLQ_FIX)
    {
        sol->fixCnt++;
    }
    else
    {
        sol->floatCnt++;
    }

    trace(
        0x02,
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8."
        "4f%s%8.4f%s%6.2f%s%6.1f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio
    );

    p += sprintf(
        p,
        "%s%s%14.4f%s%14.4f%s%14.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%"
        "s%8.4f%s%6.2f%s%6.1f%s%.4f%s%.4f%s%.4f%s%.0f%s%.0f%s%.0f\n",
        s, sep, enu2[0], sep, enu2[1], sep, enu2[2], sep, sol->stat, sep, ns, sep, SQRT(Q[0]), sep,
        SQRT(Q[4]), sep, SQRT(Q[8]), sep, sqvar(Q[1]), sep, sqvar(Q[5]), sep, sqvar(Q[2]), sep,
        sol->age, sep, sol->ratio, sep, rtk->fftFrq[0], sep, rtk->fftFrq[1], sep, rtk->fftFrq[2],
        sep, rtk->fftPower[0], sep, rtk->fftPower[1], sep, rtk->fftPower[2]
    );

    return p - (char*)buff;
}
/* output solution in the form of nmea GGA sentence --------------------------*/
static int outnmea_gga(unsigned char* buff, const sol_t* sol)
{
    gtime_t time;
    double  h, ep[6], pos[3], dms1[3], dms2[3], dop = sol->dop[1][2];
    int     solq;
    char *  p = (char*)buff, *q, sum;

    if (sol->stat <= SOLQ_NONE)
    {
        p += sprintf(p, "$GNGGA,,,,,,,,,,,,,,");
        for (q = (char*)buff + 1, sum = 0; *q; q++)
        {
            sum ^= *q;
        }
        p += sprintf(p, "*%02X%c%c", sum, 0x0D, 0x0A);
        return p - (char*)buff;
    }
    for (solq = 0; solq < 8; solq++)
    {
        if (solq_nmea[solq] == sol->stat)
        {
            break;
        }
    }
    if (solq >= 8)
    {
        solq = 0;
    }
    time = gpst2utc(sol->time);
    if (time.frac >= 0.995)
    {
        time.time++;
        time.frac = 0.0;
    }
    time2epoch(time, ep);
    ecef2pos(sol->rr, pos);
    h = geoidh(pos);
    deg2dms(fabs(pos[0]) * R2D, dms1, 7);
    deg2dms(fabs(pos[1]) * R2D, dms2, 7);
    p += sprintf(
        p,
        "$GNGGA,%02.0f%02.0f%05.2f,%02.0f%010.7f,%s,%03.0f%010.7f,%s,%d,"
        "%02d,%.1f,%.3f,M,%.3f,M,%.1f,",
        ep[3], ep[4], ep[5], dms1[0], dms1[1] + dms1[2] / 60.0, pos[0] >= 0 ? "N" : "S", dms2[0],
        dms2[1] + dms2[2] / 60.0, pos[1] >= 0 ? "E" : "W", solq, sol->ns[1], dop, pos[2] - h, h,
        sol->age
    );
    printf(
        "$GNGGA,%02.0f%02.0f%05.2f,%02.0f%010.7f,%s,%03.0f%010.7f,%s,%d,%02d,%"
        ".1f,%.3f,M,%.3f,M,%.1f,",
        ep[3], ep[4], ep[5], dms1[0], dms1[1] + dms1[2] / 60.0, pos[0] >= 0 ? "N" : "S", dms2[0],
        dms2[1] + dms2[2] / 60.0, pos[1] >= 0 ? "E" : "W", solq, sol->ns[1], dop, pos[2] - h, h,
        sol->age
    );
    for (q = (char*)buff + 1, sum = 0; *q; q++)
    {
        sum ^= *q; /* check-sum */
    }
    p += sprintf(p, "*%02X%c%c", sum, 0x0D, 0x0A);

    printf("*%02X%c%c", sum, 0x0D, 0x0A);
    return p - (char*)buff;
}

/* solution to velocity covariance -------------------------------------------*/
static void soltocov_vel(const sol_t* sol, double* P)
{
    P[0] = sol->qv[0];        /* xx */
    P[4] = sol->qv[1];        /* yy */
    P[8] = sol->qv[2];        /* zz */
    P[1] = P[3] = sol->qv[3]; /* xy */
    P[5] = P[7] = sol->qv[4]; /* yz */
    P[2] = P[6] = sol->qv[5]; /* zx */
}
/* output solution as the form of lat/lon/height -----------------------------*/
extern int outpos(unsigned char* buff, const char* s, const sol_t* sol, const solopt_t* opt)
{
    double      pos[3], vel[3], dms1[3], dms2[3], P[9], Q[9];
    const char* sep = opt2sep(opt);
    char*       p   = (char*)buff;
    int         i, ns = sol->stat == PMODE_SINGLE ? sol->ns[0] : sol->ns[1];
    ////trace(3, "outpos  :\n");

    ecef2pos(sol->rr, pos);
    soltocov(sol, P);
    covenu(pos, P, Q);
    if (opt->height == 1)
    { /* geodetic height */
        pos[2] -= geoidh(pos);
    }
    if (opt->degf)
    {
        deg2dms(pos[0] * R2D, dms1, 5);
        deg2dms(pos[1] * R2D, dms2, 5);
        p += sprintf(
            p, "%s%s%4.0f%s%02.0f%s%08.5f%s%4.0f%s%02.0f%s%08.5f", s, sep, dms1[0], sep, dms1[1],
            sep, dms1[2], sep, dms2[0], sep, dms2[1], sep, dms2[2]
        );
    }
    else
    {
        p += sprintf(p, "%s%s%14.9f%s%14.9f", s, sep, pos[0] * R2D, sep, pos[1] * R2D);
        printf("%s%s%14.9f%s%14.9f", s, sep, pos[0] * R2D, sep, pos[1] * R2D);
    }
    p += sprintf(
        p,
        "%s%10.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%"
        "6.2f%s%6.1f",
        sep, pos[2], sep, sol->stat, sep, ns, sep, SQRT(Q[4]), sep, SQRT(Q[0]), sep, SQRT(Q[8]),
        sep, sqvar(Q[1]), sep, sqvar(Q[2]), sep, sqvar(Q[5]), sep, sol->age, sep, sol->ratio
    );

    printf(
        "%s%10.4f%s%3d%s%3d%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%8.4f%s%6.2f%"
        "s%6.1f\n",
        sep, pos[2], sep, sol->stat, sep, ns, sep, SQRT(Q[4]), sep, SQRT(Q[0]), sep, SQRT(Q[8]),
        sep, sqvar(Q[1]), sep, sqvar(Q[2]), sep, sqvar(Q[5]), sep, sol->age, sep, sol->ratio
    );

    if (opt->outvel)
    { /* output velocity */
        soltocov_vel(sol, P);
        ecef2enu(pos, sol->rr + 3, vel);
        covenu(pos, P, Q);
        p += sprintf(
            p, "%s%10.5f%s%10.5f%s%10.5f%s%9.5f%s%8.5f%s%8.5f%s%8.5f%s%8.5f%s%8.5f", sep, vel[1],
            sep, vel[0], sep, vel[2], sep, SQRT(Q[4]), sep, SQRT(Q[0]), sep, SQRT(Q[8]), sep,
            sqvar(Q[1]), sep, sqvar(Q[2]), sep, sqvar(Q[5])
        );
    }
    p += sprintf(p, "\n");
    return p - (char*)buff;
}

// output number of satellite and PDOP
static void outSpp(FILE* fp, int rcv, rtk_t* rtk, gtime_t time)
{
    unsigned char buff[4096];
    int           i, week, n;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, rr[3], ep[6];

    time2epoch(time, ep);
    sow = time2gpst(time, &week);

    if (rcv == 1)
    {
        for (i = 0; i < 3; i++)
        {
            rr[i] = rtk->sol.rr[i];
        }
    }
    else
    {
        for (i = 0; i < 3; i++)
        {
            rr[i] = rtk->rb[i];
        }
    }
    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%4d%s%9.2f%s%14.4f%s%14.4f%s%14.4f", (int)ep[0],
        sep, (int)ep[1], sep, (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep,
        week, sep, sow, sep, rr[0], sep, rr[1], sep, rr[2]
    );
    p += sprintf(p, "\n");
    n = p - (char*)buff;
    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}

// output number of satellite and PDOP
static void outPdop(FILE* fp, rtk_t* rtk, gtime_t time, int index)
{
    unsigned char buff[4096];
    int           n, week, nsat;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6];
    int           ns = rtk->sol.stat == PMODE_SINGLE ? rtk->sol.ns[0] : rtk->sol.ns[index];
    time2epoch(time, ep);

    sow = time2gpst(time, &week);

    nsat = rtk->sol.ns[index];
    p += sprintf(
        p,
        "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%4d%s%9.2f%s%3d%s%2d%s%10."
        "3f%s%10.3f%s%10.3f%s%10.3f",
        (int)ep[0], sep, (int)ep[1], sep, (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep,
        (int)ep[5], sep, week, sep, sow, sep, nsat, sep, ns, sep, rtk->sol.dop[index][0], sep,
        rtk->sol.dop[index][1], sep, rtk->sol.dop[index][2], sep,
        rtk->sol.dop[index][3]
    );  // G P H V

    p += sprintf(p, "\n");

    n = p - (char*)buff;

    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}

// output residuals
static void outResi(FILE* fp, rtk_t* rtk, gtime_t time, int frq, int itype)
{
    unsigned char buff[4096];
    int           i, j, n, i0, i1;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6];
    int           week;
    double        resc_pos[NFREQ], resp_pos[NFREQ];

    for (i = 0; i < NFREQ; i++)
    {
        resc_pos[i] = 0.0;
        resp_pos[i] = 0.0;
    }

    time2epoch(time, ep);
    sow = time2gpst(time, &week);

    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, sow, sep
    );

    i0 = 0;
    i1 = MAXPRNGPS + MAXPRNGLO + MAXPRNGAL + NSATQZS + MAXPRNBDS;

    for (i = i0; i < i1; i++)
    {
        if (rtk->ssat[i].vsat[0] == 1)
        {
            for (j = 0; j < NFREQ; j++)
            {
                resc_pos[j] = rtk->ssat[i].resc[j];
                resp_pos[j] = rtk->ssat[i].resc[j];
            }
        }
        else if (rtk->ssat[i].vsat[0] == 0)
        {
            for (j = 0; j < NFREQ; j++)
            {
                resc_pos[j] = 9999.9;
                ;
                resp_pos[j] = 9999.9;
                ;
            }
        }

        if (frq == 1)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[0], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[0], sep);
            }
        }
        else if (frq == 2)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[1], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[1], sep);
            }
        }
        else if (frq == 3)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[2], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[2], sep);
            }
        }
        else if (frq == 4)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[3], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[3], sep);
            }
        }
        else if (frq == 5)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[4], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[4], sep);
            }
        }
        else if (frq == 6)
        {
            if (itype == 0)
            {
                p += sprintf(p, "%10.4f%s", resc_pos[5], sep);
            }
            if (itype == 1)
            {
                p += sprintf(p, "%10.4f%s", resp_pos[5], sep);
            }
        }
    }

    p += sprintf(p, "\n");

    n = p - (char*)buff;

    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}

// output residuals
static void outDdamb(FILE* fp, rtk_t* rtk, gtime_t time, int frq)
{
    unsigned char buff[4096];
    int           i, j, n, i0, i1;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6], ddamb = 9999.9;
    int           week;

    time2epoch(time, ep);
    sow = time2gpst(time, &week);

    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, sow, sep
    );

    i0 = 0;
    i1 = MAXPRNGPS + MAXPRNGLO + MAXPRNGAL + NSATQZS + MAXPRNBDS;

    for (i = i0; i < i1; i++)
    {
        if (rtk->ssat[i].resBias[frq] != 9999.9)
        {
            ddamb = rtk->ssat[i].resBias[frq];
        }
        else
        {
            ddamb = 9999.9;
            ;
        }
        p += sprintf(p, "%9.3f%s", ddamb, sep);
    }

    p += sprintf(p, "\n");

    n = p - (char*)buff;

    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}
// output residuals
static void outResi_spp(FILE* fp, rtk_t* rtk, gtime_t time)
{
    // unsigned char buff[4096];
    // int i, j, n, i0, i1;
    // char *p = (char *)buff;
    // char *sep = " ";
    // double sow, ep[6];
    // int week;
    // double resp_pos[NFREQ];

    // for (i = 0; i<3; i++)
    //     resp_pos[i] = 0.0;

    // time2epoch(time, ep);
    // sow = time2gpst(time, &week);

    // p += sprintf(p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%4d%s%9.2f%s",
    // (int)ep[0], sep, (int)ep[1], sep,
    //     (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep,
    //     week, sep, sow, sep);

    // i0 = 0; i1 = MAXPRNGPS + MAXPRNGLO + MAXPRNGAL + NSATQZS + MAXPRNBDS;

    // for (i = i0; i<i1; i++) {
    //     if (rtk->ssat[i].vs == 1) {
    //         for (j = 0; j<NFREQ; j++) resp_pos[j] = rtk->ssat[i].resp[j];
    //     }
    //     else if (rtk->ssat[i].vs == 0) {
    //         for (j = 0; j<NFREQ; j++) resp_pos[j] = 9999.9;;
    //     }
    //     p += sprintf(p, "%10.4f%s", resp_pos[0], sep);
    // }
    // p += sprintf(p, "\n");
    // n = p - (char *)buff;

    // if (n>0) {
    //     fwrite(buff, n, 1, fp);
    // }
}

// output satellite elevation
static void outElev(FILE* fp, rtk_t* rtk, gtime_t time)
{
    unsigned char buff[4096];
    int           i, n, i0, i1;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6];
    int           week;
    double        elev = 9999.9;
    ;

    time2epoch(time, ep);

    sow = time2gpst(time, &week);

    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, sow, sep
    );

    i0 = 0;
    i1 = MAXPRNGPS + MAXPRNGLO + MAXPRNGAL + NSATQZS + MAXPRNBDS;

    for (i = i0; i < i1; i++)
    {
        if (rtk->ssat[i].vsat[0] == 1)
        {
            elev = rtk->ssat[i].azel[0][1] * R2D;
        }
        else if (rtk->ssat[i].vsat[0] == 0)
        {
            elev = 9999.9;
            ;
        }
        p += sprintf(p, "%9.3f%s", elev, sep);
    }

    p += sprintf(p, "\n");

    n = p - (char*)buff;

    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}

static void Record_gf(FILE* fp, rtk_t* rtk, gtime_t time)
{
    unsigned char buff[4096];
    int           i, n, week;
    char *        p = (char*)buff, *sep = " ";
    double        sow, ep[6], gf;
    int           nf = rtk->opt.ionoopt == IONOOPT_IFLC ? 1 : rtk->opt.nf;
    time2epoch(time, ep);
    sow = time2gpst(time, &week);
    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%4d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, week, sep, sow, sep
    );

    for (i = 0; i < MAXSAT; i++)
    {
        if (nf == 1)
        {
            if (rtk->ssat[i].vsat[0] == 1)
            {
                gf = rtk->ssat[i].gf[0];
            }
            else
            {
                gf = 9999.9;
            };
        }
        else
        {
            if (rtk->ssat[i].vsat[0] == 1 && rtk->ssat[i].vsat[1] == 1)
            {
                gf = rtk->ssat[i].gf[0];
            }
            else
            {
                gf = 9999.9;
            };
        }
        p += sprintf(p, "%10.4f%s", gf, sep);
    }
    p += sprintf(p, "\n");
    n = p - (char*)buff;
    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}

static void Record_Ion(FILE* fp, rtk_t* rtk, gtime_t time)
{
    unsigned char buff[4096];
    int           i, n, week;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6];
    double        dion = 0.0;
    time2epoch(time, ep);
    sow = time2gpst(time, &week);
    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, sow, sep
    );
    int MAXSATGF = MAXSAT;
    for (i = 0; i < MAXSAT; i++)
    {
        if (rtk->ssat[i].vsat[0] == 0)
        {
            dion = 9999.9;
        }
        else
        {
            dion = rtk->ssat[i].dion;
        }
        p += sprintf(p, "%10.4f%s", dion, sep);
    }
    p += sprintf(p, "\n");
    n = p - (char*)buff;
    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}
static void Record_Trop(FILE* fp, rtk_t* rtk, gtime_t time)
{
    unsigned char buff[4096];
    int           i, n, week;
    char*         p   = (char*)buff;
    char*         sep = " ";
    double        sow, ep[6];
    double        ddtrop = 0.0;
    time2epoch(time, ep);
    sow = time2gpst(time, &week);
    p += sprintf(
        p, "%04d%s%02d%s%02d%s%02d%s%02d%s%02d%s%9.2f%s", (int)ep[0], sep, (int)ep[1], sep,
        (int)ep[2], sep, (int)ep[3], sep, (int)ep[4], sep, (int)ep[5], sep, sow, sep
    );

    for (i = 0; i < MAXSAT; i++)
    {
        if (rtk->ssat[i].vsat[0] == 0)
        {
            ddtrop = 9999.9;
        }
        else
        {
            // ddtrop = rtk->ssat[i].ddtrp;
        }
        p += sprintf(p, "%10.4f%s", ddtrop, sep);
    }
    p += sprintf(p, "\n");
    n = p - (char*)buff;
    if (n > 0)
    {
        fwrite(buff, n, 1, fp);
    }
}
// output result files
extern void outResult(rtk_t* rtk, const solopt_t* sopt)
{
    int nf = rtk->opt.ionoopt == IONOOPT_IFLC ? 1 : rtk->opt.nf;
    if (rtk->opt.mode != 5)
    {
        if (trace_flag[2] == 1)
        {
            outSpp(oFile.spppos_r, 1, rtk, rtk->sol.time);
            outSpp(oFile.spppos_b, 2, rtk, rtk->sol.time);
        }

        if (trace_flag[3] == 1)
        {
            outPdop(oFile.pdop1, rtk, rtk->sol.time, 0);
            outPdop(oFile.pdop2, rtk, rtk->sol.time, 1);
        }

        // if(trace_flag[4]==1){
        //     if(nf==1){
        //         Record_obs(oFile.P1,0,0, rtk,rtk->sol.time);
        //         Record_obs(oFile.L1,1,0, rtk,rtk->sol.time);
        //     }
        //     else{
        //         Record_obs(oFile.P1,0,0, rtk,rtk->sol.time);
        //         Record_obs(oFile.L1,1,0, rtk,rtk->sol.time);
        //         Record_obs(oFile.P2,0,2, rtk,rtk->sol.time);
        //         Record_obs(oFile.L2,1,2, rtk,rtk->sol.time);
        //     }
        // }

        if (trace_flag[6] == 1)
        {
            if (nf == 1)
            {
                outResi_spp(oFile.resp, rtk, rtk->sol.time);
                outResi(oFile.resp1, rtk, rtk->sol.time, 1, 1);
                outResi(oFile.resc1, rtk, rtk->sol.time, 1, 0);
            }
            else
            {
                outResi_spp(oFile.resp, rtk, rtk->sol.time);
                outResi(oFile.resc1, rtk, rtk->sol.time, 1, 0);
                outResi(oFile.resp1, rtk, rtk->sol.time, 1, 1);
                outResi(oFile.resc2, rtk, rtk->sol.time, 2, 0);
                outResi(oFile.resp2, rtk, rtk->sol.time, 2, 1);
                outResi(oFile.resc3, rtk, rtk->sol.time, 3, 0);
                outResi(oFile.resp3, rtk, rtk->sol.time, 3, 1);
                outResi(oFile.resc4, rtk, rtk->sol.time, 4, 0);
                outResi(oFile.resp4, rtk, rtk->sol.time, 4, 1);
                outResi(oFile.resc5, rtk, rtk->sol.time, 5, 0);
                outResi(oFile.resp5, rtk, rtk->sol.time, 5, 1);
                outResi(oFile.resc6, rtk, rtk->sol.time, 6, 0);
                outResi(oFile.resp6, rtk, rtk->sol.time, 6, 1);
            }
        }
        if (trace_flag[7] == 1)
        {
            outElev(oFile.elev, rtk, rtk->sol.time);
        }
        if (trace_flag[8] == 1)
        {
            // Record_gf(oFile.gf,rtk,rtk->sol.time);
            // Record_mw(oFile.mw,rtk,rtk->sol.time);
            // Record_lp(oFile.lp,rtk,rtk->sol.time);
        }
        if (trace_flag[9] == 1)
        {
            // if(rtk->opt.nf==1){
            //     Record_Amb(oFile.ambN1,0, rtk,rtk->sol.time);
            // }
            // else{
            // outDdamb(oFile.ambN1, rtk, rtk->sol.time, 0);
            // outDdamb(oFile.ambN2, rtk, rtk->sol.time, 1);
            // outDdamb(oFile.ambN3, rtk, rtk->sol.time, 2);
            // }
            if (rtk->opt.ionoopt == IONOOPT_EST)
            {
                Record_Ion(oFile.ion, rtk, rtk->sol.time);
            }
            // if (rtk->opt.tropopt == TROPOPT_EST)
            //     Record_Trop(oFile.trop, rtk, rtk->sol.time);
        }
    }
    else
    {
        if (trace_flag[2] == 1)
        {
            outSpp(oFile.spppos_r, 1, rtk, rtk->sol.time);
        }
        if (trace_flag[6] == 1)
        {
            outResi_spp(oFile.resp, rtk, rtk->sol.time);
        }
    }
}

extern int outsols(
    unsigned char* buff, rtk_t* rtk, sol_t* sol, const double* rb, const solopt_t* opt
)
{
    gtime_t        time, ts = {0};
    double         gpst;
    int            week, timeu;
    const char*    sep = opt2sep(opt);
    char           s1[64], s2[64];
    unsigned char* p = buff;

    trace(2, "outsols :, rtk->opt.senceopt,%d\n", rtk->opt.senceopt);

    /* suppress output if std is over opt->maxsolstd */
    if (opt->maxsolstd > 0.0 && sol_std(sol) > opt->maxsolstd)
    {
        return 0;
    }
    if (opt->posf == SOLF_NMEA)
    {
        if (opt->nmeaintv[0] < 0.0)
        {
            return 0;
        }
        if (!screent(sol->time, ts, ts, opt->nmeaintv[0]))
        {
            return 0;
        }
    }
    if (sol->stat <= SOLQ_NONE || (opt->posf == SOLF_ENU && norm(rb, 3) <= 0.0))
    {
        return 0;
    }
    timeu = opt->timeu < 0 ? 0 : (opt->timeu > 20 ? 20 : opt->timeu);
    time  = sol->time;
    // trace(4, "opt->times=%d opt->posf=%d\n", opt->times, opt->posf);
    if (opt->times == TIMES_UTC)
    {
        time = gpst2utc(time);
    }
    if (opt->times == TIMES_JST)
    {
        time = timeadd(time, 9 * 3600.0);
    }
    if (opt->times == 3)
    {
        time = gpst2utc(time);
        time.time += 3600 * 8;
    }

    if (opt->timef)
    {
        time2str(time, s1, timeu);
        // time_output(time, s2, timeu);
    }
    else
    {
        gpst = time2gpst(time, &week);
        if (86400 * 7 - gpst < 0.5 / pow(10.0, timeu))
        {
            week++;
            gpst = 0.0;
        }
        sprintf(s2, "%4d%s%*.*f", week, sep, 6 + (timeu <= 0 ? 0 : timeu + 1), timeu, gpst);
    }

    switch (opt->posf)
    {
        case SOLF_LLH:
            p += outpos(p, s1, sol, opt);
            break;
            // case SOLF_XYZ:  p += outecef(p, s1,rtk, sol, opt);   break;
        case SOLF_ENU:
        {
            if (rtk->opt.mode == 3)
            {
                p += outenuSstatic(p, s1, rtk, sol, rb, opt);
                break;
            }
            else
            {
                if (rtk->opt.senceopt == 1)
                {
                    p += outenu2(p, s1, rtk, sol, rb, opt);
                    break;
                }
                else
                {
                    p += outenu_dynamic(p, s1, rtk, sol, rb, opt);
                    break;
                }
            }
        }
        case SOLF_NMEA:
            p += outnmea_gga(p, sol);
            break;
            // case SOLF_ORI: p += outnmea_ori(p, sol, rb); break;
    }
    return p - buff;
}
extern int outsol(FILE* fp, rtk_t* rtk, sol_t* sol, const double* rb, const solopt_t* opt)
{
    unsigned char buff[8191 + 1];
    int           n = 0;

    if ((n = outsols(buff, rtk, sol, rb, opt)) > 0)
    {
        trace(
            4, "timeInterval=%.2f initEnuTime=%.2f\n", rtk->opt.timeInterval, rtk->opt.initEnuTime
        );
        if (rtk->enuWindwoIndex[0] * (double)rtk->opt.timeInterval <=
                (rtk->opt.initEnuTime * 3600.0 - 1) &&
            rtk->enuWindwoIndex[1] * (double)rtk->opt.timeInterval <=
                (rtk->opt.initEnuTime * 3600.0 - 1) &&
            rtk->enuWindwoIndex[2] * (double)rtk->opt.timeInterval <=
                (rtk->opt.initEnuTime * 3600.0 - 1))
        {
            return n;
        }
        else
        {
            ;
        }
        if (fp)
        {
            if (rtk->opt.typeSol == 0 && rtk->opt.mode == 2)
            {
                fwrite(buff, n, 1, fp);
            }
        }
    }
    return n;
}
