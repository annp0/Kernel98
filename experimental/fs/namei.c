#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <const.h>
#include <sys/stat.h>

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

#define ACC_MODE(x) ("\004\002\006\377"[(x)&O_ACCMODE])

struct m_inode * _namei(const char * pathname, struct m_inode * base,
        int follow_links);

static int permission(struct m_inode * inode,int mask) {
    return 1;
}

static int match(int len,const char * name,struct dir_entry * de) {
    register int same __asm__("ax");
    if (!de || !de->inode || len > NAME_LEN)
        return 0;

    if (!len && (de->name[0]=='.') && (de->name[1]=='\0'))
        return 1;

    if (len < NAME_LEN && de->name[len])
        return 0;

    __asm__("cld\n\t"
            "fs ; repe ; cmpsb\n\t"
            "setz %%al"
            :"=a" (same)
            :"a" (0),"S" ((long) name),"D" ((long) de->name),"c" (len):);

    return same;
}

static struct buffer_head * find_entry(struct m_inode ** dir,
        const char * name, int namelen, struct dir_entry ** res_dir) {
    int entries;
    int block,i;
    struct buffer_head * bh;
    struct dir_entry * de;

    entries = (*dir)->i_size / (sizeof (struct dir_entry));
    *res_dir = NULL;

    if (namelen==2 && get_fs_byte(name)=='.' && get_fs_byte(name+1)=='.') {
        if ((*dir) == current->root)
            namelen=1;
    }

    if (!(block = (*dir)->i_zone[0]))
        return NULL;

    if (!(bh = bread((*dir)->i_dev,block)))
        return NULL;

    i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (i < entries) {
        if ((char *)de >= BLOCK_SIZE+bh->b_data) {
            brelse(bh);
            bh = NULL;
            if (!(block = bmap(*dir,i/DIR_ENTRIES_PER_BLOCK)) ||
                    !(bh = bread((*dir)->i_dev,block))) {
                i += DIR_ENTRIES_PER_BLOCK;
                continue;
            }
            de = (struct dir_entry *) bh->b_data;
        }
        if (match(namelen,name,de)) {
            *res_dir = de;
            return bh;
        }
        de++;
        i++;
    }
    brelse(bh);
    return NULL;
}

static struct buffer_head * add_entry(struct m_inode * dir,
    const char * name, int namelen, struct dir_entry ** res_dir) {
    int block,i;
    struct buffer_head * bh;
    struct dir_entry * de;

    *res_dir = NULL;
    if (!namelen)
        return NULL;
    if (!(block = dir->i_zone[0]))
        return NULL;
    if (!(bh = bread(dir->i_dev,block)))
        return NULL;

    i = 0;
    de = (struct dir_entry *) bh->b_data;
    while (1) {
        if ((char *)de >= BLOCK_SIZE+bh->b_data) {
            brelse(bh);
            bh = NULL;
            block = create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
            if (!block)
                return NULL;
            if (!(bh = bread(dir->i_dev,block))) {
                i += DIR_ENTRIES_PER_BLOCK;
                continue;
            }
            de = (struct dir_entry *) bh->b_data;
        }

        if (i*sizeof(struct dir_entry) >= dir->i_size) {
            de->inode=0;
            dir->i_size = (i+1)*sizeof(struct dir_entry);
            dir->i_dirt = 1;
        }

        if (!de->inode) {
            for (i=0; i < NAME_LEN ; i++)
                de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
            bh->b_dirt = 1;
            *res_dir = de;
            return bh;
        }
        de++;
        i++;
    }
}

static struct m_inode * follow_link(struct m_inode * dir, struct m_inode * inode) {
    unsigned short fs;
    struct buffer_head * bh;

    if (!dir) {
        dir = current->root;
        dir->i_count++;
    }
    if (!inode) {
        iput(dir);
        return NULL;
    }
    if (!S_ISLNK(inode->i_mode)) {
        iput(dir);
        return inode;
    }
    __asm__("mov %%fs,%0":"=r" (fs));
    if (fs != 0x17 || !inode->i_zone[0] ||
       !(bh = bread(inode->i_dev, inode->i_zone[0]))) {
        iput(dir);
        iput(inode);
        return NULL;
    }
    iput(inode);
    __asm__("mov %0,%%fs"::"r" ((unsigned short) 0x10));
    inode = _namei(bh->b_data,dir,0);
    __asm__("mov %0,%%fs"::"r" (fs));
    brelse(bh);
    return inode;
}

