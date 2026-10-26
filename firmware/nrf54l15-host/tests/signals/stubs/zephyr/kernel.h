#ifndef SIGNALS_TEST_KERNEL_H
#define SIGNALS_TEST_KERNEL_H
struct k_sem {
    unsigned tokens;
};
#define K_SEM_DEFINE(name, start, maximum) struct k_sem name = {start}
#define K_MSEC(ms) (ms)
void k_sem_give(struct k_sem *sem);
int k_sem_take(struct k_sem *sem, int milliseconds);
void k_msleep(int milliseconds);
#endif
