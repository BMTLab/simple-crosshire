/*
 * x11.h: a small X11 client that speaks the protocol over a Unix socket.
 *
 * Xlib and libxcb encode requests into a buffer, write it to the socket,
 * and decode the replies and events that come back.
 * This file does the same for the dozen messages crosshair needs,
 * with no allocation, no threads and no locks.
 * The connection setup tells the server to use this machine's byte order,
 * little-endian, so the structures below are the wire format as they are.
 *
 * References: "X Window System Protocol" (X.Org),
 * "X Nonrectangular Window Shape Extension Protocol" (SHAPE 1.1),
 * and "The X Resize, Rotate and Reflect Extension" (RANDR 1.5).
 */

#pragma once

#include "kernel.h"
#include "text.h"

/* ---- Protocol numbers ---- */

/* Core requests. */
enum : uint8_t {
    X_CREATE_WINDOW = 1,
    X_CHANGE_WINDOW_ATTRIBUTES = 2,
    X_MAP_WINDOW = 8,
    X_CONFIGURE_WINDOW = 12,
    X_CHANGE_PROPERTY = 18,
    X_QUERY_EXTENSION = 98,
};

/* The first byte of a message from the server. */
enum : uint8_t {
    X_ERROR = 0,
    X_REPLY = 1,
    X_VISIBILITY_NOTIFY = 15,
    X_CONFIGURE_NOTIFY = 22,
    X_GENERIC_EVENT = 35,
    X_SENT_EVENT = 0x80, /* set on events another client sent with SendEvent */
};

/* Window attributes; their values follow in this order. */
enum : uint32_t {
    X_CW_BACK_PIXEL = 1U << 1,
    X_CW_BORDER_PIXEL = 1U << 3,
    X_CW_OVERRIDE_REDIRECT = 1U << 9,
    X_CW_EVENT_MASK = 1U << 11,
};

enum : uint32_t {
    X_VISIBILITY_CHANGE_MASK = 1U << 16,
    X_STRUCTURE_NOTIFY_MASK = 1U << 17,
};

/* ConfigureWindow fields; their values follow in this order. */
enum : uint16_t {
    X_CONFIG_X = 1U << 0,
    X_CONFIG_Y = 1U << 1,
    X_CONFIG_STACK_MODE = 1U << 6,
};

enum {
    X_INPUT_OUTPUT = 1,     /* window class */
    X_STACK_ABOVE = 0,      /* stack mode */
    X_UNOBSCURED = 0,       /* visibility state */
    X_TRUE_COLOR = 4,       /* visual class */
    X_PROPERTY_REPLACE = 0, /* ChangeProperty mode */
    X_SETUP_FAILED = 0,     /* connection setup status */
    X_SETUP_SUCCESS = 1,
};

/* Atoms every server predefines. */
enum : uint32_t {
    X_ATOM_STRING = 31,
    X_ATOM_WM_NAME = 39,
    X_ATOM_WM_CLASS = 67,
};

/* SHAPE: the window's drawn part and its input part as lists of rectangles. */
enum : uint8_t {
    SHAPE_RECTANGLES = 1, /* minor opcode */
    SHAPE_SET = 0,        /* operation: replace the region */
    SHAPE_BOUNDING = 0,   /* the part that is drawn */
    SHAPE_INPUT = 2,      /* the part that takes pointer input, since SHAPE 1.1 */
    SHAPE_UNSORTED = 0,
};

/* RANDR: the monitors and the event for any change of them. */
enum : uint8_t {
    RANDR_QUERY_VERSION = 0,
    RANDR_SELECT_INPUT = 4,
    RANDR_GET_MONITORS = 42,        /* since RANDR 1.5 */
    RANDR_SCREEN_CHANGE_NOTIFY = 0, /* event number after the extension's first */
};

enum : uint16_t { RANDR_SCREEN_CHANGE_NOTIFY_MASK = 1U << 0 };

/* ---- Requests ---- */

