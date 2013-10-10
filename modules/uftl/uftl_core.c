/*
 * Copyright (c) 2012~2014 ShangHai InfoTM Ltd all rights reserved.
 *
 * Use of Infotm's code is governed by terms and conditions
 * stated in the accompanying licensing statement.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author:Be5t0 <bestoapache@gmail.com>
 *
 **/
#include "uftl.h"

static short magic_num = 722;

static int scrub_block(struct ubi_volume_desc *ubi, int block);
static int garbage_checking(struct ubi_volume_desc *ubi, int low_block_num);
static void add_block_using_page(int block);
static void del_block_using_page(int block);
static void clear_block_using_page(int block);
static int get_first_empty_block(void);

static struct block_state * block_state_index;
/**
 * some interface of block and page state set and get.
 */
static int get_page_state(int block, int page){
    return block_state_index[block].page_state[page];
}
static void set_page_state(int block, int page, char state){
    block_state_index[block].page_state[page] = state;
}
static int alloc_block_state_index(int block, int page_per_block){
    int i;
    block_state_index = (struct block_state *)kmalloc(block*sizeof(struct block_state), GFP_KERNEL);
    if (unlikely(block_state_index == NULL)) {
        return -ENOMEM;
    }
    for (i = 0; i < block; i++) {
        block_state_index[i].page_state = (char *)kmalloc(page_per_block*sizeof(char), GFP_KERNEL);
        if (unlikely(block_state_index[i].page_state == NULL)){
            return -ENOMEM;
        }else{
            memset(block_state_index[i].page_state, PAGE_EMPTY, page_per_block);
        }
    }
    return 0;
}
static void free_block_state_index(int block){
    int i;
    for (i = 0; i < block; i++) {
        kfree(block_state_index[i].page_state);
    }
    kfree(block_state_index);
}

struct uftl_info * fip;

static struct page_buf * page_buf_pool;
/**
 * TODO some inferface to write or read page buf.
 */
static void alloc_page_buf_pool(void){
    int i;
    page_buf_pool = kmalloc(sizeof(struct page_buf)*BUF_NUM, GFP_KERNEL);
    for (i = 0; i < BUF_NUM; i++) {
        page_buf_pool[i].sector = FTL_NOMAP;
        page_buf_pool[i].read = 0;
        page_buf_pool[i].sector_state = 0;
        page_buf_pool[i].data_buf = kmalloc(fip->leb_page_size, GFP_KERNEL);
        page_buf_pool[i].jiffies = jiffies;
    }
}
/**
 * TODO
 * This action will need a lot of memory in BIG nand
 * I will fix it later
 */
static struct fake_int_array sector_index;
static struct fake_int_array sector_id;
/**
 * interface to get and set sector_index or sector_id 
 */
static int get_sector_index(int sector){
    int line = sector/sector_index.num;
    int num = sector%sector_index.num;
    return (sector_index.data[line][num]);
}
static void set_sector_index(int sector, int index){
    int line = sector/sector_index.num;
    int num = sector%sector_index.num;
    (sector_index.data[line][num]) = index;
}
static int alloc_sector_index(int sector){
    int i;
    sector_index.num = PER_INDEX_LEN/sizeof(int);
    sector_index.line = sector/sector_index.num+1;
    sector_index.data = kmalloc(sector_index.line*sizeof(int), GFP_KERNEL);
    for (i = 0; i < sector_index.line; i++){
        sector_index.data[i] = kmalloc(PER_INDEX_LEN, GFP_KERNEL);
        if (unlikely(sector_index.data[i] == NULL)){
            return -ENOMEM;
        }
    }
    return 0;
}
static void free_sector_index(void){
    int i;
    for (i = 0; i < sector_index.line; i++){ 
        kfree(sector_index.data[i]);
    }
    kfree(sector_index.data);
}

