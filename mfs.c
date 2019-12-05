#include "mfs.h"
#include <assert.h>
#include <error.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <unistd.h>

LogicBlock* LogicBlockCreate(SuperBlock* super_block, int32_t block_id)
{
    LogicBlock* block = malloc(sizeof(LogicBlock));
    block->block_id = block_id;
    block->size = super_block->block_size;
    sprintf(block->dir, "block-%d.mfs", super_block->block_num);
    block->main_free = super_block->main_size;
    block->ext_free = super_block->ext_size;
    block->main_offset = LOGICBLOCK_MAIN_OFFSET;
    block->ext_offset = block->main_offset + super_block->main_size;
    block->file_info_array_offset = block->ext_offset + super_block->ext_size;
    block->file_info_array = NULL;
    block->super_block = NULL;
    block->hole_size = 0;
    block->next_block_id = -1;
    block->next = NULL;
    block->pre = NULL;
    pthread_mutex_init(&(block->mutex), NULL);
    for (int32_t i = 0; i < LOGICBLOCK_DIRT_NUM; ++i)
        block->dirt[i] = true;
    return block;
}

FileInfo* FileInfoCreate(int32_t size, SuperBlock* super_block)
{
    FileInfo* file_info = malloc(sizeof(FileInfo));
    file_info->size = size;
    file_info->hsize = 0;
    file_info->size_in_ext = 0;
    file_info->size_in_main = size;
    file_info->in_ext_offset = -1;
    file_info->next_file_info_id = -1;
    file_info->next = NULL;
    file_info->pre = NULL;
    file_info->block = NULL;
    pthread_mutex_init(&(file_info->mutex), NULL);
    LogicBlock* block = NULL;
    for (int32_t i = 0; i < FILEINFO_DIRT_NUM; ++i)
        file_info->dirt[i] = true;
    if (block != NULL)
        pthread_mutex_lock(&(super_block->block_array->mutex));
    block = super_block->block_array;
    if (block == NULL || block->main_free <= size
        || block->file_info_num > MAX_FILE_NUM_IN_A_BLOCK) {
        if (block != NULL)
            pthread_mutex_unlock(&(super_block->block_array->mutex));
        pthread_mutex_lock(&(super_block->mutex));
        block = LogicBlockCreate(super_block, super_block->block_num);
        pthread_mutex_lock(&(block->mutex));
        AddBlockToSuperBlock(super_block, block);
        sprintf(file_info->dir, "%s", block->dir);
        sprintf(file_info->file_name, "%d-%d", block->block_id,
            block->file_info_num);
        ++(block->file_info_num);
        block->main_free -= file_info->size;
        block->dirt[2] = true;
        file_info->file_info_id = 0;
        block->file_info_array = file_info;
        file_info->in_main_offset = block->main_offset;
        file_info->block = block;
        int32_t fd;
        if ((fd = OpenBlock(block->dir, super_block->block_size, O_WRONLY))
            >= 0) {
            WriteLogicBlock(fd, block, super_block);
            WriteFileInfo(fd, block->file_info_array_offset, file_info, block,
                super_block);
            close(fd);
        } else
            perror("block open error");
        if ((fd = OpenBlock(
                 super_block->dir, super_block->block_size, O_WRONLY))
            >= 0) {
            WriteSuperBlock(fd, super_block);
            close(fd);
        } else
            perror("super open error");
        pthread_mutex_unlock(&(block->mutex));
        pthread_mutex_unlock(&(super_block->mutex));
    } else {
        sprintf(file_info->dir, "%s", block->dir);
        sprintf(file_info->file_name, "%d-%d", block->block_id,
            block->file_info_num);
        block->main_free -= file_info->size;
        block->dirt[2] = true;
        file_info->file_info_id = block->file_info_num;
        file_info->block = block;
        ++(block->file_info_num);
        if (block->file_info_num == 1) {
            block->file_info_array = file_info;
            file_info->in_main_offset = block->main_offset;
        } else {
            file_info->next = block->file_info_array;
            file_info->next_file_info_id = block->file_info_array->file_info_id;
            block->file_info_array->pre = file_info;
            block->file_info_array = file_info;
            file_info->in_main_offset = file_info->next->in_main_offset
                + file_info->next->size_in_main;
        }
        int32_t fd;
        if ((fd = OpenBlock(block->dir, super_block->block_size, O_WRONLY))
            >= 0) {
            WriteLogicBlock(fd, block, super_block);
            WriteFileInfo(fd,
                block->file_info_array_offset
                    + (FILEINFO_LEN * (block->file_info_num - 1)),
                file_info, block, super_block);
            close(fd);
        }
        pthread_mutex_lock(&(super_block->mutex));
        if ((fd = OpenBlock(
                 super_block->dir, super_block->block_size, O_WRONLY))
            >= 0) {
            WriteSuperBlock(fd, super_block);
            close(fd);
        }
        pthread_mutex_unlock(&(super_block->mutex));

        pthread_mutex_unlock(&(super_block->block_array->mutex));
    }
    return file_info;
}

