// SPDX-License-Identifier: MIT

/**
 * Decodes a gateway dictionary response, including independently compressed gzip members.
 *
 * Rejects on HTTP failure, invalid block lengths, decompression failure or invalid JSON. Does not update UI state.
 * @param {Response} response Successful fetch response from the dictionary endpoint; its body is consumed.
 * @returns {Promise<Object<string, string>>} Decoded description-code dictionary. Plain JSON responses are also accepted.
 */
async function decodeDictionary(response) {
    if (!response.ok) {
        throw new Error(`Dictionary request failed: HTTP ${response.status}`);
    }

    const blockHeader = response.headers.get('X-Tech-Dictionary-Blocks');

    if (!blockHeader) {
        return response.json();
    }

    const bytes = await response.arrayBuffer();
    const fragments = [];
    let offset = 0;

    for (const entry of blockHeader.split(',')) {
        const length = Number(entry);

        if (!Number.isSafeInteger(length) || length < 18 || offset + length > bytes.byteLength) {
            throw new Error('Invalid compressed dictionary block length');
        }

        const member = new Blob([bytes.slice(offset, offset + length)]);
        const stream = member.stream().pipeThrough(new DecompressionStream('gzip'));
        fragments.push(await new Response(stream).text());
        offset += length;
    }

    if (offset !== bytes.byteLength) {
        throw new Error('Incomplete compressed dictionary index');
    }

    return JSON.parse(fragments.join(''));
}
