// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *QuickJS：二进制JSON模块(仅限测试)**版权所有(C)2017-2019 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#include "quickjs-libc.h"
#include "cutils.h"

static JSValue js_bjson_read(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    uint64_t pos, len;
    JSValue obj;
    size_t size;
    
    if (JS_ToIndex(ctx, &pos, argv[1]))
        return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &len, argv[2]))
        return JS_EXCEPTION;
    buf = JS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!buf)
        return JS_EXCEPTION;
    if (pos + len > size)
        return JS_ThrowRangeError(ctx, "array buffer overflow");
    obj = JS_ReadObject(ctx, buf + pos, len, 0);
    return obj;
}

static JSValue js_bjson_write(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    size_t len;
    uint8_t *buf;
    JSValue array;
    
    buf = JS_WriteObject(ctx, &len, argv[0], 0);
    if (!buf)
        return JS_EXCEPTION;
    array = JS_NewArrayBufferCopy(ctx, buf, len);
    js_free(ctx, buf);
    return array;
}

static const JSCFunctionListEntry js_bjson_funcs[] = {
    JS_CFUNC_DEF("read", 3, js_bjson_read ),
    JS_CFUNC_DEF("write", 1, js_bjson_write ),
};

static int js_bjson_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, js_bjson_funcs,
                                  countof(js_bjson_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_bjson
#endif

JSModuleDef *JS_INIT_MODULE(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_bjson_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_bjson_funcs, countof(js_bjson_funcs));
    return m;
}
