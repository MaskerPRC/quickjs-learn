// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *正则表达式引擎**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include "cutils.h"
#include "libregexp.h"

/*  待办事项：-为字符范围(非)添加完整的Unicode规范化规则真的很有用，但需要精确的“忽略”兼容性)。-添加锁定步骤执行模式(=线性时间执行保证)当正则表达式是“简单的”时，即既没有反向引用也没有复杂的前瞻。操作码是为该执行而设计的模特。 */ 

#if defined(TEST)
#define DUMP_REOP
#endif

typedef enum {
#define DEF(id, size) REOP_ ## id,
#include "libregexp-opcode.h"
#undef DEF
    REOP_COUNT,
} REOPCodeEnum;

#define CAPTURE_COUNT_MAX 255
#define STACK_SIZE_MAX 255

/*  Unicode代码点。 */ 
#define CP_LS   0x2028
#define CP_PS   0x2029

#define TMP_BUF_SIZE 128

typedef struct {
    DynBuf byte_code;
    const uint8_t *buf_ptr;
    const uint8_t *buf_end;
    const uint8_t *buf_start;
    int re_flags;
    BOOL is_utf16;
    BOOL ignore_case;
    BOOL dotall;
    int capture_count;
    int total_capture_count; /*  -1=尚未计算。 */ 
    int has_named_captures; /*  -1=不知道，0=否，1=是。 */ 
    void *mem_opaque;
    DynBuf group_names;
    union {
        char error_msg[TMP_BUF_SIZE];
        char tmp_buf[TMP_BUF_SIZE];
    } u;
} REParseState;

typedef struct {
#ifdef DUMP_REOP
    const char *name;
#endif
    uint8_t size;
} REOpCode;

static const REOpCode reopcode_info[REOP_COUNT] = {
#ifdef DUMP_REOP
#define DEF(id, size) { #id, size },
#else
#define DEF(id, size) { size },
#endif
#include "libregexp-opcode.h"
#undef DEF
};

#define RE_HEADER_FLAGS         0
#define RE_HEADER_CAPTURE_COUNT 1
#define RE_HEADER_STACK_SIZE    2

#define RE_HEADER_LEN 7

static inline int is_digit(int c) {
    return c >= '0' && c <= '9';
}

/*  在位置‘pos’处插入‘len’个字节。 */ 
static void dbuf_insert(DynBuf *s, int pos, int len)
{
    dbuf_realloc(s, s->size + len);
    memmove(s->buf + pos + len, s->buf + pos, s->size - pos);
    s->size += len;
}

/*  使用特定的JS regexp规则进行规范化。 */ 
static uint32_t lre_canonicalize(uint32_t c, BOOL is_utf16)
{
    uint32_t res[LRE_CC_RES_LEN_MAX];
    int len;
    if (is_utf16) {
        if (likely(c < 128)) {
            if (c >= 'A' && c <= 'Z')
                c = c - 'A' + 'a';
        } else {
            lre_case_conv(res, c, 2);
            c = res[0];
        }
    } else {
        if (likely(c < 128)) {
            if (c >= 'a' && c <= 'z')
                c = c - 'a' + 'A';
        } else {
            /*  传统regexp：如果单个字符&gt;=128，则转换为大写。 */ 
            len = lre_case_conv(res, c, FALSE);
            if (len == 1 && res[0] >= 128)
                c = res[0];
        }
    }
    return c;
}

static const uint16_t char_range_d[] = {
    1,
    0x0030, 0x0039 + 1,
};

/*  Zs、Zl或Zp属性的码位范围。 */ 
static const uint16_t char_range_s[] = {
    10,
    0x0009, 0x000D + 1,
    0x0020, 0x0020 + 1,
    0x00A0, 0x00A0 + 1,
    0x1680, 0x1680 + 1,
    0x2000, 0x200A + 1,
    /*  2028；行分隔符；Zl；0；WS；N； */ 
    /*  2029；段落分隔符；ZP；0；B；N； */ 
    0x2028, 0x2029 + 1,
    0x202F, 0x202F + 1,
    0x205F, 0x205F + 1,
    0x3000, 0x3000 + 1,
    /*  零宽度不间断空格；CF；0；BN；N；字节顺序标记； */ 
    0xFEFF, 0xFEFF + 1,
};

BOOL lre_is_space(int c)
{
    int i, n, low, high;
    n = (countof(char_range_s) - 1) / 2;
    for(i = 0; i < n; i++) {
        low = char_range_s[2 * i + 1];
        if (c < low)
            return FALSE;
        high = char_range_s[2 * i + 2];
        if (c < high)
            return TRUE;
    }
    return FALSE;
}

uint32_t const lre_id_start_table_ascii[4] = {
    /*  $A-Z_a-z。 */ 
    0x00000000, 0x00000010, 0x87FFFFFE, 0x07FFFFFE
};

uint32_t const lre_id_continue_table_ascii[4] = {
    /*  $0-9 A-Z_a-Z。 */ 
    0x00000000, 0x03FF0010, 0x87FFFFFE, 0x07FFFFFE
};


static const uint16_t char_range_w[] = {
    4,
    0x0030, 0x0039 + 1,
    0x0041, 0x005A + 1,
    0x005F, 0x005F + 1,
    0x0061, 0x007A + 1,
};

#define CLASS_RANGE_BASE 0x40000000

typedef enum {
    CHAR_RANGE_d,
    CHAR_RANGE_D,
    CHAR_RANGE_s,
    CHAR_RANGE_S,
    CHAR_RANGE_w,
    CHAR_RANGE_W,
} CharRangeEnum;

static const uint16_t *char_range_table[] = {
    char_range_d,
    char_range_s,
    char_range_w,
};

static int cr_init_char_range(REParseState *s, CharRange *cr, uint32_t c)
{
    BOOL invert;
    const uint16_t *c_pt;
    int len, i;
    
    invert = c & 1;
    c_pt = char_range_table[c >> 1];
    len = *c_pt++;
    cr_init(cr, s->mem_opaque, lre_realloc);
    for(i = 0; i < len * 2; i++) {
        if (cr_add_point(cr, c_pt[i]))
            goto fail;
    }
    if (invert) {
        if (cr_invert(cr))
            goto fail;
    }
    return 0;
 fail:
    cr_free(cr);
    return -1;
}

static int cr_canonicalize(CharRange *cr)
{
    CharRange a;
    uint32_t pt[2];
    int i, ret;

    cr_init(&a, cr->mem_opaque, lre_realloc);
    pt[0] = 'a';
    pt[1] = 'z' + 1;
    ret = cr_op(&a, cr->points, cr->len, pt, 2, CR_OP_INTER);
    if (ret)
        goto fail;
    /*  转换为大写。 */ 
    /*  XXX：通用的Unicode用例要复杂得多而且也不是很有用。 */ 
    for(i = 0; i < a.len; i++) {
        a.points[i] += 'A' - 'a';
    }
    /*  注：为简单起见，我们保留小写范围。 */ 
    ret = cr_union1(cr, a.points, a.len);
 fail:
    cr_free(&a);
    return ret;
}

#ifdef DUMP_REOP
static __maybe_unused void lre_dump_bytecode(const uint8_t *buf,
                                                     int buf_len)
{
    int pos, len, opcode, bc_len, re_flags, i;
    uint32_t val;
    
    assert(buf_len >= RE_HEADER_LEN);

    re_flags=  buf[0];
    bc_len = get_u32(buf + 3);
    assert(bc_len + RE_HEADER_LEN <= buf_len);
    printf("flags: 0x%x capture_count=%d stack_size=%d\n",
           re_flags, buf[1], buf[2]);
    if (re_flags & LRE_FLAG_NAMED_GROUPS) {
        const char *p;
        p = (char *)buf + RE_HEADER_LEN + bc_len;
        printf("named groups: ");
        for(i = 1; i < buf[1]; i++) {
            if (i != 1)
                printf(",");
            printf("<%s>", p);
            p += strlen(p) + 1;
        }
        printf("\n");
        assert(p == (char *)(buf + buf_len));
    }
    printf("bytecode_len=%d\n", bc_len);

    buf += RE_HEADER_LEN;
    pos = 0;
    while (pos < bc_len) {
        printf("%5u: ", pos);
        opcode = buf[pos];
        len = reopcode_info[opcode].size;
        if (opcode >= REOP_COUNT) {
            printf(" invalid opcode=0x%02x\n", opcode);
            break;
        }
        if ((pos + len) > bc_len) {
            printf(" buffer overflow (opcode=0x%02x)\n", opcode);
            break;
        }
        printf("%s", reopcode_info[opcode].name);
        switch(opcode) {
        case REOP_char:
            val = get_u16(buf + pos + 1);
            if (val >= ' ' && val <= 126)
                printf(" '%c'", val);
            else
                printf(" 0x%04x", val);
            break;
        case REOP_char32:
            val = get_u32(buf + pos + 1);
            if (val >= ' ' && val <= 126)
                printf(" '%c'", val);
            else
                printf(" 0x%08x", val);
            break;
        case REOP_goto:
        case REOP_split_goto_first:
        case REOP_split_next_first:
        case REOP_loop:
        case REOP_lookahead:
        case REOP_negative_lookahead:
        case REOP_bne_char_pos:
            val = get_u32(buf + pos + 1);
            val += (pos + 5);
            printf(" %u", val);
            break;
        case REOP_simple_greedy_quant:
            printf(" %u %u %u %u",
                   get_u32(buf + pos + 1) + (pos + 17),
                   get_u32(buf + pos + 1 + 4),
                   get_u32(buf + pos + 1 + 8),
                   get_u32(buf + pos + 1 + 12));
            break;
        case REOP_save_start:
        case REOP_save_end:
        case REOP_back_reference:
        case REOP_backward_back_reference:
            printf(" %u", buf[pos + 1]);
            break;
        case REOP_save_reset:
            printf(" %u %u", buf[pos + 1], buf[pos + 2]);
            break;
        case REOP_push_i32:
            val = get_u32(buf + pos + 1);
            printf(" %d", val);
            break;
        case REOP_range:
            {
                int n, i;
                n = get_u16(buf + pos + 1);
                len += n * 4;
                for(i = 0; i < n * 2; i++) {
                    val = get_u16(buf + pos + 3 + i * 2);
                    printf(" 0x%04x", val);
                }
            }
            break;
        case REOP_range32:
            {
                int n, i;
                n = get_u16(buf + pos + 1);
                len += n * 8;
                for(i = 0; i < n * 2; i++) {
                    val = get_u32(buf + pos + 3 + i * 4);
                    printf(" 0x%08x", val);
                }
            }
            break;
        default:
            break;
        }
        printf("\n");
        pos += len;
    }
}
#endif