/* The four bytes every request starts with. */
typedef struct {
    uint8_t opcode; /* core opcode, or the extension's major opcode */
    uint8_t detail; /* a small request field, or the extension's minor opcode */
    uint16_t length; /* the whole request in 4-byte units */
} x_request_header;

/* The header of a fixed-size request of type TYPE. */
#define X_HEADER(opcode, detail, type) {(opcode), (detail), sizeof(type) / 4}

typedef struct {
    x_request_header header;
    uint16_t name_length;
    uint16_t unused;
    char name[8]; /* padded to four bytes */
} x_query_extension_request;

typedef struct {
    x_request_header header; /* detail: the depth, 0 for the parent's */
    uint32_t window;
    uint32_t parent;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t border_width;
    uint16_t window_class;
    uint32_t visual; /* 0 for the parent's */
    uint32_t value_mask;
    uint32_t background_pixel;
    uint32_t border_pixel;
    uint32_t override_redirect;
    uint32_t event_mask;
} x_create_window_request;

/* ChangeWindowAttributes with the event mask alone. */
typedef struct {
    x_request_header header;
    uint32_t window;
    uint32_t value_mask;
    uint32_t event_mask;
} x_select_events_request;

typedef struct {
    x_request_header header; /* detail: the mode */
    uint32_t window;
    uint32_t property;
    uint32_t type;
    uint8_t format; /* 8, 16 or 32 bits per unit */
    uint8_t unused[3];
    uint32_t data_length; /* in units of the format */
    char data[20];        /* padded to four bytes */
} x_change_property_request;

typedef struct {
    x_request_header header;
    uint32_t window;
    uint16_t value_mask;
    uint16_t unused;
    uint32_t values[3];
} x_configure_window_request;

typedef struct {
    x_request_header header;
    uint32_t window;
} x_map_window_request;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} x_rectangle;

typedef struct {
    x_request_header header; /* detail: SHAPE_RECTANGLES */
    uint8_t operation;
    uint8_t kind;
    uint8_t ordering;
    uint8_t unused;
    uint32_t window;
    int16_t x_offset;
    int16_t y_offset;
    x_rectangle rectangles[2];
} x_shape_rectangles_request;

typedef struct {
    x_request_header header; /* detail: RANDR_QUERY_VERSION */
    uint32_t major_version;
    uint32_t minor_version;
} x_randr_query_version_request;

typedef struct {
    x_request_header header; /* detail: RANDR_SELECT_INPUT */
    uint32_t window;
    uint16_t enable;
    uint16_t unused;
} x_randr_select_input_request;

typedef struct {
    x_request_header header; /* detail: RANDR_GET_MONITORS */
    uint32_t window;
    uint8_t get_active;
    uint8_t unused[3];
} x_randr_get_monitors_request;

static_assert(sizeof(x_query_extension_request) == 16);
static_assert(sizeof(x_create_window_request) == 48);
static_assert(sizeof(x_select_events_request) == 16);
static_assert(sizeof(x_change_property_request) == 44);
static_assert(sizeof(x_configure_window_request) == 24);
static_assert(sizeof(x_map_window_request) == 8);
static_assert(sizeof(x_shape_rectangles_request) == 32);
static_assert(sizeof(x_randr_query_version_request) == 12);
static_assert(sizeof(x_randr_select_input_request) == 12);
static_assert(sizeof(x_randr_get_monitors_request) == 12);

/* ---- Messages from the server ---- */

/*
 * The 32 bytes every reply, error and event starts with.
 * Replies and generic events go on for LENGTH more 4-byte units.
 */
typedef struct {
    uint8_t type;
    uint8_t detail;
    uint16_t sequence; /* the low 16 bits of the last request number the server handled */
    uint32_t length;
    uint8_t body[24];
} x_message;

typedef struct {
    uint8_t type; /* X_ERROR */
    uint8_t code;
    uint16_t sequence; /* of the failed request */
    uint32_t bad_value;
    uint16_t minor_opcode;
    uint8_t major_opcode;
    uint8_t unused[21];
} x_error;

