#ifndef WOE_H
#define WOE_H

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>

#include <sys/ioctl.h>
#include <sys/types.h>


/*
 *  DataType
 */


typedef struct erow erow;


/*
 * API
 */


void c_echo_status_message(const char *fmt, ...);
void c_move_to_line(int at);
void c_move_to_column(int at);


/*
 *  Function
 */

void editor_close(void);
void editor_row_insert(int at, char *s, size_t len);
void editor_row_insert_char(erow *row, int at, int c);
void editor_row_delete_char(erow *row, int at);
void editor_row_delete(int at);
void editor_row_update(erow *row);
void editor_row_append_string(erow *row, char *s, size_t len);
void editor_refresh_screen(void);
char *editor_prompt(char *prompt);


/*
 * UTF8 fix
 */


void utf8_move_cx_right(void);
void utf8_fix_cx_position(void);
void utf8_move_cx_left(void);

int editor_convert_cx_to_rx (erow *row, int cx);


/*
 *  File i/o
 */

char * editor_rows_to_string(int *buffer_len);

#endif
