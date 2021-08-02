import * as os from "os";
import { VT100 } from "./vt100.so";


function CTRL_(key) {
    return key.charCodeAt(0) & 0x1f;
}

function KeyPress(key) {
    return key.charCodeAt(0);
}


let special_key = {
    BACKSPACE: 127,
    LEFT:      1000,
    RIGHT:     1001,
    UP:        1002,
    DOWN:      1003,
    DELETE:    1004,
    HOME:      1005,
    END:       1006,
    PAGE_UP:   1007,
    PAGE_DOWN: 1008,
    properties: {
        127:  {name: "backspace", value: 127},
        1000: {name: "left", value: 1000},
        1001: {name: "right", value: 1001},
        1002: {name: "up", value: 1002},
        1003: {name: "down", value: 1003},
        1004: {name: "delete", value: 1004},
        1005: {name: "home", value: 1005},
        1006: {name: "end", value: 1006},
        1007: {name: "page_up", value: 1007},
        1008: {name: "page_down", value: 1008},
    }
};


let mode = {
    NORMAL:         1,
    COMMAND:        2,
    INSERT:         3,
    NUMBER_COMMAND: 4,
    properties: {
        1: {name: "normal", value: 1},
        2: {name: "command", value: 2},
        3: {name: "insert", value: 3},
        4: {name: "number command", value: 4},
    }
};


function vim_to_arrow(key) {
    switch (key) {
        case KeyPress('h'):
            return special_key.LEFT;
            break;
        case KeyPress('l'):
            return special_key.RIGHT;
            break;
        case KeyPress('k'):
            return special_key.UP;
            break;
        case KeyPress('j'):
            return special_key.DOWN;
            break;

        case KeyPress('K'):
            return special_key.PAGE_DOWN;
            break;
        case KeyPress('J'):
            return special_key.PAGE_UP;
            break;
        case KeyPress('H'):
            return special_key.PAGE_UP;
        case KeyPress('L'):
            return special_key.PAGE_DOWN;

        case KeyPress('^'):
            return special_key.HOME;
        case KeyPress('$'):
            return special_key.END;
    }
    return key;
}


function editor_move_cursor(terminal, key) {
    switch (key) {
        case special_key.DOWN:
            if (terminal.cy < (terminal.numrows - 1)) {
                terminal.cy++;
            }
            break;
        case special_key.UP:
            if (terminal.cy != 0) {
                terminal.cy--;
            }
            break;
        case special_key.RIGHT:
            terminal.move_cursur_right_or_next_line();
            break;
        case special_key.LEFT:
            {
                if (terminal.cx != 0) {
                    terminal.move_cursur_left();
                }
                else if (terminal.cy > 0) {
                    terminal.move_cursur_left_or_previous_line();
                }
            }
            break;
    }

    terminal.fix_position();
}


