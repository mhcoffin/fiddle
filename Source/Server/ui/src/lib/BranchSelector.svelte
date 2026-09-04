<script>
    let {
        branches = [],
        currentBranch = "default",
        onCheckout = (id) => {},
        onCreateBranch = (name) => {},
    } = $props();

    let isDropdownOpen = $state(false);
    let newBranchName = $state("");
    let showNewBranchInput = $state(false);

    let activeBranch = $derived(
        branches.find((b) => b.id === currentBranch) || {
            name: "Unknown",
            id: currentBranch,
        },
    );

    function toggleDropdown() {
        isDropdownOpen = !isDropdownOpen;
        if (!isDropdownOpen) {
            showNewBranchInput = false;
        }
    }

    function handleCheckout(branch) {
        console.log(
            `[BranchSelector] Requesting checkout for branch: ${branch.name} (${branch.id})`,
        );
        onCheckout(branch.id);
        isDropdownOpen = false;
    }

    function handleCreateBranch(e) {
        if (e.key === "Enter" && newBranchName.trim().length > 0) {
            console.log(
                `[BranchSelector] Requesting create branch: ${newBranchName.trim()}`,
            );
            onCreateBranch(newBranchName.trim());
            newBranchName = "";
            showNewBranchInput = false;
            isDropdownOpen = false;
        } else if (e.key === "Escape") {
            showNewBranchInput = false;
        }
    }

    // Close dropdown on click outside
    function handleClickOutside(e) {
        if (!e.target.closest(".branch-selector")) {
            isDropdownOpen = false;
            showNewBranchInput = false;
        }
    }
</script>

<svelte:window onclick={handleClickOutside} />

<div class="branch-selector">
    <button
        class="branch-btn"
        onclick={toggleDropdown}
        title="Select Version Branch"
    >
        <svg
            viewBox="0 0 24 24"
            width="14"
            height="14"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
        >
            <line x1="6" y1="3" x2="6" y2="15"></line>
            <circle cx="18" cy="6" r="3"></circle>
            <circle cx="6" cy="18" r="3"></circle>
            <path d="M18 9a9 9 0 0 1-9 9"></path>
        </svg>
        <span class="branch-name">{activeBranch.name}</span>
        <svg
            class="chevron"
            viewBox="0 0 20 20"
            width="12"
            height="12"
            fill="currentColor"
        >
            <path d="M6 8l4 4 4-4z" />
        </svg>
    </button>

    {#if isDropdownOpen}
        <div class="branch-dropdown dropdown-menu">
            <div class="dropdown-header">Branches</div>
            {#each branches as branch}
                <button
                    class="dropdown-item branch-item"
                    class:active={branch.id === currentBranch}
                    onclick={() => handleCheckout(branch)}
                >
                    <span class="branch-item-name">{branch.name}</span>
                </button>
            {/each}

            <div class="dropdown-divider"></div>

            {#if showNewBranchInput}
                <div class="new-branch-form">
                    <!-- svelte-ignore a11y_autofocus -->
                    <input
                        type="text"
                        class="new-branch-input"
                        placeholder="Branch name..."
                        bind:value={newBranchName}
                        onkeydown={handleCreateBranch}
                        autofocus
                    />
                    <div class="new-branch-hint">Press Enter to create</div>
                </div>
            {:else}
                <button
                    class="dropdown-item new-branch-btn"
                    onclick={(e) => {
                        e.stopPropagation();
                        showNewBranchInput = true;
                    }}
                >
                    <svg
                        viewBox="0 0 20 20"
                        width="12"
                        height="12"
                        fill="currentColor"
                    >
                        <path
                            d="M10 3a1 1 0 0 1 1 1v5h5a1 1 0 1 1 0 2h-5v5a1 1 0 1 1-2 0v-5H4a1 1 0 1 1 0-2h5V4a1 1 0 0 1 1-1z"
                        />
                    </svg>
                    New Branch...
                </button>
            {/if}
        </div>
    {/if}
</div>

<style>
    .branch-selector {
        position: relative;
        font-family:
            "Inter",
            system-ui,
            -apple-system,
            sans-serif;
    }

    .branch-btn {
        display: flex;
        align-items: center;
        gap: 6px;
        background: rgba(255, 255, 255, 0.05);
        border: 1px solid rgba(255, 255, 255, 0.1);
        color: #e2e8f0;
        padding: 4px 10px;
        border-radius: 4px;
        font-size: 12px;
        font-weight: 500;
        cursor: pointer;
        transition: all 0.2s;
    }

    .branch-btn:hover {
        background: rgba(255, 255, 255, 0.1);
        border-color: rgba(255, 255, 255, 0.2);
    }

    .branch-name {
        max-width: 120px;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .chevron {
        opacity: 0.6;
    }

    .branch-dropdown {
        position: absolute;
        top: 100%;
        left: 0;
        margin-top: 4px;
        width: 220px;
        z-index: 100;
        background: #1e293b;
        border: 1px solid rgba(255, 255, 255, 0.1);
        border-radius: 6px;
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5);
        padding: 4px 0;
        display: flex;
        flex-direction: column;
    }

    .dropdown-header {
        font-size: 10px;
        text-transform: uppercase;
        letter-spacing: 0.5px;
        color: #94a3b8;
        padding: 6px 12px;
        font-weight: 600;
    }

    .branch-item {
        display: block;
        width: 100%;
        background: none;
        border: none;
        color: #e2e8f0;
        padding: 6px 16px;
        text-align: left;
        cursor: pointer;
    }

    .branch-item-name {
        font-weight: 500;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .branch-item:hover {
        background: rgba(255, 255, 255, 0.05);
    }

    .branch-item.active {
        background: rgba(56, 189, 248, 0.1);
        color: #38bdf8;
    }

    .new-branch-btn {
        display: flex;
        align-items: center;
        gap: 6px;
        width: 100%;
        background: none;
        border: none;
        padding: 8px 16px;
        font-size: 12px;
        cursor: pointer;
        text-align: left;
        color: #cbd5e1;
    }

    .new-branch-btn:hover {
        color: #f8fafc;
        background: rgba(255, 255, 255, 0.05);
    }

    .new-branch-form {
        padding: 8px 12px;
        display: flex;
        flex-direction: column;
        gap: 4px;
    }

    .new-branch-input {
        background: rgba(0, 0, 0, 0.3);
        border: 1px solid rgba(255, 255, 255, 0.2);
        color: #f8fafc;
        border-radius: 4px;
        padding: 6px;
        font-size: 12px;
        width: 100%;
        box-sizing: border-box;
    }

    .new-branch-input:focus {
        outline: none;
        border-color: #38bdf8;
    }

    .new-branch-hint {
        font-size: 10px;
        color: #94a3b8;
    }
</style>
