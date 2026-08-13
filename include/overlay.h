#ifndef OVERLAY_H
#define OVERLAY_H

#include <windows.h>
#include <stdbool.h>

typedef enum
{
    OA_SEND,
    OA_SEND_CONTINUE,
    OA_SEND_HOLD,
} OverlayAction;

typedef struct
{
    void (*on_submit)(const char *text, OverlayAction action, void *arg);
    void *arg;
} OverlayCallback;

bool overlay_init(HINSTANCE h_instance);
void overlay_set_callback(const OverlayCallback *c);
void overlay_shutdown(void);

#endif
