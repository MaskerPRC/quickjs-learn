// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *微小任意精度浮点库**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#include "cutils.h"
#include "libbf.h"

/*  使其能够检查乘法结果。 */ 
//  #定义USE_MUL_CHECK。
/*  使其能够使用FFT/NTT乘法。 */ 
#define USE_FFT_MUL

//  #定义内联__属性__((Always_Inline))。

#ifdef __AVX2__
#define FFT_MUL_THRESHOLD 100 /*  在四肢中的最小因素。 */ 
#else
#define FFT_MUL_THRESHOLD 100 /*  在四肢中的最小因素。 */ 
#endif

/*  XXX：调整。 */ 
#define BASECASE_DIV_THRESHOLD_B 300
#define BASECASE_DIV_THRESHOLD_Q 300

#if LIMB_BITS == 64
#define FMT_LIMB1 "%" PRIx64 
#define FMT_LIMB "%016" PRIx64 
#define PRId_LIMB PRId64
#define PRIu_LIMB PRIu64

#else

#define FMT_LIMB1 "%x"
#define FMT_LIMB "%08x"
#define PRId_LIMB "d"
#define PRIu_LIMB "u"

#endif

typedef int bf_op2_func_t(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                          bf_flags_t flags);

#ifdef USE_FFT_MUL

#define FFT_MUL_R_OVERLAP_A (1 << 0)
#define FFT_MUL_R_OVERLAP_B (1 << 1)

static no_inline void fft_mul(bf_t *res, limb_t *a_tab, limb_t a_len,
                              limb_t *b_tab, limb_t b_len, int mul_flags);
static void fft_clear_cache(bf_context_t *s);
#endif

/*  可以前导零吗。 */ 
static inline int clz(limb_t a)
{
    if (a == 0) {
        return LIMB_BITS;
    } else {
#if LIMB_BITS == 64
        return clz64(a);
#else
        return clz32(a);
#endif
    }
}

static inline int ctz(limb_t a)
{
    if (a == 0) {
        return LIMB_BITS;
    } else {
#if LIMB_BITS == 64
        return ctz64(a);
#else
        return ctz32(a);
#endif
    }
}

static inline int ceil_log2(limb_t a)
{
    if (a <= 1)
        return 0;
    else
        return LIMB_BITS - clz(a - 1);
}

#if 0
static inline slimb_t ceil_div(slimb_t a, limb_t b)
{
    if (a >= 0)
        return (a + b - 1) / b;
    else
        return a / (slimb_t)b;
}

static inline slimb_t floor_div(slimb_t a, limb_t b)
{
    if (a >= 0) {
        return a / b;
    } else {
        return (a - b + 1) / (slimb_t)b;
    }
}
#endif

#define malloc(s) malloc_is_forbidden(s)
#define free(p) free_is_forbidden(p)
#define realloc(p, s) realloc_is_forbidden(p, s)

void bf_context_init(bf_context_t *s, bf_realloc_func_t *realloc_func,
                     void *realloc_opaque)
{
    memset(s, 0, sizeof(*s));
    s->realloc_func = realloc_func;
    s->realloc_opaque = realloc_opaque;
}

void bf_context_end(bf_context_t *s)
{
    bf_clear_cache(s);
}

/*  “Size”必须大于0。 */ 
static void *bf_malloc(bf_context_t *s, size_t size)
{
    return bf_realloc(s, NULL, size);
}

static void bf_free(bf_context_t *s, void *ptr)
{
    bf_realloc(s, ptr, 0);
}

void bf_init(bf_context_t *s, bf_t *r)
{
    r->ctx = s;
    r->sign = 0;
    r->expn = BF_EXP_ZERO;
    r->len = 0;
    r->tab = NULL;
}

void bf_resize(bf_t *r, limb_t len)
{
    if (len != r->len) {
        r->tab = bf_realloc(r->ctx, r->tab, len * sizeof(limb_t));
        r->len = len;
    }
}

void bf_set_ui(bf_t *r, uint64_t a)
{
    r->sign = 0;
    if (a == 0) {
        r->expn = BF_EXP_ZERO;
        bf_resize(r, 0);
    } 
#if LIMB_BITS == 32
    else if (a <= 0xffffffff)
#else
    else
#endif
    {
        int shift;
        bf_resize(r, 1);
        shift = clz(a);
        r->tab[0] = a << shift;
        r->expn = LIMB_BITS - shift;
    }
#if LIMB_BITS == 32
    else {
        uint32_t a1, a0;
        int shift;
        bf_resize(r, 2);
        a0 = a;
        a1 = a >> 32;
        shift = clz(a1);
        r->tab[0] = a0 << shift;
        r->tab[1] = (a1 << shift) | (a0 >> (LIMB_BITS - shift));
        r->expn = 2 * LIMB_BITS - shift;
    }
#endif
}

void bf_set_si(bf_t *r, int64_t a)
{
    if (a < 0) {
        bf_set_ui(r, -a);
        r->sign = 1;
    } else {
        bf_set_ui(r, a);
    }
}

void bf_set_nan(bf_t *r)
{
    bf_resize(r, 0);
    r->expn = BF_EXP_NAN;
    r->sign = 0;
}

void bf_set_zero(bf_t *r, int is_neg)
{
    bf_resize(r, 0);
    r->expn = BF_EXP_ZERO;
    r->sign = is_neg;
}

void bf_set_inf(bf_t *r, int is_neg)
{
    bf_resize(r, 0);
    r->expn = BF_EXP_INF;
    r->sign = is_neg;
}

void bf_set(bf_t *r, const bf_t *a)
{
    if (r == a)
        return;
    r->sign = a->sign;
    r->expn = a->expn;
    bf_resize(r, a->len);
    memcpy(r->tab, a->tab, a->len * sizeof(limb_t));
}

/*  等价于bf_set(r，a)；bf_Delete(A)。 */  
void bf_move(bf_t *r, bf_t *a)
{
    bf_context_t *s = r->ctx;
    if (r == a)
        return;
    bf_free(s, r->tab);
    *r = *a;
}

static limb_t get_limbz(const bf_t *a, limb_t idx)
{
    if (idx >= a->len)
        return 0;
    else
        return a->tab[idx];
}

/*  在选项卡中获取位位置‘pos’处的Limb_Bits。 */ 
static inline limb_t get_bits(const limb_t *tab, limb_t len, slimb_t pos)
{
    limb_t i, a0, a1;
    int p;

    i = pos >> LIMB_LOG2_BITS;
    p = pos & (LIMB_BITS - 1);
    if (i < len)
        a0 = tab[i];
    else
        a0 = 0;
    if (p == 0) {
        return a0;
    } else {
        i++;
        if (i < len)
            a1 = tab[i];
        else
            a1 = 0;
        return (a0 >> p) | (a1 << (LIMB_BITS - p));
    }
}

static inline limb_t get_bit(const limb_t *tab, limb_t len, slimb_t pos)
{
    slimb_t i;
    i = pos >> LIMB_LOG2_BITS;
    if (i < 0 || i >= len)
        return 0;
    return (tab[i] >> (pos & (LIMB_BITS - 1))) & 1;
}

static inline limb_t limb_mask(int start, int last)
{
    limb_t v;
    int n;
    n = last - start + 1;
    if (n == LIMB_BITS)
        v = -1;
    else
        v = (((limb_t)1 << n) - 1) << start;
    return v;
}

/*  如果0和bit_pos之间的一位不为零，则返回！=0。 */ 
static inline limb_t scan_bit_nz(const bf_t *r, slimb_t bit_pos)
{
    slimb_t pos;
    limb_t v;
    
    pos = bit_pos >> LIMB_LOG2_BITS;
    if (pos < 0)
        return 0;
    v = r->tab[pos] & limb_mask(0, bit_pos & (LIMB_BITS - 1));
    if (v != 0)
        return 1;
    pos--;
    while (pos >= 0) {
        if (r->tab[pos] != 0)
            return 1;
        pos--;
    }
    return 0;
}

/*  返回四舍五入的加数。请注意，对于bf_rint()，prec可以&lt;=0。 */ 
static int bf_get_rnd_add(int *pret, const bf_t *r, limb_t l,
                          slimb_t prec, int rnd_mode)
{
    int add_one, inexact;
    limb_t bit1, bit0;
    
    if (rnd_mode == BF_RNDF) {
        bit0 = 1; /*  忠实的四舍五入不尊重不准确的旗帜。 */ 
    } else {
        /*  钻头‘prec+1’的起始支腿。 */ 
        bit0 = scan_bit_nz(r, l * LIMB_BITS - 1 - bf_max(0, prec + 1));
    }

    /*  从“prec”得到这一点。 */ 
    bit1 = get_bit(r->tab, l, l * LIMB_BITS - 1 - prec);
    inexact = (bit1 | bit0) != 0;
    
    add_one = 0;
    switch(rnd_mode) {
    case BF_RNDZ:
        break;
    case BF_RNDN:
        if (bit1) {
            if (bit0) {
                add_one = 1;
            } else {
                /*  四舍五入为偶数。 */ 
                add_one =
                    get_bit(r->tab, l, l * LIMB_BITS - 1 - (prec - 1));
            }
        }
        break;
    case BF_RNDD:
    case BF_RNDU:
        if (r->sign == (rnd_mode == BF_RNDD))
            add_one = inexact;
        break;
    case BF_RNDNA:
    case BF_RNDF:
        add_one = bit1;
        break;
    case BF_RNDNU:
        if (bit1) {
            if (r->sign)
                add_one = bit0;
            else
                add_one = 1;
        }
        break;
    default:
        abort();
    }
    
    if (inexact)
        *pret |= BF_ST_INEXACT;
    return add_one;
}

static int bf_set_overflow(bf_t *r, int sign, limb_t prec, bf_flags_t flags)
{
    slimb_t i, l, e_max;
    int rnd_mode;
    
    rnd_mode = flags & BF_RND_MASK;
    if (prec == BF_PREC_INF ||
        rnd_mode == BF_RNDN ||
        rnd_mode == BF_RNDNA ||
        rnd_mode == BF_RNDNU ||
        (rnd_mode == BF_RNDD && sign == 1) ||
        (rnd_mode == BF_RNDU && sign == 0)) {
        bf_set_inf(r, sign);
    } else {
        /*  设置为最大有限数量。 */ 
        l = (prec + LIMB_BITS - 1) / LIMB_BITS;
        bf_resize(r, l);
        r->tab[0] = limb_mask((-prec) & (LIMB_BITS - 1),
                              LIMB_BITS - 1);
        for(i = 1; i < l; i++)
            r->tab[i] = (limb_t)-1;
        e_max = (limb_t)1 << (bf_get_exp_bits(flags) - 1);
        r->expn = e_max;
        r->sign = sign;
    }
    return BF_ST_OVERFLOW | BF_ST_INEXACT;
}

/*  假设‘r’非零且有限，则四舍五入为前1位。“R”是假定长度为‘l’。注意：‘pr1’可以是负数，也可以是无限(BF_PREC_INF)。 */ 
static int __bf_round(bf_t *r, limb_t prec1, bf_flags_t flags, limb_t l)
{
    limb_t v, a;
    int shift, add_one, ret, rnd_mode;
    slimb_t i, bit_pos, pos, e_min, e_max, e_range, prec;

    /*  计算e_min和e_max以匹配IEEE 754约定。 */ 
    e_range = (limb_t)1 << (bf_get_exp_bits(flags) - 1);
    e_min = -e_range + 3;
    e_max = e_range;
    
    if (unlikely(r->expn < e_min) && (flags & BF_FLAG_SUBNORMAL)) {
        /*  在可能出现不正常的情况下限制精度结果。 */ 
        prec = prec1 - (e_min - r->expn);
    } else {
        prec = prec1;
    }
    
    /*  四舍五入至前置位。 */ 
    rnd_mode = flags & BF_RND_MASK;
    ret = 0;
    add_one = bf_get_rnd_add(&ret, r, l, prec, rnd_mode);
    
    if (prec <= 0) {
        if (add_one) {
            bf_resize(r, 1);
            r->tab[0] = (limb_t)1 << (LIMB_BITS - 1);
            r->expn += 1 - prec;
            ret |= BF_ST_UNDERFLOW | BF_ST_INEXACT;
            return ret;
        } else {
            goto underflow;
        }
    } else if (add_one) {
        limb_t carry;
        
        /*  从数字‘prec-1’开始加1。 */ 
        bit_pos = l * LIMB_BITS - 1 - (prec - 1);
        pos = bit_pos >> LIMB_LOG2_BITS;
        carry = (limb_t)1 << (bit_pos & (LIMB_BITS - 1));
        
        for(i = pos; i < l; i++) {
            v = r->tab[i] + carry;
            carry = (v < carry);
            r->tab[i] = v;
            if (carry == 0)
                break;
        }
        if (carry) {
            /*  右移一位数。 */ 
            v = 1;
            for(i = l - 1; i >= pos; i--) {
                a = r->tab[i];
                r->tab[i] = (a >> 1) | (v << (LIMB_BITS - 1));
                v = a;
            }
            r->expn++;
        }
    }
    
    /*  检查下溢。 */ 
    if (unlikely(r->expn < e_min)) {
        if (flags & BF_FLAG_SUBNORMAL) {
            /*  如果不准确，还要设置下溢标志。 */ 
            if (ret & BF_ST_INEXACT)
                ret |= BF_ST_UNDERFLOW;
        } else {
        underflow:
            ret |= BF_ST_UNDERFLOW | BF_ST_INEXACT;
            bf_set_zero(r, r->sign);
            return ret;
        }
    }
    
    /*  检查溢出。 */ 
    if (unlikely(r->expn > e_max))
        return bf_set_overflow(r, r->sign, prec1, flags);
    
    /*  保持位从‘prec-1’开始。 */ 
    bit_pos = l * LIMB_BITS - 1 - (prec - 1);
    i = bit_pos >> LIMB_LOG2_BITS;
    if (i >= 0) {
        shift = bit_pos & (LIMB_BITS - 1);
        if (shift != 0)
            r->tab[i] &= limb_mask(shift, LIMB_BITS - 1);
    } else {
        i = 0;
    }
    /*  删除尾随零。 */ 
    while (r->tab[i] == 0)
        i++;
    if (i > 0) {
        l -= i;
        memmove(r->tab, r->tab + i, l * sizeof(limb_t));
    }
    bf_resize(r, l);
    return ret;
}

/*  “R”必须是有限数字。 */ 
int bf_normalize_and_round(bf_t *r, limb_t prec1, bf_flags_t flags)
{
    limb_t l, v, a;
    int shift, ret;
    slimb_t i;
    
    //  Bf_print_str(“bf_renorm”，r)；
    l = r->len;
    while (l > 0 && r->tab[l - 1] == 0)
        l--;
    if (l == 0) {
        /*  零。 */ 
        r->expn = BF_EXP_ZERO;
        bf_resize(r, 0);
        ret = 0;
    } else {
        r->expn -= (r->len - l) * LIMB_BITS;
        /*  Shift将MSB设置为“1” */ 
        v = r->tab[l - 1];
        shift = clz(v);
        if (shift != 0) {
            v = 0;
            for(i = 0; i < l; i++) {
                a = r->tab[i];
                r->tab[i] = (a << shift) | (v >> (LIMB_BITS - shift));
                v = a;
            }
            r->expn -= shift;
        }
        ret = __bf_round(r, prec1, flags, l);
    }
    //  Bf_print_str(“r_final”，r)；
    return ret;
}

/*  如果可以以精度‘prec’进行舍入，则返回TRUE精确的结果r是这样的：|r-a|&lt;=2^(exp(A)-k)。 */ 
/*  XXX：检查指数将按舍入。 */ 
int bf_can_round(const bf_t *a, slimb_t prec, bf_rnd_t rnd_mode, slimb_t k)
{
    BOOL is_rndn;
    slimb_t bit_pos, n;
    limb_t bit;
    
    if (a->expn == BF_EXP_INF || a->expn == BF_EXP_NAN)
        return FALSE;
    if (rnd_mode == BF_RNDF) {
        return (k >= (prec + 1));
    }
    if (a->expn == BF_EXP_ZERO)
        return FALSE;
    is_rndn = (rnd_mode == BF_RNDN || rnd_mode == BF_RNDNA ||
               rnd_mode == BF_RNDNU);
    if (k < (prec + 2))
        return FALSE;
    bit_pos = a->len * LIMB_BITS - 1 - prec;
    n = k - prec;
    /*  RNDN或RNDNA的位模式：0111.。或者1000……其他四舍五入模式：000...。或者111...。 */ 
    bit = get_bit(a->tab, a->len, bit_pos);
    bit_pos--;
    n--;
    bit ^= is_rndn;
    /*  XXX：速度较慢，但平均迭代次数较少。 */ 
    while (n != 0) {
        if (get_bit(a->tab, a->len, bit_pos) != bit)
            return TRUE;
        bit_pos--;
        n--;
    }
    return FALSE;
}

int bf_round(bf_t *r, limb_t prec, bf_flags_t flags)
{
    if (r->len == 0)
        return 0;
    return __bf_round(r, prec, flags, r->len);
}

/*  用于调试。 */ 
static __maybe_unused void dump_limbs(const char *str, const limb_t *tab, limb_t n)
{
    limb_t i;
    printf("%s: len=%" PRId_LIMB "\n", str, n);
    for(i = 0; i < n; i++) {
        printf("%" PRId_LIMB ": " FMT_LIMB "\n",
               i, tab[i]);
    }
}

/*  用于调试。 */ 
void bf_print_str(const char *str, const bf_t *a)
{
    slimb_t i;
    printf("%s=", str);

    if (a->expn == BF_EXP_NAN) {
        printf("NaN");
    } else {
        if (a->sign)
            putchar('-');
        if (a->expn == BF_EXP_ZERO) {
            putchar('0');
        } else if (a->expn == BF_EXP_INF) {
            printf("Inf");
        } else {
            printf("0x0.");
            for(i = a->len - 1; i >= 0; i--)
                printf(FMT_LIMB, a->tab[i]);
            printf("p%" PRId_LIMB, a->expn);
        }
    }
    printf("\n");
}

/*  比较a和b的绝对值。如果a&lt;b，0则返回&lt;0如果a=b，则&gt;0，否则。 */ 
int bf_cmpu(const bf_t *a, const bf_t *b)
{
    slimb_t i;
    limb_t len, v1, v2;
    
    if (a->expn != b->expn) {
        if (a->expn < b->expn)
            return -1;
        else
            return 1;
    }
    len = bf_max(a->len, b->len);
    for(i = len - 1; i >= 0; i--) {
        v1 = get_limbz(a, a->len - len + i);
        v2 = get_limbz(b, b->len - len + i);
        if (v1 != v2) {
            if (v1 < v2)
                return -1;
            else
                return 1;
        }
    }
    return 0;
}

/*  完整顺序：-0&lt;0，NaN==NaN且NaN大于所有其他数字。 */ 
int bf_cmp_full(const bf_t *a, const bf_t *b)
{
    int res;
    
    if (a->expn == BF_EXP_NAN || b->expn == BF_EXP_NAN) {
        if (a->expn == b->expn)
            res = 0;
        else if (a->expn == BF_EXP_NAN)
            res = 1;
        else
            res = -1;
    } else if (a->sign != b->sign) {
        res = 1 - 2 * a->sign;
    } else {
        res = bf_cmpu(a, b);
        if (a->sign)
            res = -res;
    }
    return res;
}

#define BF_CMP_EQ 1
#define BF_CMP_LT 2
#define BF_CMP_LE 3

static int bf_cmp(const bf_t *a, const bf_t *b, int op)
{
    BOOL is_both_zero;
    int res;
    
    if (a->expn == BF_EXP_NAN || b->expn == BF_EXP_NAN)
        return 0;
    if (a->sign != b->sign) {
        is_both_zero = (a->expn == BF_EXP_ZERO && b->expn == BF_EXP_ZERO);
        if (is_both_zero) {
            return op & BF_CMP_EQ;
        } else if (op & BF_CMP_LT) {
            return a->sign;
        } else {
            return FALSE;
        }
    } else {
        res = bf_cmpu(a, b);
        if (res == 0) {
            return op & BF_CMP_EQ;
        } else if (op & BF_CMP_LT) {
            return (res < 0) ^ a->sign;
        } else {
            return FALSE;
        }
    }
}

int bf_cmp_eq(const bf_t *a, const bf_t *b)
{
    return bf_cmp(a, b, BF_CMP_EQ);
}

int bf_cmp_le(const bf_t *a, const bf_t *b)
{
    return bf_cmp(a, b, BF_CMP_LE);
}

int bf_cmp_lt(const bf_t *a, const bf_t *b)
{
    return bf_cmp(a, b, BF_CMP_LT);
}

/*  计算与该模式匹配的位数‘n’：A=x1000..0B=X0111..1在计算a-b时，结果将至少有n个前导零比特。前提条件：a&gt;b和a.expn-b.expn=0或1。 */ 
static limb_t count_cancelled_bits(const bf_t *a, const bf_t *b)
{
    slimb_t bit_offset, b_offset, n;
    int p, p1;
    limb_t v1, v2, mask;

    bit_offset = a->len * LIMB_BITS - 1;
    b_offset = (b->len - a->len) * LIMB_BITS - (LIMB_BITS - 1) +
        a->expn - b->expn;
    n = 0;

    /*  首先搜索等于位。 */ 
    for(;;) {
        v1 = get_limbz(a, bit_offset >> LIMB_LOG2_BITS);
        v2 = get_bits(b->tab, b->len, bit_offset + b_offset);
        //  Printf(“v1=”fmt_Limb“v2=”fmt_Limb“\n”，v1，v2)；
        if (v1 != v2)
            break;
        n += LIMB_BITS;
        bit_offset -= LIMB_BITS;
    }
    /*  找到第一个不同位的位置。 */ 
    p = clz(v1 ^ v2) + 1;
    n += p;
    /*  然后在a中搜索“0”，在b中搜索“1” */ 
    p = LIMB_BITS - p;
    if (p > 0) {
        /*  在v1和v2的尾随p位中搜索。 */ 
        mask = limb_mask(0, p - 1);
        p1 = bf_min(clz(v1 & mask), clz((~v2) & mask)) - (LIMB_BITS - p);
        n += p1;
        if (p1 != p)
            goto done;
    }
    bit_offset -= LIMB_BITS;
    for(;;) {
        v1 = get_limbz(a, bit_offset >> LIMB_LOG2_BITS);
        v2 = get_bits(b->tab, b->len, bit_offset + b_offset);
        //  Printf(“v1=”fmt_Limb“v2=”fmt_Limb“\n”，v1，v2)；
        if (v1 != 0 || v2 != -1) {
            /*  不同：计算匹配的位数。 */ 
            p1 = bf_min(clz(v1), clz(~v2));
            n += p1;
            break;
        }
        n += LIMB_BITS;
        bit_offset -= LIMB_BITS;
    }
 done:
    return n;
}

