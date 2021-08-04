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
