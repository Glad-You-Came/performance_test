const fs = require('node:fs');

function verifyKernel(name, required, disassembly) {
    const lines = disassembly.split(/\r?\n/);
    const heading = new RegExp('^(?:[0-9a-f]+ <' + name + '>:|    ' + name + ')$');
    const start = lines.findIndex(line => heading.test(line.trimEnd()));
    if (start < 0) throw new Error('Missing disassembly for ' + name);
    const next = /^(?:[0-9a-f]+ <[^>]+>:|    [\w.]+\s*)$/i;
    const end = lines.findIndex((line, i) => i > start && next.test(line.trimEnd()));
    const body = lines.slice(start + 1, end < 0 ? undefined : end).join('\n');
    if (/\b(?:__aeabi_(?:u?ldivmod|u?idiv(?:mod)?)|__rt_[us]?div\w*)\b/i.test(body)) {
        throw new Error(name + ' calls a software division helper');
    }
    const mnemonics = [];
    for (const line of body.split('\n')) {
        const instruction = line.match(/^\s*(?:0x)?[0-9a-f]+:\s+(?:[0-9a-f]{4,8}\s+){1,2}(?:[^\s]+\s{2,})?([a-z][a-z0-9]*)(?:\.\w+)?(?:\s|$)/i);
        if (instruction) mnemonics.push(instruction[1].toLowerCase());
    }
    if (!required.some(name => mnemonics.includes(name))) {
        throw new Error(name + ' is missing a hardware instruction: ' + required.join(' or '));
    }
    return [...mnemonics].filter(name => /^(?:[us]div|mul\w*|[us]mul\w*|mla|mls|[us]mla\w*)$/.test(name));
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
    const disassembly = fs.readFileSync(process.argv[2], 'utf8');
    const result = Object.fromEntries(Object.entries(kernels).map(([name, allowed]) =>
        [name, verifyKernel(name, allowed, disassembly)]));
    fs.writeFileSync(process.argv[3], JSON.stringify(result, null, 2) + '\n');
}

module.exports = { verifyKernel };
