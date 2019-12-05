#ifndef _MFS_H
#define _MFS_H
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
/* int int_log2 (unsigned int x) {
 *   static const unsigned char log_2[256] = {
 *     0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
 *     6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
 *     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
 *     7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
 *     8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
 *     8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
 *     8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
 *     8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
 *   };
 *   int l = -1;
 *   while (x >= 256) { l += 8; x >>= 8; }
 *   return l + log_2[x];
 * } */
#define DIR_MAX_LEN 200
#define NAME_MAX_LEN 50
#ifndef BUFSIZE
#define BUFSIZE 8192
#endif

#define MAX_FILE_NUM_IN_A_BLOCK ((1<<21)-1)
#define LOGICBLOCK_DIRT_NUM 12
#define SUPERBLOCK_DIRT_NUM 8
#define FILEINFO_DIRT_NUM 10
#define ARG_LENGTH_IN_FILE 50

#define SUPERBLOCK_INFO_LEN \
    (ARG_LENGTH_IN_FILE*(SUPERBLOCK_DIRT_NUM-2)+DIR_MAX_LEN)
#define FILEINFO_LEN \
    (ARG_LENGTH_IN_FILE*(FILEINFO_DIRT_NUM-2)+DIR_MAX_LEN+NAME_MAX_LEN)
#define SUPERBLOCK_LOGICBLOCKINFO_OFFSET (SUPERBLOCK_INFO_LEN)
#define LOGICBLOCK_INFO_LEN \
    (ARG_LENGTH_IN_FILE*(LOGICBLOCK_DIRT_NUM-2)+DIR_MAX_LEN)
#define LOGICBLOCK_MAIN_OFFSET (LOGICBLOCK_INFO_LEN)

typedef struct FileInfo {
    char dir[DIR_MAX_LEN];
    char file_name[NAME_MAX_LEN];
    int32_t size;
    int32_t hsize;     //hold size
    int32_t size_in_main;
    int32_t size_in_ext;
    int32_t in_ext_offset;
    int32_t in_main_offset;
    int32_t next_file_info_id;
    int32_t file_info_id;
    bool dirt[FILEINFO_DIRT_NUM];
    struct FileInfo* next;
    struct FileInfo* pre;
    struct LogicBlock* block;
    pthread_mutex_t mutex;
} FileInfo;

typedef struct LogicBlock {
    char dir[DIR_MAX_LEN];
    int32_t block_id;
    int32_t main_free;
    int32_t ext_free;
    int32_t size;      //total space
    int32_t main_offset;
    int32_t ext_offset;
    int32_t file_info_array_offset;
    int32_t hole_size; //total size of fragments
    int32_t next_block_id;
    int32_t file_info_num;
    bool dirt[LOGICBLOCK_DIRT_NUM];
    struct LogicBlock* next;
    struct LogicBlock* pre;
    struct SuperBlock* super_block;
    pthread_mutex_t mutex;  //lock when writing new file in
    FileInfo* file_info_array;
    /*file*/
    /*file*/
    /*file*/
    /*file*/
    /*file*/
    /*...*/
    /*...*/
    /*...*/
    /*...*/
} LogicBlock;
typedef struct SuperBlock {
    char dir[DIR_MAX_LEN];
    int32_t byte_ratio;    //main_size=ext_size*byte_ratio
    int32_t ext_size;      //total size of exts
    int32_t main_size;
    int32_t block_size;    //block_size=FILEINFO_LEN*(main_size/avg_size)+ext_size+main_size
    int32_t block_num;
    int32_t top_free_block_id;     //used for establishing link list
    bool dirt[SUPERBLOCK_DIRT_NUM];
    pthread_mutex_t mutex;
    int32_t new_block_num;
    LogicBlock* block_array; //save in disk as array, in mem as linked list

    /*LogicBlockInfo*/
    /*LogicBlockInfo*/
    /*LogicBlockInfo*/
    /*LogicBlockInfo*/
    /*LogicBlockInfo*/
    /*..............*/
    /*..............*/
    /*..............*/
    /*..............*/
} SuperBlock;
LogicBlock* LogicBlockCreate(SuperBlock* super_block, int32_t block_id);
FileInfo* FileInfoCreate(int32_t size, SuperBlock* super_block);
FileInfo* ReadFileInfoFromBuf(char* buf, SuperBlock* super_block);
int32_t MfsOpen(char* file_name, FileInfo** file_info, SuperBlock* super_block,
                int32_t flag);
int32_t MfsWrite(int32_t block_fd, FileInfo* file_info, char* content);
int32_t PMfsWrite(int32_t block_fd, FileInfo* file_info, char* content,
                  int32_t relative_offset);
int32_t MfsRead(int32_t block_fd, FileInfo* file_info, char* loader);
int32_t PMfsRead(int32_t block_fd, FileInfo* file_info, char* loader,
                 int32_t relative_offset);
int32_t MfsRemove(int32_t block_fd, FileInfo* file_info);
int32_t MfsCutTail(int32_t block_fd, FileInfo* file_info, int32_t offset);
int32_t AddBlockToSuperBlock(SuperBlock* super_block, LogicBlock* block);
SuperBlock* SuperBlockCreate(char* dir, int32_t ext_size,
                             int32_t byte_ratio, int32_t avg_file_size);
int32_t OpenBlock(char* dir, int32_t block_size, int32_t flag);
int32_t WriteLogicBlockHeader(
    int32_t fd, int32_t offset, LogicBlock* block, SuperBlock* super_block, bool to_block);
int32_t WriteLogicBlock(
    int32_t fd, LogicBlock* block, SuperBlock* super_block);
int32_t WriteFileInfo(int32_t fd, int32_t offset, FileInfo* file_info,
                      LogicBlock* block, SuperBlock* super_block);
int32_t WriteSuperBlock(int32_t fd, SuperBlock* super_block);
LogicBlock* ReadLogicBlockHeaderFromBuf(SuperBlock* super_block, char* buf);
int32_t ReadSuperBlock(SuperBlock* super_block, char* dir);

#endif
