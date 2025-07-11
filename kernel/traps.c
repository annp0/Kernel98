#include <asm/system.h>
#include <kernel/kernel.h>
#include <kernel/sched.h>
#include <asm/io.h>

#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
 :"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})

#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
    :"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})


#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})


void divide_error();
void debug();
void nmi();
void int3();
void overflow();
void bounds();
void invalid_op();
void double_fault();
void coprocessor_segment_overrun();
void invalid_TSS();
void segment_not_present();
void stack_segment();
void page_fault();
void general_protection();
void reserved();
void irq13();
void alignment_check();

static void die(char* str, long esp_ptr, long nr) {
    int i = 0;
    long* esp = (long*)esp_ptr;

    printk("\n\r%s: %04x\n\r", str, nr & 0xffff);
    printk("EIP:\t%04x:%p\n\rEFLAGS:\t%p\n\rESP:\t%04x:%p\n\r",
            esp[1],esp[0],esp[2],esp[4],esp[3]);

    printk("fs: %04x\n\r",_fs());
    printk("base: %p, limit: %p\n\r",get_base(current->ldt[1]),get_limit(0x17));
    if (esp[4] == 0x17) {
        printk("Stack: ");
        for (i=0;i<4;i++)
            printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
        printk("\n\r");
    }

    for(i=0;i<10;i++)
        printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
    printk("\n\r");

    while (1) {
    }
}

void do_double_fault(long esp, long error_code) {
    die("double fault", esp, error_code);
}

void do_general_protection(long esp, long error_code) {
    die("general protection", esp, error_code);
}

void do_alignment_check(long esp, long error_code) {
    die("alignment check", esp, error_code);
}

void do_divide_error(long esp, long error_code) {
    die("divide error", esp, error_code);
}

void do_int3(long * esp, long error_code,
        long fs,long es,long ds,
        long ebp,long esi,long edi,
        long edx,long ecx,long ebx,long eax) {
    int tr;

    __asm__("str %%ax":"=a" (tr):"" (0));
    printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
            eax,ebx,ecx,edx);
    printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
            esi,edi,ebp,(long) esp);
    printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
            ds,es,fs,tr);
    printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code) {
    die("nmi", esp, error_code);
}

void do_debug(long esp, long error_code) {
    die("debug", esp, error_code);
}

void do_overflow(long esp, long error_code) {
    die("overflow", esp, error_code);
}

void do_bounds(long esp, long error_code) {
    die("bounds", esp, error_code);
}

void do_invalid_op(long esp, long error_code) {
    die("invalid_op", esp, error_code);
}

void do_device_not_available(long esp, long error_code) {
    die("device not available", esp, error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code) {
    die("coprocessor segment overrun", esp, error_code);
}

void do_segment_not_present(long esp, long error_code) {
    die("segment not present", esp, error_code);
}

void do_invalid_TSS(long esp, long error_code) {
    die("invalid tss", esp, error_code);
}

void do_stack_segment(long esp, long error_code) {
    die("stack segment", esp, error_code);
}

void do_reserved(long esp, long error_code) {
    die("reserved (15,17-47) error",esp,error_code);
}

void trap_init() {
    int i;

    set_trap_gate(0, &divide_error);
    set_trap_gate(1,&debug);
    set_trap_gate(2,&nmi);
    set_system_gate(3,&int3);
    set_system_gate(4,&overflow);
    set_system_gate(5,&bounds);
    set_trap_gate(6,&invalid_op);
    set_trap_gate(8,&double_fault);
    set_trap_gate(9,&coprocessor_segment_overrun);
    set_trap_gate(10, &invalid_TSS);
    set_trap_gate(11, &segment_not_present);
    set_trap_gate(12, &stack_segment);
    set_trap_gate(13, &general_protection);
    set_trap_gate(14, &page_fault);
    set_trap_gate(15,&reserved);
    set_trap_gate(17,&alignment_check);

    for (i=18;i<48;i++)
        set_trap_gate(i,&reserved);

    outb_p(inb_p(0x21)&0xfb,0x21);
    outb(inb_p(0xA1)&0xdf,0xA1);
}

