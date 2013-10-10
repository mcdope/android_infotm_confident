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

int ftl_erase(struct ubi_volume_desc *ubi, int lnum, int unmap)
{ 
    if (unmap) {
        return ubi_leb_unmap(ubi, lnum);
    }else{
        return ubi_leb_erase(ubi, lnum);
    }
}
int ftl_read(struct ubi_volume_desc *ubi, 
        struct uftl_info *fip,
        int lnum, int page, char *buf)
{
    add_read_leb();
    return ubi_leb_read(ubi, lnum, buf, 
            fip->leb_page_size*page,
            fip->leb_page_size, 0);
}
int ftl_write(struct ubi_volume_desc *ubi, 
        struct uftl_info *fip,
        int lnum, int page, char *buf)
{
    add_write_leb();
    return ubi_leb_write(ubi, lnum, buf,
            fip->leb_page_size*page,
            fip->leb_page_size, UBI_UNKNOWN);
}

int ftl_rescure(struct ubi_volume_desc *ubi, 
        struct uftl_info *fip, 
        int block, int till_page){
    int rewrite_size = fip->leb_page_size * till_page;
    char *rewrite_buf = kmalloc(rewrite_size, GFP_KERNEL);
    int ret;
    ftl_dbg("preparing rewrite block %d till page %d", block, till_page);
    if (rewrite_buf == NULL) {
        return -ENOMEM;
    }
    ret = ubi_leb_read(ubi, block, rewrite_buf, 0, rewrite_size, 0);
    if (ret) {
        return ret;
    }
    ret = ubi_leb_change(ubi, block, rewrite_buf, rewrite_size, UBI_SHORTTERM);
    if (ret) {
        return ret;
    }
    ftl_dbg("rewrite successed");
    kfree(rewrite_buf);
    return 0;
}

