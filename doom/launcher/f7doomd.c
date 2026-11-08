/* f7doomd: launch DOOM from the ATOTO F7's stock UI with a keyboard hotkey.
 *
 * Watches every /dev/input/event* device that has KEY_F12 (rescanned every 2s,
 * so a keyboard/hub plugged in after boot is picked up). On an F12 press it
 * runs `sh <dir>/doom_run.sh` and waits for it; doom_run.sh pauses the stock
 * UI, runs DOOM and restores the UI when DOOM exits (F12 or menu Quit).
 *
 * Usage: f7doomd <dir-containing-doom_run.sh>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define KEY_F12     88
#define EV_KEY      1
#define MAXDEV      16
#define EVIOCGBIT_KEY(len)  (0x80004521 | ((len) << 16))    /* _IOC(_IOC_READ,'E',0x20+EV_KEY,len) */

struct ev { long sec, usec; unsigned short type, code; int value; };

static struct { int fd; char name[32]; } devs[MAXDEV];
static int ndevs;

static void logmsg(const char *fmt, const char *arg)
{
    time_t t = time(NULL);
    char ts[32];

    strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
    printf("[%s] ", ts);
    printf(fmt, arg);
    printf("\n");
    fflush(stdout);
}

static int has_f12(int fd)
{
    unsigned char bits[KEY_F12 / 8 + 1];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT_KEY(sizeof(bits)), bits) < 0)
        return 0;
    return (bits[KEY_F12 / 8] >> (KEY_F12 % 8)) & 1;
}

static int known(const char *name)
{
    for (int i = 0; i < ndevs; i++)
        if (!strcmp(devs[i].name, name))
            return 1;
    return 0;
}

static void rescan(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;

    if (!d)
        return;
    while ((e = readdir(d)) && ndevs < MAXDEV) {
        char path[64];
        int fd;

        if (strncmp(e->d_name, "event", 5) || known(e->d_name))
            continue;
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (!has_f12(fd)) {
            close(fd);
            continue;
        }
        devs[ndevs].fd = fd;
        snprintf(devs[ndevs].name, sizeof(devs[ndevs].name), "%s", e->d_name);
        ndevs++;
        logmsg("watching %s for F12", path);
    }
    closedir(d);
}

static void drop(int i)
{
    logmsg("lost %s", devs[i].name);
    close(devs[i].fd);
    devs[i] = devs[--ndevs];
}

static void drain_all(void)
{
    struct ev ie;

    for (int i = 0; i < ndevs; i++)
        while (read(devs[i].fd, &ie, sizeof(ie)) == (int) sizeof(ie))
            ;
}

static void run_doom(const char *dir)
{
    char script[256];
    pid_t pid;
    int status;

    snprintf(script, sizeof(script), "%s/doom_run.sh", dir);
    logmsg("F12: launching %s", script);
    pid = fork();
    if (pid == 0) {
        /* Run a private copy: sh reads scripts lazily, so updating the
         * stick's copy mid-run would otherwise corrupt the running launch. */
        execl("/bin/sh", "sh", "-c",
              "cp \"$0\" /tmp/f7doom_run.sh && exec sh /tmp/f7doom_run.sh \"$1\"",
              script, dir, (char *) NULL);
        _exit(127);
    }
    if (pid > 0)
        waitpid(pid, &status, 0);
    logmsg("back from %s", script);
    drain_all();
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    signal(SIGCHLD, SIG_DFL);
    logmsg("f7doomd started, dir=%s", dir);

    for (;;) {
        struct pollfd p[MAXDEV];
        int n, launch = 0;

        rescan();
        for (int i = 0; i < ndevs; i++) {
            p[i].fd = devs[i].fd;
            p[i].events = POLLIN;
            p[i].revents = 0;
        }
        n = poll(p, ndevs, 2000);
        if (n <= 0)
            continue;
        for (int i = ndevs - 1; i >= 0; i--) {
            struct ev ie;
            ssize_t r;

            if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                drop(i);
                continue;
            }
            while ((r = read(devs[i].fd, &ie, sizeof(ie))) == (ssize_t) sizeof(ie))
                if (ie.type == EV_KEY && ie.code == KEY_F12 && ie.value == 1)
                    launch = 1;
            (void) r;
        }
        if (launch)
            run_doom(dir);
    }
}
