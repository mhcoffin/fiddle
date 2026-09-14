# Audio buffer SIMD audit

## Scope and implementation

The existing JUCE mixer paths already use optimized bulk operations: strip and
master gain use `AudioBuffer::applyGain`, and bus/strip mixing uses
`AudioBuffer::addFrom`. The audited macOS Release build links Accelerate/vDSP
operations for these paths; JUCE's min/max metering also emits NEON instructions.
These paths were left unchanged.

The remaining active transfer bottleneck was `AudioStreamRing`: scalar
interleaving/deinterleaving with a ring modulo operation for every frame.
Transfers now split once at the ring boundary and use `StereoBufferOps.h` on
each contiguous span. The helper uses ARM NEON or x86 SSE2 intrinsics, with a
portable C++ fallback. This keeps the native VST3 JUCE-free; adding JUCE's
`dsp::SIMDRegister` here would introduce an unnecessary dependency.

- Eight-frame SIMD batches, then an optional four-frame batch and short tail.
- Ordinary float alignment is sufficient; no vector-alignment requirement or
  padding reads/writes. No allocation, locks, or floating-point arithmetic.
- Missing output planes and mono input duplication are supported. Overlapping
  output planes retain the old complete-left-then-complete-right copy order.
- Shared-memory version 2, field layout, capacity, and atomic publication/clock
  protocol are unchanged. Old and new binaries remain compatible.
- The unused legacy `AudioSharedMemory.h` was not changed.

## Verification

Verified on Apple M2 Ultra with Apple clang 21.0.0, Release (`-O3 -DNDEBUG`):

- Server, native VST3, and test targets build successfully.
- All 56 stable CTest tests pass, including new SIMD and forced-fallback tests.
- Bit-exact transfers cover NaNs, infinities, signed zero, unaligned buffers,
  vector tails, ring wraparound, full-capacity transfers, missing/aliased output
  planes, rejected writes, silence, and shared-layout compatibility.
- AddressSanitizer/UndefinedBehaviorSanitizer pass for NEON and C++ fallback.
- ThreadSanitizer passes the existing concurrent render-ahead ring test.
- The x86-64 test runs successfully under Rosetta using SSE2. This is a
  correctness check, not an x86 performance measurement.
- Release production object code contains NEON `st2.4s` in the server producer
  and `ld2.4s` in the native plugin consumer. The x86 test contains SSE unpack
  instructions.

Build and run the regression suite:

```sh
cmake --build build/release --target FiddleServer FiddleNative FiddleTests --parallel 4
ctest --test-dir build/release --output-on-failure --no-tests=error -L stable
```

`FIDDLE_DISABLE_AUDIO_SIMD=1` selects the portable C++ implementation for the
fallback test. The compiler may still auto-vectorize that implementation.

## Transfer microbenchmark

The opt-in benchmark compares the original per-frame ring loops with the new
production ring implementation, including ring atomics. Buffers are warm and
not necessarily SIMD-aligned; results are medians of seven runs. Wrap cases
start three frames before the ring boundary.

```sh
cmake --build build/release --target StereoBufferBenchmark
./build/release/StereoBufferBenchmark
```

Representative local measurements (nanoseconds per stereo block):

| Frames | Wrap | Old write | New write | Write speedup | Old read | New read | Read speedup |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 512 | No | 242.11 | 78.37 | 3.09x | 339.10 | 88.93 | 3.81x |
| 512 | Yes | 240.70 | 64.38 | 3.74x | 335.06 | 84.04 | 3.99x |
| 1024 | No | 482.11 | 157.25 | 3.07x | 663.78 | 170.58 | 3.89x |
| 1024 | Yes | 504.42 | 127.28 | 3.96x | 664.27 | 164.20 | 4.05x |

These gains include removal of per-frame wrapping and paired-channel reads,
not just explicit SIMD instructions. They apply only to ring transfers, not
total audio-processing time. Small blocks have proportionally more fixed
overhead. Timing varies with machine load and is not a regression-test gate.

## Deployment status

Both Release artifacts were rebuilt but not installed or launched. No live
server, Dorico session, or project database was changed. To exercise both sides,
use the rebuilt server and install the rebuilt native VST3, then reload it in
the host. Updating only the server leaves the old plugin's read-side transfer
unchanged. An end-to-end audio smoke test remains pending.
