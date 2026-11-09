// SPDX-License-Identifier: MIT
/**
 * Refreshes page labels, connection/result indicators and an open diagnostic dialog.
 *
 * Uses appState without fetching data or sending commands.
 * @returns {void} No return value.
 */
function updateInterface() {
    document.documentElement.lang = appState.language;
    document.title = 'TECH · menu';
    $('intro').textContent = tr('intro');
    $('help').textContent = tr('help');
    $('search').placeholder = tr('search');
    $('hidden-label').textContent = tr('hidden');
    $('refresh').textContent = '↻';
    $('refresh').title = tr('refresh');
    $('refresh').setAttribute('aria-label', tr('refresh'));
    $('about').title = UI_LOCALES[appState.language].details;
    $('about').setAttribute('aria-label', UI_LOCALES[appState.language].details);

    for (const b of $('tabs').children) {
        b.textContent = tr(b.dataset.group);
    }

    $('connection').textContent = appState.meta.online
        ? '● ' + tr('connected')
        : '○ ' + tr('disconnected');
    $('status').textContent = appState.meta.overflow
        ? tr('overflow')
        : appState.meta.online === false
          ? tr('disconnected')
          : appState.meta.result && appState.meta.result.code !== 'idle'
            ? formatResult(appState.meta.result)
            : '';

    if ($('detail').open) {
        fillDetail();
    }
}

/**
 * Chooses the display name appropriate to an ordinary menu, overview or schedule record.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {string} Resolved description or weekday label.
 */
const display = (i) =>
    i.key.startsWith('SU:')
        ? labelFor(i.f[2] === '23' ? i.f[14] : i.f[5])
        : i.f[2] === '12' && i.f[4] === '0'
          ? dayName(Number(i.f[7]))
          : labelFor(i.f[4]);

/**
 * Loads and selects a UI dictionary without discarding edits.
 *
 * Caches dictionaries, persists the successful choice when localStorage is available and redraws the UI. A failed fetch preserves the previous language.
 * @param {string} next Requested locale key; unsupported keys fall back to English.
 * @returns {Promise<void>} Resolves after switching, ignoring a stale request or displaying an error.
 */
async function setLanguage(next) {
    if (!UI_LOCALES[next]) {
        next = 'en';
    }

    const request = ++appState.languageRequest;

    try {
        if (!appState.dictionaries[next]) {
            const response = await fetch('/labels.gz?lang=' + next);

            if (!response.ok) {
                throw Error(`HTTP ${response.status}`);
            }

            appState.dictionaries[next] = await decodeDictionary(response);
        }

        if (request !== appState.languageRequest) {
            return;
        }

        appState.language = next;
        appState.labels = appState.dictionaries[next];
        $('language').value = next;

        try {
            localStorage.setItem('tech-menu-language', next);
        } catch {}

        updateInterface();
        render();
    } catch (e) {
        if (request !== appState.languageRequest) {
            return;
        }

        $('language').value = appState.language;
        $('message').textContent = e.message;
    }
}

/**
 * Creates a detached HTML element, setting text content rather than parsing markup.
 * @param {string} tag HTML element name.
 * @param {string|number|undefined} text Optional text content; undefined leaves it unset.
 * @param {string|undefined} cls Optional CSS class string.
 * @returns {HTMLElement} New element owned by the caller until attached to the DOM.
 */
function el(tag, text, cls) {
    const e = document.createElement(tag);

    if (text !== undefined) {
        e.textContent = text;
    }

    if (cls) {
        e.className = cls;
    }

    return e;
}
