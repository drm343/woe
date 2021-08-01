/*
 *  Include
 */


#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include "woe.h"


/*
 *  Defines
 */
#define WOE_VERSION "0.1.0"
#define WOE_TAB     2
#define HELP_MESSAGE "Help: <leader>q = quit; <leader>h = help; <leader>s = save"


#define CTRL_(k) ((k) & 0x1f)


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

enum editor_mode {
    NORMAL_MODE = 1,
    COMMAND_MODE,
    INSERT_MODE,
    NUMBER_COMMAND_MODE
};

enum is_special_key {
    IS_NOT_SPECIAL_KEY = 0,
    IS_SPECIAL_KEY = 1
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
    struct termios origin_termios;
};


struct editor_config E;


int vim_to_arrow(int key) {
    switch (key) {
        case 'h':
            return ARROW_LEFT;
        case 'l':
            return ARROW_RIGHT;
        case 'k':
            return ARROW_UP;
        case 'j':
            return ARROW_DOWN;

        case 'H':
            return PAGE_UP;
        case 'L':
            return PAGE_DOWN;

        case '^':
            return HOME_KEY;
        case '$':
            return END_KEY;
    }
    return key;
}

static void *fHandle;


/*
 *  Api
 */


void c_move_to_line_of_end (void) {
    if (E.row_obj && (&E.row_obj[E.cy] != NULL)) {
        E.cx = E.row_obj[E.cy].size - 1;
        utf8_fix_cx_position();
    }
}

void c_move_to_previous_line_of_end (void) {
    E.cy--;
    c_move_to_line_of_end();
}

void c_move_to_line_of_start (void) {
    E.cx = 0;
}

void c_move_to_next_line_of_start (void) {
    if (E.cy < (E.numrows - 1)) {
        E.cy++;
        c_move_to_line_of_start();
    }
}

void c_move_start_or_end (void) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row_obj[E.cy];
    int row_len = row ? row->size : 0;
    if (row_len == 0) {
        c_move_to_line_of_start();
    }
    else if (E.cx >= row_len) {
        E.cx = row_len - 1;
    }
}

void c_echo_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.status_msg_time = time(NULL);
}

void c_help(void) {
    c_echo_status_message(HELP_MESSAGE);
}

void c_insert_char(int c) {
    if (E.cy == E.numrows) {
        editor_row_insert(E.numrows, "", 0);
    }
    editor_row_insert_char(&E.row_obj[E.cy], E.cx, c);
    E.cx++;
}

void c_delete_char(void) {
    if (E.cy == E.numrows) {
        return;
    }
    if (E.cx == 0 && E.cy == 0) {
        return;
    }

    erow *row = &E.row_obj[E.cy];
    if (E.cx > 0) {
        utf8_move_cx_left();
        editor_row_delete_char(row, E.cx);
    }
    else {
        E.cx = E.row_obj[E.cy - 1].size;
        editor_row_append_string(&E.row_obj[E.cy - 1],
                row->chars,
                row->size);
        editor_row_delete(E.cy);
        E.cy--;
        utf8_fix_cx_position();
    }
}

void c_page_up (void) {
    int page = E.row_offset - E.rows;
    erow *row = (E.cy <= 0) ?
        NULL : &E.row_obj[E.cy];

    if (row == NULL) {
        NULL;
    }
    else if (page <= 0) {
        E.cy = 0;
    }
    else {
        E.cy -= E.rows;
    }
    c_move_start_or_end();
}

void c_page_down(void) {
    int page = E.row_offset + E.rows;
    erow *row = (E.cy >= E.numrows) ?
        NULL : &E.row_obj[E.cy];

    if (row == NULL) {
        NULL;
    }
    else if (page >= (E.numrows - E.rows)) {
        E.cy = E.numrows - 1;
    }
    else {
        E.cy += E.rows;
    }
    c_move_start_or_end();
}