static int bf_add_internal(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                           bf_flags_t flags, int b_neg)
{
    const bf_t *tmp;
    int is_sub, ret, cmp_res, a_sign, b_sign;

    a_sign = a->sign;
    b_sign = b->sign ^ b_neg;
    is_sub = a_sign ^ b_sign;
    cmp_res = bf_cmpu(a, b);
    if (cmp_res < 0) {
        tmp = a;
        a = b;
        b = tmp;
        a_sign = b_sign; /*  以后再也不使用B_SIGN。 */ 
    }
    /*  ABS(A)&gt;=abs(B)。 */ 
    if (cmp_res == 0 && is_sub && a->expn < BF_EXP_INF) {
        /*  零结果。 */ 
        bf_set_zero(r, (flags & BF_RND_MASK) == BF_RNDD);
        ret = 0;
    } else if (a->len == 0 || b->len == 0) {
        ret = 0;
        if (a->expn >= BF_EXP_INF) {
            if (a->expn == BF_EXP_NAN) {
                /*  至少有一个操作数为NaN。 */ 
                bf_set_nan(r);
            } else if (b->expn == BF_EXP_INF && is_sub) {
                /*  符号不同的无穷大。 */ 
                bf_set_nan(r);
                ret = BF_ST_INVALID_OP;
            } else {
                bf_set_inf(r, a_sign);
            }
        } else {
            /*  至少一个零且不减法。 */ 
            bf_set(r, a);
            r->sign = a_sign;
            goto renorm;
        }
    } else {
        slimb_t d, a_offset, b_bit_offset, i, cancelled_bits;
        limb_t carry, v1, v2, u, r_len, carry1, precl, tot_len, z, sub_mask;

        r->sign = a_sign;
        r->expn = a->expn;
        d = a->expn - b->expn;
        /*  必须为中的前导取消位添加更多精度减法。 */ 
        if (is_sub) {
            if (d <= 1)
                cancelled_bits = count_cancelled_bits(a, b);
            else
                cancelled_bits = 1;
        } else {
            cancelled_bits = 0;
        }
        
        /*  增加两个额外的位进行四舍五入。 */ 
        precl = (cancelled_bits + prec + 2 + LIMB_BITS - 1) / LIMB_BITS;
        tot_len = bf_max(a->len, b->len + (d + LIMB_BITS - 1) / LIMB_BITS);
        r_len = bf_min(precl, tot_len);
        bf_resize(r, r_len);
        a_offset = a->len - r_len;
        b_bit_offset = (b->len - r_len) * LIMB_BITS + d;

        /*  计算舍入之前的位数。 */ 
        carry = is_sub;
        z = 0;
        sub_mask = -is_sub;
        i = r_len - tot_len;
        while (i < 0) {
            slimb_t ap, bp;
            BOOL inflag;
            
            ap = a_offset + i;
            bp = b_bit_offset + i * LIMB_BITS;
            inflag = FALSE;
            if (ap >= 0 && ap < a->len) {
                v1 = a->tab[ap];
                inflag = TRUE;
            } else {
                v1 = 0;
            }
            if (bp + LIMB_BITS > 0 && bp < (slimb_t)(b->len * LIMB_BITS)) {
                v2 = get_bits(b->tab, b->len, bp);
                inflag = TRUE;
            } else {
                v2 = 0;
            }
            if (!inflag) {
                /*  在‘a’和‘b’之外：直接转到下一个值在a或b内，因此运行时间不会取决于指数差。 */ 
                i = 0;
                if (ap < 0)
                    i = bf_min(i, -a_offset);
                /*  B_Bit_Offset+I*Limb_Bits+Limb_Bits&gt;=1相当于I&gt;=单元(-b_位偏移量+1-肢体位)/肢体位)。 */ 
                if (bp + LIMB_BITS <= 0)
                    i = bf_min(i, (-b_bit_offset) >> LIMB_LOG2_BITS);
            } else {
                i++;
            }
            v2 ^= sub_mask;
            u = v1 + v2;
            carry1 = u < v1;
            u += carry;
            carry = (u < carry) | carry1;
            z |= u;
        }
        /*  而结果是。 */ 
        for(i = 0; i < r_len; i++) {
            v1 = get_limbz(a, a_offset + i);
            v2 = get_bits(b->tab, b->len, b_bit_offset + i * LIMB_BITS);
            v2 ^= sub_mask;
            u = v1 + v2;
            carry1 = u < v1;
            u += carry;
            carry = (u < carry) | carry1;
            r->tab[i] = u;
        }
        /*  设置用于舍入的额外位。 */ 
        r->tab[0] |= (z != 0);

        /*  进位仅在加法大小写中可用。 */ 
        if (!is_sub && carry) {
            bf_resize(r, r_len + 1);
            r->tab[r_len] = 1;
            r->expn += LIMB_BITS;
        }
    renorm:
        ret = bf_normalize_and_round(r, prec, flags);
    }
    return ret;
}

static int __bf_add(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                     bf_flags_t flags)
{
    return bf_add_internal(r, a, b, prec, flags, 0);
}

static int __bf_sub(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                     bf_flags_t flags)
{
    return bf_add_internal(r, a, b, prec, flags, 1);
}

static limb_t mp_add(limb_t *res, const limb_t *op1, const limb_t *op2, 
                     limb_t n, limb_t carry)
{
    slimb_t i;
    limb_t k, a, v, k1;
    
    k = carry;
    for(i=0;i<n;i++) {
        v = op1[i];
        a = v + op2[i];
        k1 = a < v;
        a = a + k;
        k = (a < k) | k1;
        res[i] = a;
    }
    return k;
}

/*  Tabr[]+=Taba[]*b，返回高位字。 */ 
static limb_t mp_add_mul1(limb_t *tabr, const limb_t *taba, limb_t n,
                              limb_t b)
{
    limb_t i, l;
    dlimb_t t;
    
    l = 0;
    for(i = 0; i < n; i++) {
        t = (dlimb_t)taba[i] * (dlimb_t)b + l + tabr[i];
        tabr[i] = t;
        l = t >> LIMB_BITS;
    }
    return l;
}

/*  Tabr[]-=Taba[]*b.返回要减去的值到最高单词。 */ 
static limb_t mp_sub_mul1(limb_t *tabr, const limb_t *taba, limb_t n,
                          limb_t b)
{
    limb_t i, l;
    dlimb_t t;
    
    l = 0;
    for(i = 0; i < n; i++) {
        t = tabr[i] - (dlimb_t)taba[i] * (dlimb_t)b - l;
        tabr[i] = t;
        l = -(t >> LIMB_BITS);
    }
    return l;
}

/*  警告：D必须&gt;=2^(Limb_Bits-1)。 */ 
static inline limb_t udiv1norm_init(limb_t d)
{
    limb_t a0, a1;
    a1 = -d - 1;
    a0 = -1;
    return (((dlimb_t)a1 << LIMB_BITS) | a0) / d;
}

/*  返回商和‘a1*2^Limb_bit+a0的’*pr‘中的余数/d‘，其中0&lt;=a1&lt;d。 */ 
static inline limb_t udiv1norm(limb_t *pr, limb_t a1, limb_t a0,
                                limb_t d, limb_t d_inv)
{
    limb_t n1m, n_adj, q, r, ah;
    dlimb_t a;
    n1m = ((slimb_t)a0 >> (LIMB_BITS - 1));
    n_adj = a0 + (n1m & d);
    a = (dlimb_t)d_inv * (a1 - n1m) + n_adj;
    q = (a >> LIMB_BITS) + a1;
    /*  计算a-q*r并更新q，使余数为\介于0和d-1之间。 */ 
    a = ((dlimb_t)a1 << LIMB_BITS) | a0;
    a = a - (dlimb_t)q * d - d;
    ah = a >> LIMB_BITS;
    q += 1 + ah;
    r = (limb_t)a + (ah & d);
    *pr = r;
    return q;
}

/*  B必须&gt;=1&lt;&lt;(肢体_位-1)。 */ 
static limb_t mp_div1norm(limb_t *tabr, const limb_t *taba, limb_t n,
                          limb_t b, limb_t r)
{
    slimb_t i;

    if (n >= 3) {
        limb_t b_inv;
        b_inv = udiv1norm_init(b);
        for(i = n - 1; i >= 0; i--) {
            tabr[i] = udiv1norm(&r, r, taba[i], b, b_inv);
        }
    } else {
        dlimb_t a1;
        for(i = n - 1; i >= 0; i--) {
            a1 = ((dlimb_t)r << LIMB_BITS) | taba[i];
            tabr[i] = a1 / b;
            r = a1 % b;
        }
    }
    return r;
}

/*  基本大小写除法：将Taba[0..na-1]除以Tabb[0..nb-1]。制表符，制表符]必须是&gt;=1&lt;&lt;(Limb_Bits-1)。NA-Nb必须&gt;=0。‘Taba’被修改，并包含剩余部分(nb肢体)。Tabq[0..na-nb]包含商。Taba[NA]被修饰。 */ 
static void mp_divnorm(limb_t *tabq, 
                       limb_t *taba, limb_t na, 
                       const limb_t *tabb, limb_t nb)
{
    limb_t r, a, c, q, v, b1, b1_inv, n, dummy_r;
    slimb_t i;

    b1 = tabb[nb - 1];
    if (nb == 1) {
        taba[0] = mp_div1norm(tabq, taba, na, b1, 0);
        return;
    }
    taba[na] = 0;
    n = na - nb;
    if (n >= 3)
        b1_inv = udiv1norm_init(b1);
    else
        b1_inv = 0;

    /*  XXX：可以简化第一次迭代。 */ 
    for(i = n; i >= 0; i--) {
        if (unlikely(taba[i + nb] >= b1)) {
            q = -1;
        } else if (b1_inv) {
            q = udiv1norm(&dummy_r, taba[i + nb], taba[i + nb - 1], b1, b1_inv);
        } else {
            dlimb_t al;
            al = ((dlimb_t)taba[i + nb] << LIMB_BITS) | taba[i + nb - 1];
            q = al / b1;
            r = al % b1;
        }
        r = mp_sub_mul1(taba + i, tabb, nb, q);

        v = taba[i + nb];
        a = v - r;
        c = (a > v);
        taba[i + nb] = a;

        if (c != 0) {
            /*  负面结果。 */ 
            for(;;) {
                q--;
                c = mp_add(taba + i, taba + i, tabb, nb, 0);
                /*  传播进位并测试结果是否为阳性。 */ 
                if (c != 0) {
                    if (++taba[i + nb] == 0) {
                        break;
                    }
                }
            }
        }
        tabq[i] = q;
    }
}

int bf_mul(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
           bf_flags_t flags)
{
    limb_t i;
    int ret, r_sign;

    if (a->len < b->len) {
        const bf_t *tmp = a;
        a = b;
        b = tmp;
    }
    r_sign = a->sign ^ b->sign;
    /*  在这里，b-&gt;len&lt; */ 
    if (b->len == 0) {
        if (a->expn == BF_EXP_NAN || b->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            ret = 0;
        } else if (a->expn == BF_EXP_INF || b->expn == BF_EXP_INF) {
            if ((a->expn == BF_EXP_INF && b->expn == BF_EXP_ZERO) ||
                (a->expn == BF_EXP_ZERO && b->expn == BF_EXP_INF)) {
                bf_set_nan(r);
                ret = BF_ST_INVALID_OP;
            } else {
                bf_set_inf(r, r_sign);
                ret = 0;
            }
        } else {
            bf_set_zero(r, r_sign);
            ret = 0;
        }
    } else {
        bf_t tmp, *r1 = NULL;
        limb_t a_len, b_len, precl;
        limb_t *a_tab, *b_tab;
            
        a_len = a->len;
        b_len = b->len;
        
        if ((flags & BF_RND_MASK) == BF_RNDF) {
            /*   */ 
            precl = (prec + 2 + LIMB_BITS - 1) / LIMB_BITS;
            a_len = bf_min(a_len, precl);
            b_len = bf_min(b_len, precl);
        }
        a_tab = a->tab + a->len - a_len;
        b_tab = b->tab + b->len - b_len;
        
#ifdef USE_FFT_MUL
        if (b_len >= FFT_MUL_THRESHOLD) {
            int mul_flags = 0;
            if (r == a)
                mul_flags |= FFT_MUL_R_OVERLAP_A;
            if (r == b)
                mul_flags |= FFT_MUL_R_OVERLAP_B;
            fft_mul(r, a_tab, a_len, b_tab, b_len, mul_flags);
        } else
#endif
        {
            if (r == a || r == b) {
                bf_init(r->ctx, &tmp);
                r1 = r;
                r = &tmp;
            }
            bf_resize(r, a_len + b_len);
            memset(r->tab, 0, sizeof(limb_t) * a_len);
            for(i = 0; i < b_len; i++) {
                r->tab[i + a_len] = mp_add_mul1(r->tab + i, a_tab, a_len,
                                                b_tab[i]);
            }
        }
        r->sign = r_sign;
        r->expn = a->expn + b->expn;
        ret = bf_normalize_and_round(r, prec, flags);
        if (r == &tmp)
            bf_move(r1, &tmp);
    }
    return ret;
}

/*   */ 
int bf_mul_2exp(bf_t *r, slimb_t e, limb_t prec, bf_flags_t flags)
{
    slimb_t e_max;
    if (r->len == 0)
        return 0;
    e_max = ((limb_t)1 << BF_EXP_BITS_MAX) - 1;
    e = bf_max(e, -e_max);
    e = bf_min(e, e_max);
    r->expn += e;
    return __bf_round(r, prec, flags, r->len);
}

static void bf_recip_rec(bf_t *a, const bf_t *x, limb_t prec1)
{
    bf_t t0;
    limb_t prec;

    bf_init(a->ctx, &t0);

    if (prec1 <= LIMB_BITS - 3) {
        limb_t v;
        /*  初始近似。 */ 
        v = x->tab[x->len - 1];
        /*  2^(L-1)&lt;=v&lt;=2^L-1(L=肢体比特)。 */ 
        v = ((dlimb_t)1 << (2 * LIMB_BITS - 2)) / v;
        /*  2^(L-2)&lt;=v&lt;=2^(L-1)。 */ 
        bf_resize(a, 1);
        a->sign = x->sign;
        a->expn = 2 - x->expn;
        if (v == ((limb_t)1 << (LIMB_BITS - 1))) {
            a->tab[0] = v;
        } else {
            a->tab[0] = v << 1;
            a->expn--;
        }
        a->tab[0] &= limb_mask(LIMB_BITS - prec1, LIMB_BITS - 1);
    } else {
        /*  XXX：证明增加的精度。 */ 
        bf_recip_rec(a, x, (prec1 / 2) + 8);
        prec = prec1 + 8;

        /*  A=a+a*(1-x*a)。 */ 
        bf_mul(&t0, x, a, prec, BF_RNDF);
        t0.sign ^= 1;
        bf_add_si(&t0, &t0, 1, prec, BF_RNDF);
        bf_mul(&t0, &t0, a, prec, BF_RNDF);
        bf_add(a, a, &t0, prec1, BF_RNDF);
    }
    //  Bf_print_str(“r”，a)；
    bf_delete(&t0);
}

/*  注：仅支持忠实舍入。 */ 
void bf_recip(bf_t *r, const bf_t *a, limb_t prec)
{
    assert(r != a);
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
        } else if (a->expn == BF_EXP_INF) {
            bf_set_zero(r, a->sign);
        } else {
            bf_set_inf(r, a->sign);
        }
    } else {
        //  Bf_print_str(“a”，a)；
        bf_recip_rec(r, a, prec);
    }
}

/*  如有必要，可添加零个肢体，以使其至少具有前肢。 */ 
static void bf_add_zero_limbs(bf_t *r, limb_t precl)
{
    limb_t l = r->len;
    if (l < precl) {
        bf_resize(r, precl);
        memmove(r->tab + precl - l, r->tab,
                l * sizeof(limb_t));
        memset(r->tab, 0, (precl - l) * sizeof(limb_t));
    }
}

/*  在位位置&gt;=(PRCL*LIMP_BITS-1)将位设置为1。 */ 
static void bf_or_one(bf_t *r, limb_t precl)
{
    bf_add_zero_limbs(r, precl);
    r->tab[0] |= 1;
}

/*  返回e，如a=m*2^e，m为奇数。如果a为零，则返回0，无限大或南。 */ 
slimb_t bf_get_exp_min(const bf_t *a)
{
    slimb_t i;
    limb_t v;
    int k;
    
    for(i = 0; i < a->len; i++) {
        v = a->tab[i];
        if (v != 0) {
            k = ctz(v);
            return a->expn - (a->len - i) * LIMB_BITS + k;
        }
    }
    return 0;
}

/*  A和b必须是a&gt;=0和b&gt;0的有限数。“Q”是定义为下限(a/b)且r=a-q*b的整数。 */ 
static void bf_tdivremu(bf_t *q, bf_t *r,
                        const bf_t *a, const bf_t *b)
{
    if (a->expn < b->expn) {
        bf_set_ui(q, 0);
        bf_set(r, a);
    } else {
        /*  对于大数，请使用忠实模式。 */ 
        bf_div(q, a, b, bf_max(a->expn - b->expn + 1, 2), BF_RNDF);
        bf_rint(q, BF_PREC_INF, BF_RNDZ);
        bf_mul(r, q, b, BF_PREC_INF, BF_RNDN);
        bf_sub(r, a, r, BF_PREC_INF, BF_RNDN);
        if (r->len != 0 && r->sign) {
            bf_add_si(q, q, -1, BF_PREC_INF, BF_RNDZ);
            bf_add(r, r, b, BF_PREC_INF, BF_RNDZ);
        }
    }
}

static int __bf_div(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                    bf_flags_t flags)
{
    int ret, r_sign;
    limb_t n, nb, precl;
    
    r_sign = a->sign ^ b->sign;
    if (a->expn >= BF_EXP_INF || b->expn >= BF_EXP_INF) {
        if (a->expn == BF_EXP_NAN || b->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else if (a->expn == BF_EXP_INF && b->expn == BF_EXP_INF) {
            bf_set_nan(r);
            return BF_ST_INVALID_OP;
        } else if (a->expn == BF_EXP_INF) {
            bf_set_inf(r, r_sign);
            return 0;
        } else {
            bf_set_zero(r, r_sign);
            return 0;
        }
    } else if (a->expn == BF_EXP_ZERO) {
        if (b->expn == BF_EXP_ZERO) {
            bf_set_nan(r);
            return BF_ST_INVALID_OP;
        } else {
            bf_set_zero(r, r_sign);
            return 0;
        }
    } else if (b->expn == BF_EXP_ZERO) {
        bf_set_inf(r, r_sign);
        return BF_ST_DIVIDE_ZERO;
    }

    /*  商的支数(2个额外的四舍五入位)。 */ 
    precl = (prec + 2 + LIMB_BITS - 1) / LIMB_BITS;
    nb = b->len;
    n = bf_max(a->len, precl);
    
    if (nb <= BASECASE_DIV_THRESHOLD_B ||
        (slimb_t)n <= (BASECASE_DIV_THRESHOLD_Q - 1)) {
        limb_t *taba, na, i;
        slimb_t d;
        
        na = n + nb;
        taba = bf_malloc(r->ctx, (na + 1) * sizeof(limb_t));
        d = na - a->len;
        memset(taba, 0, d * sizeof(limb_t));
        memcpy(taba + d, a->tab, a->len * sizeof(limb_t));
        bf_resize(r, n + 1);
        mp_divnorm(r->tab, taba, na, b->tab, nb);
        
        /*  看看非零余数是否。 */ 
        for(i = 0; i < nb; i++) {
            if (taba[i] != 0) {
                r->tab[0] |= 1;
                break;
            }
        }
        bf_free(r->ctx, taba);
        r->expn = a->expn - b->expn + LIMB_BITS;
        r->sign = r_sign;
        ret = bf_normalize_and_round(r, prec, flags);
    } else if ((flags & BF_RND_MASK) == BF_RNDF) {
        bf_t b_inv;
        bf_init(r->ctx, &b_inv);
        bf_recip(&b_inv, b, prec + 3);
        ret = bf_mul(r, a, &b_inv, prec, flags);
        bf_delete(&b_inv);
    } else {
        bf_t a1_s, *a1 = &a1_s;
        bf_t b1_s, *b1 = &b1_s;
        bf_t rem_s, *rem = &rem_s;
        
        /*  将‘a’和‘b’的尾数转换为整数并生成至少具有prec+2位的商。 */ 
        a1->expn = (n + nb) * LIMB_BITS;
        a1->tab = a->tab;
        a1->len = a->len;
        a1->sign = 0;

        b1->expn = nb * LIMB_BITS;
        b1->tab = b->tab;
        b1->len = nb;
        b1->sign = 0;

        bf_init(r->ctx, rem);
        bf_tdivremu(r, rem, a1, b1);
        /*  余数不是零：放在四舍五入位中。 */ 
        if (rem->len != 0) {
            bf_or_one(r, precl);
        }
        bf_delete(rem);
        r->expn += a->expn - b->expn - n * LIMB_BITS;
        r->sign = r_sign;
        ret = bf_round(r, prec, flags);
    }
    return ret;
}

/*  除法和余数。RND_MODE是商的舍入模式。附加的支持四舍五入模式BF_RND_Euclidian。“q”是一个整数。“R”用prec和标志四舍五入(prec可以是BF_PREC_INF)。 */ 
int bf_divrem(bf_t *q, bf_t *r, const bf_t *a, const bf_t *b,
              limb_t prec, bf_flags_t flags, int rnd_mode)
{
    bf_t a1_s, *a1 = &a1_s;
    bf_t b1_s, *b1 = &b1_s;
    int q_sign;
    BOOL is_ceil, is_rndn;
    
    assert(q != a && q != b);
    assert(r != a && r != b);
    assert(q != r);
    
    if (a->len == 0 || b->len == 0) {
        bf_set_zero(q, 0);
        if (a->expn == BF_EXP_NAN || b->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else if (a->expn == BF_EXP_INF || b->expn == BF_EXP_ZERO) {
            bf_set_nan(r);
            return BF_ST_INVALID_OP;
        } else {
            bf_set(r, a);
            return bf_round(r, prec, flags);
        }
    }

    q_sign = a->sign ^ b->sign;
    is_rndn = (rnd_mode == BF_RNDN || rnd_mode == BF_RNDNA ||
               rnd_mode == BF_RNDNU);
    switch(rnd_mode) {
    default:
    case BF_RNDZ:
    case BF_RNDN:
    case BF_RNDNA:
        is_ceil = FALSE;
        break;
    case BF_RNDD:
        is_ceil = q_sign;
        break;
    case BF_RNDU:
        is_ceil = q_sign ^ 1;
        break;
    case BF_DIVREM_EUCLIDIAN:
        is_ceil = a->sign;
        break;
    case BF_RNDNU:
        /*  XXX：尚不支持。 */ 
        abort();
    }

    a1->expn = a->expn;
    a1->tab = a->tab;
    a1->len = a->len;
    a1->sign = 0;
    
    b1->expn = b->expn;
    b1->tab = b->tab;
    b1->len = b->len;
    b1->sign = 0;

    /*  XXX：可以改进以避免出现较大的‘Q’ */ 
    bf_tdivremu(q, r, a1, b1);

    if (r->len != 0) {
        if (is_rndn) {
            int res;
            b1->expn--;
            res = bf_cmpu(r, b1);
            b1->expn++;
            if (res > 0 ||
                (res == 0 &&
                 (rnd_mode == BF_RNDNA ||
                  get_bit(q->tab, q->len, q->len * LIMB_BITS - q->expn)))) {
                goto do_sub_r;
            }
        } else if (is_ceil) {
        do_sub_r:
            bf_add_si(q, q, 1, BF_PREC_INF, BF_RNDZ);
            bf_sub(r, r, b1, BF_PREC_INF, BF_RNDZ);
        }
    }

    r->sign ^= a->sign;
    q->sign = q_sign;
    return bf_round(r, prec, flags);
}

int bf_fmod(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
            bf_flags_t flags)
{
    bf_t q_s, *q = &q_s;
    int ret;
    
    bf_init(r->ctx, q);
    ret = bf_divrem(q, r, a, b, prec, flags, BF_RNDZ);
    bf_delete(q);
    return ret;
}

int bf_remainder(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                 bf_flags_t flags)
{
    bf_t q_s, *q = &q_s;
    int ret;
    
    bf_init(r->ctx, q);
    ret = bf_divrem(q, r, a, b, prec, flags, BF_RNDN);
    bf_delete(q);
    return ret;
}

static inline int bf_get_limb(slimb_t *pres, const bf_t *a, int flags)
{
#if LIMB_BITS == 32
    return bf_get_int32(pres, a, flags);
#else
    return bf_get_int64(pres, a, flags);
#endif
}

int bf_remquo(slimb_t *pq, bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
              bf_flags_t flags)
{
    bf_t q_s, *q = &q_s;
    int ret;
    
    bf_init(r->ctx, q);
    ret = bf_divrem(q, r, a, b, prec, flags, BF_RNDN);
    bf_get_limb(pq, q, BF_GET_INT_MOD);
    bf_delete(q);
    return ret;
}

static __maybe_unused inline limb_t mul_mod(limb_t a, limb_t b, limb_t m)
{
    dlimb_t t;
    t = (dlimb_t)a * (dlimb_t)b;
    return t % m;
}

#if defined(USE_MUL_CHECK)
static limb_t mp_mod1(const limb_t *tab, limb_t n, limb_t m, limb_t r)
{
    slimb_t i;
    dlimb_t t;

    for(i = n - 1; i >= 0; i--) {
        t = ((dlimb_t)r << LIMB_BITS) | tab[i];
        r = t % m;
    }
    return r;
}
#endif

/*  (128.0/SQRT((i+64)/256))&0xff。 */ 
static const uint8_t rsqrt_table[192] = {
  0,254,252,250,248,247,245,243,241,240,238,236,235,233,232,230,
229,228,226,225,223,222,221,220,218,217,216,215,214,212,211,210,
209,208,207,206,205,204,203,202,201,200,199,198,197,196,195,194,
194,193,192,191,190,189,189,188,187,186,185,185,184,183,182,182,
181,180,180,179,178,178,177,176,176,175,174,174,173,172,172,171,
171,170,169,169,168,168,167,167,166,166,165,164,164,163,163,162,
162,161,161,160,160,159,159,158,158,158,157,157,156,156,155,155,
154,154,154,153,153,152,152,151,151,151,150,150,149,149,149,148,
148,147,147,147,146,146,146,145,145,144,144,144,143,143,143,142,
142,142,141,141,141,140,140,140,139,139,139,138,138,138,137,137,
137,137,136,136,136,135,135,135,134,134,134,134,133,133,133,132,
132,132,132,131,131,131,131,130,130,130,130,129,129,129,129,128,
};

static void __bf_rsqrt(bf_t *a, const bf_t *x, limb_t prec1)
{
    bf_t t0;
    limb_t prec;

    if (prec1 <= 7) {
        slimb_t e;
        limb_t v;
        /*  使用8个尾数位的初始逼近。 */ 
        v = x->tab[x->len - 1];
        e = x->expn;
        if (e & 1) {
            v >>= 1;
            e++;
        }
        v = rsqrt_table[(v >> (LIMB_BITS - 8)) - 64];
        e = 1 - (e / 2);
        if (v == 0) {
            v = 128; /*  实际表值为256。 */ 
            e++;
        }
        bf_resize(a, 1);
        a->tab[0] = (v << (LIMB_BITS - 8)) &
            limb_mask(LIMB_BITS - prec1, LIMB_BITS - 1);
        a->expn = e;
        a->sign = 0;
    } else {
        /*  XXX：证明舍入。 */ 
        __bf_rsqrt(a, x, (prec1 / 2) + 2);

        prec = prec1 + 3;

        /*  X‘=x+(x/2)*(1-a*x^2)。 */ 
        bf_init(a->ctx, &t0);

        bf_mul(&t0, a, a, prec, BF_RNDF);
        bf_mul(&t0, &t0, x, prec, BF_RNDF);
        t0.sign ^= 1;
        bf_add_si(&t0, &t0, 1, prec, BF_RNDF);
        bf_mul(&t0, &t0, a, prec, BF_RNDF);
        if (t0.len != 0)
            t0.expn--;
        bf_add(a, a, &t0, prec1, BF_RNDF);

        bf_delete(&t0);
    }
}

static int __bf_sqrt(bf_t *x, const bf_t *a, limb_t prec1, bf_flags_t flags)
{
    bf_t t0, t1;
    limb_t prec;
    int ret;
    
    /*  XXX：证明舍入。 */ 
    __bf_rsqrt(x, a, (prec1 / 2) + 2);
    prec = prec1 + 3;

    /*  X‘=a*x+(x/2)*(a-(a*x)^2)。 */ 
    
    bf_init(x->ctx, &t0);
    bf_init(x->ctx, &t1);
    bf_mul(&t1, x, a, prec, BF_RNDF);
    bf_mul(&t0, &t1, &t1, prec, BF_RNDF);
    t0.sign ^= 1;
    bf_add(&t0, &t0, a, prec, BF_RNDF);
    bf_mul(&t0, &t0, x, prec, BF_RNDF);
    if (t0.len != 0)
        t0.expn--;
    ret = bf_add(x, &t1, &t0, prec1, flags);
    
    bf_delete(&t0);
    bf_delete(&t1);
    return ret;
}

/*  注：仅支持忠实舍入。 */ 
void bf_rsqrt(bf_t *r, const bf_t *a, limb_t prec)
{
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN ||
            (a->sign && a->expn != BF_EXP_ZERO)) {
            bf_set_nan(r);
        } else if (a->expn == BF_EXP_INF) {
            bf_set_zero(r, a->sign);
        } else {
            bf_set_inf(r, 0);
        }
    } else if (a->sign) {
        bf_set_nan(r);
    } else {
        //  Bf_print_str(“a”，a)；
        __bf_rsqrt(r, a, prec);
    }
}