static void re_emit_op(REParseState *s, int op)
{
    dbuf_putc(&s->byte_code, op);
}

/*  返回u32值的偏移量。 */ 
static int re_emit_op_u32(REParseState *s, int op, uint32_t val)
{
    int pos;
    dbuf_putc(&s->byte_code, op);
    pos = s->byte_code.size;
    dbuf_put_u32(&s->byte_code, val);
    return pos;
}

static int re_emit_goto(REParseState *s, int op, uint32_t val)
{
    int pos;
    dbuf_putc(&s->byte_code, op);
    pos = s->byte_code.size;
    dbuf_put_u32(&s->byte_code, val - (pos + 4));
    return pos;
}

static void re_emit_op_u8(REParseState *s, int op, uint32_t val)
{
    dbuf_putc(&s->byte_code, op);
    dbuf_putc(&s->byte_code, val);
}

static void re_emit_op_u16(REParseState *s, int op, uint32_t val)
{
    dbuf_putc(&s->byte_code, op);
    dbuf_put_u16(&s->byte_code, val);
}

#ifdef _MSC_VER
#define FMT_HACK
#else
#define FMT_HACK __attribute__((format(printf, 2, 3)))
#endif
static int FMT_HACK re_parse_error(REParseState *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->u.error_msg, sizeof(s->u.error_msg), fmt, ap);
    va_end(ap);
    return -1;
}
#undef FMT_HACK

/*  如果溢出，则返回-1。 */ 
static int parse_digits(const uint8_t **pp)
{
    const uint8_t *p;
    uint64_t v;
    int c;
    
    p = *pp;
    v = 0;
    for(;;) {
        c = *p;
        if (c < '0' || c > '9')
            break;
        v = v * 10 + c - '0';
        if (v >= INT32_MAX)
            return -1;
        p++;
    }
    *pp = p;
    return v;
}

static int re_parse_expect(REParseState *s, const uint8_t **pp, int c)
{
    const uint8_t *p;
    p = *pp;
    if (*p != c)
        return re_parse_error(s, "expecting '%c'", c);
    p++;
    *pp = p;
    return 0;
}

/*  分析转义序列，*pp指向‘\’之后：Allow_utf16值：0：不允许UTF-16转义1：允许UTF-16转义2：允许UTF-16转义和代理对转义转换为Unicode字符(Unicode regexp大小写)。如果识别，则返回Unicode字符并更新*pp，如果格式错误，则返回-1，否则返回-2。 */ 
int lre_parse_escape(const uint8_t **pp, int allow_utf16)
{
    const uint8_t *p;
    uint32_t c;

    p = *pp;
    c = *p++;
    switch(c) {
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = '\f';
        break;
    case 'n':
        c = '\n';
        break;
    case 'r':
        c = '\r';
        break;
    case 't':
        c = '\t';
        break;
    case 'v':
        c = '\v';
        break;
    case 'x':
    case 'u':
        {
            int h, n, i;
            uint32_t c1;
            
            if (*p == '{' && allow_utf16) {
                p++;
                c = 0;
                for(;;) {
                    h = from_hex(*p++);
                    if (h < 0)
                        return -1;
                    c = (c << 4) | h;
                    if (c > 0x10FFFF)
                        return -1;
                    if (*p == '}')
                        break;
                }
                p++;
            } else {
                if (c == 'x') {
                    n = 2;
                } else {
                    n = 4;
                }

                c = 0;
                for(i = 0; i < n; i++) {
                    h = from_hex(*p++);
                    if (h < 0) {
                        return -1;
                    }
                    c = (c << 4) | h;
                }
                if (c >= 0xd800 && c < 0xdc00 &&
                    allow_utf16 == 2 && p[0] == '\\' && p[1] == 'u') {
                    /*  将转义的代理项对转换为Unicode字符。 */ 
                    c1 = 0;
                    for(i = 0; i < 4; i++) {
                        h = from_hex(p[2 + i]);
                        if (h < 0)
                            break;
                        c1 = (c1 << 4) | h;
                    }
                    if (i == 4 && c1 >= 0xdc00 && c1 < 0xe000) {
                        p += 6;
                        c = (((c & 0x3ff) << 10) | (c1 & 0x3ff)) + 0x10000;
                    }
                }
            }
        }
        break;
    case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':
        c -= '0';
        if (allow_utf16 == 2) {
            /*  只接受\0，后面不跟数字。 */ 
            if (c != 0 || is_digit(*p))
                return -1;
        } else {
            /*  解析旧版八进制序列。 */ 
            uint32_t v;
            v = *p - '0';
            if (v > 7)
                break;
            c = (c << 3) | v;
            p++;
            if (c >= 32)
                break;
            v = *p - '0';
            if (v > 7)
                break;
            c = (c << 3) | v;
            p++;
        }
        break;
    default:
        return -2;
    }
    *pp = p;
    return c;
}

#ifdef CONFIG_ALL_UNICODE
/*  XXX：我们对名称和值使用相同的字符。 */ 
static BOOL is_unicode_char(int c)
{
    return ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c == '_'));
}

static int parse_unicode_property(REParseState *s, CharRange *cr,
                                  const uint8_t **pp, BOOL is_inv)
{
    const uint8_t *p;
    char name[64], value[64];
    char *q;
    BOOL script_ext;
    int ret;

    p = *pp;
    if (*p != '{')
        return re_parse_error(s, "expecting '{' after \\p");
    p++;
    q = name;
    while (is_unicode_char(*p)) {
        if ((q - name) > sizeof(name) - 1)
            goto unknown_property_name;
        *q++ = *p++;
    }
    *q = '\0';
    q = value;
    if (*p == '=') {
        p++;
        while (is_unicode_char(*p)) {
            if ((q - value) > sizeof(value) - 1)
                return re_parse_error(s, "unknown unicode property value");
            *q++ = *p++;
        }
    }
    *q = '\0';
    if (*p != '}')
        return re_parse_error(s, "expecting '}'");
    p++;
    //  Printf(“名称=%s值=%s\n”，名称，值)；

    if (!strcmp(name, "Script") || !strcmp(name, "sc")) {
        script_ext = FALSE;
        goto do_script;
    } else if (!strcmp(name, "Script_Extensions") || !strcmp(name, "scx")) {
        script_ext = TRUE;
    do_script:
        cr_init(cr, s->mem_opaque, lre_realloc);
        ret = unicode_script(cr, value, script_ext);
        if (ret) {
            cr_free(cr);
            if (ret == -2)
                return re_parse_error(s, "unknown unicode script");
            else
                goto out_of_memory;
        }
    } else if (!strcmp(name, "General_Category") || !strcmp(name, "gc")) {
        cr_init(cr, s->mem_opaque, lre_realloc);
        ret = unicode_general_category(cr, value);
        if (ret) {
            cr_free(cr);
            if (ret == -2)
                return re_parse_error(s, "unknown unicode general category");
            else
                goto out_of_memory;
        }
    } else if (value[0] == '\0') {
        cr_init(cr, s->mem_opaque, lre_realloc);
        ret = unicode_general_category(cr, name);
        if (ret == -1) {
            cr_free(cr);
            goto out_of_memory;
        }
        if (ret < 0) {
            ret = unicode_prop(cr, name);
            if (ret) {
                cr_free(cr);
                if (ret == -2)
                    goto unknown_property_name;
                else
                    goto out_of_memory;
            }
        }
    } else {
    unknown_property_name:
        return re_parse_error(s, "unknown unicode property name");
    }

    if (is_inv) {
        if (cr_invert(cr)) {
            cr_free(cr);
            return -1;
        }
    }
    *pp = p;
    return 0;
 out_of_memory:
    return re_parse_error(s, "out of memory");
}
#endif /*  CONFIG_ALL_Unicode。 */ 

