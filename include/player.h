#ifndef PLAYER_H
#define PLAYER_H

#include <initguid.h>
#include <audioclient.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include <stdbool.h>

#include "strings.h"

typedef struct
{
    float *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv_not_full;
    bool stopped;
} RingBuffer;

void ring_init(RingBuffer *r, size_t capacity);
void ring_destroy(RingBuffer *r);
void ring_push(RingBuffer *r, const float *data, size_t n);
size_t ring_pop(RingBuffer *r, float *out, size_t n);
void ring_stop(RingBuffer *r);
size_t ring_pending(RingBuffer *r);

typedef struct
{
    IAudioClient *audio_client;
    IAudioRenderClient *render_client;
    HANDLE audio_event;
    HANDLE stop_event;
    HANDLE thread;
    UINT32 buffer_frame_count;
    RingBuffer ring;
} Player;

DWORD WINAPI wasapi_render_thread(LPVOID param);
bool player_init(Player *p, int32_t sample_rate, double ring_seconds, StringView device_name);
void player_destroy(Player *p);

typedef struct
{
    int32_t sample_rate;
    double ring_seconds;
    StringView output_device;
    bool monitor_enabled;
    StringView monitor_device;
} MultiPlayerConfig;

typedef struct
{
    Player output;
    Player monitor;
    bool monitor_enabled;
} MultiPlayer;

bool multi_player_init(MultiPlayer *mp, const MultiPlayerConfig *cfg);
void multi_player_wait_drain(MultiPlayer *mp);
void multi_player_destroy(MultiPlayer *mp);

#endif