void c_move_to_line(int at) {
    at--;
    if (at <= 0) {
        at = 0;
    }
    else if (at >= E.numrows) {
        at = E.numrows;
    }

    E.cy = at;
}


void c_move_to_column(int at) {
    at--;
    if (at <= 0) {
        at = 0;
    }
    else if (at >= E.row_obj[E.cy].size) {
        at = E.row_obj[E.cy].size;
    }

    E.cx = at;
}


void c_insert_newline(void) {
    if (E.cx == 0) {
        E.cx = 0;
        editor_row_insert(E.cy, "", 0);
        E.cy++;
    }
    else {
        if (&E.row_obj[E.cy]) {
            erow *row = &E.row_obj[E.cy];

            if (E.cx >= row->size) {
                E.cy++;
                editor_row_insert(E.cy, "", 0);
                E.cx = 0;
            }
            else {
                int cx = E.cx;
                int size = row->size;

                char *car = malloc(sizeof(char) * cx + 1);
                memcpy(car, &row->chars[0], cx);
                car[cx + 1] = '\0';

                char *cdr = malloc(sizeof(char) * size - cx + 1);
                memcpy(cdr, &row->chars[cx], size - cx);
                cdr[size - cx + 1] = '\0';

                if (E.cy == 0) {
                    editor_row_insert(E.cy,
                            car,
                            cx);
                    E.cy++;

                    editor_row_insert(E.cy,
                            cdr,
                            size - cx);
                    editor_row_delete(E.cy + 1);
                }
                else {
                    editor_row_insert(E.cy + 1,
                            cdr,
                            size - cx);

                    editor_row_insert(E.cy,
                            car,
                            cx);
                    editor_row_delete(E.cy + 2);

                    E.cy++;
                }
                E.cx = 0;

                free(car);
                free(cdr);
            }
        }
    }
}


static void (*c_search)(void);
static void (*c_search_next)(void);
static void (*c_search_previous)(void);
static void (*module_search_stop)(void);


/*
 *  Terminal
 */
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}


void disable_rawmode (void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.origin_termios) == -1) {
        die("tcsetattr");
    }
}


void enable_rawmode (void) {
    if (tcgetattr(STDIN_FILENO, &E.origin_termios) == -1) {
        die("tcgetattr");
    }
    atexit(disable_rawmode);

    struct termios raw = E.origin_termios;

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


/*
 *  File i/o
 */


char * editor_rows_to_string(int *buffer_len) {
    int total_len = 0;
    
    for (int j = 0; j < E.numrows; j++) {
        total_len += E.row_obj[j].size + 1;
    }
    *buffer_len = total_len;

    char *buf = malloc(total_len);
    char *p = buf;

    for (int j = 0; j < E.numrows; j++) {
        memcpy(p, E.row_obj[j].chars, E.row_obj[j].size);
        p += E.row_obj[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}


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

void editor_row_append_string(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_row_update(row);
    E.changed++;
}

void editor_row_insert(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) {
        return;
    }

    if (E.row_obj) {
        erow *check = realloc(E.row_obj,
                sizeof(erow) * (E.numrows + 1));

        if (!check) {
            return;
        }
        else {
            E.row_obj = check;
        }
    }
    else {
        E.row_obj = realloc(E.row_obj, sizeof(erow) * (E.numrows + 1));
    }

    if (E.cy != E.numrows) {
        at = E.cy;
        for (int i = E.numrows; i > E.cy; i--) {
            memmove(&E.row_obj[i], &E.row_obj[i - 1], sizeof(erow));
        }
    }

    E.row_obj[at].size = len;
    E.row_obj[at].chars = malloc(len + 1);
    memcpy(E.row_obj[at].chars, s, len);
    E.row_obj[at].chars[len] = '\0';

    E.row_obj[at].rsize = 0;
    E.row_obj[at].render = NULL;
    editor_row_update(&E.row_obj[at]);

    E.numrows++;
    E.changed++;
}


void editor_row_insert_char(erow *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at],
            row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_row_update(row);

    E.changed++;
}


void editor_row_delete_char(erow *row, int at) {
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
    E.changed++;
}


void editor_row_free(erow *row) {
    free(row->render);
    free(row->chars);

    row->render = NULL;
    row->chars = NULL;
}


void editor_row_delete(int at) {
    if (at < 0 || at >= E.numrows) {
        return;
    }

    editor_row_free(&E.row_obj[at]);

    for (int j = at; j < E.numrows; j++) {
        memmove(&E.row_obj[j],
                &E.row_obj[j + 1],
                sizeof(erow));
    }
    E.numrows--;
    E.changed++;

    erow *check = realloc(E.row_obj,
            sizeof(erow) * E.numrows);

    if (!check) {
        return;
    }
    else {
        E.row_obj = check;
    }
}


void editor_open(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

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

        editor_row_insert(E.numrows, line, line_len);
        E.cy = E.numrows;
    }

    free(line);
    fclose(fp);
    E.cy = 0;
    E.changed = 0;
}


