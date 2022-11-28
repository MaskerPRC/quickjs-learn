// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *QuickJS操作码定义**版权所有(C)2017-2018 Fabrice Bellard*版权所有(C)2017-2018查理·戈登**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 

#ifdef FMT
FMT(none)
FMT(none_int)
FMT(none_loc)
FMT(none_arg)
FMT(none_var_ref)
FMT(u8)
FMT(i8)
FMT(loc8)
FMT(const8)
FMT(label8)
FMT(u16)
FMT(i16)
FMT(label16)
FMT(npop)
FMT(npopx)
FMT(npop_u16)
FMT(loc)
FMT(arg)
FMT(var_ref)
FMT(u32)
FMT(i32)
FMT(const)
FMT(label)
FMT(atom)
FMT(atom_u8)
FMT(atom_u16)
FMT(atom_label_u8)
FMT(atom_label_u16)
FMT(label_u16)
#undef FMT
#endif /*  FMT。 */ 

#ifdef DEF

#ifndef def
#define def(id, size, n_pop, n_push, f) DEF(id, size, n_pop, n_push, f)
#endif

DEF(invalid, 1, 0, 0, none) /*  从未排放过。 */ 

/*  推送值。 */ 
DEF(       push_i32, 5, 0, 1, i32)
DEF(     push_const, 5, 0, 1, const)
DEF(       fclosure, 5, 0, 1, const) /*  必须遵循PUSH_CONST。 */ 
DEF(push_atom_value, 5, 0, 1, atom)
DEF( private_symbol, 5, 0, 1, atom)
DEF(      undefined, 1, 0, 1, none)
DEF(           null, 1, 0, 1, none)
DEF(      push_this, 1, 0, 1, none) /*  仅在函数开始时使用。 */ 
DEF(     push_false, 1, 0, 1, none)
DEF(      push_true, 1, 0, 1, none)
DEF(         object, 1, 0, 1, none)
DEF( special_object, 2, 0, 1, u8) /*  仅在函数开始时使用。 */ 
DEF(           rest, 3, 0, 1, u16) /*  仅在函数开始时使用。 */ 

DEF(           drop, 1, 1, 0, none) /*  A-&gt;。 */ 
DEF(            nip, 1, 2, 1, none) /*  A b-&gt;b。 */ 
DEF(           nip1, 1, 3, 2, none) /*  A b c-&gt;b c。 */ 
DEF(            dup, 1, 1, 2, none) /*  A-&gt;a a。 */ 
DEF(           dup1, 1, 2, 3, none) /*  A b-&gt;a a b。 */ 
DEF(           dup2, 1, 2, 4, none) /*  A b-&gt;a b a b b。 */ 
DEF(           dup3, 1, 3, 6, none) /*  A b c-&gt;a b c a b c。 */ 
DEF(        insert2, 1, 2, 3, none) /*  对象a-&gt;对象a(Dup_X1)。 */ 
DEF(        insert3, 1, 3, 4, none) /*  对象道具a-&gt;对象道具a(Dup_X2)。 */ 
DEF(        insert4, 1, 4, 5, none) /*  这个对象道具-&gt;这个对象道具a。 */ 
DEF(          perm3, 1, 3, 3, none) /*  Obj a b-&gt;a obj b。 */ 
DEF(          perm4, 1, 4, 4, none) /*  Obj道具a b-&gt;obj道具b。 */ 
DEF(          perm5, 1, 5, 5, none) /*  这个道具b-&gt;a这个道具b。 */ 
DEF(           swap, 1, 2, 2, none) /*  A b-&gt;b a。 */ 
DEF(          swap2, 1, 4, 4, none) /*  A b c d-&gt;c d a b。 */ 
DEF(          rot3l, 1, 3, 3, none) /*  X a b-&gt;a b x。 */ 
DEF(          rot3r, 1, 3, 3, none) /*  X-&gt;xa b。 */ 
DEF(          rot4l, 1, 4, 4, none) /*  X a b c-&gt;a b c x。 */ 
DEF(          rot5l, 1, 5, 5, none) /*  X a b c d-&gt;a b c d x。 */ 