FileInfo* ReadFileInfoFromBuf(char* buf, SuperBlock* super_block)
{
    int32_t offset = 0;
    FileInfo* file_info = (FileInfo*)malloc(sizeof(FileInfo));
    sscanf(buf + offset, "%s", file_info->dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%s", file_info->file_name);
    offset += NAME_MAX_LEN;
    sscanf(buf + offset, "%d", &(file_info->size));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->hsize));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->size_in_main));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->size_in_ext));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->in_ext_offset));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->in_main_offset));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->next_file_info_id));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(file_info->file_info_id));
    offset += ARG_LENGTH_IN_FILE;
    for (int32_t i = 0; i < FILEINFO_DIRT_NUM; ++i)
        file_info->dirt[i] = false;
    return file_info;
}

int32_t MfsOpen(char* file_name, FileInfo** file_info, SuperBlock* super_block,
    int32_t flag)
{
    int32_t block_id;
    int32_t file_info_id;
    sscanf(file_name, "%d-%d", &block_id, &file_info_id);
    int32_t fd = -1;
    char dir[DIR_MAX_LEN];
    char buf[BUFSIZE * 2] = { "\0" };
    sprintf(dir, "block-%d.mfs", block_id);

    /*finding the file info*/
    if ((fd = OpenBlock(dir, super_block->block_size, O_RDONLY)) >= 0) {
        int32_t nread = 0;
        int32_t read_idx = 0;
        int32_t this_file_info_offset
            = super_block->block_array->file_info_array_offset
            + file_info_id * FILEINFO_LEN;
        do {
            nread = pread(fd, buf + read_idx, FILEINFO_LEN - read_idx,
                this_file_info_offset + read_idx);
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        if (nread == -1) {
            close(fd);
            return -1;
        }
        *file_info = ReadFileInfoFromBuf(buf, super_block);
        LogicBlock* p = super_block->block_array;
        while (p->block_id != block_id)
            p = p->next;
        (*file_info)->block = p;
        pthread_mutex_init(&((*file_info)->mutex), NULL);
    } else
        return -1;

    /*return file descriptor,
     * does not support O_APPEND flag*/
    fd = OpenBlock(dir, super_block->block_size, flag & (~O_APPEND));
    return fd;
}

/* write from the beginning*/
int32_t MfsWrite(int32_t block_fd, FileInfo* file_info, char* content)
{
    return PMfsWrite(block_fd, file_info, content, 0);
}
int32_t PMfsWrite(int32_t block_fd, FileInfo* file_info, char* content,
    int32_t relative_offset)
{
    int32_t hsize = 0;
    int32_t offset = 0;
    int32_t main_left = -1;
    int32_t ext_left = -1;
    bool in_main = true;
    if (relative_offset < 0)
        return -2;
    pthread_mutex_lock(&(file_info->mutex));
    if (relative_offset < file_info->size_in_main) {
        offset = file_info->in_main_offset + relative_offset;
        main_left = file_info->size_in_main - relative_offset;
        ext_left = file_info->size_in_ext;
    } else {
        if ((relative_offset - file_info->size_in_main)
            < file_info->size_in_ext) {
            offset = file_info->in_ext_offset
                + (relative_offset - file_info->size_in_main);
            in_main = false;
            main_left = 0;
            ext_left = file_info->size_in_ext
                - (relative_offset - file_info->size_in_main);
        } else {
            pthread_mutex_unlock(&(file_info->mutex));
            return -2;
        }
    }
    int32_t nwrite = 0;
    int32_t write_idx = 0;
    int32_t cont_len = strlen(content);
    if (in_main) {
        if (main_left >= cont_len) {
            do {
                nwrite = pwrite(block_fd, content + write_idx,
                    cont_len - write_idx, offset + write_idx);
                write_idx += nwrite;
            } while (nwrite != -1 && nwrite != 0);
            if (nwrite == -1) {
                pthread_mutex_unlock(&(file_info->mutex));
                return -1;
            }
            hsize = cont_len + relative_offset;
            if (hsize > file_info->hsize) {
                file_info->hsize = hsize;
                file_info->dirt[3] = true;
                WriteFileInfo(block_fd,
                    file_info->block->file_info_array_offset
                        + (file_info->file_info_id * (FILEINFO_LEN)),
                    file_info, file_info->block, file_info->block->super_block);
            }
            pthread_mutex_unlock(&(file_info->mutex));
            return write_idx;
        } else {
            if (file_info->in_ext_offset != -1
                && main_left + ext_left < cont_len) {
                pthread_mutex_unlock(&(file_info->mutex));
                return -3;
            }
            do {
                nwrite = pwrite(block_fd, content + write_idx,
                    main_left - write_idx, offset + write_idx);
                write_idx += nwrite;
            } while (nwrite != -1 && nwrite != 0);
            if (nwrite == -1) {
                pthread_mutex_unlock(&(file_info->mutex));
                return -1;
            }
            hsize = write_idx + relative_offset;
            if (hsize > file_info->hsize) {
                file_info->hsize = hsize;
                file_info->dirt[3] = true;
            }
            cont_len -= write_idx;
            int32_t wrote_len = write_idx;
            bool extends = false;
            if (file_info->in_ext_offset != -1) {
                assert(file_info->size_in_ext != 0);
                offset = file_info->in_ext_offset;
            } else {
                pthread_mutex_lock(&(file_info->block->mutex));
                if (cont_len >= file_info->block->ext_free) {
                    pthread_mutex_unlock(&(file_info->block->mutex));
                    pthread_mutex_unlock(&(file_info->mutex));
                    return -1;
                }
                offset = file_info->block->ext_offset
                    + (file_info->block->super_block->ext_size
                          - file_info->block->ext_free);
                pthread_mutex_unlock(&(file_info->block->mutex));
                extends = true;
            }
            nwrite = 0;
            /* write to ext block */
            do {
                nwrite = pwrite(block_fd, content + write_idx,
                    cont_len - write_idx, offset + (write_idx - wrote_len));
                write_idx += nwrite;
            } while (nwrite != -1 && nwrite != 0);
            if (nwrite == -1) {
                pthread_mutex_unlock(&(file_info->mutex));
                return -1;
            }
            hsize += cont_len;
            if (hsize > file_info->hsize) {
                file_info->hsize = hsize;
                file_info->dirt[3] = true;
            }
            if (extends) {
                pthread_mutex_lock(&(file_info->block->mutex));
                file_info->block->ext_free -= cont_len;
                file_info->block->dirt[3] = true;
                file_info->in_ext_offset = offset;
                file_info->dirt[6] = true;
                file_info->size_in_ext = cont_len;
                file_info->dirt[5] = true;
                file_info->size += file_info->size_in_ext;
                file_info->dirt[2] = true;
                WriteLogicBlock(
                    block_fd, file_info->block, file_info->block->super_block);
                pthread_mutex_unlock(&(file_info->block->mutex));
                pthread_mutex_lock(&(file_info->block->super_block->mutex));
                int32_t fd = -1;
                if ((fd = OpenBlock(
                         file_info->block->super_block->dir, -1, O_RDONLY))) {
                    /* write this block's header to superblock */
                    WriteSuperBlock(fd, file_info->block->super_block);
                    close(fd);
                }
                pthread_mutex_unlock(&(file_info->block->super_block->mutex));
            }
            WriteFileInfo(block_fd,
                file_info->block->file_info_array_offset
                    + (file_info->file_info_id * (FILEINFO_LEN)),
                file_info, file_info->block, file_info->block->super_block);
            pthread_mutex_unlock(&(file_info->mutex));
            return write_idx;
        }
    } else {
        if (ext_left >= cont_len) {
            do {
                nwrite = pwrite(block_fd, content + write_idx,
                    cont_len - write_idx, offset + write_idx);
                write_idx += nwrite;
            } while (nwrite != -1 && nwrite != 0);
            if (nwrite == -1) {
                pthread_mutex_unlock(&(file_info->mutex));
                return -1;
            }
            file_info->hsize = file_info->size_in_main + write_idx;
            file_info->dirt[3] = true;
            WriteFileInfo(block_fd,
                file_info->block->file_info_array_offset
                    + (file_info->file_info_id * (FILEINFO_LEN)),
                file_info, file_info->block, file_info->block->super_block);
            pthread_mutex_unlock(&(file_info->mutex));
            return write_idx;
        } else // can't expand ext part
        {
            pthread_mutex_unlock(&(file_info->mutex));
            return -1;
        }
    }
}

int32_t MfsRead(int32_t block_fd, FileInfo* file_info, char* loader)
{
    return PMfsRead(block_fd, file_info, loader, 0);
}

int32_t PMfsRead(int32_t block_fd, FileInfo* file_info, char* loader,
    int32_t relative_offset)
{
    int32_t nread = 0;
    int32_t read_idx = 0;
    int32_t offset = 0;
    int32_t main_left = -1;
    int32_t ext_left = -1;
    bool in_main = true;
    if (relative_offset < 0)
        return -2;
    /*set offset*/
    if (relative_offset < file_info->size_in_main) {
        offset = file_info->in_main_offset + relative_offset;
        if (file_info->hsize <= file_info->size_in_main) {
            if (file_info->hsize < relative_offset)
                return -2;
            main_left = file_info->hsize - relative_offset;
            ext_left = 0;
        } else {
            main_left = file_info->size_in_main - relative_offset;
            ext_left = file_info->hsize - file_info->size_in_main;
        }
    } else {
        if ((relative_offset - file_info->size_in_main)
            < file_info->size_in_ext) {
            offset = file_info->in_ext_offset
                + (relative_offset - file_info->size_in_main);
            in_main = false;
            main_left = 0;
            ext_left = file_info->hsize - file_info->size_in_main
                - (relative_offset - file_info->in_ext_offset);
        } else {
            return -2;
        }
    }

    if (in_main) {
        do {
            nread = pread(block_fd, loader + read_idx, main_left - read_idx,
                offset + read_idx);
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        if (nread == -1)
            return -1;
    }
    if (ext_left != 0 && ext_left != -1) {
        if (in_main) {
            offset = file_info->in_ext_offset;
        }
        do {
            nread = pread(block_fd, loader + read_idx, ext_left - read_idx,
                offset + read_idx);
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        if (nread == -1)
            return -1;
    }
    return read_idx;
}

static int32_t MfsErase(
    int32_t block_fd, FileInfo* file_info, int32_t start_offset, int32_t len)
{
    int32_t main_left = -1;
    int32_t ext_left = -1;
    if (start_offset < 0)
        return -2;
    /*set offset*/
    pthread_mutex_lock(&(file_info->mutex));
    if (start_offset < file_info->size_in_main) {
        if (file_info->hsize <= file_info->size_in_main) {
            main_left = file_info->hsize - start_offset;
            ext_left = 0;
        } else {
            main_left = file_info->size_in_main - start_offset;
            ext_left = file_info->hsize - file_info->size_in_main;
        }
    } else {
        if ((start_offset - file_info->size_in_main) < file_info->size_in_ext) {
            main_left = 0;
            ext_left = file_info->hsize - file_info->size_in_main
                - (start_offset - file_info->in_ext_offset);
        } else {
            pthread_mutex_unlock(&(file_info->mutex));
            return -2;
        }
    }
    if (main_left + ext_left < len) {
        pthread_mutex_unlock(&(file_info->mutex));
        return -3;
    }
    file_info->hsize -= len;
    file_info->dirt[3] = true;
    WriteFileInfo(block_fd,
        file_info->block->file_info_array_offset
            + (file_info->file_info_id * (FILEINFO_LEN)),
        file_info, file_info->block, file_info->block->super_block);
    pthread_mutex_unlock(&(file_info->mutex));
    return 0;
}

int32_t MfsRemove(int32_t block_fd, FileInfo* file_info)
{
    return MfsErase(block_fd, file_info, 0, file_info->hsize);
}

int32_t MfsCutTail(int32_t block_fd, FileInfo* file_info, int32_t offset)
{
    return MfsErase(block_fd, file_info, offset, file_info->hsize - offset);
}

int32_t AddBlockToSuperBlock(SuperBlock* super_block, LogicBlock* block)
{
    if (super_block->block_array == NULL) {
        assert(super_block->block_num == 0);
        super_block->block_array = block;
        super_block->top_free_block_id = block->block_id;
        super_block->dirt[6] = true;

    } else {
        super_block->block_array->pre = block;
        block->next = super_block->block_array;
        block->next_block_id = super_block->block_array->block_id;
        block->dirt[9] = true;
        super_block->block_array = block;
    }
    block->super_block = super_block;
    ++(super_block->block_num);
    super_block->dirt[5] = true;
    ++(super_block->new_block_num);
    return 0;
}
SuperBlock* SuperBlockCreate(
    char* dir, int32_t ext_size, int32_t byte_ratio, int32_t avg_file_size)
{
    SuperBlock* super_block;
    super_block = malloc(sizeof(SuperBlock));
    sprintf(super_block->dir, "%s", dir);
    super_block->ext_size = ext_size;
    super_block->byte_ratio = byte_ratio;
    super_block->main_size = ext_size * byte_ratio;
    super_block->block_size
        = FILEINFO_LEN * super_block->main_size / avg_file_size
        + super_block->main_size + super_block->ext_size + LOGICBLOCK_INFO_LEN;
    super_block->block_num = 0;
    super_block->block_array = NULL;
    super_block->top_free_block_id = -1;
    super_block->new_block_num = 0;
    pthread_mutex_init(&(super_block->mutex), NULL);
    for (int32_t i = 0; i < SUPERBLOCK_DIRT_NUM; ++i)
        super_block->dirt[i] = true;

    return super_block;
}

// if Block exists block_size is unused
int32_t OpenBlock(char* dir, int32_t block_size, int32_t flag)
{
    int32_t fd;
    if (access(dir, F_OK) < 0) {
        if ((fd = open(dir, flag | O_CREAT | O_WRONLY, 0644)) >= 0) {
            pwrite(fd, "#", 1, block_size - 1);
        } else
            return -1;
    } else {
        if ((fd = open(dir, flag & (~O_CREAT))) >= 0) {
        } else
            return -1;
    }
    return fd;
}

int32_t WriteLogicBlockHeader(int32_t fd, int32_t offset, LogicBlock* block,
    SuperBlock* super_block, bool to_block)
{
    char buffer[BUFSIZE * 2] = { "\0" };
    if (!to_block || block->dirt[0]) {
        if (strlen(block->dir) > DIR_MAX_LEN)
            return -1;
        pwrite(fd, block->dir, strlen(block->dir), offset);
        if (to_block)
            block->dirt[0] = false;
    }
    offset += DIR_MAX_LEN;
    for (int32_t i = 1; i < LOGICBLOCK_DIRT_NUM - 1;
         ++i, offset += ARG_LENGTH_IN_FILE) {
        if (to_block && !(block->dirt[i]))
            continue;
        switch (i) {
        case 1:
            sprintf(buffer, "%d#", block->block_id);
            break;
        case 2:
            sprintf(buffer, "%d#", block->main_free);
            break;
        case 3:
            sprintf(buffer, "%d#", block->ext_free);
            break;
        case 4:
            sprintf(buffer, "%d#", block->size);
            break;
        case 5:
            sprintf(buffer, "%d#", block->main_offset);
            break;
        case 6:
            sprintf(buffer, "%d#", block->ext_offset);
            break;
        case 7:
            sprintf(buffer, "%d#", block->file_info_array_offset);
            break;
        case 8:
            sprintf(buffer, "%d#", block->hole_size);
            break;
        case 9:
            sprintf(buffer, "%d#", block->next_block_id);
            break;
        case 10:
            sprintf(buffer, "%d#", block->file_info_num);
            break;
        }
        pwrite(fd, buffer, strlen(buffer), offset);
        if (to_block)
            block->dirt[i] = false;
    }

    return 0;
}
int32_t WriteLogicBlock(int32_t fd, LogicBlock* block, SuperBlock* super_block)
{
    WriteLogicBlockHeader(fd, 0, block, super_block, true);
    return 0;
}

int32_t WriteFileInfo(int32_t fd, int32_t offset, FileInfo* file_info,
    LogicBlock* block, SuperBlock* super_block)
{
    char buffer[BUFSIZE * 2] = { "\0" };
    if (file_info->dirt[0]) {
        if (strlen(file_info->dir) > DIR_MAX_LEN)
            return -1;
        pwrite(fd, file_info->dir, strlen(file_info->dir), offset);
        file_info->dirt[0] = false;
    }
    offset += DIR_MAX_LEN;
    if (file_info->dirt[1]) {
        if (strlen(file_info->file_name) > NAME_MAX_LEN)
            return -1;
        pwrite(fd, file_info->file_name, strlen(file_info->file_name), offset);
        file_info->dirt[1] = false;
    }
    offset += NAME_MAX_LEN;
    for (int32_t i = 2; i < FILEINFO_DIRT_NUM;
         ++i, offset += ARG_LENGTH_IN_FILE) {
        if (!(file_info->dirt[i]))
            continue;
        switch (i) {
        case 2:
            sprintf(buffer, "%d#", file_info->size);
            break;
        case 3:
            sprintf(buffer, "%d#", file_info->hsize);
            break;
        case 4:
            sprintf(buffer, "%d#", file_info->size_in_main);
            break;
        case 5:
            sprintf(buffer, "%d#", file_info->size_in_ext);
            break;
        case 6:
            sprintf(buffer, "%d#", file_info->in_ext_offset);
            break;
        case 7:
            sprintf(buffer, "%d#", file_info->in_main_offset);
            break;
        case 8:
            sprintf(buffer, "%d#", file_info->next_file_info_id);
            break;
        case 9:
            sprintf(buffer, "%d#", file_info->file_info_id);
            break;
        }
        pwrite(fd, buffer, strlen(buffer), offset);
        file_info->dirt[i] = false;
    }
    return 0;
}

int32_t WriteSuperBlock(int32_t fd, SuperBlock* super_block)
{
    char buffer[BUFSIZE * 2] = { "\0" };
    int32_t offset = 0;
    int32_t tmp = super_block->new_block_num;
    super_block->new_block_num = 0;
    if (super_block->dirt[0]) {
        if (strlen(super_block->dir) > DIR_MAX_LEN)
            return -1;
        pwrite(fd, super_block->dir, strlen(super_block->dir), offset);
        super_block->dirt[0] = false;
    }
    offset += DIR_MAX_LEN;
    for (int32_t i = 1; i < SUPERBLOCK_DIRT_NUM - 1;
         ++i, offset += ARG_LENGTH_IN_FILE) {
        if (!(super_block->dirt[i]))
            continue;
        switch (i) {
        case 1:
            sprintf(buffer, "%d#", super_block->byte_ratio);
            break;
        case 2:
            sprintf(buffer, "%d#", super_block->ext_size);
            break;
        case 3:
            sprintf(buffer, "%d#", super_block->main_size);
            break;
        case 4:
            sprintf(buffer, "%d#", super_block->block_size);
            break;
        case 5:
            sprintf(buffer, "%d#", super_block->block_num);
            break;
        case 6:
            sprintf(buffer, "%d#", super_block->top_free_block_id);
            break;
        }
        pwrite(fd, buffer, strlen(buffer), offset);
        super_block->dirt[i] = false;
    }
    if (super_block->dirt[SUPERBLOCK_DIRT_NUM - 1]) {
        offset += (LOGICBLOCK_INFO_LEN * (super_block->block_num - 1));
        LogicBlock* tmp_block = super_block->block_array;
        assert(tmp_block != NULL);
        /*new block is inserted before head(block_array)*/
        while (tmp) {
            WriteLogicBlockHeader(fd, offset, tmp_block, super_block, false);
            offset -= LOGICBLOCK_INFO_LEN;
            --tmp;
            tmp_block = tmp_block->next;
        }
    }
    super_block->dirt[SUPERBLOCK_DIRT_NUM - 1] = false;
    return 0;
}

LogicBlock* ReadLogicBlockHeaderFromBuf(SuperBlock* super_block, char* buf)
{
    LogicBlock* block = malloc(sizeof(LogicBlock));
    int32_t offset = 0;
    sscanf(buf + offset, "%s", block->dir);
    offset += DIR_MAX_LEN;
    sscanf(buf + offset, "%d", &(block->block_id));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->main_free));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->ext_free));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->size));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->main_offset));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->ext_offset));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->file_info_array_offset));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->hole_size));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->next_block_id));
    offset += ARG_LENGTH_IN_FILE;
    sscanf(buf + offset, "%d", &(block->file_info_num));
    offset += ARG_LENGTH_IN_FILE;
    for (int32_t i = 0; i < LOGICBLOCK_DIRT_NUM; ++i) {
        block->dirt[i] = false;
    }
    block->pre = NULL;
    block->next = NULL;
    pthread_mutex_init(&(block->mutex), NULL);
    block->file_info_array = NULL;
    block->super_block = super_block;
    return block;
}