function editor_mode_normal(terminal, key) {
    if (editor_mode_special_move(terminal, key)) {
        return true;
    }

    switch (key) {
        case KeyPress(' '):
            terminal.mode = mode.COMMAND;
            break;
        case KeyPress('i'):
            terminal.mode = mode.INSERT;
            break;
        case KeyPress('a'):
            if (terminal.check_erow_size()) {
                terminal.move_cursur_right();
            }
            terminal.mode = mode.INSERT;
            break;
        case KeyPress('A'):
            if (terminal.check_erow_size()) {
                terminal.move_to_line_of_end();
                terminal.move_cursur_right();
            }
            terminal.mode = mode.INSERT;
            break;
        case KeyPress('o'): // english small o
            if (terminal.check_row_object()) {
                terminal.cy++;
            }
            terminal.cx = 0;
            terminal.row_insert(terminal.cy, "", 0);
            terminal.mode = mode.INSERT;
            break;
        case KeyPress('O'): // english big O
            terminal.cx = 0;
            terminal.row_insert(terminal.cy, "", 0);
            terminal.mode = mode.INSERT;
            break;

        case KeyPress('0'): // number 0
        case KeyPress('1'):
        case KeyPress('2'):
        case KeyPress('3'):
        case KeyPress('4'):
        case KeyPress('5'):
        case KeyPress('6'):
        case KeyPress('7'):
        case KeyPress('8'):
        case KeyPress('9'):
            terminal.mode = mode.NUMBER_COMMAND;
            editor_mode_number_command(terminal, key);
            break;

        case KeyPress('h'):
        case KeyPress('l'):
        case KeyPress('k'):
        case KeyPress('j'):
            {
                let v = vim_to_arrow(key);
                editor_move_cursor(terminal, v);
            }
            break;

        case KeyPress('K'):
        case KeyPress('J'):
            {
                let v = vim_to_arrow(key);
                editor_mode_special_move(terminal, v);
            }
            break;
        /*
         *  Move to current page's top position or bottom position.
         */
        case KeyPress('H'):
        case KeyPress('L'):
            {
                let v = vim_to_arrow(key);

                switch (v) {
                    case special_key.PAGE_UP:
                        terminal.cy = terminal.row_offset;
                        break;
                    case special_key.PAGE_DOWN:
                        terminal.cy = terminal.row_offset
                            + terminal.rows - 1;

                        if (terminal.numrows <= 0) {
                            terminal.cy = 0;
                        }
                        else if (terminal.cy > terminal.numrows) {
                            terminal.cy = terminal.numrows - 1;
                        }
                        break;
                }

                terminal.fix_position();
            }
            break;

        case KeyPress('^'):
            terminal.move_to_line_of_start();
            break;
        case KeyPress('$'):
            terminal.move_to_line_of_end();
            break;

        case KeyPress('x'):
            terminal.move_cursur_right();
            terminal.delete_char();
            terminal.fix_position();
            break;
        case KeyPress('X'):
            terminal.delete_char();
            break;
            /*
        case 'n':
            terminal.search_next();
            break;
        case 'p':
            terminal.search_previous();
            break;
            */
    }
    return true;
}


function editor_mode_command(terminal, key) {
    let run_forever = true;

    switch (key) {
        case KeyPress('q'):
            if (terminal.changed) {
                terminal.echo_status_message("Use <leader>Q force leave");
                terminal.mode = mode.NORMAL;
            }
            else {
                terminal.file_close();
                terminal.clean_screen();
                terminal.move_cursur_home();
                run_forever = false;
            }
            break;
        case KeyPress('Q'):
            terminal.file_close();
            terminal.clean_screen();
            terminal.move_cursur_home();
            run_forever = false;
            break;
        case KeyPress('h'):
            terminal.help();
            break;
        case KeyPress('s'):
            terminal.file_save();
            terminal.mode = mode.NORMAL;
            break;
        default:
            terminal.mode = mode.NORMAL;
            break;
    }
    return run_forever;
}


function editor_mode_special_move(terminal, key) {
    switch (key) {
        case special_key.DELETE:
            terminal.move_cursur_right();
            terminal.delete_char();
            break;
        case special_key.BACKSPACE:
        case CTRL_('h'):
            terminal.delete_char();
            terminal.fix_position();
            break;
        case special_key.PAGE_UP:
            terminal.page_up();
            break;
        case special_key.PAGE_DOWN:
            terminal.page_down();
            break;
        case special_key.HOME:
            terminal.move_to_line_of_start();
            break;
        case special_key.END:
            terminal.move_to_line_of_end();
            break;
        case special_key.UP:
        case special_key.DOWN:
        case special_key.LEFT:
        case special_key.RIGHT:
            editor_move_cursor(terminal, key);
            break;
        default:
            return false;
    }
    return true;
}


function editor_mode_insert(terminal, key) {
    if (editor_mode_special_move(terminal, key)) {
        return true;
    }

    switch (key) {
        case KeyPress('\r'):
            terminal.insert_newline();
            break;
        case CTRL_('c'):
            terminal.mode = mode.NORMAL;
            if (terminal.cx <= 0) {
                terminal.cx = 0;
            }
            else if (terminal.cx >= terminal.get_erow_size_at(terminal.cy - 1))
            {
                terminal.move_cursur_left();
            }
            break;
        case CTRL_('l'):
        case KeyPress('\x1b'):
            terminal.mode = mode.NORMAL;
            if (terminal.cx <= 0) {
                terminal.cx = 0;
            }
            else if (terminal.cx >=
                terminal.get_erow_size_at(terminal.cy - 1))
            {
                terminal.move_cursur_left();
            }
            else {
                terminal.move_cursur_left();
            }
            break;
        default:
            terminal.insert_char(key);
            break;
    }
    return true;
}


