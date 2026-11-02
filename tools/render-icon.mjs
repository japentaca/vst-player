// Rasterises the icon sources (resources/icon.svg, resources/icon-small.svg) to the PNGs that
// CMakeLists.txt passes to juce_add_gui_app (ICON_BIG / ICON_SMALL; JUCE builds the .ico).
// Uses Playwright + Chromium from another checkout (by default carrilion's tools/render), so
// vst-player keeps no Node dependencies. Only needed after editing the SVGs.
//
//   node tools/render-icon.mjs [--playwright <dir of the playwright package>] [--preview <png>]
//
// --preview also writes a sheet with the icon at 256, 48, 32 and 16 px on the app background.
import { createRequire } from 'node:module'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const argv = process.argv.slice(2)
const opt = (name, def) => {
  const i = argv.indexOf('--' + name)
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : def
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const res = path.join(root, 'resources')
const playwrightDir = opt('playwright', path.resolve(root, '..', 'carrilion', 'tools', 'render', 'node_modules', 'playwright'))
if (!fs.existsSync(playwrightDir)) {
  console.error(`No se encontro Playwright en ${playwrightDir} (usar --playwright <dir>)`)
  process.exit(2)
}
const { chromium } = createRequire(path.join(playwrightDir, 'package.json'))(playwrightDir)

const svgUrl = (file) => 'data:image/svg+xml;base64,' + fs.readFileSync(path.join(res, file)).toString('base64')
const outputs = [
  { svg: 'icon.svg', png: 'icon-256.png', size: 256 },
  { svg: 'icon-small.svg', png: 'icon-32.png', size: 32 },
]

const browser = await chromium.launch()
try {
  const page = await browser.newPage()
  for (const o of outputs) {
    await page.setViewportSize({ width: o.size, height: o.size })
    await page.setContent(`<html><body style="margin:0;background:transparent"><img src="${svgUrl(o.svg)}" width="${o.size}" height="${o.size}" style="display:block"></body></html>`)
    await page.waitForFunction(() => document.images[0].complete)
    await page.screenshot({ path: path.join(res, o.png), omitBackground: true })
    console.log(`${o.svg} -> resources/${o.png} (${o.size} px)`)
  }

  const preview = opt('preview')
  if (preview) {
    // 256 and 48 from icon.svg, 32 and 16 from icon-small.svg (the sizes JUCE puts in the .ico),
    // plus the 32 and 16 px versions enlarged 4x with nearest-neighbour to see the pixels.
    const png = (file) => 'data:image/png;base64,' + fs.readFileSync(path.join(res, file)).toString('base64')
    const html = `<html><body style="margin:0;background:#1a1c20;color:#9aa1ab;font:13px sans-serif">
      <div style="display:flex;gap:28px;align-items:flex-end;padding:24px">
        <figure style="margin:0;text-align:center"><img src="${png('icon-256.png')}" width="256"><figcaption>256</figcaption></figure>
        <figure style="margin:0;text-align:center"><img src="${svgUrl('icon.svg')}" width="48"><figcaption>48</figcaption></figure>
        <figure style="margin:0;text-align:center"><img src="${png('icon-32.png')}" width="32"><figcaption>32</figcaption></figure>
        <figure style="margin:0;text-align:center"><img src="${svgUrl('icon-small.svg')}" width="16"><figcaption>16</figcaption></figure>
        <figure style="margin:0;text-align:center"><img src="${png('icon-32.png')}" width="128" style="image-rendering:pixelated"><figcaption>32 (x4)</figcaption></figure>
        <figure style="margin:0;text-align:center"><canvas id="c16" width="64" height="64" style="width:64px;height:64px;image-rendering:pixelated"></canvas><figcaption>16 (x4)</figcaption></figure>
      </div>
      <script>
        const img = new Image(); img.src = ${JSON.stringify(png('icon-32.png'))};
        img.onload = () => { const t = document.createElement('canvas'); t.width = t.height = 16;
          const tc = t.getContext('2d'); tc.imageSmoothingQuality = 'high'; tc.drawImage(img, 0, 0, 16, 16);
          const c = document.getElementById('c16').getContext('2d'); c.imageSmoothingEnabled = false; c.drawImage(t, 0, 0, 64, 64);
          document.body.dataset.ready = '1' }
      </script></body></html>`
    await page.setViewportSize({ width: 720, height: 330 })
    await page.setContent(html)
    await page.waitForSelector('body[data-ready="1"]')
    await page.screenshot({ path: preview })
    console.log(`vista previa -> ${preview}`)
  }
} finally {
  await browser.close()
}