/*  返回楼层(SQRT(A))。 */ 
static limb_t bf_isqrt(limb_t a)
{
    unsigned int l;
    limb_t u, s;
    
    if (a == 0)
        return 0;
    l = ceil_log2(a);
    u = (limb_t)1 << ((l + 1) / 2);
    /*  U&gt;=楼层(SQRT(A))。 */ 
    for(;;) {
        s = u;
        u = ((a / s) + s) / 2;
        if (u >= s)
            break;
    }
    return s;
}

/*  整数平方根加上余数。“a”必须是整数。R=FLOOR(SQRT(A))和rem=a-r^2。如果结果为是不准确的。如果不需要余数，则“rem”可以为Null。 */ 
int bf_sqrtrem(bf_t *r, bf_t *rem1, const bf_t *a)
{
    int ret;
    
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
        } else if (a->expn == BF_EXP_INF && a->sign) {
            goto invalid_op;
        } else {
            bf_set(r, a);
        }
        if (rem1)
            bf_set_ui(rem1, 0);
        ret = 0;
    } else if (a->sign) {
 invalid_op:
        bf_set_nan(r);
        if (rem1)
            bf_set_ui(rem1, 0);
        ret = BF_ST_INVALID_OP;
    } else {
        bf_t rem_s, *rem;
        int res;
        
        bf_sqrt(r, a, (a->expn + 1) / 2, BF_RNDF);
        bf_rint(r, BF_PREC_INF, BF_RNDZ);
        /*  通过计算余数看看结果是否准确。 */ 
        if (rem1) {
            rem = rem1;
        } else {
            rem = &rem_s;
            bf_init(r->ctx, rem);
        }
        bf_mul(rem, r, r, BF_PREC_INF, BF_RNDZ);
        ret = 0;
        if (rem1) {
            bf_neg(rem);
            bf_add(rem, rem, a, BF_PREC_INF, BF_RNDZ);
            if (rem->len != 0) {
                ret = BF_ST_INEXACT;
                if (rem->sign) {
                    bf_t a1_s, *a1 = &a1_s;
                    bf_add_si(r, r, -1, BF_PREC_INF, BF_RNDZ);
                    a1->tab = a->tab;
                    a1->len = a->len;
                    a1->sign = a->sign;
                    a1->expn = a->expn + 1;
                    bf_add(rem, rem, r, BF_PREC_INF, BF_RNDZ);
                    bf_add_si(rem, rem, 1, BF_PREC_INF, BF_RNDZ);
                }
            } else {
                ret = 0;
            }
        } else {
            res = bf_cmpu(rem, a);
            bf_delete(rem);
            //  Printf(“res2=%d\n”，res2)；
            if (res > 0) {
                /*  需要更正结果。 */ 
                bf_add_si(r, r, -1, BF_PREC_INF, BF_RNDZ);
            }
            ret = (res != 0 ? BF_ST_INEXACT : 0);
        }
    }
    return ret;
}

int bf_sqrt(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    int rnd_mode, ret;

    assert(r != a);

    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
        } else if (a->expn == BF_EXP_INF && a->sign) {
            goto invalid_op;
        } else {
            bf_set(r, a);
        }
        ret = 0;
    } else if (a->sign) {
 invalid_op:
        bf_set_nan(r);
        ret = BF_ST_INVALID_OP;
    } else {
        rnd_mode = flags & BF_RND_MASK;
        if (rnd_mode == BF_RNDF) {
            ret = __bf_sqrt(r, a, prec, flags);
        } else {
            bf_t a1_s, *a1 = &a1_s;
            slimb_t d, prec2;
            int res1, res2;

            bf_init(r->ctx, a1);
            bf_set(a1, a);
            /*  将尾数转换为最多为2*的整数PREC+4位。 */ 
            prec2 = prec + 2;
            /*  使‘-a-&gt;EXPN+d’可被2整除。 */ 
            d = prec2 * 2 - (a->expn & 1);
            a1->expn = d;
            res1 = bf_rint(a1, BF_PREC_INF, BF_RNDZ);
            res2 = bf_sqrtrem(r, NULL, a1);
            bf_delete(a1);
            if ((res2 | res1) != 0) {
                bf_or_one(r, (prec2 + LIMB_BITS - 1) / LIMB_BITS);
            }
            /*  更新指数。 */ 
            r->expn -= (-a->expn + d) >> 1;
            ret = bf_round(r, prec, flags);
        }
    }
    return ret;
}

static no_inline int bf_op2(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
                            bf_flags_t flags, bf_op2_func_t *func)
{
    bf_t tmp;
    int ret;
    
    if (r == a || r == b) {
        bf_init(r->ctx, &tmp);
        ret = func(&tmp, a, b, prec, flags);
        bf_move(r, &tmp);
    } else {
        ret = func(r, a, b, prec, flags);
    }
    return ret;
}

int bf_add(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
            bf_flags_t flags)
{
    return bf_op2(r, a, b, prec, flags, __bf_add);
}

int bf_sub(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
            bf_flags_t flags)
{
    return bf_op2(r, a, b, prec, flags, __bf_sub);
}

int bf_div(bf_t *r, const bf_t *a, const bf_t *b, limb_t prec,
           bf_flags_t flags)
{
    return bf_op2(r, a, b, prec, flags, __bf_div);
}

int bf_mul_ui(bf_t *r, const bf_t *a, uint64_t b1, limb_t prec,
               bf_flags_t flags)
{
    bf_t b;
    int ret;
    bf_init(r->ctx, &b);
    bf_set_ui(&b, b1);
    ret = bf_mul(r, a, &b, prec, flags);
    bf_delete(&b);
    return ret;
}

int bf_mul_si(bf_t *r, const bf_t *a, int64_t b1, limb_t prec,
               bf_flags_t flags)
{
    bf_t b;
    int ret;
    bf_init(r->ctx, &b);
    bf_set_si(&b, b1);
    ret = bf_mul(r, a, &b, prec, flags);
    bf_delete(&b);
    return ret;
}

int bf_add_si(bf_t *r, const bf_t *a, int64_t b1, limb_t prec,
              bf_flags_t flags)
{
    bf_t b;
    int ret;
    
    bf_init(r->ctx, &b);
    bf_set_si(&b, b1);
    ret = bf_add(r, a, &b, prec, flags);
    bf_delete(&b);
    return ret;
}

int bf_pow_ui(bf_t *r, const bf_t *a, limb_t b, limb_t prec,
              bf_flags_t flags)
{
    int ret, n_bits, i;
    
    assert(r != a);
    if (b == 0) {
        bf_set_ui(r, 1);
        return 0;
    }
    bf_set(r, a);
    ret = 0;
    n_bits = LIMB_BITS - clz(b);
    for(i = n_bits - 2; i >= 0; i--) {
        ret |= bf_mul(r, r, r, prec, flags);
        if ((b >> i) & 1)
            ret |= bf_mul(r, r, a, prec, flags);
    }
    return ret;
}

int bf_pow_ui_ui(bf_t *r, limb_t a1, limb_t b, limb_t prec, bf_flags_t flags)
{
    bf_t a;
    int ret;
    
    bf_init(r->ctx, &a);
    bf_set_ui(&a, a1);
    ret = bf_pow_ui(r, &a, b, prec, flags);
    bf_delete(&a);
    return ret;
}

/*  转换为整数(单舍入)。 */ 
int bf_rint(bf_t *r, limb_t prec, bf_flags_t flags)
{
    int ret;
    if (r->len == 0)
        return 0;
    if (r->expn <= 0) {
        ret = __bf_round(r, r->expn, flags & ~BF_FLAG_SUBNORMAL, r->len) &
            ~BF_ST_UNDERFLOW;
    } else {
        ret = __bf_round(r, bf_min(r->expn, prec), flags, r->len);
    }
    return ret;
}

/*  逻辑运算。 */ 
#define BF_LOGIC_OR  0
#define BF_LOGIC_XOR 1
#define BF_LOGIC_AND 2

static inline limb_t bf_logic_op1(limb_t a, limb_t b, int op)
{
    switch(op) {
    case BF_LOGIC_OR:
        return a | b;
    case BF_LOGIC_XOR:
        return a ^ b;
    default:
    case BF_LOGIC_AND:
        return a & b;
    }
}

static void bf_logic_op(bf_t *r, const bf_t *a1, const bf_t *b1, int op)
{
    bf_t b1_s, a1_s, *a, *b;
    limb_t a_sign, b_sign, r_sign;
    slimb_t l, i, a_bit_offset, b_bit_offset;
    limb_t v1, v2, v1_mask, v2_mask, r_mask;
    
    assert(r != a1 && r != b1);

    if (a1->expn <= 0)
        a_sign = 0; /*  负零被认为是正数。 */ 
    else
        a_sign = a1->sign;

    if (b1->expn <= 0)
        b_sign = 0; /*  负零被认为是正数。 */ 
    else
        b_sign = b1->sign;
    
    if (a_sign) {
        a = &a1_s;
        bf_init(r->ctx, a);
        bf_add_si(a, a1, 1, BF_PREC_INF, BF_RNDZ);
    } else {
        a = (bf_t *)a1;
    }

    if (b_sign) {
        b = &b1_s;
        bf_init(r->ctx, b);
        bf_add_si(b, b1, 1, BF_PREC_INF, BF_RNDZ);
    } else {
        b = (bf_t *)b1;
    }
    
    r_sign = bf_logic_op1(a_sign, b_sign, op);
    if (op == BF_LOGIC_AND && r_sign == 0) {
        /*  不需要为和计算额外的零。 */ 
        if (a_sign == 0 && b_sign == 0)
            l = bf_min(a->expn, b->expn);
        else if (a_sign == 0)
            l = a->expn;
        else
            l = b->expn;
    } else {
        l = bf_max(a->expn, b->expn);
    }
    /*  注：a或b可以为零。 */ 
    l = (bf_max(l, 1) + LIMB_BITS - 1) / LIMB_BITS;
    bf_resize(r, l);
    a_bit_offset = a->len * LIMB_BITS - a->expn;
    b_bit_offset = b->len * LIMB_BITS - b->expn;
    v1_mask = -a_sign;
    v2_mask = -b_sign;
    r_mask = -r_sign;
    for(i = 0; i < l; i++) {
        v1 = get_bits(a->tab, a->len, a_bit_offset + i * LIMB_BITS) ^ v1_mask;
        v2 = get_bits(b->tab, b->len, b_bit_offset + i * LIMB_BITS) ^ v2_mask;
        r->tab[i] = bf_logic_op1(v1, v2, op) ^ r_mask;
    }
    r->expn = l * LIMB_BITS;
    r->sign = r_sign;
    bf_normalize_and_round(r, BF_PREC_INF, BF_RNDZ);
    if (r_sign)
        bf_add_si(r, r, -1, BF_PREC_INF, BF_RNDZ);
    if (a == &a1_s)
        bf_delete(a);
    if (b == &b1_s)
        bf_delete(b);
}

/*  “a”和“b”必须是整数。 */ 
void bf_logic_or(bf_t *r, const bf_t *a, const bf_t *b)
{
    bf_logic_op(r, a, b, BF_LOGIC_OR);
}

/*  “a”和“b”必须是整数。 */ 
void bf_logic_xor(bf_t *r, const bf_t *a, const bf_t *b)
{
    bf_logic_op(r, a, b, BF_LOGIC_XOR);
}

/*  “a”和“b”必须是整数。 */ 
void bf_logic_and(bf_t *r, const bf_t *a, const bf_t *b)
{
    bf_logic_op(r, a, b, BF_LOGIC_AND);
}

/*  固定大小类型之间的转换。 */ 

typedef union {
    double d;
    uint64_t u;
} Float64Union;

int bf_get_float64(const bf_t *a, double *pres, bf_rnd_t rnd_mode)
{
    Float64Union u;
    int e, ret;
    uint64_t m;
    
    ret = 0;
    if (a->expn == BF_EXP_NAN) {
        u.u = 0x7ff8000000000000; /*  宁南。 */ 
    } else {
        bf_t b_s, *b = &b_s;
        
        bf_init(a->ctx, b);
        bf_set(b, a);
        if (bf_is_finite(b)) {
            ret = bf_round(b, 53, rnd_mode | BF_FLAG_SUBNORMAL | bf_set_exp_bits(11));
        }
        if (b->expn == BF_EXP_INF) {
            e = (1 << 11) - 1;
            m = 0;
        } else if (b->expn == BF_EXP_ZERO) {
            e = 0;
            m = 0;
        } else {
            e = b->expn + 1023 - 1;
#if LIMB_BITS == 32
            if (b->len == 2) {
                m = ((uint64_t)b->tab[1] << 32) | b->tab[0];
            } else {
                m = ((uint64_t)b->tab[0] << 32);
            }
#else
            m = b->tab[0];
#endif
            if (e <= 0) {
                /*  不正常。 */ 
                m = m >> (12 - e);
                e = 0;
            } else {
                m = (m << 1) >> 12;
            }
        }
        u.u = m | ((uint64_t)e << 52) | ((uint64_t)b->sign << 63);
        bf_delete(b);
    }
    *pres = u.d;
    return ret;
}

void bf_set_float64(bf_t *a, double d)
{
    Float64Union u;
    uint64_t m;
    int shift, e, sgn;
    
    u.d = d;
    sgn = u.u >> 63;
    e = (u.u >> 52) & ((1 << 11) - 1);
    m = u.u & (((uint64_t)1 << 52) - 1);
    if (e == ((1 << 11) - 1)) {
        if (m != 0) {
            bf_set_nan(a);
        } else {
            bf_set_inf(a, sgn);
        }
    } else if (e == 0) {
        if (m == 0) {
            bf_set_zero(a, sgn);
        } else {
            /*  次正态数。 */ 
            m <<= 12;
            shift = clz64(m);
            m <<= shift;
            e = -shift;
            goto norm;
        }
    } else {
        m = (m << 11) | ((uint64_t)1 << 63);
    norm:
        a->expn = e - 1023 + 1;
#if LIMB_BITS == 32
        bf_resize(a, 2);
        a->tab[0] = m;
        a->tab[1] = m >> 32;
#else
        bf_resize(a, 1);
        a->tab[0] = m;
#endif
        a->sign = sgn;
    }
}

/*  舍入模式始终为BF_RNDZ。如果存在则返回BF_ST_OVERFLOW为溢出，否则为0。 */ 
int bf_get_int32(int *pres, const bf_t *a, int flags)
{
    uint32_t v;
    int ret;
    if (a->expn >= BF_EXP_INF) {
        ret = 0;
        if (flags & BF_GET_INT_MOD) {
            v = 0;
        } else if (a->expn == BF_EXP_INF) {
            v = (uint32_t)INT32_MAX + a->sign;
        } else {
            v = INT32_MAX;
        }
    } else if (a->expn <= 0) {
        v = 0;
        ret = 0;
    } else if (a->expn <= 31) {
        v = a->tab[a->len - 1] >> (LIMB_BITS - a->expn);
        if (a->sign)
            v = -v;
        ret = 0;
    } else if (!(flags & BF_GET_INT_MOD)) {
        ret = BF_ST_OVERFLOW;
        if (a->sign) {
            v = (uint32_t)INT32_MAX + 1;
            if (a->expn == 32 && 
                (a->tab[a->len - 1] >> (LIMB_BITS - 32)) == v) {
                ret = 0;
            }
        } else {
            v = INT32_MAX;
        }
    } else {
        v = get_bits(a->tab, a->len, a->len * LIMB_BITS - a->expn); 
        if (a->sign)
            v = -v;
        ret = 0;
    }
    *pres = v;
    return ret;
}

/*  舍入模式始终为BF_RNDZ。如果存在则返回BF_ST_OVERFLOW为溢出，否则为0。 */ 
int bf_get_int64(int64_t *pres, const bf_t *a, int flags)
{
    uint64_t v;
    int ret;
    if (a->expn >= BF_EXP_INF) {
        ret = 0;
        if (flags & BF_GET_INT_MOD) {
            v = 0;
        } else if (a->expn == BF_EXP_INF) {
            v = (uint64_t)INT64_MAX + a->sign;
        } else {
            v = INT64_MAX;
        }
    } else if (a->expn <= 0) {
        v = 0;
        ret = 0;
    } else if (a->expn <= 63) {
#if LIMB_BITS == 32
        if (a->expn <= 32)
            v = a->tab[a->len - 1] >> (LIMB_BITS - a->expn);
        else
            v = (((uint64_t)a->tab[a->len - 1] << 32) |
                 get_limbz(a, a->len - 2)) >> (64 - a->expn);
#else
        v = a->tab[a->len - 1] >> (LIMB_BITS - a->expn);
#endif
        if (a->sign)
            v = -v;
        ret = 0;
    } else if (!(flags & BF_GET_INT_MOD)) {
        ret = BF_ST_OVERFLOW;
        if (a->sign) {
            uint64_t v1;
            v = (uint64_t)INT64_MAX + 1;
            if (a->expn == 64) {
                v1 = a->tab[a->len - 1];
#if LIMB_BITS == 32
                v1 |= (v1 << 32) | get_limbz(a, a->len - 2);
#endif
                if (v1 == v)
                    ret = 0;
            }
        } else {
            v = INT64_MAX;
        }
    } else {
        slimb_t bit_pos = a->len * LIMB_BITS - a->expn;
        v = get_bits(a->tab, a->len, bit_pos); 
#if LIMB_BITS == 32
        v |= (uint64_t)get_bits(a->tab, a->len, bit_pos + 32) << 32;
#endif
        if (a->sign)
            v = -v;
        ret = 0;
    }
    *pres = v;
    return ret;
}

/*  从基数转换为基数。 */ 

static const uint8_t digits_per_limb_table[BF_RADIX_MAX - 1] = {
#if LIMB_BITS == 32
32,20,16,13,12,11,10,10, 9, 9, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
#else
64,40,32,27,24,22,21,20,19,18,17,17,16,16,16,15,15,15,14,14,14,14,13,13,13,13,13,13,13,12,12,12,12,12,12,
#endif
};

static limb_t get_limb_radix(int radix)
{
    int i, k;
    limb_t radixl;
    
    k = digits_per_limb_table[radix - 2];
    radixl = radix;
    for(i = 1; i < k; i++)
        radixl *= radix;
    return radixl;
}

static void bf_integer_from_radix_rec(bf_t *r, const limb_t *tab,
                                      limb_t n, int level, limb_t n0,
                                      limb_t radix, bf_t *pow_tab)
{
    if (n == 1) {
        bf_set_ui(r, tab[0]);
    } else {
        bf_t T_s, *T = &T_s, *B;
        limb_t n1, n2;
        
        n2 = (((n0 * 2) >> (level + 1)) + 1) / 2;
        n1 = n - n2;
        //  Printf(“Level=%d N0=%ldn1=%ldn2=%ld\n”，Level，N0，n1，n2)；
        B = &pow_tab[level];
        if (B->len == 0) {
            bf_pow_ui_ui(B, radix, n2, BF_PREC_INF, BF_RNDZ);
        }
        bf_integer_from_radix_rec(r, tab + n2, n1, level + 1, n0,
                                  radix, pow_tab);
        bf_mul(r, r, B, BF_PREC_INF, BF_RNDZ);
        bf_init(r->ctx, T);
        bf_integer_from_radix_rec(T, tab, n2, level + 1, n0,
                                  radix, pow_tab);
        bf_add(r, r, T, BF_PREC_INF, BF_RNDZ);
        bf_delete(T);
    }
    //  Bf_print_str(“r=”，r)；
}

static void bf_integer_from_radix(bf_t *r, const limb_t *tab,
                                 limb_t n, limb_t radix)
{
    bf_context_t *s = r->ctx;
    int pow_tab_len, i;
    limb_t radixl;
    bf_t *pow_tab;
    
    radixl = get_limb_radix(radix);
    pow_tab_len = ceil_log2(n) + 2; /*  XXX：检查。 */ 
    pow_tab = bf_malloc(s, sizeof(pow_tab[0]) * pow_tab_len);
    for(i = 0; i < pow_tab_len; i++)
        bf_init(r->ctx, &pow_tab[i]);
    bf_integer_from_radix_rec(r, tab, n, 0, n, radixl, pow_tab);
    for(i = 0; i < pow_tab_len; i++) {
        bf_delete(&pow_tab[i]);
    }
    bf_free(s, pow_tab);
}

/*  计算并舍入T*基数^EXPN。 */ 
int bf_mul_pow_radix(bf_t *r, const bf_t *T, limb_t radix,
                     slimb_t expn, limb_t prec, bf_flags_t flags)
{
    int ret, expn_sign, overflow;
    slimb_t e, extra_bits, prec1, ziv_extra_bits;
    bf_t B_s, *B = &B_s;

    if (T->len == 0) {
        bf_set(r, T);
        return 0;
    } else if (expn == 0) {
        bf_set(r, T);
        return bf_round(r, prec, flags);
    }

    e = expn;
    expn_sign = 0;
    if (e < 0) {
        e = -e;
        expn_sign = 1;
    }
    bf_init(r->ctx, B);
    if (prec == BF_PREC_INF) {
        /*  无限精度：仅在已知结果准确的情况下使用。 */ 
        bf_pow_ui_ui(B, radix, e, BF_PREC_INF, BF_RNDN);
        if (expn_sign) {
            ret = bf_div(r, T, B, T->len * LIMB_BITS, BF_RNDN);
        } else {
            ret = bf_mul(r, T, B, BF_PREC_INF, BF_RNDN);
        }
    } else {
        ziv_extra_bits = 16;
        for(;;) {
            prec1 = prec + ziv_extra_bits;
            /*  XXX：正确的溢出/下溢处理。 */ 
            /*  XXX：需要严格的错误分析。 */ 
            extra_bits = ceil_log2(e) * 2 + 1;
            ret = bf_pow_ui_ui(B, radix, e, prec1 + extra_bits, BF_RNDN);
            overflow = !bf_is_finite(B);
            /*  XXX：如果bf_pow_ui_ui返回确切的结果，则可以停止在下一次操作之后。 */ 
            if (expn_sign)
                ret |= bf_div(r, T, B, prec1 + extra_bits, BF_RNDN);
            else
                ret |= bf_mul(r, T, B, prec1 + extra_bits, BF_RNDN);
            if ((ret & BF_ST_INEXACT) &&
                !bf_can_round(r, prec, flags & BF_RND_MASK, prec1) &&
                !overflow) {
                /*  以及更高的精确度和重试。 */ 
                ziv_extra_bits = ziv_extra_bits  + (ziv_extra_bits / 2);
            } else {
                ret = bf_round(r, prec, flags) | (ret & BF_ST_INEXACT);
                break;
            }
        }
    }
    bf_delete(B);
    return ret;
}

