#include "webserver.h"
#include "mfs_api.h"
#include "thread_pool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef SIGCLD
#define SIGCLD SIGCHLD
#endif

SuperBlock super_block;
MfsHashBlock hash_block;

struct time_counter global_cost_counter;

struct {
    char* ext;
    char* filetype;
} extensions[] = { { "gif", "image/gif" }, { "jpg", "image/jpg" },
    { "jpeg", "image/jpeg" }, { "png", "image/png" }, { "ico", "image/ico" },
    { "zip", "image/zip" }, { "gz", "image/gz" }, { "tar", "image/tar" },
    { "htm", "text/htm" }, { "html", "text/html" }, { 0, 0 } };

unsigned long get_file_size(const char* path)
{
    unsigned long filesize = -1;
    struct stat statbuff;
    if (stat(path, &statbuff) < 0) {
        return filesize;
    } else {
        filesize = statbuff.st_size;
    }
    return filesize;
}

double timeDiff(struct timeval* pre_mannual)
{
    static struct timeval pre;
    struct timeval now;
    gettimeofday(&now, NULL);
    double diff;
    diff = now.tv_sec + now.tv_usec * 1e-6;
    if (pre_mannual != NULL) {
        diff -= pre_mannual->tv_sec + pre_mannual->tv_usec * 1e-6;
        *pre_mannual = now;
    } else
        diff -= pre.tv_sec + pre.tv_usec * 1e-6;
    pre = now;
    return diff;
}

void timeToBuffer(char* buffer)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm now;
    localtime_r((time_t*)(&(tv.tv_sec)), &now);
    sprintf(buffer, "Time: %d-%d-%d-%d-%d-%d-%6ld\t", now.tm_year + 1900,
        now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec,
        tv.tv_usec);
}

void log_file_init(struct log_file* logf, const char* log_name)
{
    logf->log_len = 0;
    logf->log_name = log_name;
    logf->out_buffer[0] = 0;
}

void logger(int type, const char* s1, const char* s2, int socket_fd, int is_out,
    struct log_file* logf)
{
    int fd;
    char logbuffer[BUFSIZE * 10];
    char buffer[BUFSIZE * 10];
    char timebuffer[TIMEBUFFERSIZE];
    char* splitter = "------------------------------------------------";
    if (type != LOGSPLIT)
        timeToBuffer(timebuffer);
    else
        timebuffer[0] = 0;

