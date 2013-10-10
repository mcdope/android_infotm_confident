#include "uftl.h"
/*
 * cache buf stat
 * for user to imporve speed
 */
static struct cache_stat cache_stat_cur;

ssize_t show_cache_stat(char *buf){
    int n = 0;
    n += sprintf(buf+n, "=============================\n");
    n += sprintf(buf+n, "         cache stat          \n");
    n += sprintf(buf+n, "read sectors %10d\n", cache_stat_cur.read_sector);
    n += sprintf(buf+n, "read pages %10d\n", cache_stat_cur.read_leb_page);
    n += sprintf(buf+n, "write sectors %10d\n", cache_stat_cur.write_sector);
    n += sprintf(buf+n, "write pages %10d\n", cache_stat_cur.write_leb_page);
    n += sprintf(buf+n, "=============================\n");
    return n;
}

void clear_cache_stat(void){
    memset(&cache_stat_cur, 0, sizeof(struct cache_stat));
}
void add_read_sector(void){
    cache_stat_cur.read_sector++;
}
void add_read_leb(void){
    cache_stat_cur.read_leb_page++;
}
void add_write_sector(void){
    cache_stat_cur.write_sector++;
}
void add_write_leb(void){
    cache_stat_cur.write_leb_page++;
}


