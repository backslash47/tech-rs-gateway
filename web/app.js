// SPDX-License-Identifier: MIT
/**
 * @file Binds page events and starts menu refresh after all shared UI scripts.
 * Loaded once at the end of index.html. Opening the page does not submit writes;
 * controller/settings changes originate only from explicit editor confirmation.
 */

for (const key of Object.keys(groups)) {
    const b = el('button', tr(key));
    b.dataset.group = key;
    b.onclick = () => {
        appState.group = key;
        appState.epoch++;
        appState.items = [];
        render();
        load();
    };
    $('tabs').append(b);
}

$('search').oninput = render;
$('hidden').onchange = render;
$('refresh').onclick = load;
$('detail-close').onclick = () => $('detail').close();
$('about').onclick = () => showDetail(null);
$('complex-close').onclick = () => $('complex').close();
$('mqtt-open').onclick = openMqtt;
$('mqtt-close').onclick = () => $('mqtt-dialog').close();
initializeMqttSettings();
$('language').onchange = () => setLanguage($('language').value);
let preferred = 'en';

try {
    preferred = localStorage.getItem('tech-menu-language') || 'en';
} catch {}

setLanguage(preferred);
load();
setInterval(() => {
    if (!document.hidden) {
        load();
    }
}, 15000);
document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
        load();
    }
});
