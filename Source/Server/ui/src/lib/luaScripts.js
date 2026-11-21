// What the Layer details panel says about a layer's Lua script and the file
// it came from. A layer runs the copy of the script saved with its version,
// so an edited or missing file doesn't change how it plays
// (docs/lua-scripts.md).

/**
 * The note under a script's row, or null when there's nothing to say.
 * canUseFile: offer "Use new file", which switches every layer using the
 * script to the file as it is now.
 */
export function luaFileNote(plugin) {
    if (!plugin?.loaded) return null;
    const fileName = plugin.fileName || "the file";
    if (plugin.fileStatus === "changed") {
        if (plugin.fileError)
            return {
                text: `${fileName} has changed, but it has an error, so this layer keeps playing its saved copy. ${plugin.fileError}`,
                canUseFile: false,
            };
        return {
            text: `${fileName} has changed since this copy was saved. This layer keeps playing the saved copy until you use the new file.`,
            canUseFile: true,
        };
    }
    if (plugin.fileStatus === "missing")
        return { text: `${fileName} isn't on this Mac. This layer plays the copy saved with the setup.`, canUseFile: false };
    return null;
}

export const useFileTitle = (plugin) =>
    `Every layer that uses ${plugin?.fileName || "this script"} switches to the file as it is now. You can undo this.`;
