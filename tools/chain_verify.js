#!/usr/bin/env node
const fs = require('fs');
const path = require('path');

const chainPath = path.join(process.cwd(), 'kolibri_chain.jsonl');

if (!fs.existsSync(chainPath)) {
  console.error('chain file not found:', chainPath);
  process.exit(1);
}

const mix64 = (xBig) => {
  let x = BigInt.asUintN(64, xBig);
  x ^= x >> 33n;
  x = BigInt.asUintN(64, x * 0xff51afd7ed558ccdn);
  x ^= x >> 33n;
  x = BigInt.asUintN(64, x * 0xc4ceb9fe1a85ec53n);
  x ^= x >> 33n;
  return BigInt.asUintN(64, x);
};

const toBytes = (hex) => {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i++) {
    bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
  }
  return bytes;
};

const fromBytes = (bytes) => {
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
};

const lines = fs
  .readFileSync(chainPath, 'utf-8')
  .split(/\r?\n/)
  .filter(Boolean);

let prev = new Uint8Array(32);
let ok = true;

for (const line of lines) {
  let block;
  try {
    block = JSON.parse(line);
  } catch (err) {
    console.error('invalid json:', err.message);
    ok = false;
    break;
  }
  const hashHex = block.hash;
  const prevHex = block.prev;
  const formula = (block.formula || '').padEnd(256, '\0');
  let state = 0x9e3779b97f4a7c15n;
  state ^= mix64(BigInt(block.step) + (BigInt(block.digit) << 32n));
  state ^= mix64(BigInt(block.ts));
  state ^= mix64(BigInt(Math.round(block.eff * 1e6)));
  state ^= mix64(BigInt(Math.round(block.compl * 1e6)));
  for (let i = 0; i < formula.length; i++) {
    state ^= mix64(BigInt(formula.charCodeAt(i) + i));
  }
  const prevBytes = toBytes(prevHex);
  for (let i = 0; i < 32; i++) {
    state ^= mix64(BigInt(prevBytes[i] + i * 131));
    const mixed = mix64(state + BigInt(i) * 0x12345n);
    prev[i] = Number(mixed & 0xffn);
  }
  const expectedHex = fromBytes(prev);
  if (expectedHex !== hashHex) {
    console.error('hash mismatch at step', block.step);
    ok = false;
    break;
  }
}

if (!ok) {
  process.exit(2);
}
console.log('chain ok');
