/* Minimal soft-float double routines for rv64imac (no F/D extensions).
   Uses integer arithmetic to manipulate IEEE 754 double-precision bits. */

#include <stdint.h>

#define W __attribute__((weak))

typedef union { double d; uint64_t u; } du;
#define SIGN    (1ULL << 63)
#define EXP_MASK 0x7FF0000000000000ULL
#define FRAC_MASK 0x000FFFFFFFFFFFFFULL
#define EXP_BIAS 1023
#define EXP_SHIFT 52
#define HIDDEN (1ULL << 52)

static inline int d_exp(uint64_t u) { return (int)((u >> EXP_SHIFT) & 0x7FF); }
static inline uint64_t d_frac(uint64_t u) { return u & FRAC_MASK; }
static inline int d_sign(uint64_t u) { return u >> 63; }

static uint64_t pack(int sign, int exp, uint64_t frac) {
	return ((uint64_t)sign << 63) | ((uint64_t)(exp & 0x7FF) << EXP_SHIFT) | (frac & FRAC_MASK);
}

/* normalize and round a result with extra precision bits */
static uint64_t normalize(int sign, int exp, uint64_t frac, int extra_shift) {
	if (!frac) return pack(sign, 0, 0);
	while (frac && !(frac & (HIDDEN << extra_shift))) { frac <<= 1; exp--; }
	/* round to nearest, ties to even */
	if (extra_shift > 0) {
		uint64_t half = 1ULL << (extra_shift - 1);
		uint64_t mask = (1ULL << extra_shift) - 1;
		uint64_t trail = frac & mask;
		frac >>= extra_shift;
		if (trail > half || (trail == half && (frac & 1)))
			frac++;
		if (frac & (HIDDEN << 1)) { frac >>= 1; exp++; }
	}
	if (exp >= 0x7FF) return pack(sign, 0x7FF, 0); /* inf */
	if (exp <= 0) return pack(sign, 0, 0); /* flush denorms to zero */
	return pack(sign, exp, frac & FRAC_MASK);
}

W double __adddf3(double a, double b) {
	du ua = {a}, ub = {b};
	uint64_t au = ua.u, bu = ub.u;
	int ae = d_exp(au), be = d_exp(bu);
	int as = d_sign(au), bs = d_sign(bu);
	uint64_t af = d_frac(au), bf = d_frac(bu);
	/* handle zeros */
	if (!ae && !af) return b;
	if (!be && !bf) return a;
	/* handle inf/nan */
	if (ae == 0x7FF || be == 0x7FF) {
		du r; r.u = (ae == 0x7FF) ? au : bu; return r.d;
	}
	/* add hidden bit */
	if (ae) af |= HIDDEN; else { ae = 1; }
	if (be) bf |= HIDDEN; else { be = 1; }
	/* align exponents - use 3 extra bits for rounding */
	int extra = 3;
	af <<= extra;
	bf <<= extra;
	int exp = ae;
	if (ae > be) { int shift = ae - be; bf = (shift >= 64) ? 0 : bf >> shift; }
	else if (be > ae) { int shift = be - ae; af = (shift >= 64) ? 0 : af >> shift; exp = be; }
	uint64_t rf;
	int rs;
	if (as == bs) {
		rf = af + bf;
		rs = as;
		if (rf & (HIDDEN << (extra + 1))) { rf >>= 1; exp++; }
	} else {
		if (af >= bf) { rf = af - bf; rs = as; }
		else { rf = bf - af; rs = bs; }
	}
	du r;
	r.u = normalize(rs, exp, rf, extra);
	return r.d;
}

