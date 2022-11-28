// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *C公用事业**版权所有(C)2017年Fabrice Bellard*版权所有(C)2018年查理·戈登**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#ifndef CUTILS_H
#define CUTILS_H

#include <stdlib.h>
#include <inttypes.h>

/*  设置CPU是否为高字节顺序。 */ 
#undef WORDS_BIGENDIAN

#ifdef _MSC_VER
#include <windows.h>
#define likely(x)       x
#define unlikely(x)     x
#define force_inline __forceinline
#define no_inline __declspec(noinline)
#define __maybe_unused
#define alloca _alloca
#else
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define force_inline inline __attribute__((always_inline))
#define no_inline __attribute__((noinline))
#define __maybe_unused __attribute__((unused))
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)    tostring(s)
#define tostring(s)     #s

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif
#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef int BOOL;

#ifndef FALSE
enum {
    FALSE = 0,
    TRUE = 1,
};
#endif

void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int strstart(const char *str, const char *val, const char **ptr);
int has_suffix(const char *str, const char *suffix);

static inline int max_int(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int min_int(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline uint32_t max_uint32(uint32_t a, uint32_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline uint32_t min_uint32(uint32_t a, uint32_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline int64_t max_int64(int64_t a, int64_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int64_t min_int64(int64_t a, int64_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

/*  警告：如果a=0，则未定义。 */ 
static inline int clz32(unsigned int a)
{
#ifdef _MSC_VER
    DWORD leading_zero = 0;
    
    if ( _BitScanReverse( &leading_zero, a ) ){
       return 31 - leading_zero;
    }
    else{
         //  如上所述。
         return 32;
    }
#else
    return __builtin_clz(a);
#endif
}

/*  警告：如果a=0，则未定义。 */ 
static inline int clz64(uint64_t a)
{
    return __builtin_clzll(a);
}

#if defined(_MSC_VER)
__inline int __builtin_ctz(int v)
{
	int pos;
	if (v == 0)
		return 0;

	__asm
	{
		bsf eax, dword ptr[v];
	}
}

__inline int __builtin_clzll(int v)
{
	if (v == 0)
		return 31;

	__asm
	{
		bsr ecx, dword ptr[v];
		mov eax, 1Fh;
		sub eax, ecx;
	}
}
#endif
/*  警告：如果a=0，则未定义。 */ 
static inline int ctz32(unsigned int a)
{
    return __builtin_ctz(a);
}

/*  警告：如果a=0，则未定义。 */ 
static inline int ctz64(uint64_t a)
{
    return __builtin_ctzll(a);
}

#ifdef _MSC_VER
#pragma pack(push,1)
#define PACKED
#else
#define PACKED __attribute__((packed))
#endif
struct PACKED packed_u64 {
    uint64_t v;
};

struct PACKED packed_u32 {
    uint32_t v;
};

struct PACKED packed_u16 {
    uint16_t v;
};
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#undef PACKED

static inline uint64_t get_u64(const uint8_t *tab)
{
    return ((const struct packed_u64 *)tab)->v;
}

static inline int64_t get_i64(const uint8_t *tab)
{
    return (int64_t)((const struct packed_u64 *)tab)->v;
}

static inline void put_u64(uint8_t *tab, uint64_t val)
{
    ((struct packed_u64 *)tab)->v = val;
}

static inline uint32_t get_u32(const uint8_t *tab)
{
    return ((const struct packed_u32 *)tab)->v;
}

static inline int32_t get_i32(const uint8_t *tab)
{
    return (int32_t)((const struct packed_u32 *)tab)->v;
}

static inline void put_u32(uint8_t *tab, uint32_t val)
{
    ((struct packed_u32 *)tab)->v = val;
}

static inline uint32_t get_u16(const uint8_t *tab)
{
    return ((const struct packed_u16 *)tab)->v;
}

static inline int32_t get_i16(const uint8_t *tab)
{
    return (int16_t)((const struct packed_u16 *)tab)->v;
}

static inline void put_u16(uint8_t *tab, uint16_t val)
{
    ((struct packed_u16 *)tab)->v = val;
}

static inline uint32_t get_u8(const uint8_t *tab)
{
    return *tab;
}

static inline int32_t get_i8(const uint8_t *tab)
{
    return (int8_t)*tab;
}

static inline void put_u8(uint8_t *tab, uint8_t val)
{
    *tab = val;
}

static inline uint16_t bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static inline uint32_t bswap32(uint32_t v)
{
    return ((v & 0xff000000) >> 24) | ((v & 0x00ff0000) >>  8) |
        ((v & 0x0000ff00) <<  8) | ((v & 0x000000ff) << 24);
}

static inline uint64_t bswap64(uint64_t v)
{
    return ((v & ((uint64_t)0xff << (7 * 8))) >> (7 * 8)) | 
        ((v & ((uint64_t)0xff << (6 * 8))) >> (5 * 8)) | 
        ((v & ((uint64_t)0xff << (5 * 8))) >> (3 * 8)) | 
        ((v & ((uint64_t)0xff << (4 * 8))) >> (1 * 8)) | 
        ((v & ((uint64_t)0xff << (3 * 8))) << (1 * 8)) | 
        ((v & ((uint64_t)0xff << (2 * 8))) << (3 * 8)) | 
        ((v & ((uint64_t)0xff << (1 * 8))) << (5 * 8)) | 
        ((v & ((uint64_t)0xff << (0 * 8))) << (7 * 8));
}

/*  XXX：应采用额外的参数将松弛信息传递给调用方。 */ 
typedef void *DynBufReallocFunc(void *opaque, void *ptr, size_t size);

typedef struct DynBuf {
    uint8_t *buf;
    size_t size;
    size_t allocated_size;
    BOOL error; /*  如果发生内存分配错误，则为True。 */ 
    DynBufReallocFunc *realloc_func;
    void *opaque; /*  对于realloc_func。 */ 
} DynBuf;

void dbuf_init(DynBuf *s);
void dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func);
int dbuf_realloc(DynBuf *s, size_t new_size);
int dbuf_write(DynBuf *s, size_t offset, const uint8_t *data, size_t len);
int dbuf_put(DynBuf *s, const uint8_t *data, size_t len);
int dbuf_put_self(DynBuf *s, size_t offset, size_t len);
int dbuf_putc(DynBuf *s, uint8_t c);
int dbuf_putstr(DynBuf *s, const char *str);
static inline int dbuf_put_u16(DynBuf *s, uint16_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 2);
}
static inline int dbuf_put_u32(DynBuf *s, uint32_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 4);
}
static inline int dbuf_put_u64(DynBuf *s, uint64_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 8);
}
#if defined(__GNUC__) || defined(__clang__)
#define FMT_HACK __attribute__((format(printf, 2, 3)))
#else
#define FMT_HACK
#endif
int FMT_HACK dbuf_printf(DynBuf *s,const char *fmt, ...);
#undef FMT_HACK

void dbuf_free(DynBuf *s);
static inline BOOL dbuf_error(DynBuf *s) {
    return s->error;
}

#define UTF8_CHAR_LEN_MAX 6

int unicode_to_utf8(uint8_t *buf, unsigned int c);
int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);

static inline int from_hex(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
}

void rqsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *, void *),
            void *arg);

#endif  /*  刀具_H */ 
