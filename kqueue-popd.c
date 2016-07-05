#include <getopt.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/fs.h>
#include <sys/epoll.h>

#define CHARDEV "kqueue-pop"
#define STORDIR "."

static char message[0x10000];

static struct option options[] = {
    { .name = "debug",    .val = 'd',  },
    { .name = "storage",  .val = 's', .has_arg = required_argument },
    { .name = "chardev",  .val = 'c', .has_arg = required_argument },
    { .name = "help",     .val = 'h'   },
    { .name = NULL }
};

static struct state {
    int   debug;
    char *storage;
    char *chardev;
    int   help;
} state;

int config (int argc, char *argv[], struct state *state)
{
    int  opt;
    while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
        case 'd': state->debug = 1;
                  break;
        case 's': state->storage = strdup(optarg);
                  break;
        case 'c': state->chardev = strdup(optarg);
                  break;
        case 'h': state->help = 1;
                  break;
        }
    }

    if (!state->chardev)
        state->chardev = CHARDEV;
    if (!state->storage)
        state->storage = STORDIR;

    return 0;
}

void help (void)
{
    printf("Options: \n");
    printf("  --help     Show this help & exit\n");
    printf("  --debug    Debug mode\n");
    printf("  --storage  Directory to store received messages. \"%s\" by default\n", STORDIR);
    printf("  --chardev  Character device to communicate with kernel. \"%s\" by default\n", CHARDEV);
    printf("\n");
}

void printl(int priority, char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (state.debug)
        vfprintf((priority < LOG_NOTICE) ? stderr : stdout, format, args);
    else
        vsyslog(priority, format, args);

    va_end(args);
}

void terminate_handler(int sig)
{
    printl(LOG_NOTICE, "%d signal received; exit\n", sig);

    _Exit(EXIT_SUCCESS);
}

pid_t daemonize (void)
{
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        printf("%d daemonized\n", pid);
        exit(EXIT_SUCCESS);
    }


    /* Disable child and TTY related signals */
    sigset_t sig_set;
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGCHLD);
    sigaddset(&sig_set, SIGTSTP);
    sigaddset(&sig_set, SIGTTOU);
    sigaddset(&sig_set, SIGTTIN);
    sigprocmask(SIG_BLOCK, &sig_set, NULL);

    /* Set up a signal handler */
    struct sigaction sig_action;
    sig_action.sa_handler = terminate_handler;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = 0;

    sigaction(SIGHUP, &sig_action, NULL);
    sigaction(SIGTERM, &sig_action, NULL);
    sigaction(SIGINT, &sig_action, NULL);

    return getpid();
}


int proc_message(char *message, size_t size)
{
    if (state.debug) {
        printl(LOG_NOTICE, "% 5lu bytes message: \n%s\n", size, message);

    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm     *tm = localtime(&tv.tv_sec);

        static char msgfn[PATH_MAX];
        snprintf(msgfn, sizeof(msgfn)-1, "%s/popmsg-%04d%02d%02d%02d%02d%02d-%06d",
                state.storage,
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec,
                tv.tv_usec);

        FILE *msgfd = fopen(msgfn, "wb+");
        if (msgfd == NULL) {
            printl(LOG_ERR, "can't open %s for write\n", msgfn);
            return -1;
        }
        fwrite(message, size, 1, msgfd);
        fclose(msgfd);
    }
}

int main(int argc, char *argv[])
{
    config(argc, argv, &state);

    if (state.help) {
        help();
        return 0;
    }

    static char popfn[PATH_MAX];
    snprintf(popfn, sizeof(popfn)-1, "/dev/%s", state.chardev);

    int  popfd = open(popfn, O_RDONLY);
    if (popfd == -1) {
        fprintf(stderr, "can't open %s for read; exit(1)\n", popfn);
        return 1;
    }

    int                epfd = epoll_create(1);
    struct epoll_event epev;
    epev.events  = EPOLLIN;
    epev.data.fd = popfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, popfd, &epev);

    if (!state.debug) {
        daemonize();
        openlog(NULL, 0, LOG_DAEMON);
    }

    for (;;) {
        epoll_wait(epfd, &epev, 1, -1);

        for (;;) {
            size_t rsize = read(popfd, message, sizeof(message));
            lseek(popfd, SEEK_SET, 0);
            if (rsize == 0)
                break;

            proc_message(message, rsize);
        }
    }
    close(popfd);

    return 0;
}
