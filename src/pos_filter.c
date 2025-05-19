#include<stdio.h>
#include<stdlib.h>
#include <string.h>
#include <math.h>
#include "pos_filter.h"
#ifdef _MSC_VER
#pragma warning( disable : 4996)
#endif

#define POS_FILTER
#ifdef POS_FILTER
static int getDat(FILE* fp, int offset, double *value, int n) {
    if (fp == NULL) {
        printf("fp NULL in %s\n", __func__);
        return 1;
    }
    fseek(fp, sizeof(double) * offset, SEEK_SET);
    fread(value, sizeof(double), n, fp);
    return 0;
}
static int writeDat(FILE* fp, int offset, double *value, int n) {
    if (fp == NULL) {
        printf("fp NULL in %s\n", __func__);
        return 1;
    }
    fseek(fp, sizeof(double) * offset, SEEK_SET);
    fwrite(value, sizeof(double), n, fp);
    return 0;
}

extern int init_data(data_t* poss, char* posdatpath) {

    if (poss->nmax == 0) {
        printf("Failed to init poss_t! \n");
        return 1;
    }

    poss->n = 0;

    if (!(poss->data = (double *)calloc(sizeof(double), poss->nmax * NSIZE))) {
        return 1;
    }

    if (poss->mode == FIL && posdatpath!=NULL) {
        if (!(poss->fp= fopen(posdatpath, "wb+"))) return 1;

        fwrite(poss->data, sizeof(double), poss->nmax * NSIZE, poss->fp);
        free(poss->data);
        poss->data= NULL;
    }
    printf("++++++++++++++++++++++++++++++++++++++\n");
    printf("Pos Filter Status       : ON\n");
    printf("Pos Filter File Path    : %s\n", posdatpath);
    printf("Pos Filter Space Size   : %.2f KB\n", poss->nmax * NSIZE*sizeof(double)/1024.0);
    printf("++++++++++++++++++++++++++++++++++++++\n");
    return 0;
}

extern int free_data(data_t* poss) {

    poss->nmax = 0;
    poss->n = 0;
    if (poss->mode == MEM){
        free(poss->data);
    }else if (poss->mode == FIL){
        fclose(poss->fp);
    }

    return 0;
}

extern int update_data(data_t* poss, double* newdata) {
    int i = 0;
    int index = 0;

    index = poss->n % poss->nmax;

    if (poss->mode == MEM) {
        for (i = 0; i < NSIZE; i++) {
            poss->data[index * NSIZE + i] = newdata[i];
        }
    }else if (poss->mode == FIL) {
        writeDat(poss->fp, index*NSIZE, newdata, 3);
    }

    poss->n++;
    return 0;
}

static int updatewind(wind_t *wind, double *olddata, double *newdata, int n){
    int n1=0, n2=0, i=0;



    /* update pos statics */
    for (i = 0; i < n; i++) {
        n1 = wind->n[i] >= wind->nmax ? wind->nmax : wind->n[i] + 1;
        n2 = wind->n[i] >= wind->nmax ? wind->nmax : wind->n[i];

        wind->ave[i] = (wind->ave[i] * n2 - olddata[i] + newdata[i]) / n1;
        wind->sumX2[i] = wind->sumX2[i] - olddata[i] * olddata[i] + newdata[i] * newdata[i];
        wind->var[i] = 1.0 * wind->sumX2[i] / n1 - wind->ave[i] * wind->ave[i];
        wind->std[i] = sqrt(wind->var[i]);

        wind->n[i]++;

        if (wind->n[i] > wind->nmax * 3) {
            wind->n[i] = wind->n[i] - wind->nmax;
        }
    }

    return 0;
}

