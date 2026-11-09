export default [
    {
        files: ['web/__combined__.js'],
        languageOptions: {
            globals: {
                document: 'readonly',
                localStorage: 'readonly',
                fetch: 'readonly',
                setTimeout: 'readonly',
                setInterval: 'readonly',
                Blob: 'readonly',
                Response: 'readonly',
                DecompressionStream: 'readonly',
                URLSearchParams: 'readonly',
            },
        },
        rules: {
            'no-undef': 'error',
            'no-unused-vars': [
                'error',
                { args: 'all', argsIgnorePattern: '^_', caughtErrors: 'all' },
            ],
            'no-unreachable': 'error',
            'no-constant-condition': 'error',
            'no-dupe-args': 'error',
            'valid-typeof': 'error',
            eqeqeq: ['error', 'always'],
        },
    },
    {
        files: ['web/*.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'script',
        },
        rules: {
            curly: ['error', 'all'],
            'padding-line-between-statements': [
                'error',
                { blankLine: 'always', prev: '*', next: ['if', 'for', 'while', 'switch', 'try'] },
                { blankLine: 'always', prev: ['if', 'for', 'while', 'switch', 'try'], next: '*' },
            ],
        },
    },
];
