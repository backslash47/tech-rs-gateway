// SPDX-License-Identifier: MIT
/**
 * Formats a controller weekday using the selected UI language and UTC.
 * @param {number} day Sunday-based weekday index (0–6); other values use a numeric fallback label.
 * @returns {string} Localized weekday or fallback text.
 */
function dayName(day) {
    return day >= 0 && day < 7
        ? new Intl.DateTimeFormat(appState.language, { weekday: 'long', timeZone: 'UTC' }).format(
              new Date(Date.UTC(2023, 0, 1 + day)),
          )
        : ct('day') + ' ' + day;
}

/**
 * Rebuilds the diagnostic dialog from appState.detailItem without opening it.
 *
 * A null item displays general help. All controller text is inserted as text, not HTML.
 * @returns {void} No return value.
 */
function fillDetail() {
    const i = appState.detailItem;
    $('detail-title').textContent = i ? display(i) : UI_LOCALES[appState.language].details;
    $('detail-meta').replaceChildren();
    $('detail-raw').classList.toggle('gone', !i);
    $('about-text').classList.toggle('gone', !!i);

    if (!i) {
        $('detail-meta').append(
            el('dt', 'tech-rs-gateway'),
            el('dd', `${BUILD_INFO.version} (${BUILD_INFO.build})`),
        );
    }

    if (i) {
        for (const [k, v] of [
            ['ID', i.key],
            [tr('type'), i.f[2]],
            [tr('age'), i.age + ' s'],
            [
                tr('range'),
                ['1', '2', '3'].includes(i.f[2])
                    ? i.f[8] + '–' + i.f[9] + ' (' + tr('units') + ')'
                    : '—',
            ],
        ]) {
            $('detail-meta').append(el('dt', k), el('dd', v));
        }

        $('raw-title').textContent = tr('raw');
        $('raw-text').textContent = i.raw;
    }
}

/**
 * Selects a record and opens its diagnostic dialog.
 * @param {MenuItem|null} i Record to inspect, or null to show general help.
 * @returns {void} No return value.
 */
function showDetail(i) {
    appState.detailItem = i;
    fillDetail();
    $('detail').showModal();
}

/**
 * Clears and opens the shared complex-value dialog.
 *
 * Replaces previous dialog content and actions; does not send a controller command.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {HTMLElement} Existing dialog body container, ready for new children.
 */
function beginComplex(i) {
    $('complex-title').textContent = display(i);
    $('complex-body').replaceChildren();
    $('complex-actions').replaceChildren();
    $('complex').showModal();
    return $('complex-body');
}

/**
 * Builds a detached text-only table for complex values.
 * @param {string[]} headers Column headings in display order.
 * @param {Array<Array<unknown>>} rows Rows whose values are converted to strings.
 * @returns {HTMLTableElement} New table element; the caller attaches it to the DOM.
 */
function table(headers, rows) {
    const t = el('table', undefined, 'complex-table'),
        head = el('tr');

    for (const h of headers) {
        head.append(el('th', h));
    }

    t.append(head);

    for (const values of rows) {
        const r = el('tr');

        for (const v of values) {
            r.append(el('td', String(v)));
        }

        t.append(r);
    }

    return t;
}

/**
 * Creates a detached SVG element with stringified attributes.
 * @param {string} tag SVG element name.
 * @param {Object<string, string|number>} attrs Attributes to set on the element.
 * @returns {SVGElement} New SVG-namespace element.
 */
function svgNode(tag, attrs) {
    const n = document.createElementNS('http://www.w3.org/2000/svg', tag);

    for (const [k, v] of Object.entries(attrs)) {
        n.setAttribute(k, String(v));
    }

    return n;
}

/**
 * Opens a read-only curve chart and table.
 *
 * Falls back to raw diagnostic details if the record cannot be decoded. No write is performed.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {void} No return value.
 */
