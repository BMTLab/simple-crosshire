/*
 * Name: crosshair.c
 * Author: Nikita Neverov (BMTLab)
 * Version: 3.0.0
 * Date: 2026-10-17
 * License: MIT
 *
 * A small crosshair in the centre of the primary monitor,
 * above every window, fullscreen games included,
 * with mouse clicks passing through it.
 *
 * It is an X11 client, so on a Wayland session it lives in XWayland,
 * beside the games that Wine and Proton run there.
 * The window is override-redirect,
 * so no window manager places it or keeps it under a fullscreen window.
 * The SHAPE extension cuts the window down to the cross itself:
 * the X server paints those pixels from the window's background colour,
 * and the program never draws anything.
 * An empty input shape lets every click through.
 * Once shown, the program sleeps in poll() and wakes only
 * when another window covers it (it raises itself again),
 * when the monitor layout changes (it centres itself again),
 * or when a signal tells it to stop.
 * In the foreground it also stops when its parent process dies,
 * so a game launcher that starts it takes it along when it exits.
 *
 * It uses no library, not even the C library:
 * x11.h speaks the X11 protocol over the socket itself,
 * and kernel.h makes the system calls.
 * Showing the cross takes two round trips to the X server
 * and about 15 000 instructions of its own; waiting costs nothing.
 *
 * Usage: crosshair [-f] [-s SIZE] [-t THICKNESS] [-c RRGGBB]
 * Without -f it toggles:
 * it stops a running crosshair, or else starts one in the background.
 */

#include "kernel.h"
#include "text.h"
#include "x11.h"

/* ---- Options ---- */

/* The defaults: the look of the first, Qt version (6 px, 2 px pen) at 1.5 times its size. */
#define DEFAULT_SIZE 9
#define DEFAULT_THICKNESS 3
#define DEFAULT_COLOR ff0000
#define MAX_SIZE 512

/* A macro's value as text, and a colour macro as a number. */
#define TEXT(value) TEXT_(value)
#define TEXT_(value) #value
#define HEX(value) HEX_(value)
#define HEX_(value) 0x##value

enum { EXIT_USAGE = 2, CONTINUE = -1 };

typedef struct {
    uint16_t size;      /* side of the square the cross fills, in pixels */
    uint16_t thickness; /* width of both bars, in pixels */
    uint32_t rgb;       /* colour as 0xRRGGBB */
    bool foreground;    /* stay attached, without the toggle */
} options;

typedef struct {
    const char *name;
    char letter;
    bool takes_value;
} option_spec;

static const option_spec option_specs[] = {
    {"foreground", 'f', false},
    {"size", 's', true},
    {"thickness", 't', true},
    {"color", 'c', true},
    {"help", 'h', false},
};

enum { OPTION_COUNT = sizeof option_specs / sizeof option_specs[0] };

static void print_usage(int fd)
{
    static const char usage[] =
        "Usage: " PROGRAM " [-f] [-s SIZE] [-t THICKNESS] [-c RRGGBB]\n"
        "Draw a crosshair in the centre of the primary monitor, above every window.\n"
        "Without -f, it stops a running crosshair or starts one in the background.\n"
        "\n"
        "  -f, --foreground       stay attached; Ctrl+C or the parent's exit stops it\n"
        "  -s, --size SIZE        side of the cross in pixels"
        " (default " TEXT(DEFAULT_SIZE) ")\n"
        "  -t, --thickness N      width of the bars in pixels"
        " (default " TEXT(DEFAULT_THICKNESS) ")\n"
        "  -c, --color RRGGBB     colour (default " TEXT(DEFAULT_COLOR) ")\n"
        "  -h, --help             show this help\n";

    sys_write(fd, usage, sizeof usage - 1);
}

/* Complain about the command line, show the usage, and return the exit status for it. */
static int usage_error(const char *problem, const char *argument)
{
    complain(problem, argument);
    print_usage(STDERR_FD);
    return EXIT_USAGE;
}

static const option_spec *find_short_option(char letter)
{
    for (size_t index = 0; index < OPTION_COUNT; index++)
        if (option_specs[index].letter == letter)
            return &option_specs[index];
    return nullptr;
}

