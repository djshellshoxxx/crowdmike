#include "ControlHub.h"

namespace crowdmike {
ControlHub::ControlHub(juce::AudioProcessorValueTreeState& a, SceneBank& s, ShowModel& m) : apvts(a), scenes(s), show(m) {}

void ControlHub::startPolling() {
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) startTimer(5);
}

void ControlHub::queueMidiFromAudioThread(const juce::MidiBuffer& buffer) noexcept {
    for (const auto meta : buffer) {
        const auto msg = meta.getMessage();
        MidiEvent e;
        e.channel = msg.getChannel();
        if (msg.isController()) { e.type = MidiMsgType::CC; e.number = msg.getControllerNumber(); e.value = msg.getControllerValue(); }
        else if (msg.isNoteOn()) { e.type = MidiMsgType::NoteOnOff; e.number = msg.getNoteNumber(); e.value = msg.getVelocity(); e.isOn = true; }
        else if (msg.isNoteOff()) { e.type = MidiMsgType::NoteOnOff; e.number = msg.getNoteNumber(); e.value = 0; e.isOn = false; }
        else if (msg.isPitchWheel()) { e.type = MidiMsgType::PitchBend; e.number = 0; e.value = msg.getPitchWheelValue(); }
        else continue;
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 > 0) { queue[static_cast<size_t>(start1)] = e; fifo.finishedWrite(1); }   // full queue: drop (never block)
    }
}

void ControlHub::pump() {
    for (;;) {
        int start1, size1, start2, size2;
        fifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0) break;
        const MidiEvent e = queue[static_cast<size_t>(start1)];
        fifo.finishedRead(1);
        handleMidi(e);
    }
}

bool ControlHub::setParamNormalised(int idx, float norm) {
    if (idx < 0 || idx >= ParamBank::totalCount() || !std::isfinite(norm)) return false;
    auto* p = apvts.getParameter(ParamBank::idForIndex(idx));
    if (p == nullptr) return false;
    p->beginChangeGesture();
    p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
    p->endChangeGesture();
    return true;
}

bool ControlHub::setParamActual(int idx, float actual) {
    if (idx < 0 || idx >= ParamBank::totalCount() || !std::isfinite(actual)) return false;
    auto* p = apvts.getParameter(ParamBank::idForIndex(idx));
    return p != nullptr && setParamNormalised(idx, p->convertTo0to1(actual));
}

bool ControlHub::pulseParam(int idx, int ms) {
    if (!setParamNormalised(idx, 1.0f)) return false;
    juce::Component::SafePointer<juce::Component> dummy;
    auto* p = apvts.getParameter(ParamBank::idForIndex(idx));
    juce::Timer::callAfterDelay(ms, [p] { if (p != nullptr) p->setValueNotifyingHost(0.0f); });
    return true;
}

void ControlHub::setPanic(bool engaged) {
    setParamNormalised(ParamBank::index(Mod::Global, 0, params::GlobalP::Panic), engaged ? 1.0f : 0.0f);
}

bool ControlHub::setRecallScope(unsigned mask) {
    mask &= ScopeAll;
    if (mask == 0) return false;
    if (show.ui.recallScope != mask) {
        show.ui.recallScope = mask;
        if (onScopeChanged) onScopeChanged();
    }
    return true;
}

void ControlHub::startLearn(const LearnRequest& r) { learnRequest = r; learning.store(true); }

int ControlHub::removeBindingsFor(BindTarget t, int idx) {
    const auto before = show.bindings.size();
    show.bindings.erase(std::remove_if(show.bindings.begin(), show.bindings.end(),
        [&](const ControlBinding& b) { return b.target == t && b.targetIndex == idx; }), show.bindings.end());
    return static_cast<int>(before - show.bindings.size());
}

void ControlHub::handleMidi(const MidiEvent& e) {
    lastMidiNumber.store(e.number, std::memory_order_relaxed);
    lastMidiValue.store(e.value, std::memory_order_relaxed);
    lastMidiChannel.store(e.channel, std::memory_order_relaxed);

    if (learning.load()) {
        // Ignore note-offs and release events while learning.
        if (e.type == MidiMsgType::NoteOnOff && !e.isOn) return;
        ControlBinding b;
        b.type = e.type;
        b.channel = e.channel;
        b.number = e.type == MidiMsgType::PitchBend ? 0 : e.number;
        b.target = learnRequest.target;
        b.targetIndex = learnRequest.targetIndex;
        b.mode = learnRequest.mode;
        show.bindings.erase(std::remove_if(show.bindings.begin(), show.bindings.end(), [&](const ControlBinding& o) {
            return o.type == b.type && o.channel == b.channel && o.number == b.number;
        }), show.bindings.end());
        show.bindings.push_back(b);
        learning.store(false);
        if (onLearned) onLearned(b);
        return;
    }
    for (const auto& b : show.bindings) {
        if (b.type != e.type || (b.channel != 0 && b.channel != e.channel)) continue;
        if (b.type != MidiMsgType::PitchBend && b.number != e.number) continue;
        applyBinding(b, e);
    }
}

