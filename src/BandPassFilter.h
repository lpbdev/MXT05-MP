#include "types.h"

#define BPF_LEN       24
#define BPF_COEF_BITWIDTH 9


typedef struct _tag_BPF{
    S32 FilterCoef[BPF_LEN/2];
    S32 FilterInI[BPF_LEN];
    S32 FilterInQ[BPF_LEN];
}BPF, *pBPF;

int BandPassFilterInit(pBPF pbpf);
void BandPassFilterProcess(pBPF pbpf, S32 *InputI, S32 *InputQ);