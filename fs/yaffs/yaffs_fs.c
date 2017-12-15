/*
 * YAFFS: Yet another FFS. A NAND-flash specific file system.
 * yaffs_fs.c
 *
 * Copyright (C) 2002 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is the file system front-end to YAFFS that hooks it up to
 * the VFS.
 *
 * Special notes: 
 * >> 2.4: sb->u.generic_sbp points to the yaffs_Device associated with
 *         this superblock
 * >> 2.6: sb->s_fs_info  points to the yaffs_Device associated with this
 *         superblock
 * >> inode->i_private points to the associated yaffs_Object.
 *
 * Acknowledgements:
 * * Luc van OostenRyck for numerous patches.
 * * Nick Bane for numerous patches.
 * * Nick Bane for 2.5/2.6 integration.
 * * Andras Toth for mknod rdev issue.
 * * Michael Fischer for finding the problem with inode inconsistency.
 * * Some code bodily lifted from JFFS2.
 */

const char *yaffs_fs_c_version =
    "$Id: yaffs_fs.c,v 1.53 2006/10/03 10:13:03 charles Exp $";
extern const char *yaffs_guts_c_version;

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/mtd/super.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include <linux/seq_file.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))

#include <linux/statfs.h>	/* Added NCB 15-8-2003 */
#include <asm/statfs.h>
#define UnlockPage(p) unlock_page(p)
#define Page_Uptodate(page)	test_bit(PG_uptodate, &(page)->flags)

/* FIXME: use sb->s_id instead ? */
#define yaffs_devname(sb, buf)	bdevname(sb->s_bdev, buf)

#else

#include <linux/locks.h>
#define	BDEVNAME_SIZE		0
#define	yaffs_devname(sb, buf)	kdevname(sb->s_dev)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
/* added NCB 26/5/2006 for 2.4.25-vrs2-tcl1 kernel */
#define __user
#endif

#endif

#include <asm/uaccess.h>

#include "yportenv.h"
#include "yaffs_guts.h"
unsigned yaffs_traceMask = 0
                           | YAFFS_TRACE_ALWAYS
			   | YAFFS_TRACE_BAD_BLOCKS
                           //| YAFFS_TRACE_MTD
                           //| YAFFS_TRACE_OS
                           //| YAFFS_TRACE_TRACING
			   /* | YAFFS_TRACE_CHECKPOINT */
			   //| 0xFFFFFFFF
                           ;

static int cp_disabled = 0;

static int has_nand = 1;

static int __init set_no_nand(char *s) {
	has_nand = 0;
	return 0;
}
__setup("no-nand", set_no_nand);

#include <linux/mtd/mtd.h>
#include "yaffs_mtdif.h"
#include "yaffs_mtdif2.h"
#include "yaffs_mtdif2_nor.h"

/*#define T(x) printk x */

#define yaffs_InodeToObject(iptr) ((yaffs_Object *)((iptr)->i_private))
#define yaffs_DentryToObject(dptr) yaffs_InodeToObject((dptr)->d_inode)

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
#define yaffs_SuperToDevice(sb)	((yaffs_Device *)sb->s_fs_info)
#else
#define yaffs_SuperToDevice(sb)	((yaffs_Device *)sb->u.generic_sbp)
#endif

static void yaffs_put_super(struct super_block *sb);

static ssize_t yaffs_file_write(struct file *f, const char *buf, size_t n,
				loff_t * pos);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
static int yaffs_file_flush(struct file *file, fl_owner_t id);
#else
static int yaffs_file_flush(struct file *file);
#endif

static int yaffs_sync_object(struct file *file, loff_t start, loff_t end,
			     int datasync);

static long yaffs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static int yaffs_readdir(struct file *f, void *dirent, filldir_t filldir);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			struct nameidata *n);
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *n);
#else
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode);
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry);
#endif
static int yaffs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry);
static int yaffs_unlink(struct inode *dir, struct dentry *dentry);
static int yaffs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname);
static int yaffs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
		       dev_t dev);
#else
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
		       int dev);
#endif
static int yaffs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry);
static int yaffs_setattr(struct dentry *dentry, struct iattr *attr);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
static int yaffs_sync_fs(struct super_block *sb, int wait);
static void yaffs_write_super(struct super_block *sb);
#else
static int yaffs_sync_fs(struct super_block *sb);
static int yaffs_write_super(struct super_block *sb);
#endif
static int yaffs_remount_fs(struct super_block *sb, int *flags, char *data);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
static int yaffs_statfs(struct dentry *dentry, struct kstatfs *buf);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_statfs(struct super_block *sb, struct kstatfs *buf);
#else
static int yaffs_statfs(struct super_block *sb, struct statfs *buf);
#endif
static struct inode * yaffs_iget(struct super_block *sb, unsigned long ino);

static void yaffs_evict_inode(struct inode *);
static void yaffs_clear_inode(struct inode *);

static int yaffs_readpage(struct file *file, struct page *page);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_writepage(struct page *page, struct writeback_control *wbc);
#else
static int yaffs_writepage(struct page *page);
#endif
static int yaffs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata);
static int yaffs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata);

static void *yaffs_follow_link(struct dentry *dentry, struct nameidata *nd);
static void yaffs_put_link(struct dentry *direntry,
			   struct nameidata *nd, void *cookie);

static struct address_space_operations yaffs_file_address_operations = {
	.readpage = yaffs_readpage,
	.writepage = yaffs_writepage,
	.write_begin = yaffs_write_begin,
	.write_end = yaffs_write_end,
};

static struct file_operations yaffs_file_operations = {
	.llseek = generic_file_llseek,
	.open =	generic_file_open,
 	.read =	do_sync_read,
	.aio_read = generic_file_aio_read,
 	.write = do_sync_write,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap,
	.flush = yaffs_file_flush,
	.fsync = yaffs_sync_object,
	.unlocked_ioctl = yaffs_ioctl,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
};

static struct inode_operations yaffs_file_inode_operations = {
	.setattr = yaffs_setattr,
};

static struct inode_operations yaffs_symlink_inode_operations = {
	.readlink = generic_readlink,
	.follow_link = yaffs_follow_link,
	.put_link = yaffs_put_link,
	.setattr = yaffs_setattr,
};

static struct inode_operations yaffs_dir_inode_operations = {
	.create = yaffs_create,
	.lookup = yaffs_lookup,
	.link = yaffs_link,
	.unlink = yaffs_unlink,
	.symlink = yaffs_symlink,
	.mkdir = yaffs_mkdir,
	.rmdir = yaffs_unlink,
	.mknod = yaffs_mknod,
	.rename = yaffs_rename,
	.setattr = yaffs_setattr,
};

static struct file_operations yaffs_dir_operations = {
	.read = generic_read_dir,
	.readdir = yaffs_readdir,
	.fsync = yaffs_sync_object,
};

static struct super_operations yaffs_super_ops = {
	.statfs = yaffs_statfs,
	.put_super = yaffs_put_super,
	.evict_inode = yaffs_evict_inode,
	.sync_fs = yaffs_sync_fs,
	.write_super = yaffs_write_super,
	.remount_fs = yaffs_remount_fs,
};

static void yaffs_GrossLock(yaffs_Device * dev)
{
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs locking\n"));

	mutex_lock(&dev->gross_lock);
}

static void yaffs_GrossUnlock(yaffs_Device * dev)
{
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs unlocking\n"));
	mutex_unlock(&dev->gross_lock);

}

static void *yaffs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	unsigned char *alias;
	yaffs_Device *dev = yaffs_DentryToObject(dentry)->myDev;

	yaffs_GrossLock(dev);

	alias = yaffs_GetSymlinkAlias(yaffs_DentryToObject(dentry));

	yaffs_GrossUnlock(dev);

	if (!alias)
		return ERR_PTR(-ENOMEM);

	nd_set_link(nd, alias);
	return NULL;
}

static void yaffs_put_link(struct dentry *direntry,
			   struct nameidata *nd, void *cookie)
{
	char *alias = nd_get_link(nd);

	if (!IS_ERR(alias))
		kfree(alias);
}

