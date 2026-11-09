// SPDX-License-Identifier: MIT
/**
 * Fetches all pages of the selected menu and updates the current snapshot.
 *
 * Stale group requests are discarded using the epoch. Concurrent calls schedule a retry; drafts remain separate and focused forms are not replaced.
 * @returns {Promise<void>} Resolves after the attempt; errors are displayed in the UI rather than rethrown.
 */
async function load() {
    const g = appState.group,
        version = appState.epoch;

    if (appState.loading) {
        setTimeout(() => {
            if (version === appState.epoch) {
                load();
            }
        }, 200);
        return;
    }

    appState.loading = true;

    try {
        let offset = 0,
            next = [],
            state;
        do {
            const response = await fetch(`/api/menu?group=${g}&offset=${offset}`, {
                cache: 'no-store',
            });

            if (!response.ok) {
                throw Error(`HTTP ${response.status}`);
            }

            state = await response.json();
            next.push(...state.items);
            offset = state.next;
        } while (offset !== null);

        if (version !== appState.epoch) {
            return;
        }

        appState.items = next.map((i) => ({ ...i, f: i.raw.split(',') }));
        appState.meta = state;
        updateInterface();

        if (!document.activeElement.closest('form')) {
            render();
        }
    } catch (e) {
        $('connection').textContent = '○ ' + tr('offline');
        $('message').textContent = e.message;
    } finally {
        appState.loading = false;
    }
}
