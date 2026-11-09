# Development style

Use English for identifiers, comments, log messages and backend status text.
User-facing translations belong in `web/locales/*.json`. Controller description
and option labels come from `vendor/emodul/*.json`, not handwritten translations
in the MQTT client.

## Readability

- Use braces for every C++ and JavaScript `if`, `else`, `for` and `while` body.
- Put each statement on its own line; avoid compressed chains of operations.
- Separate control-flow blocks from surrounding statements with blank lines.
- Use four-space indentation and spaces around operators and after keywords.
- Keep HTML, CSS, JavaScript and translations in separate source files.
- Generated firmware assets are build output, not files to edit manually.

Node.js 22 or newer is required for the web tools. Install the pinned development
dependencies from `requirements-dev.txt` and run `npm ci`.

```sh
python tools/format_cpp.py
npm run format:web
ruff format tools tests
```

Before submitting changes:

```sh
python tools/format_cpp.py --check
npm run check:web-style
ruff format --check tools tests
ruff check tools tests
npm run test:web-lint
```

The C++ formatter combines `.clang-format` (including inserted braces) with
blank-line rules. JavaScript uses ESLint's mandatory-brace and block-spacing
rules plus Prettier. Python uses Ruff. Functional checks are in [development](docs/development.md).

`check:web-style` also runs semantic ESLint rules against scripts combined in
their HTML loading order. This checks shared browser helpers without suppressing
undefined/unused-variable errors through a per-file global allowlist.

## API documentation

Use Doxygen comments in public C++ headers. Prefer `/** ... */` for contracts
and `///` for short descriptions; use `///<` for field units or ownership notes.
Document the class purpose and the public operations that callers need to use.

- Explain parameter units, valid ranges, ownership and result lifetimes.
- Distinguish queued, transmitted, acknowledged and confirmed operations.
- State thread/locking requirements where relevant, including whether a method
  acquires a mutex internally or expects the caller to hold it.
- Use `@brief`, `@param`, `@return`, `@pre` and `@note` where they clarify the contract.
- Put the contract on the declaration, not a duplicate comment on its definition.
- Keep implementation comments focused on protocol quirks, invariants and reasons
  for a non-obvious choice. Do not narrate obvious statements or add empty tags.

For example:

```cpp
/**
 * @brief Queues a setting change for an initialized session.
 * @param item Controller menu item ID.
 * @param value Requested integer in protocol units.
 * @param now Current monotonic time in milliseconds.
 * @return true if queued; this is not confirmation of controller readback.
 */
bool queue_menu(int item, int value, uint32_t now);
```

Use JSDoc for shared top-level browser functions, including parameter/return types
and asynchronous side effects. Shared record shapes are typedefs in `state.js`
and `menu-complex.js`. Use Google-style Python docstrings (`Args`, `Returns`,
`Raises` where applicable) for module-level developer-tool functions.

Run `python tools/check_docs.py` after changing an interface. CI checks every
public C++ type/function in handwritten headers, every named top-level browser
function (including arrow helpers), and module-level Python tool functions/classes.
It requires documentation, matching parameter names and return contracts.
Generated/vendor code, private implementation helpers, event callbacks and test
functions are excluded. Those should still explain non-obvious behavior, not
repeat the code. Mechanical coverage does not replace review of contract accuracy.

## Labels and languages

The web uses all three bundled TECH dictionaries. Its own interface translations
are separate. Operation results use stable codes from `ResultCode` with separate
item/value fields. The web translates codes through each locale's `results`
object; do not parse English sentences or prefixes to determine application state.

MQTT labels are generated from the chosen TECH dictionary at build time:

```sh
python tools/build_assets.py --mqtt-language en
# Also supported: --mqtt-language cs or --mqtt-language pl
```

The generated source files contain shared gzip blocks and a small block index, not
a second table of translated strings. MQTT uses every code present in its selected
dictionary. Only genuinely absent codes (or a failed decode/allocation) fall back
to `TECH <code>`. Do not edit generated headers or add handwritten label subsets.
The web reads the same compressed bytes through `dictionary.js`; details and
memory bounds are in [development](docs/development.md#shared-compressed-dictionaries).
