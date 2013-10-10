#ifndef __UFTL_H
#define __UFTL_H


#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/blktrans.h>
#include <linux/mtd/ubi.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>

//#define FTL_DEBUG

#ifdef FTL_DEBUG
#define ftl_dbg(fmt, ...) do{ \
    printk(KERN_WARNING "FTL: %s,%d: " fmt "\n", \
            __func__, __LINE__, ##__VA_ARGS__); \
}while(0)
#else
#define ftl_dbg(fmt, ...) do{ \
    printk(KERN_WARNING "FTL: " fmt "\n", \
            ##__VA_ARGS__); \
}while(0)
#endif


#ifdef FTL_DEBUG
#define ftl_vdbg(fmt, ...) do{ \
    printk(KERN_WARNING "FTL: %s,%d: " fmt "\n", \
            __func__, __LINE__, ##__VA_ARGS__); \
}while(0)
#else
#define ftl_vdbg(fmt, ...) do{ \
}while(0)
#endif

#define FTL_VERSION 15 
#define SECTOR_SIZE 512 
#define UBI_LOCAL_VOLUME_NAME "local"
#define MTD_LOCAL_PART_NAME "local"
#define UBI_BLOCK "ubiblk" 
#define UBI_MAJOR 0
#define UBIBLK_WORKQUEUE_DELAYED_TIME 200
#define UBIBLK_WORKQUEUE_NAME "ubiblk_workqueue"

#define BUF_NUM 64 
#define LOG_PAGE_NUM_PER_BLOCK 1
#define PAGE_EMPTY 1
#define PAGE_USING 2
#define PAGE_DISCARD 3
#define FTL_NOMAP -1
#define NO_ID -1
#define LOW_BLOCK_NUM 5
#define GC_BLOCK_NUM 10
#define FTL_LOG_BLOCK_NUM 10
#define PAGE_LOG_SECTOR 1
#define NEED_FLUSH_TIME 200
#define PER_INDEX_LEN 8192

/**
 * block state 
 * @page_state: the state (USING, DISCARD OR EMPTY) of the page in this block.
 * @using_page: the num of using page numbers of the block.
 */
struct block_state{
	char *page_state;
	int using_page;
};
/**
 * page buffer
 * @sector: the virtual sector store in this buffer.
 * @read: whether the buffer have been read form nand.
 * @data_buf: data buffer, include page data and oob data.
 * @sector_state: the state of the sectors(whether been changed)
 * @jiffies: the last jiffies time to touch the buffer.
 */
struct page_buf{
	int sector;
	char read;
	char *data_buf;
	int sector_state;
	unsigned long jiffies;
};

/**
 * uftl_info: struct of some uftl info
 */
struct uftl_info{
    int leb_num;
    int leb_page_size;
    int leb_page_num;
    /**
     * I will use 1 or more page in a leb
     * so this num will be leb_page_num -1 or -2
     */
    int ftl_page_num;
    /**
     * the sector numbers of one page
     */
    int page_sector_num;

    int virtual_sector_num;
};
/**
 * log info: will record the page info in the log page (last page in a block)
 */
struct log_info{
    short magic;
    int id;
};

struct page_log_info{
    short magic;
    int sector;
    int id;
};

struct line_offset{
    int block;
    int page;
};

struct cache_stat{
    int read_sector;
    int read_leb_page;
    int write_sector;
    int write_leb_page;
};

struct fake_int_array{
    int **data;
    int line;
    int num;
};

extern int ftl_core_io_init(struct ubi_volume_desc *ubi, int , int , int , int);
extern int ftl_core_io_read(struct ubi_volume_desc *ubi, long sector, char *buf);
extern int ftl_core_io_write(struct ubi_volume_desc *ubi, long sector, char *buf);
extern int ftl_core_io_queue_flush(struct ubi_volume_desc *ubi);
extern int ftl_core_io_all_flush(struct ubi_volume_desc *ubi);

extern int ftl_erase(struct ubi_volume_desc *ubi, int lnum, int unmap);
extern int ftl_read(struct ubi_volume_desc *ubi, struct uftl_info *fip, int lnum, int page, char *buf);
extern int ftl_write(struct ubi_volume_desc *ubi, struct uftl_info *fip, int lnum, int page, char *buf);
extern int ftl_rescure(struct ubi_volume_desc *ubi, struct uftl_info *fip, int lnum, int till_page);

extern ssize_t show_cache_stat(char *buf);
extern void clear_cache_stat(void);
extern void add_read_sector(void);
extern void add_write_sector(void);
extern void add_read_leb(void);
extern void add_write_leb(void);
#endif


