#include "mfs_api.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


/* if flag contains O_CREAT, len is needed  */
int32_t MfsHashOpen(SuperBlock* super_block, char* file_name,
    FileInfo** file_info, int32_t flag, MfsHashBlock* hash_block, uint32_t len)
{
    mfs_idx mfsidx;
    uint32_t fd = -1;
    if (MfsHashFind(&mfsidx, file_name, hash_block) < 0) {
        if (flag & O_CREAT) {
            (*file_info) = FileInfoCreate(len, super_block);
            if (MfsHashPut(file_name, *file_info, hash_block) < 0)
                return -1;
            if ((fd = MfsOpen(
                     (*file_info)->file_name, file_info, super_block, flag))
                >= 0) {
                assert(fd > 0);
                return fd;
            } else
                return -1;
        } else {
            return -1;
        }
    } else {
        char name_buf[NAME_MAX_LEN] = { "\0" };
        sprintf(name_buf, "%u-%u", GET_BLOCK_ID(mfsidx), GET_FILE_ID(mfsidx));
        if ((fd = MfsOpen(name_buf, file_info, super_block, flag)) >= 0) {
            assert(fd > 0);
            return fd;
        } else
            return -1;
    }
}