struct inode *yaffs_get_inode(struct super_block *sb, umode_t mode, int dev,
			      yaffs_Object * obj);

/*
 * Lookup is used to find objects in the fs
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))

static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *n)
#else
static struct dentry *yaffs_lookup(struct inode *dir, struct dentry *dentry)
#endif
{
	yaffs_Object *obj;
	struct inode *inode = NULL;	/* NCB 2.5/2.6 needs NULL here */

	yaffs_Device *dev = yaffs_InodeToObject(dir)->myDev;

	yaffs_GrossLock(dev);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_lookup for %d:%s\n",
	   yaffs_InodeToObject(dir)->objectId, dentry->d_name.name));

	obj =
	    yaffs_FindObjectByName(yaffs_InodeToObject(dir),
				   dentry->d_name.name);

	obj = yaffs_GetEquivalentObject(obj);	/* in case it was a hardlink */
	
	/* Can't hold gross lock when calling yaffs_get_inode() */
	yaffs_GrossUnlock(dev);

	if (obj) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_lookup found %d\n", obj->objectId));

		inode = yaffs_get_inode(dir->i_sb, obj->yst_mode, 0, obj);

		if (inode) {
			T(YAFFS_TRACE_OS,
			  (KERN_DEBUG "yaffs_loookup dentry \n"));
/* #if 0 asserted by NCB for 2.5/6 compatability - falls through to
 * d_add even if NULL inode */
#if 0
			/*dget(dentry); // try to solve directory bug */
			d_add(dentry, inode);

			/* return dentry; */
			return NULL;
#endif
		}

	} else {
		T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_lookup not found\n"));

	}

/* added NCB for 2.5/6 compatability - forces add even if inode is
 * NULL which creates dentry hash */
	d_add(dentry, inode);

	return NULL;
	/*      return (ERR_PTR(-EIO)); */

}

/* clear is called to tell the fs to release any per-inode data it holds */
static void yaffs_clear_inode(struct inode *inode)
{
	yaffs_Object *obj;
	yaffs_Device *dev;

	obj = yaffs_InodeToObject(inode);

	T(YAFFS_TRACE_OS,
	  ("yaffs_clear_inode: ino %d, count %d %s\n", (int)inode->i_ino,
	   atomic_read(&inode->i_count),
	   obj ? "object exists" : "null object"));

	if (obj) {
		dev = obj->myDev;
		yaffs_GrossLock(dev);

		/* Clear the association between the inode and
		 * the yaffs_Object.
		 */
		obj->myInode = NULL;
		inode->i_private = NULL;

		/* If the object freeing was deferred, then the real
		 * free happens now.
		 * This should fix the inode inconsistency problem.
		 */

		yaffs_HandleDeferedFree(obj);

		yaffs_GrossUnlock(dev);
	}

}

/* delete is called when the link count is zero and the inode
 * is put (ie. nobody wants to know about it anymore, time to
 * delete the file).
 * NB Must call clear_inode()
 */
static void yaffs_evict_inode(struct inode *inode)
{
	yaffs_Object *obj = yaffs_InodeToObject(inode);
	yaffs_Device *dev;
        int deleteme = 0;

	T(YAFFS_TRACE_OS,
	  ("yaffs_delete_inode: ino %d, count %d %s\n", (int)inode->i_ino,
	   atomic_read(&inode->i_count),
	   obj ? "object exists" : "null object"));

        if (!inode->i_nlink && !is_bad_inode(inode))
                deleteme = 1;
        truncate_inode_pages (&inode->i_data, 0);
	end_writeback(inode);

	if (deleteme && obj) {
		dev = obj->myDev;
		yaffs_GrossLock(dev);
		yaffs_DeleteFile(obj);
		yaffs_GrossUnlock(dev);
	}

	yaffs_clear_inode(inode);
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
static int yaffs_file_flush(struct file *file, fl_owner_t id)
#else
static int yaffs_file_flush(struct file *file)
#endif
{
	yaffs_Object *obj = yaffs_DentryToObject(file->f_path.dentry);

	yaffs_Device *dev = obj->myDev;

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_file_flush object %d (%s)\n", obj->objectId,
	   obj->dirty ? "dirty" : "clean"));

	yaffs_GrossLock(dev);

	yaffs_FlushFile(obj, 1);

	yaffs_GrossUnlock(dev);

	return 0;
}

static int yaffs_readpage_nolock(struct file *f, struct page *pg)
{
	/* Lifted from jffs2 */

	yaffs_Object *obj;
	unsigned char *pg_buf;
	int ret;

	yaffs_Device *dev;

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_readpage at %08x, size %08x\n",
			   (unsigned)(pg->index << PAGE_CACHE_SHIFT),
			   (unsigned)PAGE_CACHE_SIZE));

	obj = yaffs_DentryToObject(f->f_path.dentry);

	dev = obj->myDev;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
	BUG_ON(!PageLocked(pg));
#else
	if (!PageLocked(pg))
		PAGE_BUG(pg);
#endif

	pg_buf = kmap(pg);
	/* FIXME: Can kmap fail? */

	yaffs_GrossLock(dev);

	ret =
	    yaffs_ReadDataFromFile(obj, pg_buf, pg->index << PAGE_CACHE_SHIFT,
				   PAGE_CACHE_SIZE);

	yaffs_GrossUnlock(dev);

	if (ret >= 0)
		ret = 0;

	if (ret) {
		ClearPageUptodate(pg);
		SetPageError(pg);
	} else {
		SetPageUptodate(pg);
		ClearPageError(pg);
	}

	flush_dcache_page(pg);
	kunmap(pg);

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_readpage done\n"));
	return ret;
}

static int yaffs_readpage_unlock(struct file *f, struct page *pg)
{
	int ret = yaffs_readpage_nolock(f, pg);
	UnlockPage(pg);
	return ret;
}

static int yaffs_readpage(struct file *f, struct page *pg)
{
	return yaffs_readpage_unlock(f, pg);
}

/* writepage inspired by/stolen from smbfs */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_writepage(struct page *page, struct writeback_control *wbc)
#else
static int yaffs_writepage(struct page *page)
#endif
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t) page->index << PAGE_CACHE_SHIFT;
	struct inode *inode;
	unsigned long end_index;
	char *buffer;
	yaffs_Object *obj;
	int nWritten = 0;
	unsigned nBytes;

	if (!mapping)
		BUG();
	inode = mapping->host;
	if (!inode)
		BUG();

	if (offset > inode->i_size) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG
		   "yaffs_writepage at %08x, inode size = %08x!!!\n",
		   (unsigned)(page->index << PAGE_CACHE_SHIFT),
		   (unsigned)inode->i_size));
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "                -> don't care!!\n"));
		unlock_page(page);
		return 0;
	}

	end_index = inode->i_size >> PAGE_CACHE_SHIFT;

	/* easy case */
	if (page->index < end_index) {
		nBytes = PAGE_CACHE_SIZE;
	} else {
		nBytes = inode->i_size & (PAGE_CACHE_SIZE - 1);
	}

	get_page(page);

	buffer = kmap(page);

	obj = yaffs_InodeToObject(inode);
	yaffs_GrossLock(obj->myDev);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_writepage at %08x, size %08x\n",
	   (unsigned)(page->index << PAGE_CACHE_SHIFT), nBytes));
	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "writepag0: obj = %05x, ino = %05x\n",
	   (int)obj->variant.fileVariant.fileSize, (int)inode->i_size));

	nWritten =
	    yaffs_WriteDataToFile(obj, buffer, page->index << PAGE_CACHE_SHIFT,
				  nBytes, 0);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "writepag1: obj = %05x, ino = %05x\n",
	   (int)obj->variant.fileVariant.fileSize, (int)inode->i_size));

	yaffs_GrossUnlock(obj->myDev);

	kunmap(page);
	SetPageUptodate(page);
	UnlockPage(page);
	put_page(page);

	return (nWritten == nBytes) ? 0 : -ENOSPC;
}