/*  如果出错，则返回-1，否则返回字符或类范围(CLASS_RANGE_BASE)。对于类范围，‘cr’为已初始化。否则，它将被忽略。 */ 
static int get_class_atom(REParseState *s, CharRange *cr,
                          const uint8_t **pp, BOOL inclass)
{
    const uint8_t *p;
    uint32_t c;
    int ret;
    
    p = *pp;

    c = *p;
    switch(c) {
    case '\\':
        p++;
        if (p >= s->buf_end)
            goto unexpected_end;
        c = *p++;
        switch(c) {
        case 'd':
            c = CHAR_RANGE_d;
            goto class_range;
        case 'D':
            c = CHAR_RANGE_D;
            goto class_range;
        case 's':
            c = CHAR_RANGE_s;
            goto class_range;
        case 'S':
            c = CHAR_RANGE_S;
            goto class_range;
        case 'w':
            c = CHAR_RANGE_w;
            goto class_range;
        case 'W':
            c = CHAR_RANGE_W;
        class_range:
            if (cr_init_char_range(s, cr, c))
                return -1;
            c = CLASS_RANGE_BASE;
            break;
        case 'c':
            c = *p;
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (((c >= '0' && c <= '9') || c == '_') &&
                 inclass && !s->is_utf16)) {   /*  附件B.1.4。 */ 
                c &= 0x1f;
                p++;
            } else if (s->is_utf16) {
                goto invalid_escape;
            } else {
                /*  否则返回‘\’和‘c’ */ 
                p--;
                c = '\\';
            }
            break;
#ifdef CONFIG_ALL_UNICODE
        case 'p':
        case 'P':
            if (s->is_utf16) {
                if (parse_unicode_property(s, cr, &p, (c == 'P')))
                    return -1;
                c = CLASS_RANGE_BASE;
                break;
            }
            /*  失败。 */ 
#endif
        default:
            p--;
            ret = lre_parse_escape(&p, s->is_utf16 * 2);
            if (ret >= 0) {
                c = ret;
            } else {
                if (ret == -2 && *p != '\0' && strchr("^$\\.*+?()[]{}|/", *p)) {
                    /*  对这些字符进行转义始终有效。 */ 
                    goto normal_char;
                } else if (s->is_utf16) {
                invalid_escape:
                    return re_parse_error(s, "invalid escape sequence in regular expression");
                } else {
                    /*  只需忽略‘\’ */ 
                    goto normal_char;
                }
            }
            break;
        }
        break;
    case '\0':
        if (p >= s->buf_end) {
        unexpected_end:
            return re_parse_error(s, "unexpected end");
        }
        /*  失败。 */ 
    default:
    normal_char:
        /*  正常充电。 */ 
        if (c >= 128) {
            c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p);
            if ((unsigned)c > 0xffff && !s->is_utf16) {
                /*  XXX：应处理非BMP-1代码点。 */ 
                return re_parse_error(s, "malformed unicode char");
            }
        } else {
            p++;
        }
        break;
    }
    *pp = p;
    return c;
}

static int re_emit_range(REParseState *s, const CharRange *cr)
{
    int len, i;
    uint32_t high;
    
    len = (unsigned)cr->len / 2;
    if (len >= 65535)
        return re_parse_error(s, "too many ranges");
    if (len == 0) {
        /*  我不确定这是否真的会发生。发出的匹配总是错误。 */ 
        re_emit_op_u32(s, REOP_char32, -1);
    } else {
        high = cr->points[cr->len - 1];
        if (high == UINT32_MAX)
            high = cr->points[cr->len - 2];
        if (high <= 0xffff) {
            /*  可以使用16位范围并转换为0xffff=无穷大。 */ 
            re_emit_op_u16(s, REOP_range, len);
            for(i = 0; i < cr->len; i += 2) {
                dbuf_put_u16(&s->byte_code, cr->points[i]);
                high = cr->points[i + 1] - 1;
                if (high == UINT32_MAX - 1)
                    high = 0xffff;
                dbuf_put_u16(&s->byte_code, high);
            }
        } else {
            re_emit_op_u16(s, REOP_range32, len);
            for(i = 0; i < cr->len; i += 2) {
                dbuf_put_u32(&s->byte_code, cr->points[i]);
                dbuf_put_u32(&s->byte_code, cr->points[i + 1] - 1);
            }
        }
    }
    return 0;
}

static int re_parse_char_class(REParseState *s, const uint8_t **pp)
{
    const uint8_t *p;
    uint32_t c1, c2;
    CharRange cr_s, *cr = &cr_s;
    CharRange cr1_s, *cr1 = &cr1_s;
    BOOL invert;
    
    cr_init(cr, s->mem_opaque, lre_realloc);
    p = *pp;
    p++;    /*  跳过‘[’ */ 
    invert = FALSE;
    if (*p == '^') {
        p++;
        invert = TRUE;
    }
    for(;;) {
        if (*p == ']')
            break;
        c1 = get_class_atom(s, cr1, &p, TRUE);
        if ((int)c1 < 0)
            goto fail;
        if (*p == '-' && p[1] != ']') {
            const uint8_t *p0 = p + 1;
            if (c1 >= CLASS_RANGE_BASE) {
                if (s->is_utf16) {
                    cr_free(cr1);
                    goto invalid_class_range;
                }
                /*  附件B：匹配‘-’字符。 */ 
                goto class_atom;
            }
            c2 = get_class_atom(s, cr1, &p0, TRUE);
            if ((int)c2 < 0)
                goto fail;
            if (c2 >= CLASS_RANGE_BASE) {
                cr_free(cr1);
                if (s->is_utf16) {
                    goto invalid_class_range;
                }
                /*  附件B：匹配‘-’字符。 */ 
                goto class_atom;
            }
            p = p0;
            if (c2 < c1) {
            invalid_class_range:
                re_parse_error(s, "invalid class range");
                goto fail;
            }
            if (cr_union_interval(cr, c1, c2))
                goto memory_error;
        } else {
        class_atom:
            if (c1 >= CLASS_RANGE_BASE) {
                int ret;
                ret = cr_union1(cr, cr1->points, cr1->len);
                cr_free(cr1);
                if (ret)
                    goto memory_error;
            } else {
                if (cr_union_interval(cr, c1, c1))
                    goto memory_error;
            }
        }
    }
    if (s->ignore_case) {
        if (cr_canonicalize(cr))
            goto memory_error;
    }
    if (invert) {
        if (cr_invert(cr))
            goto memory_error;
    }
    if (re_emit_range(s, cr))
        goto fail;
    cr_free(cr);
    p++;    /*  跳过‘]’ */ 
    *pp = p;
    return 0;
 memory_error:
    re_parse_error(s, "out of memory");
 fail:
    cr_free(cr);
    return -1;
}

/*  返回：如果BC_buf[]中的操作码总是前进字符指针，则为1。如果字符指针不能前进，则为0。代码是否可能依赖于其先前执行的副作用(反向引用)。 */ 
static int re_check_advance(const uint8_t *bc_buf, int bc_buf_len)
{
    int pos, opcode, ret, len, i;
    uint32_t val, last;
    BOOL has_back_reference;
    uint8_t capture_bitmap[CAPTURE_COUNT_MAX];
    
    ret = -2; /*  尚不清楚。 */ 
    pos = 0;
    has_back_reference = FALSE;
    memset(capture_bitmap, 0, sizeof(capture_bitmap));
    
    while (pos < bc_buf_len) {
        opcode = bc_buf[pos];
        len = reopcode_info[opcode].size;
        switch(opcode) {
        case REOP_range:
            val = get_u16(bc_buf + pos + 1);
            len += val * 4;
            goto simple_char;
        case REOP_range32:
            val = get_u16(bc_buf + pos + 1);
            len += val * 8;
            goto simple_char;
        case REOP_char:
        case REOP_char32:
        case REOP_dot:
        case REOP_any:
        simple_char:
            if (ret == -2)
                ret = 1;
            break;
        case REOP_line_start:
        case REOP_line_end:
        case REOP_push_i32:
        case REOP_push_char_pos:
        case REOP_drop:
        case REOP_word_boundary:
        case REOP_not_word_boundary:
        case REOP_prev:
            /*  没有效果。 */ 
            break;
        case REOP_save_start:
        case REOP_save_end:
            val = bc_buf[pos + 1];
            capture_bitmap[val] |= 1;
            break;
        case REOP_save_reset:
            {
                val = bc_buf[pos + 1];
                last = bc_buf[pos + 2];
                while (val < last)
                    capture_bitmap[val++] |= 1;
            }
            break;
        case REOP_back_reference:
        case REOP_backward_back_reference:
            val = bc_buf[pos + 1];
            capture_bitmap[val] |= 2;
            has_back_reference = TRUE;
            break;
        default:
            /*  安全行为：我们无法预测结果。 */ 
            if (ret == -2)
                ret = 0;
            break;
        }
        pos += len;
    }
    if (has_back_reference) {
        /*  检查是否存在引用捕获的反向引用在一些代码中生成。 */ 
        for(i = 0; i < CAPTURE_COUNT_MAX; i++) {
            if (capture_bitmap[i] == 3)
                return -1;
        }
    }
    if (ret == -2)
        ret = 0;
    return ret;
}

/*  如果不能使用简单的量词，则返回-1。否则会退回原子中的字符数。 */ 
static int re_is_simple_quantifier(const uint8_t *bc_buf, int bc_buf_len)
{
    int pos, opcode, len, count;
    uint32_t val;
    
    count = 0;
    pos = 0;
    while (pos < bc_buf_len) {
        opcode = bc_buf[pos];
        len = reopcode_info[opcode].size;
        switch(opcode) {
        case REOP_range:
            val = get_u16(bc_buf + pos + 1);
            len += val * 4;
            goto simple_char;
        case REOP_range32:
            val = get_u16(bc_buf + pos + 1);
            len += val * 8;
            goto simple_char;
        case REOP_char:
        case REOP_char32:
        case REOP_dot:
        case REOP_any:
        simple_char:
            count++;
            break;
        case REOP_line_start:
        case REOP_line_end:
        case REOP_word_boundary:
        case REOP_not_word_boundary:
            break;
        default:
            return -1;
        }
        pos += len;
    }
    return count;
}

