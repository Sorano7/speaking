#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdbool.h>

#include "player.h"

void ring_init(RingBuffer *r, size_t capacity)
{
    r->buffer = malloc(capacity * sizeof(float));
    r->capacity = capacity;
    r->head = r->tail = r->count = 0;
    r->stopped = false;
    InitializeCriticalSection(&r->cs);
    InitializeConditionVariable(&r->cv_not_full);
}

void ring_destroy(RingBuffer *r)
{
    DeleteCriticalSection(&r->cs);
    free(r->buffer);
}

void ring_push(RingBuffer *r, const float *data, size_t n)
{
    EnterCriticalSection(&r->cs);
    size_t written = 0;
    while (written < n)
    {
        while (r->count == r->capacity && !r->stopped)
        {
            SleepConditionVariableCS(&r->cv_not_full, &r->cs, INFINITE);
        }
        if (r->stopped) break;

        size_t space = r->capacity - r->count;
        size_t to_copy = n - written;
        if (to_copy > space) to_copy = space;

        for (size_t i = 0; i < to_copy; i++)
        {
            r->buffer[r->tail] = data[written+i];
            r->tail = (r->tail+1) % r->capacity;
        }
        r->count += to_copy;
        written += to_copy;
    }
    LeaveCriticalSection(&r->cs);
}

size_t ring_pop(RingBuffer *r, float *out, size_t n)
{
    EnterCriticalSection(&r->cs);
    size_t to_copy = (n < r->count) ? n : r->count;
    for (size_t i = 0; i < to_copy; i++)
    {
        out[i] = r->buffer[r->head];
        r->head = (r->head+1) % r->capacity;
    }
    r->count -= to_copy;
    WakeConditionVariable(&r->cv_not_full);
    LeaveCriticalSection(&r->cs);
    return to_copy;
}

void ring_stop(RingBuffer *r)
{
    EnterCriticalSection(&r->cs);
    r->stopped = true;
    WakeAllConditionVariable(&r->cv_not_full);
    LeaveCriticalSection(&r->cs);

}
size_t ring_pending(RingBuffer *r)
{
    EnterCriticalSection(&r->cs);
    size_t n = r->count;
    LeaveCriticalSection(&r->cs);
    return n;
}

DWORD WINAPI wasapi_render_thread(LPVOID param)
{
    Player *p = param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HANDLE events[2] = {p->audio_event, p->stop_event};

    for (;;)
    {
        DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0+1) break;

        UINT32 padding = 0;
        p->audio_client->lpVtbl->GetCurrentPadding(p->audio_client, &padding);
        UINT32 available = p->buffer_frame_count - padding;
        if (available == 0) continue;

        BYTE *data = NULL;
        HRESULT hr = p->render_client->lpVtbl->GetBuffer(p->render_client, available, &data);
        if (FAILED(hr)) continue;

        size_t got = ring_pop(&p->ring, (float *)data, available);
        DWORD flags = 0;
        if (got < available)
        {
            memset((float *)data + got, 0, (available - got) * sizeof(float));
            if (got == 0) flags = AUDCLNT_BUFFERFLAGS_SILENT;
        }
        p->render_client->lpVtbl->ReleaseBuffer(p->render_client, available, flags);
    }

    CoUninitialize();
    return 0;
}

static wchar_t *utf8_to_wide(StringView s)
{
    char buffer[s.length+1];
    memcpy(buffer, s.data, s.length+1);
    buffer[s.length] = '\0';

    int len = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *w = malloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, buffer, -1, w, len);
    return w;
}

static HRESULT find_render_device(IMMDeviceEnumerator *enumerator, const wchar_t *name, IMMDevice **out_device)
{
    *out_device = NULL;

    if (!name || name[0] == L'\0' || _wcsicmp(name, L"default") == 0) {
        return enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender, eConsole, out_device);
    }

    IMMDeviceCollection *collection = NULL;
    HRESULT hr = enumerator->lpVtbl->EnumAudioEndpoints(enumerator, eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return hr;

    UINT count = 0;
    collection->lpVtbl->GetCount(collection, &count);

    HRESULT result = E_FAIL;
    for (UINT i = 0; i < count; i++)
    {
        IMMDevice *device = NULL;
        if (FAILED(collection->lpVtbl->Item(collection, i, &device))) continue;

        int matched = 0;
        IPropertyStore *props = NULL;
        if (SUCCEEDED(device->lpVtbl->OpenPropertyStore(device, STGM_READ, &props)))
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->lpVtbl->GetValue(props, &PKEY_Device_FriendlyName, &pv)) &&
                    pv.vt == VT_LPWSTR && wcsstr(pv.pwszVal, name) != NULL)
            {
                matched = 1;
            }
            PropVariantClear(&pv);
            props->lpVtbl->Release(props);
        }

        if (matched)
        {
            *out_device = device;
            result = S_OK;
            break;
        }
        device->lpVtbl->Release(device);
    }

    collection->lpVtbl->Release(collection);
    return result;
}

