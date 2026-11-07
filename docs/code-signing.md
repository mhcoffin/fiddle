# Code signing for local builds

## Why

macOS keeps privacy permissions, such as access to `~/Documents`, per code
signature. The linker gives `FiddleServer` an ad-hoc signature that changes on
every build, so macOS treats each build as a new app and asks again.

The prompt is easy to miss. While it waits, the plug-in that triggered it
(Vienna Synchron Player reads `~/Documents` at load) blocks Fiddle's main
thread, so Fiddle looks hung at launch.

A certificate-based signature keeps the same identity across builds, so you
grant permission once.

## How the build signs

The last `FiddleServer` post-build step runs `scripts/sign-app.sh`. It signs
the executable with the first valid identity of:

1. `$FIDDLE_CODESIGN_IDENTITY`, if set when building
2. a certificate named `Fiddle Local…`
3. an `Apple Development…` certificate

With none, the build stays ad-hoc signed and prints a note. Signing never
fails the build. It doesn't enable the hardened runtime, whose library
validation would stop Fiddle loading third-party plug-ins.

The script signs a standalone copy of the executable and puts it back, as the
linker does. Signed in place, `codesign` treats the executable as the whole
bundle and rejects the scripts the build copies into `Contents/MacOS`.

The resulting designated requirement is the bundle ID plus the certificate,
for example `identifier "com.antigravity.fiddleserver" and certificate leaf =
H"…"`, so it doesn't change between builds.

`codesign` needs the login keychain, so a build run inside a sandbox that
blocks it stays ad-hoc signed; the build output says so.

Check the result:

```sh
codesign -dv build/release/FiddleServer_artefacts/Release/FiddleServer.app 2>&1 | grep -E "Identifier|Authority|Signature"
```

`Signature=adhoc` means no certificate was used.

## One-time setup

Create a certificate, either way:

- **Apple Development (free):** Xcode › Settings › Accounts, select your Apple
  ID, Manage Certificates…, then **+** › Apple Development.
- **Self-signed:** Keychain Access › Certificate Assistant › Create a
  Certificate…. Name it `Fiddle Local`, Identity Type Self-Signed Root,
  Certificate Type Code Signing. It needn't be trusted: macOS matches the
  certificate itself.

`security find-identity -p codesigning` should now list it.

Then:

1. Rebuild FiddleServer. If macOS asks whether `codesign` may use the key,
   choose **Always Allow**.
2. Launch Fiddle and allow Documents access once.

Later builds keep the permission. If macOS still asks after a rebuild, check
the signature as above. To reset the permission, run
`tccutil reset SystemPolicyDocumentsFolder com.antigravity.fiddleserver`.
