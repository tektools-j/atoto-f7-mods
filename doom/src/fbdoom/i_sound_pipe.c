// Sound for the ATOTO F7: a small software mixer (plus OPL music, opl_pipe.c) that streams 16-bit
// stereo PCM (as an endless WAV) into a FIFO read by the firmware's
// /usr/local/bin/as_track, which plays through as_server -> eCos -> speakers.
// There is no ALSA/OSS on this unit, and our static musl binary can't link the
// firmware's glibc C++ audio client, so a pipe to its test player is the bridge.
//
// Env overrides: DOOM_SND_FIFO (default /tmp/doom_snd.wav),
//                DOOM_SND_PLAYER (default /usr/local/bin/as_track, "" = don't spawn).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

// opl_pipe.c: adds emulated OPL music into the mix (no-op until music init).
void OPL_Pipe_Mix(int32_t *acc, unsigned int nframes);

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

#define OUT_RATE        44100
#define NUM_CHANNELS    16
#define MIX_FRAMES      256         // ~5.8ms per write
#define PIPE_BYTES      4096        // keep the FIFO shallow for low latency

typedef struct
{
    const byte *data;       // unsigned 8-bit mono samples
    unsigned int length;    // in samples
    unsigned int pos;       // 16.16 fixed point
    unsigned int step;      // 16.16 fixed point (src_rate / OUT_RATE)
    int left, right;        // 0..255
} channel_t;

typedef struct
{
    const byte *data;
    unsigned int length;
    unsigned int rate;
} sfxdata_t;

static channel_t channels[NUM_CHANNELS];
static pthread_mutex_t chan_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mix_thread;
static volatile int mix_running;
static boolean use_sfx_prefix;
static const char *fifo_path;
static pid_t player_pid = -1;

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    if (sfx->link != NULL)
        sfx = sfx->link;

    if (use_sfx_prefix)
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
}

// Decode a DMX sound lump: u16 format(3), u16 rate, u32 count, then the
// samples padded by 16 bytes on each side.
static sfxdata_t *CacheSfx(sfxinfo_t *sfx)
{
    sfxdata_t *s;
    byte *lump;
    unsigned int lumplen, rate, count;

    if (sfx->driver_data != NULL)
        return sfx->driver_data;

    lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    lumplen = W_LumpLength(sfx->lumpnum);

    if (lumplen < 8 || lump[0] != 0x03 || lump[1] != 0x00)
        return NULL;

    rate = lump[2] | (lump[3] << 8);
    count = lump[4] | (lump[5] << 8) | (lump[6] << 16) | ((unsigned) lump[7] << 24);
    if (count > lumplen - 8 || count <= 48 || rate == 0)
        return NULL;

    s = Z_Malloc(sizeof(*s), PU_STATIC, NULL);
    s->data = lump + 8 + 16;
    s->length = count - 32;
    s->rate = rate;
    sfx->driver_data = s;
    return s;
}

static void SetParams(channel_t *c, int vol, int sep)
{
    int left = ((254 - sep) * vol) / 127;
    int right = (sep * vol) / 127;

    c->left = left < 0 ? 0 : left > 255 ? 255 : left;
    c->right = right < 0 ? 0 : right > 255 ? 255 : right;
}

static void MixChunk(int16_t *out, int frames)
{
    int32_t acc[MIX_FRAMES * 2];

    memset(acc, 0, sizeof(int32_t) * frames * 2);

    pthread_mutex_lock(&chan_lock);
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        channel_t *c = &channels[i];

        if (c->data == NULL)
            continue;
        for (int f = 0; f < frames; f++)
        {
            unsigned int idx = c->pos >> 16;
            int s;

            if (idx >= c->length)
            {
                c->data = NULL;
                break;
            }
            s = ((int) c->data[idx] - 128) << 8;
            acc[f * 2]     += (s * c->left) >> 8;
            acc[f * 2 + 1] += (s * c->right) >> 8;
            c->pos += c->step;
        }
    }
    pthread_mutex_unlock(&chan_lock);

    OPL_Pipe_Mix(acc, frames);

    for (int i = 0; i < frames * 2; i++)
    {
        int32_t v = acc[i];
        out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
}

static void PutLE32(byte *p, unsigned int v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void PutLE16(byte *p, unsigned int v)
{
    p[0] = v; p[1] = v >> 8;
}

static int WriteAll(int fd, const void *buf, size_t len)
{
    const byte *p = buf;

    while (len > 0)
    {
        ssize_t n = write(fd, p, len);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        len -= n;
    }
    return 0;
}

static void *MixThread(void *arg)
{
    int16_t buf[MIX_FRAMES * 2];
    byte hdr[44];
    int fd;

    (void) arg;

    // Blocks until the player opens the read end.
    fd = open(fifo_path, O_WRONLY);
    if (fd < 0)
    {
        perror("sound_pipe: open fifo");
        mix_running = 0;
        return NULL;
    }
    fcntl(fd, F_SETPIPE_SZ, PIPE_BYTES);

    // WAV header declaring an effectively endless data chunk; as_track stops
    // reading once it has consumed the declared length.
    memcpy(hdr, "RIFF", 4);  PutLE32(hdr + 4, 0x7ffffff0);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    PutLE32(hdr + 16, 16);   PutLE16(hdr + 20, 1);  PutLE16(hdr + 22, 2);
    PutLE32(hdr + 24, OUT_RATE);
    PutLE32(hdr + 28, OUT_RATE * 4);
    PutLE16(hdr + 32, 4);    PutLE16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4);  PutLE32(hdr + 40, 0x7fffffc8);

    if (WriteAll(fd, hdr, sizeof(hdr)) < 0)
        goto done;

    while (mix_running)
    {
        MixChunk(buf, MIX_FRAMES);
        if (WriteAll(fd, buf, sizeof(buf)) < 0)
        {
            perror("sound_pipe: write");
            break;
        }
    }

done:
    close(fd);
    mix_running = 0;
    return NULL;
}

