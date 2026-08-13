#ifndef TRAY_H
#define TRAY_H

#include <windows.h>
#include <stdbool.h>

typedef struct
{
    void (*on_quit)(void *arg);
    void *arg;
} TrayCallback;

bool tray_init(HINSTANCE h_instance, LPCWSTR tool_tip);
void tray_set_callback(const TrayCallback *c);
void tray_shutdown(void);

#endif
