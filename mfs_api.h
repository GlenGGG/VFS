#ifndef _MFS_API_H
#define _MFS_API_H
#include "mfs_hash.h"

int32_t MfsHashOpen(SuperBlock* super_block,
                    char* file_name, FileInfo** file_info,
                    int32_t flag, MfsHashBlock* hash_block, uint32_t len);


#endif
