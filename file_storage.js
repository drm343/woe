import * as std from "std";


function KeyPress(key) {
    return key.charCodeAt(0);
}


export function FileStorage() {
    this.files = [];
    this.counter = 0;
    this.current_files = {};
    this.menu = "";
}


FileStorage.prototype.push = function(v) {
    this.files.push(v);
    this.counter = 0;
    this.next();
}


FileStorage.prototype.find = function(f) {
    return this.files.find(f);
}


FileStorage.prototype.next = function() {
    let length = this.files.length;

    if (length == 0) {
        this.menu = "";
        return;
    }

    this.menu = "";

    for (let i = 0; i < 3; i++) {
        let counter = this.counter + i;
        let file_name = this.files[counter];

        if (file_name) {
            this.current_files[i] = {'name': file_name,
                    'value': counter};
            let c = i + 1;
            this.menu += `${c}:` + file_name + ' ';
        }
    }

    if (length > 3) {
        this.menu += '4: next';
    }

    this.counter += 3;
    this.counter = this.counter > this.files.length ? 0 : this.counter;

    if (!this.menu) {
        this.next();
    }
}


FileStorage.prototype.open = function(terminal, key) {
    let is_success = true;

    switch (key) {
        case KeyPress('o'): // english small o
            if (terminal.changed) {
                terminal.echo_status_message("You need to save file first.");
                is_success = false;
                break;
            }
        case KeyPress('O'): // english big O
            {
                let v = terminal.prompt('open: %s');

                if (v) {
                    let exists = this.find(x => x == v);

                    if (!exists) {
                        this.push(v);
                    }

                    let find_file = std.popen(`find . -name ${v}`, 'r');

                    if (find_file.getline()) {
                        terminal.file_close();
                        terminal.file_open(v);
                    }
                    else {
                        terminal.file_close();
                        terminal.filename = v;
                        terminal.file_save();
                    }
                }
                else {
                    terminal.echo_status_message("Without filename.");
                }
            }
            break;
    }
    return is_success;
}


FileStorage.prototype.close = function(terminal, key) {
    let is_success = true;

    switch (key) {
        case KeyPress('c'):
            if (terminal.changed) {
                terminal.echo_status_message("Use <func>C force close");
                is_success = false;
            }
            else {
                terminal.file_close();
            }
            break;
        case KeyPress('C'):
            terminal.file_close();
            break;
    }
    return is_success;
}


FileStorage.prototype.save = function(terminal, key) {
    let is_success = true;
    switch (key) {
        case KeyPress('s'):
            terminal.file_save();
            break;
    }
    return is_success;
}