typedef struct {
    uint8_t type; /* X_REPLY */
    uint8_t unused;
    uint16_t sequence;
    uint32_t length;
    uint8_t present;
    uint8_t major_opcode;
    uint8_t first_event;
    uint8_t first_error;
    uint8_t unused2[20];
} x_query_extension_reply;

typedef struct {
    uint8_t type; /* X_REPLY */
    uint8_t unused;
    uint16_t sequence;
    uint32_t length;
    uint32_t timestamp;
    uint32_t monitor_count;
    uint32_t output_count;
    uint8_t unused2[12];
} x_randr_get_monitors_reply; /* the monitors follow */

typedef struct {
    uint32_t name;
    uint8_t primary;
    uint8_t automatic;
    uint16_t output_count;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t width_mm;
    uint32_t height_mm;
} x_randr_monitor; /* its outputs follow, four bytes each */

typedef struct {
    uint8_t type; /* X_VISIBILITY_NOTIFY */
    uint8_t unused;
    uint16_t sequence;
    uint32_t window;
    uint8_t state;
    uint8_t unused2[23];
} x_visibility_notify;

typedef struct {
    uint8_t type; /* X_CONFIGURE_NOTIFY */
    uint8_t unused;
    uint16_t sequence;
    uint32_t event;
    uint32_t window;
    uint32_t above_sibling;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t border_width;
    uint8_t override_redirect;
    uint8_t unused2[5];
} x_configure_notify;

static_assert(sizeof(x_message) == 32);
static_assert(sizeof(x_error) == 32);
static_assert(sizeof(x_query_extension_reply) == 32);
static_assert(sizeof(x_randr_get_monitors_reply) == 32);
static_assert(sizeof(x_randr_monitor) == 24);
static_assert(sizeof(x_visibility_notify) == 32);
static_assert(sizeof(x_configure_notify) == 32);

/* ---- The connection setup ---- */

typedef struct {
    uint8_t byte_order; /* 'l': little-endian */
    uint8_t unused;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t auth_name_length;
    uint16_t auth_data_length;
    uint16_t unused2;
} x_setup_request; /* the authorisation's name and data follow, each padded */

typedef struct {
    uint8_t status;
    uint8_t reason_length; /* when the server refused */
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t length; /* of the rest, in 4-byte units */
} x_setup_reply_header;

typedef struct {
    uint32_t release;
    uint32_t resource_id_base;
    uint32_t resource_id_mask;
    uint32_t motion_buffer_size;
    uint16_t vendor_length;
    uint16_t maximum_request_length;
    uint8_t screen_count;
    uint8_t format_count;
    uint8_t image_byte_order;
    uint8_t bitmap_bit_order;
    uint8_t bitmap_scanline_unit;
    uint8_t bitmap_scanline_pad;
    uint8_t min_keycode;
    uint8_t max_keycode;
    uint32_t unused;
} x_setup_success; /* the vendor, the pixmap formats and the screens follow */

typedef struct {
    uint32_t root;
    uint32_t default_colormap;
    uint32_t white_pixel;
    uint32_t black_pixel;
    uint32_t current_input_masks;
    uint16_t width;
    uint16_t height;
    uint16_t width_mm;
    uint16_t height_mm;
    uint16_t min_installed_maps;
    uint16_t max_installed_maps;
    uint32_t root_visual;
    uint8_t backing_stores;
    uint8_t save_unders;
    uint8_t root_depth;
    uint8_t depth_count;
} x_setup_screen; /* its depths follow */

typedef struct {
    uint8_t depth;
    uint8_t unused;
    uint16_t visual_count;
    uint32_t unused2;
} x_setup_depth; /* its visuals follow */

typedef struct {
    uint32_t id;
    uint8_t visual_class;
    uint8_t bits_per_rgb;
    uint16_t colormap_entries;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t unused;
} x_setup_visual;

static_assert(sizeof(x_setup_request) == 12);
static_assert(sizeof(x_setup_reply_header) == 8);
static_assert(sizeof(x_setup_success) == 32);
static_assert(sizeof(x_setup_screen) == 40);
static_assert(sizeof(x_setup_depth) == 8);
static_assert(sizeof(x_setup_visual) == 24);

