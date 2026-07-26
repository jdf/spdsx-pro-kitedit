# Live QA: the three-way device sync

Status after the 2026-07-21 hardware session: tests **1–3 passed** (single
param push incl. the power-cycle proof, rename, pool-wave reassign; the
download path too). That session also caught the oversized write-frame bug
— uploads now chunk at 64 KiB, re-verified byte-exact via spdutil — but the
full in-app upload (test 5) has not run since. **Remaining: tests 4–8.**
Test 0 is per-session pre-flight; redo it each time.

Work top to bottom — each test builds on the state the previous one leaves.
Do everything on the scratch kits (**198, 199** — 199 is already "CLAUDE WAS
HERE" litter) and never on a kit you care about. The unit's own WRITE/commit
makes changes durable, so mistakes on scratch kits are still reversible by
hand, but only scratch kits keep that cheap.

**Watch items** (the two things the fake port could not prove):
- ~~**(A) Clearing a layer**~~ RESOLVED 2026-07-26: writing wave `0` with the
  enable flag still `1` clears it; the unit shows "0 OFF".
- ~~**(B) Uploaded audio plays**~~ RESOLVED 2026-07-26: `SmpFromAudioFile`
  output through the full GUI path uploads, registers, and plays on the pad
  at the right pitch.

## 0. Pre-flight

- [ ] `./presubmit.sh` green; run the app from `build/…/SPD-SX PROgram.app`.
- [ ] **Back up the document**: `cp ~/Documents/"SPD-SX PRO.spdsx" ~/Documents/"SPD-SX PRO.spdsx.pre-qa"` (adjust if your doc lives elsewhere).
- [ ] Quit the official Roland app (one program per port).
- [ ] Plug the device; wait for the header dot to go **green** (poll is 2s).
- [ ] Note for the whole session: **quit the app before running `spdutil`** —
      the app's connection poll opens the port every 2s and the two will
      race. (Or just expect an occasional retry.)
- [ ] **File > Load Device State** once, even if the doc looks current: your
      real document may predate the base snapshot (no base rows = all 200
      kits read dirty and the conflict dialog would be a wall). After the
      load: **the Sync button must be hidden** (current == base). If it
      shows a "(N kits)" count here, stop — that's a bug, note the count.

## 1. Single param edit (the smallest push)

- [x] Switch to kit 199. Open pad 3's header, change **fade point** to a
      value you'll recognize (e.g. 77).
- [x] Sync button appears, plain label (one kit). Click it.
- [x] Status walks: `sync: reading device state…` → `sync: saving to
      device…` → `synced with device`; button disappears.
- [x] Quit app. `./build/spdutil kit 199` → pad 3 fade point = 77.
- [x] **Power-cycle the unit**, `spdutil kit 199` again → still 77 (proves
      the flash commit in this flow, not just working state).

## 2. Kit rename

- [x] Relaunch app (doc reopens; button should be hidden). Rename kit 199
      via the pencil. Push. Verify the name on the unit's kit screen (or
      `spdutil kits | grep 199`).

## 3. Reassign an existing pool wave

- [x] Device tab → drag any pool wave onto kit 199 pad 6 top. Push.
- [x] Hit pad 6 on the unit → plays that wave.

## 4. Clear a layer — watch item (A) — PASSED 2026-07-26

- [x] Cleared kit 199 pad 6 top (right-click ▸ Clear layer), pushed. The
      device reads wave 0 on both layers, neighbours untouched, and the
      document's current AND base both advanced. On the unit both layers
      read "0 OFF". **Writing wave 0 with the enable flag left at 1 does
      clear a layer** — no enable=0 needed. Watch item (A) closed.

## 5. Local file upload — watch item (B) — PASSED 2026-07-26

- [x] Dragged specgram/test.wav (44.1 kHz mono, 4.000 s) onto kit 199 pad 6
      top and pushed. Landed at 1677, the lowest free index, named
      test/test.wav; kit 199 pad 6 top points at it; the app shows a device
      wave, the Device tab lists it.