static int get_sector_id(int sector){
    int line = sector/sector_id.num;
    int num = sector%sector_id.num;
    return (sector_id.data[line][num]);
}
static void set_sector_id(int sector, int index){
    int line = sector/sector_id.num;
    int num = sector%sector_id.num;
    (sector_id.data[line][num]) = index;
}
static int alloc_sector_id(int sector){
    int i;
    sector_id.num = PER_INDEX_LEN/sizeof(int);
    sector_id.line = sector/sector_id.num+1;
    sector_id.data = kmalloc(sector_id.line*sizeof(int), GFP_KERNEL);
    for (i = 0; i < sector_id.line; i++){
        sector_id.data[i] = kmalloc(PER_INDEX_LEN, GFP_KERNEL);
        if (unlikely(sector_id.data[i] == NULL)){
            return -ENOMEM;
        }
    }
    return 0;
}
static void free_sector_id(void){
    int i;
    for (i = 0; i < sector_id.line; i++){ 
        kfree(sector_id.data[i]);
    }
    kfree(sector_id.data);
}
/*
 * just two buffers
 */
static char *tmp_page_buf;
static char *block_log_page;

static struct line_offset cur_offset;
/*                                                                           
 * the state when the sectors in the page were all changed                   
 */                                                                          
static int full_state;                                                       
/*                                                                           
 * block id                                                                  
 * a number to make us know which page contained useful data when more two pages record the same virtual sec
tor.
 */                                                                          
static int block_id;                                                         
/*
 * check count: write page more then check count will gc
 */
static int check_count;

/*
 * empty block fifo
 */
static struct kfifo empty_block_fifo;
/**
 * create index
 */
static int need_rewrite = 0;
static int rewrite_page = -1;
static int create_index(struct ubi_volume_desc *ubi){
    int i, ret;
    int block, page,old_index;
    int sector,id;
    struct log_info cur_log;

    ftl_dbg("create ftl index...");

    cur_offset.block = FTL_NOMAP;

    for (i = 0; i < fip->virtual_sector_num; i++){
        set_sector_index(i, FTL_NOMAP);
        set_sector_id(i, NO_ID);
    }

    for (block = 0; block < fip->leb_num; block++) {
       ret = ftl_read(ubi, fip, block, fip->ftl_page_num, tmp_page_buf);
       if (ret) {
           ftl_dbg("read leb %d failed, erase it", block);
           ret = ftl_erase(ubi, block, 0);
           if (ret) {
               return ret;
           }
           continue;
       }
       cur_log = *(struct log_info *)tmp_page_buf;
       if (cur_log.magic == magic_num) {
           int *log = (int *)(tmp_page_buf + sizeof(struct log_info));
           id = cur_log.id;
           for (page = 0; page < fip->ftl_page_num; page++) {
               sector = log[page];
               if (get_sector_id(sector) > id) {
                   set_page_state(block, page, PAGE_DISCARD);
               }else{
                   block_id = block_id < id ? id : block_id;
                   old_index = get_sector_index(sector);
                   if (old_index != FTL_NOMAP) {
                       set_page_state(old_index/fip->ftl_page_num,
                               old_index%fip->ftl_page_num, 
                               PAGE_DISCARD);
                   }
                   set_sector_id(sector, id);
                   set_sector_index(sector, page + fip->ftl_page_num*block);
                   set_page_state(block, page, PAGE_USING);
               }
           }
       }else{
           /*
            * TODO restore info from the last block, it has no log page
            * so I will read info from the page log sector
            */
           if (cur_offset.block != FTL_NOMAP) {
               continue;
           }
           for (page = 0; page < fip->ftl_page_num; page++) {
               struct page_log_info * page_log_cur;
               ret = ftl_read(ubi, fip, block, page, tmp_page_buf);
               if (ret) {
                   need_rewrite = 1;
                   rewrite_page = page;
                   ftl_dbg("need rewrite leb %d, err page = %d", block, page);
                   break;
               }
               page_log_cur = (struct page_log_info *)(tmp_page_buf+fip->page_sector_num*SECTOR_SIZE);
               id = page_log_cur->id;
               sector = page_log_cur->sector;

               if (page_log_cur->magic == magic_num) {
                   if (get_sector_id(sector) > id) {
                       set_page_state(block, page, PAGE_DISCARD);
                   }else{
                       block_id = block_id < id ? id : block_id;
                       old_index = get_sector_index(sector);
                       if (old_index != FTL_NOMAP) {
                           set_page_state(old_index/fip->ftl_page_num,
                                   old_index%fip->ftl_page_num, 
                                   PAGE_DISCARD);
                       }
                       set_sector_id(sector, id);
                       set_sector_index(sector, page+block*fip->ftl_page_num);
                       set_page_state(block, page, PAGE_USING);
                       memcpy(block_log_page+sizeof(struct log_info)+sizeof(int)*page,
                               &sector, sizeof(int));
                   }
               }else{
                   //Leave page is not write
                   break;
               }
           }
           if (page > 0) {
               cur_offset.block = block;
               cur_offset.page = page - 1;
           }
       }
    }
    ftl_dbg("create index ok!");

    return 0;
}
/**
 * init status
 * find first empty page, and scrub the last using leb to new leb.
 */
