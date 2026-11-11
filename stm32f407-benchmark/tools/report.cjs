const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');

const profiles = ['ac6-speed', 'gcc-speed', 'ac6-o3', 'gcc-o3', 'ac6-o2lto', 'gcc-o2lto',
                  'ac6-o0', 'gcc-o0', 'ac6-size', 'gcc-size'];
const algorithms = ['crc32', 'fir_i16', 'fir_f32'];
const extendedAlgorithms = ['mul32', 'mul64', 'udivmod32', 'sdiv32', 'gcd32', 'matmul_i16'];
const coremarkSeeds = { performance: 'e9f5', validation: '18f2' };
const interrupts = { tim2: 1024, exti: 1024, dma: 128, tim2_dma: 1024 };
const periodCycles = 16800;

function summarize(values) {
    if (!values.length) throw new Error('No values to summarize');
    const sorted = [...values].sort((a, b) => a - b);
    const n = sorted.length;
    const middle = Math.floor(n / 2);
    return {
        min: sorted[0],
        median: n % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2,
        p99: sorted[Math.ceil(n * 0.99) - 1],
        max: sorted[n - 1]
    };
}

function number(field) {
    if (!/^\d+$/.test(field) || Number(field) > 0xffffffff) {
        throw new Error('Invalid unsigned field ' + field);
    }
    return Number(field);
}

function append(samples, scenario, index, value) {
    samples[scenario] ??= [];
    if (index !== samples[scenario].length) throw new Error('Bad sample order for ' + scenario);
    samples[scenario].push(value);
}

function parseCoremark(lines, mode) {
    const capture = expression => lines.map(line => line.match(expression)?.[1]).find(Boolean);
    const ticks = Number(capture(/^Total ticks\s*:\s*(\d+)$/));
    const iterations = Number(capture(/^Iterations\s*:\s*(\d+)$/));
    const size = Number(capture(/^CoreMark Size\s*:\s*(\d+)$/));
    const seed = capture(/^seedcrc\s*:\s*0x([0-9a-f]+)$/i);
    const valid = lines.some(line => line.startsWith('Correct operation validated.'));
    if (Number.isFinite(ticks) && ticks < 10000000) return { error: 'Ran for less than 10 seconds' };
    if (!Number.isFinite(ticks) || !valid || iterations !== 10000 || size !== 666 ||
        seed !== coremarkSeeds[mode] || lines.some(line => /ERROR|Errors detected|unsupported format/.test(line))) {
        return { error: 'CoreMark CRC, duration, iteration count, or seed validation failed' };
    }
    return { ticks, iterations, score: iterations * 1e6 / ticks };
}

function parseLog(text, expected) {
    const lines = text.split(/\r?\n/).map(line => line.trim()).filter(Boolean);
    const start = lines.findIndex(line => line.startsWith('BEGIN,'));
    if (start < 0) throw new Error('Missing BEGIN');
    const [, image, coreHz, timerHz] = lines[start].split(',');
    if (image !== expected) throw new Error('Unexpected firmware ' + image);
    const end = lines.indexOf('END,' + image, start + 1);
    if (end < 0) throw new Error('Missing END for ' + image);
    const run = { algorithms: {}, irq: {}, status: {}, errors: {}, coremark: {}, extended: false };
    let coreMode, coreLines;
    for (const line of lines.slice(start + 1, end)) {
        const [type, name, ...fields] = line.split(',');
        if (line === 'SUITE,2' && !run.extended) {
            run.extended = true;
        } else if (type === 'COREBEGIN' && run.extended && !coreMode && !run.coremark[name] &&
                   Object.hasOwn(coremarkSeeds, name) && fields.length === 0) {
            coreMode = name;
            coreLines = [];
        } else if (type === 'CORELOG' && coreMode) {
            coreLines.push(line.slice('CORELOG,'.length).trim());
        } else if (type === 'COREEND' && coreMode === name && fields.length === 0) {
            run.coremark[name] = parseCoremark(coreLines, name);
            coreMode = undefined;
        } else if (type === 'ERROR' && fields.length === 2) {
            const message = fields.join(',');
            run.errors[name] = run.errors[name] ? run.errors[name] + ' | ' + message : message;
        } else if (type === 'ALG' && (algorithms.includes(name) ||
                   (run.extended && extendedAlgorithms.includes(name))) && fields.length === 3) {
            const [index, cycles, calls] = fields.map(number);
            if (calls !== 32 || cycles === 0) throw new Error('Invalid batch for ' + name);
            append(run.algorithms, name, index, cycles / calls);
        } else if (type === 'IRQ' && Object.hasOwn(interrupts, name) && fields.length === 5) {
            const [index, latency, body, stamp, active] = fields.map(number);
            if (latency === 0 || body === 0 || active > 1) throw new Error('Invalid ISR sample for ' + name);
            append(run.irq, name, index, { latency, body, stamp, active });
        } else if (type === 'STATUS' && Object.hasOwn(interrupts, name) && fields.length === 3) {
            const [count, missed, active] = fields.map(number);
            if (run.status[name]) throw new Error('Duplicate status for ' + name);
            run.status[name] = { count, missed, active };
        } else {
            throw new Error('Unrecognized firmware line ' + line);
        }
    }
    if (coreMode) throw new Error('Missing COREEND for ' + coreMode);
    if (!run.errors.board && (coreHz !== '168000000' || timerHz !== '84000000')) {
        throw new Error('Unexpected board clocks');
    }
    return run;
}