DEF(call_constructor, 3, 2, 1, npop) /*  Func new.Target args-&gt;ret。参数不在n_op中计算。 */ 
DEF(           call, 3, 1, 1, npop) /*  参数不在n_op中计算。 */ 
DEF(      tail_call, 3, 1, 0, npop) /*  参数不在n_op中计算。 */ 
DEF(    call_method, 3, 2, 1, npop) /*  参数不在n_op中计算。 */ 
DEF(tail_call_method, 3, 2, 0, npop) /*  参数不在n_op中计算。 */ 
DEF(     array_from, 3, 0, 1, npop) /*  参数不在n_op中计算。 */ 
DEF(          apply, 3, 3, 1, u16)
DEF(         return, 1, 1, 0, none)
DEF(   return_undef, 1, 0, 0, none)
DEF(check_ctor_return, 1, 1, 2, none)
DEF(     check_ctor, 1, 0, 0, none)
DEF(    check_brand, 1, 2, 2, none) /*  This_obj函数-&gt;This_obj函数。 */ 
DEF(      add_brand, 1, 2, 0, none) /*  This_obj Home_obj-&gt;。 */ 
DEF(   return_async, 1, 1, 0, none)
DEF(          throw, 1, 1, 0, none)
DEF(      throw_var, 6, 0, 0, atom_u8)
DEF(           eval, 5, 1, 1, npop_u16) /*  他妈的.。-&gt;RET_VAL。 */ 
DEF(     apply_eval, 3, 2, 1, u16) /*  函数数组-&gt;RET_EVAL。 */ 
DEF(         regexp, 1, 2, 1, none) /*  从该模式和一个字节码字符串。 */ 
DEF( get_super_ctor, 1, 1, 1, none)
DEF(      get_super, 1, 1, 1, none)
DEF(         import, 1, 1, 1, none) /*  动态模块导入。 */ 

DEF(      check_var, 5, 0, 1, atom) /*  检查变量是否存在。 */ 
DEF(  get_var_undef, 5, 0, 1, atom) /*  如果变量不存在，则推送未定义。 */ 
DEF(        get_var, 5, 0, 1, atom) /*  如果变量不存在，则引发异常。 */ 
DEF(        put_var, 5, 1, 0, atom) /*  必须在get_var之后。 */ 
DEF(   put_var_init, 5, 1, 0, atom) /*  必须在Put_var之后。用于初始化全局词法变量。 */ 
DEF( put_var_strict, 5, 2, 0, atom) /*  对于严格模式变量写入。 */ 

DEF(  get_ref_value, 1, 2, 3, none)
DEF(  put_ref_value, 1, 3, 0, none)

DEF(     define_var, 6, 0, 0, atom_u8)
DEF(check_define_var, 6, 0, 0, atom_u8)
DEF(    define_func, 6, 1, 0, atom_u8)
DEF(      get_field, 5, 1, 1, atom)
DEF(     get_field2, 5, 1, 2, atom)
DEF(      put_field, 5, 2, 0, atom)
DEF( get_private_field, 1, 2, 1, none) /*  OBJ属性-&gt;值。 */ 
DEF( put_private_field, 1, 3, 0, none) /*  OBJ价值属性-&gt;。 */ 
DEF(define_private_field, 1, 3, 1, none) /*  对象属性值-&gt;对象。 */ 
DEF(   get_array_el, 1, 2, 1, none)
DEF(  get_array_el2, 1, 2, 2, none) /*  对象属性-&gt;对象值。 */ 
DEF(   put_array_el, 1, 3, 0, none)
DEF(get_super_value, 1, 3, 1, none) /*  此对象属性-&gt;值。 */ 
DEF(put_super_value, 1, 4, 0, none) /*  此对象属性值-&gt;。 */ 
DEF(   define_field, 5, 2, 1, atom)
DEF(       set_name, 5, 1, 1, atom)
DEF(set_name_computed, 1, 2, 2, none)
DEF(      set_proto, 1, 2, 1, none)
DEF(set_home_object, 1, 2, 2, none)
DEF(define_array_el, 1, 3, 2, none)
DEF(         append, 1, 3, 2, none) /*  追加枚举对象，更新长度。 */ 
DEF(copy_data_properties, 2, 3, 3, u8)
DEF(  define_method, 6, 2, 1, atom_u8)
DEF(define_method_computed, 2, 3, 1, u8) /*  必须在定义方法之后。 */ 
DEF(   define_class, 6, 2, 2, atom_u8) /*  父组件-&gt;组件原件。 */ 
DEF(   define_class_computed, 6, 3, 3, atom_u8) /*  FIELD_NAME父类-&gt;FIELD_NAME类原件(具有计算名称的类)。 */ 