/*write in super_block*/
int32_t ReadSuperBlock(SuperBlock* super_block, char* dir)
{
    int32_t fd;
    int32_t offset = 0;
    char buf[BUFSIZE * 2] = { "\0" };
    if ((fd = OpenBlock(dir, -1, O_RDONLY)) >= 0) {
        int nread = 0;
        int read_idx = 0;
        do {
            nread = pread(fd, buf + read_idx,
                (DIR_MAX_LEN + (ARG_LENGTH_IN_FILE * (SUPERBLOCK_DIRT_NUM - 2))
                    - read_idx),
                read_idx);
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        sscanf(buf + offset, "%s", super_block->dir);
        offset += DIR_MAX_LEN;
        sscanf(buf + offset, "%d", &(super_block->byte_ratio));
        offset += ARG_LENGTH_IN_FILE;
        sscanf(buf + offset, "%d", &(super_block->ext_size));
        offset += ARG_LENGTH_IN_FILE;
        sscanf(buf + offset, "%d", &(super_block->main_size));
        offset += ARG_LENGTH_IN_FILE;
        sscanf(buf + offset, "%d", &(super_block->block_size));
        offset += ARG_LENGTH_IN_FILE;
        sscanf(buf + offset, "%d", &(super_block->block_num));
        offset += ARG_LENGTH_IN_FILE;
        sscanf(buf + offset, "%d", &(super_block->top_free_block_id));
        offset += ARG_LENGTH_IN_FILE;
        nread = 0;
        read_idx = 0;
        buf[0] = 0;
        do {
            nread = pread(fd, buf + read_idx,
                (LOGICBLOCK_INFO_LEN * super_block->block_num - read_idx),
                offset + read_idx);
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        close(fd);
        for (int32_t i = 0; i < SUPERBLOCK_DIRT_NUM; ++i)
            super_block->dirt[i] = false;
        LogicBlock** blocks
            = calloc(sizeof(LogicBlock*), super_block->block_num);
        assert(super_block->block_num != 0);
        for (int32_t i = 0, offset = 0; i < super_block->block_num;
             ++i, offset += LOGICBLOCK_INFO_LEN) {
            blocks[i] = ReadLogicBlockHeaderFromBuf(super_block, buf + offset);
        }
        int32_t idx = super_block->top_free_block_id;
        super_block->block_array = blocks[idx];
        LogicBlock* tmp = super_block->block_array;
        while (tmp && tmp->next_block_id != -1) {
            LogicBlock* next = blocks[tmp->next_block_id];
            tmp->next = next;
            next->pre = tmp;
            tmp = next;
        }
    }
    return 0;
}
