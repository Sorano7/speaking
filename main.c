#include "sherpa-onnx/c-api.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>

#include "player.h"
#include "overlay.h"
#include "tray.h"

#define STRINGS_IMPLEMENTATION
#include "strings.h"

#define CUT_IMPLEMENTATION
#include "cut.h"

typedef enum
{
    LANG_OTHER,
    LANG_EN,
    LANG_ZH,
    LANG_COUNT,
} Lang;

static SherpaOnnxGenerationConfig *tts_gen_config_lookup[LANG_COUNT] = {0};
static const SherpaOnnxOfflineTts *tts_lookup[LANG_COUNT]            = {0};

void tts_config_init(SherpaOnnxOfflineTtsConfig *config)
{
    memset(config, 0, sizeof(SherpaOnnxOfflineTtsConfig));
    config->model.num_threads = 2;
    config->model.debug       = 0;
    config->model.provider    = "cuda";
}

void tts_gen_config_init(SherpaOnnxGenerationConfig *gen_cfg)
{
    memset(gen_cfg, 0, sizeof(SherpaOnnxGenerationConfig));
    gen_cfg->speed = 1.0;
}

void tts_config_zh(SherpaOnnxOfflineTtsConfig *config, SherpaOnnxGenerationConfig *gen_cfg)
{
    tts_config_init(config);
    config->model.matcha.acoustic_model = "./models/matcha-icefall-zh-baker/model-steps-3.onnx";
    config->model.matcha.dict_dir       = "./models/matcha-icefall-zh-baker/dict";
    config->model.matcha.tokens         = "./models/matcha-icefall-zh-baker/tokens.txt";
    config->model.matcha.lexicon        = "./models/matcha-icefall-zh-baker/lexicon.txt";
    config->model.matcha.vocoder        = "./models/vocoder/vocos-22khz-univ.onnx";
    config->rule_fsts                   = "./models/matcha-icefall-zh-baker/date.fst,"
                                          "./models/matcha-icefall-zh-baker/number.fst,"
                                          "./models/matcha-icefall-zh-baker/phone.fst";
    tts_gen_config_init(gen_cfg);
    gen_cfg->sid = 0;
}

void tts_config_en(SherpaOnnxOfflineTtsConfig *config, SherpaOnnxGenerationConfig *gen_cfg)
{
    tts_config_init(config);
    config->model.kokoro.model    = "./models/kokoro-en-v0_19/model.onnx";
    config->model.kokoro.voices   = "./models/kokoro-en-v0_19/voices.bin";
    config->model.kokoro.data_dir = "./models/kokoro-en-v0_19/espeak-ng-data";
    config->model.kokoro.tokens   = "./models/kokoro-en-v0_19/tokens.txt";

    tts_gen_config_init(gen_cfg);
    gen_cfg->sid = 1;
}

static int32_t audio_callback(const float *samples, int32_t num_samples, float progress, void *arg)
{
    MultiPlayer *mp = arg;
    ring_push(&mp->output.ring, samples, (size_t)num_samples);
    if (mp->monitor_enabled)
        ring_push(&mp->monitor.ring, samples, (size_t)num_samples);
    (void)progress;
    return 1;
}

static Lang classify(uint32_t cp)
{
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
        return LANG_EN;
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return LANG_ZH;
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return LANG_ZH;

    return LANG_OTHER;
}

static const unsigned char utf8_len[16] = {1,1,1,1,1,1,1,1,0,0,0,0,2,2,3,4};
static inline uint32_t utf8_decode(StringView sv)
{
    const char *s = STR_DATA(sv);
    switch (sv.length)
    {
        case 1:
            return s[0];
        case 2:
            return ((uint32_t)(s[0] & 0x1F) << 6) |
                (uint32_t)(s[1] & 0x3F);
        case 3:
            return ((uint32_t)(s[0] & 0x0F) << 12) |
                ((uint32_t)(s[1] & 0x3F) << 6) |
                (uint32_t)(s[2] & 0x3F);
        case 4:
            return ((uint32_t)(s[0] & 0x07) << 18) |
                ((uint32_t)(s[1] & 0x3F) << 12) |
                ((uint32_t)(s[2] & 0x3F) << 6) |
                (uint32_t)(s[3] & 0x3F);
        default:
            return 0;
    }
}

