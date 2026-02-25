/*------------------------------------------------------------------------------
 * lambda.c : integer ambiguity resolution
 *
 *          Copyright (C) 2007-2008 by T.TAKASU, All rights reserved.
 *
 * reference :
 *     [1] P.J.G.Teunissen, The least-square ambiguity decorrelation adjustment:
 *         a method for fast GPS ambiguity estimation, J.Geodesy, Vol.70, 65-82,
 *         1995
 *     [2] X.-W.Chang, X.Yang, T.Zhou, MLAMBDA: A modified LAMBDA method for
 *         integer least-squares estimation, J.Geodesy, Vol.79, 552-565, 2005
 *
 * version : $Revision: 1.1 $ $Date: 2008/07/17 21:48:06 $
 * history : 2007/01/13 1.0 new
 *-----------------------------------------------------------------------------*/
#include "rtk.h"

/* constants/macros ----------------------------------------------------------*/

#define LOOPMAX 50000 /* maximum count of search loop */
#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define SGN(x) ((x) <= 0.0 ? -1.0 : 1.0)
#define SWAP(x, y)   \
    do               \
    {                \
        double tmp_; \
        tmp_ = x;    \
        x    = y;    \
        y    = tmp_; \
    } while (0)
#define LOG_PI 1.14472988584940017 /* log(pi) */
#define SQRT2 1.41421356237309510  /* sqrt(2) */
#define SQRT(x) ((x) <= 0.0 || (x) != (x) ? 0.0 : sqrt(x))
/* LD factorization (Q=L'*diag(D)*L) -----------------------------------------*/
static int LD(rtk_t* rtk, int n, const double* Q, double* L, double* D)
{
    int i, j, k, info = 0;
    // double a, * A = mat(n, n);
    memset(rtk->F, 0, sizeof(double) * NY * NX);
    double a, *A = rtk->F;
    memcpy(A, Q, sizeof(double) * n * n);
    for (i = n - 1; i >= 0; i--)
    {
        if ((D[i] = A[i + i * n]) <= 0.0)
        {
            info = -1;
            break;
        }
        a = sqrt(D[i]);
        for (j = 0; j <= i; j++)
        {
            L[i + j * n] = A[i + j * n] / a;
        }
        for (j = 0; j <= i - 1; j++)
        {
            for (k = 0; k <= j; k++)
            {
                A[j + k * n] -= L[i + k * n] * L[i + j * n];
            }
        }
        for (j = 0; j <= i; j++)
        {
            L[i + j * n] /= L[i + i * n];
        }
    }
    // free(A);
    if (info)
    {
        trace(0 * 10, "LD factorization error\n");
    }
    return info;
}
/* integer gauss transformation ----------------------------------------------*/
static void gauss(int n, double* L, double* Z, int i, int j)
{
    int k, mu;

    if ((mu = (int)ROUND(L[i + j * n])) != 0)
    {
        for (k = i; k < n; k++)
        {
            L[k + n * j] -= (double)mu * L[k + i * n];
        }
        for (k = 0; k < n; k++)
        {
            Z[k + n * j] -= (double)mu * Z[k + i * n];
        }
    }
}
/* permutations --------------------------------------------------------------*/
static void perm(int n, double* L, double* D, int j, double del, double* Z)
{
    int    k;
    double eta, lam, a0, a1;

    eta      = D[j] / del;
    lam      = D[j + 1] * L[j + 1 + j * n] / del;
    D[j]     = eta * D[j + 1];
    D[j + 1] = del;
    for (k = 0; k <= j - 1; k++)
    {
        a0               = L[j + k * n];
        a1               = L[j + 1 + k * n];
        L[j + k * n]     = -L[j + 1 + j * n] * a0 + a1;
        L[j + 1 + k * n] = eta * a0 + lam * a1;
    }
    L[j + 1 + j * n] = lam;
    for (k = j + 2; k < n; k++)
    {
        SWAP(L[k + j * n], L[k + (j + 1) * n]);
    }
    for (k = 0; k < n; k++)
    {
        SWAP(Z[k + j * n], Z[k + (j + 1) * n]);
    }
}
/* lambda reduction (z=Z'*a, Qz=Z'*Q*Z=L'*diag(D)*L) (ref.[1]) ---------------*/
static void reduction(int n, double* L, double* D, double* Z)
{
    int    i, j, k;
    double del;

    j = n - 2;
    k = n - 2;
    while (j >= 0)
    {
        if (j <= k)
        {
            for (i = j + 1; i < n; i++)
            {
                gauss(n, L, Z, i, j);
            }
        }
        del = D[j] + L[j + 1 + j * n] * L[j + 1 + j * n] * D[j + 1];
        if (del + 1E-6 < D[j + 1])
        { /* compared considering numerical error */
            perm(n, L, D, j, del, Z);
            k = j;
            j = n - 2;
        }
        else
        {
            j--;
        }
    }
}
/* modified lambda (mlambda) search (ref. [2]) -------------------------------*/
static int search(
    int n, int m, const double* L, const double* D, const double* zs, double* zn, double* s
)
{
    int     i, j, k, c, nn = 0, imax = 0;
    double  newdist, maxdist = 1E99, y;
    double *S = zeros(n, n), *dist = mat(n, 1), *zb = mat(n, 1), *z = mat(n, 1), *step = mat(n, 1);

    k       = n - 1;
    dist[k] = 0.0;
    zb[k]   = zs[k];
    z[k]    = ROUND(zb[k]);
    y       = zb[k] - z[k];
    step[k] = SGN(y);
    for (c = 0; c < LOOPMAX; c++)
    {
        newdist = dist[k] + y * y / D[k];
        if (newdist < maxdist)
        {
            if (k != 0)
            {
                dist[--k] = newdist;
                for (i = 0; i <= k; i++)
                {
                    S[k + i * n] = S[k + 1 + i * n] + (z[k + 1] - zb[k + 1]) * L[k + 1 + i * n];
                }
                zb[k]   = zs[k] + S[k + k * n];
                z[k]    = ROUND(zb[k]);
                y       = zb[k] - z[k];
                step[k] = SGN(y);
            }
            else
            {
                if (nn < m)
                {
                    if (nn == 0 || newdist > s[imax])
                    {
                        imax = nn;
                    }
                    for (i = 0; i < n; i++)
                    {
                        zn[i + nn * n] = z[i];
                    }
                    s[nn++] = newdist;
                }
                else
                {
                    if (newdist < s[imax])
                    {
                        for (i = 0; i < n; i++)
                        {
                            zn[i + imax * n] = z[i];
                        }
                        s[imax] = newdist;
                        for (i = imax = 0; i < m; i++)
                        {
                            if (s[imax] < s[i])
                            {
                                imax = i;
                            }
                        }
                    }
                    maxdist = s[imax];
                }
                z[0] += step[0];
                y       = zb[0] - z[0];
                step[0] = -step[0] - SGN(step[0]);
            }
        }
        else
        {
            if (k == n - 1)
            {
                break;
            }
            else
            {
                k++;
                z[k] += step[k];
                y       = zb[k] - z[k];
                step[k] = -step[k] - SGN(step[k]);
            }
        }
    }
    for (i = 0; i < m - 1; i++)
    { /* sort by s */
        for (j = i + 1; j < m; j++)
        {
            if (s[i] < s[j])
            {
                continue;
            }
            SWAP(s[i], s[j]);
            for (k = 0; k < n; k++)
            {
                SWAP(zn[k + i * n], zn[k + j * n]);
            }
        }
    }
    free(S);
    free(dist);
    free(zb);
    free(z);
    free(step);

    if (c >= LOOPMAX)
    {
        trace(0x02, "search loop count overflow\n");
        return -1;
    }
    return 0;
}