- [x] **Audible on the pad**, correct pitch and speed. `readwave 1677` comes
      back `valid=1  48000 Hz  1 ch  16-bit  384000 PCM bytes (4.00 s)` —
      192000 samples is exactly 4.000 s at 48k, so the resample kept the
      duration (a reinterpret would have given 3.67 s) and the MD5 header
      checksum verifies. Watch item (B) closed.
- [ ] Still untried: a stereo file, and an mp3.

## 6. Pull: a device-only change rides along

- [ ] With everything clean: `./build/spdutil setname --kits 199 --name "UNIT SIDE" --commit`
      (app quit while you do it). Relaunch app — button stays hidden (the
      app can't see device changes without a sync).
- [ ] Make any small edit on kit **198** so the button appears. Push.
- [ ] Expect NO dialog. After: kit 199's name in the kit chooser reads
      "UNIT SIDE" (pulled), kit 198's edit is on the unit, button hidden.

## 7. The conflict dance

Conflict on the pad's **layer mode** — the combo in every pad header.
(Fade point would do too, but it is hidden on MIX pads, which is what
kit 199's pads are.) Each round: make the app-side edit, quit, let Claude
stage the device side (`spdutil setparams --kits 199 --pad 5 --params
<MODE>,80,127,1,LINEAR,127,80,25,75,0 --commit`, which moves only the
mode), relaunch, push. The conflict is the SAME field changed to
DIFFERENT values on both sides, the one thing the three-way merge cannot
decide for itself.

- [x] **Device's wins.** PASSED 2026-07-26. App XFADE vs device SWITCH,
      base MIX. Keep Device's → Sync: the device was NOT written (still
      SWITCH), the document's current AND base both became SWITCH, and
      the rest of pad 5 was untouched — the merge is field-wise.
- [x] **Mine wins.** PASSED 2026-07-26. App XFADE vs device ALTERNATE,
      base SWITCH. Left as "Mine" → Sync: the device was written to
      XFADE and both snapshots advanced.
- [x] **Do Nothing.** PASSED 2026-07-26. App FADE1 vs device ALTERNATE,
      base XFADE. Do Nothing → Sync: all three stayed different, base
      deliberately NOT advanced, the button stayed visible, and pushing
      again re-showed the same conflict.
- [x] **Escape.** PASSED 2026-07-26. Escape on the dialog aborted the
      sync outright — device, current and base all unchanged.
- [x] **Bulk controls.** PASSED 2026-07-26. Three conflicts (pads 5-7):
      all rows start ticked and "Mine"; Select all clears and re-ticks;
      with none ticked the three bulk buttons disable; unticking one
      shows the dash (mixed) state; with two ticked, Keep Device's
      changed ONLY those two. Syncing that mix pushed pad 5 (Mine) and
      pulled pads 6-7 (Device's), each advancing its base.

## 8. Pull the cable mid-push (failure honesty)

- [x] PASSED (with a fix) 2026-07-26. Staged an upload plus pad edits on
      kit 199, yanked USB during "saving to device". The error surfaced,
      the document stayed dirty, and a push after replugging succeeded.
      **But the sample was uploaded TWICE** (pool 1678 and 1679): uploads
      were held until the batch commit confirmed, so an interrupted sync
      recorded nothing and the retry uploaded again, leaking a pool slot
      each time. Fixed: ExecutePush now reads each upload back off the
      device and compares before reporting it, and the report records it
      at once — so a retry resumes instead of re-uploading, and a partial
      upload is detected rather than assumed. **Re-run this test to
      confirm the sample lands once.**

## 9. Wrap-up

- [ ] Quit + relaunch: dirty/clean state survives (it's computed from the
      DB, not runtime flags).
- [ ] Optional litter sweep: kit 129 "TRACER XYZZY", kit 199, pool
      1590–1595 + today's QA uploads (`spdutil deletewave <N>`).
- [ ] Delete the `.pre-qa` doc backup if all is well; update CLAUDE.md's
      open-items (watch items A/B) and the project memory with what passed.