void tts_generate(Lang lang, StringView text, void *arg)
{
    const SherpaOnnxOfflineTts *tts = tts_lookup[lang];
    SherpaOnnxGenerationConfig *gen_cfg = tts_gen_config_lookup[lang];

    char buffer[text.length+1];
    memcpy(buffer, text.data, text.length);
    buffer[text.length] = '\0';
    DEV_DEBUG("Generating (lang: %d): %s", lang, buffer);

    const SherpaOnnxGeneratedAudio *audio = 
        SherpaOnnxOfflineTtsGenerateWithConfig(tts, buffer, gen_cfg, audio_callback, arg);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
}

void tts_generate_by_segment(String *text, void *arg)
{
    char buffer[text->length+1];
    String *segment = string_new(.buffer=buffer, .capacity=sizeof(buffer));

    Lang segment_lang = LANG_OTHER;
    for (;;)
    {
        if (text->length == 0) goto Generate;

        const char *p = STR_DATA(text);
        size_t len = utf8_len[(unsigned char)*p >> 4];
        if (len == 0) goto Generate;

        StringView token = string_consume_left(text, len);
        if (token.length == 0) goto Generate;

        Lang lang = classify(utf8_decode(token));

        if (segment_lang == LANG_OTHER && lang != segment_lang)
            segment_lang = lang;

        if (len > 0 && (lang == segment_lang || lang == LANG_OTHER))
        {
            string_append(segment, token);
            continue;
        }

Generate:
        tts_generate(segment_lang, SV(segment), arg);
        string_reset(segment);
        string_append(segment, token);
        segment_lang = lang;

        if (text->length == 0) break;
    }
}

static void on_overlay_submit(const char *text, OverlayAction action, void *arg)
{
    (void)action;
    String *s = string_from(text);
    tts_generate_by_segment(s, arg);
    string_free(&s);
}

static void on_quit(void *arg)
{
    (void)arg;
    PostQuitMessage(0);
}

static void show_error(LPCWSTR msg)
{
    MessageBoxW(NULL, msg, L"Error", MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdLine, int nShowCmd)
{
    (void)hPrev; (void)cmdLine; (void)nShowCmd;

    if (!overlay_init(hInstance))
    {
        show_error(L"Failed to initialize overlay");
        return 1;
    }

    if (!tray_init(hInstance, L"speaking (win+enter to open)"))
    {
        show_error(L"Failed to initialize tray icon");
        overlay_shutdown();
        return 1;
    }

    SherpaOnnxOfflineTtsConfig config_en;
    SherpaOnnxGenerationConfig gen_cfg_en;
    tts_config_en(&config_en, &gen_cfg_en);

    SherpaOnnxOfflineTtsConfig config_zh;
    SherpaOnnxGenerationConfig gen_cfg_zh;
    tts_config_zh(&config_zh, &gen_cfg_zh);

    tts_gen_config_lookup[LANG_OTHER] = &gen_cfg_en;
    tts_gen_config_lookup[LANG_EN]    = &gen_cfg_en;
    tts_gen_config_lookup[LANG_ZH]    = &gen_cfg_zh;

    tts_lookup[LANG_EN]    = SherpaOnnxCreateOfflineTts(&config_en);
    tts_lookup[LANG_ZH]    = SherpaOnnxCreateOfflineTts(&config_zh);
    tts_lookup[LANG_OTHER] = tts_lookup[LANG_EN];

    for (size_t i = 0; i < LANG_COUNT; i++)
    {
        if (tts_lookup[i] == NULL)
        {
            show_error(L"Failed to create TTS");
            return 1;
        }
    }

    int32_t sample_rate = SherpaOnnxOfflineTtsSampleRate(tts_lookup[LANG_EN]);

    MultiPlayerConfig player_config = {
        .sample_rate = sample_rate,
        .ring_seconds = 10.0,
        .output_device = STR("CABLE Input"),
        .monitor_enabled = true,
        .monitor_device = STR("default"),
    };

    MultiPlayer player;

    if (!multi_player_init(&player, &player_config))
    {
        show_error(L"Failed to init WASAPI");
        for (size_t i = 1; i < LANG_COUNT; i++)
            SherpaOnnxDestroyOfflineTts(tts_lookup[i]);

        tray_shutdown();
        overlay_shutdown();
        return 1;
    }

    OverlayCallback oc = {0};
    oc.on_submit = on_overlay_submit;
    oc.arg = &player;
    overlay_set_callback(&oc);

    TrayCallback tc = {0};
    tc.on_quit = on_quit;
    tc.arg = NULL;
    tray_set_callback(&tc);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    multi_player_wait_drain(&player);
    multi_player_destroy(&player);

    for (size_t i = 1; i < LANG_COUNT; i++)
        SherpaOnnxDestroyOfflineTts(tts_lookup[i]);

    tray_shutdown();
    overlay_shutdown();

    return (int)msg.wParam;
}
