# BIOS

The **default launch BIOS is the clean-room emibios build**
(LGPL-3.0-or-later, https://github.com/coolbho3k/emibios) — a 2026-10
fork decision. The consumer repo ships its own committed copy (the FFTA
recomp keeps `bios/emibios_gba_bios.bin` plus its recompile TOML at the
repo root), and the runtime's default SHA-1 gate (`kEmibiosSha1`)
expects it; a game's `[bios].sha1` pin overrides the expectation, and
`--bios <path>` overrides the path.

## The retail dump (optional, local-only)

A dump of Nintendo's retail BIOS may be placed here as `gba_bios.bin`.
It is **not in git** (copyrighted Nintendo code) and it is **no longer
the runtime's default** — the `.toml` / `.md` files in this folder ARE
tracked, so the path layout matches between developer machines. The
retail dump remains the byte-exact reference for:

- the framework's mGBA oracle (`oracle/`) and co-simulation tooling,
- byte-pin tests that assert retail-BIOS facts,
- BIOS-accuracy work: `gba_bios.toml` / `gba_bios_excltest.toml`
  (tracked) are the recompile recipes for it.

| Field    | Value (retail dump, if you supply one)                                |
|----------|-----------------------------------------------------------------------|
| File     | `bios/gba_bios.bin`                                                   |
| Size     | 16384 bytes (16 KB)                                                   |
| SHA-1    | `300c20df6731a33952ded8c436f7f186d25d3492`                            |
| MD5      | `a860e8c0b6d573d191e4ec7db1b1e4f6`                                    |

The runtime accepts either image (the game's `[bios].sha1` pin decides
what "correct" means for that game). Netplay requires all linked
machines to run byte-identical BIOS bytes — in practice, the build
default (emibios).
