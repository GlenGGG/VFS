#include "mfs_hash.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t BKDRHash(char* str)
{
    uint32_t seed = 131313;
    uint32_t hash = 0;
    while (*str) {
        hash = hash * seed + (*str++);
    }
    return hash;
}
static uint32_t APHash(char* str)
{
    uint32_t hash = 0xAAAAAAAA;
    uint32_t i = 0;
    uint32_t length = strlen(str);

    for (i = 0; i < length; ++str, ++i) {
        hash ^= ((i & 1) == 0) ? ((hash << 7) ^ (*str) * (hash >> 3))
                               : (~((hash << 11) + ((*str) ^ (hash >> 5))));
    }

    return hash;
}

static inline uint32_t FirstHash(char* str) { return APHash(str); }

static inline uint32_t SecHash(char* str) { return BKDRHash(str); }

int32_t MfsHashBlockOpen(
    MfsHashBlock* hash_block, uint32_t total_item, char* dir)
{
    if (access(dir, F_OK) < 0) {
        return MfsHashInit(hash_block, total_item, dir);
    } else {
        return ReadMfsHashBlockHeader(dir, hash_block);
    }
}
int32_t MfsHashInit(MfsHashBlock* hash_block, uint32_t total_item, char* dir)
{
    if (strlen(dir) + strlen("_sec") >= DIR_MAX_LEN)
        return -1;
    sprintf(hash_block->dir, dir, strlen(dir));
    pthread_mutex_init(&(hash_block->mutex), NULL);
    hash_block->next_file_desc_idx = 0;
    hash_block->total_item = total_item;
    MfsHashBlock sec_block;
    sprintf(sec_block.dir, "%s%s", hash_block->dir, "_sec");
    sprintf(sec_block.sec_dir, "#");
    sec_block.next_file_desc_idx = 0;
    sec_block.total_item = hash_block->total_item / 1024;
    pthread_mutex_init(&(sec_block.mutex), NULL);
    sprintf(hash_block->sec_dir, "%s", sec_block.dir);

    MfsHashCollisionTable col_table;
    col_table.next_file_desc_idx = 0;
    sprintf(col_table.dir, "%s%s", hash_block->dir, "_col");
    sprintf(sec_block.collision_dir, "%s", col_table.dir);
    sprintf(hash_block->collision_dir, "%s", col_table.dir);
    pthread_mutex_init(&(col_table.mutex), NULL);

    int32_t fd = 0;
    pthread_mutex_lock(&(hash_block->mutex));
    if ((fd = open(hash_block->dir, O_CREAT | O_WRONLY, 0644)) >= 0) {
        WriteMfsHashBlock(fd, hash_block);
        close(fd);
    } else {
        pthread_mutex_unlock(&(hash_block->mutex));
        return -1;
    }
    pthread_mutex_unlock(&(hash_block->mutex));

    pthread_mutex_lock(&(sec_block.mutex));
    if ((fd = open(sec_block.dir, O_CREAT | O_WRONLY, 0644)) >= 0) {
        WriteMfsHashBlock(fd, &sec_block);
        close(fd);
    } else {
        pthread_mutex_unlock(&(sec_block.mutex));
        return -1;
    }
    pthread_mutex_unlock(&(sec_block.mutex));

    pthread_mutex_lock(&(col_table.mutex));
    if ((fd = open(col_table.dir, O_CREAT | O_WRONLY, 0644)) >= 0) {
        WriteMfsHashCollisionTable(fd, &col_table);
        close(fd);
    } else {
        pthread_mutex_unlock(&(col_table.mutex));
        return -1;
    }
    pthread_mutex_unlock(&(col_table.mutex));

    return 0;
}