/*  “*pp”是“&lt;”之后的第一个字符。 */ 
static int re_parse_group_name(char *buf, int buf_size,
                               const uint8_t **pp, BOOL is_utf16)
{
    const uint8_t *p;
    uint32_t c;
    char *q;

    p = *pp;
    q = buf;
    for(;;) {
        c = *p;
        if (c == '\\') {
            p++;
            if (*p != 'u')
                return -1;
            c = lre_parse_escape(&p, is_utf16 * 2);
        } else if (c == '>') {
            break;
        } else if (c >= 128) {
            c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p);
        } else {
            p++;
        }
        if (c > 0x10FFFF)
            return -1;
        if (q == buf) {
            if (!lre_js_is_ident_first(c))
                return -1;
        } else {
            if (!lre_js_is_ident_next(c))
                return -1;
        }
        if ((q - buf + UTF8_CHAR_LEN_MAX + 1) > buf_size)
            return -1;
        if (c < 128) {
            *q++ = c;
        } else {
            q += unicode_to_utf8((uint8_t*)q, c);
        }
    }
    if (q == buf)
        return -1;
    *q = '\0';
    p++;
    *pp = p;
    return 0;
}

/*  如果CAPTURE_NAME=NULL：返回捕获次数+1。否则，返回Capture_Name对应的捕获索引如果没有，则为-1。 */ 
static int re_parse_captures(REParseState *s, int *phas_named_captures,
                             const char *capture_name)
{
    const uint8_t *p;
    int capture_index;
    char name[TMP_BUF_SIZE];

    capture_index = 1;
    *phas_named_captures = 0;
    for (p = s->buf_start; p < s->buf_end; p++) {
        switch (*p) {
        case '(':
            if (p[1] == '?') {
                if (p[2] == '<' && p[3] != '=' && p[3] != '!') {
                    *phas_named_captures = 1;
                    /*  潜在的命名捕获。 */ 
                    if (capture_name) {
                        p += 3;
                        if (re_parse_group_name(name, sizeof(name), &p,
                                                s->is_utf16) == 0) {
                            if (!strcmp(name, capture_name))
                                return capture_index;
                        }
                    }
                    capture_index++;
                }
            } else {
                capture_index++;
            }
            break;
        case '\\':
            p++;
            break;
        case '[':
            for (p += 1 + (*p == ']'); p < s->buf_end && *p != ']'; p++) {
                if (*p == '\\')
                    p++;
            }
            break;
        }
    }
    if (capture_name)
        return -1;
    else
        return capture_index;
}

static int re_count_captures(REParseState *s)
{
    if (s->total_capture_count < 0) {
        s->total_capture_count = re_parse_captures(s, &s->has_named_captures,
                                                   NULL);
    }
    return s->total_capture_count;
}

static BOOL re_has_named_captures(REParseState *s)
{
    if (s->has_named_captures < 0)
        re_count_captures(s);
    return s->has_named_captures;
}

static int find_group_name(REParseState *s, const char *name)
{
    const char *p, *buf_end;
    size_t len, name_len;
    int capture_index;
    
    name_len = strlen(name);
    p = (char *)s->group_names.buf;
    buf_end = (char *)s->group_names.buf + s->group_names.size;
    capture_index = 1;
    while (p < buf_end) {
        len = strlen(p);
        if (len == name_len && memcmp(name, p, name_len) == 0)
            return capture_index;
        p += len + 1;
        capture_index++;
    }
    return -1;
}

static int re_parse_disjunction(REParseState *s, BOOL is_backward_dir);

