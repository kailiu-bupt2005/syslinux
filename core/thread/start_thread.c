#include <string.h>
#include <stdlib.h>
#include "thread.h"

extern void __start_thread(void);

/*
 * Stack frame used by __switch_to, see thread_asm.S
 */
struct thread_stack {
    int errno;
    uint32_t edi, esi, ebp, ebx;
    void (*eip)(void);
};

struct thread *start_thread(const char *name, size_t stack_size, int prio,
			    void (*start_func)(void *), void *func_arg)
{
    irq_state_t irq;
    struct thread *curr, *t;
    char *stack;
    const size_t thread_mask = 31; /* Alignment mask */
    struct thread_stack *sp;

    stack_size = (stack_size + thread_mask) & ~thread_mask;
    stack = malloc(stack_size + sizeof(struct thread));
    if (!stack)
	return NULL;

    t = (struct thread *)stack;
    stack = (char *)(t + 1);	/* After the thread structure */

    memset(t, 0, sizeof *t);

    /* sp allocated from the end of the stack */
    sp = (struct thread_stack *)(stack + stack_size) - 1;
    t->esp = sp;

    sp->errno = 0;
    sp->esi = (size_t)start_func;
    sp->edi = (size_t)func_arg;
    sp->ebx = irq_state();	/* Inherit the IRQ state from the spawner */
    sp->eip = __start_thread;
    t->prio = prio;
    t->name = name;

    irq = irq_save();
    curr = current();

    t->list.prev       = &curr->list;
    t->list.next       = curr->list.next;
    curr->list.next    = &t->list;
    t->list.next->prev = &t->list;

    __schedule();

    irq_restore(irq);
    return t;
}