static int yaffs_write_begin(struct file *f, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
	unsigned to = offset + len;
	struct inode *inode = mapping->host;
	int ret = 0;

	*pagep = grab_cache_page_write_begin(mapping, index, flags);
	if (!*pagep)
		return -ENOMEM;

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_prepair_write\n"));
	if (!Page_Uptodate(*pagep) && (offset || to < PAGE_CACHE_SIZE)) {
		ret = yaffs_readpage_nolock(f, *pagep);
		if (ret)
			goto err;
	}

	return 0;
  err:
	unlock_page(*pagep);
	page_cache_release(*pagep);
	if (pos + len > inode->i_size)
		vmtruncate(inode, inode->i_size);
	return ret;

}

static int yaffs_write_end(struct file *f, struct address_space *mapping,
			   loff_t pos, unsigned nBytes, unsigned copied,
			   struct page *pg, void *fsdata)
{
	unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
	void *addr = page_address(pg) + offset;
	int nWritten;

	unsigned spos = pos;
	unsigned saddr = (unsigned)addr;

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_commit_write addr %x pos %x nBytes %d\n", saddr,
	   spos, nBytes));

	flush_dcache_page(pg);
	nWritten = yaffs_file_write(f, addr, nBytes, &pos);

	if (nWritten != nBytes) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG
		   "yaffs_commit_write not same size nWritten %d  nBytes %d\n",
		   nWritten, nBytes));
		SetPageError(pg);
		ClearPageUptodate(pg);
	} else {
		SetPageUptodate(pg);
	}

	unlock_page(pg);
	page_cache_release(pg);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_commit_write returning %d\n",
	   nWritten == nBytes ? 0 : nWritten));

	return nWritten;

}

static void yaffs_FillInodeFromObject(struct inode *inode, yaffs_Object * obj)
{
	if (inode && obj) {


		/* Check mode against the variant type and attempt to repair if broken. */
 		__u32 mode = obj->yst_mode;
 		switch( obj->variantType ){
 		case YAFFS_OBJECT_TYPE_FILE :
 		        if( ! S_ISREG(mode) ){
 			        obj->yst_mode &= ~S_IFMT;
 			        obj->yst_mode |= S_IFREG;
 			}
 
 			break;
 		case YAFFS_OBJECT_TYPE_SYMLINK :
 		        if( ! S_ISLNK(mode) ){
 			        obj->yst_mode &= ~S_IFMT;
 				obj->yst_mode |= S_IFLNK;
 			}
 
 			break;
 		case YAFFS_OBJECT_TYPE_DIRECTORY :
 		        if( ! S_ISDIR(mode) ){
 			        obj->yst_mode &= ~S_IFMT;
 			        obj->yst_mode |= S_IFDIR;
 			}
 
 			break;
 		case YAFFS_OBJECT_TYPE_UNKNOWN :
 		case YAFFS_OBJECT_TYPE_HARDLINK :
 		case YAFFS_OBJECT_TYPE_SPECIAL :
 		default:
 		        /* TODO? */
 		        break;
 		}

		inode->i_ino = obj->objectId;
		inode->i_mode = obj->yst_mode;
		inode->i_uid = obj->yst_uid;
		inode->i_gid = obj->yst_gid;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))

		inode->i_rdev = old_decode_dev(obj->yst_rdev);
		inode->i_atime.tv_sec = (time_t) (obj->yst_atime);
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime.tv_sec = (time_t) obj->yst_mtime;
		inode->i_mtime.tv_nsec = 0;
		inode->i_ctime.tv_sec = (time_t) obj->yst_ctime;
		inode->i_ctime.tv_nsec = 0;
#else
		inode->i_rdev = obj->yst_rdev;
		inode->i_atime = obj->yst_atime;
		inode->i_mtime = obj->yst_mtime;
		inode->i_ctime = obj->yst_ctime;
#endif
		inode->i_size = yaffs_GetObjectFileLength(obj);
		inode->i_blocks = (inode->i_size + 511) >> 9;

		set_nlink(inode, yaffs_GetObjectLinkCount(obj));

		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG
		   "yaffs_FillInode mode %x uid %d gid %d size %d count %d\n",
		   inode->i_mode, inode->i_uid, inode->i_gid,
		   (int)inode->i_size, atomic_read(&inode->i_count)));

		switch (obj->yst_mode & S_IFMT) {
		default:	/* fifo, device or socket */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
			init_special_inode(inode, obj->yst_mode,
					   old_decode_dev(obj->yst_rdev));
#else
			init_special_inode(inode, obj->yst_mode,
					   (dev_t) (obj->yst_rdev));
#endif
			break;
		case S_IFREG:	/* file */
			inode->i_op = &yaffs_file_inode_operations;
			inode->i_fop = &yaffs_file_operations;
			inode->i_mapping->a_ops =
			    &yaffs_file_address_operations;
			break;
		case S_IFDIR:	/* directory */
			inode->i_op = &yaffs_dir_inode_operations;
			inode->i_fop = &yaffs_dir_operations;
			break;
		case S_IFLNK:	/* symlink */
			inode->i_op = &yaffs_symlink_inode_operations;
			break;
		}

		inode->i_private = obj;
		obj->myInode = inode;

	} else {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_FileInode invalid parameters\n"));
	}

}

struct inode *yaffs_get_inode(struct super_block *sb, umode_t mode, int dev,
			      yaffs_Object * obj)
{
	struct inode *inode;

	if (!sb) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_get_inode for NULL super_block!!\n"));
		return NULL;

	}

	if (!obj) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_get_inode for NULL object!!\n"));
		return NULL;

	}

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_get_inode for object %d\n", obj->objectId));

	inode = yaffs_iget(sb, obj->objectId);

	/* NB Side effect: iget calls back to yaffs_read_inode(). */
	/* iget also increments the inode's i_count */
	/* NB You can't be holding grossLock or deadlock will happen! */

	return !IS_ERR(inode) ? inode : NULL;
}

static ssize_t yaffs_file_write(struct file *f, const char *buf, size_t n,
				loff_t * pos)
{
	yaffs_Object *obj;
	int nWritten, ipos;
	struct inode *inode;
	yaffs_Device *dev;

	obj = yaffs_DentryToObject(f->f_path.dentry);

	dev = obj->myDev;

	yaffs_GrossLock(dev);

	inode = f->f_path.dentry->d_inode;

	if (!S_ISBLK(inode->i_mode) && f->f_flags & O_APPEND) {
		ipos = inode->i_size;
	} else {
		ipos = *pos;
	}

	if (!obj) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_file_write: hey obj is null!\n"));
	} else {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG
		   "yaffs_file_write about to write writing %d bytes"
		   "to object %d at %d\n",
		   n, obj->objectId, ipos));
	}

	nWritten = yaffs_WriteDataToFile(obj, buf, ipos, n, 0);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_file_write writing %d bytes, %d written at %d\n",
	   n, nWritten, ipos));
	if (nWritten > 0) {
		ipos += nWritten;
		*pos = ipos;
		if (ipos > inode->i_size) {
			inode->i_size = ipos;
			inode->i_blocks = (ipos + 511) >> 9;

			T(YAFFS_TRACE_OS,
			  (KERN_DEBUG
			   "yaffs_file_write size updated to %d bytes, "
			   "%d blocks\n",
			   ipos, (int)(inode->i_blocks)));
		}

	}
	yaffs_GrossUnlock(dev);
	return nWritten == 0 ? -ENOSPC : nWritten;
}

static int yaffs_readdir(struct file *f, void *dirent, filldir_t filldir)
{
	yaffs_Object *obj;
	yaffs_Device *dev;
	struct inode *inode = f->f_path.dentry->d_inode;
	unsigned long offset, curoffs;
	struct list_head *i;
	yaffs_Object *l;

	char name[YAFFS_MAX_NAME_LENGTH + 1];

	obj = yaffs_DentryToObject(f->f_path.dentry);
	dev = obj->myDev;

	yaffs_GrossLock(dev);

	offset = f->f_pos;

	T(YAFFS_TRACE_OS, ("yaffs_readdir: starting at %d\n", (int)offset));

	if (offset == 0) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_readdir: entry . ino %d \n",
		   (int)inode->i_ino));
		if (filldir(dirent, ".", 1, offset, inode->i_ino, DT_DIR)
		    < 0) {
			goto out;
		}
		offset++;
		f->f_pos++;
	}
	if (offset == 1) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_readdir: entry .. ino %d \n",
		   (int) parent_ino(f->f_path.dentry)));
		if (filldir
		    (dirent, "..", 2, offset,
		     parent_ino(f->f_path.dentry), DT_DIR) < 0) {
			goto out;
		}
		offset++;
		f->f_pos++;
	}

	curoffs = 1;

