// Pure decoders: keep protocol numbers unchanged, never invent missing slots.
/**
 * @typedef {Object} CurveDescription
 * @property {number} min Lower bound in controller units.
 * @property {number} max Upper bound in controller units.
 * @property {Array<{value:number, default:number, position:number}>} points Ordered curve points.
 */
/**
 * @typedef {Object} ScheduleDescription
 * @property {number} day Controller weekday index.
 * @property {number} referenceId Referenced numeric item ID.
 * @property {number} min Minimum offset in protocol units.
 * @property {number} max Maximum offset in protocol units.
 * @property {number|null} base Known reference value, or null.
 * @property {Array<{hour:number, offset:number, value:number|null}>} hours Zero or 24 decoded slots.
 */
/**
 * Decodes a type-30 curve without inventing units or applying scaling.
 * @param {string[]} fields CSV fields from a controller record.
 * @returns {CurveDescription|null} Bounds and ordered value/default/position triples, or null for unsupported or invalid data.
 */
function decodeCurve(fields) {
    if (fields[2] !== '30' || fields.length < 15 || (fields.length - 9) % 3 !== 0) {
        return null;
    }

    const numbers = fields.slice(7).map((s) => (/^-?\d+$/.test(s) ? Number(s) : NaN));

    if (numbers.some((n) => !Number.isSafeInteger(n)) || numbers[0] > numbers[1]) {
        return null;
    }

    const points = [];

    for (let p = 2; p < numbers.length; p += 3) {
        points.push({ value: numbers[p], default: numbers[p + 1], position: numbers[p + 2] });
    }

    return { min: numbers[0], max: numbers[1], points };
}
/**
 * Decodes a type-12 schedule while preserving missing reference values and hours.
 * @param {string[]} fields CSV fields from a controller record.
 * @param {number|null} reference Finite base value in protocol units, or null when unavailable.
 * @returns {ScheduleDescription|null} Zero or 24 hourly slots with offsets and optional absolute values; null for unsupported or invalid data.
 */
function decodeSchedule(fields, reference) {
    if (fields[2] !== '12' || fields.length < 13) {
        return null;
    }

    const numbers = fields.slice(7).map((s) => (/^-?\d+$/.test(s) ? Number(s) : NaN));

    if (numbers.some((n) => !Number.isSafeInteger(n)) || numbers[4] > numbers[5]) {
        return null;
    }

    const stored = numbers.slice(6);

    if (stored.length !== 0 && stored.length !== 24) {
        return null;
    }

    if (stored.some((n) => n < 0 || n > numbers[5] - numbers[4])) {
        return null;
    }

    const base = Number.isFinite(reference) ? reference : null;
    return {
        day: numbers[0],
        referenceId: numbers[3],
        min: numbers[4],
        max: numbers[5],
        base,
        hours: stored.map((n, hour) => ({
            hour,
            offset: n + numbers[4],
            value: base === null ? null : base + n + numbers[4],
        })),
    };
}