DEF(        get_loc, 3, 0, 1, loc)
DEF(        put_loc, 3, 1, 0, loc) /*  必须在get_loc之后。 */ 
DEF(        set_loc, 3, 1, 1, loc) /*  必须在PUT_LOC之后。 */ 
DEF(        get_arg, 3, 0, 1, arg)
DEF(        put_arg, 3, 1, 0, arg) /*  必须在get_arg之后。 */ 
DEF(        set_arg, 3, 1, 1, arg) /*  必须在PUT_ARG之后。 */ 
DEF(    get_var_ref, 3, 0, 1, var_ref) 
DEF(    put_var_ref, 3, 1, 0, var_ref) /*  必须在get_var_ref之后。 */ 
DEF(    set_var_ref, 3, 1, 1, var_ref) /*  必须在put_var_ref之后。 */ 
DEF(set_loc_uninitialized, 3, 0, 0, loc)
DEF(  get_loc_check, 3, 0, 1, loc)
DEF(  put_loc_check, 3, 1, 0, loc) /*  必须在GET_LOC_CHECK之后。 */ 
DEF(  put_loc_check_init, 3, 1, 0, loc)
DEF(get_var_ref_check, 3, 0, 1, var_ref) 
DEF(put_var_ref_check, 3, 1, 0, var_ref) /*  必须在get_var_ref_check之后。 */ 
DEF(put_var_ref_check_init, 3, 1, 0, var_ref)
DEF(      close_loc, 3, 0, 0, loc)
DEF(       if_false, 5, 1, 0, label)
DEF(        if_true, 5, 1, 0, label) /*  必须在If_False之后。 */ 
DEF(           goto, 5, 0, 0, label) /*  必须在If_True之后。 */ 
DEF(          catch, 5, 0, 1, label)
DEF(          gosub, 5, 0, 0, label) /*  用于执行Finally块。 */ 
DEF(            ret, 1, 1, 0, none) /*  用于从Finally块返回。 */ 

DEF(      to_object, 1, 1, 1, none)
//  Def(TO_STRING，1，1，1，无)。
DEF(     to_propkey, 1, 1, 1, none)
DEF(    to_propkey2, 1, 2, 2, none)

DEF(   with_get_var, 10, 1, 0, atom_label_u8)     /*  必须与Scope_xxx的顺序相同。 */ 
DEF(   with_put_var, 10, 2, 1, atom_label_u8)     /*  必须与Scope_xxx的顺序相同。 */ 
DEF(with_delete_var, 10, 1, 0, atom_label_u8)     /*  必须与Scope_xxx的顺序相同。 */ 
DEF(  with_make_ref, 10, 1, 0, atom_label_u8)     /*  必须与Scope_xxx的顺序相同。 */ 
DEF(   with_get_ref, 10, 1, 0, atom_label_u8)     /*  必须与Scope_xxx的顺序相同。 */ 
DEF(with_get_ref_undef, 10, 1, 0, atom_label_u8)

DEF(   make_loc_ref, 7, 0, 2, atom_u16)
DEF(   make_arg_ref, 7, 0, 2, atom_u16)
DEF(make_var_ref_ref, 7, 0, 2, atom_u16)
DEF(   make_var_ref, 5, 0, 2, atom)

DEF(   for_in_start, 1, 1, 1, none)
DEF(   for_of_start, 1, 1, 3, none)
DEF(for_await_of_start, 1, 1, 3, none)
DEF(    for_in_next, 1, 1, 3, none)
DEF(    for_of_next, 2, 3, 5, u8)
DEF(for_await_of_next, 1, 3, 4, none)
DEF(iterator_get_value_done, 1, 1, 2, none)
DEF( iterator_close, 1, 3, 0, none)
DEF(iterator_close_return, 1, 4, 4, none)
DEF(async_iterator_close, 1, 3, 2, none)
DEF(async_iterator_next, 1, 4, 4, none)
DEF(async_iterator_get, 2, 4, 5, u8)
DEF(  initial_yield, 1, 0, 0, none)
DEF(          yield, 1, 1, 2, none)
DEF(     yield_star, 1, 2, 2, none)
DEF(async_yield_star, 1, 1, 2, none)
DEF(          await, 1, 1, 1, none)

