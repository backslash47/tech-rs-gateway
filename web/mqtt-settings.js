// SPDX-License-Identifier: MIT
/**
 * Updates MQTT dialog labels and the saved-password placeholder for the current language.
 * @returns {void} No return value.
 */
function mqttChrome() {
    document.querySelectorAll('[data-mq]').forEach((e) => (e.textContent = mt(e.dataset.mq)));
    $('mq-save').textContent = mt('save');
    $('mqtt-open').textContent = mt('settings');
    $('mq-password').placeholder = appState.mqttPasswordStored ? mt('keep') : '';
}

/**
 * Displays broker status, error text and the configured topic pattern.
 * @param {Object} s MQTT API snapshot containing connected, enabled, applying, error and prefix.
 * @returns {void} No return value.
 */
function mqttStatus(s) {
    $('mqtt-state').textContent = s.connected
        ? mt('on')
        : s.enabled || s.applying
          ? mt('waiting')
          : mt('off');
    $('mqtt-error').textContent = s.error || '';
    $('mq-topics').textContent =
        s.prefix + '/menu/<menu>/<id>/state · ' + s.prefix + '/menu/<menu>/<id>/set';
}

/**
 * Fetches settings and opens the MQTT dialog with blank password input.
 *
 * No settings are saved. The server reports only password_set; the actual password is never fetched.
 * @returns {Promise<void>} Resolves after loading or displaying a fetch error.
 */
async function openMqtt() {
    mqttChrome();
    $('mqtt-error').textContent = '';

    try {
        const r = await fetch('/api/mqtt');

        if (!r.ok) {
            throw Error('HTTP ' + r.status);
        }

        const s = await r.json();

        for (const [id, key] of [
            ['mq-host', 'host'],
            ['mq-port', 'port'],
            ['mq-user', 'username'],
            ['mq-prefix', 'prefix'],
        ]) {
            $(id).value = s[key];
        }

        for (const key of ['enabled', 'discovery', 'commands']) {
            $('mq-' + key).checked = s[key];
        }

        $('mq-password').value = '';
        $('mq-clear').checked = false;
        appState.mqttPasswordStored = s.password_set;
        mqttChrome();
        mqttStatus(s);
        $('mqtt-dialog').showModal();
    } catch (e) {
        $('message').textContent = e.message;
    }
}

/**
 * Refreshes connection/error indicators while the MQTT dialog is open.
 *
 * Does not overwrite unsaved form fields or retrieve the stored password.
 * @returns {Promise<void>} Resolves without work when the dialog is closed; fetch errors are displayed.
 */
async function refreshMqtt() {
    if (!$('mqtt-dialog').open) {
        return;
    }

    try {
        const s = await (await fetch('/api/mqtt')).json();
        mqttStatus(s);
    } catch (e) {
        $('mqtt-error').textContent = e.message;
    }
}

/**
 * Binds the settings submit handler and starts periodic visible-page status refresh.
 *
 * Call once after the DOM exists. Saving is triggered only by form submission; polling runs every 2500 milliseconds.
 * @returns {void} No return value.
 */
function initializeMqttSettings() {
    $('mqtt-form').onsubmit = async (e) => {
        e.preventDefault();

        if (!$('mqtt-form').reportValidity()) {
            return;
        }

        $('mq-save').disabled = true;

        try {
            const settings = {
                enabled: $('mq-enabled').checked,
                host: $('mq-host').value.trim(),
                port: Number($('mq-port').value),
                username: $('mq-user').value,
                password: $('mq-password').value,
                clear_password: $('mq-clear').checked,
                prefix: $('mq-prefix').value.trim(),
                discovery: $('mq-discovery').checked,
                commands: $('mq-commands').checked,
            };
            const response = await fetch('/api/mqtt', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'X-Tech-Token': appState.meta.token,
                },
                body: JSON.stringify(settings),
            });
            const result = await response.json();

            if (!response.ok || result.error) {
                throw Error(result.error || 'HTTP ' + response.status);
            }

            $('mq-password').value = '';
            $('mq-clear').checked = false;
            $('mqtt-error').textContent = '';
            await refreshMqtt();
        } catch (e) {
            $('mqtt-error').textContent = e.message;
        } finally {
            $('mq-save').disabled = false;
        }
    };
    setInterval(() => {
        if (!document.hidden) {
            refreshMqtt();
        }
    }, 2500);
}