/* What crosshair takes from the setup: one screen and its root visual. */
typedef struct {
    uint32_t root;
    uint16_t width;
    uint16_t height;
    uint8_t visual_class;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t first_id; /* the first resource id this client may use */
} x_screen;

/* ---- Buffered input ---- */

/*
 * Bytes read from a descriptor and not yet used: data[start] to data[end - 1].
 * Large enough for any reply crosshair asks for,
 * and the long connection setup streams through it.
 */
typedef struct {
    int fd;
    uint32_t start;
    uint32_t end;
    alignas(64) uint8_t data[4096];
} input_buffer;

static inline const uint8_t *input_peek(const input_buffer *input)
{
    return input->data + input->start;
}

static inline void input_consume(input_buffer *input, uint32_t length)
{
    input->start += length;
    if (input->start == input->end)
        input->start = input->end = 0;
}

/* Move the unused bytes to the front, to make room after them. */
static inline void input_compact(input_buffer *input)
{
    if (input->start == 0)
        return;
    copy_bytes(input->data, input->data + input->start, input->end - input->start);
    input->end -= input->start;
    input->start = 0;
}

/* Read whatever has arrived, waiting only if nothing has; false at the end of the stream. */
static inline bool input_read_some(input_buffer *input)
{
    if (input->end == sizeof input->data)
        input_compact(input);

    long count;
    do
        count = sys_read(input->fd, input->data + input->end, sizeof input->data - input->end);
    while (count == -EINTR);

    if (count <= 0)
        return false;
    input->end += (uint32_t)count;
    return true;
}

/* Wait until NEED bytes are buffered, at most the buffer's size. */
static inline bool input_fill(input_buffer *input, uint32_t need)
{
    if (need > sizeof input->data)
        return false;
    if (input->start + need > sizeof input->data)
        input_compact(input);

    while (input->end - input->start < need)
        if (!input_read_some(input))
            return false;
    return true;
}

/* Copy the next SIZE bytes out and consume them. */
static inline bool input_take(input_buffer *input, void *destination, uint32_t size)
{
    if (!input_fill(input, size))
        return false;

    __builtin_memcpy(destination, input_peek(input), size);
    input_consume(input, size);
    return true;
}

/* Consume LENGTH bytes, more than the buffer holds if need be. */
static inline bool input_skip(input_buffer *input, uint32_t length)
{
    while (length > 0) {
        uint32_t chunk = length < sizeof input->data ? length : sizeof input->data;
        if (!input_fill(input, chunk))
            return false;

        input_consume(input, chunk);
        length -= chunk;
    }
    return true;
}

/* ---- The connection ---- */

typedef struct {
    int fd;
    uint32_t sequence; /* number of the last request queued; the server counts the same way */
    uint32_t output_length;
    alignas(64) uint8_t output[512]; /* twice the largest batch of crosshair's requests */
    input_buffer input;
} x_connection;

/* LENGTH rounded up to the four bytes that every part of the protocol is padded to. */
static inline uint32_t x_padded(uint32_t length)
{
    return (length + 3) & ~3U;
}

/* Write out the queued requests. */
static inline bool x_flush(x_connection *connection)
{
    uint32_t sent = 0;

    while (sent < connection->output_length) {
        long count = sys_write(connection->fd, connection->output + sent,
                               connection->output_length - sent);
        if (count == -EINTR)
            continue;
        if (count <= 0)
            return false;
        sent += (uint32_t)count;
    }

    connection->output_length = 0;
    return true;
}

/* Queue LENGTH bytes, then zeros up to the next multiple of four. */
static inline void x_queue_bytes(x_connection *connection, const void *bytes, uint32_t length)
{
    uint32_t padded = x_padded(length);
    if (connection->output_length + padded > sizeof connection->output)
        x_flush(connection);

    uint8_t *destination = connection->output + connection->output_length;
    __builtin_memcpy(destination, bytes, length);
    __builtin_memset(destination + length, 0, padded - length);
    connection->output_length += padded;
}

