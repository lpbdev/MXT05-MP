#include <stddef.h>
#include "rtk_math.h"

static void swapd(double *a, double *b) {
    double t = *a;
    *a = *b;
    *b = t;
}

/* 在 a[left..right] 中找第 k 小，k 是 0-based 下标 */
static double quickselect(double *a, size_t left, size_t right, size_t k) {
    while (left < right) {
        /* 三数取中，降低最坏情况概率 */
        size_t mid = left + (right - left) / 2;

        if (a[left] > a[mid])   swapd(&a[left], &a[mid]);
        if (a[left] > a[right]) swapd(&a[left], &a[right]);
        if (a[mid] > a[right])  swapd(&a[mid], &a[right]);

        /* 把中位数放到 right 作为 pivot */
        swapd(&a[mid], &a[right]);
        double pivot = a[right];

        size_t i = left;
        for (size_t j = left; j < right; ++j) {
            if (a[j] < pivot) {
                swapd(&a[i], &a[j]);
                ++i;
            }
        }
        swapd(&a[i], &a[right]);

        if (k == i) {
            return a[i];
        } else if (k < i) {
            right = i - 1;
        } else {
            left = i + 1;
        }
    }
    return a[left];
}

/*
 * 返回中位数。
 * 注意：会修改 a 的元素顺序。
 * n == 0 时返回 0.0（调用者应自行判断，或改成一个错误码版本）。
 */
extern double median(double *a, size_t n) {
    if (a == NULL || n == 0) {
        return 0.0;
    }

    size_t k = n / 2;

    if (n & 1) {
        /* 奇数个：第 n/2 小 */
        return quickselect(a, 0, n - 1, k);
    } else {
        /* 偶数个：中间两个的平均 */
        double hi = quickselect(a, 0, n - 1, k);      /* 第 k 小 */
        double lo = quickselect(a, 0, k - 1, k - 1);  /* 第 k-1 小 */
        return 0.5 * lo + 0.5 * hi;
    }
}
