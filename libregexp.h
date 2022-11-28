// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *正则表达式引擎**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#ifndef LIBREGEXP_H
#define LIBREGEXP_H

#include <stddef.h>

#include "libunicode.h"

#define LRE_BOOL  int       /*  用于文档编制。 */ 

#define LRE_FLAG_GLOBAL     (1 << 0)
#define LRE_FLAG_IGNORECASE (1 << 1)
#define LRE_FLAG_MULTILINE  (1 << 2)
#define LRE_FLAG_DOTALL     (1 << 3)
#define LRE_FLAG_UTF16      (1 << 4)
#define LRE_FLAG_STICKY     (1 << 5)

#define LRE_FLAG_NAMED_GROUPS (1 << 7) /*  命名组出现在regexp中。 */ 

uint8_t *lre_compile(int *plen, char *error_msg, int error_msg_size,
                     const char *buf, size_t buf_len, int re_flags,
                     void *opaque);
int lre_get_capture_count(const uint8_t *bc_buf);
int lre_get_flags(const uint8_t *bc_buf);
int lre_exec(uint8_t **capture,
             const uint8_t *bc_buf, const uint8_t *cbuf, int cindex, int clen,
             int cbuf_type, void *opaque);

int lre_parse_escape(const uint8_t **pp, int allow_utf16);
LRE_BOOL lre_is_space(int c);

/*  必须由用户提供。 */ 
LRE_BOOL lre_check_stack_overflow(void *opaque, size_t alloca_size); 
void *lre_realloc(void *opaque, void *ptr, size_t size);

/*  JS识别符测试。 */ 
extern uint32_t const lre_id_start_table_ascii[4];
extern uint32_t const lre_id_continue_table_ascii[4];

static inline int lre_js_is_ident_first(int c)
{
    if ((uint32_t)c < 128) {
        return (lre_id_start_table_ascii[c >> 5] >> (c & 31)) & 1;
    } else {
#ifdef CONFIG_ALL_UNICODE
        return lre_is_id_start(c);
#else
        return !lre_is_space(c);
#endif
    }
}

static inline int lre_js_is_ident_next(int c)
{
    if ((uint32_t)c < 128) {
        return (lre_id_continue_table_ascii[c >> 5] >> (c & 31)) & 1;
    } else {
        /*  在标识符中接受ZWNJ和ZWJ。 */ 
#ifdef CONFIG_ALL_UNICODE
        return lre_is_id_continue(c) || c == 0x200C || c == 0x200D;
#else
        return !lre_is_space(c) || c == 0x200C || c == 0x200D;
#endif
    }
}

#undef LRE_BOOL

#endif /*  LIBREGEXP_H */ 
