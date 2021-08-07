import * as std from "std";

export function JSMode() {
    this.conf = "js_mode.conf";
}


JSMode.prototype.enable = function (terminal, file_storage, argv) {
    terminal.mode = argv.mode.NORMAL;

    if (file_storage.close(terminal, argv.KeyPress('c'))) {
        let v = this.conf;

        let exists = file_storage.find(x => x == v);

        if (!exists) {
            file_storage.push(v);
        }

        let find_file = std.popen(`find . -name ${v}`, 'r');

        if (find_file.getline()) {
            terminal.file_open(v);
        }
        else {
            terminal.filename = v;
            terminal.file_save();
        }
    }
    argv.menu.main();
    return argv.editor_mode_normal;
}


JSMode.prototype.eval = function (terminal, file_storage, argv) {
    terminal.mode = argv.mode.NORMAL;

    terminal.file_save();

    let file = std.open(this.conf, 'r');
    try {
        eval(file.readAsString());
    }
    catch (e) {
        terminal.echo_status_message(e);
    }
    file.close();

    argv.menu.main();
    return argv.editor_mode_normal;
}
