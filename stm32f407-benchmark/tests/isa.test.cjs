const assert = require('node:assert/strict');
const { verifyKernel } = require('../tools/verify-isa.cjs');

assert.deepEqual(verifyKernel('signed_div', ['sdiv'], '00000100 <signed_div>:\n 100: fb90 f0f1 sdiv r0, r0, r1\n'), ['sdiv']);
assert.deepEqual(verifyKernel('unsigned_div', ['udiv'], '    unsigned_div\n        0x00000100:    fbb0f0f1    ....    UDIV     r0,r0,r1\n'), ['udiv']);
assert.deepEqual(verifyKernel('unsigned_div', ['udiv'],
    '00000100 <unsigned_div>:\n 100: fbb0 f0f1 udiv r0, r0, r1\n 104: fbb0 f2f1 udiv r2, r0, r1\n'), ['udiv', 'udiv']);
assert.throws(() => verifyKernel('signed_div', ['sdiv'], '00000100 <signed_div>:\n 100: f000 f800 bl 200 <__aeabi_idiv>\n'), /hardware|helper/i);
assert.throws(() => verifyKernel('unsigned_div', ['udiv'], '00000100 <unsigned_div>:\n 100: fb90 f0f1 udiv r0, r0, r1\n 104: f000 f800 bl 200 <__aeabi_uidivmod>\n'), /helper/i);
assert.throws(() => verifyKernel('signed_div', ['sdiv'], '00000100 <other>:\n 100: fb90 f0f1 sdiv r0, r0, r1\n'), /missing/i);
console.log('ISA tests passed');
