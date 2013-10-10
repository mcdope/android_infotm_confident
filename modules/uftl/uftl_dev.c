/*
 * Direct UBI block device access
 *
 * Author: dmitry pervushin <dimka@embeddedalley.com>
 * Change by warits,Be5t0 
 *
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * Based on mtdblock by:
 * (C) 2000-2003 Nicolas Pitre <nico@cam.org>
 * (C) 1999-2003 David Woodhouse <dwmw2@infradead.org>
 */
#include "uftl.h"

static LIST_HEAD(ubiblk_devices);

/**
 * ubiblk_dev - the structure representing translation layer
 *
 * @m: interface to mtd_blktrans
 * @ubi_num: UBI device number
 * @ubi_vol: UBI volume ID
 * @usecount: reference count
 * @ubi: ubi volume point to write or write form nand/ubi
 * @uftl_mutex: mutex to make more than one thread can work together
 * @ubiblock_delayed_work: use jiffies to make data can be sync to the nand automaticly
 *
 **/
struct ubiblk_dev {

    struct mtd_blktrans_dev m;
    struct ubi_volume_desc *ubi;

    int ubi_num;
    int ubi_vol;
    int usecount;

    struct mutex uftl_mutex;

    struct list_head list;

    struct work_struct unbind;
    struct workqueue_struct *ubiblock_workqueue;
    struct delayed_work ubiblock_delayed_work;
};

static struct mtd_info *mbp;
extern struct mutex mtd_table_mutex;

static int ubiblock_open(struct mtd_blktrans_dev *mbd);
static int ubiblock_release(struct mtd_blktrans_dev *mbd);
static int ubiblock_flush(struct mtd_blktrans_dev *mbd);
static int ubiblock_readsect(struct mtd_blktrans_dev *mbd,
        unsigned long block, char *buf);
static int ubiblock_writesect(struct mtd_blktrans_dev *mbd,
        unsigned long block, char *buf);
static int ubiblock_discard(struct mtd_blktrans_dev *mbd,
        unsigned long block, unsigned nt_block);
static void *ubiblk_add(int ubi_num, int ubi_vol_id);
static void *ubiblk_add_locked(int ubi_num, int ubi_vol_id);
static int ubiblk_del(struct ubiblk_dev *u);
static int ubiblk_del_locked(struct ubiblk_dev *u);
/*
 * These two routines are just to satify mtd_blkdev's requirements
 */
static void ubiblock_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
    return;
}

static void ubiblock_remove_dev(struct mtd_blktrans_dev *mbd)
{
    return;
}

static struct mtd_blktrans_ops ubiblock_tr = {

    .name       = UBI_BLOCK,
    .major      = UBI_MAJOR, /* assign dynamically  */
    .part_bits  = 0,              
    .blksize    = SECTOR_SIZE,
    .open       = ubiblock_open,
    .release    = ubiblock_release,
    .flush      = ubiblock_flush,
    .readsect   = ubiblock_readsect,
    .writesect  = ubiblock_writesect,
    .discard    = ubiblock_discard,
    .add_mtd    = ubiblock_add_mtd,
    .remove_dev = ubiblock_remove_dev,
    .owner      = THIS_MODULE,
};


/**
 * ubiblock_writesect - write the sector
 */
static int ubiblock_writesect(struct mtd_blktrans_dev *mbd,
        unsigned long sector, char *buf)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);
    int err = 0;

    mutex_lock(&u->uftl_mutex);
    err = ftl_core_io_write(u->ubi, sector, buf);
    if (err) {
        ftl_dbg("write sector failed. Err: %d", err);
    }
    mutex_unlock(&u->uftl_mutex);

    return err;
}

/**
 * ubiblk_readsect - read the sector
 */
static int ubiblock_readsect(struct mtd_blktrans_dev *mbd,
        unsigned long sector, char *buf)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);
    int err = 0;

    mutex_lock(&u->uftl_mutex);
    err = ftl_core_io_read(u->ubi, sector, buf);
    if (err) {
        ftl_dbg("read sector failed. Err: %d", err);
    }
    mutex_unlock(&u->uftl_mutex);

    return err;
}
/**
 * ubiblk_discard - will be used to implement trim.
 */
static int ubiblock_discard(struct mtd_blktrans_dev *mbd,
        unsigned long sector, unsigned length)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);
    int err = 0;
    ftl_dbg("discard sector %ld", sector);
    mutex_lock(&u->uftl_mutex);
    /* TODO */
    mutex_unlock(&u->uftl_mutex);
    return err;
}
/**
 * auto flush buffer.
 * If some other thread get the mutex to operate the nand or the buffer, 
 * the function will return directly.
 */