    switch (type) {
    case ERROR:
        (void)sprintf(logbuffer,
            "ERROR: %s:%s Errno=%d exiting"
            "pid=%d",
            s1, s2, errno, getpid());
        break;
    case FORBIDDEN:
        (void)write(socket_fd,
            "HTTP/1.1 403 Forbidden\n"
            "Content-Length: 185\nConnection: close\n:"
            "Content-Type: text/html\n\n<html><head>\n"
            "<title>403 Forbidden</title>\n</head><body>\n"
            "<h1>Forbidden</h1>\nThe requested URL, file type"
            " or operation is not allowed on this simple"
            " static file webserver.\n</body></html>\n",
            271);
        (void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
        break;
    case NOTFOUND:
        (void)write(socket_fd,
            "HTTP/1.1 404 Not Found\n"
            "Content-Length: 136\nConnection: close\n:"
            "Content-Type: text/html\n\n<html><head>\n"
            "<title>404 Not Found</title>\n</head><body>\n"
            "<h1>Not Found</h1>\nThe requested URL was not"
            " found on this server.\n"
            "</body></html>\n",
            224);
        (void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
        break;
    case LOG:
        (void)sprintf(logbuffer, "INFO: %s:%s:%d", s1, s2, socket_fd);
        break;
    case LOGSPLIT:
        (void)sprintf(logbuffer, "%s %d %s%s", s1, socket_fd, s2, splitter);
        break;
    case LOGTIMEDIFF:
        (void)sprintf(logbuffer, "Time cost analysis:\n%s", s1);
        break;
    case PLAINLOG:
        break;
    }

    if (type != PLAINLOG) {
        sprintf(buffer, "%s%s%c", timebuffer, logbuffer, '\n');
        if (is_out == 0)
            (logf->log_len)
                += sprintf(logf->out_buffer + (logf->log_len), "%s", buffer);

        if (logf->log_name != NULL && is_out == 1
            && (fd = open(logf->log_name, O_CREAT | O_WRONLY | O_APPEND, 0644))
                >= 0) {
            (void)write(fd, buffer, strlen(buffer));
            (void)close(fd);
        }
    } else {
        if (logf->log_name != NULL && is_out == 1
            && (fd = open(logf->log_name, O_CREAT | O_WRONLY | O_APPEND, 0644))
                >= 0) {
            (void)write(fd, logf->out_buffer, logf->log_len);
            (void)close(fd);
            logf->log_len = 0;
            logf->out_buffer[0] = 0;
        }
    }
}
void counter_thread(void* data)
{
    const struct threadpool* pool;
    struct thread_info* threadinfo;
    struct thread** threads;
    threadinfo = (*(thread_info**)(data));
    cost_detail* details = (*(cost_detail**)(data + sizeof(thread_info*)));
    pool
        = (*(threadpool**)(data + sizeof(thread_info*) + sizeof(cost_detail*)));
    struct log_file logf;
    log_file_init(&logf, SERVER_CODE "-time_cost.log");

    if (!(pool->is_alive))
        return;

    threads = pool->threads;
    int num = THREAD_NUM;
    double cost_time[num][COST_TIME_NUM + 1];
    char logbuffer[LOG_BUFFER_LEN];
    int invokes[num];
    int idx;
    int i;
    int length;
    int total_invokes = 0;
    int hit = 1;
    double total_time = 0;
    memset(cost_time, 0, sizeof(cost_time));
    memset(invokes, 0, sizeof(invokes));

    while (1 && pool->is_alive) {
        /* auto shutdown in debug mod after a certain times of visit */
#ifdef DEBUG
        if (hit >= DEBUG_MAX_TIMES)
            break;
#endif
        sleep(30); // count every 30 seconds
        total_time = 0;
        total_invokes = 0;
        memset(invokes, 0, sizeof(invokes));
        memset(cost_time, 0, sizeof(cost_time));
        length = 0;
        for (idx = 0; idx < num; ++idx) {
            if (idx == *(threadinfo->id))
                continue;
            pthread_mutex_lock(&(threads[idx]->threadinfo->counter->mutex));
            for (i = 0; i < COST_TIME_NUM; ++i) {
                cost_time[idx][i]
                    = threads[idx]->threadinfo->counter->cost_time[i];
                threads[idx]->threadinfo->counter->cost_time[i] = 0;
            }
            cost_time[idx][COST_TIME_NUM]
                = threads[idx]->threadinfo->counter->total_cost_time;
            threads[idx]->threadinfo->counter->total_cost_time = 0;
            invokes[idx] = threads[idx]->threadinfo->counter->invokes;
            threads[idx]->threadinfo->counter->invokes = 0;
            pthread_mutex_unlock(&(threads[idx]->threadinfo->counter->mutex));
        }
        length = 0;
        for (idx = 0; idx < num; ++idx) {
            total_invokes += invokes[idx];
            total_time += cost_time[idx][COST_TIME_NUM];
            for (int i = 0; i < COST_TIME_NUM; ++i)
                details[i].cost_time += cost_time[idx][i];
            /*length used for appending the buffer*/
            if (idx != *threadinfo->id) {
                length += sprintf(logbuffer + length,
                    "thread "
                    "%d:\tinvokes:%d\ttotal_cost_time:%.6lf\tavg_cost_time:%."
                    "6lf",
                    idx, invokes[idx], cost_time[idx][COST_TIME_NUM],
                    cost_time[idx][COST_TIME_NUM]
                        / (invokes[idx] == 0 ? 1 : invokes[idx]));

                for (int j = 0; j < COST_TIME_NUM; ++j) {
                    length += sprintf(logbuffer + length, "\t%s:%.6lf",
                        details[j].name,
                        cost_time[idx][j]
                            / (invokes[idx] == 0 ? 1 : invokes[idx]));
                }
                length += sprintf(logbuffer + length, "\n");
            } else {
                length += sprintf(logbuffer + length,
                    "thread %d:\tthis is the counter thread\n", idx);
            }
        }
        for (int i = 0; i < COST_TIME_NUM; ++i) {
            length += sprintf(logbuffer + length,
                "%s "
                "\t\t%s:\n\t\t\t\ttotal_cost:%.6lf\n\t\t\t\tavg_invokes_cost_"
                "time:%"
                ".6lf\n",
                details[i].name, details[i].description, details[i].cost_time,
                details[i].cost_time
                    / (total_invokes == 0 ? 1 : total_invokes));
            details[i].cost_time = 0;
        }
        length += sprintf(logbuffer + length,
            "total_invokes:%d\n"
            "avg_invokes:%d\ntotal_cost_time:%.6lf\navg_thread_cost_time:%."
            "6lf\navg_"
            "invokes_cost_time:%.6lf\n",
            total_invokes, total_invokes / ((num - 1) == 0 ? 1 : (num - 1)),
            total_time, total_time / ((num - 1) == 0 ? 1 : (num - 1)),
            total_time / (total_invokes == 0 ? 1 : total_invokes));
        sprintf(logbuffer + length,
            "Notes: those abbreviatons correspond to the time stamps logged "
            "in function web\n\n");
        logger(LOGSPLIT, "\n\nLog", SERVER_CODE " start", hit, 0, &logf);
        logger(LOGTIMEDIFF, logbuffer, "", hit, 0, &logf);
        logger(LOGSPLIT, "Log", "end", hit, 0, &logf);
        logger(PLAINLOG, "", "", hit, 1, &logf);
        ++hit;
    }
}

void* web(void* data)
{
    int fd;
    int hit;
    struct thread_info* threadinfo;

    int j, file_fd, buflen;
    long i, ret, len;
    double timetmp;
    char* fstr;
    char timeDiffLog[TIMEBUFFERSIZE];   // log time diff
    char timeTmpBuffer[TIMEBUFFERSIZE]; // used for sprintf to convert doubles
    /*time base for timeDiff, cause multi-thread can't use default static base*/
    struct timeval pre;
    struct time_counter cost_counter;
    char buffer[BUFSIZE + 1];
    memset(buffer, 0, sizeof(0));
    memset(&(cost_counter.cost_time), 0, sizeof(cost_counter.cost_time));
    cost_counter.invokes = 0;
    cost_counter.total_cost_time = 0;
    timeDiffLog[0] = 0;
    fd = (*(webparam**)(data + sizeof(thread_info*)))->fd;
    hit = (*(webparam**)(data + sizeof(thread_info*)))->hit;
    threadinfo = *(thread_info**)(data);
    threadinfo->logf->idx = 0;

    timeDiff(&pre); // time counter init

    ret = read(fd, buffer, BUFSIZE);
    timetmp = timeDiff(&pre);
    sprintf(
        timeTmpBuffer, "Reading socket content cost %lf seconds.\n", timetmp);
    strcat(timeDiffLog, timeTmpBuffer);
    cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
    cost_counter.total_cost_time += timetmp;

    logger(LOGSPLIT, "\n\nLog",
        SERVER_CODE " "
                    "start",
        hit, 0, (threadinfo->logf));
    if (ret == 0 || ret == -1) {
        logger(FORBIDDEN, "failed to read browser request", "", fd, 0,
            (threadinfo->logf));
    } else {
        if (ret > 0 && ret < BUFSIZE)
            buffer[ret] = 0;
        else
            buffer[0] = 0;
        for (i = 0; i < ret; i++)
            if (buffer[i] == '\r' || buffer[i] == '\n')
                buffer[i] = '*';
        logger(LOG, "request", buffer, hit, 0, (threadinfo->logf));
        if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
            logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd,
                0, (threadinfo->logf));
        }
        for (i = 4; i < BUFSIZE; i++) {
            if (buffer[i] == ' ') {
                buffer[i] = 0;
                break;
            }
        }
        for (j = 0; j < i - 1; j++)
            if (buffer[j] == '.' && buffer[j + 1] == '.') {
                logger(FORBIDDEN,
                    "Parent directory (..)"
                    " path names not supported",
                    buffer, fd, 0, (threadinfo->logf));
            }
        if (!strncmp(&buffer[0], "GET /\0", 6)
            || !strncmp(&buffer[0], "get /\0", 6))
            (void)strcpy(buffer, "GET /index.html");
        buflen = strlen(buffer);
        fstr = (char*)0;
        for (i = 0; extensions[i].ext != 0; i++) {
            len = strlen(extensions[i].ext);
            if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
                fstr = extensions[i].filetype;
                break;
            }
        }
        if (fstr == 0)
            logger(FORBIDDEN, "file extension type not supported", buffer, fd,
                0, (threadinfo->logf));

