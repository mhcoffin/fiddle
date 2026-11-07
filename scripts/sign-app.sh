#!/bin/bash
# Sign FiddleServer's executable with a stable code-signing identity.
#
# macOS keeps privacy permissions (such as access to ~/Documents) per code
# signature. The linker's ad-hoc signature changes on every build, so macOS
# treats each build as a new app and asks again. A certificate-based
# signature keeps the same identity across builds, so permission is granted
# once. See docs/code-signing.md.
#
# Usage: sign-app.sh <path to FiddleServer executable> <bundle ID>
#
# Identity: $FIDDLE_CODESIGN_IDENTITY if set, otherwise a valid "Fiddle Local"
# certificate, otherwise a valid "Apple Development" certificate. With none,
# the build stays ad-hoc signed and this prints how to set one up. Signing
# never fails the build.
#
# The hardened runtime is deliberately not enabled: its library validation
# would stop Fiddle from loading third-party plug-ins.

executable="$1"
bundle_id="$2"

if [ ! -f "$executable" ]; then
  echo "sign-app: $executable not found; not signing" >&2
  exit 0
fi

identity="${FIDDLE_CODESIGN_IDENTITY:-}"
if [ -z "$identity" ]; then
  # A self-signed "Fiddle Local" certificate needn't be trusted: macOS
  # matches the certificate itself. Skip revoked or expired ones.
  identity="$(security find-identity -p codesigning 2>/dev/null |
    grep '"Fiddle Local' | grep -v -E 'REVOKED|EXPIRED' | head -1 | awk '{print $2}')"
fi
if [ -z "$identity" ]; then
  identity="$(security find-identity -v -p codesigning 2>/dev/null |
    grep '"Apple Development' | head -1 | awk '{print $2}')"
fi

if [ -z "$identity" ]; then
  echo "sign-app: no code-signing certificate; FiddleServer stays ad-hoc signed," >&2
  echo "sign-app: so macOS will ask again for folder access after this build." >&2
  echo "sign-app: See docs/code-signing.md to set one up." >&2
  exit 0
fi

# Sign a standalone copy, as the linker does. Signed in place, codesign treats
# the executable as the whole bundle and rejects the scripts and other files
# the build copies into Contents/MacOS.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$executable" "$work/FiddleServer"
if codesign --force --sign "$identity" --identifier "$bundle_id" --timestamp=none \
     "$work/FiddleServer" 2>&1 && cp "$work/FiddleServer" "$executable"; then
  echo "sign-app: signed FiddleServer with $identity"
else
  echo "sign-app: WARNING: signing failed; FiddleServer stays ad-hoc signed" >&2
fi
exit 0