void ControlHub::applyBinding(const ControlBinding& b, const MidiEvent& e) {
    const bool pressed = e.type == MidiMsgType::NoteOnOff ? e.isOn : e.value >= 64;
    const float norm01 = e.type == MidiMsgType::PitchBend ? static_cast<float>(e.value) / 16383.0f : static_cast<float>(e.value) / 127.0f;
    switch (b.target) {
        case BindTarget::Param: {
            if (b.targetIndex < 0 || b.targetIndex >= ParamBank::totalCount()) return;
            switch (b.mode) {
                case BindMode::Absolute: setParamNormalised(b.targetIndex, b.min01 + (b.max01 - b.min01) * norm01); break;
                case BindMode::Toggle:
                    if (pressed && (e.type == MidiMsgType::NoteOnOff ? e.isOn : true)) {
                        auto* p = apvts.getParameter(ParamBank::idForIndex(b.targetIndex));
                        if (p != nullptr) setParamNormalised(b.targetIndex, p->getValue() >= 0.5f ? 0.0f : 1.0f);
                    }
                    break;
                case BindMode::Momentary: setParamNormalised(b.targetIndex, pressed ? 1.0f : 0.0f); break;
                case BindMode::Trigger: if (pressed) pulseParam(b.targetIndex); break;
            }
            break;
        }
        case BindTarget::MacroValue:
            setParamNormalised(ParamBank::index(Mod::Macro, juce::jlimit(0, kNumMacros - 1, b.targetIndex), 0),
                               b.min01 + (b.max01 - b.min01) * norm01);
            break;
        default: {
            if (!pressed) return;
            OscAction a;
            switch (b.target) {
                case BindTarget::SceneSelect: a.kind = OscAction::Kind::SceneSelect; break;
                case BindTarget::SceneNext: a.kind = OscAction::Kind::SceneNext; break;
                case BindTarget::ScenePrev: a.kind = OscAction::Kind::ScenePrev; break;
                case BindTarget::RecordStart: a.kind = OscAction::Kind::RecordStart; break;
                case BindTarget::RecordStop: a.kind = OscAction::Kind::RecordStop; break;
                case BindTarget::RecordToggle: a.kind = OscAction::Kind::RecordToggle; break;
                case BindTarget::RecordMarker: a.kind = OscAction::Kind::RecordMarker; break;
                case BindTarget::SplitFile: a.kind = OscAction::Kind::RecordSplit; break;
                case BindTarget::ClipTrigger: a.kind = OscAction::Kind::ClipTrigger; break;
                case BindTarget::ClipStop: a.kind = OscAction::Kind::ClipStop; break;
                case BindTarget::StopAllClips: a.kind = OscAction::Kind::StopAllClips; break;
                case BindTarget::Panic: a.kind = OscAction::Kind::Panic; a.hasValue = true; a.value = 1.0f; break;
                case BindTarget::TempoTap: a.kind = OscAction::Kind::TempoTap; break;
                case BindTarget::TempoResync: a.kind = OscAction::Kind::TempoResync; break;
                case BindTarget::SceneScopeToggle: a.kind = OscAction::Kind::SceneScopeBit; break;   // no value => toggle
                default: return;
            }
            a.index = b.targetIndex;
            execute(a);
            break;
        }
    }
}

void ControlHub::handleOsc(const juce::String& address, const std::vector<float>& args) { execute(resolveOsc(address, args)); }

void ControlHub::execute(const OscAction& a) {
    using K = OscAction::Kind;
    switch (a.kind) {
        case K::None: break;
        case K::SetParam: if (a.hasValue) setParamActual(a.index, a.value); break;
        case K::SetParamNorm: if (a.hasValue) setParamNormalised(a.index, a.value); break;
        case K::Pulse: pulseParam(a.index); break;
        // MIDI / OSC recalls honour the operator's recall scope exactly like the GUI (CONTROL_SPEC section 3).
        case K::SceneSelect: scenes.recall(a.index, show.ui.recallScope); break;
        case K::SceneNext: scenes.next(show.ui.recallScope); break;
        case K::ScenePrev: scenes.previous(show.ui.recallScope); break;
        case K::SceneScopeSet: setRecallScope(static_cast<unsigned>(std::max(0, a.index))); break;
        case K::SceneScopeBit: {
            if (a.index < 0) break;
            if (a.index >= 8) { setRecallScope(ScopeAll); break; }
            const unsigned bit = 1u << static_cast<unsigned>(a.index);
            const bool on = a.hasValue ? a.value >= 0.5f : (show.ui.recallScope & bit) == 0;
            setRecallScope(on ? (show.ui.recallScope | bit) : (show.ui.recallScope & ~bit));
            break;
        }
        case K::TempoTap: if (actions.tempoTap) actions.tempoTap(); break;
        case K::TempoResync: if (actions.tempoResync) actions.tempoResync(); break;
        case K::RecordStart: if (actions.recordStart) actions.recordStart(); break;
        case K::RecordStop: if (actions.recordStop) actions.recordStop(); break;
        case K::RecordToggle: if (actions.recordToggle) actions.recordToggle(); break;
        case K::RecordMarker: if (actions.marker) actions.marker(a.index); break;
        case K::RecordSplit: if (actions.splitFile) actions.splitFile(); break;
        case K::ClipTrigger: if (actions.clipTrigger) actions.clipTrigger(a.index); break;
        case K::ClipStop: if (actions.clipStop) actions.clipStop(a.index); break;
        case K::StopAllClips: if (actions.stopAllClips) actions.stopAllClips(); break;
        case K::Panic: setPanic(a.value >= 0.5f); break;
        case K::Macro: setParamNormalised(ParamBank::index(Mod::Macro, a.index, 0), a.value); break;
    }
}
}