static struct m_inode * get_dir(const char * pathname, struct m_inode * inode) {
    char c;
    const char * thisname;
    struct buffer_head * bh;
    int namelen,inr;
    struct dir_entry * de;
    struct m_inode * dir;

    if (!inode) {
        inode = current->pwd;
        inode->i_count++;
    }
    if ((c=get_fs_byte(pathname))=='/') {
        iput(inode);
        inode = current->root;
        pathname++;
        inode->i_count++;
    }

    while (1) {
        thisname = pathname;
        if (!S_ISDIR(inode->i_mode) || !permission(inode,MAY_EXEC)) {
            iput(inode);
            return NULL;
        }
        for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
            /* nothing */ ;
        if (!c)
            return inode;

        if (!(bh = find_entry(&inode,thisname,namelen,&de))) {
            iput(inode);
            return NULL;
        }

        inr = de->inode;
        brelse(bh);
        dir = inode;
        if (!(inode = iget(dir->i_dev,inr))) {
            iput(dir);
            return NULL;
        }

        if (!(inode = follow_link(dir,inode)))
            return NULL;
    }
    
    return NULL;
}

static struct m_inode * dir_namei(const char * pathname,
        int * namelen, const char ** name, struct m_inode * base) {
    char c;
    const char * basename;
    struct m_inode * dir;

    if (!(dir = get_dir(pathname,base)))
        return NULL;

    basename = pathname;
    while ((c = get_fs_byte(pathname++))) {
        if (c == '/')
            basename = pathname;
    }

    *namelen = pathname-basename-1;
    *name = basename;

    return dir;
}

int open_namei(const char * pathname, int flag, int mode,
        struct m_inode ** res_inode) {
    const char * basename;
    int inr,dev,namelen;
    struct m_inode * dir, *inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    if ((flag & O_TRUNC) && !(flag & O_ACCMODE))
        flag |= O_WRONLY;
    mode &= 0777 & ~current->umask;
    mode |= I_REGULAR;
    if (!(dir = dir_namei(pathname,&namelen,&basename,NULL)))
        return -ENOENT;

    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) {
        return -EINVAL;
    }

    inr = de->inode;
    dev = dir->i_dev;
    brelse(bh);

    if (!(inode = follow_link(dir,iget(dev,inr))))
        return -EACCES;

    if ((S_ISDIR(inode->i_mode) && (flag & O_ACCMODE)) ||
        !permission(inode,ACC_MODE(flag))) {
        iput(inode);
        return -EPERM;
    }
    if (flag & O_TRUNC)
        truncate(inode);

    inode = iget(dev,inr);
    *res_inode = inode;
    return 0;
}

struct m_inode * _namei(const char * pathname, struct m_inode * base,
        int follow_links) {
    const char * basename;
    int inr,namelen;
    struct m_inode * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    if (!(base = dir_namei(pathname,&namelen,&basename,base)))
        return NULL;

    if (!namelen)
        return base;

    bh = find_entry(&base,basename,namelen,&de);
    if (!bh) {
        iput(base);
        return NULL;
    }

    inr = de->inode;
    brelse(bh);
    if (!(inode = iget(base->i_dev,inr))) {
        iput(base);
        return NULL;
    }

    if (follow_links)
        inode = follow_link(base,inode);
    else
        iput(base);

    inode->i_dirt=1;
    return inode;
}

struct m_inode * namei(const char * pathname) {
    return _namei(pathname,NULL,1);
}

