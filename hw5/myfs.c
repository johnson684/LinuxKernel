#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");

#define MYFS_MAGIC 0x20210607
#define TMPSIZE 32

static atomic_t val_a;
static atomic_t val_b;

enum {
    TYPE_A = 1,
    TYPE_B,
    TYPE_ADD,
    TYPE_SUB
};

static struct inode *myfs_make_inode(struct super_block *sb, int mode)
{
    struct inode *ret = new_inode(sb);

    if (ret) {
        ret->i_mode = mode;
        ret->i_uid = GLOBAL_ROOT_UID;
        ret->i_gid = GLOBAL_ROOT_GID;
        ret->i_blocks = 0;

        ret->i_atime = current_time(ret);
        ret->i_mtime = current_time(ret);
        ret->i_ctime = current_time(ret);
    }
    return ret;
}

static int myfs_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;
    return 0;
}

static ssize_t myfs_read_file(struct file *filp, char __user *buf,
                              size_t count, loff_t *offset)
{
    long type = (long) filp->private_data;
    int a, b, result = 0, len;
    char tmp[TMPSIZE];

    a = atomic_read(&val_a);
    b = atomic_read(&val_b);

    switch (type) {
        case TYPE_A:   result = a; break;
        case TYPE_B:   result = b; break;
        case TYPE_ADD: result = a + b; break;
        case TYPE_SUB: result = a - b; break;
        default:       return -EINVAL;
    }

    len = snprintf(tmp, TMPSIZE, "%d\n", result);

    if (*offset >= len)
        return 0;
    if (count > len - *offset)
        count = len - *offset;

    if (copy_to_user(buf, tmp + *offset, count))
        return -EFAULT;
    
    *offset += count;
    return count;
}

static ssize_t myfs_write_file(struct file *filp, const char __user *buf,
                               size_t count, loff_t *offset)
{
    long type = (long) filp->private_data;
    char tmp[TMPSIZE];
    long v;
    int err;

    if (*offset != 0)
        return -EINVAL;

    if (count >= TMPSIZE)
        return -EINVAL;
        
    memset(tmp, 0, TMPSIZE);
    if (copy_from_user(tmp, buf, count))
        return -EFAULT;

    err = kstrtol(strim(tmp), 10, &v);
    if (err)
        return err;

    if (v < 0 || v > 255)
        return -EINVAL;

    if (type == TYPE_A)
        atomic_set(&val_a, (int)v);
    else if (type == TYPE_B)
        atomic_set(&val_b, (int)v);
    else
        return -EINVAL; /* ADD 和 SUB 不可寫入 */

    return count;
}

static const struct file_operations myfs_rw_ops = {
    .owner = THIS_MODULE,
    .open = myfs_open,
    .read = myfs_read_file,
    .write = myfs_write_file,
};

static const struct file_operations myfs_ro_ops = {
    .owner = THIS_MODULE,
    .open = myfs_open,
    .read = myfs_read_file,
};

static struct dentry *myfs_create_file(struct super_block *sb,
                                       struct dentry *dir, const char *name,
                                       void *private_data, const struct file_operations *myfs_ops,
                                       int mode)
{
    struct dentry *dentry;
    struct inode *inode;
    struct qstr qname;

    qname.name = name;
    qname.len = strlen(name);
    qname.hash = full_name_hash(dir, name, qname.len);

    dentry = d_alloc(dir, &qname);
    if (!dentry)
        goto out;
        
    inode = myfs_make_inode(sb, S_IFREG | mode);
    if (!inode)
        goto out_dput;
        
    inode->i_fop = myfs_ops;
    inode->i_private = private_data;

    d_add(dentry, inode);
    return dentry;

out_dput:
    dput(dentry);
out:
    return NULL;
}

static struct dentry *myfs_create_dir(struct super_block *sb,
                                      struct dentry *parent, const char *name)
{
    struct dentry *dentry;
    struct inode *inode;
    struct qstr qname;

    qname.name = name;
    qname.len = strlen(name);
    qname.hash = full_name_hash(parent, name, qname.len);
    dentry = d_alloc(parent, &qname);
    if (!dentry)
        goto out;

    inode = myfs_make_inode(sb, S_IFDIR | 0755);
    if (!inode)
        goto out_dput;
    inode->i_op = &simple_dir_inode_operations;
    inode->i_fop = &simple_dir_operations;

    d_add(dentry, inode);
    return dentry;

out_dput:
    dput(dentry);
out:
    return NULL;
}

static void myfs_create_files(struct super_block *sb, struct dentry *root)
{
    struct dentry *inputdir, *outputdir;

    atomic_set(&val_a, 0);
    atomic_set(&val_b, 0);

    inputdir = myfs_create_dir(sb, root, "input");
    if (inputdir) {
        myfs_create_file(sb, inputdir, "a", (void *)TYPE_A, &myfs_rw_ops, 0644);
        myfs_create_file(sb, inputdir, "b", (void *)TYPE_B, &myfs_rw_ops, 0644);
    }

    outputdir = myfs_create_dir(sb, root, "output");
    if (outputdir) {
        myfs_create_file(sb, outputdir, "add", (void *)TYPE_ADD, &myfs_ro_ops, 0444);
        myfs_create_file(sb, outputdir, "sub", (void *)TYPE_SUB, &myfs_ro_ops, 0444);
    }
}

static const struct super_operations myfs_s_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *root;
    struct dentry *root_dentry;

    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_magic = MYFS_MAGIC;
    sb->s_op = &myfs_s_ops;

    root = myfs_make_inode(sb, S_IFDIR | 0755);
    if (!root)
        goto out;
    root->i_op = &simple_dir_inode_operations;
    root->i_fop = &simple_dir_operations;

    root_dentry = d_make_root(root);
    if (!root_dentry)
        goto out_iput;
    sb->s_root = root_dentry;

    myfs_create_files(sb, root_dentry);
    return 0;

out_iput:
    iput(root);
out:
    return -ENOMEM;
}

static struct dentry *myfs_get_super(struct file_system_type *fst,
                                     int flags, const char *devname, void *data)
{
    return mount_nodev(fst, flags, data, myfs_fill_super);
}

static struct file_system_type myfs_type = {
    .owner = THIS_MODULE,
    .name = "myfs",
    .mount = myfs_get_super,
    .kill_sb = kill_litter_super,
};

static int __init myfs_init(void)
{
    return register_filesystem(&myfs_type);
}

static void __exit myfs_exit(void)
{
    unregister_filesystem(&myfs_type);
}

module_init(myfs_init);
module_exit(myfs_exit);
