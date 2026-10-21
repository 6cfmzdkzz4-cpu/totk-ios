# Third-party code

Code in this repository that comes from other projects keeps its license; the license file sits next to the code.

| Code | From | License | File |
|---|---|---|---|
| `games/totk/third_party/d2r` (NSO rewriter, verification tools) | [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), adapted | MIT | `games/totk/third_party/d2r/LICENSE` |
| `games/totk/third_party/d2r_gfx` (NVN on Metal, texture tools, host headers) | d2r-mobile, extended for this game | MIT | `games/totk/third_party/d2r_gfx/LICENSE` |
| texture layout sizes in `games/totk/third_party/d2r_gfx/gfx/nvn/nvn_tex.c` | deko3d | zlib | `games/totk/third_party/d2r_gfx/LICENSE.deko3d` |
| `games/totk/third_party/d2r_gfx/third_party/bcdec` | [bcdec](https://github.com/iOrange/bcdec) 0.985 | MIT or public domain | `games/totk/third_party/d2r_gfx/third_party/bcdec/LICENSE` |
| `games/totk/tools/totk_xlate` (shader translator front end) | d2r-mobile, adapted | MIT | `games/totk/tools/totk_xlate/LICENSE` |
| `vendor/ryujinx` (Ryujinx.Graphics.Shader, Ryujinx.Common, Spv.Generator) | Ryujinx, as pinned by d2r-mobile; patched by `games/totk/tools/totk_xlate/patches` | MIT | `vendor/ryujinx/LICENSE.txt` |
| Locally built `totk_xlate` host tool (not distributed here) | built from the listed translator sources by `publish_native.sh`; NativeAOT runtime components come from the local .NET 10 SDK | MIT and .NET third-party licenses in the SDK; retain them if redistributing a host tool | `vendor/ryujinx/LICENSE.txt`, `games/totk/tools/totk_xlate/LICENSE`; local .NET SDK LICENSE and ThirdPartyNotices |
| audio effects in `games/totk/runtime/audio_reverb.c`, `games/totk/runtime/audio_effects.c` (delay, reverb, compressor) and the mixer | ported from Ryujinx.Audio | MIT | `games/totk/third_party/licenses/Ryujinx-Audio.txt` |

Fetched or linked at build time (not in this repository):

| Code | Use | License |
|---|---|---|
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) at `aa217ae` | SPIR-V to Metal Shading Language | Apache-2.0 |
| [hactool](https://github.com/SciresM/hactool) at `1d64a83` (with explicit private title-key-file hook), including its bundled mbedTLS | Base + update BKTR merge at build time only | ISC (hactool), Apache-2.0 (mbedTLS); see fetched source LICENSE files |
| [libopus](https://github.com/xiph/opus) at `22244de5a79bd1d6d623c32e72bf1954b56235be`, built from pinned source for iOS; Homebrew library for the build-time Mac host | Opus audio, linked statically | BSD-3-Clause |
| [libvpx](https://github.com/webmproject/libvpx) at `1024874c5919305883187e2953de8fcb4c3d7fa6`, built from pinned source for iOS; Homebrew library for the build-time Mac host | VP9 movies, linked statically | BSD-3-Clause |

Added build-time components and preparation tools:

| Code | From | License |
|---|---|---|
| `vendor/totk-ultrawide` (two authored patch templates, conversion/layout scripts) | [Fruithapje21/TOTK-Ultrawide](https://github.com/Fruithapje21/TOTK-Ultrawide), commit `194d4b3bd66db06a863b34c3af829e662cffe23d` | GPL-3.0, retained at `vendor/totk-ultrawide/LICENSE` |
| `tools/prepare_display.py` | Local integration of the separately licensed TOTK-Ultrawide build-time component | GPL-3.0-only; see the same license |
| EVFL parser, fetched only into private work/tooling | [zeldamods/evfl](https://github.com/zeldamods/evfl), pinned commit `e996fb18b389d584811f7653ca0646074b404d59` | GPL-2.0-or-later; see the fetched source LICENSE; no parser binary or game event files are distributed here |

The upstream source license and copyright notices are preserved. Shader-list
identifiers and normalized numeric pipeline descriptions contain no shader
instructions or game resources. The new feedback generators, controller policies
and personal build/signing helpers are source; their generated game-dependent
headers and assets are excluded. No NativeAOT host binary is included in this
source repository.

`games/totk/third_party/d2r_gfx/gfx/nvn/include/nvn.h` retains the MIT notice
Copyright (c) 2020 h1k421. It derives from
[open-ead/nnheaders](https://github.com/open-ead/nnheaders/blob/fed45e2614b626f62809d0e1319496f51bc09871/include/nvn/nvn.h),
commit `fed45e2614b626f62809d0e1319496f51bc09871` (original NVN definition
commit `c90a1275242d4eb7f68bd6afacd8861789de06d7`). Local changes adapt two opaque
object sizes to the verified TOTK ABI. The complete license remains in the header.
