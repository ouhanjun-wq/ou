// Render film.html frame-by-frame with headless Chromium and encode with ffmpeg.
//
//   node tools/render.mjs --stills 1,4.2,9.5      -> build/stills/t_XX.XXX.png
//   node tools/render.mjs --video [--workers 4]   -> build/video.mp4 (no audio)
//
// Every frame is a pure function of time, so workers can take disjoint ranges.
import { createRequire } from 'node:module';
import { execSync, spawn } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import url from 'node:url';

const require = createRequire(import.meta.url);
function load(name) {
  try { return require(name); } catch { return require(path.join(execSync('npm root -g').toString().trim(), name)); }
}
const { chromium } = load('playwright');

const ROOT = path.resolve(path.dirname(url.fileURLToPath(import.meta.url)), '..');
const BUILD = path.join(ROOT, 'build');
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const FPS = 60, DURATION = 30;
const FFMPEG = process.env.FFMPEG || execSync(`python3 -c "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())"`).toString().trim();

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2', '.woff': 'font/woff', '.ttf': 'font/ttf', '.m4a': 'audio/mp4', '.wav': 'audio/wav' };
function serve() {
  const srv = http.createServer((req, res) => {
    const p = path.join(ROOT, decodeURIComponent(new URL(req.url, 'http://x').pathname));
    if (!p.startsWith(ROOT) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream' });
    fs.createReadStream(p).pipe(res);
  });
  return new Promise(r => srv.listen(0, '127.0.0.1', () => r(srv)));
}

async function openPage(browser, port) {
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
  page.on('pageerror', e => console.error('[page error]', e.message));
  page.on('console', m => { if (m.type() === 'error') console.error('[console]', m.text()); });
  await page.goto(`http://127.0.0.1:${port}/film.html?render=1`);
  await page.waitForFunction(() => window.__ready === true, null, { timeout: 120000 });
  return page;
}

async function frame(page, t) {
  await page.evaluate(t => window.renderAt(t), t);
  return page.screenshot({ type: 'png', clip: { x: 0, y: 0, width: 1920, height: 1080 } });
}

const srv = await serve();
const port = srv.address().port;
const launch = () => chromium.launch({ args: ['--font-render-hinting=none', '--disable-lcd-text', '--force-color-profile=srgb', '--hide-scrollbars'] });
const browser = await launch();
fs.mkdirSync(BUILD, { recursive: true });

if (args.includes('--stills')) {
  const times = opt('--stills').split(',').map(Number);
  const dir = path.join(BUILD, 'stills');
  fs.mkdirSync(dir, { recursive: true });
  const page = await openPage(browser, port);
  for (const t of times) {
    const t0 = Date.now();
    const png = await frame(page, t);
    const f = path.join(dir, `t_${t.toFixed(3).padStart(6, '0')}.png`);
    fs.writeFileSync(f, png);
    console.log(f, `${Date.now() - t0}ms`);
  }
} else if (args.includes('--video')) {
  const workers = Number(opt('--workers', 4));
  const total = Math.round(FPS * DURATION);
  const from = Number(opt('--from', 0)), to = Number(opt('--to', total));
  const per = Math.ceil((to - from) / workers);
  const started = Date.now();
  let done = 0;
  const segs = [];
  await Promise.all(Array.from({ length: workers }, async (_, w) => {
    const a = from + w * per, b = Math.min(to, a + per);
    if (a >= b) return;
    const out = path.join(BUILD, `seg_${w}.mkv`);
    segs[w] = out;
    const ff = spawn(FFMPEG, ['-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'png', '-i', '-',
      '-c:v', 'libx264', '-preset', 'medium', '-crf', '8', '-pix_fmt', 'yuv444p', out], { stdio: ['pipe', 'inherit', 'inherit'] });
    // one browser per worker: a shared browser serialises compositing and screenshots
    const own = w === 0 ? browser : await launch();
    const page = await openPage(own, port);
    for (let f = a; f < b; f++) {
      const png = await frame(page, f / FPS);
      if (!ff.stdin.write(png)) await new Promise(r => ff.stdin.once('drain', r));
      done++;
      if (done % 60 === 0) {
        const el = (Date.now() - started) / 1000;
        console.log(`${done}/${to - from} frames  ${el.toFixed(0)}s elapsed  ~${((el / done) * (to - from - done)).toFixed(0)}s left`);
      }
    }
    ff.stdin.end();
    await new Promise(r => ff.on('close', r));
    await page.close();
    if (own !== browser) await own.close();
  }));
  const list = path.join(BUILD, 'segs.txt');
  fs.writeFileSync(list, segs.filter(Boolean).map(s => `file '${s}'`).join('\n'));
  execSync(`"${FFMPEG}" -y -loglevel error -f concat -safe 0 -i "${list}" -c copy "${path.join(BUILD, 'video.mkv')}"`);
  console.log('video ->', path.join(BUILD, 'video.mkv'), `${((Date.now() - started) / 1000).toFixed(0)}s`);
}
await browser.close();
srv.close();