/* complementaty error function (ref [1] p.227-229) --------------------------*/
static double q_gamma(double a, double x, double log_gamma_a);
static double p_gamma(double a, double x, double log_gamma_a)
{
    double y, w;
    int    i;

    if (x == 0.0)
    {
        return 0.0;
    }
    if (x >= a + 1.0)
    {
        return 1.0 - q_gamma(a, x, log_gamma_a);
    }
    y = w = exp(a * log(x) - x - log_gamma_a) / a;
    for (i = 1; i < 100; i++)
    {
        w *= x / (a + i);
        y += w;
        if (fabs(w) < 1E-15)
        {
            break;
        }
    }
    return y;
}
static double q_gamma(double a, double x, double log_gamma_a)
{
    double y, w, la = 1.0, lb = x + 1.0 - a, lc;
    int    i;

    if (x < a + 1.0)
    {
        return 1.0 - p_gamma(a, x, log_gamma_a);
    }
    w = exp(-x + a * log(x) - log_gamma_a);
    y = w / lb;
    for (i = 2; i < 100; i++)
    {
        lc = ((i - 1 - a) * (lb - la) + (i + x) * lb) / i;
        la = lb;
        lb = lc;
        w *= (i - 1 - a) / i;
        y += w / la / lb;
        if (fabs(w / la / lb) < 1E-15)
        {
            break;
        }
    }
    return y;
}
static double f_erfc(double x)
{
    return x >= 0.0 ? q_gamma(0.5, x * x, LOG_PI / 2.0) : 1.0 + p_gamma(0.5, x * x, LOG_PI / 2.0);
}
/* confidence function of integer ambiguity ----------------------------------*/
static double rtkconf_func(double N, double B, double var)
{
    double x, p = 1.0, sig = sqrt(var);
    int    i;

    x = fabs(B - N);
    for (i = 1; i < 6; i++)
    {
        p -= f_erfc((i - x) / (SQRT2 * sig)) - f_erfc((i + x) / (SQRT2 * sig));
    }
    return p;
}

