// Moves an element to <body>, so overflow, transforms and backdrop filters
// on its ancestors can't clip or reposition a fixed-position overlay.
// Use as an action: <div use:portal>.
export function portal(node) {
    document.body.appendChild(node);
    return {
        destroy() {
            node.remove();
        },
    };
}
