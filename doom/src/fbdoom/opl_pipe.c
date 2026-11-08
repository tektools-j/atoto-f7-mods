//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//
// DESCRIPTION:
//     OPL software emulation backend for the ATOTO F7 port. Derived from
//     Chocolate Doom 2.1.0 opl_sdl.c: instead of hooking SDL_mixer, the
//     emulator is driven by the i_sound_pipe.c mixer thread via OPL_Pipe_Mix().
//

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "dbopl.h"

#include "opl.h"
#include "opl_internal.h"

#include "opl_queue.h"

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

// When the callback mutex is locked using OPL_Lock, callback functions
// are not invoked.

static pthread_mutex_t callback_mutex = PTHREAD_MUTEX_INITIALIZER;

// Queue of callbacks waiting to be invoked.

static opl_callback_queue_t *callback_queue;

// Mutex used to control access to the callback queue.

static pthread_mutex_t callback_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

// Current time, in us since startup:

static uint64_t current_time;

// If non-zero, playback is currently paused.

static int opl_pipe_paused;

// Non-zero once Init has run; OPL_Pipe_Mix is a no-op until then.

static volatile int opl_pipe_active;

// Time offset (in us) due to the fact that callbacks
// were previously paused.

static uint64_t pause_offset;

// OPL software emulator structure.

static Chip opl_chip;

// Temporary mixing buffer used by the mixing callback.

static int32_t *mix_buffer = NULL;

// Register number that was written.

static int register_num = 0;

// Timers; DBOPL does not do timer stuff itself.

static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

// Output sample rate (the sound mixer runs at opl_sample_rate).

static int mixing_freq;

// Advance time by the specified number of samples, invoking any
// callback functions as appropriate.

static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    pthread_mutex_lock(&callback_queue_mutex);

    // Advance time.

    us = ((uint64_t) nsamples * OPL_SECOND) / mixing_freq;
    current_time += us;

    if (opl_pipe_paused)
    {
        pause_offset += us;
    }

    // Are there callbacks to invoke now?  Keep invoking them
    // until there are no more left.

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        // Pop the callback from the queue to invoke it.

        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // The mutex stuff here is a bit complicated.  We must
        // hold callback_mutex when we invoke the callback (so that
        // the control thread can use OPL_Lock() to prevent callbacks
        // from being invoked), but we must not be holding
        // callback_queue_mutex, as the callback must be able to
        // call OPL_SetCallback to schedule new callbacks.

        pthread_mutex_unlock(&callback_queue_mutex);

        pthread_mutex_lock(&callback_mutex);
        callback(callback_data);
        pthread_mutex_unlock(&callback_mutex);

        pthread_mutex_lock(&callback_queue_mutex);
    }

    pthread_mutex_unlock(&callback_queue_mutex);
}

// Call the OPL emulator code to fill the specified buffer.

static void FillBuffer(int16_t *buffer, unsigned int nsamples)
{
    unsigned int i;

    // This seems like a reasonable assumption.  mix_buffer is
    // 1 second long, which should always be much longer than the
    // mixer chunk.

    assert(nsamples < mixing_freq);

    Chip__GenerateBlock2(&opl_chip, nsamples, mix_buffer);

    // Mix into the destination buffer, doubling up into stereo.

    for (i=0; i<nsamples; ++i)
    {
        buffer[i * 2] = (int16_t) mix_buffer[i];
        buffer[i * 2 + 1] = (int16_t) mix_buffer[i];
    }
}

// The emulator's raw output peaks around -20 dBFS, far under the sound
// effects, so music gets a fixed gain (DOOM_MUSIC_GAIN env, default 6).

static int music_gain = 6;

// Called by the sound mixer thread (i_sound_pipe.c): render nframes of OPL
// output and add it into an interleaved stereo int32 accumulator.

