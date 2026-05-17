# TV-B-Gone XRemote (Flipper Zero)

A combined **TV-B-Gone-style universal off switch** and **per-brand full TV remote** for the Flipper Zero, sourcing the latest IR codes from the community-maintained Flipper-IRDB plus the upstream firmware's own universal `tv.ir`.

## What it does

- **Blast All TVs.** Walks every `Power` signal in the universal TV IR database and transmits each one in sequence, with a live counter and the name of the signal currently firing. Same idea as the classic TV-B-Gone keychain. Hold Back to abort.
- **Browse by Brand.** Lists every brand under `infrared/TVs/` on the SD card (the Flipper-IRDB layout), then every model `.ir` file inside that brand. Pick one and the Flipper turns into a real remote for that TV.
- **XRemote-style direct buttons.** In remote mode, physical buttons map straight to IR commands — no per-button menu scrolling:
  - **OK** → Power, **Long OK** → Mute
  - **Up / Down** → Vol +/-
  - **Left / Right** → Ch -/+
  - **Long Left** → Input, **Long Right** → Menu
  - Hold for auto-repeat on volume / channel
- **Alt-name fallback.** IRDB submissions are inconsistent (POWER, power, shutdown, off, on, standby — all the same button). The app resolves canonical names like `Power`, `Vol_up`, etc. against an alt-name table so messy files still work, the same way [kala13x/flipper-xremote](https://github.com/kala13x/flipper-xremote) does.

## Layout

```
tvbgone_xremote/
├── application.fam          # ufbt manifest (requires=["gui","storage","infrared"])
├── tvbgone_xremote.h        # shared types and prototypes
├── tvbgone_xremote.c        # app entry, view dispatcher, scene transitions
├── tvbgone_ir.c             # .ir file parsing, alt-name matching, IR transmit
├── tvbgone_fs.c             # directory enumeration on the SD card
├── blast_view.c             # TV-B-Gone screen — runs the Power blast in a worker thread
├── remote_view.c            # XRemote-style per-model controller view
├── icons/icon_10x10.png     # FAP icon
└── tools/
    └── sync_tv_codes.py     # pull latest codes from GitHub onto the SD
```

All sources sit in the app root because ufbt's default `sources=["*.c*"]` only scans the top level — subdirectories with C files would be silently skipped.

## Quick start (no Python needed)

The zip includes a pre-built `sd_payload/` directory with every brand from [Lucaslhm/Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) already staged for your SD card.

1. Build the FAP: `cd tvbgone_xremote && ufbt` → `dist/f7-D/tvbgone_xremote.fap`
2. Copy `tvbgone_xremote.fap` to `SD/apps/Infrared/` on your Flipper (use qFlipper, or `ufbt launch` to upload over USB).
3. Mount the Flipper SD on your computer (a card reader is much faster than qFlipper for thousands of small files).
4. Drag the **contents** of `sd_payload/` onto the SD root, merging with what's already there.
   - `sd_payload/infrared/TVs/` → `SD/infrared/TVs/` (118 brands)
   - `sd_payload/infrared/assets/tv.ir` → `SD/infrared/assets/tv.ir` (upstream universal blast list, 317 Power codes)
   - `sd_payload/apps_data/tvbgone_xremote/tv_extended.ir` → same path on SD (99 extra Power codes harvested from per-brand files)

That's it. Boot the Flipper, open the app, pick "Browse by Brand" → Samsung → your model, and you have a working remote.

## What's bundled

| Path on SD | Where it came from | Used by |
|---|---|---|
| `infrared/TVs/<Brand>/*.ir` | [Lucaslhm/Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) `TVs/` tree, fresh clone | Browse by Brand |
| `infrared/assets/tv.ir` | Upstream firmware ([`applications/main/infrared/resources/infrared/assets/tv.ir`](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/infrared/resources/infrared/assets/tv.ir)) | Blast All TVs (primary list) |
| `apps_data/tvbgone_xremote/tv_extended.ir` | Auto-generated: every unique Power code across all 397 IRDB TV files | Blast All TVs (extended list) |

Brands include: Samsung, LG, Sony, TCL, Hisense, Vizio, Panasonic, Philips, Sharp, Toshiba, Roku, Insignia, Element, Sceptre, Xiaomi, JVC, Sanyo, Pioneer, Hitachi, RCA, Magnavox, Bose, NEC, ViewSonic, plus 90+ regional and budget brands. See `sd_payload/infrared/TVs/` for the complete list.

## Refreshing the IRDB later

The bundled `sd_payload/` is a snapshot at zip build time. To pull a fresh copy from GitHub later (e.g. months from now after new TVs are added to IRDB), use the included Python script:

```bash
pip install --upgrade ufbt   # (only if you don't have git)
python3 tools/sync_tv_codes.py --sd /Volumes/Flipper       # macOS
python3 tools/sync_tv_codes.py --sd E:\                    # Windows
python3 tools/sync_tv_codes.py --sd ~/flipper-sd           # mounted elsewhere
```

It clones [Lucaslhm/Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) plus the upstream firmware, copies the TVs tree to your SD, and rebuilds the extended blast file. Requires git in PATH; no other dependencies.

## Notes & caveats

- **Stack size.** Manifest reserves 4 KiB; if you load very large remote files and see crashes, bump `stack_size` to `8 * 1024` in `application.fam`.
- **Worker thread.** Blast runs in a separate `FuriThread` so the UI stays responsive and Back actually aborts; otherwise long blast sessions would lock the input loop.
- **Delay between signals.** 150 ms in `blast_view.c` (`INTER_SIGNAL_DELAY_MS`). Lower = faster but some TVs need a beat to react; original TV-B-Gone uses ~250 ms.
- **Raw signals.** The parser handles both `parsed` (protocol/address/command) and `raw` (frequency/duty/data) entries. Some IRDB files use unusual protocols that decode to `Unknown`; those entries are skipped silently rather than crashing.
- **Legal.** Use only on TVs you own or have permission to control. Blasting random TVs in public spaces is, depending on jurisdiction, anywhere from rude to a misdemeanor.

## Possible extensions

- Favourites list (pin specific brand/model files for one-tap access from the main menu)
- Configurable blast delay & order (random shuffle helps with crowded rooms where slow sequential broadcast hits the same TV multiple times)
- Per-button screen mode toggle (XRemote actually offers a full grid view too)
- Audio receiver / projector / soundbar modes — same architecture, different IRDB subfolder (`AVRs/`, `Projectors/`, `Soundbars/`)
