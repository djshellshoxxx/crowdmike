# CrowdMike post-beta specification (backlog items)

Everything in this file is **out of scope for the beta**: none of it is built, none of it blocks the beta, and the beta
documentation must not promise it. Each item is specified concisely enough to be built later without further
design work. All rules of the other specs still apply: frozen parameter IDs (new ones are only ever appended with new unique
IDs), schema-v2 state and presets must keep loading identically (add a regression test), real-time safety on the audio
thread, block sizes 1..2048, 44.1-192 kHz, finite output.

Order of work (suggested): 1 AU, 2 aux buses, 3 true-peak, 4 automatic notches, 5 ASIO and signing, then the rest.

## 1. Audio Unit (AU) format (macOS)

* **Purpose**: load in Logic Pro and GarageBand, which do not host VST3/CLAP.
* **Build**: add `AU` to `FORMATS` for `juce_add_plugin` on `APPLE` only (`AU_MAIN_TYPE kAudioUnitType_Effect`, `PLUGIN_MANUFACTURER_CODE CrDL`,
  `PLUGIN_CODE CrM1` unchanged). The macOS release job packages `CrowdMike.component` next to the VST3 and CLAP.
* **Behaviour**: identical engine and parameter set (same IDs, same order); bus layouts as in section 2; latency reported with
  `setLatencySamples`; `getTailLengthSeconds` as today. Presets are the shared factory presets exposed as AU factory presets
  only if a later release reports more than one program (today a single program is reported).
* **Edge cases**: AU hosts query layouts before `prepareToPlay`; `isBusesLayoutSupported` is already stateless. AU parameter
  count (about 3000) must not make the host UI unusable: verify in Logic and fall back to a parameter-group tree if needed
  (`AudioProcessorParameterGroup` per module, additive, IDs unchanged).
* **Acceptance**: `auval -v aufx CrM1 CrDL` passes on macOS arm64 and x86_64; load in Logic, automate `input1.trimDb`, save and
  reload a project: state identical (the existing state round-trip tests plus a manual host check).

## 2. Auxiliary plug-in buses

* **Purpose**: separate host buses for (a) a performer/side-chain input used as the ducker key, (b) a stereo broadcast/cue
  output next to the main output, (c) a stereo "FX return" output.
* **Layout**: main input/output stay bus 0 and unchanged (all current layouts keep working). Optional buses, disabled by default:
  input bus 1 `Key` (mono or stereo), output bus 1 `Broadcast` (stereo), output bus 2 `Cue` (stereo).
  `isBusesLayoutSupported` accepts each aux bus as disabled, mono or stereo; the main bus rules (1..16 channels) are unchanged.
* **Routing**: new key-source choice value `Aux input` appended to `GlobalP::DuckKey` is **not** possible without changing a
  frozen choice list; instead an appended bool parameter `duck.keyAux` (default off) makes the ducker use the aux input bus
  as key instead of `DuckKey`. Output buses carry the existing `Broadcast Crowd` group bus and the Record page cue pair
  (`monitorPair`) respectively; when the bus is disabled the signal is simply not copied.
* **Real-time**: bus-aware clearing (clear every enabled output channel not written), no allocation; the engine reads the aux input
  only when `duck.keyAux` is on.
* **Standalone**: not applicable (the device controller already maps any physical channel to any engine channel).
* **Acceptance**: new tests for layouts (main + aux enabled/disabled combinations), ducker keyed from the aux bus ducks the
  groups, disabled aux bus is silent and never written; pluginval level 5 with each combination.

## 3. True-peak limiter mode

* **Purpose**: EBU R128 / broadcast ceilings that hold on the reconstructed signal, not only on samples.
* **Parameters** (appended to `GlobalP`): `limiterTruePeak` (bool, default off), applies to the output-stage limiters and the
  master limiter. IDs: `truePeakLimiter`.
* **Algorithm**: 4x oversampled detector (polyphase FIR, 48 taps, linear phase) on the lookahead path: the gain computer uses
  `max(|x|, true-peak estimate)`; audio itself is not oversampled. Lookahead grows by the FIR group delay at the base rate
  (about 6 samples at 4x) and is added to the **constant** reported latency whether the mode is on or off, so hosts
  never see latency changes.
* **Acceptance**: sine and inter-sample-peak test signals (`+0.0 dBFS` sample pairs at fs/4) stay at or below ceiling when
  measured with a 4x oversampled reference meter; mode off is bit-identical to today; latency identical on and off.

## 4. Automatic dynamic notches

