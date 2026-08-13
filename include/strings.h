#ifndef STRINGS_H
#define STRINGS_H

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vadefs.h>

#define STRING_INITIAL_CAPACITY 128
#define STRING_INLINE_CAPCITY 16



// Types of a mutable string.
typedef enum
{
    // Heap-allocated buffer; Can grow
    STR_OWNED,
    // Caller-owned buffer; Cannot grow
    STR_BORROWED,
    // Inline buffer for small strings.
    STR_INLINE,
} StringType;

// Readonly, borrowed view. Does not guarantee null-termination.
typedef struct
{
    const char *data;
    size_t length;
} StringView;

// Mutable string.
typedef struct
{
    // data[start+length] = '\0';
    size_t length;
    size_t start;
    StringType type;

    union {
        struct {
            char *data;
            // capacity >= length+1
            size_t capacity;
        };
        // SSO buffer
        char inline_buf[STRING_INLINE_CAPCITY];
    };
} String;


/******************************************************
 * Intialization & Constructors
 ******************************************************/

// Options for initializing a string.
typedef struct
{
    // Default: INLINE
    StringType type;
    // If set and type != OWNED: type = BORROWED
    char *buffer;
    // Type not set or OWNED: reserves capacity
    // BORROWED: size of the buffer
    size_t capacity;
} StringInitOpt;

// Intialize an empty String with options. 
bool string_init_opt(String *s, StringInitOpt opt);
#define string_init(s, ...) string_init_opt((s), \
        (StringInitOpt){.type=STR_INLINE, .buffer=NULL, .capacity=0, __VA_ARGS__})

// Allocate a new String with options.
String *string_new_opt(StringInitOpt opt);
#define string_new(...) string_new_opt( \
        (StringInitOpt){.type=STR_INLINE, .buffer=NULL, .capacity=0, __VA_ARGS__})

// Allocate a new string from a C-string.
String *string_from_cstr(const char *value);

// Allocate a new string from a StringView.
String *string_from_view(StringView sv);

// Allocate a new string from another String.
String *string_clone(String *s);

#define string_from(v) _Generic((v), \
    char *:       string_from_cstr,  \
    const char *: string_from_cstr,  \
    StringView:   string_from_view,  \
    String *:     string_clone       \
)(v)


/******************************************************
 * Destructors
 ******************************************************/

// Reset the content of a String.
void string_reset(String *s);

// Reset and free the content of a String.
void string_clear(String *s);

// Free the String, its content, and the pointer.
void string_free(String **s);


/******************************************************
 * Attributes & Conversion
 ******************************************************/

// Length of the StringView.
static inline int string_view_length(StringView s);

// Length of the String.
static inline int string_length(String *s);

#define STR_LEN(s) _Generic((s),    \
    StringView: string_view_length, \
    String *:   string_length       \
)(s)

// Data of the StringView.
static inline const char *string_view_data(StringView s);

// Data of the String.
const char *string_data(const String *s);

// Pointer to the storage of the String.
const char *string_storage(const String *s);

#define STR_DATA(s) _Generic((s), \
    StringView: string_view_data, \
    String *:   string_data       \
)(s)

// Convert a string literal to a string view.
#define STR(s) ((StringView){(s), sizeof((s)) - 1})

// Convert a String to a StringView.
#define SV(s) ((StringView){.length=(size_t)STR_LEN(s), .data=STR_DATA(s)})


/******************************************************
 * Fomatting
 ******************************************************/

// Format specifier for a String or StringView.
#define STR_FMT "%.*s"

// Format argument for a String or StringView.
#define STR_ARG(s) STR_LEN(s), STR_DATA(s)


/******************************************************
 * Modification
 ******************************************************/

// Append a character to the String.
bool string_append_char(String *s, char c);

// Append a StringView's content to the String.
bool string_append_view(String *s, StringView value);

// Append a String's content to the String.
bool string_append_string(String *s, const String *value);

// Append a C-string to the String.
bool string_append_cstr(String *s, const char *value);

