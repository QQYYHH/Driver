#include <linux/types.h>
#include <linux/moduleparam.h>
#include <linux/module.h>

#include <linux/kernel.h> // printk()
#include <linux/slab.h> // kmalloc()
#include <linux/fs.h>
#include <linux/errno.h> // error codes
#include <linux/fcntl.h> // O_ACCMODE
#include <linux/cdev.h>

#include <linux/uaccess.h>
#include <asm/uaccess.h> // copy_*_user 


#include "scdd.h"

/*
* 设置一些模块载入的参数
*/

int scdd_major = SCDD_MAJOR;
int scdd_minor = SCDD_MINOR;
int scdd_nr_devs = SCDD_NR_DEVS;
int scdd_unit_size = SCDD_UNIT_SIZE;
int scdd_unit_num = SCDD_UNIT_NUM;

module_param(scdd_major, int, S_IRUGO);
module_param(scdd_minor, int, S_IRUGO);
module_param(scdd_nr_devs, int, S_IRUGO);
module_param(scdd_unit_size, int, S_IRUGO);
module_param(scdd_unit_num, int, S_IRUGO);

MODULE_AUTHOR("milktime");
MODULE_LICENSE("GPL");

struct scdd_dev *scdd_devices; // allocated in scdd_init_module


/*
* 下面是一些辅助函数
* utilities
*/

/*
* clean up scdd 
* especially data list
*/
static int scdd_trim(struct scdd_dev *dev){
    if(NULL == dev) return 0;
    struct scdd_data_set *data_ptr, *next;
    int num = dev->unit_num, i;
    for(data_ptr = dev->data; data_ptr; data_ptr = next){
        if(data_ptr->data){
            for(i = 0;i < num; i++){
                kfree(data_ptr->data[i]);
            }
            kfree(data_ptr->data);
            data_ptr->data = NULL;
        }
        next = data_ptr->next;
        kfree(data_ptr);
    }
    dev->size = 0;
    dev->data = NULL;
    dev->unit_num = scdd_unit_num;
    dev->unit_size = scdd_unit_size;
    return 0;
}

/*
* 寻找第n个数据区
* 如果没有分配，动态分配
*/
static struct scdd_data_set *scdd_lookup_dset(struct scdd_dev *dev, int n){
    struct scdd_data_set *dset = dev->data, *pre;
    if(NULL == dset){ // first dset is not allocated
        dset = dev->data = kmalloc(sizeof(struct scdd_data_set), GFP_KERNEL);
        if(NULL == dset) goto FAIL;
        memset(dset, 0, sizeof(struct scdd_data_set));
    }
    pre = dset;
    dset = dset->next;
    while(n--){
        if(NULL == dset){
            dset = kmalloc(sizeof(struct scdd_data_set), GFP_KERNEL);
            if(NULL == dset) goto FAIL;
            memset(dset, 0, sizeof(struct scdd_data_set));
            pre->next = dset;
        }
        pre = dset;
        dset = dset->next;
    }
    return pre;
    FAIL:
        return NULL;
}


static void print_data(struct scdd_dev *dev){
    struct scdd_data_set *dset = dev->data;
    int cur_size = 0, i, j;
    while(dset && cur_size <= dev->size){
        if(dset->data){
            for(i = 0; i < dev->unit_num; i++){
                if(dset->data[i]){
                    char *ch = dset->data[i];
                    for(j = 0; j < dev->unit_size; j++){
                        printk(KERN_ALERT "%c__", *ch);
                        ch += 1;
                        cur_size += 1;
                        if(cur_size > dev->size) goto out;
                    }
                }
            }
        }
        else goto out;
        dset = dset->next;
    }
    out:
        return;
}

/*
* implement fs interface
*/
int scdd_open(struct inode *inode, struct file *filp){
    struct scdd_dev *dev;
    dev = container_of(inode->i_cdev, struct scdd_dev, cdev);
    // 将dev保存在打开文件的私有数据段，便于访问
    filp->private_data = dev; 

    return 0; // success
}
/*
* 关闭设备，不做任何处理
* 不能清空设备，持久化原因
*/
int scdd_release(struct inode *inode, struct file *filp){
    return 0;
}