bool player_init(Player *p, int32_t sample_rate, double ring_seconds, StringView device_name)
{
    memset(p, 0, sizeof(Player));
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
        return false;

    IMMDeviceEnumerator *enumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator);

    if (FAILED(hr)) return false;

    wchar_t *wide_name = utf8_to_wide(device_name);
    IMMDevice *device = NULL;
    hr = find_render_device(enumerator, wide_name, &device);
    free(wide_name);
    enumerator->lpVtbl->Release(enumerator);
    if (FAILED(hr)) return false;

    hr = device->lpVtbl->Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&p->audio_client);
    device->lpVtbl->Release(device);
    if (FAILED(hr)) return false;

    WAVEFORMATEXTENSIBLE wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = 1;
    wfx.Format.nSamplesPerSec = (DWORD)sample_rate;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = (WORD)(wfx.Format.nChannels * wfx.Format.wBitsPerSample / 8);
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = SPEAKER_FRONT_CENTER;
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    REFERENCE_TIME buffer_duration = 100 * 10000;
    hr = p->audio_client->lpVtbl->Initialize(
        p->audio_client,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        buffer_duration,
        0,
        (WAVEFORMATEX *)&wfx,
        NULL);
    if (FAILED(hr)) return false;

    hr = p->audio_client->lpVtbl->GetBufferSize(p->audio_client, &p->buffer_frame_count);
    if (FAILED(hr)) return false;

    p->audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = p->audio_client->lpVtbl->SetEventHandle(p->audio_client, p->audio_event);
    if (FAILED(hr)) return false;

    hr = p->audio_client->lpVtbl->GetService(p->audio_client, &IID_IAudioRenderClient,
            (void **)&p->render_client);
    if (FAILED(hr)) return false;

    ring_init(&p->ring, (size_t)(sample_rate * ring_seconds));

    p->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    p->thread = CreateThread(NULL, 0, wasapi_render_thread, p, 0, NULL);

    hr = p->audio_client->lpVtbl->Start(p->audio_client);
    if (FAILED(hr)) return false;

    return true;
}

void player_destroy(Player *p)
{
    ring_stop(&p->ring);
    SetEvent(p->stop_event);
    WaitForSingleObject(p->thread, INFINITE);
    CloseHandle(p->thread);
    CloseHandle(p->stop_event);
    CloseHandle(p->audio_event);

    p->audio_client->lpVtbl->Stop(p->audio_client);
    p->render_client->lpVtbl->Release(p->render_client);
    p->audio_client->lpVtbl->Release(p->audio_client);

    ring_destroy(&p->ring);
    CoUninitialize();
}

bool multi_player_init(MultiPlayer *mp, const MultiPlayerConfig *cfg)
{
    memset(mp, 0, sizeof(MultiPlayer));
    mp->monitor_enabled = cfg->monitor_enabled;
    if (!player_init(&mp->output, cfg->sample_rate, cfg->ring_seconds, cfg->output_device))
        return false;

    if (mp->monitor_enabled)
    {
        if (!player_init(&mp->monitor, cfg->sample_rate, cfg->ring_seconds, cfg->monitor_device))
            return false;
    }
    return true;
}

void multi_player_wait_drain(MultiPlayer *mp)
{
    for (;;)
    {
        size_t pending = ring_pending(&mp->output.ring);
        if (mp->monitor_enabled)
        {
            size_t mp_pending = ring_pending(&mp->monitor.ring);
            if (mp_pending > pending) pending = mp_pending;
        }
        if (pending == 0) break;
        Sleep(20);
    }
    Sleep(150);
}

void multi_player_destroy(MultiPlayer *mp)
{
    player_destroy(&mp->output);
    if (mp->monitor_enabled)
        player_destroy(&mp->monitor);
}
