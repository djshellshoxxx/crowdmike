# Audit part C: GUI wiring, wrappers, build config, documentation

Branch `feat/audit-c`, base `main` 6b58610. Evidence is automated unless marked manual. Tests added:
`tests/engine/UiAuditTest.cpp`, `WrapperAuditTest.cpp`, `DocsConsistencyTest.cpp`. Set `CM_SNAPSHOT_DIR` to write a PNG of every page at
1100x650, 1280x720, 1600x900, 2560x1440, compact, high-contrast and 125/150 % scale; the PNGs were inspected for the new widgets.

## 1. GUI matrix

| Area | Check | Result | Evidence |
|---|---|---|---|
| All pages (Live, Inputs, Groups, FX incl. Stadium Reverb / Crowd Multiplier, Matrix, Record, Playback, Scenes, Settings, Diagnostics) | Every visible button has a handler or parameter attachment; sliders, combos, editors likewise | Fixed 1, then pass | `ui_audit_every_visible_control_is_wired_and_named` (pages x 8 inspector selections) |
| All pages | Every interactive control has an accessible title | Pass | same test |
| All parameters | Every (module, parameter) has a control on some page / inspector selection | Fixed 11 unreachable, then pass | `ui_audit_every_parameter_has_a_control` |
| Layout | No degenerate size, outside-parent, overlapping or truncated controls at 9 size/mode combinations x 10 pages | Fixed 5, then pass | `ui_audit_layout_at_supported_sizes_and_modes` |
| Status bar tempo widget, ducker strip, Line Check panel, Show Report panel, Playback tempo sync / streaming indicators | Wired and laid out | Pass (Show Report Tracking toggle added) | wiring + layout tests, PNGs |
| Keyboard shortcuts | Documented, cheat-sheet complete | Fixed (K, clip hotkeys missing from the sheet) | GUI_SPEC section 15, Settings page |
| Accessibility titles | present everywhere | Pass; screen readers remain manual | audit test |

### GUI defects found and fixed

1. **Settings > Tempo / Beat Sync section was never parented to the page**: laid out but invisible, so manual BPM, nudge, bar length, auto input and Resync were unreachable. Now in `sections()`.
2. **Record page track "#" select button had no handler** and swallowed the click (row selection never happened from it). Now selects the track.
3. **Parameters without any control**: rehearsal mode, output limiter ceiling / release / unlock, crowd-density-to-sends, ducker range / key HPF / key LPF / lookahead. New Settings section "Safety and global controls" (`GlobalSettingsSection`).
4. **Status bar at 1100 px**: sample rate, latency and CPU got zero width (badges overlapped them). Moved to the right of the tempo row; scene name takes the leftover width.
5. **Ducker strip at 1100 px / compact**: five sliders had no track. Stacks to two rows below 1350 px (`DuckerStrip::preferredHeight(width)`).
6. **Record page at 1100x650**: marker custom label, Export and Clear had height 0; Files rows now shrink to keep the Markers section; long captions shortened ("Performer", "Problem", "Export markers").
7. **Performance strip TAIL KILL only killed the echo tail**, while key K killed only the reverb tail. Both now drive both (echo + stadium reverb); REC button now reflects the recorder state; Show Report had no Tracking on/off control (`report.enabled` could only come from a loaded file): toggle added.
8. Tempo / Global settings cells were 26 px high (label ate half): 40 px cells.

Tests for 1 to 3, 7: `ui_audit_settings_sections_are_all_visible`, `ui_audit_strip_and_report_controls_have_effects`, the wiring and coverage tests.

## 2. Wrappers and build

| Item | Result | Evidence |
|---|---|---|
| Bus layouts | Main bus 1 to 16 ch each way, disabled / over-limit rejected, no aux buses; 1x1, 1x2, 2x2, 8x8, 16x16 process finite audio | `wrapper_bus_layouts_*`, `wrapper_every_supported_layout_*` |
| Latency report | Constant at 44.1/48/96/192 kHz, unchanged by ceiling/bypass/panic/unlock, estimate = latency + block | `wrapper_latency_is_reported_and_constant` |
| Tail length | >= 4 s, grows with reverb decay | `wrapper_tail_length_*` |
| State restore | Exact parameter round trip; empty, garbage and foreign blobs ignored | `wrapper_state_restore_is_robust_and_exact`; existing golden v2 test |
| Host automation count / order | List equals `ParamBank` order, module enum frozen (Macro, FxReverb, FxMulti, Tempo appended), all automatable and named, bypass parameter | `wrapper_host_parameter_count_and_order_are_stable` |
| MIDI acceptance | plug-ins no, standalone yes; OSC sockets only in standalone | `wrapper_midi_*`, `standalone_osc_*` |
| CMake | VST3 `Fx Spatial`, ids frozen, explicit `NEEDS_MIDI_INPUT FALSE`, description added; CLAP id `com.circuitdriftlabs.crowdmike`, features now `audio-effect mixing` | `wrapper_cmake_declares_vst3_and_clap_identity` |
| Standalone | Channel resolution, bindings file, autosave, crash-recovery flag, last-session restore, MIDI query without a device now unit-tested (`DeviceController.cpp` added to `crowdmike_tests`) | `standalone_*` |

## 3. Documentation

Fixed: README index lists every `docs/*.md` (enforced by `docs_readme_index_lists_every_spec_document`); removed the `windows-build-*` text; "Planned formats" now "Formats"; IMPLEMENTATION_PLAN status table; PRODUCT_SPEC section 8 status table and section 6/7 notes; GUI_SPEC (min size 1100x650, FX modules, Record/Playback/Settings additions, shortcut table); stale "optional / v1.1 / future" items marked built (phase all-pass, transient enhancer, noise colors, FLAC, seed) or out of scope. New `docs/POST_BETA_SPEC.md` specs AU, aux buses, true-peak, automatic notches, ASIO, signing, pitch/time, 64-bit, CLAP stepped parameters. `docs_every_future_or_planned_item_is_marked_out_of_scope` guards against regressions.

## 4. Remaining gaps

| Pri | Item |
|---|---|
| P1 | Real DAW / pluginval / clap-validator runs, multichannel hardware, screen readers (manual; not possible here) |
| P1 | `FEATURE_AUDIT_2026-10-14.md` is historical and still lists tempo sync, monitor, streaming and scope recall as open; superseded by the specs (not rewritten here) |
| P2 | Record page marker list has no height at exactly 1100x650 (buttons all reachable) |
| P2 | CLAP parameters are exposed as normalised 0..1 (stepped lanes need `DISCRETE`, POST_BETA_SPEC section 9) |
| P2 | `EDITOR_WANTS_KEYBOARD_FOCUS` left default; shortcut delivery inside DAW windows is host dependent (manual check) |
| P3 | AU, aux buses, true-peak, automatic notches, pitch/time, 64-bit, ASIO, signing: POST_BETA_SPEC.md |
| P3 | One timing-sensitive test (`report_recorder_stop_exports_into_session_folder_and_second_take_does_not_overwrite`) failed once under heavy machine load and passed on rerun |

Results: `crowdmike_tests` 149 pass, `crowdmike_dsp_tests` 170, `crowdmike_recording_tests` 39.
