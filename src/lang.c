#include "lang.h"

static inline bool is_cjk(uint32_t cp)
{
    return (cp >= 0x4E00  && cp <= 0x9FFF)
        || (cp >= 0x3400  && cp <= 0x4DBF)
        || (cp >= 0x20000 && cp <= 0x2A6DF)
        || (cp >= 0x2A700 && cp <= 0x2EBEF)
        || (cp >= 0xF900  && cp <= 0xFAFF)
        || (cp >= 0x3100  && cp <= 0x312F);
}

static CpClass classify(uint32_t cp)
{
    if ((cp >= '0' && cp <= '9'))
        return CP_NUMERIC;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
        return CP_ALPHA;
    if (cp == L' ' || cp == L'\t' || cp == L'\r' || cp == L'\n')
        return CP_WHITESPACE;
    if (is_cjk(cp))
        return CP_CJK;
    return CP_OTHER;
}

static uint32_t next_cp(const wchar_t *text, int len, int *i)
{
    wchar_t c = text[*i];
    if (c >= 0xD800 && c <= 0xDBFF && *i+1 < len
            && text[*i+1] >= 0xDC00 && text[*i+1] <= 0xDFFF)
    {
        uint32_t cp = 0x10000 + ((c - 0xD800) << 10) + (text[*i+1] - 0xDC00);
        *i += 2;
        return cp;
    }
    (*i)++;
    return (uint32_t)c;
}

int scan_units(const wchar_t *text, int len, Unit *out, int max_out, 
        int *consumed_end, bool word_level, bool is_final)
{
    int n = 0;
    const int zh_max_cp = 6;

    int run_start = -1;
    int run_end = -1;
    int run_cp_count = 0;
    Lang run_lang = LANG_EN;

    int i = 0;
    int last_boundary = 0;

#define FLUSH_RUN() do { \
    if (run_start >= 0) { \
        out[n++] = (Unit){run_start, run_end, run_lang}; \
        last_boundary = run_end; \
        run_start = -1; \
        run_cp_count = 0; \
    } \
} while (0)

    while (i < len && n < max_out)
    {
        int cp_start = i;
        uint32_t cp = next_cp(text, len, &i);
        CpClass cls = classify(cp);

        switch (cls)
        {
            case CP_WHITESPACE:
                if (word_level)
                {
                    FLUSH_RUN();
                    last_boundary = i;
                }
                break;

            case CP_OTHER:
                if (run_start >= 0)
                    run_end = i;
                else if (n > 0 && out[n-1].end == cp_start)
                    out[n-1].end = i;
                if (run_start < 0) last_boundary = i;
                break;

            case CP_CJK:
            case CP_NUMERIC:
            case CP_ALPHA:
                Lang target = (cls == CP_CJK) ? LANG_ZH
                        : (cls == CP_NUMERIC) ? LANG_UNKNOWN
                        : LANG_EN;

                bool zh_at_cap = (target == LANG_ZH) && (run_cp_count >= zh_max_cp);
                bool can_continue = (run_start >= 0) && (run_lang == target) ;
                if (!can_continue || (zh_at_cap && word_level))
                {
                    FLUSH_RUN();
                    run_start = cp_start;
                    run_lang = target;
                }

                run_end = i;
                run_cp_count++;
                break;
        }
    }
    if (is_final) FLUSH_RUN();

    *consumed_end = (run_start >= 0) ? run_start : (is_final ? len : last_boundary);
    return n;
}

