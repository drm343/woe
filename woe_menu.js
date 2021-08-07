function render() {
    let menu = this.current;

    this.display = "";

    for (let index = 1; index <= 9; index++) {
        let v = menu.properties[index];

        if (v) {
            this.display += `${index}: ${v.name} `
        }
    }

    let v = menu.properties[0];

    if (v) {
        this.display += `0: ${v.name}`
    }
    return this.display;
}


// can not use submenu in main_menu. it will return undefined.
var main_menu = {
    FILES:  1,
    CANCEL: 0,
    name: "main_menu",
    main: true,
    properties: {
        0: {name: "Cancel", value: 0, submenu: false, do_job: false},
        1: {name: "Files", value: 1, submenu: false, do_job: false},
    },
};


var files_submenu = {
    OPEN:        1,
    NEW_FILE:    2,
    SAVE:        3,
    SWITCH:      4,
    CLOSE:       5,
    FORCE_CLOSE: 6,
    CANCEL:      0,
    name: "files_submenu",
    main: false,
    properties: {
        0: {name: "Cancel", value: 0, submenu: main_menu, do_job: false},
        1: {name: "Open", value: 1, submenu: false, do_job: open_file},
        2: {name: "New File", value: 2, submenu: false, do_job: open_new_file},
        3: {name: "Save", value: 3, submenu: false, do_job: save_file},
        4: {name: "Switch", value: 4, submenu: false, do_job: file_switch},
        5: {name: "Close", value: 5, submenu: false, do_job: close_file},
        6: {name: "Force Close", value: 6, submenu: false, do_job: force_close_file},
    },
};


export function Menu() {
    this.current = main_menu;
    this.render  = render;
    this.display = "";
}


Menu.prototype.main = function () {
    this.current = main_menu;
}


Menu.prototype.goto_menu = function (v) {
    let is_main = this.current.main;
    let menu_seleted;

    if (is_main) {
        let properties = {
            0: {name: "Cancel", value: 0, submenu: false, do_job: false},
            1: {name: "Files", value: 1, submenu: files_submenu, do_job: false},
        };

        menu_seleted = properties[v];
    }
    else {
        menu_seleted = this.current.properties[v];
    }


    if (menu_seleted) {
        let menu = menu_seleted.submenu;
        let do_job = menu_seleted.do_job;

        if (menu) {
            this.current = menu;
            this.render();
            return [true, false];
        }
        else if (do_job) {
            return [false, do_job];
        }
    }
    return [false, false];
}


function open_file(terminal, file_storage, argv) {
    file_storage.open(terminal, argv.KeyPress('o'));
    terminal.mode = argv.mode.NORMAL;
    return argv.editor_mode_normal;
}


function open_new_file(terminal, file_storage, argv) {
    file_storage.open(terminal, argv.KeyPress('O'));
    terminal.mode = argv.mode.NORMAL;
    return argv.editor_mode_normal;
}


function close_file(terminal, file_storage, argv) {
    file_storage.close(terminal, argv.KeyPress('c'));
    terminal.mode = argv.mode.NORMAL;
    return argv.editor_mode_normal;
}


function force_close_file(terminal, file_storage, argv) {
    file_storage.close(terminal, argv.KeyPress('C'));
    terminal.mode = argv.mode.NORMAL;
    return argv.editor_mode_normal;
}


function save_file(terminal, file_storage, argv) {
    file_storage.save(terminal, argv.KeyPress('s'));
    terminal.mode = argv.mode.NORMAL;
    return argv.editor_mode_normal;
}


function file_switch(terminal, file_storage, argv) {
    terminal.mode = argv.mode.MENU;
    let next_function = argv.editor_mode_menu;

    if (!file_storage.menu) {
        file_storage.next();
    }

    if (file_storage.menu) {
        terminal.echo_status_message(file_storage.menu);
    }
    else {
        terminal.mode = argv.mode.NORMAL;
        next_function = argv.editor_mode_normal;

        terminal.echo_status_message('');
        return argv.editor_mode_normal;
    }

    return function(terminal, key) {
        let next_function = argv.editor_mode_normal;
        let run_forever = true;

        let f = false;

        switch (key) {
            case argv.KeyPress('1'):
                f = file_storage.current_files[0];
                break;
            case argv.KeyPress('2'):
                f = file_storage.current_files[1];
                break;
            case argv.KeyPress('3'):
                f = file_storage.current_files[2];
                break;
            case argv.KeyPress('4'):
                file_storage.next();
                terminal.echo_status_message(file_storage.menu);
                break;
        }

        if (f) {
            terminal.file_close();
            terminal.file_open(f.name);
        }

        terminal.mode = argv.mode.NORMAL;
        terminal.echo_status_message('');

        argv.menu.main();
        return [run_forever, next_function];
    };
}