function analyzeIrq(run, name) {
    const samples = run.irq[name] ?? [];
    const status = run.status[name];
    const active = samples.reduce((sum, sample) => sum + sample.active, 0);
    const expected = interrupts[name];
    if (!status || status.count !== samples.length || status.active !== active) {
        return { error: run.errors[name] ?? 'Missing or inconsistent sample status' };
    }
    const jitter = [];
    let skipped = 0;
    let shortIntervals = 0;
    if (name !== 'dma') {
        for (let i = 1; i < samples.length; i++) {
            const interval = (samples[i].stamp - samples[i - 1].stamp) >>> 0;
            skipped += Math.max(0, Math.round(interval / periodCycles) - 1);
            if (interval < periodCycles / 2) shortIntervals++;
            jitter.push(Math.abs(interval - periodCycles));
        }
    }
    const missed = Math.max(status.missed, skipped);
    const shortage = Math.max(0, expected - samples.length);
    const error = run.errors[name] ??
        (samples.length !== expected ? 'Incomplete sample set' :
            shortIntervals ? 'Unexpected extra or short IRQ intervals' :
            name === 'tim2_dma' && !active ? 'No sampled DMA activity' : undefined);
    return { samples, jitter, active, missed, shortage, shortIntervals, error };
}

function validateRun(run) {
    if (run.errors.board) return ['board ' + run.errors.board];
    const failures = [];
    const expected = run.extended ? algorithms.concat(extendedAlgorithms) : algorithms;
    for (const name of expected) {
        const error = run.errors[name] ?? (run.algorithms[name]?.length !== 15 ? 'Incomplete sample set' : null);
        if (error) failures.push(name + ' ' + error);
    }
    if (run.extended) {
        for (const mode of Object.keys(coremarkSeeds)) {
            const result = run.coremark[mode];
            if (!result || result.error) failures.push('coremark ' + mode + ' ' + (result?.error ?? 'Missing result'));
        }
    }
    for (const name of Object.keys(interrupts)) {
        const { error } = analyzeIrq(run, name);
        if (error) failures.push(name + ' ' + error);
    }
    return failures;
}