W double __muldf3(double a, double b) {
	du ua = {a}, ub = {b};
	uint64_t au = ua.u, bu = ub.u;
	int ae = d_exp(au), be = d_exp(bu);
	int rs = d_sign(au) ^ d_sign(bu);
	uint64_t af = d_frac(au), bf = d_frac(bu);
	/* zeros */
	if ((!ae && !af) || (!be && !bf)) { du r; r.u = pack(rs, 0, 0); return r.d; }
	/* inf/nan */
	if (ae == 0x7FF || be == 0x7FF) { du r; r.u = pack(rs, 0x7FF, 0); return r.d; }
	if (ae) af |= HIDDEN; else { ae = 1; }
	if (be) bf |= HIDDEN; else { be = 1; }
	int exp = ae + be - EXP_BIAS;
	/* multiply 53-bit mantissas: split into 32-bit halves */
	uint64_t ahi = af >> 21, alo = af & 0x1FFFFF;
	uint64_t bhi = bf >> 21, blo = bf & 0x1FFFFF;
	uint64_t prod = ahi * bhi;
	uint64_t mid = ahi * blo + alo * bhi;
	prod += mid >> 21;
	/* prod has the significant bits; it's about 2*52=104 bits worth shifted */
	/* We need to normalize: prod ~ 2^52 * 2^52 >> 21 = 2^83 range */
	/* Shift to get hidden bit at bit 52+extra */
	int extra = 10;
	/* prod is roughly 62 bits; hidden bit should be at bit 52+extra */
	int target = EXP_SHIFT + extra;
	/* find leading bit */
	uint64_t tmp = prod;
	int lead = 0;
	while (tmp && !(tmp & (1ULL << 62))) { tmp <<= 1; lead++; }
	/* shift so leading 1 is at target */
	int cur = 62 - lead;
	if (cur > target) { prod >>= (cur - target); }
	else { prod <<= (target - cur); exp -= (target - cur); }
	exp += cur - (EXP_SHIFT * 2 - 21); /* adjust for our split multiply shift */
	du r;
	r.u = normalize(rs, exp, prod, extra);
	return r.d;
}

W double __floatunsidf(unsigned int i) {
	if (!i) return 0.0;
	int exp = EXP_BIAS + 52;
	uint64_t frac = (uint64_t)i;
	/* shift frac so bit 52 (hidden) is set */
	while (!(frac & HIDDEN)) { frac <<= 1; exp--; }
	du r;
	r.u = pack(0, exp, frac & FRAC_MASK);
	return r.d;
}

W double __floatundidf(unsigned long long i) {
	if (!i) return 0.0;
	int exp = EXP_BIAS + 52;
	uint64_t frac = i;
	while (frac & ~((HIDDEN << 1) - 1)) { frac >>= 1; exp++; }
	while (!(frac & HIDDEN)) { frac <<= 1; exp--; }
	du r;
	r.u = pack(0, exp, frac & FRAC_MASK);
	return r.d;
}

W double __floatdidf(long long i) {
	if (!i) return 0.0;
	int sign = 0;
	unsigned long long u = i;
	if (i < 0) { sign = 1; u = -i; }
	du r;
	r.d = __floatundidf(u);
	r.u |= (uint64_t)sign << 63;
	return r.d;
}

W double __floatsidf(int i) {
	if (!i) return 0.0;
	int sign = 0;
	unsigned int u = i;
	if (i < 0) { sign = 1; u = -i; }
	du r;
	r.d = __floatunsidf(u);
	r.u |= (uint64_t)sign << 63;
	return r.d;
}

float __truncdfsf2(double a);

W double __subdf3(double a, double b) {
	du ub = {b};
	ub.u ^= SIGN;
	return __adddf3(a, ub.d);
}

/* Returns -1 if a<b, 0 if a==b, 1 if a>b, 2 if unordered (NaN). */
static int dcmp(double a, double b) {
	du ua = {a}, ub = {b};
	uint64_t au = ua.u, bu = ub.u;
	int ae = d_exp(au), be = d_exp(bu);
	if ((ae == 0x7FF && d_frac(au)) || (be == 0x7FF && d_frac(bu)))
		return 2;
	if (((au | bu) << 1) == 0) return 0;	/* +0 == -0 */
	int as = d_sign(au), bs = d_sign(bu);
	if (as != bs) return as ? -1 : 1;
	if (au == bu) return 0;
	if (au > bu) return as ? -1 : 1;
	return as ? 1 : -1;
}

/* IEEE comparison helpers. NaN handling per GCC ABI:
 *   __eq/__ne return non-zero ⇒ "not equal" (NaN ≠ anything).
 *   __lt/__le return >=0 ⇒ false on NaN (so "if (x<y)" is taken false).
 *   __gt/__ge return <=0 ⇒ false on NaN. */
