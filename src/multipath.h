#include "rtk.h"
typedef struct {
    int n;       /* 数据的总数 */
    double azel[2];
    double var; /* std^2 */
    double mean;
} hm_grid;

typedef struct {
    double sod;
    double res;
    //double azel[2];
} resdata_t;


typedef struct {
    int sat;
    int freq;
    resdata_t data;
}res_t;

typedef struct {
    int maxresnum;
    int resnum;
    res_t* res;
}ress_t;

typedef struct {
    int n;
    int nmax;
    resdata_t* data;
} satres_t;
extern int initDat(FILE* fp, int npoint);
extern double getDat(FILE* fp, int offset);
extern int writeDat(FILE* fp, int offset, double value);
extern int mkfpssat(rtk_t* rtk);