int sys_mkdir(const char * pathname, int mode) {
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh, *dir_block;
    struct dir_entry * de;

    if (!(dir = dir_namei(pathname,&namelen,&basename, NULL)))
        return -ENOENT;
    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }

    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        brelse(bh);
        iput(dir);
        return -EEXIST;
    }

    inode = new_inode(dir->i_dev);
    if (!inode) {
        iput(dir);
        return -ENOSPC;
    }
    inode->i_size = 32;
    inode->i_dirt = 1;

    if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ENOSPC;
    }

    inode->i_dirt = 1;
    if (!(dir_block=bread(inode->i_dev,inode->i_zone[0]))) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ERROR;
    }

    de = (struct dir_entry *) dir_block->b_data;
    de->inode=inode->i_num;
    strcpy(de->name,".");
    de++;
    de->inode = dir->i_num;
    strcpy(de->name,"..");
    inode->i_nlinks = 2;
    dir_block->b_dirt = 1;
    brelse(dir_block);

    inode->i_mode = I_DIRECTORY | (mode & 0777 & ~current->umask);
    inode->i_dirt = 1;

    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ENOSPC;
    }

    de->inode = inode->i_num;
    bh->b_dirt = 1;
    dir->i_nlinks++;
    dir->i_dirt = 1;
    iput(dir);
    iput(inode);
    brelse(bh);
    return 0;
}

static int empty_dir(struct m_inode * inode) {
    int nr,block;
    int len;
    struct buffer_head * bh;
    struct dir_entry * de;

    len = inode->i_size / sizeof (struct dir_entry);
    if (len<2 || !inode->i_zone[0] ||
            !(bh=bread(inode->i_dev,inode->i_zone[0]))) {
        printk("warning - bad directory on dev %04x\n",inode->i_dev);
        return 0;
    }

    de = (struct dir_entry *) bh->b_data;
    if (de[0].inode != inode->i_num || !de[1].inode ||
            strcmp(".",de[0].name) || strcmp("..",de[1].name)) {
        printk("warning - bad directory on dev %04x\n",inode->i_dev);
        return 0;
    }

    nr = 2;
    de += 2;
    while (nr<len) {
        if ((void *) de >= (void *) (bh->b_data+BLOCK_SIZE)) {
            brelse(bh);
            block=bmap(inode,nr/DIR_ENTRIES_PER_BLOCK);
            if (!block) {
                nr += DIR_ENTRIES_PER_BLOCK;
                break;
            }
            if (!(bh=bread(inode->i_dev,block)))
                return 0;
            de = (struct dir_entry *) bh->b_data;
        }

        if (de->inode) {
            brelse(bh);
            return 0;
        }
        de++;
        nr++;
    }

    brelse(bh);
    return 1;
}

int sys_rmdir(const char * name) {
    const char * basename;
    int namelen;
    struct m_inode * dir, * inode;
    struct buffer_head * bh;
    struct dir_entry * de;

    if (!(dir = dir_namei(name,&namelen,&basename, NULL)))
        return -ENOENT;

    if (!namelen) {
        iput(dir);
        return -ENOENT;
    }

    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EPERM;
    }

    bh = find_entry(&dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        return -ENOENT;
    }
    if (!(inode = iget(dir->i_dev, de->inode))) {
        iput(dir);
        brelse(bh);
        return -EPERM;
    }

    if (inode->i_dev != dir->i_dev || inode->i_count>1) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -EPERM;
    }

    if (inode == dir) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -EPERM;
    }

    if (!S_ISDIR(inode->i_mode)) {
        iput(dir);
        iput(inode);
        brelse(bh);
        return -ENOTDIR;
    }

    if (!empty_dir(inode)) {
        iput(inode);
        iput(dir);
        brelse(bh);
        return -ENOTEMPTY;
    }

    if (inode->i_nlinks != 2)
        printk("empty directory has nlink!=2 (%d)",inode->i_nlinks);

    de->inode = 0;
    bh->b_dirt = 1;
    brelse(bh);
    inode->i_nlinks = 0;
    inode->i_dirt=1;
    dir->i_nlinks--;
    dir->i_dirt=1;
    iput(dir);
    iput(inode);

    return 0;
}