int32_t WriteMfsHashBlock(int32_t fd, MfsHashBlock* hash_block)
{
    char buf[BUFSIZE * 2];
    int32_t offset = 0;
    sprintf(buf, "%s", hash_block->dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%s", hash_block->sec_dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%s", hash_block->collision_dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%d", hash_block->total_item);
    pwrite(fd, buf, strlen(buf), offset);
    offset += ARG_LENGTH_IN_FILE;
    sprintf(buf, "%d", hash_block->next_file_desc_idx);
    pwrite(fd, buf, strlen(buf), offset);
    offset += ARG_LENGTH_IN_FILE;

    /*make a hole for hash items*/
    offset += (sizeof(uint32_t) * hash_block->total_item);
    pwrite(fd, "#", 1, offset);
    return 0;
}


int32_t WriteMfsHashBlockHeader(int32_t fd, MfsHashBlock* hash_block)
{
    char buf[BUFSIZE * 2];
    int32_t offset = 0;
    sprintf(buf, "%s", hash_block->dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%s", hash_block->sec_dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%s", hash_block->collision_dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%d", hash_block->total_item);
    pwrite(fd, buf, strlen(buf), offset);
    offset += ARG_LENGTH_IN_FILE;
    sprintf(buf, "%d", hash_block->next_file_desc_idx);
    pwrite(fd, buf, strlen(buf), offset);
    offset += ARG_LENGTH_IN_FILE;
    return 0;
}

int32_t WriteMfsHashCollisionTable(int32_t fd, MfsHashCollisionTable* col_table)
{
    char buf[BUFSIZE * 2];
    int32_t offset = 0;
    sprintf(buf, "%s", col_table->dir);
    pwrite(fd, buf, strlen(buf), offset);
    offset += DIR_MAX_LEN;
    sprintf(buf, "%d", col_table->next_file_desc_idx);
    pwrite(fd, buf, strlen(buf), offset);
    offset += ARG_LENGTH_IN_FILE;
    return 0;
}

int32_t ReadMfsHashBlockHeader(char* dir, MfsHashBlock* hash_block)
{
    char buf[BUFSIZE * 2];
    int32_t offset = 0;
    int32_t fd = 0;
    pthread_mutex_init(&(hash_block->mutex), NULL);
    if ((fd = open(dir, O_RDONLY)) >= 0) {
        pread(fd, buf, HASHBLOCK_HEAD_LEN, 0);
        close(fd);
    } else {
        return -1;
    }
    sscanf(buf + offset, "%s", hash_block->dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%s", hash_block->sec_dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%s", hash_block->collision_dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%u", &(hash_block->total_item));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%u", &(hash_block->next_file_desc_idx));
    return 0;
}

int32_t ReadMfsHashCollisionTableHeader(
    char* dir, MfsHashCollisionTable* col_table)
{
    char buf[BUFSIZE * 2];
    int32_t offset = 0;
    int32_t fd = 0;
    pthread_mutex_init(&(col_table->mutex), NULL);
    if ((fd = open(dir, O_RDONLY)) >= 0) {
        pread(fd, buf, HASHCOLLISIONTABLE_HEAD_LEN, 0);
        close(fd);
    } else {
        return -1;
    }
    sscanf(buf + offset, "%s", col_table->dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%u", &(col_table->next_file_desc_idx));
    return 0;
}

static int32_t MfsHashPutInBlock(char* file_name, FileInfo* file_info,
    MfsHashBlock* hash_block, uint32_t (*hash)(char* str))
{
    int32_t fd = 0;
    bool hash_block_dirt = false;
    if ((fd = open(hash_block->dir, O_RDWR)) >= 0) {
        uint32_t offset = 0;
        uint32_t firt_hash_pos = hash(file_name) % (hash_block->total_item);
        unsigned char buf[BUFSIZE * 2];
        offset = HASHBLOCK_HEAD_LEN;
        pread(fd, buf, sizeof(uint32_t),
            offset + (sizeof(uint32_t) * firt_hash_pos));
        uint32_t file_desc_idx = CHAR_TO_UINT32(buf);

        /* if file_desc_idx ==0, means it's empty.
         * To avoid confliction, add file_desc_idx by 1 when saving in file.*/
        if (file_desc_idx == 0) {
            file_desc_idx = hash_block->next_file_desc_idx + 1;
            ++(hash_block->next_file_desc_idx);
            hash_block_dirt = true;
            UINT32_TO_CHAR(buf, file_desc_idx);
            file_desc_idx -= 1;
            pwrite(fd, buf, sizeof(uint32_t),
                offset + (sizeof(uint32_t) * (firt_hash_pos)));
        } else {
            file_desc_idx -= 1;
        }
        offset += (sizeof(uint32_t) * hash_block->total_item);
        memset(buf, 0, 5);
        offset += (file_desc_idx * FILEDESC_SIZE);
        pread(fd, buf, FILEDESC_SIZE, offset);
        mfs_idx mfsidx;
        mfsidx = CHAR_TO_UINT32(buf);
        if (mfsidx == 0 || buf[0] == '#') {
            memset(buf, 0, 5);
            assert(buf[4] == 0);
            mfsidx = TO_MFS_IDX(
                file_info->block->block_id, file_info->file_info_id);
            SET_IN_FILE(mfsidx);
            UINT32_TO_CHAR(buf, mfsidx);
            sprintf((char*)(buf + sizeof(mfsidx)), "%s", file_name);
            int32_t rr = 0;
            rr = pwrite(fd, buf, FILEDESC_SIZE, offset);
            assert(rr != 0 && rr != -1);
            /* unsigned char buff[BUFSIZE] = { "\0" };
             * rr = pread(fd, buff, FILEDESC_SIZE, offset);
             * assert(strcmp((char*)buff, (char*)buf) == 0); */
            if (hash_block_dirt) {
                WriteMfsHashBlockHeader(fd, hash_block);
            }
            close(fd);
            return 0;
        } else if (strcmp((const char*)(buf + sizeof(uint32_t)), file_name)
            == 0) {
            assert((file_info->file_info_id == GET_FILE_ID(mfsidx)
                && file_info->block->block_id == GET_BLOCK_ID(mfsidx)));
            close(fd);
            return 0;
        } else {
            close(fd);
            return -1;
        }
    } else
        return -1;
}

static int32_t MfsHashPutInColBlock(
    char* file_name, FileInfo* file_info, MfsHashCollisionTable* col_table)
{
    int32_t fd = 0;
    if ((fd = open(col_table->dir, O_RDWR)) >= 0) {
        uint32_t offset = 0;
        unsigned char buf[BUFSIZE * 2];
        offset = HASHCOLLISIONTABLE_HEAD_LEN;
        offset += (col_table->next_file_desc_idx) * (FILEDESC_SIZE);
        ++(col_table->next_file_desc_idx);
        mfs_idx mfsidx;
        mfsidx
            = TO_MFS_IDX(file_info->block->block_id, file_info->file_info_id);
        SET_IN_FILE(mfsidx);
        UINT32_TO_CHAR(buf, mfsidx);
        sprintf((char*)(buf + sizeof(mfsidx)), "%s", file_name);
        pwrite(fd, buf, FILEDESC_SIZE, offset);
        WriteMfsHashCollisionTable(fd, col_table);
        close(fd);
    } else {
        return -1;
    }
    return 0;
}

int32_t MfsHashPut(
    char* file_name, FileInfo* file_info, MfsHashBlock* hash_block)
{
    if (MfsHashPutInBlock(file_name, file_info, hash_block, FirstHash) >= 0)
        return 0;
    else {
        MfsHashBlock sec_block;
        assert(ReadMfsHashBlockHeader(hash_block->sec_dir, &sec_block) >= 0);
        if (MfsHashPutInBlock(file_name, file_info, &sec_block, SecHash) >= 0)
            return 0;
        else {
            MfsHashCollisionTable col_table;
            assert(ReadMfsHashCollisionTableHeader(
                       hash_block->collision_dir, &col_table)
                >= 0);
            assert(MfsHashPutInColBlock(file_name, file_info, &col_table) >= 0);
            return 0;
        }
    }
}

int32_t MfsHashFindInBlock(mfs_idx* mfsidx, char* file_name,
    MfsHashBlock* hash_block, uint32_t (*hash)(char* str))
{
    int32_t fd = 0;
    if ((fd = open(hash_block->dir, O_RDONLY)) >= 0) {
        uint32_t offset = 0;
        uint32_t firt_hash_pos = hash(file_name) % (hash_block->total_item);
        unsigned char buf[BUFSIZE * 2] = { "\0" };
        offset = HASHBLOCK_HEAD_LEN;
        pread(fd, buf, sizeof(uint32_t),
            offset + (sizeof(uint32_t) * firt_hash_pos));
        uint32_t file_desc_idx = CHAR_TO_UINT32(buf);

        /* if file_desc_idx ==0, means it's empty.
         * To avoid confliction, add file_desc_idx by 1 when saving.*/
        offset += (sizeof(uint32_t) * hash_block->total_item);
        if (file_desc_idx == 0) {
            return -1;
        } else {
            file_desc_idx -= 1;
        }
        memset(buf, 0, 5);
        offset += (file_desc_idx * FILEDESC_SIZE);
        pread(fd, buf, FILEDESC_SIZE, offset);
        *mfsidx = CHAR_TO_UINT32(buf);
        if (*mfsidx == 0) {
            close(fd);
            return -1;
        } else if (strcmp((const char*)(buf + sizeof(uint32_t)), file_name)
            == 0) {
            close(fd);
            return 0;
        } else {
            close(fd);
            return -1;
        }
    } else
        return -1;
}

/* Find file linearly */
int32_t MfsHashFindInColBlock(
    mfs_idx* mfsidx, char* file_name, MfsHashCollisionTable* col_table)
{
    int32_t fd = 0;
    if ((fd = open(col_table->dir, O_RDONLY)) >= 0) {
        uint32_t offset = 0;
        unsigned char buf[BUFSIZE * 2];
        uint32_t read_chunk = (BUFSIZE * 2) / FILEDESC_SIZE * FILEDESC_SIZE;
        char* found_name = "";
        offset = HASHCOLLISIONTABLE_HEAD_LEN;
        do {
            int nread = 0;
            int read_idx = 0;
            do {
                nread = pread(fd, buf + read_idx, read_chunk - read_idx,
                    offset + read_idx);
                read_idx += nread;
            } while (nread != -1 && nread != 0);
            if (read_idx == 0) {
                close(fd);
                return -1;
            }
            offset += read_idx;

            int32_t in_buf_offset = 0;
            while (in_buf_offset < read_idx) {
                *mfsidx = CHAR_TO_UINT32((buf + in_buf_offset));
                found_name = (char*)(buf + sizeof(uint32_t) + in_buf_offset);
                if (strcmp(found_name, file_name) == 0) {
                    close(fd);
                    return 0;
                }
                in_buf_offset += FILEDESC_SIZE;
            }
        } while (strcmp(found_name, file_name) != 0);
        close(fd);
        return -1;
    } else {
        return -1;
    }
}

int32_t MfsHashFind(mfs_idx* mfsidx, char* file_name, MfsHashBlock* hash_block)
{
    if (MfsHashFindInBlock(mfsidx, file_name, hash_block, FirstHash) >= 0)
        return 0;
    else {
        MfsHashBlock sec_block;
        assert(ReadMfsHashBlockHeader(hash_block->sec_dir, &sec_block) >= 0);
        if (MfsHashFindInBlock(mfsidx, file_name, &sec_block, SecHash) >= 0)
            return 0;
        else {
            MfsHashCollisionTable col_table;
            assert(ReadMfsHashCollisionTableHeader(
                       hash_block->collision_dir, &col_table)
                >= 0);
            if (MfsHashFindInColBlock(mfsidx, file_name, &col_table) >= 0)
                return 0;
            else
                return -1;
        }
    }
}
