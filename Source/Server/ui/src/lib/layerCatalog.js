export function filterLayerCatalog(patches, query) {
    const terms = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const matches = terms.length === 0
        ? patches
        : patches.filter((patch) => {
            const haystack = [
                patch.name,
                patch.libraryName,
                patch.character,
                patch.family,
            ].filter(Boolean).join(" ").toLowerCase();
            return terms.every((term) => haystack.includes(term));
        });

    const groups = new Map();
    for (const patch of matches) {
        const key = patch.libraryId || patch.libraryName || "other";
        if (!groups.has(key)) {
            groups.set(key, {
                id: key,
                name: patch.libraryName || "Other",
                patches: [],
            });
        }
        groups.get(key).patches.push(patch);
    }
    return [...groups.values()]
        .map((group) => ({
            ...group,
            patches: group.patches.slice().sort((a, b) => a.name.localeCompare(b.name)),
        }))
        .sort((a, b) => a.name.localeCompare(b.name));
}
