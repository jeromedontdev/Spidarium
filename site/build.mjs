import { build } from 'esbuild';
import { cp, mkdir, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

await rm(new URL('./dist/', import.meta.url), { recursive: true, force: true });
await mkdir(new URL('./dist/site/vendor/', import.meta.url), { recursive: true });
for (const path of ['index.html', 'style.css', 'app.js', 'lib', 'firmware', 'save-schema.json', 'README.md']) {
  try {
    await cp(new URL(path, import.meta.url), new URL(`dist/site/${path}`, import.meta.url), { recursive: true });
  } catch (error) {
    if (error.code !== 'ENOENT' || !['save-schema.json', 'firmware'].includes(path)) throw error;
    console.warn(`${path} is not yet supplied by release tooling; device operations will reject missing resources.`);
  }
}
await build({
  entryPoints: [fileURLToPath(new URL('./lib/esptool-entry.js', import.meta.url))],
  outfile: fileURLToPath(new URL('./dist/site/vendor/esptool.js', import.meta.url)),
  bundle: true, format: 'esm', platform: 'browser', target: 'es2022', minify: true,
  legalComments: 'eof',
});
for (const [name, license] of [['esptool-js', 'LICENSE'], ['js-md5', 'LICENSE.txt'], ['pako', 'LICENSE'], ['atob-lite', 'LICENSE.md']]) {
  await cp(new URL(`./node_modules/${name}/${license}`, import.meta.url), new URL(`dist/site/vendor/${name}-LICENSE.txt`, import.meta.url));
}
console.log('Built site/dist/site (local esptool-js bundle; no CDN). Release tooling must copy public assets to site/dist/assets.');
