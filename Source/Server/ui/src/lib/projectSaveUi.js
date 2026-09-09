// Historical versions are editable. The server decides whether the captured
// state actually changed; the dirty hint only controls button availability.
export function projectSaveButton(dirty, historical) {
    return {
        disabled: !dirty,
        title: historical
            ? "Save changes on a new branch from this historical version"
            : "Save config (creates a new version if changed)",
    };
}

export const historicalSaveNotice =
    "Viewing historical version — saving changes creates a new branch.";