static double myDet(double* arry, int n)
{
    int    i = 0, j, k, sig = 1;
    double m, tmp, det = 1;
    while (1)
    {
        if (arry[i + i * n] != 0.0)
        {
            for (j = i + 1; j < n; j++)
            {
                m = arry[i + j * n] / arry[i + i * n];
                for (k = i; k < n; k++)
                {
                    arry[k + j * n] = arry[k + j * n] - m * arry[k + i * n];
                }
            }
        }
        else
        {
            for (j = i + 1; j < n; j++)
            {
                if (arry[j + i * n] != 0.0)
                {
                    for (k = 0; k < n; k++)
                    {
                        tmp             = arry[k + j * n];
                        arry[k + j * n] = arry[k + i * n];
                        arry[k + i * n] = tmp;
                    }
                    sig = sig * (-1);
                    break;
                }
            }
            i = i - 1;
        }
        i = i + 1;
        if (i == n)
        {
            break;
        }
    }
    for (i = 0; i < n; i++)
    {
        det = det * arry[i + i * n];
    }
    det = det * sig;
    return pow(det, 1.0 / n);
}
static int ludcmpLambda(double* A, int n, int* indx, double* d)
{
    double big, s, tmp, *vv = mat(n, 1);
    int    i, imax = 0, j, k;

    *d = 1.0;
    for (i = 0; i < n; i++)
    {
        big = 0.0;
        for (j = 0; j < n; j++)
        {
            if ((tmp = fabs(A[i + j * n])) > big)
            {
                big = tmp;
            }
        }
        if (big > 0.0)
        {
            vv[i] = 1.0 / big;
        }
        else
        {
            free(vv);
            return -1;
        }
    }
    for (j = 0; j < n; j++)
    {
        for (i = 0; i < j; i++)
        {
            s = A[i + j * n];
            for (k = 0; k < i; k++)
            {
                s -= A[i + k * n] * A[k + j * n];
            }
            A[i + j * n] = s;
        }
        big = 0.0;
        for (i = j; i < n; i++)
        {
            s = A[i + j * n];
            for (k = 0; k < j; k++)
            {
                s -= A[i + k * n] * A[k + j * n];
            }
            A[i + j * n] = s;
            if ((tmp = vv[i] * fabs(s)) >= big)
            {
                big  = tmp;
                imax = i;
            }
        }
        if (j != imax)
        {
            for (k = 0; k < n; k++)
            {
                tmp             = A[imax + k * n];
                A[imax + k * n] = A[j + k * n];
                A[j + k * n]    = tmp;
            }
            *d       = -(*d);
            vv[imax] = vv[j];
        }
        indx[j] = imax;
        if (A[j + j * n] == 0.0)
        {
            free(vv);
            return -1;
        }
        if (j != n - 1)
        {
            tmp = 1.0 / A[j + j * n];
            for (i = j + 1; i < n; i++)
            {
                A[i + j * n] *= tmp;
            }
        }
    }
    free(vv);
    return 0;
}
/* LU back-substitution ------------------------------------------------------*/
static void lubksbLambda(const double* A, int n, const int* indx, double* b)
{
    double s;
    int    i, ii = -1, ip, j;

    for (i = 0; i < n; i++)
    {
        ip    = indx[i];
        s     = b[ip];
        b[ip] = b[i];
        if (ii >= 0)
        {
            for (j = ii; j < i; j++)
            {
                s -= A[i + j * n] * b[j];
            }
        }
        else if (s)
        {
            ii = i;
        }
        b[i] = s;
    }
    for (i = n - 1; i >= 0; i--)
    {
        s = b[i];
        for (j = i + 1; j < n; j++)
        {
            s -= A[i + j * n] * b[j];
        }
        b[i] = s / A[i + i * n];
    }
}
/* inverse of matrix -----------------------------------------------------------
 * inverse of matrix (A=A^-1)
 * args   : double *A        IO  matrix (n x n)
 *          int    n         I   size of matrix A
 * return : status (0:ok,0>:error)
 *-----------------------------------------------------------------------------*/