* **Purpose**: let the feedback assistant act without an operator, bounded and reversible.
* **Parameters**: per output group of inputs is out of scope; one global set (appended to `GlobalP`): `autoNotchEnable` (bool,
  default **off**), `autoNotchMaxDepthDb` (3..18, default 9), `autoNotchMaxCount` (1..8 per input, default 4),
  `autoNotchReleaseS` (5..300, default 60).
* **Algorithm**: only candidates with severity above the detector's "stable" threshold for at least 2 s are acted on. A notch is
  engaged by ramping its depth from 0 to the suggested depth (clamped to `autoNotchMaxDepthDb`) in 50 ms on a spare biquad of the
  affected input; it releases (depth to 0) after the candidate has been absent for `autoNotchReleaseS`. Never more than
  `autoNotchMaxCount` per input; never touches the operator's own notch (`InputP::Notch*`).
* **Threading**: the analysis thread publishes candidates; a lock-free single-producer message to the audio thread engages
  slots; no allocation. Every automatic action is shown in the Feedback view as `AUTO` rows with a Revert button and counted
  in the Show Report.
* **Acceptance**: synthetic ringing tone on one input gets a notch within 3 s and loses it after the release; a musical
  sustained note shorter than the stable time does not; disabled mode is bit-identical; depth never exceeds the maximum.

## 5. ASIO on Windows

* Requires the Steinberg ASIO SDK licence. Build option `CROWDMIKE_ENABLE_ASIO` (OFF by default, CI release job ON only when the SDK path
  secret exists) sets `JUCE_ASIO=1` for the standalone. The device selector already lists any JUCE device type.
* **Acceptance**: open an 8-in/8-out ASIO interface, bind all 8 channels, record 10 min with zero reported xruns on the test machine.

## 6. Code signing and notarization

* Windows: Authenticode-sign the installer and every binary in `package_beta.sh` when `WINDOWS_CERT_PFX` secrets exist. macOS: Developer ID
  signature, hardened runtime, `notarytool` submit and staple for app, VST3, CLAP (and AU). Unsigned fallback stays for forks.
* **Acceptance**: `signtool verify /pa`, `spctl -a -vv` and `stapler validate` pass in the release job.

## 7. Pitch and time processing for clips

* Per-clip appended settings `pitchSemitones` (-12..+12, default 0) and `timeStretch` (0.5..2.0, default 1.0, ignored when
  `tempoSync` is on because tempo sync already derives the rate). Offline render on load into a second buffer (worker thread, never on the
  audio thread); the clip plays the rendered buffer; pitch 0 and time 1.0 use the original buffer bit-exactly. State: two new
  attributes on the `Clip` node, missing attributes load as defaults.
* **Acceptance**: +12 semitones doubles the dominant frequency within 1 %; stretch 2.0 doubles the duration within one block;
  defaults are bit-identical to the current playback.

## 8. 64-bit (double precision) host processing

* `supportsDoublePrecisionProcessing() == true` with `processBlock(AudioBuffer<double>&, ...)` converting to the engine's 32-bit
  float path in a preallocated scratch buffer (no extra latency). The engine stays float internally; only the wrapper changes.
* **Acceptance**: double-precision render equals float render converted back within -140 dBFS error; no allocation in
  `processBlock` (test with the existing allocation guard).

## 9. UI and platform items

* **Font scaling with window size**: fonts stay fixed in logical pixels; the UI scale setting (75-200 %) is the supported way to scale. Optional
  proportional scaling is not planned.
* **Screen-reader validation**: accessibility titles exist on every control (checked by `ui_audit_every_visible_control_is_wired_and_named`); manual
  VoiceOver / NVDA passes are part of the host/hardware validation list in `feature-audit/`, not an automated deliverable.
* **CLAP stepped parameters**: today CLAP exposes every parameter as a normalised 0..1 value (`CLAP_USE_JUCE_PARAMETER_RANGES` default). Switching to
  `DISCRETE` would present bool/choice parameters as stepped lanes in CLAP hosts. It changes how CLAP hosts store automation values, so it needs a
  deliberate migration note and a clap-validator run; deferred until the first CLAP host feedback.
* **176.4 / 192 kHz hardware**: the DSP unit tests cover 44.1 to 96 kHz (selected modules and the soak device test also run at 192 kHz); sustained operation at 176.4/192 kHz on real
  interfaces is part of the manual hardware validation.

## 10. Explicit non-goals (unchanged, restated)

Room-wide acoustic calibration, guaranteed active noise cancellation, hosting third-party plug-ins inside CrowdMike, and direct Dante/AES67
transport in the DSP core. Dante/AES67 interfaces work through the operating system's audio device like any other device.
