const NAME_SEPARATOR = /\s+[\-\u2013\u2014]\s+/;

/**
 * Split a catalog name into its reusable library prefix and useful map label.
 * Names without a separator are grouped by creator by groupExpressionMaps().
 */
export function splitExpressionMapName(name) {
    const fullName = String(name ?? "").trim();
    const match = NAME_SEPARATOR.exec(fullName);
    if (!match || match.index === 0) {
        return { prefix: "", label: fullName };
    }

    const label = fullName.slice(match.index + match[0].length).trim();
    if (!label) return { prefix: "", label: fullName };

    return {
        prefix: fullName.slice(0, match.index).trim(),
        label,
    };
}
/** Return catalog entries matching every whitespace-separated search term. */
export function filterExpressionMaps(maps, query) {
    const terms = String(query ?? "")
        .trim()
        .toLocaleLowerCase()
        .split(/\s+/)
        .filter(Boolean);

    if (terms.length === 0) return Array.isArray(maps) ? maps : [];

    return (Array.isArray(maps) ? maps : []).filter((map) => {
        const haystack = `${map?.name ?? ""} ${map?.creator ?? ""}`.toLocaleLowerCase();
        return terms.every((term) => haystack.includes(term));
    });
}

/**
 * Group catalog entries by their name prefix, falling back to creator metadata.
 * Each item retains its full catalog name for tooltips and searching.
 */
export function groupExpressionMaps(maps, query = "") {
    const groups = new Map();

    for (const map of filterExpressionMaps(maps, query)) {
        if (!map?.entityID || !map?.name) continue;
        const { prefix, label } = splitExpressionMapName(map.name);
        const groupName = prefix || String(map.creator ?? "").trim() || "Other";
        if (!groups.has(groupName)) groups.set(groupName, []);
        groups.get(groupName).push({ ...map, label });
    }

    return [...groups.entries()]
        .sort(([left], [right]) => left.localeCompare(right, undefined, { sensitivity: "base" }))
        .map(([name, items]) => ({
            name,
            items: items.sort((left, right) =>
                left.label.localeCompare(right.label, undefined, { sensitivity: "base" }),
            ),
        }));
}