extern int matinvLambda(rtk_t* rtk, double* A, int n)
{
    double d, *B;
    int    i, j, *indx;
    memset(rtk->I, 0, sizeof(double) * NX * NX);
    indx = imat(n, 1);
    B    = rtk->I;
    if (!indx)
    {
        return -1;
    }
    matcpy(B, A, n, n);
    if (ludcmpLambda(B, n, indx, &d))
    {
        free(indx);
        return -2;
    }
    for (j = 0; j < n; j++)
    {
        for (i = 0; i < n; i++)
        {
            A[i + j * n] = 0.0;
        }
        A[j + j * n] = 1.0;
        lubksbLambda(B, n, indx, A + j * n);
    }
    free(indx);
    return 0;
}
/* solve linear equation -------------------------------------------------------
 * solve linear equation (X=A\Y or X=A'\Y)
 * args   : char   *tr       I   transpose flag ("N":normal,"T":transpose)
 *          double *A        I   input matrix A (n x n)
 *          double *Y        I   input matrix Y (n x m)
 *          int    n,m       I   size of matrix A,Y
 *          double *X        O   X=A\Y or X=A'\Y (n x m)
 * return : status (0:ok,0>:error)
 * notes  : matirix stored by column-major order (fortran convention)
 *          X can be same as Y
 *-----------------------------------------------------------------------------*/
static int solveLambda(
    rtk_t* rtk, const char* tr, double* A, const double* Y, int n, int m, double* X
)
{
    // double *B=mat(n,n);
    int info;
    if (!A)
    {
        return -1;
    }
    // matcpy(B,A,n,n);
    if (!(info = matinvLambda(rtk, A, n)))
    {
        matmul(tr[0] == 'N' ? "NN" : "TN", n, m, n, 1.0, A, Y, 0.0, X);
    }
    // free(B);
    return info;
}

/* lambda/mlambda integer least-square estimation ------------------------------
 * integer least-square estimation. reduction is performed by lambda (ref.[1]),
 * and search by mlambda (ref.[2]).
 * args   : int    n      I  number of float parameters
 *          int    m      I  number of fixed solutions
 *          double *a     I  float parameters (n x 1)
 *          double *Q     I  covariance matrix of float parameters (n x n)
 *          double *F     O  fixed solutions (n x m)
 *          double *s     O  sum of squared residulas of fixed solutions (1 x m)
 * return : status (0:ok,other:error)
 * notes  : matrix stored by column-major order (fortran convension)
 *-----------------------------------------------------------------------------*/

