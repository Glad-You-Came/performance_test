const fs = require('node:fs');

/* Modes:
   strict  - kernel functions must contain a required hardware instruction and
             must not reference any software division helper (default).
   baseline -O0 tier: helpers are allowed and recorded instead of rejected.
   global  - LTO tier: kernels may be fully inlined, so require the instruction
             somewhere in the whole image and reject helpers image-wide. */

const helperPattern = /\b(?:__aeabi_(?:u?ldivmod|u?idiv(?:mod)?)|__rt_[us]?div\w*)\b/gi;
const mnemonicPattern = /^\s*(?:0x)?[0-9a-f]+:\s+(?:[0-9a-f]{4,8}\s+){1,2}(?:[^\s]+\s{2,})?([a-z][a-z0-9]*)(?:\.\w+)?(?:\s|$)/i;
const keepPattern = /^(?:[us]div|mul\w*|[us]mul\w*|mla|mls|[us]mla\w*)$/;

function collect(disassembly) {
    const mnemonics = [];
    const helpers = [];
    for (const line of disassembly.split('\n')) {
        const instruction = line.match(mnemonicPattern);
        if (instruction) mnemonics.push(instruction[1].toLowerCase());
        for (const helper of line.matchAll(helperPattern)) {
            helpers.push(helper[0].toLowerCase());
        }
    }
    return { mnemonics, helpers: [...new Set(helpers)] };
}

function verifyKernel(name, required, disassembly, mode = 'strict') {
    const lines = disassembly.split(/\r?\n/);
    const heading = new RegExp('^(?:[0-9a-f]+ <' + name + '>:|    ' + name + ')$');
    const start = lines.findIndex(line => heading.test(line.trimEnd()));
    if (mode === 'global') {
        const whole = collect(disassembly);
        if (whole.helpers.length) {
            throw new Error(name + ': image links a software division helper: ' + whole.helpers.join(', '));
        }
        if (!required.some(mnemonic => whole.mnemonics.includes(mnemonic))) {
            throw new Error(name + ' is missing a hardware instruction: ' + required.join(' or '));
        }
        return [...new Set(whole.mnemonics.filter(name => keepPattern.test(name)))];
    }
    if (start < 0) throw new Error('Missing disassembly for ' + name);
    const next = /^(?:[0-9a-f]+ <[^>]+>:|    [\w.]+\s*)$/i;
    const end = lines.findIndex((line, i) => i > start && next.test(line.trimEnd()));
    const body = lines.slice(start + 1, end < 0 ? undefined : end).join('\n');
    const { mnemonics, helpers } = collect(body);
    if (mode === 'strict' && helpers.length) {
        throw new Error(name + ' calls a software division helper');
    }
    if (mode !== 'baseline' && !required.some(name => mnemonics.includes(name))) {
        throw new Error(name + ' is missing a hardware instruction: ' + required.join(' or '));
    }
    return [...mnemonics.filter(name => keepPattern.test(name)), ...helpers];
}

const kernels = {
    mul32_kernel: ['mul', 'muls', 'mla'],
    mul64_kernel: ['umull', 'umlal'],
    udivmod32_kernel: ['udiv'],
    sdiv32_kernel: ['sdiv'],
    gcd32_kernel: ['udiv'],
    matmul_i16_kernel: ['mul', 'muls', 'mla', 'smlabb', 'smlad', 'smulbb']
};

if (require.main === module) {
    const mode = process.argv[4] ?? 'strict';
    if (!['strict', 'baseline', 'global'].includes(mode)) {
        throw new Error('Unknown verify-isa mode ' + mode);
    }
    const disassembly = fs.readFileSync(process.argv[2], 'utf8');
    const result = Object.fromEntries(Object.entries(kernels).map(([name, allowed]) =>
        [name, verifyKernel(name, allowed, disassembly, mode)]));
    fs.writeFileSync(process.argv[3], JSON.stringify(result, null, 2) + '\n');
}

module.exports = { verifyKernel };