#if 0	/* it makes possible to iterate over the same file twice - BAD! */
	/* If the directory has changed since the open or last call to
	   readdir, rewind to after the 2 canned entries. */

	if (f->f_version != inode->i_version) {
		offset = 2;
		f->f_pos = offset;
		f->f_version = inode->i_version;
	}
#endif

	list_for_each(i, &obj->variant.directoryVariant.children) {
		curoffs++;
		if (curoffs >= offset) {
			l = list_entry(i, yaffs_Object, siblings);

			yaffs_GetObjectName(l, name,
					    YAFFS_MAX_NAME_LENGTH + 1);
			T(YAFFS_TRACE_OS,
			  (KERN_DEBUG "yaffs_readdir: %s inode %d\n", name,
			   yaffs_GetObjectInode(l)));

			if (filldir(dirent,
				    name,
				    strlen(name),
				    offset,
				    yaffs_GetObjectInode(l),
				    yaffs_GetObjectType(l))
			    < 0) {
				goto up_and_out;
			}

			offset++;
			f->f_pos++;
		}
	}

      up_and_out:
      out:

	yaffs_GrossUnlock(dev);

	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
		       dev_t rdev)
#else
static int yaffs_mknod(struct inode *dir, struct dentry *dentry, int mode,
		       int rdev)
#endif
{
	struct inode *inode;

	yaffs_Object *obj = NULL;
	yaffs_Device *dev;

	yaffs_Object *parent = yaffs_InodeToObject(dir);

	int error = -ENOSPC;
	uid_t uid = current_fsuid();
	gid_t gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();
	
	if((dir->i_mode & S_ISGID) && S_ISDIR(mode))
		mode |= S_ISGID;

	if (parent) {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_mknod: parent object %d type %d\n",
		   parent->objectId, parent->variantType));
	} else {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_mknod: could not get parent object\n"));
		return -EPERM;
	}

	T(YAFFS_TRACE_OS, ("yaffs_mknod: making oject for %s, "
			   "mode %x dev %x\n",
			   dentry->d_name.name, mode, rdev));

	dev = parent->myDev;

	yaffs_GrossLock(dev);

	switch (mode & S_IFMT) {
	default:
		/* Special (socket, fifo, device...) */
		T(YAFFS_TRACE_OS, (KERN_DEBUG
				   "yaffs_mknod: making special\n"));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
		obj =
		    yaffs_MknodSpecial(parent, dentry->d_name.name, mode, uid,
				       gid, old_encode_dev(rdev));
#else
		obj =
		    yaffs_MknodSpecial(parent, dentry->d_name.name, mode, uid,
				       gid, rdev);
#endif
		break;
	case S_IFREG:		/* file          */
		T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_mknod: making file\n"));
		obj =
		    yaffs_MknodFile(parent, dentry->d_name.name, mode, uid,
				    gid);
		break;
	case S_IFDIR:		/* directory */
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_mknod: making directory\n"));
		obj =
		    yaffs_MknodDirectory(parent, dentry->d_name.name, mode,
					 uid, gid);
		break;
	case S_IFLNK:		/* symlink */
		T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_mknod: making file\n"));
		obj = NULL;	/* Do we ever get here? */
		break;
	}
	
	/* Can not call yaffs_get_inode() with gross lock held */
	yaffs_GrossUnlock(dev);

	if (obj) {
		inode = yaffs_get_inode(dir->i_sb, mode, rdev, obj);
		d_instantiate(dentry, inode);
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_mknod created object %d count = %d\n",
		   obj->objectId, atomic_read(&inode->i_count)));
		error = 0;
	} else {
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_mknod failed making object\n"));
		error = -ENOMEM;
	}

	return error;
}

static int yaffs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int retVal;
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_mkdir\n"));
	retVal = yaffs_mknod(dir, dentry, mode | S_IFDIR, 0);
#if 0
	/* attempt to fix dir bug - didn't work */
	if (!retVal) {
		dget(dentry);
	}
#endif
	return retVal;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			struct nameidata *n)
#else
static int yaffs_create(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_create\n"));
	return yaffs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int yaffs_unlink(struct inode *dir, struct dentry *dentry)
{
	int retVal;

	yaffs_Device *dev;

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_unlink %d:%s\n", (int)(dir->i_ino),
	   dentry->d_name.name));

	dev = yaffs_InodeToObject(dir)->myDev;

	yaffs_GrossLock(dev);

	retVal = yaffs_Unlink(yaffs_InodeToObject(dir), dentry->d_name.name);

	if (retVal == YAFFS_OK) {
		inode_dec_link_count(dentry->d_inode);
		dir->i_version++;
		yaffs_GrossUnlock(dev);
		mark_inode_dirty(dentry->d_inode);
		return 0;
	}
	yaffs_GrossUnlock(dev);
	return -ENOTEMPTY;
}

/*
 * Create a link...
 */
static int yaffs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	yaffs_Object *obj = NULL;
	yaffs_Object *link = NULL;
	yaffs_Device *dev;

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_link\n"));

	obj = yaffs_InodeToObject(inode);
	dev = obj->myDev;

	yaffs_GrossLock(dev);

	if (!S_ISDIR(inode->i_mode))	/* Don't link directories */
	{
		link =
		    yaffs_Link(yaffs_InodeToObject(dir), dentry->d_name.name,
			       obj);
	}

	if (link) {
		set_nlink(old_dentry->d_inode, yaffs_GetObjectLinkCount(obj));
		d_instantiate(dentry, old_dentry->d_inode);
		atomic_inc(&old_dentry->d_inode->i_count);
		T(YAFFS_TRACE_OS,
		  (KERN_DEBUG "yaffs_link link count %d i_count %d\n",
		   old_dentry->d_inode->i_nlink,
		   atomic_read(&old_dentry->d_inode->i_count)));

	}

	yaffs_GrossUnlock(dev);

	if (link) {

		return 0;
	}

	return -EPERM;
}

static int yaffs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	yaffs_Object *obj;
	yaffs_Device *dev;
	uid_t uid = current_fsuid();
	gid_t gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_symlink\n"));

	dev = yaffs_InodeToObject(dir)->myDev;
	yaffs_GrossLock(dev);
	obj = yaffs_MknodSymLink(yaffs_InodeToObject(dir), dentry->d_name.name,
				 S_IFLNK | S_IRWXUGO, uid, gid, symname);
	yaffs_GrossUnlock(dev);

	if (obj) {

		struct inode *inode;

		inode = yaffs_get_inode(dir->i_sb, obj->yst_mode, 0, obj);
		d_instantiate(dentry, inode);
		T(YAFFS_TRACE_OS, (KERN_DEBUG "symlink created OK\n"));
		return 0;
	} else {
		T(YAFFS_TRACE_OS, (KERN_DEBUG "symlink not created\n"));

	}

	return -ENOMEM;
}

static int yaffs_sync_object(struct file *file, loff_t start, loff_t end,
			     int datasync)
{

 	struct inode *inode = file->f_mapping->host;
	yaffs_Object *obj;
	yaffs_Device *dev;
	int err;

	err = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (err)
		return err;

	obj = yaffs_InodeToObject(inode);

	dev = obj->myDev;

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_sync_object\n"));
	yaffs_GrossLock(dev);
	yaffs_FlushFile(obj, 1);
	yaffs_GrossUnlock(dev);
	return 0;
}

#define	YAFFS_IOC_REFRESH		_IO('f', 777)