static void ubiblock_workqueue_func(struct work_struct *work){
    struct ubiblk_dev *u = container_of((struct delayed_work *)work, 
            struct ubiblk_dev, ubiblock_delayed_work);
    int ret = 0;
    int need_sync = 0;
    ret = mutex_trylock(&u->uftl_mutex);
    if(!ret) goto end;
    /* TODO */
    if (u->ubi != NULL) {
        need_sync = ftl_core_io_queue_flush(u->ubi);
    }
    mutex_unlock(&u->uftl_mutex);

    if (need_sync) {
        /* now we dont sync
         * to increase speed.
         */
        ubi_sync(u->ubi_num); 
    }
end:
    queue_delayed_work(u->ubiblock_workqueue, 
            &u->ubiblock_delayed_work, 
            UBIBLK_WORKQUEUE_DELAYED_TIME);
}


static int ubiblock_flush(struct mtd_blktrans_dev *mbd)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);
    int err = 0;

    ftl_dbg("Flush data to nand");

    mutex_lock(&u->uftl_mutex);
    ftl_core_io_all_flush(u->ubi);
    mutex_unlock(&u->uftl_mutex);

    ubi_sync(u->ubi_num);

    return err; 
}


static int ubiblock_open(struct mtd_blktrans_dev *mbd)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);

    if (u->usecount == 0) {
        u->ubi = ubi_open_volume(u->ubi_num, u->ubi_vol, UBI_READWRITE);
        if (IS_ERR(u->ubi))
            return PTR_ERR(u->ubi);
    }

    u->usecount++;

    return 0;
}

static int ubiblock_release(struct mtd_blktrans_dev *mbd)
{
    struct ubiblk_dev *u = container_of(mbd, struct ubiblk_dev, m);

    if (--u->usecount == 0) {
        ubiblock_flush(mbd);
        ubi_close_volume(u->ubi);
        u->ubi = NULL;
    }

    return 0;
}

/*
 * sysfs routines. The ubiblk creates two entries under /sys/block/ubiblkX:
 *  - volume, R/O, which is read like "ubi0:volume_name"
 *  - unbind, W/O; when user writes something here, the block device is
 *  removed
 *
 *  unbind schedules a work item to perform real unbind, because sysfs entry
 *  handler cannot delete itself :)
 */
ssize_t volume_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct gendisk *gd = dev_to_disk(dev);
    struct mtd_blktrans_dev *m = gd->private_data;
    struct ubiblk_dev *u = container_of(m, struct ubiblk_dev, m);

    return sprintf(buf, "%d:%d\n", u->ubi_num, u->ubi_vol);
}

static void ubiblk_unbind(struct work_struct *ws)
{
    struct ubiblk_dev *u = container_of(ws, struct ubiblk_dev, unbind);

    ubiblk_del(u);
}

ssize_t unbind_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    struct gendisk *gd = dev_to_disk(dev);
    struct mtd_blktrans_dev *m = gd->private_data;
    struct ubiblk_dev *u = container_of(m, struct ubiblk_dev, m);

    INIT_WORK(&u->unbind, ubiblk_unbind);
    schedule_work(&u->unbind);
    return count;
}

ssize_t cache_stat_show(struct device *dev, 
        struct device_attribute *attr, char *buf){
    return show_cache_stat(buf);
}
ssize_t cache_stat_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    clear_cache_stat();
    return count;
}

DEVICE_ATTR(unbind, 0644, NULL, unbind_store);
DEVICE_ATTR(volume, 0644, volume_show, NULL);
DEVICE_ATTR(cache_stat, 0666, cache_stat_show, cache_stat_store);

static int ubiblk_sysfs(struct gendisk *hd, int add)
{
    int r = 0;

    if (add) {
        r = device_create_file(disk_to_dev(hd), &dev_attr_unbind);
        if (r < 0)
            goto out;
        r = device_create_file(disk_to_dev(hd), &dev_attr_volume);
        if (r < 0)
            goto out1;
        r =device_create_file(disk_to_dev(hd), &dev_attr_cache_stat);
        if (r <0)
            goto out2;
        return 0;
    }
out2:
    device_remove_file(disk_to_dev(hd), &dev_attr_unbind);
out1:
    device_remove_file(disk_to_dev(hd), &dev_attr_volume);
out:
    return r;
}

/**
 * add the FTL by registering it with mtd_blkdevs
 */
static void *ubiblk_add(int ubi_num, int ubi_vol_id)
{
    void *p;

    mutex_lock(&mtd_table_mutex);
    p = ubiblk_add_locked(ubi_num, ubi_vol_id);
    mutex_unlock(&mtd_table_mutex);

    return p;
}

