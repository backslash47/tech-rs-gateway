import assert from 'node:assert/strict';
import test from 'node:test';
import { ESLint } from 'eslint';

test('semantic lint rejects unknown and unused variables', async () => {
    const eslint = new ESLint();
    const [result] = await eslint.lintText('const unused = 1;\nmissingFunction();\n', {
        filePath: 'web/__combined__.js',
    });
    const rules = result.messages.map((message) => message.ruleId);
    assert.ok(rules.includes('no-undef'));
    assert.ok(rules.includes('no-unused-vars'));
});

test('shared helpers and browser globals are valid', async () => {
    const eslint = new ESLint();
    const [result] = await eslint.lintText(
        'function helper() { return new URLSearchParams(); }\nhelper();\ndocument.getElementById("menu");\n',
        { filePath: 'web/__combined__.js' },
    );
    assert.equal(result.errorCount, 0);
});