#define string_append(s, v) _Generic((v), \
    char:         string_append_char,     \
    String *:     string_append_string,   \
    StringView:   string_append_view,     \
    char *:       string_append_cstr,     \
    const char *: string_append_cstr      \
)(s, v)

// Append with formatting.
bool string_appendf(String *s, const char *format, ...);


/******************************************************
 * Slicing
 ******************************************************/

// Options for slicing.
typedef struct
{
    // Starting index; Inclusive.
    size_t from;
    // Ending index; Exclusive.
    size_t to;
} StringSliceOpt;

// Return a readonly slice from the StringView with options.
// 0 <= from < to <= length.
StringView string_slice_opt(StringView s, StringSliceOpt opt);
#define string_slice(s, ...) string_slice_opt((s), \
        (StringSliceOpt){.from=0, .to=SIZE_MAX, __VA_ARGS__})


/******************************************************
 * Comparison & Equality
 ******************************************************/

// Options for comparison.
typedef struct
{
    bool icase;
} StringCmpOpt;

// Compare two StringViews with options.
int string_compare_opt(StringView a, StringView b, StringCmpOpt opt);
#define string_compare(a, b, ...) string_compare_opt((a), (b), \
        (StringCmpOpt){.icase=false, __VA_ARGS__})

// Return if two StringViews are equal.
#define string_equal(a, b, ...) string_compare(a, b, __VA_ARGS__) == 0


/******************************************************
 * Consuming Operations
 ******************************************************/

// Consumes n bytes from the left of the String.
StringView string_consume_left(String *s, size_t n);

// Consumes n bytes from the right of the String.
StringView string_consume_right(String *s, size_t n);

// Consumes the next token from the String separated by delimiter.
StringView string_split_next(String *s, char delim);

#endif


// #define STRINGS_IMPLEMENTATION
#ifdef STRINGS_IMPLEMENTATION

/**
 * Intialize an empty String with options.
 */
bool string_init_opt(String *s, StringInitOpt opt)
{
    if (s == NULL) return false;

    s->start = 0;
    s->length = 0;

    if (opt.type != STR_OWNED && opt.buffer != NULL)
        s->type = STR_BORROWED;
    else if (opt.type == STR_INLINE && opt.capacity > 0)
        s->type = STR_OWNED;
    else
        s->type = opt.type;

    switch (s->type)
    {
        case STR_OWNED:
            s->capacity = opt.capacity <= 0 
                ? STRING_INITIAL_CAPACITY : opt.capacity;
            s->data = malloc(s->capacity);
            if (s->data == NULL) return false;
            s->data[s->start] = '\0';
            return true;

        case STR_BORROWED:
            if (opt.buffer == NULL) return false;
            s->data = opt.buffer;
            s->data[s->start] = '\0';
            s->capacity = opt.capacity;
            return true;

        case STR_INLINE:
            s->inline_buf[s->start] = '\0';
            return true;

    }

    return false;
}

/**
 * Allocate a new String with options.
 */
String *string_new_opt(StringInitOpt opt)
{
    String *s = malloc(sizeof(String));
    if (s == NULL) return NULL;
    if (!string_init_opt(s, opt)) 
    {
        free(s);
        return NULL;
    }
    return s;
}

/**
 * Allocate a new string from a C-string.
 */
String *string_from_cstr(const char * value)
{
    return string_from_view((StringView){ value, strlen(value) });
}

/**
 * Allocate a new string from a StringView.
 */
String *string_from_view(StringView sv)
{
    String *s = malloc(sizeof(String));
    if (s == NULL) return NULL;

    s->start = 0;
    s->length = sv.length;

    if (sv.length < sizeof(s->inline_buf))
    {
        s->type = STR_INLINE;
        memcpy(s->inline_buf, sv.data, sizeof(s->inline_buf));
        return s;
    }

    s->type = STR_OWNED;
    s->capacity = s->length + 1;
    s->data = malloc(s->capacity);
    if (s->data == NULL) return NULL;
    memcpy(s->data+s->start, sv.data, sv.length+1);
    return s;
}

/**
 * Allocate a new string from another String.
 */