extern int update_wind_fp(wind_t* wind, int n, int nmax, FILE *fp ) {
    int i = 0, n1 = 0, n2 = 0;
    double olddata[NSIZE] = { 0 }, newdata[NSIZE] = { 0 };

    int indold = 0;
    int indnew = 0;

    indold = n - wind->nmax - wind->dely ;
    if (indold < 0) indold += nmax;
    indold = indold % nmax;

    indnew = n - wind->dely;
    if (indnew < 0) indnew += nmax;
    indnew = indnew % nmax;

    if (n < wind->dely) return 0;

    ///* update pos statics */
    //if (mode == MEM) {
    //    for (i = 0; i < NSIZE; i++) {
    //        olddata[i] = data->data[indold * NSIZE + i];
    //        newdata[i] = data->data[indnew * NSIZE + i];
    //    }
    //}
    /*else if (data->mode == FIL) {*/
        getDat(fp, indold * NSIZE, olddata, NSIZE);
        getDat(fp, indnew * NSIZE, newdata, NSIZE);
    //}

        for (i = 0; i < NSIZE; i++) {
            if (wind->n[i] < wind->nmax) {
                olddata[i] = 0;
            }
        }

    // for (i = 0; i < NSIZE; i++) {
    //     newdata[i] = newdata[i] - wind->jump[i];
    //     if (n - wind->nmax - wind->dely - 1 > wind->jn[i]) {
    //         olddata[i] = olddata[i] - wind->jump[i];
    //     }
    // }
    // if (indnew = 3814 && indold == 3790)
    //     printf("i,%d, %f, %f, %f\n", i, wind->std[0], wind->std[1], wind->std[2]);
    updatewind(wind, olddata, newdata, NSIZE);

    //if (wind->n == 70 && n1 == 24 && n2 == 24)
    //

    // trace(2, "indnew,%d,%d, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f,%.4f, %.4f, %.4f \n",
    //     indnew, indold,
    //     newdata[0], newdata[1], newdata[2],
    //     olddata[0], olddata[1], olddata[2],
    //     wind->std[0], wind->std[1], wind->std[2]);

    // printf("indold, %d, indnew, %2d, wind->n, %d, newdata=%.5f, olddata=%.5f, wind.ave=%.5f, wind.var=%.5f\n",
    // indold, indnew, wind->n, newdata[0],olddata[0], wind->ave[index], sqrt(wind->var[index]));
    // printf("index %d %d, newdata=%.5f, olddata=%.5f, wind.ave=%.5f, wind.var=%.5f\n",
    //     index, wind->n, newdata[0],olddata[0], wind->ave[index], sqrt(wind->var[index]));

    return 0;
}

extern double posmaxstd(double std, double maxjump){
    return sqrt(std*std+maxjump*maxjump/4.0)*0.75;
}

extern int update_wind(wind_t* wind, data_t* data) {
    int i = 0, n1 = 0, n2 = 0;
    double olddata[NSIZE] = { 0 }, newdata[NSIZE]={0};

    int indold = 0;
    int indnew = 0;

    indold = data->n - wind->nmax -wind->dely-1 ;
    if (indold < 0 ) indold += data->nmax;
    indold = indold % data->nmax;

    indnew = data->n - wind->dely -1;
    if (indnew<0) indnew += data->nmax;
    indnew = indnew % data->nmax;

    if (data->n <= wind->dely) return 0;

    /* update pos statics */
    if (data->mode == MEM) {
        for (i = 0; i < NSIZE; i++) {
            olddata[i] = data->data[indold * NSIZE + i];
            newdata[i] = data->data[indnew * NSIZE + i];
        }
    } else if (data->mode == FIL) {
        getDat(data->fp, indold*NSIZE, olddata, NSIZE);
        getDat(data->fp, indnew*NSIZE, newdata, NSIZE);
    }
    for (i = 0; i < NSIZE; i++) {
        if (wind->n[i] < wind->nmax) {
            olddata[i] = 0;
        }
    }

    // for (i = 0; i < NSIZE; i++) {
    //     newdata[i] = newdata[i] - wind->jump[i];
    //     if (data->n - wind->nmax - wind->dely - 1 > wind->jn[i]) {
    //         olddata[i] = olddata[i] - wind->jump[i];
    //     }
    // }
    updatewind(wind, olddata, newdata, NSIZE);

    // printf("indold, %d, indnew, %2d, wind->n, %d, newdata=%.5f, olddata=%.5f, wind.ave=%.5f, wind.var=%.5f\n",
    // indold, indnew, wind->n, newdata[0],olddata[0], wind->ave[index], sqrt(wind->var[index]));
    // printf("index %d %d, newdata=%.5f, olddata=%.5f, wind.ave=%.5f, wind.var=%.5f\n",
    //     index, wind->n, newdata[0],olddata[0], wind->ave[index], sqrt(wind->var[index]));

    return 0;
}

static int getindex(wind_t* wind, data_t* data, int offset){
    int indend = 0;
    int indnew =0;

    indend = data->n >= wind->nmax ? data->n - wind->nmax -offset : data->n + data->nmax - wind->nmax-offset;

    if (indend < 0 ) indend += data->nmax;
    indend = indend % data->nmax;

    indnew = indend + wind->nmax ;
    indnew = indnew % data->nmax;

    // if(wind->n >=0)
    //     printf("data->n , %d,wind->n, %d, indend, %d, indnew, %d\n", data->n,  wind->n, indend,indnew);
    return 0;
}
#if 0
static int getWindP(wind_t wind, int index, double *pos){
    int n=0;

    n = wind.n >= wind.nmax ? wind.nmax : wind.n;
    if(wind.std[index]==0) return 0;
    pos[0] += n/wind.std[index];
    pos[1] += n/wind.std[index]*wind.ave[index];
    return 0;
}
#endif