        FileInfo* file_info = NULL;
        if ((file_fd = MfsHashOpen(&super_block, &buffer[5], &file_info,
                 O_RDONLY, &hash_block, -1))
            < 0) {
            logger(NOTFOUND, "failed to open file", &buffer[5], fd, 0,
                (threadinfo->logf));
        }
        logger(LOG, "SEND", &buffer[5], hit, 0, (threadinfo->logf));
        len = get_file_size(&buffer[5]);
        (void)lseek(file_fd, (off_t)0, SEEK_SET);
        (void)sprintf(buffer,
            "HTTP/1.1 200 OK\nServer: nweb/%d.0\n"
            "Content-Length: %ld\nConnection:close\n"
            "Content-Type: %s\n\n",
            VERSION, len, fstr);
        logger(LOG, "Header", buffer, hit, 0, (threadinfo->logf));

        timetmp = timeDiff(&pre);
        sprintf(timeTmpBuffer,
            "Validation checking for socket content"
            " and loging cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;

        (void)write(fd, buffer, strlen(buffer));

        timetmp = timeDiff(&pre);
        sprintf(timeTmpBuffer, "Writing header to socket cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;

        if ((ret = MfsRead(file_fd, file_info, buffer)) > 0) {
            timetmp = timeDiff(&pre);
            sprintf(timeTmpBuffer, "reading request file cost %lf seconds.\n",
                timetmp);
            strcat(timeDiffLog, timeTmpBuffer);
            cost_counter.cost_time[threadinfo->logf->idx] = timetmp;
            cost_counter.total_cost_time += timetmp;
            (void)write(fd, buffer, ret);
            timetmp = timeDiff(&pre);
            sprintf(timeTmpBuffer,
                "writing request content to socket cost %lf seconds.\n",
                timetmp);
            strcat(timeDiffLog, timeTmpBuffer);
            cost_counter.cost_time[threadinfo->logf->idx + 1] = timetmp;
            cost_counter.total_cost_time += timetmp;
        }
        (threadinfo->logf->idx) += 2;
        usleep(20000);
        timetmp = timeDiff(&pre);
        sprintf(timeTmpBuffer,
            "Spleeping for socket to drain "
            "cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;
        logger(LOGTIMEDIFF, timeDiffLog, "", fd, 0, (threadinfo->logf));
        logger(LOGSPLIT, "Log", "end", hit, 0, (threadinfo->logf));
        logger(PLAINLOG, "", "", hit, 1, (threadinfo->logf));
        timetmp = timeDiff(&pre);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;
        close(file_fd);
        free(file_info);
    }
    close(fd);

#ifdef COUNT_TIME
    pthread_mutex_lock(&(threadinfo->counter->mutex));
    threadinfo->counter->total_cost_time += cost_counter.total_cost_time;
    for (int i = 0; i < COST_TIME_NUM; ++i) {
        threadinfo->counter->cost_time[i] += cost_counter.cost_time[i];
    }
    ++(threadinfo->counter->invokes);
    pthread_mutex_unlock(&(threadinfo->counter->mutex));
#endif

    return NULL;
}

int main(int argc, char** argv)
{
    int i, port, listenfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;
    struct log_file logf;
    log_file_init(&logf, "webserver.log");

    if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
        (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
                     "\tnweb is a small and very safe mini web server\n"
                     "\tnweb only servers out file/web pages "
                     "with extensions named below\n"
                     "\t and only from the named "
                     "directory or its sub-directories.\n"
                     "\tThere is no fancy features = safe and secure.\n\n"
                     "\tExample:webserver 8181 /home/nwebdir &\n\n"
                     "\tOnly Supports:",
            VERSION);
        for (i = 0; extensions[i].ext != 0; i++)
            (void)printf(" %s", extensions[i].ext);

        (void)printf(
            "\n\tNot Supported: URLs including \"..\", Java, Javascript, CGL\n"
            "\tNot Supported: directories "
            "//etc /bin /lib /tmp /usr /dev /sbin \n"
            "\tNo warranty given or implied\n"
            "\tNigle Griffiths nag@uk.ibm.com\n");
        exit(0);
    }
    if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5)
        || !strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5)
        || !strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5)
        || !strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6)) {
        (void)printf("ERROR:Bad top directory %s, see nweb -?\n", argv[2]);
        exit(3);
    }
    if (chdir(argv[2]) == -1) {
        (void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
        exit(4);
    }
    if (fork() != 0)
        return 0;
    (void)signal(SIGCLD, SIG_IGN);
    (void)signal(SIGHUP, SIG_IGN);
    for (i = 0; i < 32; i++)
        (void)close(i);
    (void)setpgrp();
    logger(LOG, "nweb starting", argv[1], getpid(), 1, &logf);

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        logger(ERROR, "system call", "socket", 0, 1, &logf);
    port = atoi(argv[1]);
    if (port < 0 || port > 60000)
        logger(
            ERROR, "Invalid port number (try 1->60000)", argv[1], 0, 1, &logf);

    /*pthread part*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifdef COUNT_TIME
    /* pthread_t pth; */
    pthread_cond_init(&global_cost_counter_cond, NULL);
    pthread_mutex_init(&global_cost_counter_mutex, NULL);
    /*pthread part end*/

    /*init time_counter*/
    memset(&global_cost_counter, 0, sizeof(global_cost_counter));
#endif
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        logger(ERROR, "system call", "bind", 0, 1, &logf);
        exit(1);
    }
    if (listen(listenfd, 64) < 0)
        logger(ERROR, "system call", "listen", 0, 0, &logf);
    struct task* curtask;
    struct threadpool* pool = initThreadPool(THREAD_NUM);

