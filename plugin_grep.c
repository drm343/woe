#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include <sys/ioctl.h>


#include "woe.h"
#include "plugin_grep.h"


#define MAX_ARRAY_SIZE 10


typedef struct {
    int row;
    int column;
} Position;


Position *create_position_array(int value) {
    Position *result = malloc(sizeof(Position) * value);
    return result;
}


static void free_position(Position *a) {
    free(a);
}


static void c_search_inline(char *query);
static char *create_tmp_file (void);


static char *create_tmp_file (void) {
    int len;
    char *buf = editor_rows_to_string(&len);

    char name[] = "tmp-XXXXXX";
    mkstemp(name);
    int fd = open(name, O_RDWR | O_CREAT, 0644);

    if (write(fd, buf, len) != len) {
        c_echo_status_message("Rico! You know what to do!", NULL);
    }
    close(fd);
    free(buf);

    char *result = malloc(sizeof(char) * 11);
    memcpy(result, name, 11);
    return result;
}


static Position *use = NULL;
static int current = 0;
static int max_result_size = 0;


static void c_search_inline(char *query) {
    if (use != NULL) {
        c_search_done();
    }
    else {
        use = create_position_array(MAX_ARRAY_SIZE);
    }

    current = 0;

    char *filename = create_tmp_file();

    FILE * fp;
    char buffer[80];
    char command_buffer[80];

    char *command = "grep -n \"%s\" %s";
    if (snprintf(command_buffer, 80,
            command, query, filename) == -1) {
        return;
    }

    fp = popen(command_buffer, "r");

    while (fgets(buffer, sizeof (buffer), fp) != NULL) {
        strtok(buffer, ":");

        Position *a = &use[current];
        a->row = atoi(buffer);
        a->column = 0;
        strtok(buffer, ":");
        c_echo_status_message("this:%s", buffer);

        if (current < MAX_ARRAY_SIZE && current >= 0) {
            current++;
            max_result_size++;
        }
        else {
            break;
        }
    }

    if (max_result_size > 0) {
        c_search_next();
    }

    pclose(fp);
    remove(filename);
    free(filename);
}


void c_search(void) {
    char *query = editor_prompt("Search: %s (ESC to cancel)");

    if (query == NULL) {
        return;
    }

    c_search_inline(query);
    free(query);
}


void c_search_done(void) {
    memset(use, 0, sizeof(Position) * MAX_ARRAY_SIZE);
    current = 0;
    max_result_size = 0;
}


void c_search_next(void) {
    if (use == NULL || max_result_size <= 0) {
        c_echo_status_message("without search result...", NULL);
        return;
    }

    if ((current + 1) < MAX_ARRAY_SIZE
            && (current + 1) < max_result_size) {
        current++;
    }
    else {
        current = 0;
    }

    Position *a = &use[current];

    c_move_to_line(a->row);
    c_move_to_column(a->column);
}


void c_search_previous(void) {
    if (use == NULL || max_result_size <= 0) {
        c_echo_status_message("without search result...", NULL);
        return;
    }

    if ((current - 1) >= 0) {
        current--;
    }
    else {
        current = max_result_size - 1;
    }

    Position *a = &use[current];

    c_move_to_line(a->row);
    c_move_to_column(a->column);
}


void module_search_stop(void) {
    free_position(use);
}