static int re_parse_term(REParseState *s, BOOL is_backward_dir)
{
    const uint8_t *p;
    int c, last_atom_start, quant_min, quant_max, last_capture_count;
    BOOL greedy, add_zero_advance_check, is_neg, is_backward_lookahead;
    CharRange cr_s, *cr = &cr_s;
    
    last_atom_start = -1;
    last_capture_count = 0;
    p = s->buf_ptr;
    c = *p;
    switch(c) {
    case '^':
        p++;
        re_emit_op(s, REOP_line_start);
        break;
    case '$':
        p++;
        re_emit_op(s, REOP_line_end);
        break;
    case '.':
        p++;
        last_atom_start = s->byte_code.size;
        last_capture_count = s->capture_count;
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        re_emit_op(s, s->dotall ? REOP_any : REOP_dot);
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        break;
    case '{':
        /*  作为扩展(见ES6附件B)，我们接受‘{’后跟数字，表示正常原子。 */ 
        if (!is_digit(p[1])) {
            if (s->is_utf16)
                goto invalid_quant_count;
            goto parse_class_atom;
        }
        /*  落锤式桁架。 */ 
    case '*':
    case '+':
    case '?':
        return re_parse_error(s, "nothing to repeat");
    case '(':
        if (p[1] == '?') {
            if (p[2] == ':') {
                p += 3;
                last_atom_start = s->byte_code.size;
                last_capture_count = s->capture_count;
                s->buf_ptr = p;
                if (re_parse_disjunction(s, is_backward_dir))
                    return -1;
                p = s->buf_ptr;
                if (re_parse_expect(s, &p, ')'))
                    return -1;
            } else if ((p[2] == '=' || p[2] == '!')) {
                is_neg = (p[2] == '!');
                is_backward_lookahead = FALSE;
                p += 3;
                goto lookahead;
            } else if (p[2] == '<' &&
                       (p[3] == '=' || p[3] == '!')) {
                int pos;
                is_neg = (p[3] == '!');
                is_backward_lookahead = TRUE;
                p += 4;
                /*  前瞻。 */ 
            lookahead:
                /*  附件B允许将先行作为原子用于量词。 */ 
                if (!s->is_utf16 && !is_backward_lookahead)  {
                    last_atom_start = s->byte_code.size;
                    last_capture_count = s->capture_count;
                }
                pos = re_emit_op_u32(s, REOP_lookahead + is_neg, 0);
                s->buf_ptr = p;
                if (re_parse_disjunction(s, is_backward_lookahead))
                    return -1;
                p = s->buf_ptr;
                if (re_parse_expect(s, &p, ')'))
                    return -1;
                re_emit_op(s, REOP_match);
                /*  在前视成功后跳转到“匹配”之后。 */ 
                put_u32(s->byte_code.buf + pos, s->byte_code.size - (pos + 4));
            } else if (p[2] == '<') {
                p += 3;
                if (re_parse_group_name(s->u.tmp_buf, sizeof(s->u.tmp_buf),
                                        &p, s->is_utf16)) {
                    return re_parse_error(s, "invalid group name");
                }
                if (find_group_name(s, s->u.tmp_buf) > 0) {
                    return re_parse_error(s, "duplicate group name");
                }
                /*  尾随零的组名。 */ 
                dbuf_put(&s->group_names, (uint8_t *)s->u.tmp_buf,
                         strlen(s->u.tmp_buf) + 1);
                s->has_named_captures = 1;
                goto parse_capture;
            } else {
                return re_parse_error(s, "invalid group");
            }
        } else {
            int capture_index;
            p++;
            /*  不带组名的捕获。 */ 
            dbuf_putc(&s->group_names, 0);
        parse_capture:
            if (s->capture_count >= CAPTURE_COUNT_MAX)
                return re_parse_error(s, "too many captures");
            last_atom_start = s->byte_code.size;
            last_capture_count = s->capture_count;
            capture_index = s->capture_count++;
            re_emit_op_u8(s, REOP_save_start + is_backward_dir,
                          capture_index);
            
            s->buf_ptr = p;
            if (re_parse_disjunction(s, is_backward_dir))
                return -1;
            p = s->buf_ptr;
            
            re_emit_op_u8(s, REOP_save_start + 1 - is_backward_dir,
                          capture_index);
            
            if (re_parse_expect(s, &p, ')'))
                return -1;
        }
        break;
    case '\\':
        switch(p[1]) {
        case 'b':
        case 'B':
            re_emit_op(s, REOP_word_boundary + (p[1] != 'b'));
            p += 2;
            break;
        case 'k':
            {
                const uint8_t *p1;
                int dummy_res;
                
                p1 = p;
                if (p1[2] != '<') {
                    /*  附件B：我们容忍在非如果没有命名捕获，则为Unicode模式定义。 */ 
                    if (s->is_utf16 || re_has_named_captures(s))
                        return re_parse_error(s, "expecting group name");
                    else
                        goto parse_class_atom;
                }
                p1 += 3;
                if (re_parse_group_name(s->u.tmp_buf, sizeof(s->u.tmp_buf),
                                        &p1, s->is_utf16)) {
                    if (s->is_utf16 || re_has_named_captures(s))
                        return re_parse_error(s, "invalid group name");
                    else
                        goto parse_class_atom;
                }
                c = find_group_name(s, s->u.tmp_buf);
                if (c < 0) {
                    /*  之前未解析捕获名称，请尝试查找After(效率低下，但希望不常见。 */ 
                    c = re_parse_captures(s, &dummy_res, s->u.tmp_buf);
                    if (c < 0) {
                        if (s->is_utf16 || re_has_named_captures(s))
                            return re_parse_error(s, "group name not defined");
                        else
                            goto parse_class_atom;
                    }
                }
                p = p1;
            }
            goto emit_back_reference;
        case '0':
            p += 2;
            c = 0;
            if (s->is_utf16) {
                if (is_digit(*p)) {
                    return re_parse_error(s, "invalid decimal escape in regular expression");
                }
            } else {
                /*  附件B.1.4：接受传统八进制。 */ 
                if (*p >= '0' && *p <= '7') {
                    c = *p++ - '0';
                    if (*p >= '0' && *p <= '7') {
                        c = (c << 3) + *p++ - '0';
                    }
                }
            }
            goto normal_char;
        case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
            {
                const uint8_t *q = ++p;
                
                c = parse_digits(&p);
                if (c < 0 || (c >= s->capture_count && c >= re_count_captures(s))) {
                    if (!s->is_utf16) {
                        /*  附件B.1.4：接受传统八进制。 */ 
                        p = q;
                        if (*p <= '7') {
                            c = 0;
                            if (*p <= '3')
                                c = *p++ - '0';
                            if (*p >= '0' && *p <= '7') {
                                c = (c << 3) + *p++ - '0';
                                if (*p >= '0' && *p <= '7') {
                                    c = (c << 3) + *p++ - '0';
                                }
                            }
                        } else {
                            c = *p++;
                        }
                        goto normal_char;
                    }
                    return re_parse_error(s, "back reference out of range in reguar expression");
                }
            emit_back_reference:
                last_atom_start = s->byte_code.size;
                last_capture_count = s->capture_count;
                re_emit_op_u8(s, REOP_back_reference + is_backward_dir, c);
            }
            break;
        default:
            goto parse_class_atom;
        }
        break;
    case '[':
        last_atom_start = s->byte_code.size;
        last_capture_count = s->capture_count;
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        if (re_parse_char_class(s, &p))
            return -1;
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        break;
    case ']':
    case '}':
        if (s->is_utf16)
            return re_parse_error(s, "syntax error");
        goto parse_class_atom;
    default:
    parse_class_atom:
        c = get_class_atom(s, cr, &p, FALSE);
        if ((int)c < 0)
            return -1;
    normal_char:
        last_atom_start = s->byte_code.size;
        last_capture_count = s->capture_count;
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        if (c >= CLASS_RANGE_BASE) {
            int ret;
            /*  注：不需要规范化。 */ 
            ret = re_emit_range(s, cr);
            cr_free(cr);
            if (ret)
                return -1;
        } else {
            if (s->ignore_case)
                c = lre_canonicalize(c, s->is_utf16);
            if (c <= 0xffff)
                re_emit_op_u16(s, REOP_char, c);
            else
                re_emit_op_u32(s, REOP_char32, c);
        }
        if (is_backward_dir)
            re_emit_op(s, REOP_prev);
        break;
    }

    /*  量词。 */ 
    if (last_atom_start >= 0) {
        c = *p;
        switch(c) {
        case '*':
            p++;
            quant_min = 0;
            quant_max = INT32_MAX;
            goto quantifier;
        case '+':
            p++;
            quant_min = 1;
            quant_max = INT32_MAX;
            goto quantifier;
        case '?':
            p++;
            quant_min = 0;
            quant_max = 1;
            goto quantifier;
        case '{':
            /*  作为扩展(见ES6附件B)，我们接受‘{’后跟数字，表示正常原子。 */ 
            if (!is_digit(p[1])) {
                if (s->is_utf16)
                    goto invalid_quant_count;
                break;
            }
            p++;
            quant_min = parse_digits(&p);
            if (quant_min < 0) {
            invalid_quant_count:
                return re_parse_error(s, "invalid repetition count");
            }
            quant_max = quant_min;
            if (*p == ',') {
                p++;
                if (is_digit(*p)) {
                    quant_max = parse_digits(&p);
                    if (quant_max < 0 || quant_max < quant_min)
                        goto invalid_quant_count;
                } else {
                    quant_max = INT32_MAX; /*  无穷大。 */ 
                }
            }
            if (re_parse_expect(s, &p, '}'))
                return -1;
        quantifier:
            greedy = TRUE;
            if (*p == '?') {
                p++;
                greedy = FALSE;
            }
            if (last_atom_start < 0) {
                return re_parse_error(s, "nothing to repeat");
            }
            if (greedy) {
                int len, pos;
                
                if (quant_max > 0) {
                    /*  针对简单量词的特定优化。 */ 
                    len = re_is_simple_quantifier(s->byte_code.buf + last_atom_start,
                                                 s->byte_code.size - last_atom_start);
                    if (len > 0) {
                        re_emit_op(s, REOP_match);
                        
                        dbuf_insert(&s->byte_code, last_atom_start, 17);
                        pos = last_atom_start;
                        s->byte_code.buf[pos++] = REOP_simple_greedy_quant;
                        put_u32(&s->byte_code.buf[pos],
                                s->byte_code.size - last_atom_start - 17);
                        pos += 4;
                        put_u32(&s->byte_code.buf[pos], quant_min);
                        pos += 4;
                        put_u32(&s->byte_code.buf[pos], quant_max);
                        pos += 4;
                        put_u32(&s->byte_code.buf[pos], len);
                        pos += 4;
                        goto done;
                    }
                }
                
                add_zero_advance_check = (re_check_advance(s->byte_code.buf + last_atom_start,
                                                           s->byte_code.size - last_atom_start) == 0);
            } else {
                add_zero_advance_check = FALSE;
            }
            
            {
                int len, pos;
                len = s->byte_code.size - last_atom_start;
                if (quant_min == 0) {
                    /*  需要重置捕获以防原子 */ 
                    if (last_capture_count != s->capture_count) {
                        dbuf_insert(&s->byte_code, last_atom_start, 3);
                        s->byte_code.buf[last_atom_start++] = REOP_save_reset;
                        s->byte_code.buf[last_atom_start++] = last_capture_count;
                        s->byte_code.buf[last_atom_start++] = s->capture_count - 1;
                    }
                    if (quant_max == 0) {
                        s->byte_code.size = last_atom_start;
                    } else if (quant_max == 1) {
                        dbuf_insert(&s->byte_code, last_atom_start, 5);
                        s->byte_code.buf[last_atom_start] = REOP_split_goto_first +
                            greedy;
                        put_u32(s->byte_code.buf + last_atom_start + 1, len);
                    } else if (quant_max == INT32_MAX) {
                        dbuf_insert(&s->byte_code, last_atom_start, 5 + add_zero_advance_check);
                        s->byte_code.buf[last_atom_start] = REOP_split_goto_first +
                            greedy;
                        put_u32(s->byte_code.buf + last_atom_start + 1,
                                len + 5 + add_zero_advance_check);
                        if (add_zero_advance_check) {
                            /*   */ 
                            s->byte_code.buf[last_atom_start + 1 + 4] = REOP_push_char_pos;
                            re_emit_goto(s, REOP_bne_char_pos, last_atom_start); 
                        } else {
                            re_emit_goto(s, REOP_goto, last_atom_start);
                        }
                    } else {
                        dbuf_insert(&s->byte_code, last_atom_start, 10);
                        pos = last_atom_start;
                        s->byte_code.buf[pos++] = REOP_push_i32;
                        put_u32(s->byte_code.buf + pos, quant_max);
                        pos += 4;
                        s->byte_code.buf[pos++] = REOP_split_goto_first + greedy;
                        put_u32(s->byte_code.buf + pos, len + 5);
                        re_emit_goto(s, REOP_loop, last_atom_start + 5);
                        re_emit_op(s, REOP_drop);
                    }
                } else if (quant_min == 1 && quant_max == INT32_MAX &&
                           !add_zero_advance_check) {
                    re_emit_goto(s, REOP_split_next_first - greedy,
                                 last_atom_start);
                } else {
                    if (quant_min == 1) {
                        /*  没有什么要补充的。 */ 
                    } else {
                        dbuf_insert(&s->byte_code, last_atom_start, 5);
                        s->byte_code.buf[last_atom_start] = REOP_push_i32;
                        put_u32(s->byte_code.buf + last_atom_start + 1,
                                quant_min);
                        last_atom_start += 5;
                        re_emit_goto(s, REOP_loop, last_atom_start);
                        re_emit_op(s, REOP_drop);
                    }
                    if (quant_max == INT32_MAX) {
                        pos = s->byte_code.size;
                        re_emit_op_u32(s, REOP_split_goto_first + greedy,
                                       len + 5 + add_zero_advance_check);
                        if (add_zero_advance_check)
                            re_emit_op(s, REOP_push_char_pos);
                        /*  复制原子。 */ 
                        dbuf_put_self(&s->byte_code, last_atom_start, len);
                        if (add_zero_advance_check)
                            re_emit_goto(s, REOP_bne_char_pos, pos);
                        else
                            re_emit_goto(s, REOP_goto, pos);
                    } else if (quant_max > quant_min) {
                        re_emit_op_u32(s, REOP_push_i32, quant_max - quant_min);
                        pos = s->byte_code.size;
                        re_emit_op_u32(s, REOP_split_goto_first + greedy, len + 5);
                        /*  复制原子。 */ 
                        dbuf_put_self(&s->byte_code, last_atom_start, len);
                        
                        re_emit_goto(s, REOP_loop, pos);
                        re_emit_op(s, REOP_drop);
                    }
                }
                last_atom_start = -1;
            }
            break;
        default:
            break;
        }
    }
 done:
    s->buf_ptr = p;
    return 0;
}

static int re_parse_alternative(REParseState *s, BOOL is_backward_dir)
{
    const uint8_t *p;
    int ret;
    size_t start, term_start, end, term_size;

    start = s->byte_code.size;
    for(;;) {
        p = s->buf_ptr;
        if (p >= s->buf_end)
            break;
        if (*p == '|' || *p == ')')
            break;
        term_start = s->byte_code.size;
        ret = re_parse_term(s, is_backward_dir);
        if (ret)
            return ret;
        if (is_backward_dir) {
            /*  颠倒术语顺序(XXX：低效，但在这里，速度并不是真正重要的)。 */ 
            end = s->byte_code.size;
            term_size = end - term_start;
            if (dbuf_realloc(&s->byte_code, end + term_size))
                return -1;
            memmove(s->byte_code.buf + start + term_size,
                    s->byte_code.buf + start,
                    end - start);
            memcpy(s->byte_code.buf + start, s->byte_code.buf + end,
                   term_size);
        }
    }
    return 0;
}
    