/*  算术/逻辑运算。 */ 
DEF(            neg, 1, 1, 1, none)
DEF(           plus, 1, 1, 1, none)
DEF(            dec, 1, 1, 1, none)
DEF(            inc, 1, 1, 1, none)
DEF(       post_dec, 1, 1, 2, none)
DEF(       post_inc, 1, 1, 2, none)
DEF(        dec_loc, 2, 0, 0, loc8)
DEF(        inc_loc, 2, 0, 0, loc8)
DEF(        add_loc, 2, 1, 0, loc8)
DEF(            not, 1, 1, 1, none)
DEF(           lnot, 1, 1, 1, none)
DEF(         typeof, 1, 1, 1, none)
DEF(         delete, 1, 2, 1, none)
DEF(     delete_var, 5, 0, 1, atom)

DEF(            mul, 1, 2, 1, none)
DEF(            div, 1, 2, 1, none)
DEF(            mod, 1, 2, 1, none)
DEF(            add, 1, 2, 1, none)
DEF(            sub, 1, 2, 1, none)
DEF(            pow, 1, 2, 1, none)
DEF(            shl, 1, 2, 1, none)
DEF(            sar, 1, 2, 1, none)
DEF(            shr, 1, 2, 1, none)
DEF(             lt, 1, 2, 1, none)
DEF(            lte, 1, 2, 1, none)
DEF(             gt, 1, 2, 1, none)
DEF(            gte, 1, 2, 1, none)
DEF(     instanceof, 1, 2, 1, none)
DEF(             in, 1, 2, 1, none)
DEF(             eq, 1, 2, 1, none)
DEF(            neq, 1, 2, 1, none)
DEF(      strict_eq, 1, 2, 1, none)
DEF(     strict_neq, 1, 2, 1, none)
DEF(            and, 1, 2, 1, none)
DEF(            xor, 1, 2, 1, none)
DEF(             or, 1, 2, 1, none)
#ifdef CONFIG_BIGNUM
DEF(      mul_pow10, 1, 2, 1, none)
DEF(       math_div, 1, 2, 1, none)
DEF(       math_mod, 1, 2, 1, none)
DEF(       math_pow, 1, 2, 1, none)
#endif
/*  必须是最后一个非短和非临时操作码。 */ 
DEF(            nop, 1, 0, 0, none) 

/*  临时操作码：从未在最终字节码中发出。 */ 

def(set_arg_valid_upto, 3, 0, 0, arg) /*  在阶段1中排放，在阶段2中移除。 */ 

def(close_var_object, 1, 0, 0, none) /*  在阶段1中排放，在阶段2中移除。 */ 
def(    enter_scope, 3, 0, 0, u16)  /*  在阶段1中排放，在阶段2中移除。 */ 
def(    leave_scope, 3, 0, 0, u16)  /*  在阶段1中排放，在阶段2中移除。 */ 

def(          label, 5, 0, 0, label) /*  在阶段1中排放，在阶段3中移除。 */ 

