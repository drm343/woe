export function FileStorage() {
    this.files = [];
    this.counter = 0;
    this.current_files = {};
}

FileStorage.prototype.push = function(v) {
    this.files.push(v);
}

FileStorage.prototype.find = function(f) {
    return this.files.find(f);
}

FileStorage.prototype.show = function() {
    let str = ''

    for (let i = 0; i < 3; i++) {
        let counter = this.counter + i;
        let file_name = this.files[counter];

        if (file_name) {
            this.current_files[i] = {'name': file_name,
                    'value': counter};
            let c = i + 1;
            str += `${c}:` + file_name + ' ';
        }
    }
    str += '4: next';

    this.counter += 3;
    this.counter = this.counter > this.files.length ? 0 : this.counter;

    if (!str) {
        return this.show();
    }
    return str;
}
