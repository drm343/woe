#include <quickjs.h>


#include "vt100.h"


#define countof(x) (sizeof(x) / sizeof((x)[0]))

#define CTRL_(k) ((k) & 0x1f)
#define WOE_VERSION "0.2.0"
#define WOE_TAB     2


enum editor_key {
    BACKSPACE  = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*
 *  Data
 */

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;


struct termios origin_termios;

static JSClassID js_vt100_class_id;

struct editor_config {
    int cx;    // current x
    int cy;    // current y
    int rx;    // fix for tabs
    int rows;  // Terminal max row
    int cols;  // Terminal max col
    int row_offset;
    int col_offset;
    int numrows;
    int mode;
    int number_command;
    erow *row_obj;
    int changed;
    char *filename;
    char status_msg[80];
    time_t status_msg_time;
};


static void js_vt100_finalizer(JSRuntime *rt, JSValue val)
{
    struct editor_config *s = JS_GetOpaque(val, js_vt100_class_id);
    js_free_rt(rt, s);
}


static JSClassDef js_vt100_class = {
    "VT100",
    .finalizer = js_vt100_finalizer,
};


/*
 *  Declare type
 */

static void utf8_fix_cx_position(struct editor_config *E);
static void move_cursur_right(struct editor_config *E);
void editor_row_delete(struct editor_config *E, int at);
void die(const char *s);
void editor_row_insert(struct editor_config *E, int at, const char *s, size_t len);
void c_echo_status_message(struct editor_config *E, const char *fmt, ...);
int editor_read_key (void);
static void editor_refresh_screen(struct editor_config *E, const char *str);


/*
 *  Append Buffer
 */


struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}


void abuf_append(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abuf_free(struct abuf *ab) {
    free(ab->b);
}


/*
 *  File i/o
 */
void file_open(struct editor_config *E, const char *filename)
{
    free(E->filename);
    E->filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        die("fopen");
    }

    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\n' ||
                                line[line_len - 1] == '\r')) {
            line_len--;
        }

        editor_row_insert(E, E->numrows, line, line_len);
        E->cy = E->numrows;
    }

    free(line);
    fclose(fp);
    E->cy = 0;
    E->changed = 0;
}


static JSValue js_file_open(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    JSValue v;

    if (!s) {
        return JS_EXCEPTION;
    }

    const char *str = JS_ToCString(ctx, argv[0]);
    file_open(s, str);
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}


void file_close(struct editor_config *E) {
    if (E->row_obj != NULL) {
        for (int at = E->numrows - 1; at >= 0; at--) {
            editor_row_delete(E, at);
        }
    }
    free(E->filename);
    E->filename = NULL;

    E->cx              = 0;
    E->cy              = 0;
    E->rx              = 0;
    E->numrows         = 0;
    E->number_command  = 0;
    E->row_offset      = 0;
    E->col_offset      = 0;
    E->row_obj         = NULL;
    E->changed         = 0;
    E->status_msg[0]   = '\0';
    E->status_msg_time = 0;
}


static JSValue js_file_close(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }

    file_close(s);
    return JS_UNDEFINED;
}