/* Queue a request of SIZE bytes; return its sequence number. */
static inline uint32_t x_send(x_connection *connection, const void *request, uint32_t size)
{
    x_queue_bytes(connection, request, size);
    return ++connection->sequence;
}

/*
 * Wait until the next whole message from the server is buffered.
 * HEAD receives its first 32 bytes and SIZE its length;
 * the message stays at input_peek() until the caller consumes SIZE bytes.
 */
static inline bool x_next_message(x_connection *connection, x_message *head, uint32_t *size)
{
    input_buffer *input = &connection->input;
    if (!input_fill(input, sizeof *head))
        return false;
    __builtin_memcpy(head, input_peek(input), sizeof *head);

    uint8_t type = head->type & ~X_SENT_EVENT;
    uint32_t extra_units = type == X_REPLY || type == X_GENERIC_EVENT ? head->length : 0;
    if (extra_units > (sizeof input->data - sizeof *head) / 4)
        return false; /* longer than any reply crosshair asks for */

    *size = sizeof *head + 4 * extra_units;
    return input_fill(input, *size);
}

/* ---- Finding and opening the display ---- */

/*
 * The display and screen numbers in DISPLAY,
 * which must name a local server: ":0", ":0.1" or "unix:0".
 */
static inline bool x_parse_display(const char *display, uint32_t *number, uint32_t *screen)
{
    if (display == nullptr)
        return false;

    const char *colon = find_byte(display, ':');
    if (colon == nullptr)
        return false;

    size_t host_length = (size_t)(colon - display);
    if (host_length != 0 && !(host_length == 4 && same_bytes(display, "unix", 4)))
        return false;

    const char *cursor = colon + 1;
    uint64_t value = 0;
    if (!take_decimal(&cursor, 65535, &value))
        return false;
    *number = (uint32_t)value;

    *screen = 0;
    if (*cursor == '.') {
        cursor++;
        if (!take_decimal(&cursor, 255, &value))
            return false;
        *screen = (uint32_t)value;
    }
    return *cursor == '\0';
}

/* Connect a new stream socket to ADDRESS; the descriptor, or -errno. */
static inline long x_connect_socket(const struct sockaddr_un *address, unsigned length)
{
    long fd = sys_socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return fd;

    long status = sys_connect((int)fd, address, length);
    if (status == 0)
        return fd;

    sys_close((int)fd);
    return status;
}

/*
 * Connect to the local display NUMBER.
 * Its abstract socket comes first, as in libxcb on Linux, then the file in /tmp/.X11-unix.
 */
static inline bool x_connect(x_connection *connection, uint32_t number)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    const unsigned path_offset = offsetof(struct sockaddr_un, sun_path);

    /* The abstract name: a NUL, then the path, with no terminator. */
    text_buffer path = text_on(&address.sun_path[1], sizeof address.sun_path - 1);
    text_add(&path, "/tmp/.X11-unix/X");
    text_add_number(&path, number);
    address.sun_path[0] = '\0';
    long fd = x_connect_socket(&address, path_offset + 1 + (unsigned)path.length);

    /* The file: the same path, terminated. */
    if (fd < 0) {
        copy_bytes(address.sun_path, &address.sun_path[1], path.length + 1);
        fd = x_connect_socket(&address, path_offset + (unsigned)path.length + 1);
    }
    if (fd < 0)
        return false;

    connection->fd = (int)fd;
    connection->sequence = 0;
    connection->output_length = 0;
    connection->input.fd = (int)fd;
    connection->input.start = connection->input.end = 0;
    return true;
}

/* ---- The authorisation cookie ---- */

enum { X_COOKIE_CAPACITY = 64 };
enum : uint16_t { XAUTH_FAMILY_LOCAL = 256, XAUTH_FAMILY_WILD = 65535 };

#define X_COOKIE_NAME "MIT-MAGIC-COOKIE-1"

/* One counted field of an Xauthority entry, kept if it fits. */
typedef struct {
    uint16_t length;
    bool fits;
    uint8_t bytes[256];
} xauth_field;