static int init_status(struct ubi_volume_desc *ubi){
    int i, block, page;
    int ret = 0;
    /* 
     * create the block using page numbers and store them in the memory
     */
    ret = kfifo_alloc(&empty_block_fifo, fip->leb_num * sizeof(int), GFP_KERNEL);
    if (ret) {
        return ret;
    }
    for (block = 0; block < fip->leb_num; block++) {
        clear_block_using_page(block);
        for (page = 0; page < fip->ftl_page_num; page++){
            if (get_page_state(block, page) == PAGE_USING) {
                add_block_using_page(block);
            }
        }
        if (block_state_index[block].using_page == 0) {
            ret = ftl_erase(ubi, block, 1);
            if (ret) {
                return ret;
            }
            for (page = 0; page < fip->ftl_page_num; page++) {
                set_page_state(block, page, PAGE_EMPTY);
            }
            kfifo_in(&empty_block_fifo, &block, sizeof(int));
        }
    }

    /*
     * find the last write block
     */
    if (cur_offset.block == FTL_NOMAP) {
        cur_offset.block = get_first_empty_block();
        cur_offset.page = -1;
        block_id++;
    }else if (need_rewrite) {
        ret = ftl_rescure(ubi, fip, cur_offset.block, rewrite_page);
        if (ret) {
            return ret;
        }
    }
    full_state = 0;
    for(i = 0; i < fip->page_sector_num; i++){
        int mask = 1<<i;
        full_state |= mask;
    }
    check_count = 0;
    return 0;
}
/**
 * ftl init
 */
int ftl_core_io_init(struct ubi_volume_desc *ubi, 
        int leb_num, 
        int leb_page_num,
        int leb_page_size,
        int sector_num)
{
    int ret;

    if (likely(block_state_index != NULL)) {
        return -EBUSY;
    }

    ftl_vdbg("leb num: %d, page num: %d, page size: %d, sector num %d",
            leb_num, leb_page_num, leb_page_size, sector_num);

    block_id = 0;

    fip = (struct uftl_info *)kmalloc(sizeof(struct uftl_info), GFP_KERNEL);
    if (unlikely(fip == NULL)) {
        return -ENOMEM;
    }
    fip->leb_num = leb_num - FTL_LOG_BLOCK_NUM;
    fip->leb_page_num = leb_page_num;
    fip->leb_page_size = leb_page_size;
    fip->ftl_page_num = leb_page_num - LOG_PAGE_NUM_PER_BLOCK;
    fip->page_sector_num = leb_page_size / SECTOR_SIZE - PAGE_LOG_SECTOR;
    /*
     * NOTICE
     * This is a huge bug if you do not add one if sector_num mod page_sector_num != 0
     */
    fip->virtual_sector_num = sector_num / fip->page_sector_num + 1;

    ret = alloc_sector_index(fip->virtual_sector_num);
    if (ret) {
        goto ERR1;
    }

    ret = alloc_sector_id(fip->virtual_sector_num);
    if (ret) {
        goto ERR2;
    }

    ret = alloc_block_state_index(fip->leb_num, fip->ftl_page_num);
    if (ret) {
        goto ERR3;
    }

    alloc_page_buf_pool();

    block_log_page = kmalloc(fip->leb_page_size, GFP_KERNEL);
    tmp_page_buf = kmalloc(fip->leb_page_size, GFP_KERNEL);

    if (page_buf_pool == NULL 
            || block_log_page == NULL
            || tmp_page_buf == NULL){
        goto ERR4;
    }

    ret = create_index(ubi);
    if (ret) {
        goto ERR4;
    }

    ret = init_status(ubi);
    if (ret) {
        goto ERR4;
    }

    free_sector_id();
    return 0;

ERR4:
    free_block_state_index(fip->leb_num);
ERR3:
    free_sector_id();
ERR2:
    free_sector_index();
ERR1:
    kfree(fip);
    return ret; 
}