int sys_symlink(const char * oldname, const char * newname) {
    struct dir_entry * de;
    struct m_inode * dir, * inode;
    struct buffer_head * bh, * name_block;
    const char * basename;
    int namelen, i;
    char c;

    dir = dir_namei(newname,&namelen,&basename, NULL);
    if (!dir)
        return -EACCES;
    if (!namelen) {
        iput(dir);
        return -EPERM;
    }
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        return -EACCES;
    }
    if (!(inode = new_inode(dir->i_dev))) {
        iput(dir);
        return -ENOSPC;
    }
    inode->i_mode = S_IFLNK | (0777 & ~current->umask);
    inode->i_dirt = 1;
    if (!(inode->i_zone[0]=new_block(inode->i_dev))) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ENOSPC;
    }
    inode->i_dirt = 1;
    if (!(name_block=bread(inode->i_dev,inode->i_zone[0]))) {
        iput(dir);
        inode->i_nlinks--;
        iput(inode);
        return -ERROR;
    }
    i = 0;
    while (i < 1023 && (c=get_fs_byte(oldname++)))
        name_block->b_data[i++] = c;
    name_block->b_data[i] = 0;
    name_block->b_dirt = 1;
    brelse(name_block);
    inode->i_size = i;
    inode->i_dirt = 1;
    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        inode->i_nlinks--;
        iput(inode);
        brelse(bh);
        iput(dir);
        return -EEXIST;
    }
    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        inode->i_nlinks--;
        iput(inode);
        iput(dir);
        return -ENOSPC;
    }
    de->inode = inode->i_num;
    bh->b_dirt = 1;
    brelse(bh);
    iput(dir);
    iput(inode);
    return 0;
}

int sys_link(const char * oldname, const char * newname) {
    struct dir_entry * de;
    struct m_inode * oldinode, * dir;
    struct buffer_head * bh;
    const char * basename;
    int namelen;

    oldinode=namei(oldname);
    if (!oldinode)
        return -ENOENT;
    if (S_ISDIR(oldinode->i_mode)) {
        iput(oldinode);
        return -EPERM;
    }
    dir = dir_namei(newname,&namelen,&basename, NULL);
    if (!dir) {
        iput(oldinode);
        return -EACCES;
    }
    if (!namelen) {
        iput(oldinode);
        iput(dir);
        return -EPERM;
    }
    if (dir->i_dev != oldinode->i_dev) {
        iput(dir);
        iput(oldinode);
        return -EXDEV;
    }
    if (!permission(dir,MAY_WRITE)) {
        iput(dir);
        iput(oldinode);
        return -EACCES;
    }
    bh = find_entry(&dir,basename,namelen,&de);
    if (bh) {
        brelse(bh);
        iput(dir);
        iput(oldinode);
        return -EEXIST;
    }
    bh = add_entry(dir,basename,namelen,&de);
    if (!bh) {
        iput(dir);
        iput(oldinode);
        return -ENOSPC;
    }
    de->inode = oldinode->i_num;
    bh->b_dirt = 1;
    brelse(bh);
    iput(dir);
    oldinode->i_nlinks++;
    oldinode->i_dirt = 1;
    iput(oldinode);
    return 0;
}

int sys_unlink(const char * name)
{
	const char * basename;
	int namelen;
	struct m_inode * dir, * inode;
	struct buffer_head * bh;
	struct dir_entry * de;

	if (!(dir = dir_namei(name,&namelen,&basename, NULL)))
		return -ENOENT;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (!permission(dir,MAY_WRITE)) {
		iput(dir);
		return -EPERM;
	}
	bh = find_entry(&dir,basename,namelen,&de);
	if (!bh) {
		iput(dir);
		return -ENOENT;
	}
	if (!(inode = iget(dir->i_dev, de->inode))) {
		iput(dir);
		brelse(bh);
		return -ENOENT;
	}
	if ((dir->i_mode & S_ISVTX)) {
		iput(dir);
		iput(inode);
		brelse(bh);
		return -EPERM;
	}

	if (S_ISDIR(inode->i_mode)) {
		iput(inode);
		iput(dir);
		brelse(bh);
		return -EPERM;
	}
	if (!inode->i_nlinks) {
		printk("Deleting nonexistent file (%04x:%d), %d\n",
			inode->i_dev,inode->i_num,inode->i_nlinks);
		inode->i_nlinks=1;
	}
	de->inode = 0;
	bh->b_dirt = 1;
	brelse(bh);
	inode->i_nlinks--;
	inode->i_dirt = 1;
	iput(inode);
	iput(dir);
	return 0;
}