String *string_clone(String *s)
{
    String *clone = NULL;
    switch (s->type)
    {
        case STR_INLINE:
            clone = string_new();
            break;

        case STR_OWNED:
        case STR_BORROWED:
            clone = string_new(.capacity=s->length+1);
            break;
    }
    if (clone == NULL) return NULL;

    if (!string_append(clone, s))
    {
        string_free(&clone);
        return NULL;
    }
    return clone;
}

/**
 * Data of the String.
 */
const char *string_data(const String *s)
{
    if (s == NULL) return NULL;
    return string_storage(s) + s->start;
}

/**
 * Pointer to the storage of the String.
 */
const char *string_storage(const String *s)
{
    if (s == NULL) return NULL;

    switch (s->type)
    {
        case STR_OWNED:
        case STR_BORROWED:
            return s->data;
        case STR_INLINE:
            return s->inline_buf;
    }
}

/**
 * Reset the content of the String.
 */
void string_reset(String *s)
{
    if (s == NULL) return;

    s->length = 0;
    s->start = 0;
    switch (s->type)
    {
        case STR_OWNED:
        case STR_BORROWED:
            if (s->data != NULL)
                s->data[s->start] = '\0';
            return;

        case STR_INLINE:
            s->inline_buf[s->start] = '\0';
            return;
    }
}

/**
 * Reset and free the content of a String.
 */
void string_clear(String *s)
{
    if (s == NULL) return;

    switch (s->type)
    {
        case STR_OWNED:
            if (s->data != NULL) free(s->data);
        case STR_BORROWED:
            s->data = NULL;
            break;

        case STR_INLINE:
            break;
    }
}

/**
 * Free the String, its content, and the pointer.
 */
void string_free(String **s)
{
    if (s == NULL || *s == NULL) return;
    string_clear(*s);
    free(*s);
    *s = NULL;
}

/**
 * Length of the StringView.
 */
static inline int string_view_length(StringView s) { return s.length; }

/**
 * Length of the String.
 */
static inline int string_length(String *s) { return s->length; }

/**
 * Data of the StringView.
 */
static inline const char *string_view_data(StringView s) { return s.data; }

/**
 * Append a character to an owned string.
 */
static bool __append_char_owned(String *s, char c)
{

    if (s->length+1 > s->capacity-1)
    {
        size_t new_capacity = s->capacity * 2;

        char *new_data = realloc(s->data, new_capacity);
        if (new_data == NULL) return false;

        s->data = new_data;
        s->capacity = new_capacity;
    }

    s->data[s->start+s->length] = c;
    s->length++;
    s->data[s->start+s->length] = '\0';
    return true;

}

/**
 * Append a character to an borrowed string.
 */
static bool __append_char_borrowed(String *s, char c)
{
    if (s->length+1 > s->capacity-1)
        return false;

    s->data[s->start+s->length] = c;
    s->length++;
    s->data[s->start+s->length] = '\0';
    return true;
}


/**
 * Append a character to an inline string.
 */
static bool __append_char_inline(String *s, char c)
{
    if (s->length+1 > sizeof(s->inline_buf)-1)
    {
        char *buffer = malloc(sizeof(s->inline_buf));
        memcpy(buffer, s->inline_buf, sizeof(s->inline_buf));

        if (!string_init(s, .type=STR_OWNED)) return false;
        if (!string_append(s, buffer))       return false;
        if (!string_append(s, c))            return false;
        free(buffer);
        return true;
    }

    s->inline_buf[s->length++] = c;
    s->inline_buf[s->length] = '\0';
    return true;
}

/**
 * Append a character to the String.
 */
bool string_append_char(String *s, char c)
{
    if (s == NULL) return false;

    switch (s->type)
    {
        case STR_OWNED:   return __append_char_owned(s, c);
        case STR_BORROWED:  return __append_char_borrowed(s, c);
        case STR_INLINE: return __append_char_inline(s, c);
    }
}

/**
 * If the string can append content of size. Does not account for OOM.
 */