/*
 * set buffer state
 */
static void set_page_buf_state(int buf_pos, int sector_pos){
    int mask = 1 << sector_pos;
    page_buf_pool[buf_pos].sector_state |= mask;
}
/*
 * get buffer state
 */
static int get_page_buf_state(int buf_pos, int sector_pos){
    int mask = 1 << sector_pos;
    return page_buf_pool[buf_pos].sector_state & mask;
}

static int is_sector_in_buf(long sector){
    int i;
    int virtual_sector = sector / fip->page_sector_num;

    for (i = 0; i < BUF_NUM; i++) {
        if (virtual_sector == page_buf_pool[i].sector) {
            return i;
        }
    }
    return -1;
}

static int get_new_buf(void){
    int i;
    for(i = 0; i < BUF_NUM; i++){
        if(page_buf_pool[i].sector == -1){
            return i;
        }
    }
    return -1;
}

/*
 * get new empty page to write
 */
static int get_new_page(void){
    cur_offset.page++;
    if (cur_offset.page == fip->ftl_page_num) {
        cur_offset.block = get_first_empty_block();
        cur_offset.page = 0;
		block_id++;
    }
    return cur_offset.block * fip->ftl_page_num + cur_offset.page;
}

static int get_first_empty_block(void){
    int block, len;
    len = kfifo_out(&empty_block_fifo, &block, sizeof(int));
    if (len == sizeof(int)) {
        return block;
    }else{
        return -1;
    }
}

static int get_empty_block_num(void){
    return kfifo_len(&empty_block_fifo)/sizeof(int);
}

/*
 * return the using page number of the block.
 * if the block have empty page will return the max number to avoid it to be recycle.
 */
static int get_block_using_page_num(int block){
    if ((block == cur_offset.block 
            && get_page_state(block, fip->ftl_page_num-1) == PAGE_EMPTY)
            || get_page_state(block, 0) == PAGE_EMPTY){
        return fip->ftl_page_num;
    }else{
        return block_state_index[block].using_page;
    }
}
static void add_block_using_page(int block){
    block_state_index[block].using_page++;
}
static void del_block_using_page(int block){
    block_state_index[block].using_page--;
}
static void clear_block_using_page(int block){
    block_state_index[block].using_page = 0;
}

/*
 * write one buf to nand
 */
