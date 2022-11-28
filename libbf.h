// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *微小任意精度浮点库**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#ifndef LIBBF_H
#define LIBBF_H

#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
#define LIMB_LOG2_BITS 6
#else
#define LIMB_LOG2_BITS 5
#endif

#define LIMB_BITS (1 << LIMB_LOG2_BITS)

#if LIMB_BITS == 64
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;
typedef int64_t slimb_t;
typedef uint64_t limb_t;
typedef uint128_t dlimb_t;
#define EXP_MIN INT64_MIN
#define EXP_MAX INT64_MAX

#else

typedef int32_t slimb_t;
typedef uint32_t limb_t;
typedef uint64_t dlimb_t;
#define EXP_MIN INT32_MIN
#define EXP_MAX INT32_MAX

#endif

/*  以位为单位。 */ 
#define BF_EXP_BITS_MIN 3
#define BF_EXP_BITS_MAX (LIMB_BITS - 2)
#define BF_PREC_MIN 2
#define BF_PREC_MAX (((limb_t)1 << BF_EXP_BITS_MAX) - 2)
#define BF_PREC_INF (BF_PREC_MAX + 1) /*  无限精度。 */ 

#if LIMB_BITS == 64
#define BF_CHKSUM_MOD (UINT64_C(975620677) * UINT64_C(9795002197))
#else
#define BF_CHKSUM_MOD 975620677U
#endif

#define BF_EXP_ZERO EXP_MIN
#define BF_EXP_INF (EXP_MAX - 1)
#define BF_EXP_NAN EXP_MAX

/*  +/-零用EXPN=BF_EXP_ZERO和LEN=0表示，+/-无穷用EXPn=BF_EXP_INF和LEN=0表示，NaN用EXPN=BF_EXP_NaN和len=0表示(忽略符号)。 */ 
typedef struct {
    struct bf_context_t *ctx;
    int sign;
    slimb_t expn;
    limb_t len;
    limb_t *tab;
} bf_t;

typedef enum {
    BF_RNDN, /*  四舍五入到最接近，平局到偶数。 */ 
    BF_RNDZ, /*  四舍五入为零。 */ 
    BF_RNDD, /*  四舍五入-信息。 */ 
    BF_RNDU, /*  四舍五入为+信息。 */ 
    BF_RNDNA, /*  四舍五入到最近，平局远离零。 */ 
    BF_RNDNU, /*  四舍五入到最近，与+inf的关系。 */ 
    BF_RNDF, /*  忠实舍入(非确定性，RNDD或RNDU，始终设置不准确标志)。 */ 
} bf_rnd_t;

/*  允许低于正态数(仅当指数数BITS为&lt;BF_EXP_BITS_MAX和PREC！=BF_PREC_INF)。 */ 
#define BF_FLAG_SUBNORMAL (1 << 3)

#define BF_RND_MASK 0x7
#define BF_EXP_BITS_SHIFT 4
#define BF_EXP_BITS_MASK 0x3f

/*  包含四舍五入模式和指数位数。 */ 
typedef uint32_t bf_flags_t;

typedef void *bf_realloc_func_t(void *opaque, void *ptr, size_t size);

typedef struct {
    bf_t val;
    limb_t prec;
} BFConstCache;

typedef struct bf_context_t {
    void *realloc_opaque;
    bf_realloc_func_t *realloc_func;
    BFConstCache log2_cache;
    BFConstCache pi_cache;
    struct BFNTTState *ntt_state;
} bf_context_t;

static inline int bf_get_exp_bits(bf_flags_t flags)
{
    return BF_EXP_BITS_MAX - ((flags >> BF_EXP_BITS_SHIFT) & BF_EXP_BITS_MASK);
}

static inline bf_flags_t bf_set_exp_bits(int n)
{
    return (BF_EXP_BITS_MAX - n) << BF_EXP_BITS_SHIFT;
}

/*  返回状态。 */ 
#define BF_ST_INVALID_OP  (1 << 0)
#define BF_ST_DIVIDE_ZERO (1 << 1)
#define BF_ST_OVERFLOW    (1 << 2)
#define BF_ST_UNDERFLOW   (1 << 3)
#define BF_ST_INEXACT     (1 << 4)
/*  尚未使用，则表示发生了内存分配错误。非数是返回的。 */ 
#define BF_ST_MEM_ERROR   (1 << 5) 

#define BF_RADIX_MAX 36 /*  Bf_atof()和bf_ftoa()的最大基数。 */ 

