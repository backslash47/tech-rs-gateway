// SPDX-License-Identifier: MIT
/**
 * Builds a detached scalar editor with draft tracking and a guarded submit handler.
 *
 * Requires supported writable metadata. Submission sends value plus expected previous value; errors preserve the draft, while acceptance clears it pending fresh readback.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {HTMLFormElement} Form for a numeric, boolean or choice item.
 */
function createScalarEditor(i) {
    const t = Number(i.f[2]);
    const form = el('form');
    let input;
    const draft = appState.drafts.get(i.key),
        initial = draft ? draft.value : i.f[7];

    if (t === 10) {
        input = el('button', undefined, 'toggle');
        input.type = 'button';
        input.setAttribute('role', 'switch');
        input.setAttribute('aria-checked', initial === '1' ? 'true' : 'false');
        input.value = initial;
    } else if (t === 11) {
        input = el('select');

        for (let n = 9; n + 1 < i.f.length; n += 2) {
            const o = el('option', labelFor(i.f[n]));
            o.value = i.f[n + 1];
            input.append(o);
        }

        input.value = initial;
    } else {
        input = el('input');
        input.type = 'number';
        input.step = '1';
        input.min = i.f[8];
        input.max = i.f[9];
        input.value = initial;
        input.title = `${tr('range')} ${i.f[8]} – ${i.f[9]} (${tr('units')})`;
    }

    input.setAttribute('aria-label', display(i));
    input.dataset.key = i.key;
    const save = el('button', tr('save'), 'save');
    save.type = 'submit';
    const blocked = () => !appState.meta.online || appState.meta.busy || appState.submitting;
    const update = () => {
        save.classList.toggle('gone', !appState.drafts.has(i.key));
        save.disabled = blocked() || (input.tagName === 'INPUT' && !input.checkValidity());
    };
    const change = () => {
        if (input.value === i.f[7]) {
            appState.drafts.delete(i.key);
        } else {
            appState.drafts.set(i.key, {
                value: input.value,
                expected: appState.drafts.get(i.key)?.expected ?? i.f[7],
            });
        }

        update();
    };

    if (t === 10) {
        input.onclick = () => {
            input.value = input.value === '1' ? '0' : '1';
            input.setAttribute('aria-checked', input.value === '1' ? 'true' : 'false');
            change();
        };
    } else {
        input.oninput = change;
    }

    form.append(input);

    if (t === 1 && i.f[11] && i.f[11] !== '0' && appState.labels[i.f[11]]) {
        form.append(el('span', labelFor(i.f[11]), 'muted'));
    }

    form.append(save);
    update();
    form.onsubmit = async (e) => {
        e.preventDefault();
        const d = appState.drafts.get(i.key);

        if (!d || blocked() || !form.reportValidity()) {
            return;
        }

        appState.submitting = true;
        update();

        try {
            const response = await fetch(
                '/api/write?' +
                    new URLSearchParams({
                        key: i.key,
                        value: d.value,
                        expected: d.expected,
                    }),
                { method: 'POST', headers: { 'X-Tech-Token': appState.meta.token } },
            );
            const result = await response.json();
            $('message').textContent = formatResult(result);

            if (response.ok) {
                appState.drafts.delete(i.key);
            }

            input.blur();
            await load();
        } catch (e) {
            $('message').textContent = e.message;
        } finally {
            appState.submitting = false;
            render();
        }
    };
    return form;
}

/**
 * Builds a display value using the menu or overview record layout.
 *
 * Overview temperatures use the documented tenths-degree representation; numeric menu values remain in protocol units.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {HTMLSpanElement} Detached span with a value, missing-data indicator or read-only fallback.
 */
function createReadOnlyValue(i) {
    const t = Number(i.f[2]);
    let value = ['1', '2', '3', '10', '11'].includes(i.f[2]) ? i.f[7] : undefined;

    // SU has its own layouts: version is literal field4; status is a label code.
    if (appState.group === 'SU' && t === 50) {
        value = i.f[4];
    }

    if (appState.group === 'SU' && t === 40) {
        value = labelFor(i.f[4]);
    }

    if (appState.group === 'SU' && t === 1) {
        value = ['-2030', '-2000'].includes(i.f[6]) ? tr('na') : `${Number(i.f[6]) / 10} °C`;
    }

    if (appState.group !== 'SU' && t === 10 && ['0', '1'].includes(value)) {
        value = tr(value === '1' ? 'on' : 'off');
    }

    if (appState.group !== 'SU' && t === 11) {
        for (let p = 9; p + 1 < i.f.length; p += 2) {
            if (i.f[p + 1] === i.f[7]) {
                value = labelFor(i.f[p]);
                break;
            }
        }
    }

    if (appState.group === 'SU' && t === 23) {
        value = `${tr('water')} ${Number(i.f[6]) / 10} °C · ${tr('target')} ${i.f[13]} °C · ${tr('valve')} ${i.f[9]} % · ${tr('pump')} ${tr(i.f[10] === '1' ? 'on' : 'off')}`;
    }

    return el(
        'span',
        value === undefined ? tr('readonly') : value,
        value === undefined || value === tr('na') ? 'muted' : 'value' + (t === 23 ? ' long' : ''),
    );
}

/**
 * Builds one record row with its appropriate editor, action or read-only display.
 *
 * Uses current connection/write state to disable controls; creating the row does not transmit.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {HTMLElement} Detached article element with bound UI handlers.
 */
function createMenuRow(i) {
    const box = el('article', undefined, 'item' + (!i.visible ? ' hidden' : '')),
        t = Number(i.f[2]);
    const line = el('div', undefined, 'row' + (t === 10 || !i.writable ? ' simple' : '')),
        title = el('div', display(i), 'title'),
        controls = el('div', undefined, 'controls');
    line.append(title);

    if (i.action) {
        const action = el('button', ct('run'), 'action-button');
        action.disabled = !appState.meta.online || appState.meta.busy || appState.submitting;
        action.onclick = () => showAction(i);
        controls.append(action);
    } else if (appState.group !== 'SU' && t === 30 && decodeCurve(i.f)) {
        const view = el('button', ct('view'), 'action-button');
        view.onclick = () => showCurve(i);
        controls.append(view);
    } else if (appState.group !== 'SU' && t === 12 && decodeSchedule(i.f, null)) {
        const view = el('button', ct('view'), 'action-button');
        view.onclick = () => showSchedule(i);
        controls.append(view);
    } else if (i.writable) {
        controls.append(createScalarEditor(i));
    } else {
        controls.append(createReadOnlyValue(i));
    }

    const more = el('button', '⋯', 'more');
    more.title = UI_LOCALES[appState.language].details;
    more.setAttribute('aria-label', UI_LOCALES[appState.language].details + ': ' + display(i));
    more.onclick = () => showDetail(i);
    controls.append(more);
    line.append(controls);
    box.append(line);
    return box;
}
