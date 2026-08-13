#ifndef LANG_H
#define LANG_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef enum
{
    LANG_UNKNOWN,
    LANG_EN,
    LANG_ZH,
    LANG_COUNT,
} Lang;

typedef enum
{
    CP_OTHER,
    CP_CJK,
    CP_WHITESPACE,
    CP_ALPHA,
    CP_NUMERIC,
} CpClass;

typedef struct
{
    int start;
    int end;
    Lang lang;
} Unit;

int scan_units(const wchar_t *text, int len, Unit *out, int max_out, 
        int *consumed_end, bool word_level, bool is_final);

#endif