static char * editor_rows_to_string(struct editor_config *E,
        int *buffer_len)
{
    int total_len = 0;
    
    for (int j = 0; j < E->numrows; j++) {
        total_len += E->row_obj[j].size + 1;
    }
    *buffer_len = total_len;

    char *buf = malloc(total_len);
    char *p = buf;

    for (int j = 0; j < E->numrows; j++) {
        memcpy(p, E->row_obj[j].chars, E->row_obj[j].size);
        p += E->row_obj[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}


static char *editor_prompt(struct editor_config *E,
        const char *prompt)
{
    size_t buf_size = 128;
    char *buf = malloc(buf_size);

    size_t buf_len = 0;
    buf[0] = '\0';

    while (1) {
        c_echo_status_message(E, prompt, buf);
        editor_refresh_screen(E, "");

        int c = editor_read_key();
        if (c == DEL_KEY || c == CTRL_('h') || c == BACKSPACE) {
            if (buf_len != 0) {
                buf_len--;
                buf[buf_len] = '\0';
            }
        }
        else if (c == '\x1b') {
            c_echo_status_message(E, "");
            free(buf);
            return NULL;
        }
        else if (c == '\r') {
            if (buf_len != 0) {
                c_echo_status_message(E, "");
                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128) {
            if (buf_len == buf_size - 1) {
                buf_size *= 2;
                buf = realloc(buf, buf_size);
            }
            buf[buf_len++] = c;
            buf[buf_len] = '\0';
        }
    }
}


static JSValue js_editor_prompt(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    JSValue v;

    if (!s) {
        return JS_EXCEPTION;
    }

    const char *str = JS_ToCString(ctx, argv[0]);

    char *result = editor_prompt(s, str);

    if (result != NULL) {
        v = JS_NewString(ctx, result);
    }
    else {
        v = JS_UNDEFINED;
    }

    free(result);
    JS_FreeCString(ctx, str);

    return v;
}

void file_save(struct editor_config *E) {
    if (E->filename == NULL) {
        E->filename = editor_prompt(E, "Save as: %s");
        if (E->filename == NULL) {
            c_echo_status_message(E, "without filename!");
            return;
        }
    }

    int len;
    char *buf = editor_rows_to_string(E, &len);

    int fd = open(E->filename, O_RDWR | O_CREAT, 0644);

    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                c_echo_status_message(E, "save %s success", E->filename);
                E->changed = 0;
            }
        }
    }
    else {
        c_echo_status_message(E,
                "save %s failed: %s",
                E->filename,
                strerror(errno));
    }

    close(fd);
    free(buf);
}


static JSValue js_file_save(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }

    file_save(s);
    return JS_UNDEFINED;
}


/*
 *  Convert position
 */
int editor_convert_cx_to_rx (erow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        char c = row->chars[j];
        unsigned char cc = c;

        if (c == '\t') {
            rx += (WOE_TAB - 1) - (rx % WOE_TAB);
        }
        else if (cc > 128 && cc < 192) {
            continue;
        }
        else if (cc > 192) {
            rx += 1;
        }
        rx++;
    }
    return rx;
}


/*
 *  Terminal
 */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}


void disable_rawmode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &origin_termios) == -1) {
        die("tcsetattr");
    }
}


static JSValue js_disable_rawmode(JSContext *ctx, JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    disable_rawmode();
    return JS_UNDEFINED;
}


void enable_rawmode (void)
{
    if (tcgetattr(STDIN_FILENO, &origin_termios) == -1) {
        die("tcgetattr");
    }

    struct termios raw = origin_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}


static JSValue js_enable_rawmode(JSContext *ctx, JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    enable_rawmode();
    return JS_UNDEFINED;
}


void c_echo_status_message(struct editor_config *E,
        const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E->status_msg, sizeof(E->status_msg), fmt, ap);
    va_end(ap);
    E->status_msg_time = time(NULL);
}


static JSValue js_echo_status_message(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    const char *str = JS_ToCString(ctx, argv[0]);
    c_echo_status_message(s, str, NULL);
    JS_FreeCString(ctx, str);

    return JS_UNDEFINED;
}


static JSValue js_clean_screen(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    write(STDOUT_FILENO, "\x1b[2J", 4);
    return JS_UNDEFINED;
}


/*
 *  Api
 */


void move_to_line(struct editor_config *E,
        int at)
{
    at--;
    if (at <= 0) {
        at = 0;
    }
    else if (at >= E->numrows) {
        at = E->numrows;
    }

    E->cy = at;
}


static JSValue js_move_to_line(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    int v;

    if (!s) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt32(ctx, &v, argv[0])) {
        return JS_EXCEPTION;
    }

    move_to_line(s, v);
    return JS_UNDEFINED;
}


static void move_to_line_of_end(struct editor_config *E)
{
    if (E->row_obj && (&E->row_obj[E->cy] != NULL)) {
        E->cx = E->row_obj[E->cy].size - 1;
        utf8_fix_cx_position(E);
    }
}


static JSValue js_move_to_line_of_end(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    move_to_line_of_end(s);
    return JS_UNDEFINED;
}


static void move_cursur_left_or_previous_line(struct editor_config *E)
{
    E->cy--;
    move_to_line_of_end(E);
}