static inline int to_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    else
        return 36;
}

/*  在‘pos’处添加肢体，并减少pos。如果需要，将创建新空间。 */ 
static void bf_add_limb(bf_t *a, slimb_t *ppos, limb_t v)
{
    slimb_t pos;
    pos = *ppos;
    if (unlikely(pos < 0)) {
        limb_t new_size, d;
        new_size = bf_max(a->len + 1, a->len * 3 / 2);
        a->tab = bf_realloc(a->ctx, a->tab, sizeof(limb_t) * new_size);
        d = new_size - a->len;
        memmove(a->tab + d, a->tab, a->len * sizeof(limb_t));
        a->len = new_size;
        pos += d;
    }
    a->tab[pos--] = v;
    *ppos = pos;
}

static int bf_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    return c;
}

static int strcasestart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (bf_tolower(*p) != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/*  RETURN(状态，n，EXP)。‘Status’是浮点状态。“N”是解析后的数字。如果PREC=BF_PREC_INF：如果数字是整数或基数是2的幂，*p指数=0。否则，‘*pindex’是以‘基’为单位的指数。否则*指数=0。 */ 
int bf_atof2(bf_t *r, slimb_t *pexponent,
             const char *str, const char **pnext, int radix,
             limb_t prec, bf_flags_t flags)
{
    const char *p, *p_start;
    int is_neg, radix_bits, exp_is_neg, ret, digits_per_limb, shift, sep;
    int ret_legacy_octal = 0;
    limb_t cur_limb;
    slimb_t pos, expn, int_len, digit_count;
    BOOL has_decpt, is_bin_exp, is_float;
    bf_t a_s, *a;
    
    /*  数字之间可选的分隔符。 */ 
    sep = (flags & BF_ATOF_UNDERSCORE_SEP) ? '_' : 256;
    
    *pexponent = 0;
    p = str;
    if (!(flags & (BF_ATOF_INT_ONLY | BF_ATOF_JS_QUIRKS)) &&
        radix <= 16 &&
        strcasestart(p, "nan", &p)) {
        bf_set_nan(r);
        ret = 0;
        goto done;
    }
    is_neg = 0;
    
    if (p[0] == '+') {
        p++;
        p_start = p;
        if (flags & BF_ATOF_NO_PREFIX_AFTER_SIGN)
            goto no_radix_prefix;
    } else if (p[0] == '-') {
        is_neg = 1;
        p++;
        p_start = p;
        if (flags & BF_ATOF_NO_PREFIX_AFTER_SIGN)
            goto no_radix_prefix;
    } else {
        p_start = p;
    }
    if (p[0] == '0') {
        if ((p[1] == 'x' || p[1] == 'X') &&
            (radix == 0 || radix == 16) &&
            !(flags & BF_ATOF_NO_HEX)) {
            radix = 16;
            p += 2;
        } else if ((p[1] == 'o' || p[1] == 'O') &&
                   radix == 0 && (flags & BF_ATOF_BIN_OCT)) {
            p += 2;
            radix = 8;
        } else if ((p[1] == 'b' || p[1] == 'B') &&
                   radix == 0 && (flags & BF_ATOF_BIN_OCT)) {
            p += 2;
            radix = 2;
        } else if ((p[1] >= '0' && p[1] <= '9') &&
                   radix == 0 && (flags & BF_ATOF_LEGACY_OCTAL)) {
            int i;
            ret_legacy_octal = BF_ATOF_ST_LEGACY_OCTAL;
            /*  旧版八进制文字中不允许使用分隔符。 */ 
            sep = 256;
            for (i = 1; (p[i] >= '0' && p[i] <= '7'); i++)
                continue;
            if (p[i] == '8' || p[i] == '9')
                goto no_prefix;
            p += 1;
            radix = 8;
        } else {
            /*  0后面不能跟分隔符。 */ 
            if (p[1] == sep) {
                p++;
                bf_set_zero(r, 0);
                ret = 0;
                if (flags & BF_ATOF_INT_PREC_INF)
                    ret |= BF_ATOF_ST_INTEGER;
                goto done;
            }
            goto no_prefix;
        }
        /*  前缀后面必须有一个数字。 */ 
        if (to_digit((uint8_t)*p) >= radix) {
            bf_set_nan(r);
            ret = 0;
            goto done;
        }
    no_prefix: ;
    } else {
    no_radix_prefix:
        if (!(flags & BF_ATOF_INT_ONLY) && radix <= 16 &&
            ((!(flags & BF_ATOF_JS_QUIRKS) && strcasestart(p, "inf", &p)) ||
             ((flags & BF_ATOF_JS_QUIRKS) && strstart(p, "Infinity", &p)))) {
            bf_set_inf(r, is_neg);
            ret = 0;
            goto done;
        }
    }
    
    if (radix == 0)
        radix = 10;
    if ((radix & (radix - 1)) != 0) {
        radix_bits = 0; /*  基不是2的幂。 */ 
        a = &a_s;
        bf_init(r->ctx, a);
    } else {
        radix_bits = ceil_log2(radix);
        a = r;
    }

    /*  跳过前导零。 */ 
    /*  Xxx：也可以跳过小数点后的零。 */ 
    while (*p == '0' || (*p == sep && to_digit(p[1]) < radix))
        p++;

    if (radix_bits) {
        shift = digits_per_limb = LIMB_BITS;
    } else {
        radix_bits = 0;
        shift = digits_per_limb = digits_per_limb_table[radix - 2];
    }
    cur_limb = 0;
    bf_resize(a, 1);
    pos = 0;
    has_decpt = FALSE;
    int_len = digit_count = 0;
    is_float = FALSE;
    for(;;) {
        limb_t c;
        if (*p == '.' && (p > p_start || to_digit(p[1]) < radix)) {
            if ((flags & BF_ATOF_INT_ONLY) ||
                (radix != 10 && (flags & BF_ATOF_ONLY_DEC_FLOAT)))
                break;
            if (has_decpt)
                break;
            is_float = TRUE;
            has_decpt = TRUE;
            int_len = digit_count;
            p++;
        }
        if (*p == sep && to_digit(p[1]) < radix)
            p++;
        c = to_digit(*p);
        if (c >= radix)
            break;
        digit_count++;
        p++;
        if (radix_bits) {
            shift -= radix_bits;
            if (shift <= 0) {
                cur_limb |= c >> (-shift);
                bf_add_limb(a, &pos, cur_limb);
                if (shift < 0)
                    cur_limb = c << (LIMB_BITS + shift);
                else
                    cur_limb = 0;
                shift += LIMB_BITS;
            } else {
                cur_limb |= c << shift;
            }
        } else {
            cur_limb = cur_limb * radix + c;
            shift--;
            if (shift == 0) {
                bf_add_limb(a, &pos, cur_limb);
                shift = digits_per_limb;
                cur_limb = 0;
            }
        }
    }
    if (!has_decpt)
        int_len = digit_count;

    /*  添加最后一条肢体，并用零填充。 */ 
    if (shift != digits_per_limb) {
        if (radix_bits == 0) {
            while (shift != 0) {
                cur_limb *= radix;
                shift--;
            }
        }
        bf_add_limb(a, &pos, cur_limb);
    }
            
    /*  将下一个肢体重置为零(我们更愿意在重整化)。 */ 
    memset(a->tab, 0, (pos + 1) * sizeof(limb_t));

    if (p == p_start)
        goto error;

    /*  解析指数(如果有的话)。 */ 
    expn = 0;
    is_bin_exp = FALSE;
    if (!(flags & BF_ATOF_INT_ONLY) && 
        !(radix != 10 && (flags & BF_ATOF_ONLY_DEC_FLOAT)) &&
        ((radix == 10 && (*p == 'e' || *p == 'E')) ||
         (radix != 10 && (*p == '@' ||
                          (radix_bits && (*p == 'p' || *p == 'P'))))) &&
        p > p_start) {
        is_bin_exp = (*p == 'p' || *p == 'P');
        p++;
        is_float = TRUE;
        exp_is_neg = 0;
        if (*p == '+') {
            p++;
        } else if (*p == '-') {
            exp_is_neg = 1;
            p++;
        }
        for(;;) {
            int c;
            if (*p == sep && to_digit(p[1]) < 10)
                p++;
            c = to_digit(*p);
            if (c >= 10)
                break;
            if (unlikely(expn > ((EXP_MAX - 2 - 9) / 10))) {
                /*  指数溢出。 */ 
                if (exp_is_neg) {
                    bf_set_zero(r, is_neg);
                    ret = BF_ST_UNDERFLOW | BF_ST_INEXACT;
                } else {
                    bf_set_inf(r, is_neg);
                    ret = BF_ST_OVERFLOW | BF_ST_INEXACT;
                }
                goto done;
            }
            p++;
            expn = expn * 10 + c;
        }
        if (exp_is_neg)
            expn = -expn;
    } else if (!is_float) {
        if (*p == 'n' && (flags & BF_ATOF_INT_N_SUFFIX)) {
            p++;
            prec = BF_PREC_INF;
        } else if (flags & BF_ATOF_INT_PREC_INF) {
            prec = BF_PREC_INF;
        } else {
            is_float = TRUE;
        }
    }
    if (radix_bits) {
        /*  XXX：可能溢出。 */ 
        if (!is_bin_exp)
            expn *= radix_bits; 
        a->expn = expn + (int_len * radix_bits);
        a->sign = is_neg;
        ret = bf_normalize_and_round(a, prec, flags);
    } else {
        limb_t l;
        pos++;
        l = a->len - pos; /*  四肢数。 */ 
        if (l == 0) {
            bf_set_zero(r, is_neg);
            ret = 0;
        } else {
            bf_t T_s, *T = &T_s;

            expn -= l * digits_per_limb - int_len;
            bf_init(r->ctx, T);
            bf_integer_from_radix(T, a->tab + pos, l, radix);
            T->sign = is_neg;
            if (prec == BF_PREC_INF && is_float) {
                /*  返回指数。 */ 
                *pexponent = expn;
                bf_set(r, T);
                ret = 0;
            } else {
                ret = bf_mul_pow_radix(r, T, radix, expn, prec, flags);
            }
            bf_delete(T);
        }
        bf_delete(a);
    }
    if (!is_float)
        ret |= BF_ATOF_ST_INTEGER;
 done:
    if (pnext)
        *pnext = p;
    return ret | ret_legacy_octal;
 error:
    if (!radix_bits)
        bf_delete(a);
    ret = 0;
    if (flags & BF_ATOF_NAN_IF_EMPTY)  {
        bf_set_nan(r);
    } else {
        bf_set_zero(r, 0);
        if (flags & BF_ATOF_INT_PREC_INF)
            ret |= BF_ATOF_ST_INTEGER;
    }
    goto done;
}

int bf_atof(bf_t *r, const char *str, const char **pnext, int radix,
            limb_t prec, bf_flags_t flags)
{
    slimb_t dummy_exp;
    return bf_atof2(r, &dummy_exp, str, pnext, radix, prec, flags);
}

/*  基数转换为基数。 */ 

#if LIMB_BITS == 64
#define RADIXL_10 UINT64_C(10000000000000000000)
#else
#define RADIXL_10 UINT64_C(1000000000)
#endif

static const uint32_t inv_log2_radix[BF_RADIX_MAX - 1][LIMB_BITS / 2 + 1] = {
#if LIMB_BITS == 32
{ 0x80000000, 0x00000000,},
{ 0x50c24e60, 0xd4d4f4a7,},
{ 0x40000000, 0x00000000,},
{ 0x372068d2, 0x0a1ee5ca,},
{ 0x3184648d, 0xb8153e7a,},
{ 0x2d983275, 0x9d5369c4,},
{ 0x2aaaaaaa, 0xaaaaaaab,},
{ 0x28612730, 0x6a6a7a54,},
{ 0x268826a1, 0x3ef3fde6,},
{ 0x25001383, 0xbac8a744,},
{ 0x23b46706, 0x82c0c709,},
{ 0x229729f1, 0xb2c83ded,},
{ 0x219e7ffd, 0xa5ad572b,},
{ 0x20c33b88, 0xda7c29ab,},
{ 0x20000000, 0x00000000,},
{ 0x1f50b57e, 0xac5884b3,},
{ 0x1eb22cc6, 0x8aa6e26f,},
{ 0x1e21e118, 0x0c5daab2,},
{ 0x1d9dcd21, 0x439834e4,},
{ 0x1d244c78, 0x367a0d65,},
{ 0x1cb40589, 0xac173e0c,},
{ 0x1c4bd95b, 0xa8d72b0d,},
{ 0x1bead768, 0x98f8ce4c,},
{ 0x1b903469, 0x050f72e5,},
{ 0x1b3b433f, 0x2eb06f15,},
{ 0x1aeb6f75, 0x9c46fc38,},
{ 0x1aa038eb, 0x0e3bfd17,},
{ 0x1a593062, 0xb38d8c56,},
{ 0x1a15f4c3, 0x2b95a2e6,},
{ 0x19d630dc, 0xcc7ddef9,},
{ 0x19999999, 0x9999999a,},
{ 0x195fec80, 0x8a609431,},
{ 0x1928ee7b, 0x0b4f22f9,},
{ 0x18f46acf, 0x8c06e318,},
{ 0x18c23246, 0xdc0a9f3d,},
#else
{ 0x80000000, 0x00000000, 0x00000000,},
{ 0x50c24e60, 0xd4d4f4a7, 0x021f57bc,},
{ 0x40000000, 0x00000000, 0x00000000,},
{ 0x372068d2, 0x0a1ee5ca, 0x19ea911b,},
{ 0x3184648d, 0xb8153e7a, 0x7fc2d2e1,},
{ 0x2d983275, 0x9d5369c4, 0x4dec1661,},
{ 0x2aaaaaaa, 0xaaaaaaaa, 0xaaaaaaab,},
{ 0x28612730, 0x6a6a7a53, 0x810fabde,},
{ 0x268826a1, 0x3ef3fde6, 0x23e2566b,},
{ 0x25001383, 0xbac8a744, 0x385a3349,},
{ 0x23b46706, 0x82c0c709, 0x3f891718,},
{ 0x229729f1, 0xb2c83ded, 0x15fba800,},
{ 0x219e7ffd, 0xa5ad572a, 0xe169744b,},
{ 0x20c33b88, 0xda7c29aa, 0x9bddee52,},
{ 0x20000000, 0x00000000, 0x00000000,},
{ 0x1f50b57e, 0xac5884b3, 0x70e28eee,},
{ 0x1eb22cc6, 0x8aa6e26f, 0x06d1a2a2,},
{ 0x1e21e118, 0x0c5daab1, 0x81b4f4bf,},
{ 0x1d9dcd21, 0x439834e3, 0x81667575,},
{ 0x1d244c78, 0x367a0d64, 0xc8204d6d,},
{ 0x1cb40589, 0xac173e0c, 0x3b7b16ba,},
{ 0x1c4bd95b, 0xa8d72b0d, 0x5879f25a,},
{ 0x1bead768, 0x98f8ce4c, 0x66cc2858,},
{ 0x1b903469, 0x050f72e5, 0x0cf5488e,},
{ 0x1b3b433f, 0x2eb06f14, 0x8c89719c,},
{ 0x1aeb6f75, 0x9c46fc37, 0xab5fc7e9,},
{ 0x1aa038eb, 0x0e3bfd17, 0x1bd62080,},
{ 0x1a593062, 0xb38d8c56, 0x7998ab45,},
{ 0x1a15f4c3, 0x2b95a2e6, 0x46aed6a0,},
{ 0x19d630dc, 0xcc7ddef9, 0x5aadd61b,},
{ 0x19999999, 0x99999999, 0x9999999a,},
{ 0x195fec80, 0x8a609430, 0xe1106014,},
{ 0x1928ee7b, 0x0b4f22f9, 0x5f69791d,},
{ 0x18f46acf, 0x8c06e318, 0x4d2aeb2c,},
{ 0x18c23246, 0xdc0a9f3d, 0x3fe16970,},
#endif
};

static const limb_t log2_radix[BF_RADIX_MAX - 1] = {
#if LIMB_BITS == 32
0x20000000,
0x32b80347,
0x40000000,
0x4a4d3c26,
0x52b80347,
0x59d5d9fd,
0x60000000,
0x6570068e,
0x6a4d3c26,
0x6eb3a9f0,
0x72b80347,
0x766a008e,
0x79d5d9fd,
0x7d053f6d,
0x80000000,
0x82cc7edf,
0x8570068e,
0x87ef05ae,
0x8a4d3c26,
0x8c8ddd45,
0x8eb3a9f0,
0x90c10501,
0x92b80347,
0x949a784c,
0x966a008e,
0x982809d6,
0x99d5d9fd,
0x9b74948f,
0x9d053f6d,
0x9e88c6b3,
0xa0000000,
0xa16bad37,
0xa2cc7edf,
0xa4231623,
0xa570068e,
#else
0x2000000000000000,
0x32b803473f7ad0f4,
0x4000000000000000,
0x4a4d3c25e68dc57f,
0x52b803473f7ad0f4,
0x59d5d9fd5010b366,
0x6000000000000000,
0x6570068e7ef5a1e8,
0x6a4d3c25e68dc57f,
0x6eb3a9f01975077f,
0x72b803473f7ad0f4,
0x766a008e4788cbcd,
0x79d5d9fd5010b366,
0x7d053f6d26089673,
0x8000000000000000,
0x82cc7edf592262d0,
0x8570068e7ef5a1e8,
0x87ef05ae409a0289,
0x8a4d3c25e68dc57f,
0x8c8ddd448f8b845a,
0x8eb3a9f01975077f,
0x90c10500d63aa659,
0x92b803473f7ad0f4,
0x949a784bcd1b8afe,
0x966a008e4788cbcd,
0x982809d5be7072dc,
0x99d5d9fd5010b366,
0x9b74948f5532da4b,
0x9d053f6d26089673,
0x9e88c6b3626a72aa,
0xa000000000000000,
0xa16bad3758efd873,
0xa2cc7edf592262d0,
0xa4231623369e78e6,
0xa570068e7ef5a1e8,
#endif
};

/*  计算b=log2(基数)的楼层(a*b)或天花板(a*b)B=1/log2(基数)。对于is_inv=0，不能保证严格的准确性当基数不是2的幂的时候。 */ 
slimb_t bf_mul_log2_radix(slimb_t a1, unsigned int radix, int is_inv,
                          int is_ceil1)
{
    int is_neg;
    limb_t a;
    BOOL is_ceil;

    is_ceil = is_ceil1;
    a = a1;
    if (a1 < 0) {
        a = -a;
        is_neg = 1;
    } else {
        is_neg = 0;
    }
    is_ceil ^= is_neg;
    if ((radix & (radix - 1)) == 0) {
        int radix_bits;
        /*  基数是2的幂。 */ 
        radix_bits = ceil_log2(radix);
        if (is_inv) {
            if (is_ceil)
                a += radix_bits - 1;
            a = a / radix_bits;
        } else {
            a = a * radix_bits;
        }
    } else {
        const uint32_t *tab;
        limb_t b0, b1;
        dlimb_t t;
        
        if (is_inv) {
            tab = inv_log2_radix[radix - 2];
#if LIMB_BITS == 32
            b1 = tab[0];
            b0 = tab[1];
#else
            b1 = ((limb_t)tab[0] << 32) | tab[1];
            b0 = (limb_t)tab[2] << 32;
#endif
            t = (dlimb_t)b0 * (dlimb_t)a;
            t = (dlimb_t)b1 * (dlimb_t)a + (t >> LIMB_BITS);
            a = t >> (LIMB_BITS - 1);
        } else {
            b0 = log2_radix[radix - 2];
            t = (dlimb_t)b0 * (dlimb_t)a;
            a = t >> (LIMB_BITS - 3);
        }
        /*  A=FLOOR(结果)和‘RESULT’不能是整数。 */ 
        a += is_ceil;
    }
    if (is_neg)
        a = -a;
    return a;
}

/*  “N”是输出分支的数量。 */ 
static void bf_integer_to_radix_rec(bf_t *pow_tab,
                                    limb_t *out, const bf_t *a, limb_t n,
                                    int level, limb_t n0, limb_t radixl,
                                    unsigned int radixl_bits)
{
    limb_t n1, n2, q_prec;
    assert(n >= 1);
    if (n == 1) {
        out[0] = get_bits(a->tab, a->len, a->len * LIMB_BITS - a->expn);
    } else if (n == 2) {
        dlimb_t t;
        slimb_t pos;
        pos = a->len * LIMB_BITS - a->expn;
        t = ((dlimb_t)get_bits(a->tab, a->len, pos + LIMB_BITS) << LIMB_BITS) |
            get_bits(a->tab, a->len, pos);
        if (likely(radixl == RADIXL_10)) {
            /*  如果可能，请使用常量除法。 */ 
            out[0] = t % RADIXL_10;
            out[1] = t / RADIXL_10;
        } else {
            out[0] = t % radixl;
            out[1] = t / radixl;
        }
    } else {
        bf_t Q, R, *B, *B_inv;
        int q_add;
        bf_init(a->ctx, &Q);
        bf_init(a->ctx, &R);
        n2 = (((n0 * 2) >> (level + 1)) + 1) / 2;
        n1 = n - n2;
        B = &pow_tab[2 * level];
        B_inv = &pow_tab[2 * level + 1];
        if (B->len == 0) {
            /*  计算基数^n2。 */ 
            bf_pow_ui_ui(B, radixl, n2, BF_PREC_INF, BF_RNDZ);
            /*  我们为最大可能的‘n1’值使用足够的位，即n2+1。 */ 
            bf_recip(B_inv, B, (n2 + 1) * radixl_bits + 2);
        }
        //  Printf(“%d：n1=%”PRId64“n2=%”PRId64“\n”，Level，n1，n2)；
        q_prec = n1 * radixl_bits;
        bf_mul(&Q, a, B_inv, q_prec, BF_RNDN);
        bf_rint(&Q, BF_PREC_INF, BF_RNDZ);
        
        bf_mul(&R, &Q, B, BF_PREC_INF, BF_RNDZ);
        bf_sub(&R, a, &R, BF_PREC_INF, BF_RNDZ);
        /*  必要时调整。 */ 
        q_add = 0;
        while (R.sign && R.len != 0) {
            bf_add(&R, &R, B, BF_PREC_INF, BF_RNDZ);
            q_add--;
        }
        while (bf_cmpu(&R, B) >= 0) {
            bf_sub(&R, &R, B, BF_PREC_INF, BF_RNDZ);
            q_add++;
        }
        if (q_add != 0) {
            bf_add_si(&Q, &Q, q_add, BF_PREC_INF, BF_RNDZ);
        }
        bf_integer_to_radix_rec(pow_tab, out + n2, &Q, n1, level + 1, n0,
                                radixl, radixl_bits);
        bf_integer_to_radix_rec(pow_tab, out, &R, n2, level + 1, n0,
                                radixl, radixl_bits);
        bf_delete(&Q);
        bf_delete(&R);
    }
}

static void bf_integer_to_radix(bf_t *r, const bf_t *a, limb_t radixl)
{
    bf_context_t *s = r->ctx;
    limb_t r_len;
    bf_t *pow_tab;
    int i, pow_tab_len;
    
    r_len = r->len;
    pow_tab_len = (ceil_log2(r_len) + 2) * 2; /*  XXX：检查。 */ 
    pow_tab = bf_malloc(s, sizeof(pow_tab[0]) * pow_tab_len);
    for(i = 0; i < pow_tab_len; i++)
        bf_init(r->ctx, &pow_tab[i]);

    bf_integer_to_radix_rec(pow_tab, r->tab, a, r_len, 0, r_len, radixl,
                            ceil_log2(radixl));

    for(i = 0; i < pow_tab_len; i++) {
        bf_delete(&pow_tab[i]);
    }
    bf_free(s, pow_tab);
}

/*  A必须&gt;=0。“p”是所需的基数位数。‘基数’。‘R’是表示为整数的尾数。*PE包含指数。 */ 
static void bf_convert_to_radix(bf_t *r, slimb_t *pE,
                                const bf_t *a, int radix,
                                limb_t P, bf_rnd_t rnd_mode,
                                BOOL is_fixed_exponent)
{
    slimb_t E, e, prec, extra_bits, ziv_extra_bits, prec0;
    bf_t B_s, *B = &B_s;
    int e_sign, ret, res;
    
    if (a->len == 0) {
        /*  零案例。 */ 
        *pE = 0;
        bf_set(r, a);
        return;
    }

    if (is_fixed_exponent) {
        E = *pE;
    } else {
        /*  计算新指数。 */ 
        E = 1 + bf_mul_log2_radix(a->expn - 1, radix, TRUE, FALSE);
    }
    //  Bf_print_str(“a”，a)；
    //  Printf(“E=%1d P=%1d基数=%d\n”，E，P，基数)；
    
    for(;;) {
        e = P - E;
        e_sign = 0;
        if (e < 0) {
            e = -e;
            e_sign = 1;
        }
        /*  注意：在这里，log2(基数)的精度并不重要。 */ 
        prec0 = bf_mul_log2_radix(P, radix, FALSE, TRUE);
        ziv_extra_bits = 16;
        for(;;) {
            prec = prec0 + ziv_extra_bits;
            /*  XXX：需要严格的错误分析。 */ 
            extra_bits = ceil_log2(e) * 2 + 1;
            ret = bf_pow_ui_ui(r, radix, e, prec + extra_bits, BF_RNDN);
            if (!e_sign)
                ret |= bf_mul(r, r, a, prec + extra_bits, BF_RNDN);
            else
                ret |= bf_div(r, a, r, prec + extra_bits, BF_RNDN);
            /*  如果结果不准确，请检查其是否安全四舍五入为整数。 */ 
            if ((ret & BF_ST_INEXACT) &&
                !bf_can_round(r, r->expn, rnd_mode, prec)) {
                /*  以及更高的精确度和重试。 */ 
                ziv_extra_bits = ziv_extra_bits  + (ziv_extra_bits / 2);
                continue;
            } else {
                bf_rint(r, BF_PREC_INF, rnd_mode);
                break;
            }
        }
        if (is_fixed_exponent)
            break;
        /*  检查结果是否为&lt;B^P。 */ 
        /*  XXX：先做一个快速近似测试？ */ 
        bf_init(r->ctx, B);
        bf_pow_ui_ui(B, radix, P, BF_PREC_INF, BF_RNDZ);
        res = bf_cmpu(r, B);
        bf_delete(B);
        if (res < 0)
            break;
        /*  尝试更大的指数。 */ 
        E++;
    }
    *pE = E;
}

static void limb_to_a(char *buf, limb_t n, unsigned int radix, int len)
{
    int digit, i;

    if (radix == 10) {
        /*  具有常数因子的特殊情况。 */ 
        for(i = len - 1; i >= 0; i--) {
            digit = (limb_t)n % 10;
            n = (limb_t)n / 10;
            buf[i] = digit + '0';
        }
    } else {
        for(i = len - 1; i >= 0; i--) {
            digit = (limb_t)n % radix;
            n = (limb_t)n / radix;
            if (digit < 10)
                digit += '0';
            else
                digit += 'a' - 10;
            buf[i] = digit;
        }
    }
}

/*  对于2个基数的幂。 */ 
static void limb_to_a2(char *buf, limb_t n, unsigned int radix_bits, int len)
{
    int digit, i;
    unsigned int mask;

    mask = (1 << radix_bits) - 1;
    for(i = len - 1; i >= 0; i--) {
        digit = n & mask;
        n >>= radix_bits;
        if (digit < 10)
            digit += '0';
        else
            digit += 'a' - 10;
        buf[i] = digit;
    }
}

/*  “a”必须是整数。圆点是一个 */ 
static void output_digits(DynBuf *s, const bf_t *a1, int radix, limb_t n_digits,
                          limb_t dot_pos)
{
    limb_t i, v, l;
    slimb_t pos, pos_incr;
    int digits_per_limb, buf_pos, radix_bits, first_buf_pos;
    char buf[65];
    bf_t a_s, *a;
    
    if ((radix & (radix - 1)) != 0) {
        limb_t n, radixl;

        digits_per_limb = digits_per_limb_table[radix - 2];
        radixl = get_limb_radix(radix);
        a = &a_s;
        bf_init(a1->ctx, a);
        n = (n_digits + digits_per_limb - 1) / digits_per_limb;
        bf_resize(a, n);
        bf_integer_to_radix(a, a1, radixl);
        radix_bits = 0;
        pos = n;
        pos_incr = 1;
        first_buf_pos = pos * digits_per_limb - n_digits;
    } else {
        a = (bf_t *)a1;
        radix_bits = ceil_log2(radix);
        digits_per_limb = LIMB_BITS / radix_bits;
        pos_incr = digits_per_limb * radix_bits;
        pos = a->len * LIMB_BITS - a->expn + n_digits * radix_bits;
        first_buf_pos = 0;
    }
    buf_pos = digits_per_limb;
    i = 0;
    while (i < n_digits) {
        if (buf_pos == digits_per_limb) {
            pos -= pos_incr;
            if (radix_bits == 0) {
                v = get_limbz(a, pos);
                limb_to_a(buf, v, radix, digits_per_limb);
            } else {
                v = get_bits(a->tab, a->len, pos);
                limb_to_a2(buf, v, radix_bits, digits_per_limb);
            }
            buf_pos = first_buf_pos;
            first_buf_pos = 0;
        }
        if (i < dot_pos) {
            l = dot_pos;
        } else {
            if (i == dot_pos)
                dbuf_putc(s, '.');
            l = n_digits;
        }
        l = bf_min(digits_per_limb - buf_pos, l - i);
        dbuf_put(s, (uint8_t *)(buf + buf_pos), l);
        buf_pos += l;
        i += l;
    }
    if (a != a1)
        bf_delete(a);
}

static void *bf_dbuf_realloc(void *opaque, void *ptr, size_t size)
{
    bf_context_t *s = opaque;
    return bf_realloc(s, ptr, size);
}

/*  以字节为单位返回长度。添加了尾随的‘\0’ */ 
size_t bf_ftoa(char **pbuf, const bf_t *a2, int radix, limb_t prec,
               bf_flags_t flags)
{
    DynBuf s_s, *s = &s_s;
    int radix_bits;
    
    //  Bf_print_str(“ftoa”，a2)；
    //  Printf(“基数=%d\n”，基数)；
    dbuf_init2(s, a2->ctx, bf_dbuf_realloc);
    if (a2->expn == BF_EXP_NAN) {
        dbuf_putstr(s, "NaN");
    } else {
        if (a2->sign)
            dbuf_putc(s, '-');
        if (a2->expn == BF_EXP_INF) {
            if (flags & BF_FTOA_JS_QUIRKS)
                dbuf_putstr(s, "Infinity");
            else
                dbuf_putstr(s, "Inf");
        } else {
            int fmt;
            slimb_t n_digits, n, i, n_max, n1;
            bf_t a1_s, *a1;
            bf_t a_s, *a = &a_s;

            /*  做一个正数。 */ 
            a->tab = a2->tab;
            a->len = a2->len;
            a->expn = a2->expn;
            a->sign = 0;
            
            if ((radix & (radix - 1)) != 0)
                radix_bits = 0;
            else
                radix_bits = ceil_log2(radix);

            fmt = flags & BF_FTOA_FORMAT_MASK;
            a1 = &a1_s;
            bf_init(a2->ctx, a1);
            if (fmt == BF_FTOA_FORMAT_FRAC) {
                size_t pos, start;
                /*  再四舍五入一位数。 */ 
                n = 1 + bf_mul_log2_radix(bf_max(a->expn, 0), radix, TRUE, TRUE);
                n_digits = n + prec;
                n1 = n;
                bf_convert_to_radix(a1, &n1, a, radix, n_digits,
                                    flags & BF_RND_MASK, TRUE);
                start = s->size;
                output_digits(s, a1, radix, n_digits, n);
                /*  删除前导零，因为我们多分配了一个数字。 */ 
                pos = start;
                while ((pos + 1) < s->size && s->buf[pos] == '0' &&
                       s->buf[pos + 1] != '.')
                    pos++;
                if (pos > start) {
                    memmove(s->buf + start, s->buf + pos, s->size - pos);
                    s->size -= (pos - start);
                }
            } else {
                if (fmt == BF_FTOA_FORMAT_FIXED) {
                    n_digits = prec;
                    n_max = n_digits;
                } else {
                    slimb_t n_digits_max, n_digits_min;
                    
                    if (prec == BF_PREC_INF) {
                        assert(radix_bits != 0);
                        /*  XXX：可以使用准确的位数。 */ 
                        prec = a->len * LIMB_BITS;
                    }
                    n_digits = 1 + bf_mul_log2_radix(prec, radix, TRUE, TRUE);
                    /*  非指数的最大位数记数法。理性的做法是制定同样的规则作为JS，即对于以10为基数的64位浮点数，n_max=21。 */ 
                    n_max = n_digits + 4;
                    if (fmt == BF_FTOA_FORMAT_FREE_MIN) {
                        bf_t b_s, *b = &b_s;
                        
                        /*  通过以下方式查找最小位数一分为二。 */ 
                        n_digits_max = n_digits;
                        n_digits_min = 1;
                        bf_init(a2->ctx, b);
                        while (n_digits_min < n_digits_max) {
                            n_digits = (n_digits_min + n_digits_max) / 2;
                            bf_convert_to_radix(a1, &n, a, radix, n_digits,
                                                flags & BF_RND_MASK, FALSE);
                            /*  转换回数字并进行比较。 */ 
                            bf_mul_pow_radix(b, a1, radix, n - n_digits,
                                             prec,
                                             (flags & ~BF_RND_MASK) |
                                             BF_RNDN);
                            if (bf_cmpu(b, a) == 0) {
                                n_digits_max = n_digits;
                            } else {
                                n_digits_min = n_digits + 1;
                            }
                        }
                        bf_delete(b);
                        n_digits = n_digits_max;
                    }
                }
                bf_convert_to_radix(a1, &n, a, radix, n_digits,
                                    flags & BF_RND_MASK, FALSE);
                if (a1->expn == BF_EXP_ZERO &&
                    fmt != BF_FTOA_FORMAT_FIXED &&
                    !(flags & BF_FTOA_FORCE_EXP)) {
                    /*  只需输出零。 */ 
                    dbuf_putstr(s, "0");
                } else {
                    if (flags & BF_FTOA_ADD_PREFIX) {
                        if (radix == 16)
                            dbuf_putstr(s, "0x");
                        else if (radix == 8)
                            dbuf_putstr(s, "0o");
                        else if (radix == 2)
                            dbuf_putstr(s, "0b");
                    }
                    if (a1->expn == BF_EXP_ZERO)
                        n = 1;
                    if ((flags & BF_FTOA_FORCE_EXP) ||
                        n <= -6 || n > n_max) {
                        const char *fmt;
                        /*  指数记数法。 */ 
                        output_digits(s, a1, radix, n_digits, 1);
                        if (radix_bits != 0 && radix <= 16) {
                            if (flags & BF_FTOA_JS_QUIRKS)
                                fmt = "p%+" PRId_LIMB;
                            else
                                fmt = "p%" PRId_LIMB;
                            dbuf_printf(s, fmt, (n - 1) * radix_bits);
                        } else {
                            if (flags & BF_FTOA_JS_QUIRKS)
                                fmt = "%c%+" PRId_LIMB;
                            else
                                fmt = "%c%" PRId_LIMB;
                            dbuf_printf(s, fmt,
                                        radix <= 10 ? 'e' : '@', n - 1);
                        }
                    } else if (n <= 0) {
                        /*  0.x。 */ 
                        dbuf_putstr(s, "0.");
                        for(i = 0; i < -n; i++) {
                            dbuf_putc(s, '0');
                        }
                        output_digits(s, a1, radix, n_digits, n_digits);
                    } else {
                        if (n_digits <= n) {
                            /*  没有圆点。 */ 
                            output_digits(s, a1, radix, n_digits, n_digits);
                            for(i = 0; i < (n - n_digits); i++)
                                dbuf_putc(s, '0');
                        } else {
                            output_digits(s, a1, radix, n_digits, n);
                        }
                    }
                }
            }
            bf_delete(a1);
        }
    }
    dbuf_putc(s, '\0');
    *pbuf = (char *)s->buf;
    return s->size - 1;
}

/*  *************************************************************。 */ 
/*  超越函数。 */ 

/*  注：算法来自MPFR。 */ 
static void bf_const_log2_rec(bf_t *T, bf_t *P, bf_t *Q, limb_t n1,
                              limb_t n2, BOOL need_P)
{
    bf_context_t *s = T->ctx;
    if ((n2 - n1) == 1) {
        if (n1 == 0) {
            bf_set_ui(P, 3);
        } else {
            bf_set_ui(P, n1);
            P->sign = 1;
        }
        bf_set_ui(Q, 2 * n1 + 1);
        Q->expn += 2;
        bf_set(T, P);
    } else {
        limb_t m;
        bf_t T1_s, *T1 = &T1_s;
        bf_t P1_s, *P1 = &P1_s;
        bf_t Q1_s, *Q1 = &Q1_s;
        
        m = n1 + ((n2 - n1) >> 1);
        bf_const_log2_rec(T, P, Q, n1, m, TRUE);
        bf_init(s, T1);
        bf_init(s, P1);
        bf_init(s, Q1);
        bf_const_log2_rec(T1, P1, Q1, m, n2, need_P);
        bf_mul(T, T, Q1, BF_PREC_INF, BF_RNDZ);
        bf_mul(T1, T1, P, BF_PREC_INF, BF_RNDZ);
        bf_add(T, T, T1, BF_PREC_INF, BF_RNDZ);
        if (need_P)
            bf_mul(P, P, P1, BF_PREC_INF, BF_RNDZ);
        bf_mul(Q, Q, Q1, BF_PREC_INF, BF_RNDZ);
        bf_delete(T1);
        bf_delete(P1);
        bf_delete(Q1);
    }
}

/*  计算LOG(2)，精确地进行精确的舍入。 */ 
static void bf_const_log2_internal(bf_t *T, limb_t prec)
{
    limb_t w, N;
    bf_t P_s, *P = &P_s;
    bf_t Q_s, *Q = &Q_s;

    w = prec + 15;
    N = w / 3 + 1;
    bf_init(T->ctx, P);
    bf_init(T->ctx, Q);
    bf_const_log2_rec(T, P, Q, 0, N, FALSE);
    bf_div(T, T, Q, prec, BF_RNDN);
    bf_delete(P);
    bf_delete(Q);
}

/*  PI常量。 */ 

#define CHUD_A 13591409
#define CHUD_B 545140134
#define CHUD_C 640320
#define CHUD_BITS_PER_TERM 47

static void chud_bs(bf_t *P, bf_t *Q, bf_t *G, int64_t a, int64_t b, int need_g,
                    limb_t prec)
{
    bf_context_t *s = P->ctx;
    int64_t c;

    if (a == (b - 1)) {
        bf_t T0, T1;
        
        bf_init(s, &T0);
        bf_init(s, &T1);
        bf_set_ui(G, 2 * b - 1);
        bf_mul_ui(G, G, 6 * b - 1, prec, BF_RNDN);
        bf_mul_ui(G, G, 6 * b - 5, prec, BF_RNDN);
        bf_set_ui(&T0, CHUD_B);
        bf_mul_ui(&T0, &T0, b, prec, BF_RNDN);
        bf_set_ui(&T1, CHUD_A);
        bf_add(&T0, &T0, &T1, prec, BF_RNDN);
        bf_mul(P, G, &T0, prec, BF_RNDN);
        P->sign = b & 1;

        bf_set_ui(Q, b);
        bf_mul_ui(Q, Q, b, prec, BF_RNDN);
        bf_mul_ui(Q, Q, b, prec, BF_RNDN);
        bf_mul_ui(Q, Q, (uint64_t)CHUD_C * CHUD_C * CHUD_C / 24, prec, BF_RNDN);
        bf_delete(&T0);
        bf_delete(&T1);
    } else {
        bf_t P2, Q2, G2;
        
        bf_init(s, &P2);
        bf_init(s, &Q2);
        bf_init(s, &G2);

        c = (a + b) / 2;
        chud_bs(P, Q, G, a, c, 1, prec);
        chud_bs(&P2, &Q2, &G2, c, b, need_g, prec);
        
        /*  Q=Q1*Q2。 */ 
        /*  G=G1*G2。 */ 
        /*  P=P1*Q2+P2*G1。 */ 
        bf_mul(&P2, &P2, G, prec, BF_RNDN);
        if (!need_g)
            bf_set_ui(G, 0);
        bf_mul(P, P, &Q2, prec, BF_RNDN);
        bf_add(P, P, &P2, prec, BF_RNDN);
        bf_delete(&P2);

        bf_mul(Q, Q, &Q2, prec, BF_RNDN);
        bf_delete(&Q2);
        if (need_g)
            bf_mul(G, G, &G2, prec, BF_RNDN);
        bf_delete(&G2);
    }
}

/*  计算PI与忠实的四舍五入精度‘prec’使用丘德诺夫斯基公式。 */ 
static void bf_const_pi_internal(bf_t *Q, limb_t prec)
{
    bf_context_t *s = Q->ctx;
    int64_t n, prec1;
    bf_t P, G;

    /*  系列术语数。 */ 
    n = prec / CHUD_BITS_PER_TERM + 1;
    /*  XXX：精确度分析。 */ 
    prec1 = prec + 32;

    bf_init(s, &P);
    bf_init(s, &G);

    chud_bs(&P, Q, &G, 0, n, 0, BF_PREC_INF);
    
    bf_mul_ui(&G, Q, CHUD_A, prec1, BF_RNDN);
    bf_add(&P, &G, &P, prec1, BF_RNDN);
    bf_div(Q, Q, &P, prec1, BF_RNDF);
 
    bf_set_ui(&P, CHUD_C / 64);
    bf_rsqrt(&G, &P, prec1);
    bf_mul_ui(&G, &G, (uint64_t)CHUD_C * CHUD_C / (8 * 12), prec1, BF_RNDF);
    bf_mul(Q, Q, &G, prec, BF_RNDN);
    bf_delete(&P);
    bf_delete(&G);
}

static int bf_const_get(bf_t *T, limb_t prec, bf_flags_t flags,
                        BFConstCache *c,
                        void (*func)(bf_t *res, limb_t prec))
{
    limb_t ziv_extra_bits, prec1;

    ziv_extra_bits = 32;
    for(;;) {
        prec1 = prec + ziv_extra_bits;
        if (c->prec < prec1) {
            if (c->val.len == 0)
                bf_init(T->ctx, &c->val);
            func(&c->val, prec1);
            c->prec = prec1;
        } else {
            prec1 = c->prec;
        }
        bf_set(T, &c->val);
        if (!bf_can_round(T, prec, flags & BF_RND_MASK, prec1)) {
            /*  以及更高的精确度和重试。 */ 
            ziv_extra_bits = ziv_extra_bits  + (ziv_extra_bits / 2);
        } else {
            break;
        }
    }
    return bf_round(T, prec, flags);
}

static void bf_const_free(BFConstCache *c)
{
    bf_delete(&c->val);
    memset(c, 0, sizeof(*c));
}

int bf_const_log2(bf_t *T, limb_t prec, bf_flags_t flags)
{
    bf_context_t *s = T->ctx;
    return bf_const_get(T, prec, flags, &s->log2_cache, bf_const_log2_internal);
}

int bf_const_pi(bf_t *T, limb_t prec, bf_flags_t flags)
{
    bf_context_t *s = T->ctx;
    return bf_const_get(T, prec, flags, &s->pi_cache, bf_const_pi_internal);
}

void bf_clear_cache(bf_context_t *s)
{
#ifdef USE_FFT_MUL
    fft_clear_cache(s);
#endif
    bf_const_free(&s->log2_cache);
    bf_const_free(&s->pi_cache);
}

/*  ZivFunc应该使用忠实的四舍五入来计算结果‘r’精确度‘PREC’。为了提高效率，最终的bf_round()不需要在函数中完成。 */ 
typedef int ZivFunc(bf_t *r, const bf_t *a, limb_t prec, void *opaque);

static int bf_ziv_rounding(bf_t *r, const bf_t *a,
                           limb_t prec, bf_flags_t flags,
                           ZivFunc *f, void *opaque)
{
    int rnd_mode, ret;
    slimb_t prec1, ziv_extra_bits;
    
    rnd_mode = flags & BF_RND_MASK;
    if (rnd_mode == BF_RNDF) {
        /*  不需要迭代。 */ 
        f(r, a, prec, opaque);
        ret = 0;
    } else {
        ziv_extra_bits = 32;
        for(;;) {
            prec1 = prec + ziv_extra_bits;
            ret = f(r, a, prec1, opaque);
            if (ret & (BF_ST_OVERFLOW | BF_ST_UNDERFLOW)) {
                /*  永远不会发生，因为它表明舍入不能正确完成，但我们不能抓住所有的案例。 */ 
                return ret;
            }
            /*  如果结果准确，我们就可以停下来。 */ 
            if (!(ret & BF_ST_INEXACT)) {
                ret = 0;
                break;
            }
            if (bf_can_round(r, prec, rnd_mode, prec1)) {
                ret = BF_ST_INEXACT;
                break;
            }
            ziv_extra_bits = ziv_extra_bits * 2;
        }
    }
    return bf_round(r, prec, flags) | ret;
}

/*  计算指数使用忠实的四舍五入的精度‘prec’。注：算法来自MPFR。 */ 
static int bf_exp_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    slimb_t n, K, l, i, prec1;
    
    assert(r != a);

    /*  减少争论：T=a-n*log(2)，其中0&lt;=T&lt;log(2)且n为整数。 */ 
    bf_init(s, T);
    if (a->expn <= -1) {
        /*  0&lt;=abs(A)&lt;=0.5。 */ 
        if (a->sign)
            n = -1;
        else
            n = 0;
    } else {
        bf_const_log2(T, LIMB_BITS, BF_RNDZ);
        bf_div(T, a, T, LIMB_BITS, BF_RNDD);
        bf_get_limb(&n, T, 0);
    }

    K = bf_isqrt((prec + 1) / 2);
    l = (prec - 1) / K + 1;
    /*  XXX：精确分析？ */ 
    prec1 = prec + (K + 2 * l + 18) + K + 8;
    if (a->expn > 0)
        prec1 += a->expn;
    //  Print tf(“n=%ldK=%ldpr1=%ld\n”，n，K，pro1)；

    bf_const_log2(T, prec1, BF_RNDF);
    bf_mul_si(T, T, n, prec1, BF_RNDN);
    bf_sub(T, a, T, prec1, BF_RNDN);

    /*  缩小T的范围。 */ 
    bf_mul_2exp(T, -K, BF_PREC_INF, BF_RNDZ);
    
    /*  泰勒展开在零附近：1+x+x^2/2+...+x^n/n！=(1+x*(1+x/2*(1+...。(X/N)。 */ 
    {
        bf_t U_s, *U = &U_s;
        
        bf_init(s, U);
        bf_set_ui(r, 1);
        for(i = l ; i >= 1; i--) {
            bf_set_ui(U, i);
            bf_div(U, T, U, prec1, BF_RNDN);
            bf_mul(r, r, U, prec1, BF_RNDN);
            bf_add_si(r, r, 1, prec1, BF_RNDN);
        }
        bf_delete(U);
    }
    bf_delete(T);
    
    /*  撤消范围缩小。 */ 
    for(i = 0; i < K; i++) {
        bf_mul(r, r, r, prec1, BF_RNDN);
    }

    /*  撤消参数约简。 */ 
    bf_mul_2exp(r, n, BF_PREC_INF, BF_RNDZ);

    return BF_ST_INEXACT;
}

