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
#include "lang.h"

#define STRINGS_IMPLEMENTATION
#include "strings.h"

#define CUT_IMPLEMENTATION
#include "cut.h"

static SherpaOnnxGenerationConfig *tts_gen_config_lookup[LANG_COUNT] = {0};
static const SherpaOnnxOfflineTts *tts_lookup[LANG_COUNT]            = {0};

static Lang last_lang = LANG_EN;
static bool fast_mode = false;

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

void tts_generate(Lang lang, StringView text, void *arg)
{
    if (lang == LANG_UNKNOWN)
        lang = last_lang;
    else
        last_lang = lang;

    const SherpaOnnxOfflineTts *tts = tts_lookup[lang];
    SherpaOnnxGenerationConfig *gen_cfg = tts_gen_config_lookup[lang];

    DEV_DEBUG("Generating (lang: %d): "STR_FMT, lang, STR_ARG(text));

    char buffer[text.length+1+3];
    memcpy(buffer, text.data, text.length);
    for (size_t i = sizeof(buffer); i > text.length; i--)
    {
        buffer[i] = '.';
    }
    buffer[text.length] = '\0';

    const SherpaOnnxGeneratedAudio *audio = 
        SherpaOnnxOfflineTtsGenerateWithConfig(tts, buffer, gen_cfg, audio_callback, arg);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
}

static inline void on_overlay_submit(StringView text, Lang lang, void *arg)
{
    tts_generate(lang, text, arg);
}

static inline void on_quit(void)
{
    PostQuitMessage(0);
}

static inline void show_error(LPCWSTR msg)
{
    MessageBoxW(NULL, msg, L"Error", MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdLine, int nShowCmd)
{
    (void)hPrev; (void)cmdLine; (void)nShowCmd;

#ifdef CUT_DEV
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_out != INVALID_HANDLE_VALUE)
    {
        DWORD dw_mode = 0;
        if (GetConsoleMode(h_out, &dw_mode))
        {
            dw_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h_out, dw_mode);
        }
    }
#endif

    if (!overlay_init(hInstance))
    {
        show_error(L"Failed to initialize overlay");
        return 1;
    }
    DEV_DEBUG("Overlay initialized");

    if (!tray_init(hInstance, L"speaking (win+enter to open)"))
    {
        show_error(L"Failed to initialize tray icon");
        overlay_shutdown();
        return 1;
    }

    DEV_DEBUG("Tray icon initialized");

    SherpaOnnxOfflineTtsConfig config_en;
    SherpaOnnxGenerationConfig gen_cfg_en;
    tts_config_en(&config_en, &gen_cfg_en);

    SherpaOnnxOfflineTtsConfig config_zh;
    SherpaOnnxGenerationConfig gen_cfg_zh;
    tts_config_zh(&config_zh, &gen_cfg_zh);

    tts_gen_config_lookup[LANG_EN]    = &gen_cfg_en;
    tts_gen_config_lookup[LANG_ZH]    = &gen_cfg_zh;

    tts_lookup[LANG_EN] = SherpaOnnxCreateOfflineTts(&config_en);
    if (tts_lookup[LANG_EN] == NULL)
    {
        show_error(L"Failed to create TTS");
        return 1;
    }
    DEV_DEBUG("TTS created for LANG_EN");

    tts_lookup[LANG_ZH] = SherpaOnnxCreateOfflineTts(&config_zh);
    if (tts_lookup[LANG_ZH] == NULL)
    {
        show_error(L"Failed to create TTS");
        return 1;
    }
    DEV_DEBUG("TTS created for LANG_ZH");

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
        show_error(L"Failed to initialize WASAPI");
        for (size_t i = 1; i < LANG_COUNT; i++)
            SherpaOnnxDestroyOfflineTts(tts_lookup[i]);

        tray_shutdown();
        overlay_shutdown();
        return 1;
    }
    DEV_DEBUG("WASAPI initialized");

    OverlayCallback oc = {0};
    oc.on_submit = on_overlay_submit;
    oc.arg = &player;
    overlay_set_callback(&oc);

    TrayCallback tc = {0};
    tc.on_quit = on_quit;
    tray_set_callback(&tc);

    DEV_DEBUG("Ready");

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