function memory(image, dir) {
    const elf = fs.readFileSync(path.join(dir, image + '.elf'));
    const binary = fs.readFileSync(path.join(dir, image + '.bin'));
    if (elf.toString('hex', 0, 6) !== '7f454c460101') throw new Error('Expected ELF32 LE');
    const word = at => elf.readUInt32LE(at);
    const table = word(32);
    const sectionSize = elf.readUInt16LE(46);
    const sectionCount = elf.readUInt16LE(48);
    const sections = Array.from({ length: sectionCount }, (_, i) => {
        const at = table + i * sectionSize;
        return {
            type: word(at + 4), flags: word(at + 8), address: word(at + 12),
            offset: word(at + 16), size: word(at + 20), link: word(at + 24), entrySize: word(at + 36)
        };
    });
    const ramBase = 0x20000000;
    const ramEnd = 0x2001c000;
    const symbols = {};
    for (const section of sections.filter(section => section.type === 2)) {
        const strings = sections[section.link].offset;
        for (let at = section.offset; at < section.offset + section.size; at += section.entrySize) {
            const start = strings + word(at);
            const name = elf.toString('ascii', start, elf.indexOf(0, start));
            const sectionIndex = elf.readUInt16LE(at + 14);
            if (name && sectionIndex !== 0) symbols[name] = { address: word(at + 4), size: word(at + 8) };
        }
    }
    const stackTop = image.startsWith('gcc') ? symbols._estack?.address : symbols.__initial_sp?.address;
    const stackStart = image.startsWith('gcc') ? stackTop - 1024 : symbols.STACK?.address;
    if (stackTop - stackStart !== 1024 || stackTop > ramEnd || stackStart < ramBase ||
        binary.readUInt32LE(0) !== stackTop) throw new Error('Unexpected stack layout in ' + image);
    const reset = binary.readUInt32LE(4);
    if (!(reset & 1) || reset < 0x08000000 || reset >= 0x08000000 + binary.length) {
        throw new Error('Invalid Thumb reset vector in ' + image);
    }
    for (const [irq, name] of [[10, 'EXTI4_IRQHandler'], [28, 'TIM2_IRQHandler'], [56, 'DMA2_Stream0_IRQHandler']]) {
        const vector = binary.readUInt32LE(4 * (16 + irq));
        if (!symbols[name] || vector !== (symbols[name].address | 1)) {
            throw new Error('Invalid interrupt vector for ' + name + ' in ' + image);
        }
    }
    for (const name of ['dma_source', 'dma_target', 'irq_samples']) {
        const object = symbols[name];
        if (!object || object.size !== 16384 || object.address < ramBase || object.address + object.size > ramEnd) {
            throw new Error('Invalid SRAM1 placement for ' + name + ' in ' + image);
        }
    }
    const ram = sections.filter(section => (section.flags & 2) && section.address >= ramBase &&
                                          section.address < ramEnd && section.size);
    return {
        flash: binary.length,
        staticRam: Math.max(...ram.map(section => section.address + section.size)) - ramBase,
        stack: 1024,
        dmaBuffers: symbols.dma_source.size + symbols.dma_target.size,
        sampleBuffer: symbols.irq_samples.size
    };
}

function show(stats, hz) {
    const digits = hz ? value => (value / (hz / 1e6)).toFixed(3) : value => value.toFixed(2);
    return [stats.min, stats.median, stats.p99, stats.max].map(digits).join(' / ');
}

