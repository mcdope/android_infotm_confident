

#include <linux/crc32.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "ubi.h"

#if CONFIG_MTD_UBI_MERGE

int ubi_transfer_peb(const struct ubi_device *ubi, int pnum, int *ppnum, int *ppeb_size){

	struct mtd_info *mtd = ubi->mtd;

	*ppnum = pnum * ubi->merged_peb_count;
	*ppeb_size = ubi->peb_size / ubi->merged_peb_count;

	return 0;
}

#endif