static inline slimb_t bf_max(slimb_t a, slimb_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline slimb_t bf_min(slimb_t a, slimb_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

void bf_context_init(bf_context_t *s, bf_realloc_func_t *realloc_func,
                     void *realloc_opaque);
void bf_context_end(bf_context_t *s);
/*  为BF高速缓存数据分配的空闲内存。 */ 
void bf_clear_cache(bf_context_t *s);

static inline void *bf_realloc(bf_context_t *s, void *ptr, size_t size)
{
    return s->realloc_func(s->realloc_opaque, ptr, size);
}

void bf_init(bf_context_t *s, bf_t *r);

static inline void bf_delete(bf_t *r)
{
    bf_context_t *s = r->ctx;
    /*  我们接受删除归零的bf_t结构。 */ 
    if (s) {
        bf_realloc(s, r->tab, 0);
    }
}

static inline void bf_neg(bf_t *r)
{
    r->sign ^= 1;
}

static inline int bf_is_finite(const bf_t *a)
{
    return (a->expn < BF_EXP_INF);
}

static inline int bf_is_nan(const bf_t *a)
{
    return (a->expn == BF_EXP_NAN);
}

static inline int bf_is_zero(const bf_t *a)
{
    return (a->expn == BF_EXP_ZERO);
}

void bf_set_ui(bf_t *r, uint64_t a);
void bf_set_si(bf_t *r, int64_t a);
void bf_set_nan(bf_t *r);
void bf_set_zero(bf_t *r, int is_neg);
void bf_set_inf(bf_t *r, int is_neg);
void bf_set(bf_t *r, const bf_t *a);
void bf_move(bf_t *r, bf_t *a);
int bf_get_float64(const bf_t *a, double *pres, bf_rnd_t rnd_mode);
void bf_set_float64(bf_t *a, double d);

int bf_cmpu(const bf_t *a, const bf_t *b);
int bf_cmp_full(const bf_t *a, const bf_t *b);
int bf_cmp_eq(const bf_t *a, const bf_t *b);
int bf_cmp_le(const bf_t *a, const bf_t *b);
int bf_cmp_lt(const bf_t *a, const bf_t *b);
int bf_add(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec, bf_flags_t flags);
int bf_sub(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec, bf_flags_t flags);
int bf_add_si(bf_t *r, const bf_t *a, int64_t b1, limb_t prec, bf_flags_t flags);
int bf_mul(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec, bf_flags_t flags);
int bf_mul_ui(bf_t *r, const bf_t *a, uint64_t b1, limb_t prec, bf_flags_t flags);
int bf_mul_si(bf_t *r, const bf_t *a, int64_t b1, limb_t prec, 
              bf_flags_t flags);
int bf_mul_2exp(bf_t *r, slimb_t e, limb_t prec, bf_flags_t flags);
int bf_div(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec, bf_flags_t flags);
#define BF_DIVREM_EUCLIDIAN BF_RNDF
int bf_divrem(bf_t *q, bf_t *r, const bf_t *a, const bf_t *b,
              limb_t prec, bf_flags_t flags, int rnd_mode);
int bf_fmod(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
            bf_flags_t flags);
int bf_remainder(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                 bf_flags_t flags);
int bf_remquo(slimb_t *pq, bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
              bf_flags_t flags);
int bf_pow_ui(bf_t *r, const bf_t *a, limb_t b, limb_t prec,
              bf_flags_t flags);
int bf_pow_ui_ui(bf_t *r, limb_t a1, limb_t b, limb_t prec, bf_flags_t flags);
int bf_rint(bf_t *r, limb_t prec, bf_flags_t flags);
int bf_round(bf_t *r, limb_t prec, bf_flags_t flags);
int bf_sqrtrem(bf_t *r, bf_t *rem1, const bf_t *a);
int bf_sqrt(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
slimb_t bf_get_exp_min(const bf_t *a);
void bf_logic_or(bf_t *r, const bf_t *a, const bf_t *b);
void bf_logic_xor(bf_t *r, const bf_t *a, const bf_t *b);
void bf_logic_and(bf_t *r, const bf_t *a, const bf_t *b);

/*  Bf_atof的其他标志。 */ 
/*  如果基数=0或基数=16，则不接受十六进制基数前缀(0x或0x。 */ 
#define BF_ATOF_NO_HEX       (1 << 16)
/*  如果基数=0，则接受二进制(0b或0B)或八进制(0o或0o)基数前缀。 */ 
#define BF_ATOF_BIN_OCT      (1 << 17)
/*  只接受整数(没有小数点，没有指数，没有无穷大，也没有NaN。 */ 
#define BF_ATOF_INT_ONLY     (1 << 18)
/*  不接受符号后的基数前缀。 */ 
#define BF_ATOF_NO_PREFIX_AFTER_SIGN (1 << 19)
/*  不分析NaN和分析区分大小写的“Infinity” */ 
#define BF_ATOF_JS_QUIRKS    (1 << 20)
/*  请勿将整数舍入为指定的精度。 */ 
#define BF_ATOF_INT_PREC_INF (1 << 21)
/*  支持格式正确的数字的传统八进制语法。 */ 
#define BF_ATOF_LEGACY_OCTAL (1 << 22)
/*  ACCEPT_BETWING数字作为数字分隔符。 */ 
#define BF_ATOF_UNDERSCORE_SEP (1 << 23)
/*  如果存在‘n’后缀，则强制进行整数分析(XXX：REMOVE)。 */ 
#define BF_ATOF_INT_N_SUFFIX   (1 << 24)
/*  如果设置为空，则返回NaN(而不是0)。 */ 
#define BF_ATOF_NAN_IF_EMPTY   (1 << 25)
/*  如果基数=0，则只接受十进制浮点。 */ 
#define BF_ATOF_ONLY_DEC_FLOAT (1 << 26)

/*  其他返回标志。 */ 
/*  指示解析的数字是一个整数(仅当使用标志BF_ATOF_INT_PREC_INF或BF_ATOF_INT_N_SUBFIX)。 */ 
#define BF_ATOF_ST_INTEGER   (1 << 5)
/*  解析为传统八进制的整数。 */ 
#define BF_ATOF_ST_LEGACY_OCTAL (1 << 6)

int bf_atof(bf_t *a, const char *str, const char **pnext, int radix,
            limb_t prec, bf_flags_t flags);
/*  此版本接受PREC=BF_PREC_INF并返回基数指数。 */ 
int bf_atof2(bf_t *r, slimb_t *pexponent,
             const char *str, const char **pnext, int radix,
             limb_t prec, bf_flags_t flags);
int bf_mul_pow_radix(bf_t *r, const bf_t *T, limb_t radix,
                     slimb_t expn, limb_t prec, bf_flags_t flags);

#define BF_FTOA_FORMAT_MASK (3 << 16)
/*  固定格式：PREC有效数字四舍五入(标志和BF_RND_MASK)。如果有太多的零，则使用指数记数法需要的。 */ 
#define BF_FTOA_FORMAT_FIXED (0 << 16)
/*  分数格式：小数点后的PREC数字，四舍五入(标志&BF_RND_MASK)。 */ 
#define BF_FTOA_FORMAT_FRAC  (1 << 16)
/*  自由格式：根据需要使用任意多个数字，以便bf_atof()使用精度‘prec’时返回相同的数字，四舍五入为最接近的和次正常+指数配置的‘标志’。这个只有当‘a’已四舍五入为所需内容时，结果才有意义精确度。基数为二的幂。 */ 
#define BF_FTOA_FORMAT_FREE  (2 << 16)
/*  与BF_FTOA_FORMAT_FREE相同，但使用最小位数(需要更多的计算时间)。 */ 
#define BF_FTOA_FORMAT_FREE_MIN (3 << 16)

/*  对固定格式或自由格式强制使用指数表示法。 */ 
#define BF_FTOA_FORCE_EXP    (1 << 20)
/*  为基数16添加0x前缀，为基数8添加0o前缀或为非零值时以2为基数。 */ 
#define BF_FTOA_ADD_PREFIX   (1 << 21)
#define BF_FTOA_JS_QUIRKS    (1 << 22)

size_t bf_ftoa(char **pbuf, const bf_t *a, int radix, limb_t prec,
               bf_flags_t flags);

/*  取模2^n而不是饱和。NaN和Infinity返回0。 */ 
#define BF_GET_INT_MOD (1 << 0) 
int bf_get_int32(int *pres, const bf_t *a, int flags);
int bf_get_int64(int64_t *pres, const bf_t *a, int flags);

/*  导出以下函数仅用于测试。 */ 
void bf_print_str(const char *str, const bf_t *a);
void bf_resize(bf_t *r, limb_t len);
int bf_get_fft_size(int *pdpl, int *pnb_mods, limb_t len);
void bf_recip(bf_t *r, const bf_t *a, limb_t prec);
void bf_rsqrt(bf_t *a, const bf_t *x, limb_t prec);
int bf_normalize_and_round(bf_t *r, limb_t prec1, bf_flags_t flags);
int bf_can_round(const bf_t *a, slimb_t prec, bf_rnd_t rnd_mode, slimb_t k);
slimb_t bf_mul_log2_radix(slimb_t a1, unsigned int radix, int is_inv,
                          int is_ceil1);

/*  超越函数。 */ 
int bf_const_log2(bf_t *T, limb_t prec, bf_flags_t flags);
int bf_const_pi(bf_t *T, limb_t prec, bf_flags_t flags);
int bf_exp(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_log(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
#define BF_POW_JS_QUICKS (1 << 16)
int bf_pow(bf_t *r, const bf_t *x, const bf_t *y, limb_t prec, bf_flags_t flags);
int bf_cos(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_sin(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_tan(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_atan(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_atan2(bf_t *r, const bf_t *y, const bf_t *x,
             limb_t prec, bf_flags_t flags);
int bf_asin(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);
int bf_acos(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags);

#endif /*  LIBBF_H */ 