static JSValue js_move_cursur_left_or_previous_line(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    move_cursur_left_or_previous_line(s);
    return JS_UNDEFINED;
}


static void move_to_line_of_start(struct editor_config *E) {
    E->cx = 0;
}


static JSValue js_move_to_line_of_start(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    move_to_line_of_start(s);
    return JS_UNDEFINED;
}


static void fix_position(struct editor_config *E)
{
    erow *row = (E->cy >= E->numrows) ? NULL : &E->row_obj[E->cy];
    int row_len = row ? row->size : 0;
    if (row_len == 0) {
        move_to_line_of_start(E);
    }
    else if (E->cx >= row_len) {
        E->cx = row_len - 1;
    }
}


static JSValue js_fix_position(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    fix_position(s);
    return JS_UNDEFINED;
}


static void move_to_next_line_of_start (struct editor_config *E)
{
    if (E->cy < (E->numrows - 1)) {
        E->cy++;
        move_to_line_of_start(E);
    }
}


static void move_cursur_right_or_next_line(struct editor_config *E)
{
    erow *row = (E->cy >= E->numrows) ?
        NULL : &E->row_obj[E->cy];
    int last_char_size = 1;

    if (row) {
        unsigned char c = row->chars[row->size - 1];
        int index = row->size - 1;

        while (c > 128 && c < 192) {
            index--;
            c = row->chars[index];

            if (c > 192 && c < 224) {
                last_char_size = 2;
                break;
            }
            else if (c > 224 && c < 240) {
                last_char_size = 3;
                break;
            }
            else if (c > 240) {
                last_char_size = 4;
                break;
            }
        }
    }

    if (row && E->cx < (row->size - last_char_size)) {
        move_cursur_right(E);
    }
    else if (row && ((E->cx == (row->size - last_char_size))
                || row->size == 0)) {
        move_to_next_line_of_start(E);
    }
}

static JSValue js_move_cursur_right_or_next_line(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    move_cursur_right_or_next_line(s);
    return JS_UNDEFINED;
}


static void move_cursur_right(struct editor_config *s)
{
    erow *row = (s->cy >= s->numrows) ? NULL : &s->row_obj[s->cy];

    if (row) {
        char c = row->chars[s->cx];
        unsigned char cc = c;

        if (cc < 192) {
            s->cx++;
        }
        else if (cc < 224 && cc > 192) {
            s->cx += 2;
        }
        else if (cc < 240 && cc > 224) {
            s->cx += 3;
        }
        else if (cc > 240) {
            s->cx += 4;
        }
    }
}

static JSValue js_utf8_move_cx_right(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    move_cursur_right(s);
    return JS_UNDEFINED;
}


static void move_cursur_left(struct editor_config *s)
{
    erow *row = (s->cy >= s->numrows) ? NULL : &s->row_obj[s->cy];

    if (row) {
        s->cx--;
        unsigned char cc = row->chars[s->cx];

        while (cc >= 0b10000000 && cc <= 0b10111111) {
            s->cx--;
            cc = row->chars[s->cx];
        }
    }
}

static JSValue js_utf8_move_cx_left(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    move_cursur_left(s);
    return JS_UNDEFINED;
}


static void page_up (struct editor_config *E)
{
    int page = E->row_offset - E->rows;
    erow *row = (E->cy <= 0) ?
        NULL : &E->row_obj[E->cy];

    if (row == NULL) {
        NULL;
    }
    else if (page <= 0) {
        E->cy = 0;
    }
    else {
        E->cy -= E->rows;
    }
    fix_position(E);
}

static JSValue js_page_up(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    page_up(s);
    return JS_UNDEFINED;
}


static void page_down(struct editor_config *E)
{
    int page = E->row_offset + E->rows;
    erow *row = (E->cy >= E->numrows) ?
        NULL : &E->row_obj[E->cy];

    if (row == NULL) {
        NULL;
    }
    else if (page >= (E->numrows - E->rows)) {
        E->cy = E->numrows - 1;
    }
    else {
        E->cy += E->rows;
    }
    fix_position(E);
}


static JSValue js_page_down(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    page_down(s);
    return JS_UNDEFINED;
}


