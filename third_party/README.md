# Third-party libraries for zplayer

These libraries let the player run without Allegro (SDL3 + Vulkan). ZQuest still uses
the Allegro libraries under `libs/` and `allegro/`.

| Library | Version | Source | Kept | License |
|---|---|---|---|---|
| SDL3 | 3.4.20 | `SDL3-3.4.20.tar.gz` from github.com/libsdl-org/SDL releases, sha256 `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f` (matches GitHub's published digest) | Everything except `test/`, `examples/`, `Xcode/`, `VisualC/`, `VisualC-GDK/` | zlib (`SDL3/LICENSE.txt`) |
| Vulkan-Headers | vulkan-sdk-1.4.363.0 | GitHub source archive of that tag, sha256 `4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e` | C headers in `include/vulkan/` and `include/vk_video/` (no C++ headers, no registry) | Apache-2.0 / MIT (`Vulkan-Headers/LICENSES/`) |
| volk | 1.4.350 | GitHub source archive of that tag, sha256 `e47c6efe5294bb03729a976b385864bba5daf46ec60f0bdea11e1d1446345f9a` | `volk.h`, `volk.c` | MIT (`volk/LICENSE.md`) |
| stb_vorbis | v1.22 (file last changed in commit `1ee679ca2ef753a528db5ba6801e1067b40481b8`, 2021-07-12) | `stb_vorbis.c` from github.com/nothings/stb at that commit, sha256 `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636` | `stb_vorbis.c` | public domain or MIT (end of the file) |
| minimp3 | commit `ea99364f61c14656440e8d77e9c233ccf3124633` (2026-07-27) | github.com/lieff/minimp3 at that commit: `minimp3.h` sha256 `57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad`, `minimp3_ex.h` sha256 `8437f3fc1d4d8ab2269a1624f5380a08df1967a048f8887789bae6e25db7db79` | `minimp3.h`, `minimp3_ex.h`, `LICENSE` | CC0-1.0 (`minimp3/LICENSE`) |
| Game_Music_Emu (libgme) | 0.6.5 | `libgme-0.6.5-src.tar.gz` from github.com/libgme/game-music-emu releases, sha256 `a133f19278222136ba0d8c27b64a07987ba05fec9d2e6d293ccd8cabdd97ddbb` | `gme/` sources (minus its CMakeLists.txt, gme.exports, libgme.pc.in), `license.txt`, `license.gpl2.txt`, `readme.txt`, `changes.txt`; no demo, player or test files. Built with the GENS YM2612 core only | LGPL-2.1 (`gme/license.txt`) |
| DUMB | 2.0.3 | GitHub source archive of tag 2.0.3 (codeload.github.com/kode54/dumb), sha256 `99bfac926aeb8d476562303312d9f47fd05b43803050cd889b44da34a9b2a4f9` | `include/dumb.h`, `include/internal/*.h` except `aldumb.h`, `src/core`, `src/helpers`, `src/it`, `LICENSE`, `README.md`; the Allegro glue (`src/allegro`, `aldumb.h`) is left out | DUMB license, zlib-like (`dumb/LICENSE`) |
| TinySoundFont | commit `853a0a171759f1ddba0de1442133a75912bbeffa` (2026-07-19) | github.com/schellingb/TinySoundFont at that commit: `tsf.h` sha256 `70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197`, `tml.h` sha256 `93257db259e0efb2ea2037d7157841bec8cb4a2d7986286e43c8090705326546` | `tsf.h`, `tml.h`, `LICENSE` | MIT (`tinysoundfont/LICENSE`) |
| Dear ImGui | v1.93.0 (released 2026-10-14) | github.com/ocornut/imgui at tag v1.93.0: `imgui.cpp` sha256 `9542082a1eaeffcab7b3d8069e707839ce1714d167005e9f273ac9f656b415ed`, `imgui.h` sha256 `314462301948fbd5673f5ead6f9a53acf54767cbee57e186e69f8f8b022c7094`, `backends/imgui_impl_vulkan.cpp` sha256 `192a1db32ad86cb5413b9d13766a381d620a7e184503fc045422ac593f9037af`, `backends/imgui_impl_sdl3.cpp` sha256 `306060dffa276b574f0541194e8e079c1685c29c1095602a3972de0bb23171e7` | core sources and headers (`imgui*.cpp/h`, `imstb_*.h`, `imconfig.h`), `backends/imgui_impl_sdl3.*`, `backends/imgui_impl_vulkan.*`, `LICENSE.txt`; no demo, examples or other backends | MIT (`imgui/LICENSE.txt`) |

Shaders are compiled to SPIR-V with glslang 16.6.0
(`glslang-16.6.0-windows-x86_64-release.zip`, sha256
`82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b`). The compiled SPIR-V
is checked in, so building the player does not need glslang.

The music libraries are built by `src/port/audio_libs.cmake` (targets `zc_stb_vorbis`, `zc_minimp3`, `zc_gme`, `zc_dumb`, `zc_tsf`). They replace the Allegro add-ons the Allegro build uses for enhanced music (alogg, almp3, aldumb); Game_Music_Emu and DUMB are the same libraries the Allegro build links, in newer versions.
