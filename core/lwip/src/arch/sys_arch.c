#include "arch/sys_arch.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include <core/jiffies.h>
#include <stdlib.h>

void sys_init(void)
{
}

sys_sem_t sys_sem_new(u8_t count)
{
    sys_sem_t sem = mem_malloc(sizeof(struct semaphore));
    if (!sem)
	return NULL;

    sem_init(sem, count);
    return sem;
}

void sys_sem_free(sys_sem_t sem)
{
    mem_free(sem);
}

u32_t sys_arch_sem_wait(sys_sem_t sem, u32_t timeout)
{
    jiffies_t rv;

    rv = sem_down(sem, (timeout+MS_PER_JIFFY-1)/MS_PER_JIFFY);
    if (rv == (jiffies_t)-1)
	return SYS_ARCH_TIMEOUT;
    else
	return rv * MS_PER_JIFFY;
}

sys_mbox_t sys_mbox_new(int size)
{
    struct mailbox *mbox;

    mbox = mem_malloc(sizeof(struct mailbox) + size*sizeof(void *));
    if (!mbox)
	return NULL;

    mbox_init(mbox, size);
    return mbox;
}

void sys_mbox_free(sys_mbox_t mbox)
{
    mem_free(mbox);
}

void sys_mbox_post(sys_mbox_t mbox, void *msg)
{
    mbox_post(mbox, msg, 0);
}

err_t sys_mbox_trypost(sys_mbox_t mbox, void *msg)
{
    return mbox_post(mbox, msg, -1) ? ERR_MEM : ERR_OK;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t mbox, void **msg, u32_t timeout)
{
    jiffies_t rv;

    rv = mbox_fetch(mbox, msg, (timeout+MS_PER_JIFFY-1)/MS_PER_JIFFY);
    if (rv == (jiffies_t)-1)
	return SYS_ARCH_TIMEOUT;
    else
	return rv * MS_PER_JIFFY;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t mbox, void **msg)
{
    return mbox_fetch(mbox, msg, -1);
}

u32_t sys_now(void)
{
    return jiffies() * MS_PER_JIFFY;
}
