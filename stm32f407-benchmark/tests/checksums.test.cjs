const assert = require('node:assert/strict');

/* Recomputes the reference checksums hard-coded in bench/algorithms.c and
   bench/integer_workloads.c from the generators, independently of the firmware. */

const u32 = v => BigInt.asUintN(32, BigInt(v));
const ITEMS = 256, DIM = 16;

const a = [], b = [], signedA = [], signedB = [];
for (let i = 0; i < ITEMS; i++) {
    a.push(u32(u32(u32(i + 1) * 0x9e3779b9n) ^ 0x735a2d97n));
    b.push(u32(u32(u32(u32(i + 7) * 0x85ebca6bn) >> BigInt(i % 20)) | 1n));
    let sa = BigInt.asIntN(32, a[i] & 0x7fffffffn);
    let sb = BigInt.asIntN(32, b[i] & 0x7fffffffn);
    if ((i & 1) !== 0) sa = -sa;
    if ((i & 2) !== 0) sb = -sb;
    signedA.push(sa); signedB.push(sb);
}

let mul32 = 0n, mul64 = 0n, udivmod = 0n, sdiv = 0n, gcd = 0n;
for (let i = 0; i < ITEMS; i++) {
    mul32 = u32(mul32 + u32(a[i] * b[i]));
    mul64 = BigInt.asUintN(64, mul64 + BigInt.asUintN(64, a[i] * b[i]));
    udivmod = u32(udivmod + u32(a[i] / b[i]) + u32(a[i] % b[i]));
    sdiv = u32(sdiv + BigInt.asUintN(32, signedA[i] / signedB[i]));
    let x = a[i], y = b[i];
    while (y !== 0n) { const r = x % y; x = y; y = r; }
    gcd = u32(gcd + x);
}
assert.equal(mul32, 0x9f75f7ban);
assert.equal(mul64, 0x9c5a7a1c9f75f7ban);
assert.equal(udivmod, 0x4ad0bdf0n);
assert.equal(sdiv, 0x000db5f5n);
assert.equal(gcd, 0x1746n);

const ma = [], mb = [];
for (let i = 0; i < DIM * DIM; i++) {
    ma.push((i * 37 + 11) % 257 - 128);
    mb.push((i * 53 + 7) % 251 - 125);
}
const matmul = new Array(DIM * DIM).fill(0);
for (let row = 0; row < DIM; row++) {
    for (let col = 0; col < DIM; col++) {
        let sum = 0;
        for (let k = 0; k < DIM; k++) {
            sum += ma[row * DIM + k] * mb[k * DIM + col];
        }
        matmul[row * DIM + col] = sum | 0;
    }
}
let fnv = 2166136261n;
for (const value of matmul) fnv = u32(u32(fnv ^ BigInt.asUintN(32, BigInt(value))) * 16777619n);
assert.equal(fnv, 0x1cdaf95bn);

let crc = 0xffffffff;
for (let i = 0; i < 1024; i++) {
    crc ^= (i * 37 + (i >> 3) * 19 + 11) & 0xff;
    for (let bit = 0; bit < 8; bit++) {
        crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
}
assert.equal((~crc) >>> 0, 0x28e2568e);

/* fir_f32 compares against (float)reference * 2^-11 with exact equality, which
   requires every integer reference to be exactly representable in float32. */
const taps = [], inputs = [], reference = new Array(256).fill(0);
for (let i = 0; i < 271; i++) inputs.push(((i * 73 + 19) % 251) - 125);
for (let tap = 0; tap < 16; tap++) taps.push(((tap * 11 + 5) % 31) - 15);
for (let tap = 0; tap < 16; tap++) {
    for (let i = 0; i < 256; i++) reference[i] += inputs[i + tap] * taps[tap];
}
const bound = reference.reduce((max, v) => Math.max(max, Math.abs(v)), 0);
assert.equal(bound, 5165);
assert.ok(bound < 2 ** 24);

console.log('Checksum tests passed');