static void utf8_fix_cx_position(struct editor_config *s)
{
    unsigned char c = s->row_obj[s->cy].chars[s->cx];

    if (c < 128) {
        return;
    }
    move_cursur_left(s);
}


static JSValue js_utf8_fix_cx_position(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val,
            js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    utf8_fix_cx_position(s);
    return JS_UNDEFINED;
}


static JSValue js_move_cursur_home(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    write(STDOUT_FILENO, "\x1b[H", 3);
    return JS_UNDEFINED;
}


/*
 *  Input
 */
int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }
    return 0;
}


int get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1
          || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return get_cursor_position(rows, cols);
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


int editor_read_key (void) {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else {
                switch (seq[1]) {
                    /* Arrow Key */
                    case 'A': // up
                        return ARROW_UP;
                    case 'B': // down
                        return ARROW_DOWN;
                    case 'C': // right
                        return ARROW_RIGHT;
                    case 'D': // left
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        }
        else if (seq[0] == '0') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    }
    else {
        return c;
    }
}


static JSValue js_editor_read_key(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx, this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    return JS_NewInt32(ctx, editor_read_key());
}


void editor_row_free(erow *row) {
    free(row->render);
    free(row->chars);

    row->render = NULL;
    row->chars = NULL;
}


void editor_row_delete(struct editor_config *E, int at) {
    if (at < 0 || at >= E->numrows) {
        return;
    }

    editor_row_free(&E->row_obj[at]);

    for (int j = at; j < E->numrows; j++) {
        memmove(&(E->row_obj[j]),
                &(E->row_obj[j + 1]),
                sizeof(erow));
    }
    E->numrows--;
    E->changed++;

    erow *check = realloc(E->row_obj,
            sizeof(erow) * E->numrows);

    if (!check) {
        return;
    }
    else {
        E->row_obj = check;
    }
}


void editor_row_update(erow *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + tabs * (WOE_TAB - 1) + 1);

    int index = 0;
    for (int j = 0; j < row->size; j++) {
        char c = row->chars[j];

        if (c == '\t') {
            row->render[index++] = ' ';
            while (index % WOE_TAB != 0) {
                row->render[index++] = ' ';
            }
        }
        else {
            row->render[index++] = row->chars[j];
        }
    }
    row->render[index] = '\0';
    row->rsize = index;
}


void editor_row_insert(struct editor_config *E,
    int at, const char *s, size_t len)
{
    if (at < 0 || at > E->numrows) {
        return;
    }

    if (E->row_obj) {
        erow *check = realloc(E->row_obj,
                sizeof(erow) * (E->numrows + 1));

        if (!check) {
            return;
        }
        else {
            E->row_obj = check;
        }
    }
    else {
        E->row_obj = realloc(E->row_obj, sizeof(erow) * (E->numrows + 1));
    }

    if (E->cy != E->numrows) {
        at = E->cy;
        for (int i = E->numrows; i > E->cy; i--) {
            memmove(&E->row_obj[i], &E->row_obj[i - 1], sizeof(erow));
        }
    }

    E->row_obj[at].size = len;
    E->row_obj[at].chars = malloc(len + 1);
    memcpy(E->row_obj[at].chars, s, len);
    E->row_obj[at].chars[len] = '\0';

    E->row_obj[at].rsize = 0;
    E->row_obj[at].render = NULL;
    editor_row_update(&E->row_obj[at]);

    E->numrows++;
    E->changed++;
}

static JSValue js_editor_row_insert(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque(this_val, js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }

    int at, len;

    JS_ToInt32(ctx, &at, argv[0]);
    JS_ToInt32(ctx, &len, argv[2]);

    const char *str = JS_ToCString(ctx, argv[1]);
    editor_row_insert(s, at, str, len);
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}


void editor_row_insert_char(struct editor_config *E,
    erow *row, int at, int c)
{
    if (at < 0 || at > row->size) {
        at = row->size;
    }

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at],
            row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_row_update(row);

    E->changed++;
}


void editor_row_append_string(struct editor_config *E,
        erow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_row_update(row);
    E->changed++;
}