function editor_mode_number_command(terminal, key) {
    if (editor_mode_special_move(terminal, key)) {
        terminal.mode = mode.NORMAL;
        terminal.number_command = 0;
        return true;
    }
    let value = 0;

    switch (key) {
        case CTRL_('l'):
        case KeyPress('\x1b'):
        case CTRL_('c'):
            terminal.mode = mode.NORMAL;
            terminal.number_command = 0;
            break;
        case KeyPress('0'):
            break;
        case KeyPress('1'):
            value = 1;
            break;
        case KeyPress('2'):
            value = 2;
            break;
        case KeyPress('3'):
            value = 3;
            break;
        case KeyPress('4'):
            value = 4;
            break;
        case KeyPress('5'):
            value = 5;
            break;
        case KeyPress('6'):
            value = 6;
            break;
        case KeyPress('7'):
            value = 7;
            break;
        case KeyPress('8'):
            value = 8;
            break;
        case KeyPress('9'):
            value = 9;
            break;
        case KeyPress('g'):
            terminal.move_to_line(terminal.number_command);
            terminal.mode = mode.NORMAL;
            terminal.number_command = 0;
            break;
        default:
            terminal.mode = mode.NORMAL;
            terminal.number_command = 0;
            break;
    }

    terminal.number_command = terminal.number_command * 10 + value;
    return true;
}


function process_keys(terminal) {
    let run_forever = true;

    let v = terminal.next_key();

    switch (terminal.mode) {
        case mode.COMMAND:
            run_forever = editor_mode_command(terminal, v);
            break;
        case mode.NORMAL:
            run_forever = editor_mode_normal(terminal, v);
            break;
        case mode.INSERT:
            run_forever = editor_mode_insert(terminal, v);
            break;
        case mode.NUMBER_COMMAND:
            run_forever = editor_mode_number_command(terminal, v);
            break;
        default:
            run_forever = false;
            break;
    }
    return run_forever;
}


function Counter() {
    this.sum = 0;
}

Counter.prototype.length = function (str) {
    this.sum = 0;
    for (let i = 0; i < str.length; i++) {
        if (str.charCodeAt(i) > 128) {
            this.sum += 2;
        }
        else {
            this.sum += 1;
        }
    }
    return this.sum;
}


function bar_status(terminal) {
    let origin_filename = terminal.filename;
    let filename = origin_filename
        + ' '.repeat(10 - bar_counter.length(origin_filename));
    let changed = terminal.changed ? "(modified)" : "";
    let right = `${filename} - ${terminal.numrows} lines ${changed}`;

    let current_mode = mode.properties[terminal.mode].name;
    let cx = terminal.cx + 1;
    let cy = terminal.cy + 1;

    let cx_size = 0;
    if (terminal.check_row_object()) {
        cx_size = terminal.get_erow_size_at(terminal.cy);
    }
    else {
        cx_size = 0;
    }

    let left = `${current_mode} ${cx}/${cx_size} ${cy}/${terminal.numrows}`

    let space = ' '.repeat(80
        - bar_counter.length(right) - bar_counter.length(left));

    let v = right + space + left;
    return v;
}


function main() {
    let run_forever = true;

    let terminal = new VT100(mode.NORMAL);
    terminal.enable_rawmode();

    if (scriptArgs.length >= 2) {
        terminal.file_open(scriptArgs[1]);
    }

    terminal.help();

    while (run_forever) {
        let v = bar_status(terminal);
        terminal.refresh_woe_ui(v);
        run_forever = process_keys(terminal);
    }
    terminal.disable_rawmode();
}

var bar_counter = new Counter();
main();
