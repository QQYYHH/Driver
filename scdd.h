/*
* scdd.h -- default for the char module
* author : Martin Qi
*/

#ifndef _SCDD_H_
#define _SCDD_H_

/* dynamic major by default
corresponding to device driver
*/
#ifndef SCDD_MAJOR
#define SCDD_MAJOR 0 

/*
minor by default 
corresponding to hardware device 
*/
#ifndef SCDD_MINOR
#define SCDD_MINOR 0


/*
define the The unit size of the store unit
定义设备存储单元大小
单位 字节B
*/
#ifndef SCDD_UNIT_SIZE
#define SCDD_UNIT_SIZE 4096


/*
定义每一个设备存储区 存储单元数量
设备存储区通过链表连接
1000 by default
每个存储区大小 = 1000 * 4096 B
*/
#ifndef SCDD_UNIT_NUM
#define SCDD_UNIT_NUM 1000


/*
* 设备存储区的定义
* 存储区以链表的形式连接
*/
struct scdd_data_set{
    void **data; // 连续的存储单元
    struct scdd_data_set *next;
}

/*
* 定义抽象设备
* 内含linux提供的字符设备结构体 cdev
*/
struct scdd_dev{
    struct scdd_data_set *data; // 指向数据存储区
    int unit_num; // 每个数据存储区 存储单元的个数
    int unit_size; // 每个数据存储区 存储单元的大小
    unsigned long size; // 实际存储数据的大小
    struct cdev cdev; // linux 字符设备结构体
}

/*
* prototypes for shared functions
* 实现 满足VFS接口规范 的操作函数
* open read write close/release
* llseek
*/

int scdd_open(struct inode *inode, struct file *filp);

int scdd_release(struct inode *inode, struct file *filp);

ssize_t scdd_read(struct file *filp, char __user *buf, size_t count, 
                loff_t *f_pos);

ssize_t scdd_write(struct file *filp, const char __user *buf, size_t count, 
                loff_t *f_pos);

loff_t scdd_llseek(struct file *filp, loff_t off, in whence);