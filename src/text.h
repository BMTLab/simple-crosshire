/*
 * text.h: strings, numbers and messages, without a C library.
 *
 * Every string the program builds goes into a fixed array on the stack,
 * through a text_buffer that stops at the array's end and remembers it did.
 */

#pragma once

#include "kernel.h"

#include <stdarg.h>

#define PROGRAM "crosshair"

/* ---- Strings ---- */

/* The length of TEXT, without its terminating NUL. */
static inline size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0')
        length++;
    return length;
}

/*
 * Whether the LENGTH bytes at FIRST and SECOND are the same.
 * It stops at the first difference, so a shorter NUL-terminated string
 * compared with a longer one is never read past its end.
 */
static inline bool same_bytes(const void *first, const void *second, size_t length)
{
    const uint8_t *left = first;
    const uint8_t *right = second;

    for (size_t index = 0; index < length; index++)
        if (left[index] != right[index])
            return false;
    return true;
}

/* The first BYTE in TEXT, or nullptr. */
static inline const char *find_byte(const char *text, char byte)
{
    for (; *text != '\0'; text++)
        if (*text == byte)
            return text;
    return nullptr;
}

/* The value of the variable NAME in ENVIRONMENT, or nullptr when it is not set. */
static inline const char *environment_value(char **environment, const char *name)
{
    size_t length = text_length(name);

    for (char **entry = environment; *entry != nullptr; entry++)
        if (same_bytes(*entry, name, length) && (*entry)[length] == '=')
            return *entry + length + 1;
    return nullptr;
}

/* ---- Text in a fixed buffer ---- */

typedef struct {
    char *data;
    size_t capacity; /* the size of the array, its terminating NUL included */
    size_t length;
    bool truncated; /* something did not fit */
} text_buffer;

/* An empty text over the array DATA of CAPACITY bytes. */
static inline text_buffer text_on(char *data, size_t capacity)
{
    data[0] = '\0';
    return (text_buffer){.data = data, .capacity = capacity};
}

static inline void text_append(text_buffer *text, const char *part, size_t length)
{
    size_t room = text->capacity - 1 - text->length;
    if (length > room) {
        length = room;
        text->truncated = true;
    }

    copy_bytes(text->data + text->length, part, length);
    text->length += length;
    text->data[text->length] = '\0';
}

static inline void text_add(text_buffer *text, const char *part)
{
    text_append(text, part, text_length(part));
}

/*
 * Append NUMBER in decimal.
 * The digits come out last to first;
 * the compiler turns the division by 10 into a multiplication.
 */
static inline void text_add_number(text_buffer *text, uint64_t number)
{
    char digits[20];
    char *first = &digits[sizeof digits];

    do {
        *--first = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);

    text_append(text, first, (size_t)(&digits[sizeof digits] - first));
}

/* ---- Numbers in text ---- */

/*
 * Read the decimal digits at *CURSOR as a number of at most MAXIMUM,
 * and move *CURSOR past them.
 * False when there is no digit, or the number is larger.
 */
static inline bool take_decimal(const char **cursor, uint64_t maximum, uint64_t *value)
{
    const char *digit = *cursor;
    uint64_t number = 0;

    for (; *digit >= '0' && *digit <= '9'; digit++) {
        number = number * 10 + (uint64_t)(*digit - '0');
        if (number > maximum)
            return false;
    }
    if (digit == *cursor)
        return false;

    *cursor = digit;
    *value = number;
    return true;
}

/* TEXT as a whole decimal number from MINIMUM to MAXIMUM. */
static inline bool parse_decimal(const char *text, uint64_t minimum, uint64_t maximum,
                                 uint64_t *value)
{
    return take_decimal(&text, maximum, value) && *text == '\0' && *value >= minimum;
}

/* The value of the hexadecimal digit DIGIT, or -1. */
static inline int hex_digit(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';

    digit |= 0x20; /* 'A' to 'F' become 'a' to 'f' */
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    return -1;
}

/* A colour as six hexadecimal digits RRGGBB, with or without a leading '#'. */
static inline bool parse_rgb(const char *text, uint32_t *rgb)
{
    if (*text == '#')
        text++;

    uint32_t value = 0;
    for (int index = 0; index < 6; index++) {
        int digit = hex_digit(text[index]);
        if (digit < 0)
            return false;
        value = value << 4 | (uint32_t)digit;
    }
    if (text[6] != '\0')
        return false;

    *rgb = value;
    return true;
}

/* ---- Messages ---- */

/*
 * Write PROGRAM ": ", then the strings from FIRST up to a null, then a newline,
 * to FD in one write.
 */
static void say(int fd, const char *first, ...)
{
    char line_data[512];
    text_buffer line = text_on(line_data, sizeof line_data - 1); /* room kept for the newline */
    text_add(&line, PROGRAM ": ");

    va_list parts;
    va_start(parts, first);
    for (const char *part = first; part != nullptr; part = va_arg(parts, const char *))
        text_add(&line, part);
    va_end(parts);

    line.data[line.length++] = '\n';
    sys_write(fd, line.data, line.length);
}

/* A message for the user on standard output, or a complaint on standard error. */
#define tell(...) say(STDOUT_FD, __VA_ARGS__, (const char *)nullptr)
#define complain(...) say(STDERR_FD, __VA_ARGS__, (const char *)nullptr)
