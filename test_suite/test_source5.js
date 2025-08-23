// JavaScript test file
const fs = require('fs');
const path = require('path');

class TestClass {
    constructor(name) {
        this.name = name;
        this.data = [];
    }
    
    addData(value) {
        this.data.push(value);
    }
    
    getSum() {
        return this.data.reduce((a, b) => a + b, 0);
    }
}

function main() {
    const obj = new TestClass("test");
    for (let i = 0; i < 100; i++) {
        obj.addData(i);
    }
    
    console.log(`Sum: ${obj.getSum()}`);
    return 0;
}

main();
