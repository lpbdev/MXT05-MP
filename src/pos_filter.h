#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif
#include <stdio.h>

#define NSIZE 3  // for pos data

/* two dat save modes */
enum DATMOE
{
    MEM = 0,
    FIL
};
typedef struct
{
    int     nmax;
    int     n;
    int     mode;  // 0:mem, 1:FILE
    double* data;
    FILE*   fp;
} data_t;

typedef struct
{
    int    nmax;
    int    n[NSIZE];
    int    dely;
    double var[NSIZE];
    double std[NSIZE];
    double ave[NSIZE];
    double sumX2[NSIZE];  // \sum(x^2)
    // double jump[NSIZE];
    // int jn[NSIZE];
    double thres[NSIZE];  // max std threshold
    int    jumpflag[NSIZE];
} wind_t;  // data filter windows

extern int    init_data(data_t* poss, char* posdatpath);
extern int    free_data(data_t* poss);
extern int    update_data(data_t* poss, double* newdata);
extern int    update_wind(wind_t* wind, data_t* data);
extern int    update_wind_fp(wind_t* wind, int n, int nmax, FILE* fp);
extern double posmaxstd(double std, double maxjump);
extern int    calcjump(wind_t* wa, wind_t* wb, double* jump, double* tmpjump, int nj);
