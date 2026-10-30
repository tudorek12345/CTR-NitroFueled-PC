# Licenses, attribution and proprietary content

The port and these input/runtime contributions remain **GPL-2.0-or-later** under
[LICENSE](LICENSE) and [CONTRIBUTING.md](CONTRIBUTING.md). No ownership of upstream code,
game content or third-party trademarks is claimed. Existing copyright notices remain in place.
This change does not relicense any component.

## Components added by this change

| Component | Origin | License |
| --- | --- | --- |
| `scripts/buttons.py` | [martijnhe/OCTR-NitroFueled-PC-EU](https://github.com/martijnhe/OCTR-NitroFueled-PC-EU/blob/bc034670949fe629685b36e2c50044ceee642ed7/scripts/buttons.py); adapted here for base-game font codes and bounds checks | GPL-2.0-or-later (SPDX header retained) |
| `scripts/iga.py` | [Same repository and commit](https://github.com/martijnhe/OCTR-NitroFueled-PC-EU/blob/bc034670949fe629685b36e2c50044ceee642ed7/scripts/iga.py), copied unchanged; original Git blob `1fa762f28d249d565dd177ff801f2cd09d404ccf` | GPL-2.0-or-later (SPDX header retained) |
| `assets/button_icons/xbox.png` | Nicolae "Xelu" Berbece / Those Awesome Guys; sheet arranged in the community repository above | CC0-1.0; [asset attribution](assets/button_icons/README.md) |
| `tools/xbox_button_icons.py`, input changes, mutex lifetime fix and regression tests | Contributions to this port | GPL-2.0-or-later |

Pillow is an optional dependency of the icon installation tool and is not bundled by this
change. Its [license](https://github.com/python-pillow/Pillow/blob/main/LICENSE) applies separately.

## Existing dependencies and proprietary material

| Component | License or status | Where the existing notices live |
| --- | --- | --- |
| SacredFire, bbport and shadPS4 code | GPL-2.0-or-later | `LICENSE`, `gpu/shadps4/LICENSE`, [README credits](README.md#credits-and-licenses) |
| FSR-Vulkan, AMD FidelityFX SDK, Dear ImGui, LibAtrac9, miniz and other dependencies | Their existing licenses; the named components are MIT | Respective source/submodule license files; README credits |
| SDL3, winpthreads, FFmpeg and other Windows runtime DLLs | Per-library licenses, not one blanket license | Portable packager generates `THIRD-PARTY.txt` with MSYS2 package/version/license details |
| DLSS bridge by IFreemz | MIT; covers the bridge directory only | `dlss_bridge/LICENSE.txt` |
| NVIDIA DLSS SDK / `nvngx_dlss.dll` | NVIDIA's separate proprietary SDK license; not covered by the port's GPL or the bridge's MIT license | Optional existing portable packaging retains `licenses/NVIDIA-DLSS-SDK-LICENSE.txt`; SDK source is not added here |
| Optional FSR 4 v07 model/shader bundle | Existing packaging identifies it as MIT, copyright AMD | `fsr4_shaders/LICENSE-FSR4-v07.txt` when supplied; no model/binary added by this change |
| CTR Nitro-Fueled executable, archives, music, art, fonts, videos, updates, DLC, PKGs and saves | Proprietary game content; not licensed by this repository | Supplied locally by the player; none is committed or published here |

The icon tool reads the player's fonts and creates modified archives locally. Those generated
archives still contain game content and are excluded from this source change. The CC0 icon
sheet does not make the game's font files public domain.

*Crash Team Racing*, *Nitro-Fueled*, PlayStation, Xbox, NVIDIA, DLSS and other product names
remain their owners' trademarks. As stated in the README, this community port is not affiliated
with or endorsed by Sony Interactive Entertainment, Activision or Beenox.