#ifdef COUNT_TIME
    cost_detail details[COST_TIME_NUM] = { { 0, "RSC",
                                               "Reading socket content" },
        { 0, "VCSCL", "Validation checking for socket content and loging" },
        { 0, "WHSC", "Writing header to socket" },
        { 0, "RRF", "reading requested file" },
        { 0, "WRCS", "writing requested content to socket" },
        { 0, "SLEEP", "Spleeping for socket to drain" },
        { 0, "LOG", "Logging buffer to file" } };
    void* param = malloc(
        sizeof(cost_detail*) + sizeof(thread_info*) + sizeof(threadpool*));

    (*(cost_detail**)(param + sizeof(thread_info*))) = (details);
    (*(threadpool**)(param + sizeof(thread_info*) + sizeof(cost_detail**)))
        = (pool);
    curtask = (task*)malloc(sizeof(task));
    curtask->arg = param;
    curtask->function = (void*)counter_thread;
    if (push_taskqueue(&(pool->queue), curtask) < 0) {
        error("counter_thread push_taskqueue", "webserver.log");
    }

#endif
    if (pool == NULL) {
        error("initThreadPool", "webserver.log");
        return 1;
    }
    if (pool->num_threads != 10)
        error("thread_num", "webserver.log");

    /* mfs init */
    ReadSuperBlock(&super_block, "super");
    MfsHashBlockOpen(&hash_block, -1, "mfshash");

    for (hit = 1;; hit++) {
        length = sizeof(cli_addr);
        webparam* param = (webparam*)malloc(sizeof(webparam));
        if ((param->fd = accept(listenfd, (struct sockaddr*)&cli_addr, &length))
            < 0)
            logger(ERROR, "system call", "accept", 0, 1, &logf);
        param->hit = hit;

        curtask = (task*)malloc(sizeof(task));

        curtask->arg = malloc(sizeof(webparam*) + sizeof(thread_info*));
        (*(webparam**)(curtask->arg + sizeof(thread_info*))) = param;
        curtask->function = (void*)web;

        if (push_taskqueue(&(pool->queue), curtask) < 0) {
            error("push_taskqueue", "webserver.log");
        }
    }
    return 0;
}