static long yaffs_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg) {
	struct inode *inode = filp->f_dentry->d_inode;
	yaffs_Device *dev = yaffs_InodeToObject(inode)->myDev;
	struct mtd_info *mtd = dev->genericDevice;
	long ret;

	if (cmd != YAFFS_IOC_REFRESH) {
		return -ENOTTY;
	}
	if (mtd->type != MTD_NANDFLASH) {
		return -ENODEV;
	}
#if defined(MIPSEL) && !defined(CONFIG_SMP)
	if (!is_nand_bad()) {
		return -ENOENT;
	}
#endif

	yaffs_GrossLock(dev);

	ret = yaffs_RefreshOneBlock(dev);

	yaffs_GrossUnlock(dev);
	return ret;
}

/*
 * The VFS layer already does all the dentry stuff for rename.
 *
 * NB: POSIX says you can rename an object over an old object of the same name
 */
static int yaffs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	yaffs_Device *dev;
	int retVal = YAFFS_FAIL;
	yaffs_Object *target;

        T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_rename\n"));
	dev = yaffs_InodeToObject(old_dir)->myDev;

	yaffs_GrossLock(dev);

	/* Check if the target is an existing directory that is not empty. */
	target =
	    yaffs_FindObjectByName(yaffs_InodeToObject(new_dir),
				   new_dentry->d_name.name);
	
	

	if (target &&
	    target->variantType == YAFFS_OBJECT_TYPE_DIRECTORY &&
	    !list_empty(&target->variant.directoryVariant.children)) {
	    
	        T(YAFFS_TRACE_OS, (KERN_DEBUG "target is non-empty dir\n"));

		retVal = YAFFS_FAIL;
	} else {

		/* Now does unlinking internally using shadowing mechanism */
	        T(YAFFS_TRACE_OS, (KERN_DEBUG "calling yaffs_RenameObject\n"));
		
		retVal =
		    yaffs_RenameObject(yaffs_InodeToObject(old_dir),
				       old_dentry->d_name.name,
				       yaffs_InodeToObject(new_dir),
				       new_dentry->d_name.name);

	}
	yaffs_GrossUnlock(dev);

	if (retVal == YAFFS_OK) {
		if(target) {
			inode_dec_link_count(new_dentry->d_inode);
			mark_inode_dirty(new_dentry->d_inode);
		}

		return 0;
	} else {
		return -ENOTEMPTY;
	}

}

static int yaffs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	yaffs_Device *dev;

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_setattr of object %d\n",
	   yaffs_InodeToObject(inode)->objectId));

	if ((error = inode_change_ok(inode, attr)) == 0) {

		dev = yaffs_InodeToObject(inode)->myDev;
		yaffs_GrossLock(dev);
		if (yaffs_SetAttributes(yaffs_InodeToObject(inode), attr) ==
		    YAFFS_OK) {
			error = 0;
		} else {
			error = -EPERM;
		}
		yaffs_GrossUnlock(dev);
		if (!error) {
			if ((attr->ia_valid & ATTR_SIZE) &&
			    attr->ia_size != i_size_read(inode)) {
				error = vmtruncate(inode, attr->ia_size);
				if (error)
				    return error;
			}
			setattr_copy(inode, attr);
			mark_inode_dirty(inode);
		}
	}
	return error;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
static int yaffs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	yaffs_Device *dev = yaffs_DentryToObject(dentry)->myDev;
	struct super_block *sb = dentry->d_sb;
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);
#else
static int yaffs_statfs(struct super_block *sb, struct statfs *buf)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);
#endif

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_statfs\n"));

	yaffs_GrossLock(dev);

	buf->f_type = YAFFS_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_namelen = 255;
	if (sb->s_blocksize > dev->nBytesPerChunk) {

		buf->f_blocks =
		    (dev->endBlock - dev->startBlock +
		     1) * dev->nChunksPerBlock / (sb->s_blocksize /
						  dev->nBytesPerChunk);
		buf->f_bfree =
		    yaffs_GetNumberOfFreeChunks(dev) / (sb->s_blocksize /
							dev->nBytesPerChunk);
	} else {

		buf->f_blocks =
		    (dev->endBlock - dev->startBlock +
		     1) * dev->nChunksPerBlock * (dev->nBytesPerChunk /
						  sb->s_blocksize);
		buf->f_bfree =
		    yaffs_GetNumberOfFreeChunks(dev) * (dev->nBytesPerChunk /
							sb->s_blocksize);
	}
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_bavail = buf->f_bfree;

	yaffs_GrossUnlock(dev);
	return 0;
}



static int yaffs_do_sync_fs(struct super_block *sb, int save_cp)
{

	yaffs_Device *dev = yaffs_SuperToDevice(sb);
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_do_sync_fs %d\n", save_cp));

	if(dev) {
		yaffs_GrossLock(dev);

		yaffs_FlushEntireDeviceCache(dev);
		if (save_cp && !cp_disabled) yaffs_CheckpointSave(dev);
		
		yaffs_GrossUnlock(dev);

		sb->s_dirt = 0;
	}
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
static void yaffs_write_super(struct super_block *sb)
#else
static int yaffs_write_super(struct super_block *sb)
#endif
{

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_write_super\n"));
	yaffs_do_sync_fs(sb, 0);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17))
	return 0;
#endif
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
static int yaffs_sync_fs(struct super_block *sb, int wait)
#else
static int yaffs_sync_fs(struct super_block *sb)
#endif
{

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_sync_fs\n"));
	
	yaffs_do_sync_fs(sb, 0);
	return 0;
}


static struct inode *yaffs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	yaffs_Object *obj;
	yaffs_Device *dev = yaffs_SuperToDevice(sb);

	T(YAFFS_TRACE_OS,
	  (KERN_DEBUG "yaffs_iget for %lu\n", ino));

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	/* NB This is called as a side effect of other functions, but
	 * we had to release the lock to prevent deadlocks, so 
	 * need to lock again.
	 */

	yaffs_GrossLock(dev);
	
	obj = yaffs_FindObjectByNumber(dev, inode->i_ino);

	yaffs_FillInodeFromObject(inode, obj);

	yaffs_GrossUnlock(dev);

	unlock_new_inode(inode);
	return inode;
}

static LIST_HEAD(yaffs_dev_list);
static struct mutex yaffs_context_lock;

static void yaffs_put_super(struct super_block *sb)
{
	yaffs_Device *dev = yaffs_SuperToDevice(sb);

	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_put_super\n"));

	yaffs_GrossLock(dev);
	
	yaffs_FlushEntireDeviceCache(dev);
	
	if (dev->putSuperFunc) {
		dev->putSuperFunc(sb);
	}
	
	if (!cp_disabled) yaffs_CheckpointSave(dev);
	yaffs_Deinitialise(dev);
	
	yaffs_GrossUnlock(dev);

	mutex_lock(&yaffs_context_lock);
	list_del(&dev->devList);
	mutex_unlock(&yaffs_context_lock);
	
	if(dev->spareBuffer){
		YFREE(dev->spareBuffer);
		dev->spareBuffer = NULL;
	}

	kfree(dev);
}

static int yaffs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	T(YAFFS_TRACE_OS, (KERN_INFO "yaffs_remount_fs\n"));

	if ((*flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY)) {
		yaffs_do_sync_fs(sb, 1);
	}
	return 0;
}


static void yaffs_MTDPutSuper(struct super_block *sb)
{

	struct mtd_info *mtd = yaffs_SuperToDevice(sb)->genericDevice;

	if (mtd->sync) {
		mtd->sync(mtd);
	}

	put_mtd_device(mtd);
}


static void yaffs_MarkSuperBlockDirty(void *vsb)
{
	struct super_block *sb = (struct super_block *)vsb;
	
	T(YAFFS_TRACE_OS, (KERN_DEBUG "yaffs_MarkSuperBlockDirty() sb = %p\n",sb));
//	if(sb)
//		sb->s_dirt = 1;
}

