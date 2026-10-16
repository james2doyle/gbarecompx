# gbarecompx tools

Framework-side tooling. Two kinds live here:

- **Build/codegen binaries** (`gba_recompile`, `gba_scan`, `bios_smoke`,
  `test_rom_runner`) — CMake outputs, see the repo root README.
- **Python protocol tools** — they speak the framework's own formats and
  surfaces (the TCP debug protocol, `GBARECOMP_FP_SAVE` instruction rings,
  `# gbarecomp-keyinput-v1` input traces, the self-heal cache, the mGBA
  oracle, GBA static analysis). **Run them from a game checkout** — the
  framework convention is that the consumer repo root holds `game.toml`,
  `game.gba`, `bios/`, `generated/`, `logs/` and the built runner under
  `build/`; the tools default to those relative paths and accept overrides.

| Tool | What |
|---|---|
| `framediff.py` | Native-vs-oracle state comparison against the framework's mGBA oracle (`oracle/`) |
| `dualrun.py` | Lockstep native-vs-oracle probes (run/prove/px subcommands) |
| `resolve.py` | The healing-loop driver: strict replay of an input trace, harvesting misses into a proposal overlay |
| `misspack.py` | Turn a self-heal miss fragment into per-PC evidence packs (disassembly + TOML stubs) |
| `cache_harvest.py` | Harvest the heal cache (`recomp_cache/`) into a proposal fragment |
| `ringscan.py` | Query a `GBARECOMP_FP_SAVE` instruction ring (by pc / reg / cycles, with context disassembly) |
| `trace_split.py` | Split a keyinput trace into monotonic segments (mid-session reloads) |
| `quit.py` | Cleanly close a running runner session over the TCP surface |
| `livewatch.py` | Frame-stall watchdog for a live windowed session |
| `disarm.py` | Disassemble a window of the game's ROM as ARM or Thumb (evidence workflow) |
| `m4a_detect.py` | Detect the m4a/MusicPlayer2000 (Sappy) audio engine in a GBA ROM |
| `analyze_frame_phase.py` | Analyze a `GBARECOMP_FRAME_PHASE` frame-phase CSV |
| `analyze_present_cadence.py` | Analyze a present-cadence CSV (MC-WS-002) |
| `cli.py` / `build_cli.py` | Release-CLI helpers |

Tests: `tools/tests/` (stdlib unittest; the CI `guard` job runs the
discovery plus the source-shape guards' self-tests). Game-specific
harnesses — attract corridors, route fixtures, device gates, release
packaging — belong to each game's repo, not here.
