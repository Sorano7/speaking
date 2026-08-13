#ifndef OVERLAY_H
#define OVERLAY_H

#include "lang.h"
#include "strings.h"
#include <windows.h>
#include <stdbool.h>

typedef struct
{
    void (*on_submit)(StringView text, Lang lang, void *arg);
    void *arg;
} OverlayCallback;

bool overlay_init(HINSTANCE h_instance);
void overlay_set_callback(const OverlayCallback *c);
void overlay_shutdown(void);

#endif
