// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *QuickJS：C模块示例**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#include "../quickjs.h"

#define countof(x) (sizeof(x) / sizeof((x)[0]))

static int fib(int n)
{
    if (n <= 0)
        return 0;
    else if (n == 1)
        return 1;
    else
        return fib(n - 1) + fib(n - 2);
}

static JSValue js_fib(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv)
{
    int n, res;
    if (JS_ToInt32(ctx, &n, argv[0]))
        return JS_EXCEPTION;
    res = fib(n);
    return JS_NewInt32(ctx, res);
}

static const JSCFunctionListEntry js_fib_funcs[] = {
    JS_CFUNC_DEF("fib", 1, js_fib ),
};

static int js_fib_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, js_fib_funcs,
                                  countof(js_fib_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_fib
#endif

JSModuleDef *JS_INIT_MODULE(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_fib_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_fib_funcs, countof(js_fib_funcs));
    return m;
}