int bf_exp(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    bf_context_t *s = r->ctx;
    assert(r != a);
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
        } else if (a->expn == BF_EXP_INF) {
            if (a->sign)
                bf_set_zero(r, 0);
            else
                bf_set_inf(r, 0);
        } else {
            bf_set_ui(r, 1);
        }
        return 0;
    }

    /*  原油溢流和下溢试验。 */ 
    if (a->expn > 0) {
        bf_t T_s, *T = &T_s;
        bf_t log2_s, *log2 = &log2_s;
        slimb_t e_min, e_max;
        e_max = (limb_t)1 << (bf_get_exp_bits(flags) - 1);
        e_min = -e_max + 3;
        if (flags & BF_FLAG_SUBNORMAL)
            e_min -= (prec - 1);

        bf_init(s, T);
        bf_init(s, log2);
        bf_const_log2(log2, LIMB_BITS, BF_RNDU);
        bf_mul_ui(T, log2, e_max, LIMB_BITS, BF_RNDU);
        /*  A&gt;e_max*log(2)表示exp(A)&gt;e_max。 */ 
        if (bf_cmp_lt(T, a) > 0) {
            /*  溢出。 */ 
            bf_delete(T);
            bf_delete(log2);
            return bf_set_overflow(r, 0, prec, flags);
        }
        /*  &lt;e_min*log(2)表示exp(A)&lt;e_min。 */ 
        bf_mul_si(T, log2, e_min, LIMB_BITS, BF_RNDD);
        if (bf_cmp_lt(a, T)) {
            int rnd_mode = flags & BF_RND_MASK;

            /*  下溢。 */ 
            bf_delete(T);
            bf_delete(log2);
            if (rnd_mode == BF_RNDU) {
                /*  设置最小值。 */ 
                bf_set_ui(r, 1);
                r->expn = e_min;
            } else {
                bf_set_zero(r, 0);
            }
            return BF_ST_UNDERFLOW | BF_ST_INEXACT;
        }
        bf_delete(log2);
        bf_delete(T);
    }

    return bf_ziv_rounding(r, a, prec, flags, bf_exp_internal, NULL);
}