void editor_close(void) {
    if (E.row_obj != NULL) {
        for (int at = E.numrows - 1; at >= 0; at--) {
            editor_row_delete(at);
        }
    }
    free(E.filename);
    E.filename = NULL;
}

void editor_save(void) {
    if (E.filename == NULL) {
        E.filename = editor_prompt("Save as: %s");
        if (E.filename == NULL) {
            c_echo_status_message("without filename!");
            return;
        }
    }

    int len;
    char *buf = editor_rows_to_string(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                c_echo_status_message("save %s success", E.filename);
                E.changed = 0;
            }
        }
    }
    else {
        c_echo_status_message("儲存失敗 %s: %s",
                E.filename,
                strerror(errno));
    }

    close(fd);
    free(buf);
}


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
 *  Input
 */


void utf8_move_cx_right(void) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row_obj[E.cy];

    if (row) {
        char c = row->chars[E.cx];
        unsigned char cc = c;

        if (cc < 192) {
            E.cx++;
        }
        else if (cc < 224 && cc > 192) {
            E.cx += 2;
        }
        else if (cc < 240 && cc > 224) {
            E.cx += 3;
        }
        else if (cc > 240) {
            E.cx += 4;
        }
    }
}

void utf8_fix_cx_position(void) {
    unsigned char c = E.row_obj[E.cy].chars[E.cx];

    if (c < 128) {
        return;
    }
    utf8_move_cx_left();
}

void utf8_move_cx_left(void) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row_obj[E.cy];

    if (row) {
        E.cx--;
        unsigned char cc = row->chars[E.cx];

        while (cc >= 0b10000000 && cc <= 0b10111111) {
            E.cx--;
            cc = row->chars[E.cx];
        }
    }
}


