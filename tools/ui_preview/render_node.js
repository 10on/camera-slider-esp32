#!/usr/bin/env node

// Browser-free renderer for index.html. The preview only needs fillRect and a tiny DOM;
// keeping those here makes the 1:1 documentation PNGs reproducible in CI and by agents.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const zlib = require('zlib');

const ROOT = path.resolve(__dirname, '../..');
const OUT = path.join(ROOT, 'docs/ui');

const crcTable = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
  crcTable[n] = c >>> 0;
}

function crc32(data) {
  let c = 0xFFFFFFFF;
  for (const byte of data) c = crcTable[(c ^ byte) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

function chunk(type, data) {
  const name = Buffer.from(type, 'ascii');
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([name, data])));
  return Buffer.concat([len, name, data, crc]);
}

function encodePng(width, height, rgba) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) {
    const dst = y * (width * 4 + 1);
    raw[dst] = 0;
    rgba.copy(raw, dst + 1, y * width * 4, (y + 1) * width * 4);
  }
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

function parseColor(css) {
  const m = /rgb\((\d+),(\d+),(\d+)\)/.exec(css);
  if (!m) throw new Error(`Unsupported fillStyle: ${css}`);
  return [Number(m[1]), Number(m[2]), Number(m[3]), 255];
}

class PixelContext {
  constructor(canvas) { this.canvas = canvas; this.fillStyle = 'rgb(0,0,0)'; }
  setTransform() {}
  clearRect(x, y, w, h) { this.paint(x, y, w, h, [0, 0, 0, 0]); }
  fillRect(x, y, w, h) { this.paint(x, y, w, h, parseColor(this.fillStyle)); }
  paint(x, y, w, h, color) {
    const x0 = Math.max(0, Math.floor(x)), y0 = Math.max(0, Math.floor(y));
    const x1 = Math.min(this.canvas.width, Math.ceil(x + w));
    const y1 = Math.min(this.canvas.height, Math.ceil(y + h));
    for (let py = y0; py < y1; py++) for (let px = x0; px < x1; px++) {
      const i = (py * this.canvas.width + px) * 4;
      this.canvas.pixels[i] = color[0]; this.canvas.pixels[i + 1] = color[1];
      this.canvas.pixels[i + 2] = color[2]; this.canvas.pixels[i + 3] = color[3];
    }
  }
}

class Element {
  constructor(tag) { this.tagName = tag; this.style = {}; this.children = []; this.textContent = ''; }
  append(...items) { this.children.push(...items); }
  appendChild(item) { this.children.push(item); return item; }
}

class Canvas extends Element {
  constructor() { super('canvas'); this._width = 300; this._height = 150; this.reset(); }
  reset() { this.pixels = Buffer.alloc(this._width * this._height * 4); this.ctx = new PixelContext(this); }
  set width(v) { this._width = v; this.reset(); } get width() { return this._width; }
  set height(v) { this._height = v; this.reset(); } get height() { return this._height; }
  getContext(kind) { if (kind !== '2d') throw new Error(`Unsupported context: ${kind}`); return this.ctx; }
  toDataURL() { return `data:image/png;base64,${encodePng(this.width, this.height, this.pixels).toString('base64')}`; }
}

const outElement = new Element('div');
const document = {
  title: '',
  getElementById(id) { return id === 'out' ? outElement : null; },
  createElement(tag) { return tag === 'canvas' ? new Canvas() : new Element(tag); },
};

const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const scripts = [...html.matchAll(/<script(?: [^>]*)?>([\s\S]*?)<\/script>/g)]
  .map(match => match[1]).filter(code => code.trim());
const assets = fs.readFileSync(path.join(__dirname, 'assets.js'), 'utf8');
const context = { document, console };
context.window = context;
vm.runInNewContext(`${assets}\n${scripts.join('\n')}`, context, { filename: 'ui-preview.js' });

fs.mkdirSync(OUT, { recursive: true });
const exported = context.__EXPORTED__;
for (const [name, dataUrl] of Object.entries(exported)) {
  fs.writeFileSync(path.join(OUT, `${name}.png`), Buffer.from(dataUrl.split(',')[1], 'base64'));
}
console.log(`Rendered ${Object.keys(exported).length} screenshots to ${OUT}`);