static int sync_one_buf(struct ubi_volume_desc *ubi, int buf_pos){
    int i, new_index, old_index, ret = 0;
    int index, block, page;
    if (0 == page_buf_pool[buf_pos].sector_state){
        goto end;
    }
    /*
     * if the page buf is not read from nand and not all sectors 
     * has been write, will read origin data from nand and merge
     */
    if (page_buf_pool[buf_pos].sector_state != full_state
            && !page_buf_pool[buf_pos].read
            && get_sector_index(page_buf_pool[buf_pos].sector) != FTL_NOMAP)
    {
        index = get_sector_index(page_buf_pool[buf_pos].sector);
        block = index/fip->ftl_page_num;
        page = index%fip->ftl_page_num;
        ret = ftl_read(ubi, fip, block, page, tmp_page_buf);
        if (ret) {
            return ret;
        }
        for (i = 0; i < fip->page_sector_num; i++) {
            if (!get_page_buf_state(buf_pos, i)) {
                memcpy(page_buf_pool[buf_pos].data_buf+SECTOR_SIZE*i,
                        tmp_page_buf+SECTOR_SIZE*i, SECTOR_SIZE);
            }
        }
    }
    /*
     * write data to nand
     */
    new_index = get_new_page();
    if (new_index == -1) {
        return -EIO;
    }
    /*
     * TODO log the page info in the last "sector"
     * if I have a new method, I will del it
     */
    do{
        struct page_log_info page_log_cur;
        page_log_cur.magic = magic_num;
        page_log_cur.sector = page_buf_pool[buf_pos].sector;
        page_log_cur.id = block_id;
        memcpy(page_buf_pool[buf_pos].data_buf+fip->page_sector_num*SECTOR_SIZE,
                &page_log_cur, sizeof(page_log_cur));
    }while(0);
    /******end*********/
    block = new_index/fip->ftl_page_num;
    page = new_index%fip->ftl_page_num;
    ret = ftl_write(ubi, fip, block, page, page_buf_pool[buf_pos].data_buf); 
    if (ret) {
        return ret;
    }
    memcpy(block_log_page+sizeof(struct log_info)+sizeof(int)*page,
                &page_buf_pool[buf_pos].sector, sizeof(int));
    if (page == fip->ftl_page_num - 1) {
        struct log_info log_cur;
        log_cur.magic = magic_num;
        log_cur.id = block_id;
        memcpy(block_log_page, &log_cur, sizeof(struct log_info));
        ret = ftl_write(ubi, fip, block, 
                fip->ftl_page_num, block_log_page);
        if (ret) {
            return ret;
        }
    }
    old_index = get_sector_index(page_buf_pool[buf_pos].sector);
    set_page_state(block, page, PAGE_USING);
    add_block_using_page(block);
    set_sector_index(page_buf_pool[buf_pos].sector, new_index);

    /*
     * if the old page locate block has no using page will be recycle
     */
    if (old_index != FTL_NOMAP) {
        block = old_index/fip->ftl_page_num;
        page = old_index%fip->ftl_page_num;
        set_page_state(block, page, PAGE_DISCARD);
        del_block_using_page(block);
        if (get_block_using_page_num(block) == 0) {
            ret = scrub_block(ubi, block);
            if (ret) {
                return ret;
            }
        }
    }

    check_count++;
    if (check_count > fip->ftl_page_num
            && get_empty_block_num() < LOW_BLOCK_NUM){
        ret = garbage_checking(ubi, GC_BLOCK_NUM);
        if (ret) {
            return ret;
        }
        check_count = 0;
    }

    
end:
    page_buf_pool[buf_pos].sector = FTL_NOMAP;
    page_buf_pool[buf_pos].read = 0;
    page_buf_pool[buf_pos].sector_state = 0;
    page_buf_pool[buf_pos].jiffies = jiffies;

    return 0;
}

/*
 * sync one buf
 */
static int flush_one_buf(struct ubi_volume_desc *ubi){
    int i, ret = 0, buf_pos = 0;
    unsigned long jiffies_tmp = jiffies;
    for (i = 0; i < BUF_NUM; i++) {
        if (page_buf_pool[buf_pos].jiffies < jiffies_tmp) {
            jiffies_tmp = page_buf_pool[i].jiffies;
            buf_pos = i;
        }
    }
    ret = sync_one_buf(ubi, buf_pos);
    return ret;
}


/*
 * recycle block
 */
