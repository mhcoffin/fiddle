let rememberedQuery = "";

export function getRememberedExpressionMapQuery() {
    return rememberedQuery;
}

export function rememberExpressionMapQuery(query) {
    rememberedQuery = String(query ?? "");
}