void editor_row_delete_char(struct editor_config *E,
        erow *row, int at)
{
    if (at < 0 || at >= row->size) {
        return;
    }

    char c = row->chars[at];
    unsigned char cc = c;
    int remove_len = 1;

    if (cc < 192) {
        remove_len = 1;
    }
    else if (cc < 224 && cc > 192) {
        remove_len = 2;
    }
    else if (cc < 240 && cc > 224) {
        remove_len = 3;
    }
    else if (cc > 240) {
        remove_len = 4;
    }

    memmove(&row->chars[at], &row->chars[at + remove_len],
            row->size - at - (remove_len - 1));
    row->size = (row->size - remove_len) > 0 ?
        (row->size - remove_len) : 0;

    editor_row_update(row);
    E->changed++;
}


void c_delete_char(struct editor_config *E)
{
    if (E->cy == E->numrows) {
        return;
    }
    if (E->cx == 0 && E->cy == 0) {
        return;
    }

    erow *row = &E->row_obj[E->cy];
    if (E->cx > 0) {
        move_cursur_left(E);
        editor_row_delete_char(E, row, E->cx);
    }
    else {
        E->cx = E->row_obj[E->cy - 1].size;
        editor_row_append_string(E,
                &E->row_obj[E->cy - 1],
                row->chars,
                row->size);
        editor_row_delete(E, E->cy);
        E->cy--;
        utf8_fix_cx_position(E);
    }
}


static JSValue js_delete_char(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque(this_val, js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }
    c_delete_char(s);
    return JS_UNDEFINED;
}


void c_insert_char(struct editor_config *s,
        int c)
{
    if (s->cy == s->numrows) {
        editor_row_insert(s, s->numrows, "", 0);
    }
    editor_row_insert_char(s, &s->row_obj[s->cy], s->cx, c);
    s->cx++;
}


static JSValue js_insert_char(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque(this_val, js_vt100_class_id);

    int v;

    if (!s) {
        return JS_EXCEPTION;
    }
    if (JS_ToInt32(ctx, &v, argv[0])) {
        return JS_EXCEPTION;
    }
    c_insert_char(s, v);
    return JS_UNDEFINED;
}


void c_insert_newline(struct editor_config *E)
{
    if (E->cx == 0) {
        E->cx = 0;
        editor_row_insert(E, E->cy, "", 0);
        E->cy++;
    }
    else {
        if (&E->row_obj[E->cy]) {
            erow *row = &E->row_obj[E->cy];

            if (E->cx >= row->size) {
                E->cy++;
                editor_row_insert(E, E->cy, "", 0);
                E->cx = 0;
            }
            else {
                int cx = E->cx;
                int size = row->size;

                char *car = malloc(sizeof(char) * cx + 1);
                memcpy(car, &row->chars[0], cx);
                car[cx + 1] = '\0';

                char *cdr = malloc(sizeof(char) * size - cx + 1);
                memcpy(cdr, &row->chars[cx], size - cx);
                cdr[size - cx + 1] = '\0';

                if (E->cy == 0) {
                    editor_row_insert(E,
                            E->cy,
                            car,
                            cx);
                    E->cy++;

                    editor_row_insert(E,
                            E->cy,
                            cdr,
                            size - cx);
                    editor_row_delete(E, E->cy + 1);
                }
                else {
                    editor_row_insert(E,
                            E->cy + 1,
                            cdr,
                            size - cx);

                    editor_row_insert(E,
                            E->cy,
                            car,
                            cx);
                    editor_row_delete(E, E->cy + 2);

                    E->cy++;
                }
                E->cx = 0;

                free(car);
                free(cdr);
            }
        }
    }
}


static JSValue js_insert_newline(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque(this_val, js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }
    c_insert_newline(s);
    return JS_UNDEFINED;
}


static JSValue js_mode_get(JSContext *ctx,
        JSValue val)
{
    struct editor_config *s = JS_GetOpaque(val, js_vt100_class_id);

    if (!s) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(ctx, s->mode);
}


static JSValue js_mode_set(JSContext *ctx, JSValueConst this_val,
        JSValue val)
{
    struct editor_config *s = JS_GetOpaque2(ctx,
            this_val, js_vt100_class_id);

    int v;
    if (!s) {
        return JS_EXCEPTION;
    }
    if (JS_ToInt32(ctx, &v, val)) {
        return JS_EXCEPTION;
    }
    s->mode = v;
    return JS_UNDEFINED;
}


