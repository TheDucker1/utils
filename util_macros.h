#ifndef UTIL_MACROS_H_
#define UTIL_MACROS_H_

#include<stdint.h>

#ifdef DEBUG
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#else
#define LOG(...)
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define ABS(a) ((a) < 0 ? -(a) : (a))

extern const uint32_t QUICK_MASK[];
extern const uint8_t QUICK_8BIT_REFLECT[];
int __BIT_REFLECT(int x, int n);

#define _BIT_REFLECT(x, n) {x = __BIT_REFLECT(x, n);}
#define BIT_REFLECT(x, n) _BIT_REFLECT(x, n)

#define _BIT(l, n) ((uint32_t)(QUICK_MASK[l] << n))
#define BIT(l, n) _BIT(l, n)
#define _BITx(x, l, n) ((x & BIT(l, n)) >> n)
#define BITx(x, l, n) _BITx(x, l ,n)

#define __revBytes32(x) x = BITx(x, 8, 24) | \
                        (BITx(x, 8, 16) << 8) | \
                        (BITx(x, 8, 8) << 16) | \
                        (BITx(x, 8, 0) << 24)
#define _revBytes32(x) __revBytes32(x)

#define __revBytes16(x) x = (BITx(x, 8, 0) << 8) | \
                            (BITx(x, 8, 8))
#define _revBytes16(x) __revBytes16(x)

#endif /* UTIL_MACROS_H_ */