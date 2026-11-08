// DOOM keyboard input for the ATOTO F7: Linux evdev (/dev/input/eventN) plus a
// UDP "network keyboard" (the unit has no usable USB-HID or BT-HID path).
// UDP packet (port F7_NETKBD_PORT): "F7K" + one byte per currently-held Linux
// keycode. The sender repeats it every ~50ms; we diff against the previous set,
// so a lost packet can't leave a key stuck. Silence > NETKBD_TIMEOUT_MS releases all.
// We define the Linux keycodes locally to avoid KEY_* name clashes with doomkeys.h.

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "config.h"
#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "i_system.h"

int vanilla_keyboard_mapping = 1;

// While DOOM runs it grabs every input device (EVIOCGRAB), so taps and keys
// don't queue up for the paused stock UI and replay when it resumes.
// F12 quits back to the UI (the launcher daemon uses F12 to start DOOM).

#include <sys/ioctl.h>
#include "i_system.h"

#define EVIOCGRAB 0x40044590    // _IOW('E', 0x90, int)

// Linux input_event (32-bit: long=4 -> 16 bytes). Avoid <linux/input.h>.
struct ev { long sec; long usec; unsigned short type; unsigned short code; int value; };
#define EV_KEY 1

// Linux input-event-codes we care about
enum {
    L_ESC=1, L_1=2,L_2=3,L_3=4,L_4=5,L_5=6,L_6=7,L_7=8,L_8=9,L_9=10,L_0=11,
    L_MINUS=12,L_EQUAL=13,L_BSP=14,L_TAB=15,
    L_Q=16,L_W=17,L_E=18,L_R=19,L_T=20,L_Y=21,L_U=22,L_I=23,L_O=24,L_P=25,
    L_ENTER=28,L_LCTRL=29,
    L_A=30,L_S=31,L_D=32,L_F=33,L_G=34,L_H=35,L_J=36,L_K=37,L_L=38,
    L_LSHIFT=42, L_Z=44,L_X=45,L_C=46,L_V=47,L_B=48,L_N=49,L_M=50,
    L_RSHIFT=54, L_LALT=56, L_SPACE=57,
    L_F12=88, L_RCTRL=97, L_RALT=100,
    L_UP=103,L_LEFT=105,L_RIGHT=106,L_DOWN=108,
};

#define F7_NETKBD_PORT 5555
#define NETKBD_TIMEOUT_MS 500

#define MAXFD 16
static int fds[MAXFD];
static int nfds = 0;

// map a Linux keycode -> (doom key in *dk, typed char in *ch)
static void map_key(unsigned short c, unsigned char *dk, unsigned char *ch)
{
    *dk = 0; *ch = 0;
    switch (c) {
        case L_UP:    *dk = KEY_UPARROW;    return;
        case L_DOWN:  *dk = KEY_DOWNARROW;  return;
        case L_LEFT:  *dk = KEY_LEFTARROW;  return;
        case L_RIGHT: *dk = KEY_RIGHTARROW; return;
        case L_LCTRL: case L_RCTRL: *dk = KEY_FIRE;   return;
        case L_SPACE: *dk = KEY_USE; *ch = ' ';       return;
        case L_LSHIFT: case L_RSHIFT: *dk = KEY_RSHIFT; return;
        case L_LALT: case L_RALT: *dk = KEY_LALT;     return;
        case L_ENTER: *dk = KEY_ENTER; *ch = '\r';    return;
        case L_ESC:   *dk = KEY_ESCAPE;               return;
        case L_TAB:   *dk = KEY_TAB; *ch = '\t';      return;
        case L_BSP:   *dk = KEY_BACKSPACE;            return;
        default: break;
    }
    // digits
    if (c >= L_1 && c <= L_9) { *dk = *ch = '1' + (c - L_1); return; }
    if (c == L_0) { *dk = *ch = '0'; return; }
    // letters (lowercase)
    static const struct { unsigned short c; char a; } L[] = {
        {L_A,'a'},{L_B,'b'},{L_C,'c'},{L_D,'d'},{L_E,'e'},{L_F,'f'},{L_G,'g'},
        {L_H,'h'},{L_I,'i'},{L_J,'j'},{L_K,'k'},{L_L,'l'},{L_M,'m'},{L_N,'n'},
        {L_O,'o'},{L_P,'p'},{L_Q,'q'},{L_R,'r'},{L_S,'s'},{L_T,'t'},{L_U,'u'},
        {L_V,'v'},{L_W,'w'},{L_X,'x'},{L_Y,'y'},{L_Z,'z'},
    };
    for (unsigned i = 0; i < sizeof(L)/sizeof(L[0]); i++)
        if (L[i].c == c) { *dk = *ch = (unsigned char)L[i].a; return; }
}