static bool __can_append(String *s, size_t size)
{
    if (s == NULL) return false;
    switch (s->type)
    {
        case STR_INLINE:
        case STR_OWNED:
            return true;
        case STR_BORROWED:
            return s->start+s->length+size <= s->capacity-1;
    }
}

/**
 * Append a StringView's content to the String.
 */
bool string_append_view(String *s, StringView value)
{
    if (!__can_append(s, value.length))
        return false;

    for (size_t i = 0; i < value.length; i++)
    {
        if (!string_append_char(s, value.data[i]))
            return false;
    }
    return true;
}

/**
 * Append a String's content to the String.
 */
bool string_append_string(String *s, const String *value)
{
    if (!__can_append(s, value->length))
        return false;

    if (s == NULL) return false;

    char *buffer = malloc(value->length+1);
    if (buffer == NULL) return false;

    memcpy(buffer, string_data(value), value->length+1);
    buffer[value->length] = '\0';

    bool result = string_append_cstr(s, buffer);
    free(buffer);
    return result;
}

/**
 * Append a C-string to the String.
 */
bool string_append_cstr(String *s, const char *value)
{
    if (!__can_append(s, strlen(value)))
        return false;

    if (s == NULL) return false;

    const char *ptr = value;
    while (*ptr)
    {
        if (!string_append_char(s, *ptr))
        {
            return false;
        }
        ptr++;
    }
    return true;
}

/**
 * Append with formatting.
 */
bool string_appendf(String *s, const char *format, ...)
{
    va_list args;
    va_list args_copy;

    va_start(args, format);
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (size < 0)
    {
        va_end(args);
        return false;
    }

    char *buffer = malloc((size_t)size + 1);
    if (!buffer)
    {
        va_end(args);
        return false;
    }

    vsnprintf(buffer, (size_t)size+1, format, args);
    va_end(args);

    bool result = string_append(s, buffer);
    free(buffer);
    return result;
}

/**
 * Return a readonly slice from the StringView with options.
 * 0 <= from < to <= length.
 */
StringView string_slice_opt(StringView s, StringSliceOpt opt)
{
    if (opt.to == SIZE_MAX) opt.to = s.length;
    assert(opt.to <= s.length);
    assert(opt.from < opt.to);

    s.data += opt.from;
    s.length = opt.to - opt.from;
    return s;
}

/**
 * Compare two StringViews with options.
 */
int string_compare_opt(StringView a, StringView b, StringCmpOpt opt)
{
    size_t n = a.length > b.length ? b.length : a.length;
    if (a.length > b.length)
        return (int)a.data[n];

    if (b.length > a.length)
        return 0 - (int)b.data[n];

    if (opt.icase)
    {
        return strnicmp(a.data, b.data, n);
    }
    return strncmp(a.data, b.data, n);
}

/**
 * Consumes n bytes from the left of the String.
 */
StringView string_consume_left(String *s, size_t n)
{
    StringView sv = {0};
    if (s == NULL) return sv;
    if (n == 0) return sv;

    assert(n <= s->length);

    sv.data = string_data(s);
    sv.length = n;

    s->length -= n;
    s->start += n;
    return sv;
}

/**
 * Consumes n bytes from the right of the String.
 */
StringView string_consume_right(String *s, size_t n)
{
    StringView sv = {0};
    if (s == NULL) return sv;
    if (n == 0) return sv;

    assert(n <= s->length);

    s->length -= n;

    sv.data = string_data(s) + n;
    sv.length = n;

    return sv;
}

/**
 * Consumes the next token from the String separated by delimiter.
 */
StringView string_split_next(String *s, char delim)
{
    StringView sv = {0};
    if (s == NULL) return sv;

    char *data = (char *)string_storage(s);
    if (data == NULL) return sv;
    sv.data = data + s->start;

    for (size_t d = 0; d < s->length; d++)
    {
        size_t idx = s->start + d;
        if (data[idx] == delim)
        {
            sv.length = idx - s->start;

            s->start = idx + 1;
            s->length -= d;
            return sv;
        }
    }

    sv.length = s->length;
    s->start = s->length - 1;
    s->length = 0;
    return sv;
}

#endif