static int scrub_block(struct ubi_volume_desc *ubi, int block){
    int page, new_index;
    int sector, ret = 0;
    int new_block, new_page;
    if (block_state_index[block].using_page == 0) {
        goto end;
    }
    ftl_vdbg("recycle block: %d, page_using %d", block, block_state_index[block].using_page);
    for (page = 0; page < fip->ftl_page_num; page++) {
        if (get_page_state(block, page) == PAGE_USING) {
            ret = ftl_read(ubi, fip, block, page, tmp_page_buf);
            if (ret) {
                ftl_dbg("read leb %d:%d failed, skip this page", block, page);
                continue;
            }
            sector = ((struct page_log_info *)
                    (tmp_page_buf + fip->page_sector_num * SECTOR_SIZE))->sector;
            new_index = get_new_page();
            if (new_index < 0) {
                return -EIO;
            }
            set_sector_index(sector, new_index);

            new_block = new_index/fip->ftl_page_num;
            new_page = new_index%fip->ftl_page_num;
            set_page_state(new_block, new_page, PAGE_USING);
            add_block_using_page(new_block);

            /*
             * TODO
             * update id info from log sector in the sector
             */
            ((struct page_log_info *)
                (tmp_page_buf+fip->page_sector_num*SECTOR_SIZE))->id
                = block_id;
            /**************end************/
            ret = ftl_write(ubi, fip, new_block, new_page, tmp_page_buf);
            if (ret) {
                return ret;
            }
            /* holy shit */
            *(int *)(block_log_page+sizeof(struct log_info)+sizeof(int)*new_page) = sector;

            if (new_page == fip->ftl_page_num - 1) {
                struct log_info log_cur;
                log_cur.magic = magic_num;
                log_cur.id = block_id;
                *(struct log_info *)block_log_page = log_cur;
                ret = ftl_write(ubi, fip, new_block, 
                        fip->ftl_page_num, block_log_page);
                if (ret) {
                    return ret;
                }
            }
        }
    }
end:
    ret = ftl_erase(ubi, block, 1);
    if (ret) {
        return ret;
    }
    kfifo_in(&empty_block_fifo, &block, sizeof(int));
    for (page = 0; page < fip->ftl_page_num; page++) {
        set_page_state(block, page, PAGE_EMPTY);
    }
    clear_block_using_page(block);
    ftl_vdbg("recycle %d, ok",block);
    return 0;
}

static int garbage_checking(struct ubi_volume_desc *ubi, int low_block_num){
    int block, ret = 0;
    ftl_dbg("starting gc...");
    while (get_empty_block_num() < low_block_num) {
        ftl_vdbg("empty %d", get_empty_block_num());
        int min_using_page = fip->ftl_page_num;
        int block_gc = -1;
        int n;
        for (block = 0; block < fip->leb_num; block++) {
            n = get_block_using_page_num(block);
            if (n < min_using_page) {
                min_using_page = n;
                block_gc = block;
            }
        }
        if (block_gc == -1){
            BUG();
            return -EIO;
        }
        ret = scrub_block(ubi, block_gc);
        if (ret) {
            return ret;
        }
    }
    ftl_dbg("gc finish.");
    return 0;
}

