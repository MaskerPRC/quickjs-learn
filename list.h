// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *类似Linux klist的系统**版权所有(C)2016-2017 Fabrice Bellard**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
#ifndef LIST_H
#define LIST_H

#ifndef NULL
#include <stddef.h>
#endif

struct list_head {
    struct list_head *prev;
    struct list_head *next;
};

#define LIST_HEAD_INIT(el) { &(el), &(el) }

/*  返回‘type*’类型的指针，该指针包含‘el’作为字段‘Members’ */ 
#define list_entry(el, type, member) \
    ((type *)((uint8_t *)(el) - offsetof(type, member)))

static inline void init_list_head(struct list_head *head)
{
    head->prev = head;
    head->next = head;
}

/*  在“prev”和“Next”之间插入“el” */ 
static inline void __list_add(struct list_head *el, 
                              struct list_head *prev, struct list_head *next)
{
    prev->next = el;
    el->prev = prev;
    el->next = next;
    next->prev = el;
}

/*  在列表‘head’的开头加上‘el’(=在元素head之后)。 */ 
static inline void list_add(struct list_head *el, struct list_head *head)
{
    __list_add(el, head, head->next);
}

/*  在列表‘Head’的末尾添加‘el’(=在元素Head之前)。 */ 
static inline void list_add_tail(struct list_head *el, struct list_head *head)
{
    __list_add(el, head->prev, head);
}

static inline void list_del(struct list_head *el)
{
    struct list_head *prev, *next;
    prev = el->prev;
    next = el->next;
    prev->next = next;
    next->prev = prev;
    el->prev = NULL; /*  故障安全。 */ 
    el->next = NULL; /*  故障安全。 */ 
}

static inline int list_empty(struct list_head *el)
{
    return el->next == el;
}

#define list_for_each(el, head) \
  for(el = (head)->next; el != (head); el = el->next)

#define list_for_each_safe(el, el1, head)                \
    for(el = (head)->next, el1 = el->next; el != (head); \
        el = el1, el1 = el->next)

#define list_for_each_prev(el, head) \
  for(el = (head)->prev; el != (head); el = el->prev)

#define list_for_each_prev_safe(el, el1, head)           \
    for(el = (head)->prev, el1 = el->prev; el != (head); \
        el = el1, el1 = el->prev)

#endif /*  列表_H */ 
