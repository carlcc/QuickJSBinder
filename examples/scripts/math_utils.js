/**
 * @file math_utils.js
 * @brief A utility ES module demonstrating named exports.
 *
 * This module is imported by test_es_module.js via:
 *   import { add, mul, PI, clamp } from "./math_utils.js";
 */

export const PI = 3.14159265358979;
export const E  = 2.71828182845905;

export function add(a, b) {
    return a + b;
}

export function mul(a, b) {
    return a * b;
}

export function clamp(value, min, max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * Calculate the distance between two 2D points.
 */
export function distance(x1, y1, x2, y2) {
    const dx = x2 - x1;
    const dy = y2 - y1;
    return Math.sqrt(dx * dx + dy * dy);
}
