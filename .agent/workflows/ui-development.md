---
description: How to work with the Svelte UI
---

# Svelte UI Development Workflow

The Svelte UI is located at `/Users/mhc/fiddle/Source/Server/ui`.

## Package Manager

**IMPORTANT**: This project uses **pnpm**, not npm. This was changed to avoid recurring npm bugs with optional dependencies (specifically `@rollup/rollup-darwin-arm64`).

## Common Commands

All commands should be run from `/Users/mhc/fiddle/Source/Server/ui`:

```bash
# Install dependencies
pnpm install

# Start development server
pnpm run dev

# Build for production
pnpm run build

# Preview production build
pnpm run preview
```

## Troubleshooting

### If you see "Cannot find module @rollup/rollup-darwin-arm64" error:

This is typically an IDE cache issue, NOT a missing dependency issue. The module is installed correctly.

**Solution**: Restart the Svelte Language Server in VS Code:
1. Press Cmd+Shift+P
2. Run "Svelte: Restart Language Server"

If the error persists after restart, the package is actually installed - check `node_modules/@rollup/rollup-darwin-arm64` to verify.

### Clean Reinstall

If you need to do a clean reinstall:

```bash
cd /Users/mhc/fiddle/Source/Server/ui
rm -rf node_modules pnpm-lock.yaml
pnpm install
```

**Note**: Delete `pnpm-lock.yaml`, not `package-lock.json` (npm's lockfile).

## Integration with Backend

The Svelte UI connects to the C++ Fiddle Server. Make sure the server is running when developing the UI.