/*
* 针对 一个存储单元读写
* 返回值有以下几种情况
* ret = count: 所请求的字节数完成
* 0 < ret < count: 部分数据传输完毕，可能由于设备差异
* 就比如说scdd，以存储单元 为单位，读写满一个存储单元就返回，这就会导致 ret < count
* 通常，程序会多次调用该函数，知道数据传输完毕为止
* ret = 0 : 文件结尾标志
* ret < 0: 出现其他错误(-EINTR [被系统调用中断], -EFAULT [无效地址])
*/
ssize_t scdd_read(struct file *filp, char __user *buf, size_t count, 
                loff_t *f_pos)
{
    struct scdd_dev *dev = filp -> private_data;
    struct scdd_data_set *dset;
    int unit_num = dev->unit_num, unit_size = dev->unit_size;
    int itemsize = unit_num * unit_size;
    
    ssize_t retval = 0;
    if(NULL == dev){
        retval = -ENOENT;
        goto out;
    }
    if(*f_pos >= dev->size)
        goto out;
    if(*f_pos + count > dev->size){
        count = dev->size - *f_pos;
    }

    int n = (long)*f_pos / itemsize;
    int res = (long)*f_pos % itemsize;
    int dset_i = res / unit_size, dset_off = res % unit_size;
    dset = scdd_lookup_dset(dev, n);
    if( !dset || !dset->data || !dset->data[dset_i]){
        // 调试信息
        /*if(dset){
            printk(KERN_ALERT "#### dset: %p ####", dset);
            if(dset->data){
                printk(KERN_ALERT "#### dset->data: %p ####", dset->data);
                if(dset->data[dset_i]){
                    printk(KERN_ALERT "#### dset->data[dset_i]: %p ####", dset->data[dset_i]);
                }
            }
        }
        else printk(KERN_ALERT "dset, dset->data, dset->data[dset_i] is NULL");*/
        retval = -ENOENT;
        goto out;
    }
    
    // read to the end of this storage unit
    if(count > unit_size - dset_off){
        count = unit_size - dset_off;
    }
    if(copy_to_user(buf, dset->data[dset_i] + dset_off, count)){
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;
    //printk(KERN_ALERT "read down#############\n");

    out:
        return retval;
}

/*
* write 类似 read
* ret = 0: 意味着什么也没写入。但不是错误，会重新调用write
*/
ssize_t scdd_write(struct file *filp, const char __user *buf, size_t count, 
                loff_t *f_pos)
{
    struct scdd_dev *dev = filp -> private_data;
    struct scdd_data_set *dset;
    int unit_num = dev->unit_num, unit_size = dev->unit_size;
    int itemsize = unit_num * unit_size;
    ssize_t retval = -ENOMEM; // value used in 'goto out' statements
    if(NULL == dev)
        goto out;

    // 这里假定存储空间无限，因此不会有内存不够的情况，无需做异常处理

    int n = (long)*f_pos / itemsize;
    int res = (long)*f_pos % itemsize;
    int dset_i = res / unit_size, dset_off = res % unit_size;
    dset = scdd_lookup_dset(dev, n);
    // printk(KERN_ALERT "dset address is %p", dset);
    if(NULL == dset)
        goto out;
    if(!dset->data){
        dset->data = kmalloc(unit_num * sizeof(void *), GFP_KERNEL);
        if(!dset->data)
            goto out;
        memset(dset->data, 0, unit_num * sizeof(void *));
    }
    if(!dset->data[dset_i]){
        dset->data[dset_i] = kmalloc(unit_size, GFP_KERNEL);
        if(!dset->data[dset_i])
            goto out;
    }

    // write only up to the end of the storage unit
    if(count > unit_size - dset_off)
        count = unit_size - dset_off;

    if(copy_from_user(dset->data[dset_i] + dset_off, buf, count)){
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    // write
    // 更新 dev->size大小
    if(dev->size < *f_pos)
        dev->size = *f_pos;

    // 写完之后，输出所有数据用于debug
    /*printk(KERN_ALERT "after write data size is %d", dev->size);
    printk(KERN_ALERT "dset address is %p", dev->data);
    print_data(dev);
    printk(KERN_ALERT "write_print_down#############");*/
    out:
        return retval;
}

loff_t scdd_llseek(struct file *filp, loff_t off, int whence){
    struct scdd_dev *dev = filp->private_data;
    loff_t newpos;
    switch (whence)
    {
    case 0: // SEEK_SET
        newpos = off;
        break;
    case 1: // SEEK_CUR
        newpos = filp->f_pos + off;
        break;
    case 2: // SEEK_END
        newpos = dev->size + off;
        break;
    
    default: // can't happen
        return -EINVAL;
    }
    if(newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

struct file_operations scdd_fops = {
    .owner = THIS_MODULE, 
    .llseek = scdd_llseek,
    .read = scdd_read,
    .write = scdd_write,
    .open = scdd_open,
    .release = scdd_release,
};

/*
* Finally, setup the module
*/

void scdd_cleanup_module(void){
    int i;
    dev_t devno = MKDEV(scdd_major, scdd_minor);
    if(scdd_devices){
        for(i = 0; i < scdd_nr_devs; i++){
            scdd_trim(scdd_devices + i);
            cdev_del(&scdd_devices[i].cdev);
        }
        kfree(scdd_devices);
    }
    unregister_chrdev_region(devno, scdd_nr_devs);
}

/*
* Set up the char_dev structure for this device.
*/
static void scdd_setup_cdev(struct scdd_dev *dev, int index){
    int err;
    dev_t devno = MKDEV(scdd_major, scdd_minor + index);
    cdev_init(&dev->cdev, &scdd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scdd_fops;
    // 将具体设备结构体 和 设备号关联
    err = cdev_add(&dev->cdev, devno, 1);
    if(err){
        printk(KERN_NOTICE "Error %d add scdd%d\n", err, index);
    }
}

int scdd_init_module(void){
    printk(KERN_ALERT "HELLO WORLD!!!");
    int res, i;
    dev_t dev = 0;

    if(scdd_major){ // 静态分配设备号
        dev = MKDEV(scdd_major, scdd_minor);
        res = register_chrdev_region(dev, scdd_nr_devs, "scdd");
    }
    else{ // 动态分配设备号
        res = alloc_chrdev_region(&dev, scdd_minor, scdd_nr_devs, 
                "scdd");
        scdd_major = MAJOR(dev);
    }
    if(res < 0){
        printk(KERN_WARNING "scdd: can't get major %d\n", scdd_major);
        return res;
    }

    scdd_devices = kmalloc(scdd_nr_devs * sizeof(struct scdd_dev), GFP_KERNEL);
    if(!scdd_devices){
        res = -ENOMEM;
        goto fail;
    }
    memset(scdd_devices, 0, scdd_nr_devs * sizeof(struct scdd_dev));

    // initialize each device
    for(i = 0; i < scdd_nr_devs; i++){
        scdd_devices[i].unit_num = scdd_unit_num;
        scdd_devices[i].unit_size = scdd_unit_size;
        // todo mutex
        scdd_setup_cdev(&scdd_devices[i], i);
    }



    return 0;
    fail:
        scdd_cleanup_module();
        return res;
}

module_init(scdd_init_module);
module_exit(scdd_cleanup_module);