void OPL_Pipe_Mix(int32_t *acc, unsigned int nframes)
{
    int16_t buffer[1024 * 2];
    unsigned int filled = 0;

    if (!opl_pipe_active)
    {
        return;
    }

    while (nframes > 1024)
    {
        OPL_Pipe_Mix(acc, 1024);
        acc += 1024 * 2;
        nframes -= 1024;
    }

    // Repeatedly call the OPL emulator update function until the buffer is
    // full.

    while (filled < nframes)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;

        pthread_mutex_lock(&callback_queue_mutex);

        // Work out the time until the next callback waiting in
        // the callback queue must be invoked.  We can then fill the
        // buffer with this many samples.

        if (opl_pipe_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = nframes - filled;
        }
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next_callback_time - current_time) * mixing_freq;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > nframes - filled)
            {
                nsamples = nframes - filled;
            }
        }

        pthread_mutex_unlock(&callback_queue_mutex);

        // Add emulator output to buffer.

        FillBuffer(buffer + filled * 2, nsamples);
        filled += nsamples;

        // Invoke callbacks for this point in time.

        AdvanceTime(nsamples);
    }

    for (unsigned int i = 0; i < nframes * 2; ++i)
    {
        acc[i] += buffer[i] * music_gain;
    }
}

static void OPL_Pipe_Shutdown(void)
{
    opl_pipe_active = 0;

    if (callback_queue != NULL)
    {
        OPL_Queue_Destroy(callback_queue);
        callback_queue = NULL;
    }

    free(mix_buffer);
    mix_buffer = NULL;
}

static int OPL_Pipe_Init(unsigned int port_base)
{
    opl_pipe_paused = 0;
    pause_offset = 0;

    // Queue structure of callbacks to invoke.

    callback_queue = OPL_Queue_Create();
    current_time = 0;

    mixing_freq = opl_sample_rate;

    if (getenv("DOOM_MUSIC_GAIN") != NULL)
    {
        music_gain = atoi(getenv("DOOM_MUSIC_GAIN"));
    }

    // Mix buffer:

    mix_buffer = malloc(mixing_freq * sizeof(uint32_t));

    // Create the emulator structure:

    DBOPL_InitTables();
    Chip__Chip(&opl_chip);
    Chip__Setup(&opl_chip, mixing_freq);

    opl_pipe_active = 1;

    return 1;
}

static unsigned int OPL_Pipe_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    // If the timer is enabled, calculate the time when the timer
    // will expire.

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer1.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        default:
            Chip__WriteReg(&opl_chip, reg_num, value);
            break;
    }
}

static void OPL_Pipe_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_Pipe_SetCallback(uint64_t us, opl_callback_t callback,
                                void *data)
{
    pthread_mutex_lock(&callback_queue_mutex);
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
    pthread_mutex_unlock(&callback_queue_mutex);
}

static void OPL_Pipe_ClearCallbacks(void)
{
    pthread_mutex_lock(&callback_queue_mutex);
    OPL_Queue_Clear(callback_queue);
    pthread_mutex_unlock(&callback_queue_mutex);
}

static void OPL_Pipe_Lock(void)
{
    pthread_mutex_lock(&callback_mutex);
}

static void OPL_Pipe_Unlock(void)
{
    pthread_mutex_unlock(&callback_mutex);
}

static void OPL_Pipe_SetPaused(int paused)
{
    opl_pipe_paused = paused;
}

static void OPL_Pipe_AdjustCallbacks(float factor)
{
    pthread_mutex_lock(&callback_queue_mutex);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    pthread_mutex_unlock(&callback_queue_mutex);
}

opl_driver_t opl_pipe_driver =
{
    "pipe",
    OPL_Pipe_Init,
    OPL_Pipe_Shutdown,
    OPL_Pipe_PortRead,
    OPL_Pipe_PortWrite,
    OPL_Pipe_SetCallback,
    OPL_Pipe_ClearCallbacks,
    OPL_Pipe_Lock,
    OPL_Pipe_Unlock,
    OPL_Pipe_SetPaused,
    OPL_Pipe_AdjustCallbacks,
};

