// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *Unicode实用程序**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#ifndef LIBUNICODE_H
#define LIBUNICODE_H

#include <inttypes.h>

#define LRE_BOOL  int       /*  用于文档编制。 */ 

/*  将其定义为包括所有Unicode表(大于40KB)。 */ 
#define CONFIG_ALL_UNICODE

#define LRE_CC_RES_LEN_MAX 3

typedef enum {
    UNICODE_NFC,
    UNICODE_NFD,
    UNICODE_NFKC,
    UNICODE_NFKD,
} UnicodeNormalizationEnum;

int lre_case_conv(uint32_t *res, uint32_t c, int conv_type);
LRE_BOOL lre_is_cased(uint32_t c);
LRE_BOOL lre_is_case_ignorable(uint32_t c);

/*  焦炭范围。 */ 

typedef struct {
    int len; /*  在点数上，总是均匀的。 */ 
    int size;
    uint32_t *points; /*  按递增价值排序的点。 */ 
    void *mem_opaque;
    void *(*realloc_func)(void *opaque, void *ptr, size_t size);
} CharRange;

typedef enum {
    CR_OP_UNION,
    CR_OP_INTER,
    CR_OP_XOR,
} CharRangeOpEnum;

void cr_init(CharRange *cr, void *mem_opaque, void *(*realloc_func)(void *opaque, void *ptr, size_t size));
void cr_free(CharRange *cr);
int cr_realloc(CharRange *cr, int size);
int cr_copy(CharRange *cr, const CharRange *cr1);

static inline int cr_add_point(CharRange *cr, uint32_t v)
{
    if (cr->len >= cr->size) {
        if (cr_realloc(cr, cr->len + 1))
            return -1;
    }
    cr->points[cr->len++] = v;
    return 0;
}

static inline int cr_add_interval(CharRange *cr, uint32_t c1, uint32_t c2)
{
    if ((cr->len + 2) > cr->size) {
        if (cr_realloc(cr, cr->len + 2))
            return -1;
    }
    cr->points[cr->len++] = c1;
    cr->points[cr->len++] = c2;
    return 0;
}

int cr_union1(CharRange *cr, const uint32_t *b_pt, int b_len);

static inline int cr_union_interval(CharRange *cr, uint32_t c1, uint32_t c2)
{
    uint32_t b_pt[2];
    b_pt[0] = c1;
    b_pt[1] = c2 + 1;
    return cr_union1(cr, b_pt, 2);
}

int cr_op(CharRange *cr, const uint32_t *a_pt, int a_len,
          const uint32_t *b_pt, int b_len, int op);

int cr_invert(CharRange *cr);

#ifdef CONFIG_ALL_UNICODE

LRE_BOOL lre_is_id_start(uint32_t c);
LRE_BOOL lre_is_id_continue(uint32_t c);

int unicode_normalize(uint32_t **pdst, const uint32_t *src, int src_len,
                      UnicodeNormalizationEnum n_type,
                      void *opaque, void *(*realloc_func)(void *opaque, void *ptr, size_t size));

/*  Unicode字符范围函数。 */ 

int unicode_script(CharRange *cr,
                   const char *script_name, LRE_BOOL is_ext);
int unicode_general_category(CharRange *cr, const char *gc_name);
int unicode_prop(CharRange *cr, const char *prop_name);

#endif /*  CONFIG_ALL_Unicode。 */ 

#undef LRE_BOOL

#endif /*  LIBUNICODE_H */ 
