const assert = require('node:assert/strict');
const { summarize, parseLog, analyzeIrq, validateRun } = require('../tools/report.cjs');
assert.deepEqual(summarize([30, 10, 20, 40]), { min: 10, median: 25, p99: 40, max: 40 });
const log = ['BEGIN,armcc-speed,168000000,84000000', 'ERROR,exti,timeout,4', 'END,armcc-speed'].join('\n');
assert.equal(parseLog(log, 'armcc-speed').errors.exti, 'timeout,4');
assert.throws(() => parseLog(log, 'gcc-speed'), /firmware/);
assert.throws(() => parseLog(log.replace('END,armcc-speed', ''), 'armcc-speed'), /end/i);

const last = 0xfffffff0;
const first = (last + 16812) >>> 0;
const second = (first + 33600) >>> 0;
const samples = [
    'BEGIN,gcc-size,168000000,84000000',
    'IRQ,tim2_dma,0,14,30,' + last + ',1',
    'IRQ,tim2_dma,1,15,32,' + first + ',0',
    'IRQ,tim2_dma,2,16,34,' + second + ',1',
    'STATUS,tim2_dma,3,0,2',
    'END,gcc-size'
].join('\n');
const loaded = analyzeIrq(parseLog(samples, 'gcc-size'), 'tim2_dma');
assert.deepEqual(loaded.jitter, [12, 16800]);
assert.equal(loaded.missed, 1);
assert.equal(loaded.active, 2);
assert.equal(loaded.shortage, 1021);
assert.match(loaded.error, /Incomplete/);
assert.throws(() => parseLog(samples.replace('IRQ,tim2_dma,2', 'IRQ,tim2_dma,1'), 'gcc-size'), /sample order/);
const badStatus = samples.replace('STATUS,tim2_dma,3,0,2', 'STATUS,tim2_dma,3,0,0');
assert.match(analyzeIrq(parseLog(badStatus, 'gcc-size'), 'tim2_dma').error, /inconsistent/);
assert.ok(validateRun(parseLog(samples, 'gcc-size')).includes('tim2_dma Incomplete sample set'));
const coremarkFrame = [
    'BEGIN,gcc-size,168000000,84000000',
    'SUITE,2',
    'COREBEGIN,performance',
    'CORELOG,CoreMark Size    : 666',
    'CORELOG,Total ticks      : 12500000',
    'CORELOG,Iterations       : 10000',
    'CORELOG,seedcrc          : 0xe9f5',
    'CORELOG,Correct operation validated. See README.md for run and reporting rules.',
    'COREEND,performance',
    'END,gcc-size'
].join('\n');
assert.equal(parseLog(coremarkFrame, 'gcc-size').coremark.performance.score, 800);
assert.ok(validateRun(parseLog(coremarkFrame, 'gcc-size')).some(failure => /validation/.test(failure)));
assert.match(parseLog(coremarkFrame.replace('12500000', '9999999'), 'gcc-size').coremark.performance.error, /10 seconds/);
console.log('Report tests passed');
