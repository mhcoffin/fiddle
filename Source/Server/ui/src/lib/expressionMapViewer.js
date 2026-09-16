export const expressionMapActionText = (action) => {
    if (!action) return "";
    switch (action.type) {
        case "keySwitch": return `Key ${action.param1} @ ${action.param2}`;
        case "noteVelocity": return `Note ${action.param1} @ ${action.param2}`;
        case "cc":
        case "controlChange": return `CC ${action.param1} = ${action.param2}`;
        case "channelSwitch": return `Channel ${action.param1}`;
        case "programChange": return `Program ${action.param1}`;
        default: return `Unknown (${action.param1}, ${action.param2})`;
    }
};

export const expressionMapActionsText = (actions) =>
    actions?.length ? actions.map(expressionMapActionText).join(", ") : "—";

export const expressionMapDynamicsText = (type, cc) =>
    type === "cc" ? `CC ${cc}` : "Note velocity";

const searchableText = (combination) => [
    combination.name,
    ...(combination.techniqueIDs ?? []),
    ...(combination.switchOnActions ?? []).map(expressionMapActionText),
    ...(combination.switchOffActions ?? []).map(expressionMapActionText),
    combination.condition,
].join(" ").toLocaleLowerCase();

export const filterExpressionMapCombinations = (combinations, query) => {
    const needle = query.trim().toLocaleLowerCase();
    return needle
        ? combinations.filter((combination) => searchableText(combination).includes(needle))
        : combinations;
};

export const selectExpressionMapCombination = (combinations, requestedIndex) =>
    combinations.find((combination) => combination.index === requestedIndex)
        ?? combinations[0]
        ?? null;
