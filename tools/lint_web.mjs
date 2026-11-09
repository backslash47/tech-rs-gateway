// Check the browser scripts in their actual shared scope and HTML loading order.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { ESLint } from 'eslint';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const html = readFileSync(path.join(root, 'web/index.html'), 'utf8');
const scripts = [...html.matchAll(/<script src="([^"]+)"><\/script>/g)].map((match) => match[1]);
const generated = {
    'locales.js': 'const UI_LOCALES = {};',
    'build-info.js': 'const BUILD_INFO = {};',
};
const source = scripts
    .map((name) => generated[name] ?? readFileSync(path.join(root, 'web', name), 'utf8'))
    .join('\n');
const eslint = new ESLint({ cwd: root });
const results = await eslint.lintText(source, { filePath: path.join(root, 'web/__combined__.js') });
const formatter = await eslint.loadFormatter('stylish');
const report = formatter.format(results);
if (report) {
    console.log(report);
}
process.exitCode = results.some((result) => result.errorCount || result.warningCount) ? 1 : 0;