static JSValue js_editor_config_attr_get(JSContext *ctx,
        JSValue val,
        int magic)
{
    struct editor_config *s = JS_GetOpaque(val, js_vt100_class_id);
    JSValue v;

    if (!s) {
        return JS_EXCEPTION;
    }

    switch (magic) {
        case 0:
            v = JS_NewInt32(ctx, s->cx);
            break;
        case 1:
            v = JS_NewInt32(ctx, s->cy);
            break;
        case 2:
            v = JS_NewInt32(ctx, s->rx);
            break;
        case 3:
            v = JS_NewInt32(ctx, s->rows);
            break;
        case 4:
            v = JS_NewInt32(ctx, s->cols);
            break;
        case 5:
            v = JS_NewInt32(ctx, s->row_offset);
            break;
        case 6:
            v = JS_NewInt32(ctx, s->col_offset);
            break;
        case 7:
            v = JS_NewInt32(ctx, s->changed);
            break;
        case 8:
            v = JS_NewInt32(ctx, s->numrows);
            break;
        case 9:
            v = JS_NewInt32(ctx, s->number_command);
            break;
        case 10:
            {
                if (s->filename) {
                    v = JS_NewString(ctx, s->filename);
                }
                else {
                    v = JS_NewString(ctx, "[No name]");
                }
            }
            break;
    }
    return v;
}


static JSValue js_editor_config_attr_set(JSContext *ctx,
        JSValueConst this_val,
        JSValue val,
        int magic)
{
    struct editor_config *s = JS_GetOpaque2(ctx,
            this_val, js_vt100_class_id);

    int v;
    if (!s) {
        return JS_EXCEPTION;
    }

    const char *str = NULL;
    if (magic == 10) {
        str = JS_ToCString(ctx, val);
    }
    else {
        if (JS_ToInt32(ctx, &v, val)) {
            return JS_EXCEPTION;
        }
    }

    switch (magic) {
        case 0:
            s->cx = v;
            break;
        case 1:
            s->cy = v;
            break;
        case 2:
            s->rx = v;
            break;
        case 3:
            s->rows = v;
            break;
        case 4:
            s->cols = v;
            break;
        case 5:
            s->row_offset = v;
            break;
        case 6:
            s->col_offset = v;
            break;
        case 7: // variable changed can not modified by user.
            break;
        case 8:
            s->numrows = v;
            break;
        case 9:
            s->number_command = v;
            break;
        case 10:
            free(s->filename);
            s->filename = strdup(str);
            JS_FreeCString(ctx, str);
            break;
    }
    return JS_UNDEFINED;
}


static JSValue js_erow_check_size(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx,
            this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    if (&s->row_obj[s->cy]) {
        if (s->row_obj[s->cy].size >= 1) {
            return JS_TRUE;
        }
    }
    return JS_FALSE;
}


static JSValue js_check_row_object(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx,
            this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }

    if (s->row_obj) {
        return JS_TRUE;
    }
    return JS_FALSE;
}


static JSValue js_erow_get_size(JSContext *ctx,
        JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque2(ctx,
            this_val, js_vt100_class_id);
    if (!s) {
        return JS_EXCEPTION;
    }
    JSValue v;
    int index;
    JS_ToInt32(ctx, &index, argv[0]);

    if (s->row_obj) {
        v = JS_NewInt32(ctx, s->row_obj[index].size);
    }
    return v;
}


/*
 *  Output
 */
JSValue editor_scroll (struct editor_config *s)
{
    if (!s) {
        return JS_EXCEPTION;
    }

    s->rx = 0;
    if (s->cy < s->numrows) {
        s->rx = editor_convert_cx_to_rx(&(s->row_obj[s->cy]), s->cx);
    }

    if (s->cy < s->row_offset) {
        s->row_offset = s->cy;
    }
    if (s->cy >= s->row_offset + s->rows) {
        s->row_offset = s->cy - s->rows + 1;
    }
    if (s->rx < s->col_offset) {
        s->col_offset = s->rx;
    }
    if (s->rx >= s->col_offset + s->cols) {
        s->col_offset = s->rx - s->cols + 1;
    }
    return JS_UNDEFINED;
}