extern int lambda(
    rtk_t* rtk, int n, int m, const double* a, const double* Q, double* f, double* s, int lcopt
)
{
    int     i, j, k = 0, index = 0, info;
    double *L, *D, *Z, *z, *E, *ZD, *L_D, *QD;
    double *P, detAdop, tmp, thresDet, thresP;
    if (n <= 0 || m <= 0)
    {
        return -1;
    }
    // L = zeros(n, n);
    D = mat(n, 1);
    // Z = eye(n);
    z = mat(n, 1);
    E = mat(n, m);
    P = zeros(SELETE_SAT_NUM * 4, 1);

    // ZD = zeros(n, n);
    // L_D = zeros(n, n);
    // QD = zeros(n, n);

    // DP = rtk->H;
    // Qab = rtk->F;
    memset(rtk->I, 0, sizeof(double) * NX * NX);
    ;
    memset(rtk->H, 0, sizeof(double) * NY * NX);
    // memset(rtk->F, 0, sizeof(double) * NY * NX);
    L = rtk->I;
    Z = rtk->H;
    // E = rtk->F;
    for (i = 0; i < n; i++)
    {
        Z[i + i * n] = 1.0;
    }
    /* LD factorization */
    if (!(info = LD(rtk, n, Q, L, D)))
    {  // F
        /* lambda reduction */

        reduction(n, L, D, Z);
        matmul("TN", n, 1, n, 1.0, Z, a, 0.0, z); /* z=Z'*a */
        /* mlambda search */
        if (!(info = search(n, m, L, D, z, E, s)))
        {
            memcpy(rtk->F, rtk->I, sizeof(double) * NX * NX);
            info = solveLambda(rtk, "T", Z, E, n, m, f); /* F=Z'\E */
            if (info == -1)
            {
                trace(0x02, "solve error,info=%d\n", info);
            }
        }
        else
        {
            trace(0x02, "search loop count overflow,info=%d\n", info);
            free(D);
            free(z);
            free(E);
            free(P);
            return -2;
        }
    }
    else
    {
        trace(0x02, "LD factorization error,info=%d\n", info);
        //*msg+=sprintf(*msg,"*********Q************");
        // for(i=0;i<n*n;i++)
        //{
        //    if(i%n==0)  *msg+=sprintf(*msg,"\n ");
        //    *msg+=sprintf(*msg,"%10.4lf",Q[i]);
        //}
        //*msg+=sprintf(*msg,"\n ");
    }
#if 1
    if (info >= 0)
    {
        memcpy(rtk->I, rtk->F, sizeof(double) * NX * NX);
        memset(rtk->F, 0, sizeof(double) * NY * NX);
        memset(rtk->H, 0, sizeof(double) * NY * NX);
        L_D = rtk->F;
        ZD  = rtk->H;
        for (i = 0; i < n; i++)
        {
            ZD[i + i * n] = D[i];
        }
        matmul("NN", n, n, n, 1.0, L, ZD, 0.0, L_D); /* z=L*D */
        QD = rtk->H;
        memset(rtk->H, 0, sizeof(double) * NY * NX);
        matmul("TN", n, n, n, 1.0, L_D, L, 0.0, QD); /* z=L*D*L' */  // TT NN TN NT
        memset(rtk->F, 0, sizeof(double) * NY * NX);
        memcpy(rtk->F, QD, sizeof(double) * NY * NX);

        detAdop = myDet(rtk->F, n);
        trace(0x02, "ADOP = %.2f\n", detAdop);

        if (lcopt == 0)
        {
            thresDet = 0.0228;
            thresP   = 0.999;
        }
        else if (lcopt == 3)
        {
            thresDet = 0.8;
            thresP   = 0.9;
        }
        else
        {
            thresDet = 1.5;
            thresP   = 0.85;
        }
        if (detAdop > thresDet)
        {
            info = -2;
        }
        if (info >= 0)
        {
            for (i = 0; i < n; i++)
            {
                if (fabs(E[i] - z[i]) < 0.5)
                {
                    P[k] = rtkconf_func(E[i], z[i], QD[i + i * n]);
                    k++;
                }
            }
            for (i = 0; i < k; i++)
            {
                for (j = i + 1; j < k; j++)
                {
                    if (P[i] < P[j])
                    {
                        tmp  = P[i];
                        P[i] = P[j];
                        P[j] = tmp;
                    }
                }
            }
            if (n <= 8 && k > 1)
            {
                index = k - 1;
            }
            else
            {
                index = k / 2;
            }

            trace(0x02, "nb=%2d\tk=%2d\t%lf\t%f\n", n, k, P[index], P[k / 2]);
            if (P[index] < thresP || k < n / 2)
            {
                // if (P < pow(0.9999, k)||k<n) {
                //*msg += sprintf(*msg, "bottstrapping nb=%2d\tk=%2d\tp[%d]=%lf\n", n, k, index,
                // P[index]);
                // printf("bottstrapping failed nb=%2d\tk=%2d\tp=%lf\n", n, k, P[index]);
                info = -3;
            }
        }
    }
#endif
    // free(L);
    free(D);
    free(P);
    // free(Z);
    free(z);
    free(E);
    // free(ZD);
    // free(L_D);
    // free(QD);
    return info;
}
