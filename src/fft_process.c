//#include <stdio.h>
//#include "types.h"
//#include "fft.h"
//#include "BandPassFilter.h"
//
//void main()
//{
//    S32 i, count = 0, idx;
//    S32 data_in_I = 0, data_in_Q = 0;
//    FLOAT data;
//
//    U8 FFTDectTh = 3;
//    U32 FFTMag, FFTMagMean, FFTMagMax = 0, FFTMagMaxIdx = 0;
//    FLOAT Fs = 1.0, FFTDectFreq = 0;
//
//    compx_12bit  FFTInBuf[FFT_LEN]={0};
//    compx_12bit  FFTOutBufTemp[FFT_LEN]={0};
//    compx_12bit  FFTOutBuf[FFT_LEN]={0};
//
//    BPF BandPassFilter;
//    FLOAT data_out_I = 0, data_out_Q = 0;
//
//    char DataFileName[256] =
//    {
//        "sin.dat"
//    };
//    char ResultFileName[256] =
//    {
//        "result.dat"
//    };
//    char BpfFileName[256] =
//    {
//        "Bpf.dat"
//    };
//
//    FILE *data_file = fopen(DataFileName, "r");
//    FILE *result_file = fopen(ResultFileName, "w");
//    FILE *Bpf_file = fopen(BpfFileName, "w");
//
//    BandPassFilterInit(&BandPassFilter);
//
//    while(1)
//    {
//        if(1 != fscanf(data_file, "%f", &data))
//            break;
//
//        data_in_I = data*10000;// TBD
//
//        //FFT data in
//        for(i = 0; i < FFT_LEN-1; i++)
//        {
//            FFTInBuf[i].real = FFTInBuf[i+1].real;
//            FFTInBuf[i].imag = FFTInBuf[i+1].imag;
//        }
//        FFTInBuf[i].real = data_in_I;
//        FFTInBuf[i].imag = 0;
//        Saturate12bit(FFTInBuf[i].real);
//        Saturate12bit(FFTInBuf[i].imag);
//
//        // FFT process
//        fft_256_12b12b(FFTInBuf, FFTOutBufTemp);
//
//        // FFT reverse & calculate FFT mean
//        FFTMag = 0;
//        for(i = 0; i < FFT_LEN; i++)
//        {
//            idx = bit_reverse(i, FFT_LEN_BITWIDTH);
//            FFTOutBuf[idx].real = FFTOutBufTemp[i].real;
//            FFTOutBuf[idx].imag = FFTOutBufTemp[i].imag;
//            FFTMag += calc_mag(FFTOutBuf[idx]);
//        }
//        FFTMagMean = (FFTMag >> FFT_LEN_BITWIDTH);
//
//        // find max FFT bin, compare with threshold, output frequency
//        for(i = 0; i < FFT_LEN/2; i++)
//        {
//            FFTMag = calc_mag(FFTOutBuf[i]);
//            if(FFTMagMax < FFTMag)
//            {
//                FFTMagMax = FFTMag;
//                FFTMagMaxIdx = i;
//            }
//        }
//        if(FFTMagMax > (FFTMagMean*FFTDectTh))
//        {
//            FFTDectFreq = Fs / FFT_LEN * FFTMagMaxIdx;
//            fprintf(result_file, "FFT %d, FFTMagMean %d, FFTMagMaxIdx %d, FFTDectFreq %f\n", count, FFTMagMean, FFTMagMaxIdx, FFTDectFreq);
//        }
//        else
//        {
//            fprintf(result_file, "FFT %d, FFTMagMean %d, No Freq dect!\n", count, (FFTMag>>8));
//        }
//
//        // FFT result output
//        for(i = 0; i < FFT_LEN; i++)
//        {
//            fprintf(result_file, "I-%d %d, Q-%d %d\n", i, FFTOutBuf[i].real, i, FFTOutBuf[i].imag);
//        }
//
//        count++;
//
//        // BandPassFilter & output
//        BandPassFilterProcess(&BandPassFilter, &data_in_I, &data_in_Q);
//        data_out_I = (data_in_I >> (BPF_COEF_BITWIDTH - 1)) / 10000.0;
//        data_out_Q = (data_in_Q >> (BPF_COEF_BITWIDTH - 1)) / 10000.0;
//        fprintf(Bpf_file, "I-%d %f, Q-%d %f\n", count, data_out_I, count, data_out_Q);
//    }
//
//    fclose(data_file);
//    fclose(result_file);
//    fclose(Bpf_file);
//}