static int bf_log_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    bf_t U_s, *U = &U_s;
    bf_t V_s, *V = &V_s;
    slimb_t n, prec1, l, i, K;
    
    assert(r != a);

    bf_init(s, T);
    /*  参数缩减1。 */ 
    /*  T=a*2^n，2/3&lt;=T&lt;=4/3。 */ 
    {
        bf_t U_s, *U = &U_s;
        bf_set(T, a);
        n = T->expn;
        T->expn = 0;
        /*  U=~2/3。 */ 
        bf_init(s, U);
        bf_set_ui(U, 0xaaaaaaaa); 
        U->expn = 0;
        if (bf_cmp_lt(T, U)) {
            T->expn++;
            n--;
        }
        bf_delete(U);
    }
    //  Printf(“n=%ld\n”，n)；
    //  Bf_print_str(“T”，T)；

    /*  XXX：精确度分析。 */ 
    /*  参数约简的迭代次数2。 */ 
    K = bf_isqrt((prec + 1) / 2); 
    /*  泰勒展开的级数。 */ 
    l = prec / (2 * K) + 1; 
    /*  中间计算的精度。 */ 
    prec1 = prec + K + 2 * l + 32;

    bf_init(s, U);
    bf_init(s, V);
    
    /*  注：此处发生取消，因此我们使用更高的精度(XXX：通过计算精确抵消来降低精度)。 */ 
    bf_add_si(T, T, -1, BF_PREC_INF, BF_RNDN); 

    /*  论据缩减2。 */ 
    for(i = 0; i < K; i++) {
        /*  T=T/(1+SQRT(1+T))。 */ 
        bf_add_si(U, T, 1, prec1, BF_RNDN);
        bf_sqrt(V, U, prec1, BF_RNDF);
        bf_add_si(U, V, 1, prec1, BF_RNDN);
        bf_div(T, T, U, prec1, BF_RNDN);
    }

    {
        bf_t Y_s, *Y = &Y_s;
        bf_t Y2_s, *Y2 = &Y2_s;
        bf_init(s, Y);
        bf_init(s, Y2);

        /*  用y=x/(2+x)计算ln(1+x)=ln((1+y)/(1-y))=y+y^3/3+...+y^(2*l+1)/(2*l+1)Y=Y^2=y*(1+Y/3+Y^2/5+...)=y*(1+Y*(1/3+Y*(1/5+...)。 */ 
        bf_add_si(Y, T, 2, prec1, BF_RNDN);
        bf_div(Y, T, Y, prec1, BF_RNDN);

        bf_mul(Y2, Y, Y, prec1, BF_RNDN);
        bf_set_ui(r, 0);
        for(i = l; i >= 1; i--) {
            bf_set_ui(U, 1);
            bf_set_ui(V, 2 * i + 1);
            bf_div(U, U, V, prec1, BF_RNDN);
            bf_add(r, r, U, prec1, BF_RNDN);
            bf_mul(r, r, Y2, prec1, BF_RNDN);
        }
        bf_add_si(r, r, 1, prec1, BF_RNDN);
        bf_mul(r, r, Y, prec1, BF_RNDN);
        bf_delete(Y);
        bf_delete(Y2);
    }
    bf_delete(V);
    bf_delete(U);

    /*  将泰勒展开乘以2，然后撤消论据缩减2。 */ 
    bf_mul_2exp(r, K + 1, BF_PREC_INF, BF_RNDZ);
    
    /*  撤消参数约简1。 */ 
    bf_const_log2(T, prec1, BF_RNDF);
    bf_mul_si(T, T, n, prec1, BF_RNDN);
    bf_add(r, r, T, prec1, BF_RNDN);
    
    bf_delete(T);
    return BF_ST_INEXACT;
}

int bf_log(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    
    assert(r != a);
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else if (a->expn == BF_EXP_INF) {
            if (a->sign) {
                bf_set_nan(r);
                return BF_ST_INVALID_OP;
            } else {
                bf_set_inf(r, 0);
                return 0;
            }
        } else {
            bf_set_inf(r, 1);
            return 0;
        }
    }
    if (a->sign) {
        bf_set_nan(r);
        return BF_ST_INVALID_OP;
    }
    bf_init(s, T);
    bf_set_ui(T, 1);
    if (bf_cmp_eq(a, T)) {
        bf_set_zero(r, 0);
        bf_delete(T);
        return 0;
    }
    bf_delete(T);

    return bf_ziv_rounding(r, a, prec, flags, bf_log_internal, NULL);
}

/*  X和y有限且x&gt;0。 */ 
/*  XXX：溢出/下溢处理。 */ 
static int bf_pow_generic(bf_t *r, const bf_t *x, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    const bf_t *y = opaque;
    bf_t T_s, *T = &T_s;
    limb_t prec1;

    bf_init(s, T);
    /*  XXX：增加精度的证明。 */ 
    prec1 = prec + 32;
    bf_log(T, x, prec1, BF_RNDF);
    bf_mul(T, T, y, prec1, BF_RNDF);
    bf_exp(r, T, prec1, BF_RNDF);
    bf_delete(T);
    return BF_ST_INEXACT;
}

/*  X和y有限，x&gt;0，y整数，y适合一条腿。 */ 
/*  XXX：溢出/下溢处理。 */  
static int bf_pow_int(bf_t *r, const bf_t *x, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    const bf_t *y = opaque;
    bf_t T_s, *T = &T_s;
    limb_t prec1;
    int ret;
    slimb_t y1;
    
    bf_get_limb(&y1, y, 0);
    if (y1 < 0)
        y1 = -y1;
    /*  XXX：增加精度的证明。 */ 
    prec1 = prec + ceil_log2(y1) * 2 + 8;
    ret = bf_pow_ui(r, x, y1 < 0 ? -y1 : y1, prec1, BF_RNDN);
    if (y->sign) {
        bf_init(s, T);
        bf_set_ui(T, 1);
        ret |= bf_div(r, T, r, prec1, BF_RNDN);
        bf_delete(T);
    }
    return ret;
}

/*  X必须是有限的非零浮点数。如果存在浮点数r，如x=r^(2^n)，并返回此浮点数点编号‘r’。否则，返回FALSE，并且r未定义。 */ 
static BOOL check_exact_power2n(bf_t *r, const bf_t *x, slimb_t n)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    slimb_t e, i, er;
    limb_t v;
    
    /*  X=m*2^e，m个奇数。 */ 
    e = bf_get_exp_min(x);
    /*  快速检查指数。 */ 
    if (n > (LIMB_BITS - 1)) {
        if (e != 0)
            return FALSE;
        er = 0;
    } else {
        if ((e & (((limb_t)1 << n) - 1)) != 0)
            return FALSE;
        er = e >> n;
    }
    /*  每个完全奇数平方=1模8。 */ 
    v = get_bits(x->tab, x->len, x->len * LIMB_BITS - x->expn + e);
    if ((v & 7) != 1)
        return FALSE;

    bf_init(s, T);
    bf_set(T, x);
    T->expn -= e;
    for(i = 0; i < n; i++) {
        if (i != 0)
            bf_set(T, r);
        if (bf_sqrtrem(r, NULL, T) != 0)
            return FALSE;
    }
    r->expn += er;
    return TRUE;
}

/*  对于x和y整数和y&gt;=0，接受PREC=BF_PREC_INF。 */ 
int bf_pow(bf_t *r, const bf_t *x, const bf_t *y, limb_t prec, bf_flags_t flags)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    bf_t ytmp_s;
    BOOL y_is_int, y_is_odd;
    int r_sign, ret, rnd_mode;
    slimb_t y_emin;
    
    if (x->len == 0 || y->len == 0) {
        if (y->expn == BF_EXP_ZERO) {
            /*  POW(x，0)=1。 */ 
            bf_set_ui(r, 1);
        } else if (x->expn == BF_EXP_NAN) {
            bf_set_nan(r);
        } else {
            int cmp_x_abs_1;
            bf_set_ui(r, 1);
            cmp_x_abs_1 = bf_cmpu(x, r);
            if (cmp_x_abs_1 == 0 && (flags & BF_POW_JS_QUICKS) &&
                (y->expn >= BF_EXP_INF)) {
                bf_set_nan(r);
            } else if (cmp_x_abs_1 == 0 &&
                       (!x->sign || y->expn != BF_EXP_NAN)) {
                /*  POW(1，y)=1，即使y=NaN。 */ 
                /*  POW(-1，+/-inf)=1。 */ 
            } else if (y->expn == BF_EXP_NAN) {
                bf_set_nan(r);
            } else if (y->expn == BF_EXP_INF) {
                if (y->sign == (cmp_x_abs_1 > 0)) {
                    bf_set_zero(r, 0);
                } else {
                    bf_set_inf(r, 0);
                }
            } else {
                y_emin = bf_get_exp_min(y);
                y_is_odd = (y_emin == 0);
                if (y->sign == (x->expn == BF_EXP_ZERO)) {
                    bf_set_inf(r, y_is_odd & x->sign);
                    if (y->sign) {
                        /*  Y&lt;0的POW(0，y)。 */ 
                        return BF_ST_DIVIDE_ZERO;
                    }
                } else {
                    bf_set_zero(r, y_is_odd & x->sign);
                }
            }
        }
        return 0;
    }
    bf_init(s, T);
    bf_set(T, x);
    y_emin = bf_get_exp_min(y);
    y_is_int = (y_emin >= 0);
    rnd_mode = flags & BF_RND_MASK;
    if (x->sign) {
        if (!y_is_int) {
            bf_set_nan(r);
            bf_delete(T);
            return BF_ST_INVALID_OP;
        }
        y_is_odd = (y_emin == 0);
        r_sign = y_is_odd;
        /*  如果结果的符号为已更改。 */ 
        if (r_sign && (rnd_mode == BF_RNDD || rnd_mode == BF_RNDU))
            flags ^= 1;
        bf_neg(T);
    } else {
        r_sign = 0;
    }

    bf_set_ui(r, 1);
    if (bf_cmp_eq(T, r)) {
        /*  ABS(X)=1：无事可做。 */ 
        ret = 0;
    } else if (y_is_int) {
        slimb_t T_bits, e;
    int_pow:
        T_bits = T->expn - bf_get_exp_min(T);
        if (T_bits == 1) {
            /*  POW(2^b，y)=2^(b*y)。 */ 
            bf_mul_si(T, y, T->expn - 1, LIMB_BITS, BF_RNDZ);
            bf_get_limb(&e, T, 0);
            bf_set_ui(r, 1);
            ret = bf_mul_2exp(r, e, prec, flags);
        } else if (prec == BF_PREC_INF) {
            slimb_t y1;
            /*  无限精度的特殊情况(整数情况)。 */ 
            bf_get_limb(&y1, y, 0);
            assert(!y->sign);
            /*  X必须是整数，因此abs(X)&gt;=2。 */ 
            if (y1 >= ((slimb_t)1 << BF_EXP_BITS_MAX)) {
                bf_delete(T);
                return bf_set_overflow(r, 0, BF_PREC_INF, flags);
            }
            ret = bf_pow_ui(r, T, y1, BF_PREC_INF, BF_RNDZ);
        } else {
            if (y->expn <= 31) {
                /*  足够小的功率：在所有情况下都使用幂运算。 */ 
            } else if (y->sign) {
                /*  不能准确。 */ 
                goto general_case;
            } else {
                if (rnd_mode == BF_RNDF)
                    goto general_case; /*  不需要跟踪准确的结果。 */ 
                /*  看看结果是否有机会准确：如果x=a*2^b(奇数)，则x^y=a^y*2^(b*y)X^y需要至少Floor_log2(A)*y位的精度。 */ 
                bf_mul_si(r, y, T_bits - 1, LIMB_BITS, BF_RNDZ);
                bf_get_limb(&e, r, 0);
                if (prec < e)
                    goto general_case;
            }
            ret = bf_ziv_rounding(r, T, prec, flags, bf_pow_int, (void *)y);
        }
    } else {
        if (rnd_mode != BF_RNDF) {
            bf_t *y1;
            if (y_emin < 0 && check_exact_power2n(r, T, -y_emin)) {
                /*  这个问题归结为一个整数的幂。 */ 
#if 0
                printf("\nn=%ld\n", -y_emin);
                bf_print_str("T", T);
                bf_print_str("r", r);
#endif
                bf_set(T, r);
                y1 = &ytmp_s;
                y1->tab = y->tab;
                y1->len = y->len;
                y1->sign = y->sign;
                y1->expn = y->expn - y_emin;
                y = y1;
                goto int_pow;
            }
        }
    general_case:
        ret = bf_ziv_rounding(r, T, prec, flags, bf_pow_generic, (void *)y);
    }
    bf_delete(T);
    r->sign = r_sign;
    return ret;
}

/*  计算SQRT(-2*x-x^2)，从cos(X)-1得到|sin(X)|。 */ 
static void bf_sqrt_sin(bf_t *r, const bf_t *x, limb_t prec1)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    bf_init(s, T);
    bf_set(T, x);
    bf_mul(r, T, T, prec1, BF_RNDN);
    bf_mul_2exp(T, 1, BF_PREC_INF, BF_RNDZ);
    bf_add(T, T, r, prec1, BF_RNDN);
    bf_neg(T);
    bf_sqrt(r, T, prec1, BF_RNDF);
    bf_delete(T);
}

int bf_sincos(bf_t *s, bf_t *c, const bf_t *a, limb_t prec)
{
    bf_context_t *s1 = a->ctx;
    bf_t T_s, *T = &T_s;
    bf_t U_s, *U = &U_s;
    bf_t r_s, *r = &r_s;
    slimb_t K, prec1, i, l, mod, prec2;
    int is_neg;
    
    assert(c != a && s != a);
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            if (c)
                bf_set_nan(c);
            if (s)
                bf_set_nan(s);
            return 0;
        } else if (a->expn == BF_EXP_INF) {
            if (c)
                bf_set_nan(c);
            if (s)
                bf_set_nan(s);
            return BF_ST_INVALID_OP;
        } else {
            if (c)
                bf_set_ui(c, 1);
            if (s)
                bf_set_zero(s, a->sign);
            return 0;
        }
    }

    bf_init(s1, T);
    bf_init(s1, U);
    bf_init(s1, r);
    
    /*  XXX：精确度分析。 */ 
    K = bf_isqrt(prec / 2);
    l = prec / (2 * K) + 1;
    prec1 = prec + 2 * K + l + 8;
    
    /*  模减后，-pi/4&lt;=T&lt;=pi/4。 */ 
    if (a->expn <= -1) {
        /*  ABS(A)&lt;=0.25：无需模数降阶。 */ 
        bf_set(T, a);
        mod = 0;
    } else {
        slimb_t cancel;
        cancel = 0;
        for(;;) {
            prec2 = prec1 + cancel;
            bf_const_pi(U, prec2, BF_RNDF);
            bf_mul_2exp(U, -1, BF_PREC_INF, BF_RNDZ);
            bf_remquo(&mod, T, a, U, prec2, BF_RNDN);
            //  Print tf(“T.exn=%ldPRE2=%ld\n”，T-&gt;EXPN，PREP2)；
            if (mod == 0 || (T->expn != BF_EXP_ZERO &&
                             (T->expn + prec2) >= (prec1 - 1)))
                break;
            /*  增加位数，直到精度足够好。 */ 
            cancel = bf_max(-T->expn, (cancel + 1) * 3 / 2);
        }
        mod &= 3;
    }
    
    is_neg = T->sign;
        
    /*  计算Cosm1(X)=cos(X)-1。 */ 
    bf_mul(T, T, T, prec1, BF_RNDN);
    bf_mul_2exp(T, -2 * K, BF_PREC_INF, BF_RNDZ);
    
    /*  泰勒展开：-x^2/2+x^4/4！-x^6/6！+...。 */ 
    bf_set_ui(r, 1);
    for(i = l ; i >= 1; i--) {
        bf_set_ui(U, 2 * i - 1);
        bf_mul_ui(U, U, 2 * i, BF_PREC_INF, BF_RNDZ);
        bf_div(U, T, U, prec1, BF_RNDN);
        bf_mul(r, r, U, prec1, BF_RNDN);
        bf_neg(r);
        if (i != 1)
            bf_add_si(r, r, 1, prec1, BF_RNDN);
    }
    bf_delete(U);

    /*  撤消参数缩减：Cosm1(2*x)=2*(2*Cosm1(X)+Cosm1(X)^2)。 */ 
    for(i = 0; i < K; i++) {
        bf_mul(T, r, r, prec1, BF_RNDN);
        bf_mul_2exp(r, 1, BF_PREC_INF, BF_RNDZ);
        bf_add(r, r, T, prec1, BF_RNDN);
        bf_mul_2exp(r, 1, BF_PREC_INF, BF_RNDZ);
    }
    bf_delete(T);

    if (c) {
        if ((mod & 1) == 0) {
            bf_add_si(c, r, 1, prec1, BF_RNDN);
        } else {
            bf_sqrt_sin(c, r, prec1);
            c->sign = is_neg ^ 1;
        }
        c->sign ^= mod >> 1;
    }
    if (s) {
        if ((mod & 1) == 0) {
            bf_sqrt_sin(s, r, prec1);
            s->sign = is_neg;
        } else {
            bf_add_si(s, r, 1, prec1, BF_RNDN);
        }
        s->sign ^= mod >> 1;
    }
    bf_delete(r);
    return BF_ST_INEXACT;
}

static int bf_cos_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    return bf_sincos(NULL, r, a, prec);
}

int bf_cos(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_cos_internal, NULL);
}

static int bf_sin_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    return bf_sincos(r, NULL, a, prec);
}

int bf_sin(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_sin_internal, NULL);
}

static int bf_tan_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    bf_t T_s, *T = &T_s;
    limb_t prec1;
    
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else if (a->expn == BF_EXP_INF) {
            bf_set_nan(r);
            return BF_ST_INVALID_OP;
        } else {
            bf_set_zero(r, a->sign);
            return 0;
        }
    }

    /*  XXX：精确度分析。 */ 
    prec1 = prec + 8;
    bf_init(s, T);
    bf_sincos(r, T, a, prec1);
    bf_div(r, r, T, prec1, BF_RNDF);
    bf_delete(T);
    return BF_ST_INEXACT;
}

int bf_tan(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_tan_internal, NULL);
}

/*  如果Add_Pi2为True，则将pi/2添加到结果(用于acos(X)到避免取消)。 */ 
static int bf_atan_internal(bf_t *r, const bf_t *a, limb_t prec,
                            void *opaque)
{
    bf_context_t *s = r->ctx;
    BOOL add_pi2 = (BOOL)(intptr_t)opaque;
    bf_t T_s, *T = &T_s;
    bf_t U_s, *U = &U_s;
    bf_t V_s, *V = &V_s;
    bf_t X2_s, *X2 = &X2_s;
    int cmp_1;
    slimb_t prec1, i, K, l;
    
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else {
            if (a->expn == BF_EXP_INF)
                i = 1 - 2 * a->sign;
            else
                i = 0;
            i += add_pi2;
            /*  返回i*(pi/2)，其中-1&lt;=i&lt;=2。 */ 
            if (i == 0) {
                bf_set_zero(r, add_pi2 ? 0 : a->sign);
                return 0;
            } else {
                /*  PI或PI/2。 */ 
                bf_const_pi(r, prec, BF_RNDF);
                if (i != 2)
                    bf_mul_2exp(r, -1, BF_PREC_INF, BF_RNDZ);
                r->sign = (i < 0);
                return BF_ST_INEXACT;
            }
        }
    }
    
    bf_init(s, T);
    bf_set_ui(T, 1);
    cmp_1 = bf_cmpu(a, T);
    if (cmp_1 == 0 && !add_pi2) {
        /*  捷径：ABS(A)==1-&gt;+/-pi/4。 */ 
        bf_const_pi(r, prec, BF_RNDF);
        bf_mul_2exp(r, -2, BF_PREC_INF, BF_RNDZ);
        r->sign = a->sign;
        bf_delete(T);
        return BF_ST_INEXACT;
    }

    /*  XXX：精确度分析。 */ 
    K = bf_isqrt((prec + 1) / 2);
    l = prec / (2 * K) + 1;
    prec1 = prec + K + 2 * l + 32;
    //  Print tf(“prec=%ldK=%ldl=%ldpr1=%ld\n”，prec，K，l，pr1)；
    
    if (cmp_1 > 0) {
        bf_set_ui(T, 1);
        bf_div(T, T, a, prec1, BF_RNDN);
    } else {
        bf_set(T, a);
    }

    /*  ABS(T)&lt;=1。 */ 

    /*  论据归约。 */ 

    bf_init(s, U);
    bf_init(s, V);
    bf_init(s, X2);
    for(i = 0; i < K; i++) {
        /*  T=T/(1+SQRT(1+T^2))。 */ 
        bf_mul(U, T, T, prec1, BF_RNDN);
        bf_add_si(U, U, 1, prec1, BF_RNDN);
        bf_sqrt(V, U, prec1, BF_RNDN);
        bf_add_si(V, V, 1, prec1, BF_RNDN);
        bf_div(T, T, V, prec1, BF_RNDN);
    }

    /*  泰勒级数：X-x^3/3+...+(-1)^l*y^(2*l+1)/(2*l+1)。 */ 
    bf_mul(X2, T, T, prec1, BF_RNDN);
    bf_set_ui(r, 0);
    for(i = l; i >= 1; i--) {
        bf_set_si(U, 1);
        bf_set_ui(V, 2 * i + 1);
        bf_div(U, U, V, prec1, BF_RNDN);
        bf_neg(r);
        bf_add(r, r, U, prec1, BF_RNDN);
        bf_mul(r, r, X2, prec1, BF_RNDN);
    }
    bf_neg(r);
    bf_add_si(r, r, 1, prec1, BF_RNDN);
    bf_mul(r, r, T, prec1, BF_RNDN);

    /*  撤消参数约简。 */ 
    bf_mul_2exp(r, K, BF_PREC_INF, BF_RNDZ);
    
    bf_delete(U);
    bf_delete(V);
    bf_delete(X2);

    i = add_pi2;
    if (cmp_1 > 0) {
        /*  撤消反转：R=Sign(A)*PI/2-r。 */ 
        bf_neg(r);
        i += 1 - 2 * a->sign;
    }
    /*  将i*(pi/2)与-1&lt;=i&lt;=2相加。 */ 
    if (i != 0) {
        bf_const_pi(T, prec1, BF_RNDF);
        if (i != 2)
            bf_mul_2exp(T, -1, BF_PREC_INF, BF_RNDZ);
        T->sign = (i < 0);
        bf_add(r, T, r, prec1, BF_RNDN);
    }
    
    bf_delete(T);
    return BF_ST_INEXACT;
}

int bf_atan(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_atan_internal, (void *)FALSE);
}

static int bf_atan2_internal(bf_t *r, const bf_t *y, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    const bf_t *x = opaque;
    bf_t T_s, *T = &T_s;
    limb_t prec1;
    int ret;
    
    if (y->expn == BF_EXP_NAN || x->expn == BF_EXP_NAN) {
        bf_set_nan(r);
        return 0;
    }

    /*  假设inf/inf=1和0/0=0，则计算atan(y/x)。 */ 
    bf_init(s, T);
    prec1 = prec + 32;
    if (y->expn == BF_EXP_INF && x->expn == BF_EXP_INF) {
        bf_set_ui(T, 1);
        T->sign = y->sign ^ x->sign;
    } else if (y->expn == BF_EXP_ZERO && x->expn == BF_EXP_ZERO) {
        bf_set_zero(T, y->sign ^ x->sign);
    } else {
        bf_div(T, y, x, prec1, BF_RNDF);
    }
    ret = bf_atan(r, T, prec1, BF_RNDF);

    if (x->sign) {
        /*  如果x&lt;0(包括-0)，则返回符号(Y)* */ 
        bf_const_pi(T, prec1, BF_RNDF);
        T->sign = y->sign;
        bf_add(r, r, T, prec1, BF_RNDN);
        ret |= BF_ST_INEXACT;
    }

    bf_delete(T);
    return ret;
}

int bf_atan2(bf_t *r, const bf_t *y, const bf_t *x,
             limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, y, prec, flags, bf_atan2_internal, (void *)x);
}

