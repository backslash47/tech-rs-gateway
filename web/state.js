// SPDX-License-Identifier: MIT
/**
 * @typedef {Object} MenuItem
 * @property {string} key Group-qualified controller item ID.
 * @property {string} raw Original controller record.
 * @property {string[]} f Parsed CSV fields in protocol order.
 * @property {boolean} visible Visibility including ancestors.
 * @property {boolean} writable Scalar-write capability.
 * @property {boolean} action Experimental action capability.
 * @property {number} age Record age in seconds, not proof of delta-stream validity.
 */
/**
 * @typedef {Object} OperationResult
 * @property {string} code Stable backend result code.
 * @property {string} item Group-qualified item key, or an empty string.
 * @property {number|null} value Protocol value, or null if not applicable.
 */
/** Single mutable UI state; request counters reject stale asynchronous responses. */
const appState = {
    language: 'en',
    languageRequest: 0,
    dictionaries: {},
    group: 'MU',
    labels: {},
    items: [],
    meta: {},
    loading: false,
    epoch: 0,
    drafts: new Map(),
    openGroups: new Map(),
    detailItem: null,
    submitting: false,
    mqttPasswordStored: false,
};
const groups = { MU: 1, MI: 1, MS: 1, SU: 1 };
/**
 * Finds a page element by ID.
 * @param {string} id DOM element ID.
 * @returns {HTMLElement|null} Existing element, or null when absent; callers require the page markup to be loaded.
 */
const $ = (id) => document.getElementById(id);
/**
 * Looks up general interface text in the selected locale.
 * @param {string} key Key in the interface translation map.
 * @returns {string|undefined} Translation, or undefined for an unknown key.
 */
const tr = (key) => UI_LOCALES[appState.language].interface[key];
/**
 * Looks up complex-editor text in the selected locale.
 * @param {string} key Key in the complex translation map.
 * @returns {string|undefined} Translation, or undefined for an unknown key.
 */
const ct = (key) => UI_LOCALES[appState.language].complex[key];
/**
 * Looks up MQTT interface text in the selected locale.
 * @param {string} key Key in the mqtt translation map.
 * @returns {string|undefined} Translation, or undefined for an unknown key.
 */
const mt = (key) => UI_LOCALES[appState.language].mqtt[key];
/**
 * Resolves a controller description code from the loaded dictionary.
 * @param {string|number|undefined} code Description code, not the menu item ID.
 * @returns {string} Dictionary label or the localized item-code fallback.
 */
const labelFor = (code) => appState.labels[code] || `${tr('item')} ${code}`;

/**
 * Formats a structured operation result without parsing backend prose.
 * @param {Partial<OperationResult>} result Result fields; omitted input defaults to an empty object.
 * @returns {string} Localized code with optional item/value; unknown codes are shown verbatim.
 */
function formatResult(result = {}) {
    const message = UI_LOCALES[appState.language].results[result.code] || result.code || '';
    const item = result.item ? ` · ${result.item}` : '';
    const value = result.value === null || result.value === undefined ? '' : ` = ${result.value}`;
    return message + item + value;
}