static void SpawnPlayer(void)
{
    const char *player = getenv("DOOM_SND_PLAYER");

    if (player == NULL)
        player = "/usr/local/bin/as_track";
    if (player[0] == '\0')
        return;

    player_pid = fork();
    if (player_pid == 0)
    {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0)
        {
            dup2(devnull, 1);
            dup2(devnull, 2);
        }
        execl(player, player, fifo_path, (char *) NULL);
        _exit(127);
    }
    if (player_pid < 0)
        perror("sound_pipe: fork");
}

static boolean I_Pipe_InitSound(boolean _use_sfx_prefix)
{
    struct stat st;

    use_sfx_prefix = _use_sfx_prefix;
    // OPL music is rendered at snd_samplerate into this same stream.
    snd_samplerate = OUT_RATE;
    fifo_path = getenv("DOOM_SND_FIFO");
    if (fifo_path == NULL)
        fifo_path = "/tmp/doom_snd.wav";

    if (stat(fifo_path, &st) == 0 && !S_ISFIFO(st.st_mode))
        unlink(fifo_path);
    if (stat(fifo_path, &st) != 0 && mkfifo(fifo_path, 0666) != 0)
    {
        perror("sound_pipe: mkfifo");
        return false;
    }

    // A dead player must not kill the game.
    signal(SIGPIPE, SIG_IGN);

    memset(channels, 0, sizeof(channels));
    SpawnPlayer();

    mix_running = 1;
    if (pthread_create(&mix_thread, NULL, MixThread, NULL) != 0)
    {
        mix_running = 0;
        return false;
    }
    printf("sound_pipe: %d Hz stereo -> %s\n", OUT_RATE, fifo_path);
    return true;
}

static void I_Pipe_ShutdownSound(void)
{
    mix_running = 0;
    if (player_pid > 0)
    {
        kill(player_pid, SIGTERM);
        waitpid(player_pid, NULL, 0);
        player_pid = -1;
    }
}

static int I_Pipe_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_Pipe_UpdateSound(void)
{
}

static void I_Pipe_UpdateSoundParams(int handle, int vol, int sep)
{
    if (handle < 0 || handle >= NUM_CHANNELS)
        return;
    pthread_mutex_lock(&chan_lock);
    SetParams(&channels[handle], vol, sep);
    pthread_mutex_unlock(&chan_lock);
}

static int I_Pipe_StartSound(sfxinfo_t *sfx, int handle, int vol, int sep)
{
    sfxdata_t *s;
    channel_t *c;

    if (handle < 0 || handle >= NUM_CHANNELS || !mix_running)
        return -1;
    s = CacheSfx(sfx);
    if (s == NULL)
        return -1;

    pthread_mutex_lock(&chan_lock);
    c = &channels[handle];
    c->data = s->data;
    c->length = s->length;
    c->pos = 0;
    c->step = (unsigned int) (((unsigned long long) s->rate << 16) / OUT_RATE);
    SetParams(c, vol, sep);
    pthread_mutex_unlock(&chan_lock);
    return handle;
}

static void I_Pipe_StopSound(int handle)
{
    if (handle < 0 || handle >= NUM_CHANNELS)
        return;
    pthread_mutex_lock(&chan_lock);
    channels[handle].data = NULL;
    pthread_mutex_unlock(&chan_lock);
}

static boolean I_Pipe_SoundIsPlaying(int handle)
{
    boolean playing;

    if (handle < 0 || handle >= NUM_CHANNELS)
        return false;
    pthread_mutex_lock(&chan_lock);
    playing = channels[handle].data != NULL;
    pthread_mutex_unlock(&chan_lock);
    return playing;
}

static void I_Pipe_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    for (int i = 0; i < num_sounds; i++)
    {
        char namebuf[9];

        GetSfxLumpName(&sounds[i], namebuf, sizeof(namebuf));
        sounds[i].lumpnum = W_CheckNumForName(namebuf);
        if (sounds[i].lumpnum != -1)
            CacheSfx(&sounds[i]);
    }
}

static snddevice_t sound_pipe_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t sound_pipe_module =
{
    sound_pipe_devices,
    arrlen(sound_pipe_devices),
    I_Pipe_InitSound,
    I_Pipe_ShutdownSound,
    I_Pipe_GetSfxLumpNum,
    I_Pipe_UpdateSound,
    I_Pipe_UpdateSoundParams,
    I_Pipe_StartSound,
    I_Pipe_StopSound,
    I_Pipe_SoundIsPlaying,
    I_Pipe_PrecacheSounds,
};
