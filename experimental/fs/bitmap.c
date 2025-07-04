#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

#define clear_block(addr) \
    __asm__("cld\n\t" \
            "rep\n\t" \
            "stosl" \
            ::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):)


#define set_bit(nr,addr) ({\
    register int res __asm__("ax"); \
    __asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
            "=a" (res):"a" (0),"r" (nr),"m" (*(addr))); \
    res;})

#define clear_bit(nr, addr) ({\
    register int res __asm__("ax"); \
    __asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
            "=a" (res):"a" (0),"r" (nr),"m" (*(addr))); \
    res;})


#define find_first_zero(addr) ({ \
    int __res; \
    __asm__("cld\n" \
        "1:\tlodsl\n\t" \
        "notl %%eax\n\t" \
        "bsfl %%eax,%%edx\n\t" \
        "je 2f\n\t" \
        "addl %%edx,%%ecx\n\t" \
        "jmp 3f\n" \
        "2:\taddl $32,%%ecx\n\t" \
        "cmpl $8192,%%ecx\n\t" \
        "jl 1b\n" \
        "3:" \
        :"=c" (__res):"c" (0),"S" (addr):"ax","dx"); \
    __res;})


int new_block(int dev) {
    struct buffer_head * bh;
    struct super_block * psb;
    int i, j;

    psb = &sb;
    j = 8192;
    for (i=0 ; i<8 ; i++) {
        if ((bh = psb->s_zmap[i])) {
            if ((j = find_first_zero(bh->b_data))<8192)
                break;
        }
    }
    if (i>=8 || !bh || j >= 8192) {
        return 0;
    }

    if (set_bit(j, bh->b_data))
        panic("new_block: bit already set");

    bh->b_dirt = 1;
    j += i*8192 + psb->s_firstdatazone-1;
    if (j >= psb->s_nzones)
        return 0;

    if (!(bh=getblk(dev,j)))
        panic("new_block: cannot get block");
    if (bh->b_count != 1)
        panic("new block: count is != 1");
    clear_block(bh->b_data);
    bh->b_uptodate = 1;
    bh->b_dirt = 1;
    brelse(bh);
    return j;
}

int free_block(int dev, int block) {
    struct super_block * psb;
    struct buffer_head * bh;

    psb = &sb;

    if (block < psb->s_firstdatazone || block >= psb->s_nzones)
        panic("trying to free block not in datazone");

    bh = get_hash_table(dev,block);
    if (bh) {
        if (bh->b_count > 1) {
            brelse(bh);
            return 0;
        }
        bh->b_dirt=0;
        bh->b_uptodate=0;
        if (bh->b_count)
            brelse(bh);
    }

    block -= psb->s_firstdatazone - 1;
    if (clear_bit(block&8191,psb->s_zmap[block/8192]->b_data)) {
        printk("block (%04x:%d) ",dev,block+psb->s_firstdatazone-1);
        panic("free_block: bit already cleared\n");
    }
    psb->s_zmap[block/8192]->b_dirt = 1;
    return 1;
}

struct m_inode * new_inode(int dev) {
    struct m_inode * inode;
    struct super_block * psb;
    struct buffer_head * bh;
    int i, j;

    if (!(inode=get_empty_inode()))
        return NULL;

    psb = &sb;
    j = 8192;
    for (i=0 ; i<8 ; i++) {
        if ((bh=psb->s_imap[i])) {
            if ((j=find_first_zero(bh->b_data))<8192)
                break;
        }
    }

    if (!bh || j >= 8192 || j+i*8192 > psb->s_ninodes) {
        iput(inode);
        return NULL;
    }

    if (set_bit(j,bh->b_data))
        panic("new_inode: bit already set");

    bh->b_dirt = 1;
    inode->i_count = 1;
    inode->i_nlinks = 1;
    inode->i_dev = dev;
    inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_dirt=1;
    inode->i_num = j + i*8192;
    return inode;
}

void free_inode(struct m_inode * inode) {
	struct super_block * psb = &sb;
	struct buffer_head * bh;

	if (!inode)
		return;
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
	if (inode->i_nlinks)
		panic("trying to free inode with links");
	if (inode->i_num < 1 || inode->i_num > psb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	if (!(bh=psb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
	if (clear_bit(inode->i_num&8191,bh->b_data))
		panic("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	memset(inode,0,sizeof(*inode));
}