function generate(root, runDir, expectedRuns) {
    const imageNames = {
        'ac6-speed': 'AC6 速度档',
        'gcc-speed': 'GCC 速度档',
        'ac6-o3': 'AC6 -O3',
        'gcc-o3': 'GCC -O3',
        'ac6-o2lto': 'AC6 -O2+flto',
        'gcc-o2lto': 'GCC -O2+flto',
        'ac6-o0': 'AC6 -O0',
        'gcc-o0': 'GCC -O0',
        'ac6-size': 'AC6 体积档',
        'gcc-size': 'GCC 体积档'
    };
    const caseNames = {
        crc32: 'CRC-32', fir_i16: '整数 FIR', fir_f32: '单精度 FIR',
        mul32: '32 位乘加', mul64: '32×32→64 位乘加',
        udivmod32: '无符号求商和余数', sdiv32: '有符号除法',
        gcd32: '欧几里得 GCD', matmul_i16: '16×16 整数矩阵乘法',
        tim2: 'TIM2 中断', exti: 'EXTI4 回环', dma: 'DMA 传输与完成中断',
        tim2_dma: 'DMA 负载下的 TIM2 中断'
    };
    const lines = [
        '# STM32F407IGT6 AC6 与 GCC 上板对比',
        '',
        '## 固件占用',
        '',
        '| 构建配置 | Flash（字节） | 静态 SRAM1（字节） | 栈（字节） | DMA 缓冲区（字节） | 中断采样缓冲区（字节） |',
        '|---|---:|---:|---:|---:|---:|'
    ];
    for (const image of profiles) {
        const dir = path.join(root, 'build', image);
        if (!fs.existsSync(path.join(dir, image + '.elf'))) {
            lines.push('| ' + imageNames[image] + ' | 无构建产物 | | | | |');
            continue;
        }
        const size = memory(image, dir);
        lines.push('| ' + [imageNames[image], size.flash, size.staticRam, size.stack, size.dmaBuffers, size.sampleBuffer].join(' | ') + ' |');
    }
    lines.push('', 'Flash 按烧录 BIN 的长度统计。静态 SRAM1 按地址跨度统计，包含对齐、栈和两类缓冲区。',
        '', '## 算法耗时', '', '将有效轮次的样本合并统计。耗时按内核 168 MHz 换算，百分比为 GCC 相对同档 AC6 的耗时变化。',
        '', '| 构建配置 | 测试项目 | 有效轮次 | 最小值 / 中位数（周期） | 最小值 / 中位数（µs） | GCC 耗时变化 |',
        '|---|---|---:|---:|---:|---:|');
    const runs = {};
    const failures = [];
    const algoStats = {};
    for (const image of profiles) {
        runs[image] = [];
        const hashFile = path.join(runDir, image + '.sha256');
        if (fs.existsSync(hashFile)) {
            const bin = path.join(root, 'build', image, image + '.bin');
            const hash = fs.existsSync(bin) && createHash('sha256').update(fs.readFileSync(bin)).digest('hex');
            if (hash !== fs.readFileSync(hashFile, 'ascii').trim().toLowerCase()) {
                    failures.push(imageNames[image] + ' 的采集固件哈希与当前构建不一致');
                algoStats[image] = {};
                continue;
            }
        }
        for (let n = 1; n <= 3; n++) {
            const file = path.join(runDir, image + '-' + n + '.csv');
            if (!fs.existsSync(file)) continue;
            try {
                const run = parseLog(fs.readFileSync(file, 'utf8'), image);
                if (run.errors.board) throw new Error(run.errors.board);
                runs[image].push({ n, run });
            } catch (error) {
                failures.push(image + '-' + n + ' ' + error.message);
            }
        }
        algoStats[image] = {};
        for (const name of algorithms.concat(extendedAlgorithms)) {
            const eligible = extendedAlgorithms.includes(name) ? runs[image].filter(({ run }) => run.extended) : runs[image];
            const valid = eligible.filter(({ n, run }) => {
                if (!run.errors[name] && run.algorithms[name]?.length === 15) return true;
                failures.push(image + '-' + n + ' ' + caseNames[name] + ' ' + (run.errors[name] ?? '样本不完整'));
                return false;
            });
            if (valid.length) {
                algoStats[image][name] = {
                    count: valid.length, stats: summarize(valid.flatMap(({ run }) => run.algorithms[name]))
                };
            }
        }
    }
    for (const image of profiles) {
        for (const [name, { count, stats }] of Object.entries(algoStats[image])) {
            const baseline = image.startsWith('gcc') ? algoStats[image.replace('gcc', 'ac6')][name] : null;
            const relative = baseline ? ((stats.median / baseline.stats.median - 1) * 100).toFixed(2) + '%' : '—';
            lines.push('| ' + [imageNames[image], caseNames[name], count,
                stats.min.toFixed(2) + ' / ' + stats.median.toFixed(2),
                (stats.min / 168).toFixed(3) + ' / ' + (stats.median / 168).toFixed(3), relative].join(' | ') + ' |');
        }
    }
    lines.push('', '## 外设实时性', '',
        '时间列依次列出最小值、中位数、P99 和实测最大值。P99 采用最近秩法。抖动是相邻中断采样间隔相对 100 µs 的绝对偏差。',
        '', '| 构建配置 | 测试项目 | 有效轮次 | 响应或总耗时（µs） | ISR 处理耗时（µs） | 抖动（µs） | 漏触发次数 | 缺少样本数 | DMA 活动样本数 |',
        '|---|---|---:|---:|---:|---:|---:|---:|---:|');
    for (const image of profiles) {
        for (const name of Object.keys(interrupts)) {
            const checked = runs[image].map(({ n, run }) => {
                const result = analyzeIrq(run, name);
                if (result.error) failures.push(image + '-' + n + ' ' + name + ' ' + result.error);
                return result;
            });
            if (!checked.length) continue;
            const valid = checked.filter(result => !result.error);
            const samples = valid.flatMap(result => result.samples);
            const jitter = valid.flatMap(result => result.jitter);
            const total = field => checked.reduce((sum, result) => sum + (result[field] ?? 0), 0);
            lines.push('| ' + [imageNames[image], caseNames[name], valid.length,
                samples.length ? show(summarize(samples.map(s => s.latency)), name === 'dma' ? 168e6 : 84e6) : '—',
                samples.length ? show(summarize(samples.map(s => s.body)), 168e6) : '—',
                jitter.length ? show(summarize(jitter), 168e6) : '—',
                total('missed'), total('shortage'), total('active')].join(' | ') + ' |');
        }
    }
    lines.push('', 'DMA 总耗时从软件启动传输计到完成中断首次采样。漏触发由中断间隔和比较事件跳过次数推算。缺少样本数用于记录提前结束的采集。');
    lines.push('', '## EEMBC CoreMark 1.0', '',
        '单线程，数据集 2,000 字节。性能和校验种子各迭代 10,000 次，均须通过 CRC 校验并运行至少 10 秒。得分按 TIM5 的 1 MHz 计数换算。',
        '', '| 构建配置 | 有效轮次 | 得分中位数（迭代/秒） | 性能运行耗时中位数（秒） | 校验运行耗时中位数（秒） |',
        '|---|---:|---:|---:|---:|');
    for (const image of profiles) {
        const valid = runs[image].filter(({ n, run }) => {
            if (!run.extended) return false;
            const modes = Object.keys(coremarkSeeds);
            const errors = modes.filter(mode => !run.coremark[mode] || run.coremark[mode].error);
            for (const mode of errors) failures.push(image + '-' + n + ' CoreMark ' +
                (mode === 'performance' ? '性能运行' : '校验运行') + ' ' +
                (run.coremark[mode]?.error ?? '缺少结果'));
            return !errors.length;
        });
        if (!valid.length) continue;
        const perf = valid.map(({ run }) => run.coremark.performance);
        const val = valid.map(({ run }) => run.coremark.validation);
        lines.push('| ' + [imageNames[image], valid.length, summarize(perf.map(row => row.score)).median.toFixed(2),
            (summarize(perf.map(row => row.ticks)).median / 1e6).toFixed(3),
            (summarize(val.map(row => row.ticks)).median / 1e6).toFixed(3)].join(' | ') + ' |');
    }
    lines.push('', '## 硬件整数乘除指令', '',
        '| 构建配置 | 32 位乘加 | 32×32→64 位乘加 | 无符号求商和余数 | 有符号除法 | GCD | 矩阵乘法 |',
        '|---|---|---|---|---|---|---|');
    for (const image of profiles) {
        const file = path.join(root, 'build', image, 'isa.json');
        if (!fs.existsSync(file)) continue;
        const opcodes = JSON.parse(fs.readFileSync(file, 'utf8'));
        const formatOpcodes = instructions => [...new Set(instructions)].map(opcode => {
            const count = instructions.filter(instruction => instruction === opcode).length;
            return count === 1 ? opcode : opcode + ' ×' + count;
        }).join(', ');
        lines.push('| ' + [imageNames[image], ...extendedAlgorithms.map(name => formatOpcodes(opcodes[name + '_kernel']))].join(' | ') + ' |');
    }
    lines.push('', '指令数量按函数反汇编中的静态出现次数统计。');
    lines.push('', '## 采集状态', '');
    if (fs.existsSync(runDir)) {
        for (const name of fs.readdirSync(runDir).filter(name => name.endsWith('.error.txt')).sort()) {
            failures.push(name + ' ' + fs.readFileSync(path.join(runDir, name), 'utf8').trim().replace(/\r?\n/g, ' '));
        }
    }
    for (const image of profiles) lines.push('- ' + imageNames[image] + ' 已读取 ' + runs[image].length + '/' + expectedRuns + ' 轮');
    for (const error of failures) lines.push('- 采集或校验异常，原始诊断信息为 ' + error);
    return lines.join('\n') + '\n';
}

if (require.main === module) {
    if (process.argv[2] === '--check-run') {
        try {
            const run = parseLog(fs.readFileSync(process.argv[3], 'utf8'), process.argv[4]);
            const failures = validateRun(run);
            if (failures.length) throw new Error(failures.join(' | '));
        } catch (error) {
            console.log(error.message);
            process.exitCode = 1;
        }
    } else {
        const root = path.resolve(__dirname, '..');
        const runDir = path.resolve(process.argv[2] ?? path.join(root, 'runs'));
        const expectedRuns = Number(process.argv[3] ?? 3);
        if (![1, 2, 3].includes(expectedRuns)) throw new Error('Expected run count must be 1, 2, or 3');
        const report = path.join(root, 'build', 'comparison.md');
        fs.mkdirSync(path.dirname(report), { recursive: true });
        fs.writeFileSync(report, generate(root, runDir, expectedRuns));
        console.log(report);
    }
}

module.exports = { summarize, parseLog, analyzeIrq, validateRun, memory };
