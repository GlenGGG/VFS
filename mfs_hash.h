#ifndef _MFS_HASH_H
#include <stdint.h>
#include "mfs.h"

/*
 * mfs_idx
 *+1+------10-------+---------------21-----------+
 *|0|   block_id    |            file_id         |
 *+----------------------------------------------+
 *
 * The top one bit represents whether the hash item has been taken.
 *
 * */


#define IS_IN_TABLE(mfs_idx) (mfs_idx&0x80000000)
#define GET_BLOCK_ID(mfs_idx) (mfs_idx>>21&(0x03ff))
#define GET_FILE_ID(mfs_idx) (mfs_idx&0x01fffff)
#define SET_IN_FILE(mfs_idx) \
    do \
{ \
    mfs_idx|=0x80000000; \
}while(0)

#define TO_MFS_IDX(block_id, file_id) \
    (((uint32_t)block_id<<21)|((uint32_t)file_id))

#define HASHBLOCK_HEAD_LEN ((ARG_LENGTH_IN_FILE*2+DIR_MAX_LEN*3))
#define HASHCOLLISIONTABLE_HEAD_LEN ((ARG_LENGTH_IN_FILE+DIR_MAX_LEN))
#define FILEDESC_SIZE (sizeof(mfs_idx)+NAME_MAX_LEN)
#define HASHBLOCK_NEXT_IDX_OFFSET (DIR_MAX_LEN*3+ARG_LENGTH_IN_FILE)
#define HASHCOLLISION_NEXT_IDX_OFFSET (DIR_MAX_LEN)

#define CHAR_TO_UINT32(buf) (((((uint32_t)buf[0])<<24)&0XFF000000)|\
        ((((uint32_t)buf[1])<<16)&0X00FF0000)|\
        ((((uint32_t)buf[2])<<8)&0X0000FF00)|\
        ((((uint32_t)buf[3]))&0X000000FF))
#define UINT32_TO_CHAR(buf, num) \
    do\
{\
    buf[0]=(uint32_t)((num&0XFF000000)>>24);\
    buf[1]=(uint32_t)((num&0X00FF0000)>>16);\
    buf[2]=(uint32_t)((num&0X0000FF00)>>8);\
    buf[3]=(uint32_t)((num&0X000000FF));\
}while(0)


typedef uint32_t mfs_idx;

typedef struct FileDesc {
    mfs_idx idx;
    char file_name[NAME_MAX_LEN];
} FileDescIdx;

typedef struct MfsHashBlock {
    char dir[DIR_MAX_LEN];
    char sec_dir[DIR_MAX_LEN];
    char collision_dir[DIR_MAX_LEN];
    uint32_t total_item;
    uint32_t next_file_desc_idx;
    pthread_mutex_t mutex;
} MfsHashBlock;

typedef struct MfsHashCollisionTable {
    char dir[DIR_MAX_LEN];
    uint32_t next_file_desc_idx;
    pthread_mutex_t mutex;
} MfsHashCollisionTable;

int32_t MfsHashBlockOpen(MfsHashBlock* hash_block, uint32_t total_item, char* dir);
int32_t MfsHashInit(MfsHashBlock* hash_table, uint32_t total_item, char* dir);
int32_t WriteMfsHashBlock(int32_t fd, MfsHashBlock* hash_table);
int32_t WriteMfsHashBlockHeader(int32_t fd, MfsHashBlock* hash_table);
int32_t WriteMfsHashCollisionTable(int32_t fd, MfsHashCollisionTable* col_table);
int32_t ReadMfsHashBlockHeader(char* dir, MfsHashBlock* hash_block);
int32_t ReadMfsHashCollisionTableHeader(
    char* dir, MfsHashCollisionTable* col_table);

int32_t MfsHashPut(char* file_name, FileInfo* file_info, MfsHashBlock* hash_block);
int32_t MfsHashFind(mfs_idx *mfsidx, char* file_name, MfsHashBlock* hash_block);

#endif
