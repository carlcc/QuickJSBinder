/**
 * @file vec2.js
 * @brief A Vec2 class exported as an ES module.
 *
 * Demonstrates exporting a class from a module.
 */

export default class Vec2 {
    constructor(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }

    length() {
        return Math.sqrt(this.x * this.x + this.y * this.y);
    }

    add(other) {
        return new Vec2(this.x + other.x, this.y + other.y);
    }

    scale(s) {
        return new Vec2(this.x * s, this.y * s);
    }

    toString() {
        return `Vec2(${this.x}, ${this.y})`;
    }
}
