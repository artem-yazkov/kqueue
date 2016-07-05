#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

static char message[0x10000];

int main()
{
    const char *pushfn = "/dev/kqueue-push";
    int pushfd = open(pushfn, O_WRONLY);

    if (pushfd == -1) {
        fprintf(stderr, "can't open %s for write; exit(1)\n", pushfn);
        return 1;
    }

    char msgfn [PATH_MAX];
    char format[32];

    snprintf(format, sizeof(format), "%%%lus", sizeof(msgfn));

    while (fscanf(stdin, format, msgfn) == 1) {

        int msgfd = open(msgfn, O_RDONLY);
        if (msgfd == -1) {
            fprintf(stderr, "can't open %s for read; exit(1)\n");
            return 1;
        }
        size_t rsize = read(msgfd, message, sizeof(message));
        close(msgfd);

        size_t wsize = write(pushfd, message, rsize);
        fprintf(stdout, "push %5lu bytes from %s\n", wsize, msgfn);
    }

    close(pushfd);

    return 0;
}