void editor_draw_rows(struct editor_config *s,
        struct abuf *ab)
{
    int y;

    for (y = 0; y < s->rows; y++) {
        int file_row = y + s->row_offset;

        if (file_row >= s->numrows) {
            if (s->numrows == 0 && y == s->rows / 3) {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
                        "Woe -- version %s", WOE_VERSION);

                if (welcome_len > s->cols) {
                    welcome_len = s->cols;
                }

                int padding = (s->cols - welcome_len) / 2;
                if (padding) {
                    abuf_append(ab, "~", 1);
                    padding--;
                }
                while (padding--) {
                    abuf_append(ab, " ", 1);
                }

                abuf_append(ab, welcome, welcome_len);
            }
            else {
                abuf_append(ab, "~", 1);
            }
        }
        else {
            int len = s->row_obj[file_row].rsize - s->col_offset;

            if (len < 0) {
                len = 0;
            }
            if (len > s->cols) {
                len = s->cols;
            }
            abuf_append(ab,
                    &s->row_obj[file_row].render[s->col_offset],
                    len);
        }

        abuf_append(ab, "\x1b[K", 3);
        abuf_append(ab, "\r\n", 2);
    }
}


void editor_draw_status_bar(struct editor_config *s,
        struct abuf *ab, const char *str)
{
    abuf_append(ab, "\x1b[7m", 4); // turn reverse video on;
    abuf_append(ab, str, strlen(str));
    abuf_append(ab, "\x1b[m", 3);
    abuf_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct editor_config *s,
        struct abuf *ab)
{
    abuf_append(ab, "\x1b[K", 3);
    int msg_len = strlen(s->status_msg);

    if (msg_len > s->cols) {
        msg_len = s->cols;
    }

    if (msg_len && time(NULL) - s->status_msg_time < 5) {
        abuf_append(ab, s->status_msg, msg_len);
    }
}


static void editor_refresh_screen(struct editor_config *E,
        const char* str)
{
    struct abuf ab = ABUF_INIT;

    abuf_append(&ab, "\x1b[?25l", 6);
    abuf_append(&ab, "\x1b[H", 3);

    editor_draw_rows(E, &ab);
    editor_draw_status_bar(E, &ab, str);
    editor_draw_message_bar(E, &ab);

    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
            (E->cy - E->row_offset) + 1,
            (E->rx - E->col_offset) + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}


static JSValue js_editor_refresh_screen(
        JSContext *ctx, JSValueConst this_val,
        int argc, JSValueConst *argv)
{
    struct editor_config *s = JS_GetOpaque(this_val, js_vt100_class_id);

    JSValue v = editor_scroll(s);

    const char *str = JS_ToCString(ctx, argv[0]);
    editor_refresh_screen(s, str);
    JS_FreeCString(ctx, str);
    return v;
}