static int bf_asin_internal(bf_t *r, const bf_t *a, limb_t prec, void *opaque)
{
    bf_context_t *s = r->ctx;
    BOOL is_acos = (BOOL)(intptr_t)opaque;
    bf_t T_s, *T = &T_s;
    limb_t prec1, prec2;
    int res;
    
    if (a->len == 0) {
        if (a->expn == BF_EXP_NAN) {
            bf_set_nan(r);
            return 0;
        } else if (a->expn == BF_EXP_INF) {
            bf_set_nan(r);
            return BF_ST_INVALID_OP;
        } else {
            if (is_acos) {
                bf_const_pi(r, prec, BF_RNDF);
                bf_mul_2exp(r, -1, BF_PREC_INF, BF_RNDZ);
                return BF_ST_INEXACT;
            } else {
                bf_set_zero(r, a->sign);
                return 0;
            }
        }
    }
    bf_init(s, T);
    bf_set_ui(T, 1);
    res = bf_cmpu(a, T);
    if (res > 0) {
        bf_delete(T);
        bf_set_nan(r);
        return BF_ST_INVALID_OP;
    } else if (res == 0 && a->sign == 0 && is_acos) {
        bf_set_zero(r, 0);
        bf_delete(T);
        return 0;
    }
    
    /*   */ 
    prec1 = prec + 8;
    /*   */ 
    /*  XXX：尽可能使用较低的精度。 */ 
    if (a->expn >= 0)
        prec2 = BF_PREC_INF;
    else
        prec2 = prec1;
    bf_mul(T, a, a, prec2, BF_RNDN);
    bf_neg(T);
    bf_add_si(T, T, 1, prec2, BF_RNDN);

    bf_sqrt(r, T, prec1, BF_RNDN);
    bf_div(T, a, r, prec1, BF_RNDN);
    if (is_acos)
        bf_neg(T);
    bf_atan_internal(r, T, prec1, (void *)(intptr_t)is_acos);
    bf_delete(T);
    return BF_ST_INEXACT;
}

int bf_asin(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_asin_internal, (void *)FALSE);
}

int bf_acos(bf_t *r, const bf_t *a, limb_t prec, bf_flags_t flags)
{
    return bf_ziv_rounding(r, a, prec, flags, bf_asin_internal, (void *)TRUE);
}

#ifdef USE_FFT_MUL
/*  *************************************************************。 */ 
/*  用FFT进行整数乘法。 */ 

/*  或Tab中位位置‘pos’处的Limb_Bits。 */ 
static inline void put_bits(limb_t *tab, limb_t len, slimb_t pos, limb_t val)
{
    limb_t i;
    int p;

    i = pos >> LIMB_LOG2_BITS;
    p = pos & (LIMB_BITS - 1);
    if (i < len)
        tab[i] |= val << p;
    if (p != 0) {
        i++;
        if (i < len) {
            tab[i] |= val >> (LIMB_BITS - p);
        }
    }
}

#if defined(__AVX2__)

typedef double NTTLimb;

/*  我们必须满足：模&gt;=1&lt;&lt;NTT_MOD_Log2_MIN。 */ 
#define NTT_MOD_LOG2_MIN 50
#define NTT_MOD_LOG2_MAX 51
#define NB_MODS 5
#define NTT_PROOT_2EXP 39
static const int ntt_int_bits[NB_MODS] = { 254, 203, 152, 101, 50, };

static const limb_t ntt_mods[NB_MODS] = { 0x00073a8000000001, 0x0007858000000001, 0x0007a38000000001, 0x0007a68000000001, 0x0007fd8000000001,
};

static const limb_t ntt_proot[2][NB_MODS] = {
    { 0x00056198d44332c8, 0x0002eb5d640aad39, 0x00047e31eaa35fd0, 0x0005271ac118a150, 0x00075e0ce8442bd5, },
    { 0x000461169761bcc5, 0x0002dac3cb2da688, 0x0004abc97751e3bf, 0x000656778fc8c485, 0x0000dc6469c269fa, },
};

static const limb_t ntt_mods_cr[NB_MODS * (NB_MODS - 1) / 2] = {
 0x00020e4da740da8e, 0x0004c3dc09c09c1d, 0x000063bd097b4271, 0x000799d8f18f18fd,
 0x0005384222222264, 0x000572b07c1f07fe, 0x00035cd08888889a,
 0x00066015555557e3, 0x000725960b60b623,
 0x0002fc1fa1d6ce12,
};

#else

typedef limb_t NTTLimb;

#if LIMB_BITS == 64

#define NTT_MOD_LOG2_MIN 61
#define NTT_MOD_LOG2_MAX 62
#define NB_MODS 5
#define NTT_PROOT_2EXP 51
static const int ntt_int_bits[NB_MODS] = { 307, 246, 185, 123, 61, };

static const limb_t ntt_mods[NB_MODS] = { 0x28d8000000000001, 0x2a88000000000001, 0x2ed8000000000001, 0x3508000000000001, 0x3aa8000000000001,
};

static const limb_t ntt_proot[2][NB_MODS] = {
    { 0x1b8ea61034a2bea7, 0x21a9762de58206fb, 0x02ca782f0756a8ea, 0x278384537a3e50a1, 0x106e13fee74ce0ab, },
    { 0x233513af133e13b8, 0x1d13140d1c6f75f1, 0x12cde57f97e3eeda, 0x0d6149e23cbe654f, 0x36cd204f522a1379, },
};

static const limb_t ntt_mods_cr[NB_MODS * (NB_MODS - 1) / 2] = {
 0x08a9ed097b425eea, 0x18a44aaaaaaaaab3, 0x2493f57f57f57f5d, 0x126b8d0649a7f8d4,
 0x09d80ed7303b5ccc, 0x25b8bcf3cf3cf3d5, 0x2ce6ce63398ce638,
 0x0e31fad40a57eb59, 0x02a3529fd4a7f52f,
 0x3a5493e93e93e94a,
};

#elif LIMB_BITS == 32

/*  我们必须满足：模&gt;=1&lt;&lt;NTT_MOD_Log2_MIN。 */ 
#define NTT_MOD_LOG2_MIN 29
#define NTT_MOD_LOG2_MAX 30
#define NB_MODS 5
#define NTT_PROOT_2EXP 20
static const int ntt_int_bits[NB_MODS] = { 148, 119, 89, 59, 29, };

static const limb_t ntt_mods[NB_MODS] = { 0x0000000032b00001, 0x0000000033700001, 0x0000000036d00001, 0x0000000037300001, 0x000000003e500001,
};

static const limb_t ntt_proot[2][NB_MODS] = {
    { 0x0000000032525f31, 0x0000000005eb3b37, 0x00000000246eda9f, 0x0000000035f25901, 0x00000000022f5768, },
    { 0x00000000051eba1a, 0x00000000107be10e, 0x000000001cd574e0, 0x00000000053806e6, 0x000000002cd6bf98, },
};

static const limb_t ntt_mods_cr[NB_MODS * (NB_MODS - 1) / 2] = {
 0x000000000449559a, 0x000000001eba6ca9, 0x000000002ec18e46, 0x000000000860160b,
 0x000000000d321307, 0x000000000bf51120, 0x000000000f662938,
 0x000000000932ab3e, 0x000000002f40eef8,
 0x000000002e760905,
};

#endif /*  肢体比特。 */ 

#endif /*  ！AVX2。 */ 

#if defined(__AVX2__)
#define NTT_TRIG_K_MAX 18
#else
#define NTT_TRIG_K_MAX 19
#endif

typedef struct BFNTTState {
    bf_context_t *ctx;
    
    /*  用于mul_mod_fast()。 */ 
    limb_t ntt_mods_div[NB_MODS];

    limb_t ntt_proot_pow[NB_MODS][2][NTT_PROOT_2EXP + 1];
    limb_t ntt_proot_pow_inv[NB_MODS][2][NTT_PROOT_2EXP + 1];
    NTTLimb *ntt_trig[NB_MODS][2][NTT_TRIG_K_MAX + 1];
    /*  %1/2^n模数m。 */ 
    limb_t ntt_len_inv[NB_MODS][NTT_PROOT_2EXP + 1][2];
#if defined(__AVX2__)
    __m256d ntt_mods_cr_vec[NB_MODS * (NB_MODS - 1) / 2];
    __m256d ntt_mods_vec[NB_MODS];
    __m256d ntt_mods_inv_vec[NB_MODS];
#else
    limb_t ntt_mods_cr_inv[NB_MODS * (NB_MODS - 1) / 2];
#endif
} BFNTTState;

static NTTLimb *get_trig(BFNTTState *s, int k, int inverse, int m_idx);

/*  与最多(Limb_Bits-1)位模数相加模数。 */ 
static inline limb_t add_mod(limb_t a, limb_t b, limb_t m)
{
    limb_t r;
    r = a + b;
    if (r >= m)
        r -= m;
    return r;
}

/*  最高可达Limb_Bits位模数的子模数。 */ 
static inline limb_t sub_mod(limb_t a, limb_t b, limb_t m)
{
    limb_t r;
    r = a - b;
    if (r > a)
        r += m;
    return r;
}

/*  返回(R0+R1*B)模数m前提条件：0&lt;=R0+R1*B&lt;2^(64+NTT_MOD_LOG2_MIN)。 */ 
static inline limb_t mod_fast(dlimb_t r, 
                                limb_t m, limb_t m_inv)
{
    limb_t a1, q, t0, r1, r0;
    
    a1 = r >> NTT_MOD_LOG2_MIN;
    
    q = ((dlimb_t)a1 * m_inv) >> LIMB_BITS;
    r = r - (dlimb_t)q * m - m * 2;
    r1 = r >> LIMB_BITS;
    t0 = (slimb_t)r1 >> 1;
    r += m & t0;
    r0 = r;
    r1 = r >> LIMB_BITS;
    r0 += m & r1;
    return r0;
}

/*  使用预计算模逆的更快版本。前提条件：0&lt;=a*b&lt;2^(64+NTT_MOD_LOG2_MIN)。 */ 
static inline limb_t mul_mod_fast(limb_t a, limb_t b, 
                                    limb_t m, limb_t m_inv)
{
    dlimb_t r;
    r = (dlimb_t)a * (dlimb_t)b;
    return mod_fast(r, m, m_inv);
}

static inline limb_t init_mul_mod_fast(limb_t m)
{
    dlimb_t t;
    assert(m < (limb_t)1 << NTT_MOD_LOG2_MAX);
    assert(m >= (limb_t)1 << NTT_MOD_LOG2_MIN);
    t = (dlimb_t)1 << (LIMB_BITS + NTT_MOD_LOG2_MIN);
    return t / m;
}

/*  当乘数为常量时使用更快的版本。0&lt;=a&lt;2^64，0&lt;=b&lt;m。 */ 
static inline limb_t mul_mod_fast2(limb_t a, limb_t b, 
                                     limb_t m, limb_t b_inv)
{
    limb_t r, q;

    q = ((dlimb_t)a * (dlimb_t)b_inv) >> LIMB_BITS;
    r = a * b - q * m;
    if (r >= m)
        r -= m;
    return r;
}

/*  当乘数为常量时使用更快的版本。0&lt;=a&lt;2^64，0&lt;=b&lt;m。设r=a*b mod m。返回值为‘r’或‘r+M‘。 */ 
static inline limb_t mul_mod_fast3(limb_t a, limb_t b, 
                                     limb_t m, limb_t b_inv)
{
    limb_t r, q;

    q = ((dlimb_t)a * (dlimb_t)b_inv) >> LIMB_BITS;
    r = a * b - q * m;
    return r;
}

static inline limb_t init_mul_mod_fast2(limb_t b, limb_t m)
{
    return ((dlimb_t)b << LIMB_BITS) / m;
}

#ifdef __AVX2__

static inline limb_t ntt_limb_to_int(NTTLimb a, limb_t m)
{
    slimb_t v;
    v = a;
    if (v < 0)
        v += m;
    if (v >= m)
        v -= m;
    return v;
}

static inline NTTLimb int_to_ntt_limb(limb_t a, limb_t m)
{
    return (slimb_t)a;
}

static inline NTTLimb int_to_ntt_limb2(limb_t a, limb_t m)
{
    if (a >= (m / 2))
        a -= m;
    return (slimb_t)a;
}

/*  如果r&lt;0，则返回r+m，否则返回r。 */ 
static inline __m256d ntt_mod1(__m256d r, __m256d m)
{
    return _mm256_blendv_pd(r, r + m, r);
}

/*  输入：ABS(R)&lt;2*m输出：ABS(R)&lt;m。 */ 
static inline __m256d ntt_mod(__m256d r, __m256d mf, __m256d m2f)
{
    return _mm256_blendv_pd(r, r + m2f, r) - mf;
}

/*  输入：ABS(a*b)&lt;2*m^2，输出：ABS(R)&lt;m。 */ 
static inline __m256d ntt_mul_mod(__m256d a, __m256d b, __m256d mf,
                                  __m256d m_inv)
{
    __m256d r, q, ab1, ab0, qm0, qm1;
    ab1 = a * b;
    q = _mm256_round_pd(ab1 * m_inv, 0); /*  四舍五入到最接近。 */ 
    qm1 = q * mf;
    qm0 = _mm256_fmsub_pd(q, mf, qm1); /*  低位部分。 */ 
    ab0 = _mm256_fmsub_pd(a, b, ab1); /*  低位部分。 */ 
    r = (ab1 - qm1) + (ab0 - qm0);
    return r;
}

static void *bf_aligned_malloc(bf_context_t *s, size_t size, size_t align)
{
    void *ptr;
    void **ptr1;
    ptr = bf_malloc(s, size + sizeof(void *) + align - 1);
    if (!ptr)
        return NULL;
    ptr1 = (void **)(((uintptr_t)ptr + sizeof(void *) + align - 1) &
                     ~(align - 1));
    ptr1[-1] = ptr;
    return ptr1;
}

static void bf_aligned_free(bf_context_t *s, void *ptr)
{
    if (!ptr)
        return;
    bf_free(s, ((void **)ptr)[-1]);
}

static void *ntt_malloc(BFNTTState *s, size_t size)
{
    return bf_aligned_malloc(s->ctx, size, 64);
}

static void ntt_free(BFNTTState *s, void *ptr)
{
    bf_aligned_free(s->ctx, ptr);
}

static no_inline void ntt_fft(BFNTTState *s,
                              NTTLimb *out_buf, NTTLimb *in_buf,
                              NTTLimb *tmp_buf, int fft_len_log2,
                              int inverse, int m_idx)
{
    limb_t nb_blocks, fft_per_block, p, k, n, stride_in, i, j;
    NTTLimb *tab_in, *tab_out, *tmp, *trig;
    __m256d m_inv, mf, m2f, c, a0, a1, b0, b1;
    limb_t m;
    int l;
    
    m = ntt_mods[m_idx];
    
    m_inv = _mm256_set1_pd(1.0 / (double)m);
    mf = _mm256_set1_pd(m);
    m2f = _mm256_set1_pd(m * 2);

    n = (limb_t)1 << fft_len_log2;
    assert(n >= 8);
    stride_in = n / 2;

    tab_in = in_buf;
    tab_out = tmp_buf;
    trig = get_trig(s, fft_len_log2, inverse, m_idx);
    p = 0;
    for(k = 0; k < stride_in; k += 4) {
        a0 = _mm256_load_pd(&tab_in[k]);
        a1 = _mm256_load_pd(&tab_in[k + stride_in]);
        c = _mm256_load_pd(trig);
        trig += 4;
        b0 = ntt_mod(a0 + a1, mf, m2f);
        b1 = ntt_mul_mod(a0 - a1, c, mf, m_inv);
        a0 = _mm256_permute2f128_pd(b0, b1, 0x20);
        a1 = _mm256_permute2f128_pd(b0, b1, 0x31);
        a0 = _mm256_permute4x64_pd(a0, 0xd8);
        a1 = _mm256_permute4x64_pd(a1, 0xd8);
        _mm256_store_pd(&tab_out[p], a0);
        _mm256_store_pd(&tab_out[p + 4], a1);
        p += 2 * 4;
    }
    tmp = tab_in;
    tab_in = tab_out;
    tab_out = tmp;

    trig = get_trig(s, fft_len_log2 - 1, inverse, m_idx);
    p = 0;
    for(k = 0; k < stride_in; k += 4) {
        a0 = _mm256_load_pd(&tab_in[k]);
        a1 = _mm256_load_pd(&tab_in[k + stride_in]);
        c = _mm256_setr_pd(trig[0], trig[0], trig[1], trig[1]);
        trig += 2;
        b0 = ntt_mod(a0 + a1, mf, m2f);
        b1 = ntt_mul_mod(a0 - a1, c, mf, m_inv);
        a0 = _mm256_permute2f128_pd(b0, b1, 0x20);
        a1 = _mm256_permute2f128_pd(b0, b1, 0x31);
        _mm256_store_pd(&tab_out[p], a0);
        _mm256_store_pd(&tab_out[p + 4], a1);
        p += 2 * 4;
    }
    tmp = tab_in;
    tab_in = tab_out;
    tab_out = tmp;
    
    nb_blocks = n / 4;
    fft_per_block = 4;

    l = fft_len_log2 - 2;
    while (nb_blocks != 2) {
        nb_blocks >>= 1;
        p = 0;
        k = 0;
        trig = get_trig(s, l, inverse, m_idx);
        for(i = 0; i < nb_blocks; i++) {
            c = _mm256_set1_pd(trig[0]);
            trig++;
            for(j = 0; j < fft_per_block; j += 4) {
                a0 = _mm256_load_pd(&tab_in[k + j]);
                a1 = _mm256_load_pd(&tab_in[k + j + stride_in]);
                b0 = ntt_mod(a0 + a1, mf, m2f);
                b1 = ntt_mul_mod(a0 - a1, c, mf, m_inv);
                _mm256_store_pd(&tab_out[p + j], b0);
                _mm256_store_pd(&tab_out[p + j + fft_per_block], b1);
            }
            k += fft_per_block;
            p += 2 * fft_per_block;
        }
        fft_per_block <<= 1;
        l--;
        tmp = tab_in;
        tab_in = tab_out;
        tab_out = tmp;
    }

    tab_out = out_buf;
    for(k = 0; k < stride_in; k += 4) {
        a0 = _mm256_load_pd(&tab_in[k]);
        a1 = _mm256_load_pd(&tab_in[k + stride_in]);
        b0 = ntt_mod(a0 + a1, mf, m2f);
        b1 = ntt_mod(a0 - a1, mf, m2f);
        _mm256_store_pd(&tab_out[k], b0);
        _mm256_store_pd(&tab_out[k + stride_in], b1);
    }
}

static void ntt_vec_mul(BFNTTState *s,
                        NTTLimb *tab1, NTTLimb *tab2, limb_t fft_len_log2,
                        int k_tot, int m_idx)
{
    limb_t i, c_inv, n, m;
    __m256d m_inv, mf, a, b, c;
    
    m = ntt_mods[m_idx];
    c_inv = s->ntt_len_inv[m_idx][k_tot][0];
    m_inv = _mm256_set1_pd(1.0 / (double)m);
    mf = _mm256_set1_pd(m);
    c = _mm256_set1_pd(int_to_ntt_limb(c_inv, m));
    n = (limb_t)1 << fft_len_log2;
    for(i = 0; i < n; i += 4) {
        a = _mm256_load_pd(&tab1[i]);
        b = _mm256_load_pd(&tab2[i]);
        a = ntt_mul_mod(a, b, mf, m_inv);
        a = ntt_mul_mod(a, c, mf, m_inv);
        _mm256_store_pd(&tab1[i], a);
    }
}

static no_inline void mul_trig(NTTLimb *buf,
                               limb_t n, limb_t c1, limb_t m, limb_t m_inv1)
{
    limb_t i, c2, c3, c4;
    __m256d c, c_mul, a0, mf, m_inv;
    assert(n >= 2);
    
    mf = _mm256_set1_pd(m);
    m_inv = _mm256_set1_pd(1.0 / (double)m);

    c2 = mul_mod_fast(c1, c1, m, m_inv1);
    c3 = mul_mod_fast(c2, c1, m, m_inv1);
    c4 = mul_mod_fast(c2, c2, m, m_inv1);
    c = _mm256_setr_pd(1, int_to_ntt_limb(c1, m),
                       int_to_ntt_limb(c2, m), int_to_ntt_limb(c3, m));
    c_mul = _mm256_set1_pd(int_to_ntt_limb(c4, m));
    for(i = 0; i < n; i += 4) {
        a0 = _mm256_load_pd(&buf[i]);
        a0 = ntt_mul_mod(a0, c, mf, m_inv);
        _mm256_store_pd(&buf[i], a0);
        c = ntt_mul_mod(c, c_mul, mf, m_inv);
    }
}

#else

static void *ntt_malloc(BFNTTState *s, size_t size)
{
    return bf_malloc(s->ctx, size);
}

static void ntt_free(BFNTTState *s, void *ptr)
{
    bf_free(s->ctx, ptr);
}

static inline limb_t ntt_limb_to_int(NTTLimb a, limb_t m)
{
    if (a >= m)
        a -= m;
    return a;
}

static inline NTTLimb int_to_ntt_limb(slimb_t a, limb_t m)
{
    return a;
}

static no_inline void ntt_fft(BFNTTState *s, NTTLimb *out_buf, NTTLimb *in_buf,
                              NTTLimb *tmp_buf, int fft_len_log2,
                              int inverse, int m_idx)
{
    limb_t nb_blocks, fft_per_block, p, k, n, stride_in, i, j, m, m2;
    NTTLimb *tab_in, *tab_out, *tmp, a0, a1, b0, b1, c, *trig, c_inv;
    int l;
    
    m = ntt_mods[m_idx];
    m2 = 2 * m;
    n = (limb_t)1 << fft_len_log2;
    nb_blocks = n;
    fft_per_block = 1;
    stride_in = n / 2;
    tab_in = in_buf;
    tab_out = tmp_buf;
    l = fft_len_log2;
    while (nb_blocks != 2) {
        nb_blocks >>= 1;
        p = 0;
        k = 0;
        trig = get_trig(s, l, inverse, m_idx);
        for(i = 0; i < nb_blocks; i++) {
            c = trig[0];
            c_inv = trig[1];
            trig += 2;
            for(j = 0; j < fft_per_block; j++) {
                a0 = tab_in[k + j];
                a1 = tab_in[k + j + stride_in];
                b0 = add_mod(a0, a1, m2);
                b1 = a0 - a1 + m2;
                b1 = mul_mod_fast3(b1, c, m, c_inv);
                tab_out[p + j] = b0;
                tab_out[p + j + fft_per_block] = b1;
            }
            k += fft_per_block;
            p += 2 * fft_per_block;
        }
        fft_per_block <<= 1;
        l--;
        tmp = tab_in;
        tab_in = tab_out;
        tab_out = tmp;
    }
    /*  在最后一步中没有玩耍。 */ 
    tab_out = out_buf; 
    for(k = 0; k < stride_in; k++) {
        a0 = tab_in[k];
        a1 = tab_in[k + stride_in];
        b0 = add_mod(a0, a1, m2);
        b1 = sub_mod(a0, a1, m2);
        tab_out[k] = b0;
        tab_out[k + stride_in] = b1;
    }
}

static void ntt_vec_mul(BFNTTState *s,
                        NTTLimb *tab1, NTTLimb *tab2, int fft_len_log2,
                        int k_tot, int m_idx)
{
    limb_t i, norm, norm_inv, a, n, m, m_inv;
    
    m = ntt_mods[m_idx];
    m_inv = s->ntt_mods_div[m_idx];
    norm = s->ntt_len_inv[m_idx][k_tot][0];
    norm_inv = s->ntt_len_inv[m_idx][k_tot][1];
    n = (limb_t)1 << fft_len_log2;
    for(i = 0; i < n; i++) {
        a = tab1[i];
        /*  需要缩小范围，以使产品&lt;2^(Limb_Bits+NTT_MOD_Log2_Min)。 */ 
        if (a >= m)
            a -= m;
        a = mul_mod_fast(a, tab2[i], m, m_inv);
        a = mul_mod_fast3(a, norm, m, norm_inv);
        tab1[i] = a;
    }
}

static no_inline void mul_trig(NTTLimb *buf,
                               limb_t n, limb_t c_mul, limb_t m, limb_t m_inv)
{
    limb_t i, c0, c_mul_inv;
    
    c0 = 1;
    c_mul_inv = init_mul_mod_fast2(c_mul, m);
    for(i = 0; i < n; i++) {
        buf[i] = mul_mod_fast(buf[i], c0, m, m_inv);
        c0 = mul_mod_fast2(c0, c_mul, m, c_mul_inv);
    }
}

#endif /*  ！AVX2。 */ 