char *editor_prompt(char *prompt) {
    size_t buf_size = 128;
    char *buf = malloc(buf_size);

    size_t buf_len = 0;
    buf[0] = '\0';

    while (1) {
        c_echo_status_message(prompt, buf);
        editor_refresh_screen();

        int c = editor_read_key();
        if (c == DEL_KEY || c == CTRL_('h') || c == BACKSPACE) {
            if (buf_len != 0) {
                buf_len--;
                buf[buf_len] = '\0';
            }
        }
        else if (c == '\x1b') {
            c_echo_status_message("");
            free(buf);
            return NULL;
        }
        else if (c == '\r') {
            if (buf_len != 0) {
                c_echo_status_message("");
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


void editor_move_cursor(int key) {
    switch (key) {
        case ARROW_DOWN:
            if (E.cy < (E.numrows - 1)) {
                E.cy++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_RIGHT:
            {
                erow *row = (E.cy >= E.numrows) ?
                    NULL : &E.row_obj[E.cy];
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

                if (row && E.cx < (row->size - last_char_size)) {
                    utf8_move_cx_right();
                }
                else if (row && ((E.cx == (row->size - last_char_size))
                            || row->size == 0)) {
                    c_move_to_next_line_of_start();
                }
            }
            break;
        case ARROW_LEFT:
            {
                if (E.cx != 0) {
                    utf8_move_cx_left();
                }
                else if (E.cy > 0) {
                    c_move_to_previous_line_of_end();
                }
            }
            break;
    }

    c_move_start_or_end();
}

void editor_mode_command(int key) {
    switch (key) {
        case 'q':
            if (E.changed) {
                c_echo_status_message("Use <leader>Q force leave");
                E.mode = NORMAL_MODE;
            }
            else {
                editor_close();
                write(STDOUT_FILENO, "\x1b[2J", 4);
                write(STDOUT_FILENO, "\x1b[H", 3);
                exit(0);
            }
            break;
        case 'Q':
            editor_close();
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case 'h':
            c_help();
            E.mode = NORMAL_MODE;
            break;
        case 's':
            editor_save();
            E.mode = NORMAL_MODE;
            break;
        case 'f':
            c_search();
            E.mode = NORMAL_MODE;
            break;
        default:
            E.mode = NORMAL_MODE;
            break;
    }
}

int editor_mode_special_move(int key) {
    switch (key) {
        case DEL_KEY:
            utf8_move_cx_right();
            c_delete_char();
            return IS_SPECIAL_KEY;
        case BACKSPACE:
        case CTRL_('h'):
            c_delete_char();
            return IS_SPECIAL_KEY;
        case PAGE_UP:
            c_page_up();
            return IS_SPECIAL_KEY;
        case PAGE_DOWN:
            c_page_down();
            return IS_SPECIAL_KEY;
        case HOME_KEY:
            c_move_to_line_of_start();
            return IS_SPECIAL_KEY;
        case END_KEY:
            c_move_to_line_of_end();
            return IS_SPECIAL_KEY;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(key);
            return IS_SPECIAL_KEY;
        default:
            return IS_NOT_SPECIAL_KEY;
    }
    return IS_NOT_SPECIAL_KEY;
}

void editor_mode_number_command(int key) {
    if (editor_mode_special_move(key)) {
        E.mode = NORMAL_MODE;
        E.number_command = 0;
        return;
    }
    int value = 0;

    switch (key) {
        case CTRL_('l'):
        case '\x1b':
        case CTRL_('c'):
            E.mode = NORMAL_MODE;
            E.number_command = 0;
            return;
        case '0':
            break;
        case '1':
            value = 1;
            break;
        case '2':
            value = 2;
            break;
        case '3':
            value = 3;
            break;
        case '4':
            value = 4;
            break;
        case '5':
            value = 5;
            break;
        case '6':
            value = 6;
            break;
        case '7':
            value = 7;
            break;
        case '8':
            value = 8;
            break;
        case '9':
            value = 9;
            break;
        case 'g':
            c_move_to_line(E.number_command);
            E.mode = NORMAL_MODE;
            E.number_command = 0;
            break;
        default:
            E.mode = NORMAL_MODE;
            E.number_command = 0;
            return;
    }

    E.number_command = E.number_command * 10 + value;
}

void editor_mode_insert(int key) {
    if (editor_mode_special_move(key)) {
        return;
    }

    switch (key) {
        case '\r':
            c_insert_newline();
            break;
        case CTRL_('c'):
            E.mode = NORMAL_MODE;
            if (E.cx <= 0) {
                E.cx = 0;
            }
            else if (E.cx >= E.row_obj[E.cy].size) {
                utf8_move_cx_left();
            }
            break;
        case CTRL_('l'):
        case '\x1b':
            E.mode = NORMAL_MODE;
            if (E.cx <= 0) {
                E.cx = 0;
            }
            else if (E.cx >= E.row_obj[E.cy].size) {
                utf8_move_cx_left();
            }
            break;
        default:
            c_insert_char(key);
            break;
    }
}

void editor_mode_normal(int key) {
    if (editor_mode_special_move(key)) {
        return;
    }

    switch (key) {
        case ' ':
            E.mode = COMMAND_MODE;
            break;
        case 'i':
            E.mode = INSERT_MODE;
            break;
        case 'a':
            {
                if (&E.row_obj[E.cy]) {
                   if (E.row_obj[E.cy].size >= 1) {
                       utf8_move_cx_right();
                   }
                }
            }
            E.mode = INSERT_MODE;
            break;
        case 'A':
            {
                if (&E.row_obj[E.cy]) {
                   if (E.row_obj[E.cy].size >= 1) {
                       c_move_to_line_of_end();
                       utf8_move_cx_right();
                   }
                }
            }
            E.mode = INSERT_MODE;
            break;
        case 'o': // english o
            if (E.row_obj) {
                E.cy++;
            }
            E.cx = 0;
            editor_row_insert(E.cy, "", 0);
            E.mode = INSERT_MODE;
            break;
        case 'O': // english O
            E.cx = 0;
            editor_row_insert(E.cy, "", 0);
            E.mode = INSERT_MODE;
            break;
        case '0': // number 0
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            E.mode = NUMBER_COMMAND_MODE;
            editor_mode_number_command(key);
            break;

        case 'x':
            utf8_move_cx_right();
            c_delete_char();
            break;
        case 'X':
            c_delete_char();
            break;

        case 'h':
        case 'l':
        case 'k':
        case 'j':
            editor_move_cursor(vim_to_arrow(key));
            break;
        case 'H':
        case 'L':
            {
                key = vim_to_arrow(key);

                if (key == PAGE_UP) {
                    E.cy = E.row_offset;
                }
                else if (key == PAGE_DOWN) {
                    E.cy = E.row_offset + E.rows - 1;

                    if (E.numrows <= 0) {
                        E.cy = 0;
                    }
                    else if (E.cy > E.numrows) {
                        E.cy = E.numrows - 1;
                    }
                }

                c_move_start_or_end();
            }
            break;
        case 'K':
            c_page_up();
            break;
        case 'J':
            c_page_down();
            break;
        case '^':
            c_move_to_line_of_start();
            break;
        case '$':
            c_move_to_line_of_end();
            break;

        case 'n':
            c_search_next();
            break;
        case 'p':
            c_search_previous();
            break;
    }
}

void editor_process_keypress (void) {
    int c = editor_read_key();

    if (E.mode == COMMAND_MODE) {
        editor_mode_command(c);
    }
    else if (E.mode == NORMAL_MODE) {
        editor_mode_normal(c);
    }
    else if (E.mode == INSERT_MODE) {
        editor_mode_insert(c);
    }
    else if (E.mode == NUMBER_COMMAND_MODE) {
        editor_mode_number_command(c);
    }
    else {
        die("mode_error");
    }
}


/*
 *  Output
 */


void editor_scroll (void) {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editor_convert_cx_to_rx(&E.row_obj[E.cy], E.cx);
    }

    if (E.cy < E.row_offset) {
        E.row_offset = E.cy;
    }
    if (E.cy >= E.row_offset + E.rows) {
        E.row_offset = E.cy - E.rows + 1;
    }
    if (E.rx < E.col_offset) {
        E.col_offset = E.rx;
    }
    if (E.rx >= E.col_offset + E.cols) {
        E.col_offset = E.rx - E.cols + 1;
    }
}

void editor_draw_rows(struct abuf *ab) {
    int y;

    for (y = 0; y < E.rows; y++) {
        int file_row = y + E.row_offset;

        if (file_row >= E.numrows) {
            if (E.numrows == 0 && y == E.rows / 3) {
                char welcome[80];
                int welcome_len = snprintf(welcome, sizeof(welcome),
                        "Woe -- version %s", WOE_VERSION);

                if (welcome_len > E.cols) {
                    welcome_len = E.cols;
                }

                int padding = (E.cols - welcome_len) / 2;
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
            int len = E.row_obj[file_row].rsize - E.col_offset;

            if (len < 0) {
                len = 0;
            }
            if (len > E.cols) {
                len = E.cols;
            }
            abuf_append(ab,
                    &E.row_obj[file_row].render[E.col_offset],
                    len);
        }

        abuf_append(ab, "\x1b[K", 3);
        abuf_append(ab, "\r\n", 2);
    }
}

void editor_draw_status_bar(struct abuf *ab) {
    abuf_append(ab, "\x1b[7m", 4);

    char status[80];
    char rstatus[80];

    char *mode;
    switch (E.mode) {
        case NORMAL_MODE:
            mode = "Normal";
            break;
        case INSERT_MODE:
            mode = "Insert";
            break;
        case COMMAND_MODE:
            mode = "Command";
            break;
        case NUMBER_COMMAND_MODE:
            mode = "Number Command";
            break;
        default:
            mode = "Error"; // Impossible or Error
            break;
    }

    int len = E.cols;
    int left_len = snprintf(status, sizeof(status),
            "%.20s - %d lines %s",
            E.filename ? E.filename : "[No Name]",
            E.numrows,
            E.changed ? "(modified)" : "");
    int right_len = snprintf(rstatus, sizeof(rstatus),
            "%s %d/%d %d/%d",
            mode,
            E.cx + 1,
            E.row_obj ? E.row_obj[E.cy].size : 0,
            E.cy + 1, E.numrows);

    if (left_len > E.cols) {
        left_len = E.cols;
        len = 0;
    }
    else {
        len = (E.cols - left_len - right_len);
    }
    abuf_append(ab, status, left_len);

    while (len) {
        abuf_append(ab, " ", 1);
        len--;
    }
    abuf_append(ab, rstatus, right_len);
    abuf_append(ab, "\x1b[m", 3);
    abuf_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct abuf *ab) {
    abuf_append(ab, "\x1b[K", 3);
    int msg_len = strlen(E.status_msg);

    if (msg_len > E.cols) {
        msg_len = E.cols;
    }

    if (msg_len && time(NULL) - E.status_msg_time < 5) {
        abuf_append(ab, E.status_msg, msg_len);
    }
}

void editor_refresh_screen(void) {
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    abuf_append(&ab, "\x1b[?25l", 6);
    abuf_append(&ab, "\x1b[H", 3);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    char buf[32];

    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
            (E.cy - E.row_offset) + 1,
            (E.rx - E.col_offset) + 1);
    abuf_append(&ab, buf, strlen(buf));

    abuf_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abuf_free(&ab);
}


/*
 *  Init
 */


void init_editor(void) {
    E.cx              = 0;
    E.cy              = 0;
    E.rx              = 0;
    E.numrows         = 0;
    E.number_command  = 0;
    E.row_offset      = 0;
    E.col_offset      = 0;
    E.row_obj         = NULL;
    E.changed         = 0;
    E.filename        = NULL;
    E.status_msg[0]   = '\0';
    E.status_msg_time = 0;
    E.mode            = NORMAL_MODE;

    if (get_window_size(&E.rows, &E.cols) == -1) {
        die("get_window_size");
    }
    E.rows -= 2;
}


void load_module(void) {
    fHandle = dlopen("libplugin-grep.so.1", RTLD_NOW);

    if (!fHandle) {
        fprintf(stderr, "%s\n", dlerror());
        exit(1);
    }
    dlerror();

    c_search = (void (*)(void))dlsym(fHandle, "c_search");
    c_search_next = (void (*)(void))dlsym(fHandle, "c_search_next");
    c_search_previous = (void (*)(void))dlsym(fHandle, "c_search_previous");
    module_search_stop = (void (*)(void))dlsym(fHandle, "module_search_stop");
}


int main(int argc, char *argv[]) {
    enable_rawmode();
    init_editor();
    load_module();

    if (argc >= 2) {
        editor_open(argv[1]);
    }

    c_help();

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
    module_search_stop();
    dlclose(fHandle);
}