static int re_parse_disjunction(REParseState *s, BOOL is_backward_dir)
{
    int start, len, pos;

    start = s->byte_code.size;
    if (re_parse_alternative(s, is_backward_dir))
        return -1;
    while (*s->buf_ptr == '|') {
        s->buf_ptr++;

        len = s->byte_code.size - start;

        /*  在第一个备选方案之前插入拆分。 */ 
        dbuf_insert(&s->byte_code, start, 5);
        s->byte_code.buf[start] = REOP_split_next_first;
        put_u32(s->byte_code.buf + start + 1, len + 5);

        pos = re_emit_op_u32(s, REOP_goto, 0);

        if (re_parse_alternative(s, is_backward_dir))
            return -1;
        
        /*  修补GoTO。 */ 
        len = s->byte_code.size - (pos + 4);
        put_u32(s->byte_code.buf + pos, len);
    }
    return 0;
}

/*  控制流是递归的，因此分析可以是线性的。 */ 
static int compute_stack_size(const uint8_t *bc_buf, int bc_buf_len)
{
    int stack_size, stack_size_max, pos, opcode, len;
    uint32_t val;
    
    stack_size = 0;
    stack_size_max = 0;
    bc_buf += RE_HEADER_LEN;
    bc_buf_len -= RE_HEADER_LEN;
    pos = 0;
    while (pos < bc_buf_len) {
        opcode = bc_buf[pos];
        len = reopcode_info[opcode].size;
        assert(opcode < REOP_COUNT);
        assert((pos + len) <= bc_buf_len);
        switch(opcode) {
        case REOP_push_i32:
        case REOP_push_char_pos:
            stack_size++;
            if (stack_size > stack_size_max) {
                if (stack_size > STACK_SIZE_MAX)
                    return -1;
                stack_size_max = stack_size;
            }
            break;
        case REOP_drop:
        case REOP_bne_char_pos:
            assert(stack_size > 0);
            stack_size--;
            break;
        case REOP_range:
            val = get_u16(bc_buf + pos + 1);
            len += val * 4;
            break;
        case REOP_range32:
            val = get_u16(bc_buf + pos + 1);
            len += val * 8;
            break;
        }
        pos += len;
    }
    return stack_size_max;
}

/*  ‘buf’必须是长度为buf_len的以零结尾的UTF-8字符串。如果出错则返回NULL，并在*perror_msg中分配错误消息。否则，编译后的字节码及其长度以Plen表示。 */ 
uint8_t *lre_compile(int *plen, char *error_msg, int error_msg_size,
                     const char *buf, size_t buf_len, int re_flags,
                     void *opaque)
{
    REParseState s_s, *s = &s_s;
    int stack_size;
    BOOL is_sticky;
    
    memset(s, 0, sizeof(*s));
    s->mem_opaque = opaque;
    s->buf_ptr = (const uint8_t *)buf;
    s->buf_end = s->buf_ptr + buf_len;
    s->buf_start = s->buf_ptr;
    s->re_flags = re_flags;
    s->is_utf16 = ((re_flags & LRE_FLAG_UTF16) != 0);
    is_sticky = ((re_flags & LRE_FLAG_STICKY) != 0);
    s->ignore_case = ((re_flags & LRE_FLAG_IGNORECASE) != 0);
    s->dotall = ((re_flags & LRE_FLAG_DOTALL) != 0);
    s->capture_count = 1;
    s->total_capture_count = -1;
    s->has_named_captures = -1;
    
    dbuf_init2(&s->byte_code, opaque, lre_realloc);
    dbuf_init2(&s->group_names, opaque, lre_realloc);

    dbuf_putc(&s->byte_code, re_flags); /*  第一个元素是旗帜。 */ 
    dbuf_putc(&s->byte_code, 0); /*  第二个元素是捕获的数量。 */ 
    dbuf_putc(&s->byte_code, 0); /*  堆栈大小。 */ 
    dbuf_put_u32(&s->byte_code, 0); /*  字节码长度。 */ 
    
    if (!is_sticky) {
        /*  遍历所有位置(与.*？(...)大致相同)。我们在没有显式循环的情况下这样做，因此锁定步骤线程执行将可能在经过优化的实施。 */ 
        re_emit_op_u32(s, REOP_split_goto_first, 1 + 5);
        re_emit_op(s, REOP_any);
        re_emit_op_u32(s, REOP_goto, -(5 + 1 + 5));
    }
    re_emit_op_u8(s, REOP_save_start, 0);

    if (re_parse_disjunction(s, FALSE)) {
    error:
        dbuf_free(&s->byte_code);
        dbuf_free(&s->group_names);
        pstrcpy(error_msg, error_msg_size, s->u.error_msg);
        *plen = 0;
        return NULL;
    }

    re_emit_op_u8(s, REOP_save_end, 0);
    
    re_emit_op(s, REOP_match);

    if (*s->buf_ptr != '\0') {
        re_parse_error(s, "extraneous characters at the end");
        goto error;
    }

    if (dbuf_error(&s->byte_code)) {
        re_parse_error(s, "out of memory");
        goto error;
    }
    
    stack_size = compute_stack_size(s->byte_code.buf, s->byte_code.size);
    if (stack_size < 0) {
        re_parse_error(s, "too many imbricated quantifiers");
        goto error;
    }
    
    s->byte_code.buf[RE_HEADER_CAPTURE_COUNT] = s->capture_count;
    s->byte_code.buf[RE_HEADER_STACK_SIZE] = stack_size;
    put_u32(s->byte_code.buf + 3, s->byte_code.size - RE_HEADER_LEN);

    /*  如果需要，添加命名组。 */ 
    if (s->group_names.size > (s->capture_count - 1)) {
        dbuf_put(&s->byte_code, s->group_names.buf, s->group_names.size);
        s->byte_code.buf[RE_HEADER_FLAGS] |= LRE_FLAG_NAMED_GROUPS;
    }
    dbuf_free(&s->group_names);
    
#ifdef DUMP_REOP
    lre_dump_bytecode(s->byte_code.buf, s->byte_code.size);
#endif
    
    error_msg[0] = '\0';
    *plen = s->byte_code.size;
    return s->byte_code.buf;
}

static BOOL is_line_terminator(uint32_t c)
{
    return (c == '\n' || c == '\r' || c == CP_LS || c == CP_PS);
}

static BOOL is_word_char(uint32_t c)
{
    return ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c == '_'));
}

#define GET_CHAR(c, cptr, cbuf_end)                                     \
    do {                                                                \
        if (cbuf_type == 0) {                                           \
            c = *cptr++;                                                \
        } else {                                                        \
            uint32_t __c1;                                              \
            c = *(uint16_t *)cptr;                                      \
            cptr += 2;                                                  \
            if (c >= 0xd800 && c < 0xdc00 &&                            \
                cbuf_type == 2 && cptr < cbuf_end) {                    \
                __c1 = *(uint16_t *)cptr;                               \
                if (__c1 >= 0xdc00 && __c1 < 0xe000) {                  \
                    c = (((c & 0x3ff) << 10) | (__c1 & 0x3ff)) + 0x10000; \
                    cptr += 2;                                          \
                }                                                       \
            }                                                           \
        }                                                               \
    } while (0)

#define PEEK_CHAR(c, cptr, cbuf_end)             \
    do {                                         \
        if (cbuf_type == 0) {                    \
            c = cptr[0];                         \
        } else {                                 \
            uint32_t __c1;                                              \
            c = ((uint16_t *)cptr)[0];                                  \
            if (c >= 0xd800 && c < 0xdc00 &&                            \
                cbuf_type == 2 && (cptr + 2) < cbuf_end) {              \
                __c1 = ((uint16_t *)cptr)[1];                           \
                if (__c1 >= 0xdc00 && __c1 < 0xe000) {                  \
                    c = (((c & 0x3ff) << 10) | (__c1 & 0x3ff)) + 0x10000; \
                }                                                       \
            }                                                           \
        }                                        \
    } while (0)

#define PEEK_PREV_CHAR(c, cptr, cbuf_start)                 \
    do {                                         \
        if (cbuf_type == 0) {                    \
            c = cptr[-1];                        \
        } else {                                 \
            uint32_t __c1;                                              \
            c = ((uint16_t *)cptr)[-1];                                 \
            if (c >= 0xdc00 && c < 0xe000 &&                            \
                cbuf_type == 2 && (cptr - 4) >= cbuf_start) {              \
                __c1 = ((uint16_t *)cptr)[-2];                          \
                if (__c1 >= 0xd800 && __c1 < 0xdc00 ) {                 \
                    c = (((__c1 & 0x3ff) << 10) | (c & 0x3ff)) + 0x10000; \
                }                                                       \
            }                                                           \
        }                                                               \
    } while (0)

#define GET_PREV_CHAR(c, cptr, cbuf_start)       \
    do {                                         \
        if (cbuf_type == 0) {                    \
            cptr--;                              \
            c = cptr[0];                         \
        } else {                                 \
            uint32_t __c1;                                              \
            cptr -= 2;                                                  \
            c = ((uint16_t *)cptr)[0];                                 \
            if (c >= 0xdc00 && c < 0xe000 &&                            \
                cbuf_type == 2 && cptr > cbuf_start) {                  \
                __c1 = ((uint16_t *)cptr)[-1];                          \
                if (__c1 >= 0xd800 && __c1 < 0xdc00 ) {                 \
                    cptr -= 2;                                          \
                    c = (((__c1 & 0x3ff) << 10) | (c & 0x3ff)) + 0x10000; \
                }                                                       \
            }                                                           \
        }                                                               \
    } while (0)

