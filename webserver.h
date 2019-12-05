#ifndef _WEBSERVER_H
#define _WEBSERVER_H
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define LOGSPLIT 99999
#define LOGTIMEDIFF 9999
#define PLAINLOG 9998
#define TIMEBUFFERSIZE 1000 // size of buffer related to time
#define PROCESS_NUM 11
#define NAME_LEN 50
#define DESCRIPTION_LEN 100
#define COST_TIME_NUM 7
#define SERVER_CODE "THREAD"
#define LOG_BUFFER_LEN BUFSIZE*10

#ifndef COUNT_TIME
#define COUNT_TIME
#endif

typedef struct {
    int hit;
    int fd;
} webparam;

typedef struct log_file {
    const char* log_name;
    int log_len;
    char out_buffer[LOG_BUFFER_LEN];
    int idx;
} log_file;

typedef struct time_counter {
    double cost_time[COST_TIME_NUM];
    double total_cost_time;
    int invokes;
    pthread_mutex_t mutex;
} time_counter;


typedef struct cost_detail {
    double cost_time;
    const char name[NAME_LEN];
    const char description[DESCRIPTION_LEN];
} cost_detail;

pthread_cond_t global_cost_counter_cond;
pthread_mutex_t global_cost_counter_mutex;

void log_file_init(struct log_file* logf, const char* log_name);
unsigned long get_file_size(const char* path);
double timeDiff(struct timeval* pre_mannual);
void timeToBuffer(char* buffer);
void logger(int type, const char* s1, const char* s2, int socket_fd, int is_out,
            struct log_file* logf);
void* web(void* data);
#endif
