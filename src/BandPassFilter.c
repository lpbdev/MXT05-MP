#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <memory.h>
#endif
#include "BandPassFilter.h"

int BpfCoef[24] = {-5,9,8,-7,-5,-2,-10,20,31,-42,-50,53,53,-50,-42,31,20,-10,-2,-5,-7,8,9,-5};

int BandPassFilterInit(pBPF pbpf)
{
    memcpy(pbpf->FilterCoef, BpfCoef, sizeof(BpfCoef));
    memset(pbpf->FilterInI, 0, sizeof(pbpf->FilterInI));
    memset(pbpf->FilterInQ, 0, sizeof(pbpf->FilterInQ));
}

void BandPassFilterProcess(pBPF pbpf, S32 *InputI, S32 *InputQ)
{
    U32 LpfCnt;

	for(LpfCnt = (BPF_LEN - 1); LpfCnt > 0; LpfCnt--)
	{
		pbpf->FilterInI[LpfCnt] = pbpf->FilterInI[LpfCnt - 1];
		pbpf->FilterInQ[LpfCnt] = pbpf->FilterInQ[LpfCnt - 1];
	}
	pbpf->FilterInI[0] = *InputI;
	pbpf->FilterInQ[0] = *InputQ;

	*InputI = 0;
	*InputQ = 0;
	for(LpfCnt = 0; LpfCnt < (BPF_LEN )/2; LpfCnt++)
	{
		*InputI += (pbpf->FilterInI[LpfCnt] + pbpf->FilterInI[BPF_LEN - (LpfCnt + 1)]) * pbpf->FilterCoef[LpfCnt];
		*InputQ += (pbpf->FilterInQ[LpfCnt] + pbpf->FilterInQ[BPF_LEN - (LpfCnt + 1)]) * pbpf->FilterCoef[LpfCnt];
	}
}
