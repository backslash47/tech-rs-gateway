// SPDX-License-Identifier: MIT
/**
 * Rebuilds the menu tree for the current search and hidden-item selection.
 *
 * Preserves draft values and expanded groups in appState. Hierarchy traversal is bounded and cycle-guarded; no network request is made.
 * @returns {void} No return value.
 */
function render() {
    mqttChrome();

    for (const b of $('tabs').children) {
        b.classList.toggle('selected', b.dataset.group === appState.group);
    }

    $('menu').replaceChildren();
    const query = $('search').value.toLocaleLowerCase(),
        show = $('hidden').checked;
    const byId = new Map(appState.items.map((i) => [i.f[0], i]));
    const relevant = new Set();

    for (const i of appState.items) {
        if (!show && !i.visible) {
            continue;
        }

        if (query && !`${display(i)} ${i.key} ${i.raw}`.toLocaleLowerCase().includes(query)) {
            continue;
        }

        let p = i;

        for (let d = 0; p && d < 32; d++) {
            relevant.add(p.key);

            if (p.f[0] === '0' || p.f[1] === '0') {
                break;
            }

            p = byId.get(p.f[1]);
        }
    }

    const visited = new Set();
    function add(parent, container, depth) {
        if (depth > 32) {
            return;
        }

        for (const i of appState.items) {
            if (i.f[0] === '0' || i.f[1] !== parent || !relevant.has(i.key) || visited.has(i.key)) {
                continue;
            }

            visited.add(i.key);

            if (appState.group !== 'SU' && i.f[2] === '0') {
                const d = el('details', undefined, 'group' + (!i.visible ? ' hidden' : ''));
                d.dataset.key = i.key;
                d.open =
                    !!query ||
                    (appState.openGroups.has(i.key) ? appState.openGroups.get(i.key) : depth < 2);
                d.ontoggle = () => {
                    if (d.isConnected && !query) {
                        appState.openGroups.set(i.key, d.open);
                    }
                };
                d.append(el('summary', display(i)));
                const children = el('div', undefined, 'children');
                add(i.f[0], children, depth + 1);
                d.append(children);
                container.append(d);
            } else {
                if (depth === 0) {
                    let flat = container.lastElementChild;

                    if (!flat?.classList.contains('flat')) {
                        flat = el('section', undefined, 'flat');
                        container.append(flat);
                    }

                    flat.append(createMenuRow(i));
                } else {
                    container.append(createMenuRow(i));
                }
            }
        }
    }
    add('0', $('menu'), 0);

    for (const i of appState.items) {
        if (i.f[0] !== '0' && relevant.has(i.key) && !visited.has(i.key)) {
            const flat = el('section', undefined, 'flat');
            flat.append(createMenuRow(i));
            $('menu').append(flat);
        }
    }

    if (!$('menu').children.length) {
        $('menu').append(el('p', tr('empty'), 'muted'));
    }
}
