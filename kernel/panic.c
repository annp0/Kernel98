#include <kernel/kernel.h>
#include <kernel/sched.h>

volatile void panic(const char * s) {
    printk("Kernel panic: %s\n\r",s);
    if (current == task[0])
        printk("In swapper task - not syncing\n\r");
    for(;;);
}