int ftl_core_io_read(struct ubi_volume_desc *ubi, long sector, char *buf){
    int buf_pos = is_sector_in_buf(sector);
    int ret = 0;
    int block, page, index;
    int virtual_sector = sector/fip->page_sector_num;

    add_read_sector();

    if (buf_pos == -1) {
        if (get_sector_index(virtual_sector) == FTL_NOMAP) {
            return 0;
        }
        buf_pos = get_new_buf();
        if (buf_pos == -1) {
            ret = flush_one_buf(ubi);
            if (ret) {
                return ret;
            }
            buf_pos = get_new_buf();
            if (buf_pos == -1) {
                return -EIO;
            }
        }
        page_buf_pool[buf_pos].sector = virtual_sector;
        page_buf_pool[buf_pos].read = 1;
        page_buf_pool[buf_pos].sector_state = 0;
        index = get_sector_index(virtual_sector);
        block = index/fip->ftl_page_num; 
        page = index%fip->ftl_page_num;
        ret = ftl_read(ubi, fip, block, page, 
                page_buf_pool[buf_pos].data_buf);
        if (ret) {
            return ret;
        }
        memcpy(buf, page_buf_pool[buf_pos].data_buf+sector%fip->page_sector_num*SECTOR_SIZE, SECTOR_SIZE);
        page_buf_pool[buf_pos].jiffies = jiffies;
    }else{
        if (!get_page_buf_state(buf_pos, sector%fip->page_sector_num)
                && !page_buf_pool[buf_pos].read
                && get_sector_index(virtual_sector) != FTL_NOMAP){
            int i;
            index = get_sector_index(virtual_sector);
            block = index/fip->ftl_page_num; 
            page = index%fip->ftl_page_num;
            ret = ftl_read(ubi, fip, block, page, tmp_page_buf);
            if (ret) {
                return ret;
            }
            for (i = 0; i < fip->page_sector_num; i++) {
                if (!get_page_buf_state(buf_pos, i)) {
                    memcpy(page_buf_pool[buf_pos].data_buf+SECTOR_SIZE*i,
                            tmp_page_buf+SECTOR_SIZE*i, SECTOR_SIZE);
                }
            }
            page_buf_pool[buf_pos].read = 1;
        }
        memcpy(buf, page_buf_pool[buf_pos].data_buf+sector%fip->page_sector_num*SECTOR_SIZE, SECTOR_SIZE);
        page_buf_pool[buf_pos].jiffies = jiffies;
    }
    return 0;
}
int ftl_core_io_write(struct ubi_volume_desc *ubi, long sector, char *buf){
    int buf_pos;
    int ret = 0;
    int virtual_sector = sector/fip->page_sector_num;

    add_write_sector();

    buf_pos = is_sector_in_buf(sector);

    if (buf_pos == -1) {
        buf_pos = get_new_buf();
        if (buf_pos == -1){
            ret = flush_one_buf(ubi);
            if (ret) {
                return ret;
            }
            buf_pos = get_new_buf();
            if (buf_pos == -1){
                return -EIO;
            }
        }
        page_buf_pool[buf_pos].sector = virtual_sector;
        page_buf_pool[buf_pos].sector_state = 0;
        memcpy(page_buf_pool[buf_pos].data_buf+sector%fip->page_sector_num*SECTOR_SIZE, buf, SECTOR_SIZE);
        set_page_buf_state(buf_pos, sector%fip->page_sector_num);
        page_buf_pool[buf_pos].jiffies = jiffies;
    }else{
        memcpy(page_buf_pool[buf_pos].data_buf+sector%fip->page_sector_num*SECTOR_SIZE, buf, SECTOR_SIZE);
        set_page_buf_state(buf_pos, sector%fip->page_sector_num);
        page_buf_pool[buf_pos].jiffies = jiffies;
    }
    return 0;
}

int ftl_core_io_queue_flush(struct ubi_volume_desc *ubi){
    int buf_pos;
    int need_sync_nand = 0;
    for (buf_pos = 0; buf_pos < BUF_NUM; buf_pos++) {
        if (page_buf_pool[buf_pos].sector == FTL_NOMAP
                || page_buf_pool[buf_pos].sector_state == 0){
            continue;
        }else if (page_buf_pool[buf_pos].jiffies + NEED_FLUSH_TIME < jiffies){
            sync_one_buf(ubi, buf_pos);
            need_sync_nand = 1;
        }
    }
    return need_sync_nand;
}

int ftl_core_io_all_flush(struct ubi_volume_desc *ubi){
    int buf_pos;
    int ret;

    for (buf_pos = 0; buf_pos < BUF_NUM; buf_pos++) {
        ret = sync_one_buf(ubi, buf_pos);
    }
    return ret;
}