// js init module
static const JSCFunctionListEntry js_vt100_proto_funcs[] = {
    JS_CGETSET_DEF("mode", js_mode_get, js_mode_set),

    JS_CGETSET_MAGIC_DEF("cx",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 0),
    JS_CGETSET_MAGIC_DEF("cy",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 1),
    JS_CGETSET_MAGIC_DEF("rx",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 2),
    JS_CGETSET_MAGIC_DEF("rows",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 3),
    JS_CGETSET_MAGIC_DEF("cols",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 4),
    JS_CGETSET_MAGIC_DEF("row_offset",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 5),
    JS_CGETSET_MAGIC_DEF("col_offset",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 6),
    JS_CGETSET_MAGIC_DEF("changed",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 7),
    JS_CGETSET_MAGIC_DEF("numrows",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 8),
    JS_CGETSET_MAGIC_DEF("number_command",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 9),
    JS_CGETSET_MAGIC_DEF("filename",
            js_editor_config_attr_get,
            js_editor_config_attr_set, 10),

    JS_CFUNC_DEF("enable_rawmode", 0, js_enable_rawmode),
    JS_CFUNC_DEF("disable_rawmode", 0, js_disable_rawmode),
    JS_CFUNC_DEF("next_key", 0, js_editor_read_key),
    JS_CFUNC_DEF("refresh_woe_ui", 1, js_editor_refresh_screen),

    JS_CFUNC_DEF("file_open", 1, js_file_open),
    JS_CFUNC_DEF("file_close", 0, js_file_close),
    JS_CFUNC_DEF("file_save", 0, js_file_save),

    JS_CFUNC_DEF("clean_screen", 0, js_clean_screen),
    JS_CFUNC_DEF("move_cursur_home", 0, js_move_cursur_home),
    JS_CFUNC_DEF("move_cursur_left", 0, js_utf8_move_cx_left),
    JS_CFUNC_DEF("move_cursur_right", 0, js_utf8_move_cx_right),
    JS_CFUNC_DEF("fix_position", 0, js_fix_position),
    JS_CFUNC_DEF("move_cursur_left_or_previous_line", 0, js_move_cursur_left_or_previous_line),
    JS_CFUNC_DEF("move_cursur_right_or_next_line", 0, js_move_cursur_right_or_next_line),
    JS_CFUNC_DEF("move_to_line_of_start", 0, js_move_to_line_of_start),
    JS_CFUNC_DEF("move_to_line_of_end", 0, js_move_to_line_of_end),
    JS_CFUNC_DEF("move_to_line", 1, js_move_to_line),
    JS_CFUNC_DEF("page_up", 0, js_page_up),
    JS_CFUNC_DEF("page_down", 0, js_page_down),

    JS_CFUNC_DEF("delete_char", 0, js_delete_char),
    JS_CFUNC_DEF("insert_newline", 0, js_insert_newline),
    JS_CFUNC_DEF("insert_char", 1, js_insert_char),
    JS_CFUNC_DEF("row_insert", 3, js_editor_row_insert),

    JS_CFUNC_DEF("get_erow_size_at", 1, js_erow_get_size),
    JS_CFUNC_DEF("check_erow_size", 0, js_erow_check_size),
    JS_CFUNC_DEF("check_row_object", 0, js_check_row_object),

    JS_CFUNC_DEF("prompt", 1, js_editor_prompt),
    JS_CFUNC_DEF("echo_status_message", 0, js_echo_status_message),
};


static JSValue js_vt100_ctor(JSContext *ctx,
        JSValueConst new_target,
        int argc, JSValueConst *argv)
{
    struct editor_config *s;
    JSValue obj = JS_UNDEFINED;
    JSValue proto;
    int default_mode = 1;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt32(ctx, &default_mode, argv[0])) {
        return JS_EXCEPTION;
    }

    s->cx              = 0;
    s->cy              = 0;
    s->rx              = 0;
    s->numrows         = 0;
    s->number_command  = 0;
    s->row_offset      = 0;
    s->col_offset      = 0;
    s->row_obj         = NULL;
    s->changed         = 0;
    s->filename        = NULL;
    s->status_msg[0]   = '\0';
    s->status_msg_time = 0;
    s->mode            = default_mode;

    if (get_window_size(&(s->rows), &(s->cols)) == -1) {
        die("get_window_size");
    }
    s->rows -= 2;

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        goto fail;
    }
    obj = JS_NewObjectProtoClass(ctx, proto, js_vt100_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) {
        goto fail;
    }
    JS_SetOpaque(obj, s);
    return obj;
fail:
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}


static int js_vt100_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue vt100_proto, vt100_class;

    JS_NewClassID(&js_vt100_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_vt100_class_id, &js_vt100_class);

    vt100_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, vt100_proto, js_vt100_proto_funcs, countof(js_vt100_proto_funcs));

    vt100_class = JS_NewCFunction2(ctx, js_vt100_ctor, "VT100", 0, JS_CFUNC_constructor, 0);

    JS_SetConstructor(ctx, vt100_class, vt100_proto);
    JS_SetClassProto(ctx, js_vt100_class_id, vt100_proto);

    JS_SetModuleExport(ctx, m, "VT100", vt100_class);
    return 0;
}


JSModuleDef *js_init_module(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_vt100_init);
    if (!m) {
        return NULL;
    }
    JS_AddModuleExport(ctx, m, "VT100");
    return m;
}