static struct super_block *yaffs_internal_read_super(int yaffsVersion,
						     struct super_block *sb,
						     void *data, int silent)
{
	int nBlocks;
	struct inode *inode = NULL;
	struct dentry *root;
	yaffs_Device *dev = 0;
	char devname_buf[BDEVNAME_SIZE + 1];
	struct mtd_info *mtd;
	int err;

	sb->s_magic = YAFFS_MAGIC;
	sb->s_op = &yaffs_super_ops;

	if (!sb)
		printk(KERN_INFO "yaffs: sb is NULL\n");
	else if (!sb->s_dev)
		printk(KERN_INFO "yaffs: sb->s_dev is NULL\n");
	else
		printk(KERN_INFO "yaffs: dev is %d name is \"%s\"\n",
		       sb->s_dev,
		       yaffs_devname(sb, devname_buf));

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	T(YAFFS_TRACE_OS, ("yaffs_read_super: Using yaffs%d\n", yaffsVersion));
	T(YAFFS_TRACE_OS,
	  ("yaffs_read_super: block size %d\n", (int)(sb->s_blocksize)));

#ifdef CONFIG_YAFFS_DISABLE_WRITE_VERIFY
	T(YAFFS_TRACE_OS,
	  ("yaffs: Write verification disabled. All guarantees "
	   "null and void\n"));
#endif

	T(YAFFS_TRACE_ALWAYS, ("yaffs: Attempting MTD mount on %u.%u, "
			       "\"%s\"\n",
			       MAJOR(sb->s_dev), MINOR(sb->s_dev),
			       yaffs_devname(sb, devname_buf)));

	/* Check it's an mtd device..... */
	if (MAJOR(sb->s_dev) != MTD_BLOCK_MAJOR) {
		return NULL;	/* This isn't an mtd device */
	}
	/* Get the device */
	mtd = get_mtd_device(NULL, MINOR(sb->s_dev));
	if (!mtd) {
		T(YAFFS_TRACE_ALWAYS,
		  ("yaffs: MTD device #%u doesn't appear to exist\n",
		   MINOR(sb->s_dev)));
		return NULL;
	}
	/* Check it's NAND */
	if (mtd->type != MTD_NANDFLASH && mtd->type != MTD_NORFLASH) {
		T(YAFFS_TRACE_ALWAYS,
		  ("yaffs: MTD device is not NAND it's type %d\n", mtd->type));
		return NULL;
	}

	T(YAFFS_TRACE_OS, (" erase %p\n", mtd->erase));
	T(YAFFS_TRACE_OS, (" read %p\n", mtd->read));
	T(YAFFS_TRACE_OS, (" write %p\n", mtd->write));
	T(YAFFS_TRACE_OS, (" readoob %p\n", mtd->read_oob));
	T(YAFFS_TRACE_OS, (" writeoob %p\n", mtd->write_oob));
	T(YAFFS_TRACE_OS, (" block_isbad %p\n", mtd->block_isbad));
	T(YAFFS_TRACE_OS, (" block_markbad %p\n", mtd->block_markbad));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	T(YAFFS_TRACE_OS, (" writesize %d\n", mtd->writesize));
#else
	T(YAFFS_TRACE_OS, (" oobblock %d\n", mtd->oobblock));
#endif
	T(YAFFS_TRACE_OS, (" oobsize %d\n", mtd->oobsize));
	T(YAFFS_TRACE_OS, (" erasesize %d\n", mtd->erasesize));
	T(YAFFS_TRACE_OS, (" size %lld\n", mtd->size));

        if (mtd->type == MTD_NORFLASH) {
                yaffsVersion = 2;
        }
	
#ifdef CONFIG_YAFFS_AUTO_YAFFS2

	if (yaffsVersion == 1 && 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	    mtd->writesize >= 2048) {
#else
	    mtd->oobblock >= 2048) {
#endif
	    T(YAFFS_TRACE_ALWAYS,("yaffs: auto selecting yaffs2\n"));
	    yaffsVersion = 2;
	}	
	
	/* Added NCB 26/5/2006 for completeness */
	if (yaffsVersion == 2 && 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	    mtd->writesize == 512) {
#else
	    mtd->oobblock == 512) {
#endif
	    T(YAFFS_TRACE_ALWAYS,("yaffs: auto selecting yaffs1\n"));
	    yaffsVersion = 1;
	}	

#endif

	if (yaffsVersion == 2 && mtd->type == MTD_NANDFLASH) {
		/* Check for version 2 style functions */
		if (!mtd->erase ||
		    !mtd->block_isbad ||
		    !mtd->block_markbad ||
		    !mtd->read ||
		    !mtd->write ||
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
		    !mtd->read_oob || !mtd->write_oob) {
#else
		    !mtd->write_ecc ||
		    !mtd->read_ecc || !mtd->read_oob || !mtd->write_oob) {
#endif
			T(YAFFS_TRACE_ALWAYS,
			  ("yaffs: MTD device does not support required "
			   "functions\n"));;
			return NULL;
		}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
		if (mtd->writesize < YAFFS_MIN_YAFFS2_CHUNK_SIZE ||
#else
		if (mtd->oobblock < YAFFS_MIN_YAFFS2_CHUNK_SIZE ||
#endif
		    mtd->oobsize < YAFFS_MIN_YAFFS2_SPARE_SIZE) {
			T(YAFFS_TRACE_ALWAYS,
			  ("yaffs: MTD device does not have the "
			   "right page sizes\n"));
			return NULL;
		}
        } else if (yaffsVersion == 2 && mtd->type == MTD_NORFLASH) {
		/* Check for version 2 style functions */
		if (!mtd->erase ||
		    !mtd->read ||
		    !mtd->write) {
			T(YAFFS_TRACE_ALWAYS,
			  ("yaffs: MTD device does not support required "
			   "functions\n"));;
			return NULL;
		}
	} else {
		/* Check for V1 style functions */
		if (!mtd->erase ||
		    !mtd->read ||
		    !mtd->write ||
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
		    !mtd->read_oob || !mtd->write_oob) {
#else
		    !mtd->write_ecc ||
		    !mtd->read_ecc || !mtd->read_oob || !mtd->write_oob) {
#endif
			T(YAFFS_TRACE_ALWAYS,
			  ("yaffs: MTD device does not support required "
			   "functions\n"));;
			return NULL;
		}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
		if (mtd->writesize < YAFFS_BYTES_PER_CHUNK ||
#else
		if (mtd->oobblock < YAFFS_BYTES_PER_CHUNK ||
#endif
		    mtd->oobsize != YAFFS_BYTES_PER_SPARE) {
			T(YAFFS_TRACE_ALWAYS,
			  ("yaffs: MTD device does not support have the "
			   "right page sizes\n"));
			return NULL;
		}
	}

	/* OK, so if we got here, we have an MTD that's NAND and looks
	 * like it has the right capabilities
	 * Set the yaffs_Device up for mtd
	 */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
	sb->s_fs_info = dev = kmalloc(sizeof(yaffs_Device), GFP_KERNEL);
#else
	sb->u.generic_sbp = dev = kmalloc(sizeof(yaffs_Device), GFP_KERNEL);
#endif
	if (!dev) {
		/* Deep shit could not allocate device structure */
		T(YAFFS_TRACE_ALWAYS,
		  ("yaffs_read_super: Failed trying to allocate "
		   "yaffs_Device. \n"));
		return NULL;
	}

	memset(dev, 0, sizeof(yaffs_Device));
	dev->genericDevice = mtd;
	dev->name = mtd->name;

	/* Set up the memory size parameters.... */

	nBlocks = mtd->size / (YAFFS_CHUNKS_PER_BLOCK * YAFFS_BYTES_PER_CHUNK);
	dev->startBlock = 0;
	dev->endBlock = nBlocks - 1;
	dev->nChunksPerBlock = YAFFS_CHUNKS_PER_BLOCK;
	dev->nBytesPerChunk = YAFFS_BYTES_PER_CHUNK;
	dev->nReservedBlocks = 5;
	dev->nShortOpCaches = 10;	/* Enable short op caching */

	/* ... and the functions. */
	if (yaffsVersion == 2 && mtd->type == MTD_NANDFLASH) {
		dev->writeChunkWithTagsToNAND =
		    nandmtd2_WriteChunkWithTagsToNAND;
		dev->readChunkWithTagsFromNAND =
		    nandmtd2_ReadChunkWithTagsFromNAND;
		dev->markNANDBlockBad = nandmtd2_MarkNANDBlockBad;
		dev->queryNANDBlock = nandmtd2_QueryNANDBlock;
                dev->eraseBlockInNAND = nandmtd_EraseBlockInNAND;
                dev->initialiseNAND = nandmtd_InitialiseNAND;
		dev->spareBuffer = YMALLOC(mtd->oobsize);
		dev->isYaffs2 = 1;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
		dev->nBytesPerChunk = mtd->writesize;
		dev->nChunksPerBlock = mtd->erasesize / mtd->writesize;
#else
		dev->nBytesPerChunk = mtd->oobblock;
		dev->nChunksPerBlock = mtd->erasesize / mtd->oobblock;
#endif
		nBlocks = (unsigned) mtd->size / mtd->erasesize;

		dev->nCheckpointReservedBlocks = 10;
		dev->nReservedBlocks = 2;
		if (nBlocks < (dev->nReservedBlocks + 1 + 
			       dev->nCheckpointReservedBlocks) * 10) {
			dev->nCheckpointReservedBlocks = 0;
			dev->nReservedBlocks = max(nBlocks / 10, 2);
		}
		dev->startBlock = 0;
		dev->endBlock = nBlocks - 1;
	} else if (yaffsVersion == 2 && mtd->type == MTD_NORFLASH) {
		dev->writeChunkWithTagsToNAND =
		    normtd2_WriteChunkWithTagsToNAND;
		dev->readChunkWithTagsFromNAND =
		    normtd2_ReadChunkWithTagsFromNAND;
		dev->markNANDBlockBad = normtd2_MarkNANDBlockBad;
		dev->queryNANDBlock = normtd2_QueryNANDBlock;
                dev->eraseBlockInNAND = normtd_EraseBlockInNAND;
                dev->initialiseNAND = normtd_InitialiseNAND;

		dev->isYaffs2 = 1;
		dev->nBytesPerChunk = 1024;
		dev->nChunksPerBlock = MTD_NOR_ERASESIZE / (dev->nBytesPerChunk + 16);
                nBlocks = (uint32_t) mtd->size / MTD_NOR_ERASESIZE;
		dev->spareBuffer = YMALLOC(16);

		dev->nCheckpointReservedBlocks = 0;
		dev->nReservedBlocks = 2;
		dev->startBlock = 0;
		dev->endBlock = nBlocks - 1;
		cp_disabled = 1;
	} else {
		dev->writeChunkToNAND = nandmtd_WriteChunkToNAND;
		dev->readChunkFromNAND = nandmtd_ReadChunkFromNAND;
                dev->eraseBlockInNAND = nandmtd_EraseBlockInNAND;
                dev->initialiseNAND = nandmtd_InitialiseNAND;
		dev->isYaffs2 = 0;
	}
	/* ... and common functions */

	dev->putSuperFunc = yaffs_MTDPutSuper;
	
	dev->superBlock = (void *)sb;
	dev->markSuperBlockDirty = yaffs_MarkSuperBlockDirty;
	

#ifndef CONFIG_YAFFS_DOES_ECC
	dev->useNANDECC = 1;
#endif

#ifdef CONFIG_YAFFS_DISABLE_WIDE_TNODES
	dev->wideTnodesDisabled = 1;
#endif

	mutex_lock(&yaffs_context_lock);
	list_add_tail(&dev->devList, &yaffs_dev_list);
	mutex_unlock(&yaffs_context_lock);

	mutex_init(&dev->gross_lock);

	yaffs_GrossLock(dev);

	err = yaffs_GutsInitialise(dev);

	T(YAFFS_TRACE_OS,
	  ("yaffs_read_super: guts initialised %s\n",
	   (err == YAFFS_OK) ? "OK" : "FAILED"));
	
	/* Release lock before yaffs_get_inode() */
	yaffs_GrossUnlock(dev);

	/* Create root inode */
	if (err == YAFFS_OK)
		inode = yaffs_get_inode(sb, S_IFDIR | 0755, 0,
					yaffs_Root(dev));

	if (!inode)
		return NULL;

	inode->i_op = &yaffs_dir_inode_operations;
	inode->i_fop = &yaffs_dir_operations;

	T(YAFFS_TRACE_OS, ("yaffs_read_super: got root inode\n"));

	root = d_alloc_root(inode);

	T(YAFFS_TRACE_OS, ("yaffs_read_super: d_alloc_root done\n"));

	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;

	T(YAFFS_TRACE_OS, ("yaffs_read_super: done\n"));
	return sb;
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs_internal_read_super_mtd(struct super_block *sb, void *data,
					 int silent)
{
	return yaffs_internal_read_super(1, sb, data, silent) ? 0 : -EINVAL;
}

static struct dentry *yaffs_mount(struct file_system_type *fs,
				  int flags, const char *dev_name,
				  void *data)
{

	return mount_bdev(fs, flags, dev_name, data,
			  yaffs_internal_read_super_mtd);
}

static struct file_system_type yaffs_fs_type = {
	.owner = THIS_MODULE,
	.name = "yaffs",
	.mount = yaffs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
#else
static struct super_block *yaffs_read_super(struct super_block *sb, void *data,
					    int silent)
{
	return yaffs_internal_read_super(1, sb, data, silent);
}

static DECLARE_FSTYPE(yaffs_fs_type, "yaffs", yaffs_read_super,
		      FS_REQUIRES_DEV);
#endif


#ifdef CONFIG_YAFFS_YAFFS2

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
static int yaffs2_internal_read_super_mtd(struct super_block *sb, void *data,
					  int silent)
{
	return yaffs_internal_read_super(2, sb, data, silent) ? 0 : -EINVAL;
}

static struct dentry *yaffs2_mount(struct file_system_type *fs,
				   int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs, flags, dev_name, data,
			  yaffs2_internal_read_super_mtd);
}

static struct file_system_type yaffs2_fs_type = {
	.owner = THIS_MODULE,
	.name = "yaffs2",
	.mount = yaffs2_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
#else
static struct super_block *yaffs2_read_super(struct super_block *sb,
					     void *data, int silent)
{
	return yaffs_internal_read_super(2, sb, data, silent);
}

static DECLARE_FSTYPE(yaffs2_fs_type, "yaffs2", yaffs2_read_super,
		      FS_REQUIRES_DEV);
#endif

#endif				/* CONFIG_YAFFS_YAFFS2 */

static void yaffs_dump_dev(struct seq_file *m, yaffs_Device * dev)
{
	seq_printf(m, "startBlock......... %d\n", dev->startBlock);
	seq_printf(m, "endBlock........... %d\n", dev->endBlock);
	seq_printf(m, "chunkGroupBits..... %d\n", dev->chunkGroupBits);
	seq_printf(m, "chunkGroupSize..... %d\n", dev->chunkGroupSize);
	seq_printf(m, "nErasedBlocks...... %d\n", dev->nErasedBlocks);
	seq_printf(m, "nTnodesCreated..... %d\n", dev->nTnodesCreated);
	seq_printf(m, "nFreeTnodes........ %d\n", dev->nFreeTnodes);
	seq_printf(m, "nObjectsCreated.... %d\n", dev->nObjectsCreated);
	seq_printf(m, "nFreeObjects....... %d\n", dev->nFreeObjects);
	seq_printf(m, "nFreeChunks........ %d\n", dev->nFreeChunks);
	seq_printf(m, "nPageWrites........ %d\n", dev->nPageWrites);
	seq_printf(m, "nPageReads......... %d\n", dev->nPageReads);
	seq_printf(m, "nBlockErasures..... %d\n", dev->nBlockErasures);
	seq_printf(m, "nGCCopies.......... %d\n", dev->nGCCopies);
	seq_printf(m, "garbageCollections. %d\n", dev->garbageCollections);
	seq_printf(m, "passiveGCs......... %d\n",
		   dev->passiveGarbageCollections);
	seq_printf(m, "nRetriedWrites..... %d\n", dev->nRetriedWrites);
	seq_printf(m, "nRetireBlocks...... %d\n", dev->nRetiredBlocks);
	seq_printf(m, "nBadBlocks......... %d\n", dev->nBadBlocks);
	seq_printf(m, "eccFixed........... %d\n", dev->eccFixed);
	seq_printf(m, "eccUnfixed......... %d\n", dev->eccUnfixed);
	seq_printf(m, "tagsEccFixed....... %d\n", dev->tagsEccFixed);
	seq_printf(m, "tagsEccUnfixed..... %d\n", dev->tagsEccUnfixed);
	seq_printf(m, "cacheHits.......... %d\n", dev->cacheHits);
	seq_printf(m, "nDeletedFiles...... %d\n", dev->nDeletedFiles);
	seq_printf(m, "nUnlinkedFiles..... %d\n", dev->nUnlinkedFiles);
	seq_printf(m, "nBackgroudDeletions %d\n", dev->nBackgroundDeletions);
	seq_printf(m, "useNANDECC......... %d\n", dev->useNANDECC);
	seq_printf(m, "isYaffs2........... %d\n", dev->isYaffs2);
}

static int yaffs_proc_show(struct seq_file *m, void *v)
{
	struct list_head *item;
	int n = 0;

	/* Print header first */
	seq_printf(m, "YAFFS built:" __DATE__ " " __TIME__
		   "\n%s\n%s\n", yaffs_fs_c_version,
		   yaffs_guts_c_version);

	/* Locate and print the Nth entry.  Order N-squared but N is small. */
	mutex_lock(&yaffs_context_lock);
	list_for_each(item, &yaffs_dev_list) {
		yaffs_Device *dev = list_entry(item, yaffs_Device, devList);
		seq_printf(m, "\nDevice %d \"%s\"\n", n, dev->name);
		yaffs_dump_dev(m, dev);
		++n;
	}
	mutex_unlock(&yaffs_context_lock);
	return 0;
}

static int yaffs_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, yaffs_proc_show, NULL);
}

/**
 * Set the verbosity of the warnings and error messages.
 *
 */

static struct {
	char *mask_name;
	unsigned mask_bitfield;
} mask_flags[] = {
	{"allocate", YAFFS_TRACE_ALLOCATE},
	{"always", YAFFS_TRACE_ALWAYS},
	{"bad_blocks", YAFFS_TRACE_BAD_BLOCKS},
	{"buffers", YAFFS_TRACE_BUFFERS},
	{"bug", YAFFS_TRACE_BUG},
	{"deletion", YAFFS_TRACE_DELETION},
	{"erase", YAFFS_TRACE_ERASE},
	{"error", YAFFS_TRACE_ERROR},
	{"gc_detail", YAFFS_TRACE_GC_DETAIL},
	{"gc", YAFFS_TRACE_GC},
	{"mtd", YAFFS_TRACE_MTD},
	{"nandaccess", YAFFS_TRACE_NANDACCESS},
	{"os", YAFFS_TRACE_OS},
	{"scan_debug", YAFFS_TRACE_SCAN_DEBUG},
	{"scan", YAFFS_TRACE_SCAN},
	{"tracing", YAFFS_TRACE_TRACING},
	{"write", YAFFS_TRACE_WRITE},
	{"all", 0xffffffff},
	{"none", 0},
	{NULL, 0},
};

static ssize_t yaffs_proc_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *lpos)
{
	unsigned rg = 0, mask_bitfield;
	char *end, *mask_name;
	int i;
	int done = 0;
	int add, len;
	int pos = 0;

	char buf[128];

	if (count > sizeof(buf)) count = sizeof(buf);
	if (copy_from_user(buf, buffer, count) > 0) return -EINVAL;

	rg = yaffs_traceMask;

	while (!done && (pos < count)) {
		done = 1;
		while ((pos < count) && isspace(buf[pos])) {
			pos++;
		}

		switch (buf[pos]) {
		case '+':
		case '-':
		case '=':
			add = buf[pos];
			pos++;
			break;

		default:
			add = ' ';
			break;
		}
		mask_name = NULL;
		mask_bitfield = simple_strtoul(buf + pos, &end, 0);
		if (end > buf + pos) {
			mask_name = "numeral";
			len = end - (buf + pos);
			done = 0;
		} else if (strncmp(buf + pos, "disable_cp", 10) == 0) {
			cp_disabled = (add != '-');
		} else {

			for (i = 0; mask_flags[i].mask_name != NULL; i++) {
				len = strlen(mask_flags[i].mask_name);
				if (strncmp(buf + pos, mask_flags[i].mask_name, len) == 0) {
					mask_name = mask_flags[i].mask_name;
					mask_bitfield = mask_flags[i].mask_bitfield;
					done = 0;
					break;
				}
			}
		}

		if (mask_name != NULL) {
			pos += len;
			done = 0;
			switch(add) {
			case '-':
				rg &= ~mask_bitfield;
				break;
			case '+':
				rg |= mask_bitfield;
				break;
			case '=':
				rg = mask_bitfield;
				break;
			default:
				rg |= mask_bitfield;
				break;
			}
		}
	}

	yaffs_traceMask = rg;
	if ((rg & YAFFS_TRACE_ALWAYS) && !cp_disabled) {
		for (i = 0; mask_flags[i].mask_name != NULL; i++) {
			char flag;
			flag = ((rg & mask_flags[i].mask_bitfield) == mask_flags[i].mask_bitfield) ? '+' : '-';
			printk("%c%s\n", flag, mask_flags[i].mask_name);
		}
	}

	return count;
}

static const struct file_operations yaffs_proc_fops = {
	.open		= yaffs_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= yaffs_proc_write,
};

/* Stuff to handle installation of file systems */
struct file_system_to_install {
	struct file_system_type *fst;
	int installed;
};

static struct file_system_to_install fs_to_install[] = {
//#ifdef CONFIG_YAFFS_YAFFS1
	{&yaffs_fs_type, 0},
//#endif
//#ifdef CONFIG_YAFFS_YAFFS2
	{&yaffs2_fs_type, 0},
//#endif
	{NULL, 0}
};

#ifdef CONFIG_ARCH_RB
#include <mach/system.h>
#endif

extern int rb_big_boot_partition;

static int __init init_yaffs_fs(void)
{
	int error = 0;
	struct file_system_to_install *fsinst;

#ifdef CONFIG_ARCH_RB
	if (has_nand) {
		/* use UBIFS for NAND */
		return -EINVAL;
	}
#endif
#ifdef CONFIG_MTD_NAND_RB
	if (rb_big_boot_partition) {
		return -EINVAL;
	}
#endif

	T(YAFFS_TRACE_ALWAYS,
	  ("yaffs " __DATE__ " " __TIME__ " Installing. \n"));

	mutex_init(&yaffs_context_lock);

	proc_create_data("yaffs", 0644, NULL, &yaffs_proc_fops, NULL);

	/* Now add the file system entries */

	fsinst = fs_to_install;

	while (fsinst->fst && !error) {
		error = register_filesystem(fsinst->fst);
		if (!error) {
			fsinst->installed = 1;
		}
		fsinst++;
	}

	/* Any errors? uninstall  */
	if (error) {
		fsinst = fs_to_install;

		while (fsinst->fst) {
			if (fsinst->installed) {
				unregister_filesystem(fsinst->fst);
				fsinst->installed = 0;
			}
			fsinst++;
		}
	}

	return error;
}

static void __exit exit_yaffs_fs(void)
{

	struct file_system_to_install *fsinst;

	T(YAFFS_TRACE_ALWAYS, ("yaffs " __DATE__ " " __TIME__
			       " removing. \n"));

	remove_proc_entry("yaffs", NULL);

	fsinst = fs_to_install;

	while (fsinst->fst) {
		if (fsinst->installed) {
			unregister_filesystem(fsinst->fst);
			fsinst->installed = 0;
		}
		fsinst++;
	}

}

module_init(init_yaffs_fs)
module_exit(exit_yaffs_fs)

MODULE_DESCRIPTION("YAFFS2 - a NAND specific flash file system");
MODULE_AUTHOR("Charles Manning, Aleph One Ltd., 2002-2006");
MODULE_LICENSE("GPL");