def(scope_get_var_undef, 7, 0, 1, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(  scope_get_var, 7, 0, 1, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(  scope_put_var, 7, 1, 0, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(scope_delete_var, 7, 0, 1, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def( scope_make_ref, 11, 0, 2, atom_label_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(  scope_get_ref, 7, 0, 2, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(scope_put_var_init, 7, 0, 2, atom_u16) /*  在阶段1中排放，在阶段2中移除。 */ 
def(scope_get_private_field, 7, 1, 1, atom_u16) /*  OBJ-&gt;值，在阶段1中发出，在阶段2中删除。 */ 
def(scope_get_private_field2, 7, 1, 2, atom_u16) /*  Obj-&gt;obj值，在第一阶段发出，在第二阶段移除。 */ 
def(scope_put_private_field, 7, 1, 1, atom_u16) /*  OBJ值-&gt;，在阶段1中发射，在阶段2中删除。 */ 

def( set_class_name, 5, 1, 1, u32) /*  在阶段1中排放，在阶段2中移除。 */ 
    
def(       line_num, 5, 0, 0, u32) /*  在阶段1中排放，在阶段3中移除。 */ 

#if SHORT_OPCODES
DEF(    push_minus1, 1, 0, 1, none_int)
DEF(         push_0, 1, 0, 1, none_int)
DEF(         push_1, 1, 0, 1, none_int)
DEF(         push_2, 1, 0, 1, none_int)
DEF(         push_3, 1, 0, 1, none_int)
DEF(         push_4, 1, 0, 1, none_int)
DEF(         push_5, 1, 0, 1, none_int)
DEF(         push_6, 1, 0, 1, none_int)
DEF(         push_7, 1, 0, 1, none_int)
DEF(        push_i8, 2, 0, 1, i8)
DEF(       push_i16, 3, 0, 1, i16)
DEF(    push_const8, 2, 0, 1, const8)
DEF(      fclosure8, 2, 0, 1, const8) /*  必须遵循PUSH_CONT8。 */ 
DEF(push_empty_string, 1, 0, 1, none)

DEF(       get_loc8, 2, 0, 1, loc8)
DEF(       put_loc8, 2, 1, 0, loc8)
DEF(       set_loc8, 2, 1, 1, loc8)

DEF(       get_loc0, 1, 0, 1, none_loc)
DEF(       get_loc1, 1, 0, 1, none_loc)
DEF(       get_loc2, 1, 0, 1, none_loc)
DEF(       get_loc3, 1, 0, 1, none_loc)
DEF(       put_loc0, 1, 1, 0, none_loc)
DEF(       put_loc1, 1, 1, 0, none_loc)
DEF(       put_loc2, 1, 1, 0, none_loc)
DEF(       put_loc3, 1, 1, 0, none_loc)
DEF(       set_loc0, 1, 1, 1, none_loc)
DEF(       set_loc1, 1, 1, 1, none_loc)
DEF(       set_loc2, 1, 1, 1, none_loc)
DEF(       set_loc3, 1, 1, 1, none_loc)
DEF(       get_arg0, 1, 0, 1, none_arg)
DEF(       get_arg1, 1, 0, 1, none_arg)
DEF(       get_arg2, 1, 0, 1, none_arg)
DEF(       get_arg3, 1, 0, 1, none_arg)
DEF(       put_arg0, 1, 1, 0, none_arg)
DEF(       put_arg1, 1, 1, 0, none_arg)
DEF(       put_arg2, 1, 1, 0, none_arg)
DEF(       put_arg3, 1, 1, 0, none_arg)
DEF(       set_arg0, 1, 1, 1, none_arg)
DEF(       set_arg1, 1, 1, 1, none_arg)
DEF(       set_arg2, 1, 1, 1, none_arg)
DEF(       set_arg3, 1, 1, 1, none_arg)
DEF(   get_var_ref0, 1, 0, 1, none_var_ref)
DEF(   get_var_ref1, 1, 0, 1, none_var_ref)
DEF(   get_var_ref2, 1, 0, 1, none_var_ref)
DEF(   get_var_ref3, 1, 0, 1, none_var_ref)
DEF(   put_var_ref0, 1, 1, 0, none_var_ref)
DEF(   put_var_ref1, 1, 1, 0, none_var_ref)
DEF(   put_var_ref2, 1, 1, 0, none_var_ref)
DEF(   put_var_ref3, 1, 1, 0, none_var_ref)
DEF(   set_var_ref0, 1, 1, 1, none_var_ref)
DEF(   set_var_ref1, 1, 1, 1, none_var_ref)
DEF(   set_var_ref2, 1, 1, 1, none_var_ref)
DEF(   set_var_ref3, 1, 1, 1, none_var_ref)

DEF(     get_length, 1, 1, 1, none)

DEF(      if_false8, 2, 1, 0, label8)
DEF(       if_true8, 2, 1, 0, label8) /*  必须在If_False8之后。 */ 
DEF(          goto8, 2, 0, 0, label8) /*  必须在if_true8之后。 */ 
DEF(         goto16, 3, 0, 0, label16)

DEF(          call0, 1, 1, 1, npopx)
DEF(          call1, 1, 1, 1, npopx)
DEF(          call2, 1, 1, 1, npopx)
DEF(          call3, 1, 1, 1, npopx)

DEF(   is_undefined, 1, 1, 1, none)
DEF(        is_null, 1, 1, 1, none)
DEF(    is_function, 1, 1, 1, none)
#endif

#undef DEF
#undef def
#endif  /*  DEF */ 