static const option_spec *find_long_option(const char *name, size_t length)
{
    for (size_t index = 0; index < OPTION_COUNT; index++) {
        const char *candidate = option_specs[index].name;
        if (text_length(candidate) == length && same_bytes(candidate, name, length))
            return &option_specs[index];
    }
    return nullptr;
}

/* Take option LETTER with its VALUE into OPTS; return CONTINUE or the exit status to stop with. */
static int apply_option(options *opts, char letter, const char *value)
{
    uint64_t number = 0;

    switch (letter) {
    case 'f':
        opts->foreground = true;
        return CONTINUE;

    case 's':
        if (!parse_decimal(value, 1, MAX_SIZE, &number)) {
            complain("size must be 1 to " TEXT(MAX_SIZE) ": ", value);
            return EXIT_USAGE;
        }
        opts->size = (uint16_t)number;
        return CONTINUE;

    case 't':
        if (!parse_decimal(value, 1, MAX_SIZE, &number)) {
            complain("thickness must be 1 to " TEXT(MAX_SIZE) ": ", value);
            return EXIT_USAGE;
        }
        opts->thickness = (uint16_t)number;
        return CONTINUE;

    case 'c':
        if (!parse_rgb(value, &opts->rgb)) {
            complain("colour must be RRGGBB: ", value);
            return EXIT_USAGE;
        }
        return CONTINUE;

    default: /* 'h' */
        print_usage(STDOUT_FD);
        return EXIT_SUCCESS;
    }
}

/* "--name", "--name value" or "--name=value" at argv[*INDEX]. */
static int parse_long_option(int argc, char **argv, int *index, options *opts)
{
    const char *argument = argv[*index];
    const char *name = argument + 2;
    const char *equals = find_byte(name, '=');
    size_t name_length = equals != nullptr ? (size_t)(equals - name) : text_length(name);

    const option_spec *spec = find_long_option(name, name_length);
    if (spec == nullptr)
        return usage_error("unknown option ", argument);

    const char *value = nullptr;
    if (spec->takes_value) {
        if (equals != nullptr)
            value = equals + 1;
        else if (*index + 1 < argc)
            value = argv[++*index];
        else
            return usage_error("a value must follow ", argument);
    } else if (equals != nullptr) {
        return usage_error("no value goes with ", spec->name);
    }

    return apply_option(opts, spec->letter, value);
}

/* "-f", a group such as "-fs15", or "-s 15" at argv[*INDEX]. */
static int parse_short_options(int argc, char **argv, int *index, options *opts)
{
    for (const char *letter = argv[*index] + 1; *letter != '\0'; letter++) {
        char option[3] = {'-', *letter, '\0'};

        const option_spec *spec = find_short_option(*letter);
        if (spec == nullptr)
            return usage_error("unknown option ", option);

        if (!spec->takes_value) {
            int status = apply_option(opts, spec->letter, nullptr);
            if (status != CONTINUE)
                return status;
            continue;
        }

        /* The value is the rest of this argument, or else the next one; either ends the group. */
        const char *value = letter[1] != '\0' ? letter + 1 : nullptr;
        if (value == nullptr && *index + 1 < argc)
            value = argv[++*index];
        if (value == nullptr)
            return usage_error("a value must follow ", option);
        return apply_option(opts, spec->letter, value);
    }
    return CONTINUE;
}

/* Fill OPTS from the command line; return CONTINUE, or the exit status to stop with. */
static int parse_options(int argc, char **argv, options *opts)
{
    *opts = (options){
        .size = DEFAULT_SIZE,
        .thickness = DEFAULT_THICKNESS,
        .rgb = HEX(DEFAULT_COLOR),
    };

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];

        if (argument[0] != '-' || argument[1] == '\0')
            return usage_error("unexpected argument ", argument);

        /* "--" ends the options, and nothing else is expected after it. */
        if (argument[1] == '-' && argument[2] == '\0') {
            if (index + 1 < argc)
                return usage_error("unexpected argument ", argv[index + 1]);
            break;
        }

        int status = argument[1] == '-' ? parse_long_option(argc, argv, &index, opts)
                                        : parse_short_options(argc, argv, &index, opts);
        if (status != CONTINUE)
            return status;
    }

    if (opts->thickness > opts->size) {
        char numbers_data[64];
        text_buffer numbers = text_on(numbers_data, sizeof numbers_data);
        text_add_number(&numbers, opts->thickness);
        text_add(&numbers, " is larger than size ");
        text_add_number(&numbers, opts->size);

        complain("thickness ", numbers.data);
        return EXIT_USAGE;
    }
    return CONTINUE;
}