static no_inline NTTLimb *get_trig(BFNTTState *s,
                                   int k, int inverse1, int m_idx1)
{
    NTTLimb *tab;
    limb_t i, n2, c, c_mul, m, c_mul_inv;
    int m_idx, inverse;
    
    if (k > NTT_TRIG_K_MAX)
        return NULL;

    for(;;) {
        tab = s->ntt_trig[m_idx1][inverse1][k];
        if (tab)
            return tab;
        n2 = (limb_t)1 << (k - 1);
        for(m_idx = 0; m_idx < NB_MODS; m_idx++) {
            m = ntt_mods[m_idx];
            for(inverse = 0; inverse < 2; inverse++) {
#ifdef __AVX2__
                tab = ntt_malloc(s, sizeof(NTTLimb) * n2);
#else
                tab = ntt_malloc(s, sizeof(NTTLimb) * n2 * 2);
#endif
                c = 1;
                c_mul = s->ntt_proot_pow[m_idx][inverse][k];
                c_mul_inv = s->ntt_proot_pow_inv[m_idx][inverse][k];
                for(i = 0; i < n2; i++) {
#ifdef __AVX2__
                    tab[i] = int_to_ntt_limb2(c, m);
#else
                    tab[2 * i] = int_to_ntt_limb(c, m);
                    tab[2 * i + 1] = init_mul_mod_fast2(c, m);
#endif
                    c = mul_mod_fast2(c, c_mul, m, c_mul_inv);
                }
                s->ntt_trig[m_idx][inverse][k] = tab;
            }
        }
    }
}

void fft_clear_cache(bf_context_t *s1)
{
    int m_idx, inverse, k;
    BFNTTState *s = s1->ntt_state;
    if (s) {
        for(m_idx = 0; m_idx < NB_MODS; m_idx++) {
            for(inverse = 0; inverse < 2; inverse++) {
                for(k = 0; k < NTT_TRIG_K_MAX + 1; k++) {
                    if (s->ntt_trig[m_idx][inverse][k]) {
                        ntt_free(s, s->ntt_trig[m_idx][inverse][k]);
                        s->ntt_trig[m_idx][inverse][k] = NULL;
                    }
                }
            }
        }
#if defined(__AVX2__)
        bf_aligned_free(s1, s);
#else
        bf_free(s1, s);
#endif
        s1->ntt_state = NULL;
    }
}

#define STRIP_LEN 16

/*  Dst=buf1，src=buf2。 */ 
static void ntt_fft_partial(BFNTTState *s, NTTLimb *buf1,
                            int k1, int k2, limb_t n1, limb_t n2, int inverse,
                            limb_t m_idx)
{
    limb_t i, j, c_mul, c0, m, m_inv, strip_len, l;
    NTTLimb *buf2, *buf3;
    
    buf3 = ntt_malloc(s, sizeof(NTTLimb) * n1);
    if (k2 == 0) {
        ntt_fft(s, buf1, buf1, buf3, k1, inverse, m_idx);
    } else {
        strip_len = STRIP_LEN;
        buf2 = ntt_malloc(s, sizeof(NTTLimb) * n1 * strip_len);

        m = ntt_mods[m_idx];
        m_inv = s->ntt_mods_div[m_idx];
        c0 = s->ntt_proot_pow[m_idx][inverse][k1 + k2];
        c_mul = 1;
        assert((n2 % strip_len) == 0);
        for(j = 0; j < n2; j += strip_len) {
            for(i = 0; i < n1; i++) {
                for(l = 0; l < strip_len; l++) {
                    buf2[i + l * n1] = buf1[i * n2 + (j + l)];
                }
            }
            for(l = 0; l < strip_len; l++) {
                if (inverse)
                    mul_trig(buf2 + l * n1, n1, c_mul, m, m_inv);
                ntt_fft(s, buf2 + l * n1, buf2 + l * n1, buf3, k1, inverse, m_idx);
                if (!inverse)
                    mul_trig(buf2 + l * n1, n1, c_mul, m, m_inv);
                c_mul = mul_mod_fast(c_mul, c0, m, m_inv);
            }
            
            for(i = 0; i < n1; i++) {
                for(l = 0; l < strip_len; l++) {
                    buf1[i * n2 + (j + l)] = buf2[i + l *n1];
                }
            }
        }
        ntt_free(s, buf2);
    }
    ntt_free(s, buf3);
}


/*  Dst=buf1，src=buf2，tMP=buf3。 */ 
static void ntt_conv(BFNTTState *s, NTTLimb *buf1, NTTLimb *buf2,
                     int k, int k_tot, limb_t m_idx)
{
    limb_t n1, n2, i;
    int k1, k2;
    
    if (k <= NTT_TRIG_K_MAX) {
        k1 = k;
    } else {
        /*  FFT的递归分裂。 */ 
        k1 = bf_min(k / 2, NTT_TRIG_K_MAX);
    }
    k2 = k - k1;
    n1 = (limb_t)1 << k1;
    n2 = (limb_t)1 << k2;
    
    ntt_fft_partial(s, buf1, k1, k2, n1, n2, 0, m_idx);
    ntt_fft_partial(s, buf2, k1, k2, n1, n2, 0, m_idx);
    if (k2 == 0) {
        ntt_vec_mul(s, buf1, buf2, k, k_tot, m_idx);
    } else {
        for(i = 0; i < n1; i++) {
            ntt_conv(s, buf1 + i * n2, buf2 + i * n2, k2, k_tot, m_idx);
        }
    }
    ntt_fft_partial(s, buf1, k1, k2, n1, n2, 1, m_idx);
}


static no_inline void limb_to_ntt(BFNTTState *s,
                                  NTTLimb *tabr, limb_t fft_len,
                                  const limb_t *taba, limb_t a_len, int dpl,
                                  int first_m_idx, int nb_mods)
{
    slimb_t i, n;
    dlimb_t a, b;
    int j, shift;
    limb_t base_mask1, a0, a1, a2, r, m, m_inv;
    
#if 0
    for(i = 0; i < a_len; i++) {
        printf("%" PRId64 ": " FMT_LIMB "\n",
               (int64_t)i, taba[i]);
    }
#endif   
    memset(tabr, 0, sizeof(NTTLimb) * fft_len * nb_mods);
    shift = dpl & (LIMB_BITS - 1);
    if (shift == 0)
        base_mask1 = -1;
    else
        base_mask1 = ((limb_t)1 << shift) - 1;
    n = bf_min(fft_len, (a_len * LIMB_BITS + dpl - 1) / dpl);
    for(i = 0; i < n; i++) {
        a0 = get_bits(taba, a_len, i * dpl);
        if (dpl <= LIMB_BITS) {
            a0 &= base_mask1;
            a = a0;
        } else {
            a1 = get_bits(taba, a_len, i * dpl + LIMB_BITS);
            if (dpl <= (LIMB_BITS + NTT_MOD_LOG2_MIN)) {
                a = a0 | ((dlimb_t)(a1 & base_mask1) << LIMB_BITS);
            } else {
                if (dpl > 2 * LIMB_BITS) {
                    a2 = get_bits(taba, a_len, i * dpl + LIMB_BITS * 2) &
                        base_mask1;
                } else {
                    a1 &= base_mask1;
                    a2 = 0;
                }
                //  Printf(“a=0x%016lx%016lx%016lx\n”，a2，a1，a0)；
                a = (a0 >> (LIMB_BITS - NTT_MOD_LOG2_MAX + NTT_MOD_LOG2_MIN)) |
                    ((dlimb_t)a1 << (NTT_MOD_LOG2_MAX - NTT_MOD_LOG2_MIN)) |
                    ((dlimb_t)a2 << (LIMB_BITS + NTT_MOD_LOG2_MAX - NTT_MOD_LOG2_MIN));
                a0 &= ((limb_t)1 << (LIMB_BITS - NTT_MOD_LOG2_MAX + NTT_MOD_LOG2_MIN)) - 1;
            }
        }
        for(j = 0; j < nb_mods; j++) {
            m = ntt_mods[first_m_idx + j];
            m_inv = s->ntt_mods_div[first_m_idx + j];
            r = mod_fast(a, m, m_inv);
            if (dpl > (LIMB_BITS + NTT_MOD_LOG2_MIN)) {
                b = ((dlimb_t)r << (LIMB_BITS - NTT_MOD_LOG2_MAX + NTT_MOD_LOG2_MIN)) | a0;
                r = mod_fast(b, m, m_inv);
            }
            tabr[i + j * fft_len] = int_to_ntt_limb(r, m);
        }
    }
}

#if defined(__AVX2__)

#define VEC_LEN 4

typedef union {
    __m256d v;
    double d[4];
} VecUnion;

static no_inline void ntt_to_limb(BFNTTState *s, limb_t *tabr, limb_t r_len,
                                  const NTTLimb *buf, int fft_len_log2, int dpl,
                                  int nb_mods)
{
    const limb_t *mods = ntt_mods + NB_MODS - nb_mods;
    const __m256d *mods_cr_vec, *mf, *m_inv;
    VecUnion y[NB_MODS];
    limb_t u[NB_MODS], carry[NB_MODS], fft_len, base_mask1, r;
    slimb_t i, len, pos;
    int j, k, l, shift, n_limb1, p;
    dlimb_t t;
        
    j = NB_MODS * (NB_MODS - 1) / 2 - nb_mods * (nb_mods - 1) / 2;
    mods_cr_vec = s->ntt_mods_cr_vec + j;
    mf = s->ntt_mods_vec + NB_MODS - nb_mods;
    m_inv = s->ntt_mods_inv_vec + NB_MODS - nb_mods;
        
    shift = dpl & (LIMB_BITS - 1);
    if (shift == 0)
        base_mask1 = -1;
    else
        base_mask1 = ((limb_t)1 << shift) - 1;
    n_limb1 = ((unsigned)dpl - 1) / LIMB_BITS;
    for(j = 0; j < NB_MODS; j++) 
        carry[j] = 0;
    for(j = 0; j < NB_MODS; j++) 
        u[j] = 0; /*  避免警告。 */ 
    memset(tabr, 0, sizeof(limb_t) * r_len);
    fft_len = (limb_t)1 << fft_len_log2;
    len = bf_min(fft_len, (r_len * LIMB_BITS + dpl - 1) / dpl);
    len = (len + VEC_LEN - 1) & ~(VEC_LEN - 1);
    i = 0;
    while (i < len) {
        for(j = 0; j < nb_mods; j++)
            y[j].v = *(__m256d *)&buf[i + fft_len * j];

        /*  中国余数将获得混合基数表示。 */ 
        l = 0;
        for(j = 0; j < nb_mods - 1; j++) {
            y[j].v = ntt_mod1(y[j].v, mf[j]);
            for(k = j + 1; k < nb_mods; k++) {
                y[k].v = ntt_mul_mod(y[k].v - y[j].v,
                                     mods_cr_vec[l], mf[k], m_inv[k]);
                l++;
            }
        }
        y[j].v = ntt_mod1(y[j].v, mf[j]);
        
        for(p = 0; p < VEC_LEN; p++) {
            /*  恢复正常表示。 */ 
            u[0] = (int64_t)y[nb_mods - 1].d[p];
            l = 1;
            for(j = nb_mods - 2; j >= 1; j--) {
                r = (int64_t)y[j].d[p];
                for(k = 0; k < l; k++) {
                    t = (dlimb_t)u[k] * mods[j] + r;
                    r = t >> LIMB_BITS;
                    u[k] = t;
                }
                u[l] = r;
                l++;
            }
            /*  Xxx：对于nb_mods=5，l应为4。 */ 
            
            /*  最后一步添加进位。 */ 
            r = (int64_t)y[0].d[p];
            for(k = 0; k < l; k++) {
                t = (dlimb_t)u[k] * mods[j] + r + carry[k];
                r = t >> LIMB_BITS;
                u[k] = t;
            }
            u[l] = r + carry[l];

#if 0
            printf("%" PRId64 ": ", i);
            for(j = nb_mods - 1; j >= 0; j--) {
                printf(" %019" PRIu64, u[j]);
            }
            printf("\n");
#endif
            
            /*  写下数字。 */ 
            pos = i * dpl;
            for(j = 0; j < n_limb1; j++) {
                put_bits(tabr, r_len, pos, u[j]);
                pos += LIMB_BITS;
            }
            put_bits(tabr, r_len, pos, u[n_limb1] & base_mask1);
            /*  按DPL位数移位并设置进位。 */ 
            if (shift == 0) {
                for(j = n_limb1 + 1; j < nb_mods; j++)
                    carry[j - (n_limb1 + 1)] = u[j];
            } else {
                for(j = n_limb1; j < nb_mods - 1; j++) {
                    carry[j - n_limb1] = (u[j] >> shift) |
                        (u[j + 1] << (LIMB_BITS - shift));
                }
                carry[nb_mods - 1 - n_limb1] = u[nb_mods - 1] >> shift;
            }
            i++;
        }
    }
}
#else
static no_inline void ntt_to_limb(BFNTTState *s, limb_t *tabr, limb_t r_len,
                                  const NTTLimb *buf, int fft_len_log2, int dpl,
                                  int nb_mods)
{
    const limb_t *mods = ntt_mods + NB_MODS - nb_mods;
    const limb_t *mods_cr, *mods_cr_inv;
    limb_t y[NB_MODS], u[NB_MODS], carry[NB_MODS], fft_len, base_mask1, r;
    slimb_t i, len, pos;
    int j, k, l, shift, n_limb1;
    dlimb_t t;
        
    j = NB_MODS * (NB_MODS - 1) / 2 - nb_mods * (nb_mods - 1) / 2;
    mods_cr = ntt_mods_cr + j;
    mods_cr_inv = s->ntt_mods_cr_inv + j;

    shift = dpl & (LIMB_BITS - 1);
    if (shift == 0)
        base_mask1 = -1;
    else
        base_mask1 = ((limb_t)1 << shift) - 1;
    n_limb1 = ((unsigned)dpl - 1) / LIMB_BITS;
    for(j = 0; j < NB_MODS; j++) 
        carry[j] = 0;
    for(j = 0; j < NB_MODS; j++) 
        u[j] = 0; /*  避免警告。 */ 
    memset(tabr, 0, sizeof(limb_t) * r_len);
    fft_len = (limb_t)1 << fft_len_log2;
    len = bf_min(fft_len, (r_len * LIMB_BITS + dpl - 1) / dpl);
    for(i = 0; i < len; i++) {
        for(j = 0; j < nb_mods; j++)  {
            y[j] = ntt_limb_to_int(buf[i + fft_len * j], mods[j]);
        }

        /*  中国余数将获得混合基数表示。 */ 
        l = 0;
        for(j = 0; j < nb_mods - 1; j++) {
            for(k = j + 1; k < nb_mods; k++) {
                limb_t m;
                m = mods[k];
                /*  注意：submod()中没有溢出，因为模数按升序排序。 */ 
                y[k] = mul_mod_fast2(y[k] - y[j] + m, 
                                     mods_cr[l], m, mods_cr_inv[l]);
                l++;
            }
        }
        
        /*  恢复正常表示。 */ 
        u[0] = y[nb_mods - 1];
        l = 1;
        for(j = nb_mods - 2; j >= 1; j--) {
            r = y[j];
            for(k = 0; k < l; k++) {
                t = (dlimb_t)u[k] * mods[j] + r;
                r = t >> LIMB_BITS;
                u[k] = t;
            }
            u[l] = r;
            l++;
        }
        
        /*  最后一步添加进位。 */ 
        r = y[0];
        for(k = 0; k < l; k++) {
            t = (dlimb_t)u[k] * mods[j] + r + carry[k];
            r = t >> LIMB_BITS;
            u[k] = t;
        }
        u[l] = r + carry[l];

#if 0
        printf("%" PRId64 ": ", (int64_t)i);
        for(j = nb_mods - 1; j >= 0; j--) {
            printf(" " FMT_LIMB, u[j]);
        }
        printf("\n");
#endif
        
        /*  写下数字。 */ 
        pos = i * dpl;
        for(j = 0; j < n_limb1; j++) {
            put_bits(tabr, r_len, pos, u[j]);
            pos += LIMB_BITS;
        }
        put_bits(tabr, r_len, pos, u[n_limb1] & base_mask1);
        /*  按DPL位数移位并设置进位。 */ 
        if (shift == 0) {
            for(j = n_limb1 + 1; j < nb_mods; j++)
                carry[j - (n_limb1 + 1)] = u[j];
        } else {
            for(j = n_limb1; j < nb_mods - 1; j++) {
                carry[j - n_limb1] = (u[j] >> shift) |
                    (u[j + 1] << (LIMB_BITS - shift));
            }
            carry[nb_mods - 1 - n_limb1] = u[nb_mods - 1] >> shift;
        }
    }
}
#endif

static int ntt_static_init(bf_context_t *s1)
{
    BFNTTState *s;
    int inverse, i, j, k, l;
    limb_t c, c_inv, c_inv2, m, m_inv;

    if (s1->ntt_state)
        return 0;
#if defined(__AVX2__)
    s = bf_aligned_malloc(s1, sizeof(*s), 64);
#else
    s = bf_malloc(s1, sizeof(*s));
#endif
    if (!s)
        return -1;
    memset(s, 0, sizeof(*s));
    s1->ntt_state = s;
    s->ctx = s1;
    
    for(j = 0; j < NB_MODS; j++) {
        m = ntt_mods[j];
        m_inv = init_mul_mod_fast(m);
        s->ntt_mods_div[j] = m_inv;
#if defined(__AVX2__)
        s->ntt_mods_vec[j] = _mm256_set1_pd(m);
        s->ntt_mods_inv_vec[j] = _mm256_set1_pd(1.0 / (double)m);
#endif
        c_inv2 = (m + 1) / 2; /*  1/2。 */ 
        c_inv = 1;
        for(i = 0; i <= NTT_PROOT_2EXP; i++) {
            s->ntt_len_inv[j][i][0] = c_inv;
            s->ntt_len_inv[j][i][1] = init_mul_mod_fast2(c_inv, m);
            c_inv = mul_mod_fast(c_inv, c_inv2, m, m_inv);
        }

        for(inverse = 0; inverse < 2; inverse++) {
            c = ntt_proot[inverse][j];
            for(i = 0; i < NTT_PROOT_2EXP; i++) {
                s->ntt_proot_pow[j][inverse][NTT_PROOT_2EXP - i] = c;
                s->ntt_proot_pow_inv[j][inverse][NTT_PROOT_2EXP - i] =
                    init_mul_mod_fast2(c, m);
                c = mul_mod_fast(c, c, m, m_inv);
            }
        }
    }

    l = 0;
    for(j = 0; j < NB_MODS - 1; j++) {
        for(k = j + 1; k < NB_MODS; k++) {
#if defined(__AVX2__)
            s->ntt_mods_cr_vec[l] = _mm256_set1_pd(int_to_ntt_limb2(ntt_mods_cr[l],
                                                                    ntt_mods[k]));
#else
            s->ntt_mods_cr_inv[l] = init_mul_mod_fast2(ntt_mods_cr[l],
                                                       ntt_mods[k]);
#endif
            l++;
        }
    }
    return 0;
}

int bf_get_fft_size(int *pdpl, int *pnb_mods, limb_t len)
{
    int dpl, fft_len_log2, n_bits, nb_mods, dpl_found, fft_len_log2_found;
    int int_bits, nb_mods_found;
    limb_t cost, min_cost;
    
    min_cost = -1;
    dpl_found = 0;
    nb_mods_found = 4;
    fft_len_log2_found = 0;
    for(nb_mods = 3; nb_mods <= NB_MODS; nb_mods++) {
        int_bits = ntt_int_bits[NB_MODS - nb_mods];
        dpl = bf_min((int_bits - 4) / 2,
                     2 * LIMB_BITS + 2 * NTT_MOD_LOG2_MIN - NTT_MOD_LOG2_MAX);
        for(;;) {
            fft_len_log2 = ceil_log2((len * LIMB_BITS + dpl - 1) / dpl);
            if (fft_len_log2 > NTT_PROOT_2EXP)
                goto next;
            n_bits = fft_len_log2 + 2 * dpl;
            if (n_bits <= int_bits) {
                cost = ((limb_t)(fft_len_log2 + 1) << fft_len_log2) * nb_mods;
                //  Printf(“n=%d DPL=%d：Cost=%”PRId64“\n”，nb_mods，DPL，(Int64_T)Cost)；
                if (cost < min_cost) {
                    min_cost = cost;
                    dpl_found = dpl;
                    nb_mods_found = nb_mods;
                    fft_len_log2_found = fft_len_log2;
                }
                break;
            }
            dpl--;
            if (dpl == 0)
                break;
        }
    next: ;
    }
    if (!dpl_found)
        abort();
    /*  如果可能，限制DPL以降低肢体/NTT转换的固定成本。 */ 
    if (dpl_found > (LIMB_BITS + NTT_MOD_LOG2_MIN) &&
        ((limb_t)(LIMB_BITS + NTT_MOD_LOG2_MIN) << fft_len_log2_found) >=
        len * LIMB_BITS) {
        dpl_found = LIMB_BITS + NTT_MOD_LOG2_MIN;
    }
    *pnb_mods = nb_mods_found;
    *pdpl = dpl_found;
    return fft_len_log2_found;
}

static no_inline void fft_mul(bf_t *res, limb_t *a_tab, limb_t a_len,
                              limb_t *b_tab, limb_t b_len, int mul_flags)
{
    bf_context_t *s1 = res->ctx;
    BFNTTState *s;
    int dpl, fft_len_log2, j, nb_mods, reduced_mem;
    slimb_t len, fft_len;
    NTTLimb *buf1, *buf2, *ptr;
#if defined(USE_MUL_CHECK)
    limb_t ha, hb, hr, h_ref;
#endif
    
    ntt_static_init(s1);
    s = s1->ntt_state;
    
    /*  查找每个肢体的最佳位数(DPL)。 */ 
    len = a_len + b_len;
    fft_len_log2 = bf_get_fft_size(&dpl, &nb_mods, len);
    fft_len = (uint64_t)1 << fft_len_log2;
    //  Printf(“len=%”PRId64“fft_len_log2=%d dpl=%d\n”，len，fft_len_log2，dpl)；
#if defined(USE_MUL_CHECK)
    ha = mp_mod1(a_tab, a_len, BF_CHKSUM_MOD, 0);
    hb = mp_mod1(b_tab, b_len, BF_CHKSUM_MOD, 0);
#endif
    if ((mul_flags & (FFT_MUL_R_OVERLAP_A | FFT_MUL_R_OVERLAP_B)) == 0) {
        bf_resize(res, 0);
    } else if (mul_flags & FFT_MUL_R_OVERLAP_B) {
        limb_t *tmp_tab, tmp_len;
        /*  最好先释放‘b’ */ 
        tmp_tab = a_tab;
        a_tab = b_tab;
        b_tab = tmp_tab;
        tmp_len = a_len;
        a_len = b_len;
        b_len = tmp_len;
    }
    buf1 = ntt_malloc(s, sizeof(NTTLimb) * fft_len * nb_mods);
    limb_to_ntt(s, buf1, fft_len, a_tab, a_len, dpl,
                NB_MODS - nb_mods, nb_mods);
    if ((mul_flags & (FFT_MUL_R_OVERLAP_A | FFT_MUL_R_OVERLAP_B)) == 
        FFT_MUL_R_OVERLAP_A) {
        bf_resize(res, 0);
    }
    reduced_mem = (fft_len_log2 >= 14);
    if (!reduced_mem) {
        buf2 = ntt_malloc(s, sizeof(NTTLimb) * fft_len * nb_mods);
        limb_to_ntt(s, buf2, fft_len, b_tab, b_len, dpl,
                    NB_MODS - nb_mods, nb_mods);
        bf_resize(res, 0); /*  在RES==b的情况下。 */ 
    } else {
        buf2 = ntt_malloc(s, sizeof(NTTLimb) * fft_len);
    }
    for(j = 0; j < nb_mods; j++) {
        if (reduced_mem) {
            limb_to_ntt(s, buf2, fft_len, b_tab, b_len, dpl,
                        NB_MODS - nb_mods + j, 1);
            ptr = buf2;
        } else {
            ptr = buf2 + fft_len * j;
        }
        ntt_conv(s, buf1 + fft_len * j, ptr,
                 fft_len_log2, fft_len_log2, j + NB_MODS - nb_mods);
    }
    bf_resize(res, 0); /*  在Res==b和减少的MEM的情况下。 */ 
    ntt_free(s, buf2);
    bf_resize(res, len);
    ntt_to_limb(s, res->tab, len, buf1, fft_len_log2, dpl, nb_mods);
    ntt_free(s, buf1);
#if defined(USE_MUL_CHECK)
    hr = mp_mod1(res->tab, len, BF_CHKSUM_MOD, 0);
    h_ref = mul_mod(ha, hb, BF_CHKSUM_MOD);
    if (hr != h_ref) {
        printf("ntt_mul_error: len=%" PRId_LIMB " fft_len_log2=%d dpl=%d nb_mods=%d\n",
               len, fft_len_log2, dpl, nb_mods);
        //  Printf(“ha=0x”FMT_Limb“HB=0x”FMT_Limb“hr=0x”FMT_Limb“Expect=0x”FMT_Limb“\n”，ha，hb，hr，h_ref)；
        exit(1);
    }
#endif    
}

#else /*  USE_FFT_MUL。 */ 

int bf_get_fft_size(int *pdpl, int *pnb_mods, limb_t len)
{
    return 0;
}

#endif /*  ！USE_FFT_MUL */ 