extern int calcjump(wind_t *wa, wind_t *wb, double *jump, double *tmpjump, int nj){
    int i=0;

    if(wa->n[0]<wa->nmax || wb->n[0] < wb->nmax) {
        printf("window not full\n");
        return 1;
    }

    for(i=0;i<nj;i++){
        if(wa->std[i]>wa->thres[i]){
            wa->jumpflag[i]=1;
        }

        if(wa->jumpflag[i]==1){
            if(fabs(tmpjump[i])< fabs(wa->ave[i]-wb->ave[i])){
                tmpjump[i]= wa->ave[i]-wb->ave[i];
            }
        }

        if(wa->jumpflag[i]==1 && wa->std[i]<wb->std[i]){
            jump[i] +=tmpjump[i];
            tmpjump[i]=0.0;
            wa->jumpflag[i]=0;
        }
        // if(offset ==1 && offset2 ==1){
        //     jumpdist = wind2m.ave[index] - wind1hd.ave[index];
        //     jn= poss.n;
        // }else{
        //     if(jn!=0){
        //         if(wind12.n - wind12.jn[index] > wind12.nmax ){
        //             wind12.ave[index] = wind12.ave[index] +wind12.jump[index];
        //             wind12.jump[index]=0.0;
        //             wind12.jn[index]=0;
        //         }
        //         wind12.jn[index]=jn;
        //         wind12.jump[index] += jumpdist;

        //         printf("%s jumpdist=%f \n", hms, jumpdist);
        //         jn=0;
        //         jumpdist=0.0;
        //     }
        // }
        // realpos=wind12.ave[index]+wind12.jump[index] ;

    }

    return 0;
}
#if 0
extern int readpos(char* pospath, int intv, int index) {

    char cmd[64];
    char *p = cmd;

    p += sprintf(p, "%s\n","hello");
    int kk = p-(char*)cmd;

    printf("len=%d, kk=%d, %s===", strlen(cmd), kk, cmd);
    FILE* posfp;

    char* tok;
    char buff[512];
    char hms[64];
    int i = 0;
    double pos[3] = { 0.0 };

    FILE* outfp;
    char outbuff[512] = "";

    char outpath[64];
    sprintf(outpath, "%s.pos", pospath);



    if (!(posfp = fopen(pospath, "r"))) return 1;
    if (!(outfp = fopen(outpath, "w"))) return 1;

    int poslen =(int) 12*3600 / intv;

    data_t poss;

    poss.mode = FIL;
    sprintf(outpath, "%s.dat", pospath);
    poss.nmax = poslen+1;
    init_data(&poss,outpath);

    wind_t wind2m = {0};
    wind2m.nmax = (int)2*60/intv;
    wind2m.dely = 0;
    wind2m.jump[0] = 0.0;

    int dely = (int)2*60/intv;

    wind_t wind5m = {0};
    wind5m.nmax = (int)5*60/intv;
    wind5m.dely = 0;


    wind_t wind5md = {0};
    wind5md.nmax = (int)5*60/intv;
    wind5md.dely = dely;

    wind_t wind1h = {0};
    wind1h.nmax = (int)1*3600/intv;
    wind1h.dely = 0;

    wind_t wind1hd = {0};
    wind1hd.nmax = (int)1*3600/intv;
    wind1hd.dely = dely;
    wind1hd.jump[0] = 0.0;
    wind1hd.jump[1] = 0.0;


    wind_t wind12 = {0};
    wind12.nmax = (int)12*3600/intv;
    wind12.dely = 0;


    wind_t wind12d = {0};
    wind12d.nmax = (int)12*3600/intv;
    wind12d.dely = dely;
    wind12d.jump[0] = 0.0;
    wind12d.jump[1] = 0.0;


    double offset;
    double offset2=0;

    double realpos=0.0;
    double jumpdist = 0;
    int jn=0;

    while (fgets(buff, 512, posfp)) {
        if (strncmp(buff, "%%", 1)) {
            //outresult,2022 8 17 0 0 15,     15,  1,G03,50.49,33.15,0.3836,0.01,-240
            //outresult,       2022 8 17 0 0 9,9, G03,1,50.52,33.18,0.1571,0.0052,-242
            //outresult,2024/05/28 06:56:00.00, 5, 0, G05, 257.16,  47.12, 0.0070, -0.0049
            //printf("%s\n", buff);
            tok = strtok(buff, ",");
            snprintf(hms, 20, buff);
            for (i = 0; i < 9; i++) {
                tok = strtok(NULL, ",");
                switch (i) {

                case 0: {
                    sscanf(tok, "%lf ", pos);
                    break;
                }
                case 1: {
                    sscanf(tok, "%lf ", pos + 1);
                    break;
                }
                case 2: {
                    sscanf(tok, "%lf ", pos + 2);
                    break;
                }
                default:
                    continue;
                }
            }
            update_data(&poss, pos);

            fprintf(outfp, "%s,", hms);

            update_wind(&wind2m, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
            //     wind2m.ave[index], sqrt(wind2m.var[index]) * 1000, wind2m.ave[2]);

            update_wind(&wind5m, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
            //         wind5m.ave[index], sqrt(wind5m.var[index]) * 1000, wind5m.ave[2]);

            update_wind(&wind5md, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
                     // wind5md.ave[index], sqrt(wind5md.var[index]) * 1000, wind5md.ave[2]);


            update_wind(&wind1h, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
                    // wind1h.ave[index], sqrt(wind1h.var[index]) * 1000, wind1h.ave[2]);

            update_wind(&wind1hd, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
                    // wind1hd.ave[index], sqrt(wind1hd.var[index]) * 1000, wind1hd.ave[2]);

            update_wind(&wind12, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
                    // wind12.ave[index], sqrt(wind12.var[index]) * 1000, wind12.jump[index]);

            update_wind(&wind12d, &poss);
            // fprintf(outfp, "%14.4f, %14.4f, %14.4f,",
            //         wind12d.ave[index], sqrt(wind12d.var[index]) * 1000, wind12d.ave[2]);


            offset = 0;
            offset2 = 0;
            if (  (wind2m.std[index] - wind1hd.std[index] >2)  &&fabs(wind2m.ave[index] - wind1hd.ave[index]) > 0.003)    {
                offset=1;
            }

            if (fabs(wind2m.ave[index] - wind5md.ave[index]) > 0.003   &&fabs(wind2m.ave[index] - wind1hd.ave[index]) > 0.003 ) {
                offset2=1;
            }


            if(offset ==1 && offset2 ==1){
                jumpdist = wind2m.ave[index] - wind1hd.ave[index];
                jn= poss.n;
            }else{
                if(jn!=0){
                    if(wind12.n - wind12.jn[index] > wind12.nmax ){
                        wind12.ave[index] = wind12.ave[index] +wind12.jump[index];
                        wind12.jump[index]=0.0;
                        wind12.jn[index]=0;
                    }
                    wind12.jn[index]=jn;
                    wind12.jump[index] += jumpdist;

                    printf("%s jumpdist=%f \n", hms, jumpdist);
                    jn=0;
                    jumpdist=0.0;
                }
            }
            realpos=wind12.ave[index]+wind12.jump[index] ;


            // fprintf(outfp, "%14.4f,", offset2);
            // fprintf(outfp, "%14.4f,", offset);
            fprintf(outfp, "%14.4f,", realpos);
            fprintf(outfp, "\n");


        }
    }


    fclose(posfp);
    fclose(outfp);
    return 0;
}

extern int test_update_data() {
    data_t pos;
    wind_t wind05 = { 0 };
    wind_t wind10;

    int i = 0;
    int imax = 35;

    pos.nmax = 10;
    init_data(&pos,NULL);

    wind05.nmax = 7;
    wind05.dely=2;
    wind05.n=0;


    double newdata[3] = { 0.0 };
    int j=0;

    for (i = 0; i < imax; i++) {
        newdata[0] = i + 1;
        newdata[1] = i + 1;
        newdata[2] = i + 1;

        // printf("i,%d, wind05, %d, %.2f, %.2f\n", i,wind05.n, wind05.ave[index], wind05.var[index]);

        update_data(&pos, newdata);
        printf("data_t: i=%d, ",i);
        for (j = 0; j < pos.nmax; j++)
        {
            printf("%4.2f, ", pos.data[0 + j * 3]);
        }
        printf("\n");
        update_wind(&wind05, &pos);
    }

    return 0;
}

int main(){
    // test_update_data();
    readpos("./02_bak/rtk.pos",5,0);
}
#endif


#else

extern int init_data(data_t* poss, char* posdatpath) { return 0; }
extern int free_data(data_t* poss) { return 0; }
extern int update_data(data_t* poss, double* newdata) { return 0; }
extern int update_wind(wind_t* wind, data_t* data) { return 0; }
extern int update_wind_fp(wind_t* wind, int n, int nmax, FILE* fp) { return 0; }
extern double posmaxstd(double std, double maxjump) { return 0.0; }
extern int calcjump(wind_t* wa, wind_t* wb, double* jump, double* tmpjump, int nj) { return 0; }

#endif