function showCurve(i) {
    const c = decodeCurve(i.f);

    if (!c) {
        showDetail(i);
        return;
    }

    const body = beginComplex(i),
        points = [...c.points].sort((a, b) => a.position - b.position);
    const xmin = points[0].position,
        xmax = points.at(-1).position,
        ymin = Math.min(c.min, ...points.map((p) => p.value)),
        ymax = Math.max(c.max, ...points.map((p) => p.value));
    const x = (v) => 40 + ((v - xmin) / Math.max(1, xmax - xmin)) * 360,
        y = (v) => 185 - ((v - ymin) / Math.max(1, ymax - ymin)) * 155;
    const svg = svgNode('svg', {
        viewBox: '0 0 440 225',
        class: 'complex-chart',
        role: 'img',
        'aria-label': display(i),
    });
    svg.append(svgNode('path', { d: 'M40 25V185H410', stroke: 'var(--muted)', fill: 'none' }));
    svg.append(
        svgNode('polyline', {
            points: points.map((p) => `${x(p.position)},${y(p.value)}`).join(' '),
            stroke: 'var(--accent)',
            'stroke-width': 3,
            fill: 'none',
        }),
    );

    for (const p of points) {
        svg.append(
            svgNode('circle', { cx: x(p.position), cy: y(p.value), r: 4, fill: 'var(--accent)' }),
        );

        for (const [text, py] of [
            [p.value, y(p.value) - 10],
            [p.position, 207],
        ]) {
            const label = svgNode('text', {
                x: x(p.position),
                y: py,
                'text-anchor': 'middle',
                fill: 'var(--text)',
                'font-size': 12,
            });
            label.textContent = String(text);
            svg.append(label);
        }
    }

    body.append(
        svg,
        table(
            [ct('position'), ct('value'), ct('default')],
            points.map((p) => [p.position, p.value, p.default]),
        ),
        el('p', ct('readonly'), 'complex-note'),
    );
}

/**
 * Opens a read-only schedule using a recent reference item when available.
 *
 * Only a reference younger than 600 seconds is used. Missing reference values stay unknown; invalid records fall back to raw details.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {void} No return value.
 */
function showSchedule(i) {
    const reference = appState.items.find(
            (r) => r.f[0] === i.f[10] && r.f[2] === '1' && r.age < 600,
        ),
        base = reference ? Number(reference.f[7]) : null,
        s = decodeSchedule(i.f, base);

    if (!s) {
        showDetail(i);
        return;
    }

    const body = beginComplex(i);
    body.append(el('p', dayName(s.day)));

    if (s.hours.length) {
        body.append(
            table(
                [ct('hour'), ct('offset'), ...(s.base === null ? [] : [ct('value')])],
                s.hours.map((h) => [
                    String(h.hour).padStart(2, '0') + ':00',
                    h.offset > 0 ? '+' + h.offset : h.offset,
                    ...(s.base === null ? [] : [h.value]),
                ]),
            ),
        );
    } else {
        body.append(el('p', ct('empty'), 'complex-note'));
    }

    body.append(
        el(
            'p',
            ct('reference') +
                ': ' +
                (reference ? display(reference) + ' = ' + base : s.referenceId),
            'complex-note',
        ),
        el('p', ct('readonly'), 'complex-note'),
    );
}

/**
 * Opens an experimental action dialog and binds its confirmation handler.
 *
 * Opening or cancelling does not transmit. Confirmation posts the subtype value and expected raw record, then refreshes the menu; delivery is not execution confirmation.
 * @param {MenuItem} i Record from the currently loaded menu, including parsed fields and capability flags.
 * @returns {void} No return value.
 */
function showAction(i) {
    const body = beginComplex(i);
    body.append(el('p', labelFor(i.f[7])), el('p', ct('experimental'), 'complex-note'));
    const cancel = el('button', ct('cancel')),
        confirm = el('button', ct(i.f[8] === '1' ? 'yes' : 'confirm'), 'save');
    cancel.onclick = () => $('complex').close();
    confirm.disabled = !appState.meta.online || appState.meta.busy || appState.submitting;
    confirm.onclick = async () => {
        if (appState.submitting) {
            return;
        }

        appState.submitting = true;
        confirm.disabled = true;

        try {
            const value = { 0: 2, 1: 1, 2: 3 }[i.f[8]];

            if (value === undefined) {
                throw Error(ct('readonly'));
            }

            const response = await fetch(
                '/api/write?' +
                    new URLSearchParams({
                        key: i.key,
                        value: String(value),
                        expected: i.raw,
                        action: '1',
                    }),
                { method: 'POST', headers: { 'X-Tech-Token': appState.meta.token } },
            );
            const result = await response.json();
            $('message').textContent = formatResult(result);
            $('complex').close();
            await load();
        } catch (e) {
            $('message').textContent = e.message;
            $('complex').close();
        } finally {
            appState.submitting = false;
            render();
        }
    };
    $('complex-actions').append(cancel, confirm);
}
