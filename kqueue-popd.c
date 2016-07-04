#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/fs.h>
#include <sys/epoll.h>

static char message[0x10000];

int main()
{
    const char *fname = "/dev/kqueue-pop";
    int popfd = open(fname, O_RDONLY);

    int                epfd = epoll_create(1);
    struct epoll_event epev;

    epev.events  = EPOLLIN;
    epev.data.fd = popfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, popfd, &epev);

    for (;;) {
        epoll_wait(epfd, &epev, 1, -1);
        size_t rsize = read(popfd, message, sizeof(message));

        if (rsize > 0) {
            message[rsize] = '\0';
            fprintf(stdout, "* %lu: %s\n", rsize, message);
        }
    }
    close(popfd);

    return 0;
}
