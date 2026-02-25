#include "types.h"
#include "fft.h"
#include "basiccommonops.h"

#define TWIDDLE_FFT_WIDTH 10

static int10 FFTCosTable[FFT_LEN / 2] = {
    256,  256,  256,  255,  255,  254,  253,  252,  251,  250,  248,  247,  245,  243,  241,  239,
    237,  234,  231,  229,  226,  223,  220,  216,  213,  209,  206,  202,  198,  194,  190,  185,
    181,  177,  172,  167,  162,  157,  152,  147,  142,  137,  132,  126,  121,  115,  109,  104,
    98,   92,   86,   80,   74,   68,   62,   56,   50,   44,   38,   31,   25,   19,   13,   6,
    0,    -6,   -13,  -19,  -25,  -31,  -38,  -44,  -50,  -56,  -62,  -68,  -74,  -80,  -86,  -92,
    -98,  -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256
};

static int10 FFTSinTable[FFT_LEN / 2] = {
    0,    -6,   -13,  -19,  -25,  -31,  -38,  -44,  -50,  -56,  -62,  -68,  -74,  -80,  -86,  -92,
    -98,  -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
    -256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
    -237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
    -181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
    -98,  -92,  -86,  -80,  -74,  -68,  -62,  -56,  -50,  -44,  -38,  -31,  -25,  -19,  -13,  -6
};

uint12 calc_mag(compx_12bit x)
{
    uint12 mag;
    uint13 max, min;

    if (x.real < 0)
    {
        x.real *= -1;
    }
    if (x.imag < 0)
    {
        x.imag *= -1;
    }

    // find max and min
    if (x.real >= x.imag)
    {
        max = x.real;
        min = x.imag;
    }
    else
    {
        max = x.imag;
        min = x.real;
    }

    if (max >= (min * 3))
    {
        mag = max + (min >> 3);
    }
    else
    {
        mag = max - (max >> 3) + (min >> 1);
    }
    return mag;
}

int bit_reverse(int in, int BitWidth)
{
    int mask = 0;
    int out  = 0;
    int i    = 0;

    for (i = 0; i < BitWidth; ++i)
    {
        mask = 1 << i;

        if (in & mask)
        {
            out += 1 << (BitWidth - i - 1);
        }
    }
    return out;
}

void fft_256_12b12b(compx_12bit InPara[], compx_12bit OutPara[])
{
    compx_12bit tmp;
    compx_10bit w;
    int11       tempI, tempQ;
    int         i;
    int         N = FFT_LEN;  // the length of FFT
    int         M = 0;        // the number of butterfly states.  M = log2(N)
    int         m = 0;
    int         t = 0;
    int         la, lb, lc, ld, lu, l, r, n;

    // copy data from InPara to OutPara
    for (i = 0; i < N; i++)
    {
        OutPara[i].real = (int)InPara[i].real;
        OutPara[i].imag = (int)InPara[i].imag;
    }

    // calculate  M =  Log2(N). For example, if N=512, then M=9
    for (M = 1, t = N; (t = t / 2) != 1; M++)
        ;

    // Loop:  for each butterfly stage
    for (m = 1; m <= M; m++)
    {
        // la: The distance between two inputs of butterfly.
        la = 1 << (M - m);

        // the number of different twiddle factor for this level of butterfly.
        lb = 1 << (m - 1);  // power(2,m-1);

        // the number of butterfly with the same twiddle factor
        ld = 1 << (M - m);  // power(2,M-m);

        // Shift right by 1 bit in order to ensure:  max{|Xm(i)|} < 0.5
        for (i = 0; i < N; ++i)
        {
            // not shift right,but overflow saturation
            if (m == 5)
            {
                Saturate11bit(OutPara[i].real);
                Saturate11bit(OutPara[i].imag);
            }
            else
            {
                //
                OutPara[i].real = CONVERGENT_ROUND_SHIFT(OutPara[i].real, 1);
                OutPara[i].imag = CONVERGENT_ROUND_SHIFT(OutPara[i].imag, 1);
            }
        }

        // Loop: for each twiddle factor
        for (l = 1; l <= lb; l++)
        {
            r = (l - 1);

            if (m != 1)
            {
                r = bit_reverse(r, m - 1);
            }

            r *= 1 << (M - m);

            // Loop: for each butterfly with the same twiddle factor
            for (n = 0; n < ld; n++)
            {
                // lu: index of upper-poisitioned input of butterfly.
                // lc: index of lower-positioned  input of butterfly
                lu = n + (l - 1) * (1 << (M - m + 1));
                lc = lu + la;

                // w.real:10.x bit
                w.real = FFTCosTable[r];
                w.imag = FFTSinTable[r];

                // tmp.real:10bit
                tmp.real = CONVERGENT_ROUND_SHIFT(
                    (OutPara[lc].real * w.real - OutPara[lc].imag * w.imag), (TWIDDLE_FFT_WIDTH - 2)
                );
                tmp.imag = CONVERGENT_ROUND_SHIFT(
                    (OutPara[lc].real * w.imag + OutPara[lc].imag * w.real), (TWIDDLE_FFT_WIDTH - 2)
                );

                tempI = OutPara[lu].real - tmp.real;
                tempQ = OutPara[lu].imag - tmp.imag;
                Saturate12bit(tempI);
                Saturate12bit(tempQ);
                OutPara[lc].real = tempI;
                OutPara[lc].imag = tempQ;

                tempI = OutPara[lu].real + tmp.real;
                tempQ = OutPara[lu].imag + tmp.imag;
                Saturate12bit(tempI);
                Saturate12bit(tempQ);
                OutPara[lu].real = tempI;
                OutPara[lu].imag = tempQ;
            }
        }
    }
}