static void post_key(unsigned short code, int pressed)
{
    unsigned char dk, ch;
    event_t ev;

    map_key(code, &dk, &ch);
    if (dk == 0)
        return;
    ev.type  = pressed ? ev_keydown : ev_keyup;
    ev.data1 = dk;
    ev.data2 = pressed ? ch : 0;
    D_PostEvent(&ev);
}

static int net_fd = -1;
static unsigned char net_held[256];    // keycodes currently held per the sender
static long long net_last_ms;

static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void net_apply(const unsigned char *held)
{
    for (int c = 0; c < 256; c++) {
        if (held[c] != net_held[c]) {
            net_held[c] = held[c];
            post_key((unsigned short)c, held[c]);
        }
    }
}

static void net_init(void)
{
    struct sockaddr_in a;

    net_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (net_fd < 0) { perror("netkbd: socket"); return; }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(F7_NETKBD_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(net_fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("netkbd: bind");
        close(net_fd);
        net_fd = -1;
        return;
    }
    fcntl(net_fd, F_SETFL, O_NONBLOCK);
    printf("netkbd: listening on udp/%d\n", F7_NETKBD_PORT);
}

static void net_poll(void)
{
    unsigned char buf[300], held[256];
    int n, got = 0;

    if (net_fd < 0)
        return;
    // drain the socket; only the newest snapshot matters
    while ((n = recv(net_fd, buf, sizeof(buf), 0)) >= 3) {
        if (memcmp(buf, "F7K", 3) != 0)
            continue;
        memset(held, 0, sizeof(held));
        for (int i = 3; i < n; i++)
            held[buf[i]] = 1;
        got = 1;
    }
    if (got) {
        net_last_ms = now_ms();
        net_apply(held);
    } else if (net_last_ms && now_ms() - net_last_ms > NETKBD_TIMEOUT_MS) {
        memset(held, 0, sizeof(held));    // sender went quiet: release everything
        net_apply(held);
        net_last_ms = 0;
    }
}

void I_InitInput(void)
{
    net_init();

    DIR *d = opendir("/dev/input");
    struct dirent *e;
    if (!d) { printf("evdev: cannot open /dev/input\n"); return; }
    while ((e = readdir(d)) && nfds < MAXFD) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        fds[nfds++] = fd;
        printf("evdev: reading %s%s\n", path,
               !getenv("DOOM_NO_GRAB") && ioctl(fd, EVIOCGRAB, 1) == 0 ? " (grabbed)" : "");
    }
    closedir(d);
    if (nfds == 0) printf("evdev: no /dev/input/event* opened — no keyboard?\n");
}

void I_GetEvent(void)
{
    struct ev ie;
    for (int i = 0; i < nfds; i++) {
        while (read(fds[i], &ie, sizeof(ie)) == (int)sizeof(ie)) {
            if (ie.type != EV_KEY) continue;
            if (ie.code == L_F12 && ie.value == 1)
                I_Quit();                       // saves config, exits to the UI
            post_key(ie.code, ie.value != 0);    // press (1) or autorepeat (2)
        }
    }
    net_poll();
}

// Called by the menu's quit path (m_menu.c): run the exit handlers (saves
// the config, stops sound) and exit.
void kbd_shutdown(void)
{
    printf("Exiting normally.\n");
    fflush(stdout);
    I_Quit();
}