static void *ubiblk_add_locked(int ubi_num, int ubi_vol_id)
{
    struct ubiblk_dev *u = kzalloc(sizeof(*u), GFP_KERNEL);
    struct ubi_volume_info uvi;
    struct ubi_volume_desc *ubi;
    int ret;

    int page_size, page_num;

    if (!u) {
        u = ERR_PTR(-ENOMEM);
        goto out;
    }

    ubi = ubi_open_volume(ubi_num, ubi_vol_id, UBI_READWRITE);

    u->ubi = ubi;
    if (IS_ERR(u->ubi)) {
        pr_err("USER DEBUG:cannot open the volume\n");
        u = (void *)u->ubi;
        goto out;
    }

    ubi_get_volume_info(ubi, &uvi);

    u->m.mtd = mbp;
    u->m.devnum = -1;
    u->m.tr = &ubiblock_tr;

    u->ubi_num = ubi_num;
    u->ubi_vol = ubi_vol_id;

    page_size = (u->m.mtd)->writesize;
    page_num = uvi.usable_leb_size/page_size;

    /*tell the filesystem how many sectors can be used,here I keep some blocks for recycle block.*/
    u->m.size 
        = (uvi.size / 5 * 4 - GC_BLOCK_NUM - FTL_LOG_BLOCK_NUM) 
        * (page_size/SECTOR_SIZE - PAGE_LOG_SECTOR)  
        * (page_num - LOG_PAGE_NUM_PER_BLOCK);

    u->usecount = 0;

    mutex_init(&u->uftl_mutex);

    ret = ftl_core_io_init(ubi, uvi.size, page_num, page_size, u->m.size);

    if (ret) {
        if (ret != -EBUSY) {
            ftl_dbg("io init failed. Err: %d", ret);
            goto out;
        }else{
            ftl_dbg("already init ftl index, skip");
        }
    }
    clear_cache_stat();

    INIT_LIST_HEAD(&u->list);
    list_add_tail(&u->list, &ubiblk_devices);

    add_mtd_blktrans_dev(&u->m);
    ubiblk_sysfs(u->m.disk, true);

    u->ubiblock_workqueue = create_singlethread_workqueue(UBIBLK_WORKQUEUE_NAME);
    if (!u->ubiblock_workqueue) {
        ftl_dbg("create workqueue failed.");
        goto out;
    }else{
        ftl_dbg("create workquque successed.");
    }
    INIT_DELAYED_WORK(&u->ubiblock_delayed_work, ubiblock_workqueue_func);
    ubiblock_workqueue_func(&(u->ubiblock_delayed_work.work));

out:
    ubi_close_volume(ubi);
    return u;
}

static int ubiblk_del(struct ubiblk_dev *u)
{
    int r;
    mutex_lock(&mtd_table_mutex);
    r = ubiblk_del_locked(u);
    mutex_unlock(&mtd_table_mutex);
    return r;
}

static int ubiblk_del_locked(struct ubiblk_dev *u)
{
    if (u->usecount != 0)
        return -EBUSY;
    ubiblk_sysfs(u->m.disk, false);
    del_mtd_blktrans_dev(&u->m);
    list_del(&u->list);
    kfree(u);
    return 0;
}

static struct ubiblk_dev *ubiblk_find(int num, int vol)
{
    struct ubiblk_dev *pos;

    list_for_each_entry(pos, &ubiblk_devices, list)
        if (pos->ubi_num == num && pos->ubi_vol == vol)
            return pos;
    return NULL;
}

static int ubiblock_notification(struct notifier_block *blk,
        unsigned long type, void *v)
{
    struct ubi_notification *nt = v;
    struct ubiblk_dev *u;

    switch (type) {
        case UBI_VOLUME_ADDED:
            ftl_dbg("add %s", nt->vi.name);
            if (!strcmp(nt->vi.name, UBI_LOCAL_VOLUME_NAME)) {
                ubiblk_add(nt->vi.ubi_num, nt->vi.vol_id);
            }else{
                ftl_dbg("not %s, skip", UBI_LOCAL_VOLUME_NAME);
            }
            break;
        case UBI_VOLUME_REMOVED:
            if (strcmp(nt->vi.name, UBI_LOCAL_VOLUME_NAME)) {
                break;
            }
            u = ubiblk_find(nt->vi.ubi_num, nt->vi.vol_id);
            if (u)
                ubiblk_del(u);
            break;
        case UBI_VOLUME_RENAMED:
        case UBI_VOLUME_RESIZED:
            break;
    }
    return NOTIFY_OK;
}

static struct notifier_block ubiblock_nb = {
    .notifier_call = ubiblock_notification,
};

static int __init ubiblock_init(void)
{
    int r;

    ftl_dbg("Version: %d", FTL_VERSION);

    mbp =  get_mtd_device_nm(MTD_LOCAL_PART_NAME);

    r = register_mtd_blktrans(&ubiblock_tr);
    if (r)
        goto out;
    r = ubi_register_volume_notifier(&ubiblock_nb, 0);
    if (r)
        goto out_unreg;

    ftl_dbg("init successed.");

    return 0;

out_unreg:
    deregister_mtd_blktrans(&ubiblock_tr);
out:
    ftl_dbg("init failed.");
    return 0;
}

static void __exit ubiblock_exit(void){
    deregister_mtd_blktrans(&ubiblock_tr);
    ftl_dbg("exit.");
}

module_init(ubiblock_init);
module_exit(ubiblock_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Caching block device emulation access to UBI devices");
MODULE_AUTHOR("dmitry pervushin <dimka@embeddedalley.com>,warits,Be5t0");
