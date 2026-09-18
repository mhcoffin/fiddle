export function formatBalance(value) {
    const amount = Math.round(Math.abs(value));
    return amount === 0 ? "C" : `${value < 0 ? "L" : "R"}${amount}`;
}

// Accept signed percentages, L25/25L, R25/25R, C, and Center.
export function parseBalance(text) {
    const input = text.trim().toUpperCase();
    if (input === "C" || input === "CENTER") return 0;
    const directional = input.match(/^(?:([LR])\s*(\d+(?:\.\d+)?)|(\d+(?:\.\d+)?)\s*([LR]))$/);
    let value;
    if (directional) {
        value = Number(directional[2] || directional[3]) * ((directional[1] || directional[4]) === "L" ? -1 : 1);
    } else {
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(input)) return null;
        value = Number(input);
    }
    return Number.isFinite(value) && Math.abs(value) <= 100 ? value : null;
}
