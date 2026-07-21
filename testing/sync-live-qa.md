# Live QA: the three-way device sync (built 2026-07-21, never run against hardware)

Work top to bottom — each test builds on the state the previous one leaves.
Do everything on the scratch kits (**198, 199** — 199 is already "CLAUDE WAS
HERE" litter) and never on a kit you care about. The unit's own WRITE/commit
makes changes durable, so mistakes on scratch kits are still reversible by
hand, but only scratch kits keep that cheap.

**Watch items** (the two things the fake port could not prove):
- **(A) Clearing a layer**: we write wave `0` with the enable flag still `1`.
  The official app may write enable `0` instead. Symptom would be a pad that
  shows something odd on the unit's wave screen or still plays the old wave.
- **(B) Uploaded audio plays**: registration was live-verified last session,
  but this is the first time `SmpFromAudioFile` output (resampled PCM +
  checksum) goes through the full GUI path. Silent playback = checksum or
  resample bug.

## 0. Pre-flight

- [ ] `./presubmit.sh` green; run the app from `build/…/spdsx-patchedit.app`.
- [ ] **Back up the document**: `cp ~/Documents/"SPD-SX PRO.spdsx" ~/Documents/"SPD-SX PRO.spdsx.pre-qa"` (adjust if your doc lives elsewhere).
- [ ] Quit the official Roland app (one program per port).
- [ ] Plug the device; wait for the header dot to go **green** (poll is 2s).
- [ ] Note for the whole session: **quit the app before running `spdutil`** —
      the app's connection poll opens the port every 2s and the two will
      race. (Or just expect an occasional retry.)
- [ ] **File > Load Device State** once, even if the doc looks current: your
      real document may predate the base snapshot (no base rows = all 200
      kits read dirty and the conflict dialog would be a wall). After the
      load: **the Save button must be hidden** (current == base). If it
      shows a "(N kits)" count here, stop — that's a bug, note the count.

## 1. Single param edit (the smallest push)

- [ ] Switch to kit 199. Open pad 3's header, change **fade point** to a
      value you'll recognize (e.g. 77).
- [ ] Save button appears, plain label (one kit). Click it.
- [ ] Status walks: `sync: reading device state…` → `sync: saving to
      device…` → `synced with device`; button disappears.
- [ ] Quit app. `./build/spdutil kit 199` → pad 3 fade point = 77.
- [ ] **Power-cycle the unit**, `spdutil kit 199` again → still 77 (proves
      the flash commit in this flow, not just working state).

## 2. Kit rename

- [ ] Relaunch app (doc reopens; button should be hidden). Rename kit 199
      via the pencil. Push. Verify the name on the unit's kit screen (or
      `spdutil kits | grep 199`).

## 3. Reassign an existing pool wave

- [ ] Device tab → drag any pool wave onto kit 199 pad 6 top. Push.
- [ ] Hit pad 6 on the unit → plays that wave.

## 4. Clear a layer — watch item (A)

- [ ] If the GUI lets you empty that slot, do it, push, then on the UNIT:
      pad 6's wave screen should show OFF/empty and the pad should be
      silent. If it shows garbage or still plays: capture the official
      app clearing a wave with `re/frida-scripts/paramlog.js` — we likely
      need enable=0 — and note it in CLAUDE.md.

## 5. Local file upload — watch item (B)

- [ ] Drag a small **44.1 kHz** WAV from the Files tab onto kit 199 pad 1
      top. Push. (This sync also re-reads the pool directory — bank 0x20 —
      so expect it to take noticeably longer than tests 1–3.)
- [ ] After: the slot shows a device wave (not a file), still plays in-app;
      Device tab lists the new wavename.
- [ ] `spdutil samples | tail` → the record sits at the lowest free index.
- [ ] **Hit the pad on the unit** → audible, correct pitch/speed (pitch or
      speed off = resample bug; silence = checksum bug).
- [ ] Repeat once with a stereo file and once with an mp3 if handy.

## 6. Pull: a device-only change rides along

- [ ] With everything clean: `./build/spdutil setname 199 --name "UNIT SIDE" --commit`
      (app quit while you do it). Relaunch app — button stays hidden (the
      app can't see device changes without a sync).
- [ ] Make any small edit on kit **198** so the button appears. Push.
- [ ] Expect NO dialog. After: kit 199's name in the kit chooser reads
      "UNIT SIDE" (pulled), kit 198's edit is on the unit, button hidden.

## 7. The conflict dance

- [ ] Clean state. In the app: kit 199 pad 5 fade point → 60. Quit app.
      On the device: change the SAME pad's fade point to 90 **on the
      unit's own UI** (that guarantees a one-field change; `spdutil
      setparams` works too but wants all 10 values). An easier CLI
      alternative that conflicts on the kit NAME instead:
      `./build/spdutil setname 199 --name "DEVICE NAME" --commit` after
      renaming the kit differently in the app. Relaunch, push.
- [ ] Dialog appears with **one row**: kit 199, pad 5, "fade point (yours
      60, device 90)" (or the two names, if you staged the name variant).
- [ ] Choose **Device wins** → ends clean; the app's pad 5 now shows 90;
      nothing written to the device for that pad.
- [ ] Re-stage the same conflict; choose **My copy wins** → unit ends at 60.
- [ ] Re-stage; choose **Do nothing** → status says `synced (skipped
      conflicts remain)`, button STAYS visible, and pushing again re-shows
      the same conflict.
- [ ] Re-stage; hit **escape** on the dialog → sync aborts, nothing changed
      anywhere, button re-enabled.

## 8. Pull the cable mid-push (failure honesty)

- [ ] Stage a multi-pad edit, push, and yank USB during `saving to
      device…`. Expect an error alert; the doc unchanged (still dirty).
- [ ] Replug, wait for green, push again → succeeds. If the failed run got
      far enough to upload a file first: confirm `spdutil samples` shows it
      **once**, not twice (uploads are recorded as they land).

## 9. Wrap-up

- [ ] Quit + relaunch: dirty/clean state survives (it's computed from the
      DB, not runtime flags).
- [ ] Optional litter sweep: kit 129 "TRACER XYZZY", kit 199, pool
      1590–1595 + today's QA uploads (`spdutil deletewave <N>`).
- [ ] Delete the `.pre-qa` doc backup if all is well; update CLAUDE.md's
      open-items (watch items A/B) and the project memory with what passed.