W int __eqdf2(double a, double b) { return dcmp(a, b); }
W int __nedf2(double a, double b) { return dcmp(a, b); }
W int __ltdf2(double a, double b) { int r = dcmp(a, b); return r == 2 ? 1 : r; }
W int __ledf2(double a, double b) { int r = dcmp(a, b); return r == 2 ? 1 : r; }
W int __gtdf2(double a, double b) { int r = dcmp(a, b); return r == 2 ? -1 : r; }
W int __gedf2(double a, double b) { int r = dcmp(a, b); return r == 2 ? -1 : r; }
W int __unorddf2(double a, double b) { return dcmp(a, b) == 2; }

/* Single-precision arithmetic via double-precision (correct, not fastest). */
W double __extendsfdf2(float a) {
	union { float f; uint32_t u; } fa = {a};
	uint32_t u = fa.u;
	int sign = u >> 31;
	int exp = (u >> 23) & 0xFF;
	uint32_t frac = u & 0x7FFFFF;
	du r;
	if (exp == 0xFF) { r.u = pack(sign, 0x7FF, frac ? 1 : 0); return r.d; }
	if (!exp && !frac) { r.u = (uint64_t)sign << 63; return r.d; }
	if (!exp) {
		while (!(frac & 0x800000)) { frac <<= 1; exp--; }
		exp++;
	}
	int dexp = exp - 127 + EXP_BIAS;
	r.u = pack(sign, dexp, ((uint64_t)frac & 0x7FFFFF) << 29);
	return r.d;
}

W float __addsf3(float a, float b) { return __truncdfsf2(__adddf3(__extendsfdf2(a), __extendsfdf2(b))); }
W float __subsf3(float a, float b) { return __truncdfsf2(__subdf3(__extendsfdf2(a), __extendsfdf2(b))); }
W float __mulsf3(float a, float b) { return __truncdfsf2(__muldf3(__extendsfdf2(a), __extendsfdf2(b))); }

W int __eqsf2(float a, float b) { return __eqdf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __nesf2(float a, float b) { return __nedf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __ltsf2(float a, float b) { return __ltdf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __lesf2(float a, float b) { return __ledf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __gtsf2(float a, float b) { return __gtdf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __gesf2(float a, float b) { return __gedf2(__extendsfdf2(a), __extendsfdf2(b)); }
W int __unordsf2(float a, float b) { return __unorddf2(__extendsfdf2(a), __extendsfdf2(b)); }

W float __truncdfsf2(double a) {
	du ua = {a};
	uint64_t u = ua.u;
	int sign = d_sign(u);
	int exp = d_exp(u);
	uint64_t frac = d_frac(u);
	/* special cases */
	if (exp == 0x7FF) {
		uint32_t r = ((uint32_t)sign << 31) | 0x7F800000;
		if (frac) r |= 1; /* nan */
		union { float f; uint32_t u; } fu;
		fu.u = r;
		return fu.f;
	}
	/* convert double exponent to float exponent */
	int fexp = exp - EXP_BIAS + 127;
	/* double has 52 frac bits, float has 23; shift right by 29 with rounding */
	uint32_t ff = (uint32_t)(frac >> 29);
	uint64_t round_bits = frac & 0x1FFFFFFF;
	if (round_bits > 0x10000000 || (round_bits == 0x10000000 && (ff & 1)))
		ff++;
	if (ff & 0x800000) { /* carry into hidden bit */
		if (exp) { ff = 0; fexp++; }
	}
	if (fexp >= 0xFF) {
		union { float f; uint32_t u; } fu;
		fu.u = ((uint32_t)sign << 31) | 0x7F800000;
		return fu.f;
	}
	if (fexp <= 0 || (!exp && !frac)) {
		union { float f; uint32_t u; } fu;
		fu.u = (uint32_t)sign << 31;
		return fu.f;
	}
	union { float f; uint32_t u; } fu;
	fu.u = ((uint32_t)sign << 31) | ((uint32_t)fexp << 23) | (ff & 0x7FFFFF);
	return fu.f;
}
