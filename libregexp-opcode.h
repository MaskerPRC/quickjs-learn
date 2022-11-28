// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *正则表达式引擎**版权所有(C)2017-2018 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 

#ifdef DEF

DEF(invalid, 1) /*  从未使用过。 */ 
DEF(char, 3)
DEF(char32, 5)
DEF(dot, 1)
DEF(any, 1) /*  与点相同，但匹配任何字符，包括行终止符。 */ 
DEF(line_start, 1)
DEF(line_end, 1)
DEF(goto, 5)
DEF(split_goto_first, 5)
DEF(split_next_first, 5)
DEF(match, 1)
DEF(save_start, 2) /*  保存起始位置。 */ 
DEF(save_end, 2) /*  保存结束位置，必须在保存开始之后。 */ 
DEF(save_reset, 3) /*  重置保存位置。 */ 
DEF(loop, 5) /*  递减堆栈顶部并转到If！=0。 */ 
DEF(push_i32, 5) /*  将整数推送到堆栈上。 */ 
DEF(drop, 1)
DEF(word_boundary, 1)
DEF(not_word_boundary, 1)
DEF(back_reference, 2)
DEF(backward_back_reference, 2) /*  必须在BACK_REFERENCE之后。 */ 
DEF(range, 3) /*  可变长度。 */ 
DEF(range32, 3) /*  可变长度。 */ 
DEF(lookahead, 5)
DEF(negative_lookahead, 5)
DEF(push_char_pos, 1) /*  按下堆栈上的字符位置。 */ 
DEF(bne_char_pos, 5) /*  弹出一个堆栈元素，如果等于字符，则跳转职位。 */ 
DEF(prev, 1) /*  转到上一次计费。 */ 
DEF(simple_greedy_quant, 17)

#endif /*  DEF */ 