#define PREV_CHAR(cptr, cbuf_start)       \
    do {                                  \
        if (cbuf_type == 0) {             \
            cptr--;                       \
        } else {                          \
            cptr -= 2;                          \
            if (cbuf_type == 2) {                                       \
                c = ((uint16_t *)cptr)[0];                              \
                if (c >= 0xdc00 && c < 0xe000 && cptr > cbuf_start) {   \
                    c = ((uint16_t *)cptr)[-1];                         \
                    if (c >= 0xd800 && c < 0xdc00)                      \
                        cptr -= 2;                                      \
                }                                                       \
            }                                                           \
        }                                                               \
    } while (0)

typedef uintptr_t StackInt;

typedef enum {
    RE_EXEC_STATE_SPLIT,
    RE_EXEC_STATE_LOOKAHEAD,
    RE_EXEC_STATE_NEGATIVE_LOOKAHEAD,
    RE_EXEC_STATE_GREEDY_QUANT,
} REExecStateEnum;

typedef struct REExecState {
    REExecStateEnum type : 8;
    uint8_t stack_len;
    size_t count; /*  仅用于RE_EXEC_STATE_GREEDY_QUANT。 */ 
    const uint8_t *cptr;
    const uint8_t *pc;
    void *buf[0];
} REExecState;

typedef struct {
    const uint8_t *cbuf;
    const uint8_t *cbuf_end;
    /*  0=8位字符，1=16位字符，2=16位字符，UTF-16。 */ 
    int cbuf_type; 
    int capture_count;
    int stack_size_max;
    BOOL multi_line;
    BOOL ignore_case;
    BOOL is_utf16;
    void *opaque; /*  用于堆栈溢出检查。 */ 

    size_t state_size;
    uint8_t *state_stack;
    size_t state_stack_size;
    size_t state_stack_len;
} REExecContext;

static int push_state(REExecContext *s,
                      uint8_t **capture,
                      StackInt *stack, size_t stack_len,
                      const uint8_t *pc, const uint8_t *cptr,
                      REExecStateEnum type, size_t count)
{
    REExecState *rs;
    uint8_t *new_stack;
    size_t new_size, i, n;
    StackInt *stack_buf;

    if (unlikely((s->state_stack_len + 1) > s->state_stack_size)) {
        /*  重新分配堆栈。 */ 
        new_size = s->state_stack_size * 3 / 2;
        if (new_size < 8)
            new_size = 8;
        new_stack = lre_realloc(s->opaque, s->state_stack, new_size * s->state_size);
        if (!new_stack)
            return -1;
        s->state_stack_size = new_size;
        s->state_stack = new_stack;
    }
    rs = (REExecState *)(s->state_stack + s->state_stack_len * s->state_size);
    s->state_stack_len++;
    rs->type = type;
    rs->count = count;
    rs->stack_len = stack_len;
    rs->cptr = cptr;
    rs->pc = pc;
    n = 2 * s->capture_count;
    for(i = 0; i < n; i++)
        rs->buf[i] = capture[i];
    stack_buf = (StackInt *)(rs->buf + n);
    for(i = 0; i < stack_len; i++)
        stack_buf[i] = stack[i];
    return 0;
}

/*  如果匹配，则返回1；如果不匹配，则返回0；如果错误，则返回-1。 */ 
static intptr_t lre_exec_backtrack(REExecContext *s, uint8_t **capture,
                                   StackInt *stack, int stack_len,
                                   const uint8_t *pc, const uint8_t *cptr,
                                   BOOL no_recurse)
{
    int opcode, ret;
    int cbuf_type;
    uint32_t val, c;
    const uint8_t *cbuf_end;
    
    cbuf_type = s->cbuf_type;
    cbuf_end = s->cbuf_end;

    for(;;) {
        //  Printf(“top=%p：pc=%d\n”，th_list.top，(Int)(pc-(BC_buf+RE_Header_LEN)；
        opcode = *pc++;
        switch(opcode) {
        case REOP_match:
            {
                REExecState *rs;
                if (no_recurse)
                    return (intptr_t)cptr;
                ret = 1;
                goto recurse;
            no_match:
                if (no_recurse)
                    return 0;
                ret = 0;
            recurse:
                for(;;) {
                    if (s->state_stack_len == 0)
                        return ret;
                    rs = (REExecState *)(s->state_stack +
                                         (s->state_stack_len - 1) * s->state_size);
                    if (rs->type == RE_EXEC_STATE_SPLIT) {
                        if (!ret) {
                        pop_state:
                            memcpy(capture, rs->buf,
                                   sizeof(capture[0]) * 2 * s->capture_count);
                        pop_state1:
                            pc = rs->pc;
                            cptr = rs->cptr;
                            stack_len = rs->stack_len;
                            memcpy(stack, rs->buf + 2 * s->capture_count,
                                   stack_len * sizeof(stack[0]));
                            s->state_stack_len--;
                            break;
                        }
                    } else if (rs->type == RE_EXEC_STATE_GREEDY_QUANT) {
                        if (!ret) {
                            uint32_t char_count, i;
                            memcpy(capture, rs->buf,
                                   sizeof(capture[0]) * 2 * s->capture_count);
                            stack_len = rs->stack_len;
                            memcpy(stack, rs->buf + 2 * s->capture_count,
                                   stack_len * sizeof(stack[0]));
                            pc = rs->pc;
                            cptr = rs->cptr;
                            /*  后退。 */ 
                            char_count = get_u32(pc + 12);
                            for(i = 0; i < char_count; i++) {
                                PREV_CHAR(cptr, s->cbuf);
                            }
                            pc = (pc + 16) + (int)get_u32(pc);
                            rs->cptr = cptr;
                            rs->count--;
                            if (rs->count == 0) {
                                s->state_stack_len--;
                            }
                            break;
                        }
                    } else {
                        ret = ((rs->type == RE_EXEC_STATE_LOOKAHEAD && ret) ||
                               (rs->type == RE_EXEC_STATE_NEGATIVE_LOOKAHEAD && !ret));
                        if (ret) {
                            /*  保留捕获，以防发生正面预测。 */ 
                            if (rs->type == RE_EXEC_STATE_LOOKAHEAD)
                                goto pop_state1;
                            else
                                goto pop_state;
                        }
                    }
                    s->state_stack_len--;
                }
            }
            break;
        case REOP_char32:
            val = get_u32(pc);
            pc += 4;
            goto test_char;
        case REOP_char:
            val = get_u16(pc);
            pc += 2;
        test_char:
            if (cptr >= cbuf_end)
                goto no_match;
            GET_CHAR(c, cptr, cbuf_end);
            if (s->ignore_case) {
                c = lre_canonicalize(c, s->is_utf16);
            }
            if (val != c)
                goto no_match;
            break;
        case REOP_split_goto_first:
        case REOP_split_next_first:
            {
                const uint8_t *pc1;
                
                val = get_u32(pc);
                pc += 4;
                if (opcode == REOP_split_next_first) {
                    pc1 = pc + (int)val;
                } else {
                    pc1 = pc;
                    pc = pc + (int)val;
                }
                ret = push_state(s, capture, stack, stack_len,
                                 pc1, cptr, RE_EXEC_STATE_SPLIT, 0);
                if (ret < 0)
                    return -1;
                break;
            }
        case REOP_lookahead:
        case REOP_negative_lookahead:
            val = get_u32(pc);
            pc += 4;
            ret = push_state(s, capture, stack, stack_len,
                             pc + (int)val, cptr,
                             RE_EXEC_STATE_LOOKAHEAD + opcode - REOP_lookahead,
                             0);
            if (ret < 0)
                return -1;
            break;
            
        case REOP_goto:
            val = get_u32(pc);
            pc += 4 + (int)val;
            break;
        case REOP_line_start:
            if (cptr == s->cbuf)
                break;
            if (!s->multi_line)
                goto no_match;
            PEEK_PREV_CHAR(c, cptr, s->cbuf);
            if (!is_line_terminator(c))
                goto no_match;
            break;
        case REOP_line_end:
            if (cptr == cbuf_end)
                break;
            if (!s->multi_line)
                goto no_match;
            PEEK_CHAR(c, cptr, cbuf_end);
            if (!is_line_terminator(c))
                goto no_match;
            break;
        case REOP_dot:
            if (cptr == cbuf_end)
                goto no_match;
            GET_CHAR(c, cptr, cbuf_end);
            if (is_line_terminator(c))
                goto no_match;
            break;
        case REOP_any:
            if (cptr == cbuf_end)
                goto no_match;
            GET_CHAR(c, cptr, cbuf_end);
            break;
        case REOP_save_start:
        case REOP_save_end:
            val = *pc++;
            assert(val < s->capture_count);
            capture[2 * val + opcode - REOP_save_start] = (uint8_t *)cptr;
            break;
        case REOP_save_reset:
            {
                uint32_t val2;
                val = pc[0];
                val2 = pc[1];
                pc += 2;
                assert(val2 < s->capture_count);
                while (val <= val2) {
                    capture[2 * val] = NULL;
                    capture[2 * val + 1] = NULL;
                    val++;
                }
            }
            break;
        case REOP_push_i32:
            val = get_u32(pc);
            pc += 4;
            stack[stack_len++] = val;
            break;
        case REOP_drop:
            stack_len--;
            break;
        case REOP_loop:
            val = get_u32(pc);
            pc += 4;
            if (--stack[stack_len - 1] != 0) {
                pc += (int)val;
            }
            break;
        case REOP_push_char_pos:
            stack[stack_len++] = (uintptr_t)cptr;
            break;
        case REOP_bne_char_pos:
            val = get_u32(pc);
            pc += 4;
            if (stack[--stack_len] != (uintptr_t)cptr)
                pc += (int)val;
            break;
        case REOP_word_boundary:
        case REOP_not_word_boundary:
            {
                BOOL v1, v2;
                /*  字符之前。 */ 
                if (cptr == s->cbuf) {
                    v1 = FALSE;
                } else {
                    PEEK_PREV_CHAR(c, cptr, s->cbuf);
                    v1 = is_word_char(c);
                }
                /*  当前费用。 */ 
                if (cptr >= cbuf_end) {
                    v2 = FALSE;
                } else {
                    PEEK_CHAR(c, cptr, cbuf_end);
                    v2 = is_word_char(c);
                }
                if (v1 ^ v2 ^ (REOP_not_word_boundary - opcode))
                    goto no_match;
            }
            break;
        case REOP_back_reference:
        case REOP_backward_back_reference:
            {
                const uint8_t *cptr1, *cptr1_end, *cptr1_start;
                uint32_t c1, c2;
                
                val = *pc++;
                if (val >= s->capture_count)
                    goto no_match;
                cptr1_start = capture[2 * val];
                cptr1_end = capture[2 * val + 1];
                if (!cptr1_start || !cptr1_end)
                    break;
                if (opcode == REOP_back_reference) {
                    cptr1 = cptr1_start;
                    while (cptr1 < cptr1_end) {
                        if (cptr >= cbuf_end)
                            goto no_match;
                        GET_CHAR(c1, cptr1, cptr1_end);
                        GET_CHAR(c2, cptr, cbuf_end);
                        if (s->ignore_case) {
                            c1 = lre_canonicalize(c1, s->is_utf16);
                            c2 = lre_canonicalize(c2, s->is_utf16);
                        }
                        if (c1 != c2)
                            goto no_match;
                    }
                } else {
                    cptr1 = cptr1_end;
                    while (cptr1 > cptr1_start) {
                        if (cptr == s->cbuf)
                            goto no_match;
                        GET_PREV_CHAR(c1, cptr1, cptr1_start);
                        GET_PREV_CHAR(c2, cptr, s->cbuf);
                        if (s->ignore_case) {
                            c1 = lre_canonicalize(c1, s->is_utf16);
                            c2 = lre_canonicalize(c2, s->is_utf16);
                        }
                        if (c1 != c2)
                            goto no_match;
                    }
                }
            }
            break;
        case REOP_range:
            {
                int n;
                uint32_t low, high, idx_min, idx_max, idx;
                
                n = get_u16(pc); /*  N必须&gt;=1。 */ 
                pc += 2;
                if (cptr >= cbuf_end)
                    goto no_match;
                GET_CHAR(c, cptr, cbuf_end);
                if (s->ignore_case) {
                    c = lre_canonicalize(c, s->is_utf16);
                }
                idx_min = 0;
                low = get_u16(pc + 0 * 4);
                if (c < low)
                    goto no_match;
                idx_max = n - 1;
                high = get_u16(pc + idx_max * 4 + 2);
                /*  最后一个值的0xffff表示+无穷大。 */ 
                if (unlikely(c >= 0xffff) && high == 0xffff)
                    goto range_match;
                if (c > high)
                    goto no_match;
                while (idx_min <= idx_max) {
                    idx = (idx_min + idx_max) / 2;
                    low = get_u16(pc + idx * 4);
                    high = get_u16(pc + idx * 4 + 2);
                    if (c < low)
                        idx_max = idx - 1;
                    else if (c > high)
                        idx_min = idx + 1;
                    else
                        goto range_match;
                }
                goto no_match;
            range_match:
                pc += 4 * n;
            }
            break;
        case REOP_range32:
            {
                int n;
                uint32_t low, high, idx_min, idx_max, idx;
                
                n = get_u16(pc); /*  N必须&gt;=1。 */ 
                pc += 2;
                if (cptr >= cbuf_end)
                    goto no_match;
                GET_CHAR(c, cptr, cbuf_end);
                if (s->ignore_case) {
                    c = lre_canonicalize(c, s->is_utf16);
                }
                idx_min = 0;
                low = get_u32(pc + 0 * 8);
                if (c < low)
                    goto no_match;
                idx_max = n - 1;
                high = get_u32(pc + idx_max * 8 + 4);
                if (c > high)
                    goto no_match;
                while (idx_min <= idx_max) {
                    idx = (idx_min + idx_max) / 2;
                    low = get_u32(pc + idx * 8);
                    high = get_u32(pc + idx * 8 + 4);
                    if (c < low)
                        idx_max = idx - 1;
                    else if (c > high)
                        idx_min = idx + 1;
                    else
                        goto range32_match;
                }
                goto no_match;
            range32_match:
                pc += 8 * n;
            }
            break;
        case REOP_prev:
            /*  转到上一次计费。 */ 
            if (cptr == s->cbuf)
                goto no_match;
            PREV_CHAR(cptr, s->cbuf);
            break;
        case REOP_simple_greedy_quant:
            {
                uint32_t next_pos, quant_min, quant_max;
                size_t q;
                intptr_t res;
                const uint8_t *pc1;
                
                next_pos = get_u32(pc);
                quant_min = get_u32(pc + 4);
                quant_max = get_u32(pc + 8);
                pc += 16;
                pc1 = pc;
                pc += (int)next_pos;
                
                q = 0;
                for(;;) {
                    res = lre_exec_backtrack(s, capture, stack, stack_len,
                                             pc1, cptr, TRUE);
                    if (res == -1)
                        return res;
                    if (!res)
                        break;
                    cptr = (uint8_t *)res;
                    q++;
                    if (q >= quant_max && quant_max != INT32_MAX)
                        break;
                }
                if (q < quant_min)
                    goto no_match;
                if (q > quant_min) {
                    /*  将检查一直到quant_min的所有匹配。 */ 
                    ret = push_state(s, capture, stack, stack_len,
                                     pc1 - 16, cptr,
                                     RE_EXEC_STATE_GREEDY_QUANT,
                                     q - quant_min);
                    if (ret < 0)
                        return -1;
                }
            }
            break;
        default:
            abort();
        }
    }
}