/* ---- The PID file ---- */

/* Where a running crosshair records its PID: the user's runtime directory, or else /tmp. */
static void pid_file_path(text_buffer *path, char **environment)
{
    const char *runtime = environment_value(environment, "XDG_RUNTIME_DIR");

    if (runtime != nullptr && *runtime != '\0') {
        text_add(path, runtime);
        text_add(path, "/" PROGRAM ".pid");
    } else {
        text_add(path, "/tmp/" PROGRAM "-");
        text_add_number(path, (uint64_t)sys_getuid());
        text_add(path, ".pid");
    }
}

/* Read at most CAPACITY bytes of the file at PATH; the count, or -errno. */
static long read_small_file(const char *path, char *buffer, size_t capacity)
{
    long fd = sys_open(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return fd;

    long length = sys_read((int)fd, buffer, capacity);
    sys_close((int)fd);
    return length;
}

/* The PID the file at PATH records, or 0. */
static long recorded_pid(const char *path)
{
    char content[24];
    long length = read_small_file(path, content, sizeof content - 1);
    if (length <= 0)
        return 0;
    content[length] = '\0';

    const char *cursor = content;
    uint64_t pid = 0;
    if (!take_decimal(&cursor, INT32_MAX, &pid))
        return 0;
    return (long)pid;
}

/* The PID the file at PATH records, if that process is still a crosshair; else 0. */
static long running_pid(const char *path)
{
    long pid = recorded_pid(path);
    if (pid == 0)
        return 0;

    /* Its name, from /proc/PID/comm: "crosshair" and a newline. */
    char comm_path_data[32];
    text_buffer comm_path = text_on(comm_path_data, sizeof comm_path_data);
    text_add(&comm_path, "/proc/");
    text_add_number(&comm_path, (uint64_t)pid);
    text_add(&comm_path, "/comm");

    char name[32];
    long length = read_small_file(comm_path.data, name, sizeof name);

    bool is_crosshair = length == sizeof PROGRAM && same_bytes(name, PROGRAM "\n", sizeof PROGRAM);
    return is_crosshair ? pid : 0;
}

static void write_pid_file(const char *path)
{
    char content_data[24];
    text_buffer content = text_on(content_data, sizeof content_data);
    text_add_number(&content, (uint64_t)sys_getpid());
    text_add(&content, "\n");

    long fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        return;

    sys_write((int)fd, content.data, content.length);
    sys_close((int)fd);
}

/* The toggle's other half: stop the crosshair running as PID. */
static int stop_crosshair(long pid)
{
    char pid_data[24];
    text_buffer pid_text = text_on(pid_data, sizeof pid_data);
    text_add_number(&pid_text, (uint64_t)pid);

    if (sys_kill((int)pid, SIGTERM) != 0) {
        complain("cannot stop pid ", pid_text.data);
        return EXIT_FAILURE;
    }

    tell("stopped (pid ", pid_text.data, ")");
    return EXIT_SUCCESS;
}

/* ---- The process ---- */

/*
 * Block the stop signals and take them from a descriptor instead,
 * which poll() watches beside the X connection.
 */
static long receive_stop_signals(void)
{
    uint64_t signals = signal_bit(SIGTERM) | signal_bit(SIGINT) | signal_bit(SIGHUP);

    sys_block_signals(&signals);
    return sys_signalfd(&signals, SFD_CLOEXEC);
}

/*
 * In the foreground the parent's death stops the crosshair too, SIGKILL included:
 * the kernel then sends SIGTERM, which arrives through the signal descriptor.
 * False when the parent has died already.
 */
static bool follow_parent(void)
{
    long parent = sys_getppid();

    if (sys_prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
        complain("cannot follow the parent process");
    return sys_getppid() == parent;
}

/* Leave the terminal: the parent reports the child and exits at once. */
static void detach(void)
{
    long child = sys_fork();
    if (child < 0) {
        complain("cannot fork");
        sys_exit(EXIT_FAILURE);
    }

    if (child > 0) {
        char pid_data[24];
        text_buffer pid_text = text_on(pid_data, sizeof pid_data);
        text_add_number(&pid_text, (uint64_t)child);

        tell("started (pid ", pid_text.data, ")");
        sys_exit(EXIT_SUCCESS);
    }

    sys_setsid();
    long null = sys_open("/dev/null", O_RDWR, 0);
    if (null >= 0) {
        sys_dup2((int)null, STDIN_FD);
        sys_dup2((int)null, STDOUT_FD);
        sys_dup2((int)null, STDERR_FD);
        if (null > STDERR_FD)
            sys_close((int)null);
    }
}

/* ---- The overlay window ---- */

typedef struct {
    x_connection x;

    uint32_t root;
    uint32_t window;
    uint16_t root_width; /* to centre on when there is no monitor list */
    uint16_t root_height;
    uint16_t size;
    int16_t left; /* where the window is now */
    int16_t top;

    uint8_t shape_opcode;
    uint8_t randr_opcode; /* 0 when the server has no RANDR */
    uint8_t randr_first_event;
    bool has_monitors;         /* RANDR 1.5: GetMonitors works */
    uint32_t monitors_request; /* the GetMonitors in flight, 0 for none */

    /* What the events since the last poll asked for. */
    bool want_raise;
    bool want_centre;
} overlay;

/* An 8-bit channel value, scaled into the bits of a TrueColor mask. */
static uint32_t channel_bits(uint32_t value, uint32_t mask)
{
    if (mask == 0)
        return 0;

    uint32_t shift = (uint32_t)__builtin_ctz(mask);
    uint32_t maximum = mask >> shift; /* 255 for the usual 8 bits */
    return (value * maximum / 255) << shift;
}

static uint32_t true_colour_pixel(const x_screen *screen, uint32_t rgb)
{
    return channel_bits(rgb >> 16 & 0xff, screen->red_mask)
           | channel_bits(rgb >> 8 & 0xff, screen->green_mask)
           | channel_bits(rgb & 0xff, screen->blue_mask);
}

static void report_x_error(const x_message *message)
{
    x_error error;
    __builtin_memcpy(&error, message, sizeof error);

    char details_data[64];
    text_buffer details = text_on(details_data, sizeof details_data);
    text_add_number(&details, error.code);
    text_add(&details, " for request ");
    text_add_number(&details, error.major_opcode);
    text_add(&details, ".");
    text_add_number(&details, error.minor_opcode);

    complain("the X server reported error ", details.data);
}

/*
 * Note what an event asks for; overlay_act() does it before the next poll.
 * Events that other clients send are ignored: only the server's own tell what happened.
 */
static void overlay_note_event(overlay *self, const x_message *event)
{
    if (event->type == X_VISIBILITY_NOTIFY) {
        x_visibility_notify visibility;
        __builtin_memcpy(&visibility, event, sizeof visibility);
        if (visibility.window == self->window && visibility.state != X_UNOBSCURED)
            self->want_raise = true;

    } else if (event->type == X_CONFIGURE_NOTIFY) {
        x_configure_notify configure;
        __builtin_memcpy(&configure, event, sizeof configure);
        if (configure.window == self->root) {
            self->root_width = configure.width;
            self->root_height = configure.height;
            self->want_centre = true;
        }

    } else if (self->randr_opcode != 0
               && event->type == self->randr_first_event + RANDR_SCREEN_CHANGE_NOTIFY) {
        self->want_centre = true;
    }
}

/*
 * Wait for the reply to request SEQUENCE, or the error it caused,
 * while the window is being set up.
 * Events that come first are noted for the main loop;
 * an error from an earlier request means the window could not be made.
 * The message stays in the input buffer: HEAD holds its first 32 bytes,
 * and the caller consumes SIZE bytes.
 */
static bool overlay_await(overlay *self, uint32_t sequence, x_message *head, uint32_t *size)
{
    for (;;) {
        if (!x_next_message(&self->x, head, size)) {
            complain("the X server closed the connection");
            return false;
        }

        bool answer = head->type == X_REPLY || head->type == X_ERROR;
        if (answer && head->sequence == (uint16_t)sequence)
            return true;
        if (head->type == X_ERROR) {
            report_x_error(head);
            return false;
        }

        overlay_note_event(self, head);
        input_consume(&self->x.input, *size);
    }
}

/* Wait for a QueryExtension reply; false when the connection broke. */
static bool overlay_await_extension(overlay *self, uint32_t sequence,
                                    x_query_extension_reply *reply)
{
    x_message head;
    uint32_t size = 0;
    if (!overlay_await(self, sequence, &head, &size))
        return false;

    __builtin_memcpy(reply, &head, sizeof *reply);
    if (head.type != X_REPLY)
        reply->present = false;
    input_consume(&self->x.input, size);
    return true;
}

/*
 * Move the cross so its centre is at CENTRE_X, CENTRE_Y,
 * and also raise it above its siblings when RAISE.
 * One ConfigureWindow does both; none goes out when nothing changes.
 */
static void overlay_place(overlay *self, int32_t centre_x, int32_t centre_y, bool raise)
{
    int32_t half = self->size / 2;
    int16_t left = (int16_t)(centre_x - half);
    int16_t top = (int16_t)(centre_y - half);

    x_configure_window_request request = {
        .header = {X_CONFIGURE_WINDOW, 0, 0},
        .window = self->window,
    };
    uint32_t count = 0;

    if (left != self->left || top != self->top) {
        request.value_mask |= X_CONFIG_X | X_CONFIG_Y;
        request.values[count++] = (uint32_t)(int32_t)left;
        request.values[count++] = (uint32_t)(int32_t)top;
        self->left = left;
        self->top = top;
    }
    if (raise) {
        request.value_mask |= X_CONFIG_STACK_MODE;
        request.values[count++] = X_STACK_ABOVE;
    }
    if (count == 0)
        return;

    uint32_t size = (uint32_t)offsetof(x_configure_window_request, values) + 4 * count;
    request.header.length = (uint16_t)(size / 4);
    x_send(&self->x, &request, size);
}

/*
 * The centre of the primary monitor in a GetMonitors REPLY of SIZE bytes,
 * or of the first monitor when none is primary; false for an empty list.
 */
static bool primary_monitor_centre(const uint8_t *reply, uint32_t size, int32_t *centre_x,
                                   int32_t *centre_y)
{
    x_randr_get_monitors_reply header;
    __builtin_memcpy(&header, reply, sizeof header);

    uint32_t offset = sizeof header;
    bool found = false;

    for (uint32_t index = 0; index < header.monitor_count; index++) {
        x_randr_monitor monitor;
        if (offset > size || size - offset < sizeof monitor)
            break;
        __builtin_memcpy(&monitor, reply + offset, sizeof monitor);

        if (monitor.primary || index == 0) {
            *centre_x = monitor.x + monitor.width / 2;
            *centre_y = monitor.y + monitor.height / 2;
            found = true;
            if (monitor.primary)
                break;
        }
        offset += sizeof monitor + 4U * monitor.output_count;
    }
    return found;
}

/*
 * Centre the cross on the primary monitor of MONITORS, a GetMonitors reply of SIZE bytes,
 * or on the root window without one; raise it too when RAISE.
 */
static void overlay_centre(overlay *self, const uint8_t *monitors, uint32_t size, bool raise)
{
    int32_t centre_x = self->root_width / 2;
    int32_t centre_y = self->root_height / 2;

    if (monitors != nullptr)
        primary_monitor_centre(monitors, size, &centre_x, &centre_y);
    overlay_place(self, centre_x, centre_y, raise);
}

static uint32_t overlay_query_monitors(overlay *self)
{
    x_randr_get_monitors_request request = {
        .header = X_HEADER(self->randr_opcode, RANDR_GET_MONITORS, x_randr_get_monitors_request),
        .window = self->root,
        .get_active = true,
    };
    return x_send(&self->x, &request, sizeof request);
}

/* The window: override-redirect, painted by the server in PIXEL, at the root's centre. */
static void overlay_create_window(overlay *self, uint32_t pixel)
{
    uint16_t size = self->size;
    self->left = (int16_t)(self->root_width / 2 - size / 2);
    self->top = (int16_t)(self->root_height / 2 - size / 2);

    x_create_window_request create = {
        .header = X_HEADER(X_CREATE_WINDOW, 0, x_create_window_request),
        .window = self->window,
        .parent = self->root,
        .x = self->left,
        .y = self->top,
        .width = size,
        .height = size,
        .window_class = X_INPUT_OUTPUT,
        .value_mask = X_CW_BACK_PIXEL | X_CW_BORDER_PIXEL | X_CW_OVERRIDE_REDIRECT
                      | X_CW_EVENT_MASK,
        .background_pixel = pixel,
        .override_redirect = true,
        .event_mask = X_VISIBILITY_CHANGE_MASK,
    };
    x_send(&self->x, &create, sizeof create);

    /* WM_NAME and WM_CLASS, so that xprop, xwininfo and window rules can tell it. */
    x_change_property_request name = {
        .header = {X_CHANGE_PROPERTY, X_PROPERTY_REPLACE, 0},
        .window = self->window,
        .property = X_ATOM_WM_NAME,
        .type = X_ATOM_STRING,
        .format = 8,
        .data_length = sizeof PROGRAM - 1,
        .data = PROGRAM,
    };
    uint32_t name_size = (uint32_t)offsetof(x_change_property_request, data)
                         + x_padded(sizeof PROGRAM - 1);
    name.header.length = (uint16_t)(name_size / 4);
    x_send(&self->x, &name, name_size);

    x_change_property_request class = name;
    class.property = X_ATOM_WM_CLASS;
    class.data_length = 2 * sizeof PROGRAM; /* the instance and the class, each with its NUL */
    __builtin_memcpy(class.data, PROGRAM "\0" PROGRAM, 2 * sizeof PROGRAM);
    class.header.length = sizeof class / 4;
    x_send(&self->x, &class, sizeof class);

    /* The root's ConfigureNotify: the screen changed size. */
    x_select_events_request root_events = {
        .header = X_HEADER(X_CHANGE_WINDOW_ATTRIBUTES, 0, x_select_events_request),
        .window = self->root,
        .value_mask = X_CW_EVENT_MASK,
        .event_mask = X_STRUCTURE_NOTIFY_MASK,
    };
    x_send(&self->x, &root_events, sizeof root_events);
}

/* Cut the window down to the cross, and let every click through it. */
static void overlay_shape(overlay *self, uint16_t thickness)
{
    uint16_t size = self->size;
    int16_t offset = (int16_t)((size - thickness) / 2);

    x_shape_rectangles_request bars = {
        .header = X_HEADER(self->shape_opcode, SHAPE_RECTANGLES, x_shape_rectangles_request),
        .operation = SHAPE_SET,
        .kind = SHAPE_BOUNDING,
        .ordering = SHAPE_UNSORTED,
        .window = self->window,
        .rectangles = {
            {offset, 0, thickness, size}, /* the vertical bar */
            {0, offset, size, thickness}, /* the horizontal bar */
        },
    };
    x_send(&self->x, &bars, sizeof bars);

    /* No rectangles at all: the input region is empty, so the pointer reaches the windows below. */
    x_shape_rectangles_request no_input = bars;
    no_input.kind = SHAPE_INPUT;
    no_input.header.length = offsetof(x_shape_rectangles_request, rectangles) / 4;
    x_send(&self->x, &no_input, offsetof(x_shape_rectangles_request, rectangles));
}

/*
 * Ask RANDR for screen changes and for the monitors, one write with the shapes.
 * Returns the GetMonitors request, whose reply places the window.
 */
static uint32_t overlay_watch_monitors(overlay *self, uint32_t *version_request)
{
    x_randr_query_version_request version = {
        .header = X_HEADER(self->randr_opcode, RANDR_QUERY_VERSION, x_randr_query_version_request),
        .major_version = 1,
        .minor_version = 5,
    };
    *version_request = x_send(&self->x, &version, sizeof version);

    x_randr_select_input_request changes = {
        .header = X_HEADER(self->randr_opcode, RANDR_SELECT_INPUT, x_randr_select_input_request),
        .window = self->root,
        .enable = RANDR_SCREEN_CHANGE_NOTIFY_MASK,
    };
    x_send(&self->x, &changes, sizeof changes);

    return overlay_query_monitors(self);
}

/*
 * Connect, make the window and show it; false with a message.
 *
 * The requests go out in three writes and wait for two answers:
 *   1. the connection setup and both extension queries;
 *   2. the window, its names, its shapes and the RANDR requests;
 *   3. the move to the primary monitor's centre and the map.
 * The server handles requests that follow the setup in the same stream,
 * so the extension queries need no round trip of their own,
 * and the GetMonitors reply also proves the window was made.
 */
static bool overlay_open(overlay *self, const options *opts, char **environment)
{
    x_connection *x = &self->x;
    self->size = opts->size;

    const char *display = environment_value(environment, "DISPLAY");
    uint32_t display_number = 0;
    uint32_t screen_number = 0;
    if (!x_parse_display(display, &display_number, &screen_number)) {
        complain("DISPLAY must name a local X display such as :0, not \"",
                 display != nullptr ? display : "", "\"");
        return false;
    }

    /* The cookie comes from a file, read before the connection uses the buffer. */
    uint8_t cookie[X_COOKIE_CAPACITY];
    size_t cookie_length = x_find_cookie(&x->input, environment, display_number, cookie);

    if (!x_connect(x, display_number)) {
        complain("cannot connect to the X display ", display);
        return false;
    }

    /* Write 1. */
    x_queue_setup(x, cookie, cookie_length);
    uint32_t shape_query = x_query_extension(x, "SHAPE");
    uint32_t randr_query = x_query_extension(x, "RANDR");
    if (!x_flush(x)) {
        complain("cannot write to the X server");
        return false;
    }

    x_screen screen;
    if (!x_read_setup(x, screen_number, &screen))
        return false;
    if (screen.visual_class != X_TRUE_COLOR) {
        complain("the X screen's visual is not TrueColor");
        return false;
    }

    self->root = screen.root;
    self->root_width = screen.width;
    self->root_height = screen.height;
    self->window = screen.first_id;

    /* Write 2: the window first, since it needs nothing from the extension replies. */
    overlay_create_window(self, true_colour_pixel(&screen, opts->rgb));

    x_query_extension_reply shape;
    x_query_extension_reply randr;
    if (!overlay_await_extension(self, shape_query, &shape)
        || !overlay_await_extension(self, randr_query, &randr))
        return false;
    if (!shape.present) {
        complain("the X server has no SHAPE extension");
        return false;
    }

    self->shape_opcode = shape.major_opcode;
    overlay_shape(self, opts->thickness);

    uint32_t version_request = 0;
    uint32_t monitors_request = 0;
    if (randr.present) {
        self->randr_opcode = randr.major_opcode;
        self->randr_first_event = randr.first_event;
        monitors_request = overlay_watch_monitors(self, &version_request);
    }
    if (!x_flush(x)) {
        complain("cannot write to the X server");
        return false;
    }

    /* Write 3, after the monitors: GetMonitors fails on a server older than RANDR 1.5. */
    if (randr.present) {
        x_message head;
        uint32_t size = 0;

        if (!overlay_await(self, version_request, &head, &size))
            return false;
        input_consume(&x->input, size);

        if (!overlay_await(self, monitors_request, &head, &size))
            return false;
        self->has_monitors = head.type == X_REPLY;
        overlay_centre(self, self->has_monitors ? input_peek(&x->input) : nullptr, size, true);
        input_consume(&x->input, size);
    } else {
        overlay_centre(self, nullptr, 0, true);
    }

    x_map_window_request map = {
        .header = X_HEADER(X_MAP_WINDOW, 0, x_map_window_request),
        .window = self->window,
    };
    x_send(x, &map, sizeof map);

    if (!x_flush(x)) {
        complain("cannot write to the X server");
        return false;
    }
    return true;
}

/* Handle a message from the server in the main loop. */
static void overlay_handle(overlay *self, const x_message *head, const uint8_t *message,
                           uint32_t size)
{
    bool answers_monitors = self->monitors_request != 0
                            && head->sequence == (uint16_t)self->monitors_request;

    if (head->type == X_REPLY) {
        if (answers_monitors) {
            self->monitors_request = 0;
            overlay_centre(self, message, size, false);
        }
        return;
    }

    if (head->type == X_ERROR) {
        report_x_error(head);
        if (answers_monitors) {
            self->monitors_request = 0;
            self->has_monitors = false;
            overlay_centre(self, nullptr, 0, false);
        }
        return;
    }

    overlay_note_event(self, head);
}

/* Read what the server sent and handle every message in it. */
static bool overlay_drain(overlay *self)
{
    input_buffer *input = &self->x.input;
    if (!input_read_some(input))
        return false;

    while (input->start != input->end) {
        x_message head;
        uint32_t size = 0;
        if (!x_next_message(&self->x, &head, &size))
            return false;

        overlay_handle(self, &head, input_peek(input), size);
        input_consume(input, size);
    }
    return true;
}

/* Send what the events since the last poll asked for. */
static void overlay_act(overlay *self)
{
    if (self->want_centre && self->monitors_request == 0) {
        self->want_centre = false;
        if (self->has_monitors)
            self->monitors_request = overlay_query_monitors(self);
        else
            overlay_centre(self, nullptr, 0, false);
    }

    if (self->want_raise) {
        self->want_raise = false;
        overlay_place(self, self->left + self->size / 2, self->top + self->size / 2, true);
    }
}

/* Serve the X connection until a stop signal arrives; return the exit status. */
static int overlay_run(overlay *self, int signal_fd)
{
    struct pollfd watched[2] = {
        {.fd = self->x.fd, .events = POLLIN},
        {.fd = signal_fd, .events = POLLIN},
    };

    for (;;) {
        overlay_act(self);
        if (!x_flush(&self->x))
            return EXIT_FAILURE;

        long ready = sys_poll(watched, 2, -1);
        if (ready == -EINTR)
            continue;
        if (ready < 0) {
            complain("poll failed");
            return EXIT_FAILURE;
        }

        if (watched[1].revents & POLLIN)
            return EXIT_SUCCESS;
        if (watched[0].revents & (POLLERR | POLLHUP))
            return EXIT_FAILURE;
        if ((watched[0].revents & POLLIN) && !overlay_drain(self))
            return EXIT_FAILURE;
    }
}

/* ---- main ---- */

int main(int argc, char **argv, char **environment)
{
    options opts;
    int status = parse_options(argc, argv, &opts);
    if (status != CONTINUE)
        return status;

    char pid_path_data[256];
    text_buffer pid_path = text_on(pid_path_data, sizeof pid_path_data);
    pid_file_path(&pid_path, environment);
    const char *pid_file = pid_path.truncated ? nullptr : pid_path.data;

    /* Without -f the command toggles: a running crosshair is stopped. */
    if (!opts.foreground && pid_file != nullptr) {
        long running = running_pid(pid_file);
        if (running > 0)
            return stop_crosshair(running);
    }

    long signal_fd = receive_stop_signals();
    if (signal_fd < 0) {
        complain("cannot receive signals");
        return EXIT_FAILURE;
    }
    if (opts.foreground && !follow_parent())
        return EXIT_SUCCESS;

    overlay self = {};
    if (!overlay_open(&self, &opts, environment))
        return EXIT_FAILURE;

    if (!opts.foreground)
        detach();
    if (pid_file != nullptr)
        write_pid_file(pid_file);

    status = overlay_run(&self, (int)signal_fd);

    /* The file is ours to remove while it still names this process. */
    if (pid_file != nullptr && recorded_pid(pid_file) == sys_getpid())
        sys_unlink(pid_file);
    return status;
}
