export function availableEffects(plugins, query = "") {
    const needle = query.trim().toLocaleLowerCase();
    return (Array.isArray(plugins) ? plugins : [])
        .filter((plugin) => plugin.valid !== false && plugin.compatibleAsEffect)
        .filter((plugin) => {
            if (!needle) return true;
            return [plugin.name, plugin.manufacturer, plugin.category]
                .some((value) => String(value ?? "").toLocaleLowerCase().includes(needle));
        })
        .sort((a, b) => {
            const maker = String(a.manufacturer || "Other").localeCompare(
                String(b.manufacturer || "Other"),
            );
            return maker || String(a.name || "").localeCompare(String(b.name || ""));
        });
}

export function groupEffects(plugins, query = "") {
    const groups = [];
    for (const plugin of availableEffects(plugins, query)) {
        const manufacturer = plugin.manufacturer || "Other";
        let group = groups.at(-1);
        if (!group || group.manufacturer !== manufacturer) {
            group = { manufacturer, plugins: [] };
            groups.push(group);
        }
        group.plugins.push(plugin);
    }
    return groups;
}

export function clampMasterGain(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return 0;
    return Math.max(-120, Math.min(6, numeric));
}