/*  如果匹配，则返回1；如果不匹配，则返回0；如果错误，则返回-1。Cindex是匹配的起始位置，必须如下所示：0&lt;=cindex&lt;=克莱恩。 */ 
int lre_exec(uint8_t **capture,
             const uint8_t *bc_buf, const uint8_t *cbuf, int cindex, int clen,
             int cbuf_type, void *opaque)
{
    REExecContext s_s, *s = &s_s;
    int re_flags, i, alloca_size, ret;
    StackInt *stack_buf;
    
    re_flags = bc_buf[RE_HEADER_FLAGS];
    s->multi_line = (re_flags & LRE_FLAG_MULTILINE) != 0;
    s->ignore_case = (re_flags & LRE_FLAG_IGNORECASE) != 0;
    s->is_utf16 = (re_flags & LRE_FLAG_UTF16) != 0;
    s->capture_count = bc_buf[RE_HEADER_CAPTURE_COUNT];
    s->stack_size_max = bc_buf[RE_HEADER_STACK_SIZE];
    s->cbuf = cbuf;
    s->cbuf_end = cbuf + (clen << cbuf_type);
    s->cbuf_type = cbuf_type;
    if (s->cbuf_type == 1 && s->is_utf16)
        s->cbuf_type = 2;
    s->opaque = opaque;

    s->state_size = sizeof(REExecState) +
        s->capture_count * sizeof(capture[0]) * 2 +
        s->stack_size_max * sizeof(stack_buf[0]);
    s->state_stack = NULL;
    s->state_stack_len = 0;
    s->state_stack_size = 0;
    
    for(i = 0; i < s->capture_count * 2; i++)
        capture[i] = NULL;
    alloca_size = s->stack_size_max * sizeof(stack_buf[0]);
    stack_buf = alloca(alloca_size);
    ret = lre_exec_backtrack(s, capture, stack_buf, 0, bc_buf + RE_HEADER_LEN,
                             cbuf + (cindex << cbuf_type), FALSE);
    lre_realloc(s->opaque, s->state_stack, 0);
    return ret;
}

int lre_get_capture_count(const uint8_t *bc_buf)
{
    return bc_buf[RE_HEADER_CAPTURE_COUNT];
}

int lre_get_flags(const uint8_t *bc_buf)
{
    return bc_buf[RE_HEADER_FLAGS];
}

#ifdef TEST

BOOL lre_check_stack_overflow(void *opaque, size_t alloca_size)
{
    return FALSE;
}

void *lre_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

int main(int argc, char **argv)
{
    int len, ret, i;
    uint8_t *bc;
    char error_msg[64];
    uint8_t *capture[CAPTURE_COUNT_MAX * 2];
    const char *input;
    int input_len, capture_count;
    
    if (argc < 3) {
        printf("usage: %s regexp input\n", argv[0]);
        exit(1);
    }
    bc = lre_compile(&len, error_msg, sizeof(error_msg), argv[1],
                     strlen(argv[1]), 0, NULL);
    if (!bc) {
        fprintf(stderr, "error: %s\n", error_msg);
        exit(1);
    }

    input = argv[2];
    input_len = strlen(input);
    
    ret = lre_exec(capture, bc, (uint8_t *)input, 0, input_len, 0, NULL);
    printf("ret=%d\n", ret);
    if (ret == 1) {
        capture_count = lre_get_capture_count(bc);
        for(i = 0; i < 2 * capture_count; i++) {
            uint8_t *ptr;
            ptr = capture[i];
            printf("%d: ", i);
            if (!ptr)
                printf("<nil>");
            else
                printf("%u", (int)(ptr - (uint8_t *)input));
            printf("\n");
        }
    }
    return 0;
}
#endif