/* Xauthority files store their numbers big-endian. */
static inline bool xauth_read_number(input_buffer *input, uint16_t *value)
{
    uint8_t bytes[2];
    if (!input_take(input, bytes, sizeof bytes))
        return false;

    *value = (uint16_t)(bytes[0] << 8 | bytes[1]);
    return true;
}

static inline bool xauth_read_field(input_buffer *input, xauth_field *field)
{
    if (!xauth_read_number(input, &field->length))
        return false;

    field->fits = field->length <= sizeof field->bytes;
    if (!field->fits)
        return input_skip(input, field->length);
    return input_take(input, field->bytes, field->length);
}

static inline bool xauth_field_is(const xauth_field *field, const void *bytes, size_t length)
{
    return field->fits && field->length == length && same_bytes(field->bytes, bytes, length);
}

/*
 * The MIT-MAGIC-COOKIE-1 for local display NUMBER from the Xauthority file,
 * chosen the way libXau chooses it for libxcb:
 * the first entry for this host (family Local with the host name)
 * or for any host (family Wild), whose display number is NUMBER or empty.
 * The file streams through INPUT, the connection's buffer, before the connection opens.
 * Returns the cookie's length, 0 when there is none.
 */
static inline size_t x_find_cookie(input_buffer *input, char **environment, uint32_t number,
                                   uint8_t cookie[static X_COOKIE_CAPACITY])
{
    char path_data[256];
    text_buffer path = text_on(path_data, sizeof path_data);

    const char *file = environment_value(environment, "XAUTHORITY");
    if (file == nullptr || *file == '\0') {
        const char *home = environment_value(environment, "HOME");
        if (home == nullptr)
            return 0;
        text_add(&path, home);
        text_add(&path, "/.Xauthority");
        if (path.truncated)
            return 0;
        file = path.data;
    }

    long fd = sys_open(file, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    input->fd = (int)fd;
    input->start = input->end = 0;

    struct new_utsname host;
    sys_uname(&host);
    size_t host_length = text_length(host.nodename);

    char number_data[8];
    text_buffer number_text = text_on(number_data, sizeof number_data);
    text_add_number(&number_text, number);

    size_t cookie_length = 0;
    xauth_field field;
    uint16_t family;

    while (cookie_length == 0 && xauth_read_number(input, &family)) {
        if (!xauth_read_field(input, &field))
            break;
        bool host_matches = family == XAUTH_FAMILY_WILD
                            || (family == XAUTH_FAMILY_LOCAL
                                && xauth_field_is(&field, host.nodename, host_length));

        if (!xauth_read_field(input, &field))
            break;
        bool number_matches = field.length == 0
                              || xauth_field_is(&field, number_text.data, number_text.length);

        if (!xauth_read_field(input, &field))
            break;
        bool is_cookie = xauth_field_is(&field, X_COOKIE_NAME, sizeof X_COOKIE_NAME - 1);

        if (!xauth_read_field(input, &field))
            break;
        if (host_matches && number_matches && is_cookie && field.fits
            && field.length <= X_COOKIE_CAPACITY) {
            copy_bytes(cookie, field.bytes, field.length);
            cookie_length = field.length;
        }
    }

    sys_close((int)fd);
    input->start = input->end = 0;
    return cookie_length;
}

/* ---- The connection setup ---- */

/* Queue the connection setup for protocol 11.0, with the cookie when there is one. */
static inline void x_queue_setup(x_connection *connection, const uint8_t *cookie,
                                 size_t cookie_length)
{
    bool with_cookie = cookie_length > 0;
    x_setup_request request = {
        .byte_order = 'l',
        .major_version = 11,
        .auth_name_length = with_cookie ? sizeof X_COOKIE_NAME - 1 : 0,
        .auth_data_length = (uint16_t)cookie_length,
    };

    x_queue_bytes(connection, &request, sizeof request);
    if (with_cookie) {
        x_queue_bytes(connection, X_COOKIE_NAME, sizeof X_COOKIE_NAME - 1);
        x_queue_bytes(connection, cookie, (uint32_t)cookie_length);
    }
}

/*
 * Reads a message of known length from the input.
 * A shortfall clears COMPLETE and zero-fills what was asked for,
 * so a parser checks only once, at its end.
 */
typedef struct {
    input_buffer *input;
    uint32_t remaining;
    bool complete;
} x_message_reader;

static inline void reader_take(x_message_reader *reader, void *destination, uint32_t size)
{
    bool available = reader->complete && size <= reader->remaining;
    if (available && input_take(reader->input, destination, size)) {
        reader->remaining -= size;
        return;
    }

    reader->complete = false;
    __builtin_memset(destination, 0, size);
}

static inline void reader_skip(x_message_reader *reader, uint32_t size)
{
    if (reader->complete && size <= reader->remaining && input_skip(reader->input, size)) {
        reader->remaining -= size;
        return;
    }
    reader->complete = false;
}

/*
 * Read the server's answer to the connection setup,
 * and take from it screen SCREEN_NUMBER, its root visual,
 * and the first resource id this client may use.
 * The answer lists every visual of every screen,
 * hundreds of them under XWayland, so it streams through the buffer.
 */
static inline bool x_read_setup(x_connection *connection, uint32_t screen_number, x_screen *screen)
{
    x_setup_reply_header header;
    if (!input_take(&connection->input, &header, sizeof header)) {
        complain("the X server closed the connection");
        return false;
    }
    x_message_reader reader = {&connection->input, 4U * header.length, true};

    /* A refusal carries the reason: its length is in the header when the server failed us. */
    if (header.status != X_SETUP_SUCCESS) {
        char reason[256];
        uint32_t length = header.status == X_SETUP_FAILED ? header.reason_length : reader.remaining;
        if (length > sizeof reason - 1)
            length = sizeof reason - 1;

        reader_take(&reader, reason, length);
        reason[length] = '\0';
        complain("the X server refused the connection: ", reason);
        return false;
    }

    x_setup_success success;
    reader_take(&reader, &success, sizeof success);
    reader_skip(&reader, x_padded(success.vendor_length));
    reader_skip(&reader, 8U * success.format_count);

    bool found = false;
    for (uint32_t screen_index = 0; screen_index < success.screen_count; screen_index++) {
        x_setup_screen root;
        reader_take(&reader, &root, sizeof root);

        bool wanted = screen_index == screen_number;
        if (wanted) {
            screen->root = root.root;
            screen->width = root.width;
            screen->height = root.height;
        }

        for (uint32_t depth_index = 0; depth_index < root.depth_count; depth_index++) {
            x_setup_depth depth;
            reader_take(&reader, &depth, sizeof depth);

            for (uint32_t visual_index = 0; visual_index < depth.visual_count; visual_index++) {
                x_setup_visual visual;
                reader_take(&reader, &visual, sizeof visual);

                if (wanted && visual.id == root.root_visual) {
                    screen->visual_class = visual.visual_class;
                    screen->red_mask = visual.red_mask;
                    screen->green_mask = visual.green_mask;
                    screen->blue_mask = visual.blue_mask;
                    found = true;
                }
            }
        }
    }
    reader_skip(&reader, reader.remaining); /* whatever a newer server appends */

    if (!reader.complete) {
        complain("the X server sent a broken connection setup");
        return false;
    }
    if (!found) {
        complain("the X display has no such screen");
        return false;
    }

    /* Ids are the base with bits of the mask; the lowest of those bits gives the first id. */
    uint32_t mask = success.resource_id_mask;
    screen->first_id = success.resource_id_base | (mask & -mask);
    return true;
}

/* ---- Requests every client needs ---- */

static inline uint32_t x_query_extension(x_connection *connection, const char *name)
{
    x_query_extension_request request = {
        .header = X_HEADER(X_QUERY_EXTENSION, 0, x_query_extension_request),
        .name_length = (uint16_t)text_length(name),
    };
    copy_bytes(request.name, name, request.name_length);
    return x_send(connection, &request, sizeof request);
}
