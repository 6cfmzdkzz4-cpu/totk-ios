# Locally built shader translator

This source-only repository does not ship the upstream `totk_xlate` executable.
`tools/ios_build.py` builds it from the licensed source in `vendor/ryujinx` and
`games/totk/tools/totk_xlate`, using the .NET 10 SDK and NativeAOT. The output,
its checksum, SDK version and source fingerprint stay inside the personal work
folder. It contains no game code or game shaders.

The upstream checksum file is retained as provenance, not as the expected hash
of a local rebuild. NativeAOT output can vary with toolchain and build paths.
The build records the local executable's actual hash, and subsequent shader
preparation verifies it.
