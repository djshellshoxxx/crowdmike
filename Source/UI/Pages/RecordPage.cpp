// Record page (GUI_SPEC section 8, RECORDING_SPEC): track list with source assignment, arm / mute / monitor, meters,
// file settings, disk + time estimate, rolling waveform, markers, split, record-all and a large Record/Stop.
// Every edit goes to ShowModel::recorder and is applied with applyShowChanges() (diffed in the processor, so it is cheap).
#include "Engine/TapMap.h"
#include "Recording/MarkerExport.h"
#include "UI/Meters.h"
#include "UI/PageKit.h"
#include "UI/Pages.h"
#include "UI/RecordWidgets.h"
#include "UI/ReportPanels.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
namespace {
using rec::MarkerType;

const char* const kMarkerNames[] = { "Big Cheer", "Chant", "Applause", "Performer Moment", "Problem/Feedback", "Custom" };

class RecordPage final : public Page, private Tickable, private juce::ListBoxModel {
public:
    explicit RecordPage(EditorContext& c) : Page(c), reportPanel(c) {
        addAndMakeVisible(reportPanel);
        // ---- header
        recButton.setComponentID("rec.record");
        kit::setupButton(recButton, "Start / stop recording of every armed track (Ctrl+R)");
        recButton.onClick = [this] { toggleRecord(); };
        recordAll.setButtonText("Record all");
        kit::setupButton(recordAll, "Arm every configured (non-muted) track", "rec.recordAll");
        recordAll.onClick = [this] { armAll(); };
        split.setButtonText("Split file");
        kit::setupButton(split, "Close the current files and continue seamlessly in new ones", "rec.split");
        split.onClick = [this] {
            if (!proc.isRecording()) { ctx.say("Split: not recording", true); return; }
            proc.getRecorder().splitFile();
            ctx.say("Recording split into new files");
        };
        autoAssign.setButtonText("Auto-assign");
        kit::setupButton(autoAssign, "Create tracks for every active raw input and the master bus (free slots only)", "rec.autoAssign");
        autoAssign.onClick = [this] { autoAssignTracks(); };
        for (auto* l : { &takeLabel, &elapsedLabel, &remainLabel, &diskLabel }) { kit::styleLabel(*l, 13.0f, true); addAndMakeVisible(*l); }
        for (auto* c2 : std::initializer_list<juce::Component*> { &recButton, &recordAll, &split, &autoAssign, &stateBadge, &dropBadge, &errBadge })
            addAndMakeVisible(c2);

        // ---- tracks
        for (int t = 0; t < kMaxRecordTracks; ++t) {
            auto* row = rows.add(new RecordTrackRow(proc, t));
            row->onSelect = [this](int track) { selectTrack(track); };
            row->onEdited = [this] { modelEdited(); };
            trackHolder.addAndMakeVisible(row);
        }
        trackView.setViewedComponent(&trackHolder, false);
        trackView.setScrollBarsShown(true, false);
        trackView.setTitle("Recorder tracks");
        addAndMakeVisible(trackView);
        for (auto* l : { &hSrc, &hName, &hCh, &hState, &hLevel }) { kit::styleLabel(*l, 11.0f, true, true); addAndMakeVisible(*l); }
        hSrc.setText("#   SOURCE", juce::dontSendNotification);
        hName.setText("NAME", juce::dontSendNotification);
        hCh.setText("CH", juce::dontSendNotification);
        hState.setText("ARM / MUTE / MONITOR", juce::dontSendNotification);
        hLevel.setText("LEVEL", juce::dontSendNotification);
        waveform.setTitle("Rolling waveform of the selected track");
        addAndMakeVisible(waveform);

        // ---- file settings
        format.addItemList({ "WAV 16-bit PCM", "WAV 24-bit PCM", "WAV 32-bit float", "FLAC 24-bit (lossless)" }, 1);
        format.setTitle("File format");
        format.setTooltip("Bit depth of new takes (sample rate = engine rate)");
        format.setComponentID("rec.format");
        format.onChange = [this] { show().recorder.format = format.getSelectedId() - 1; modelEdited(); };
        folderButton.setButtonText("Folder...");
        kit::setupButton(folderButton, "Choose the destination folder for new takes", "rec.folder");
        folderButton.onClick = [this] { chooseFolder(); };
        kit::styleLabel(folderLabel, 12.0f, false, true);
        setupSecondsSlider(preRoll, 0, 60, "Pre-roll: seconds of history included when Record is pressed (0 = off; memory is preallocated)", "rec.preRoll");
        preRoll.onValueChange = [this] { show().recorder.preRollSeconds = static_cast<int>(preRoll.getValue()); modelEdited(); };
        setupSecondsSlider(postRoll, 0, 30, "Post-roll: seconds that keep recording after Stop", "rec.postRoll");
        postRoll.onValueChange = [this] { show().recorder.postRollSeconds = static_cast<int>(postRoll.getValue()); modelEdited(); };
        setupSecondsSlider(splitMinutes, 0, 240, "Split files automatically after this many minutes (0 = off)", "rec.splitMinutes");
        splitMinutes.setTextValueSuffix(" min");
        splitMinutes.onValueChange = [this] { show().recorder.maxFileSeconds = splitMinutes.getValue() * 60.0; modelEdited(); };
        setupSecondsSlider(splitMB, 0, 4096, "Split files automatically at this size in MB (0 = off)", "rec.splitMB");
        splitMB.setTextValueSuffix(" MB");
        splitMB.onValueChange = [this] { show().recorder.maxFileMegabytes = splitMB.getValue(); modelEdited(); };
        setupText(pattern, "File name pattern", "rec.pattern", [this] { show().recorder.pattern = pattern.getText().isNotEmpty() ? pattern.getText() : juce::String("{date}_{time}_{scene}_{source}"); });
        pattern.onTextChange = [this] { updatePreview(); };
        setupText(showName, "Show name ({show} token)", "rec.show", [this] { show().recorder.show = showName.getText(); });
        setupText(venue, "Venue ({venue} token)", "rec.venue", [this] { show().recorder.venue = venue.getText(); });
        // cue / monitor output + options (RECORDING_SPEC 6, 8, 14)
        monitorPair.addItem("Off", 1);
        for (int p = 0; p < kMaxOutputs / 2; ++p) monitorPair.addItem("Out " + juce::String(p * 2 + 1) + "-" + juce::String(p * 2 + 2), p + 2);
        monitorPair.setTitle("Cue output pair");
        monitorPair.setTooltip("Cue / monitor: output pair that carries the tracks whose MON switch is on (added after the output limiters; use a pair that is not part of the PA)");
        monitorPair.setComponentID("rec.monitorPair");
        monitorPair.onChange = [this] { show().recorder.monitorPair = monitorPair.getSelectedId() - 2; modelEdited(); };
        monitorGain.setSliderStyle(juce::Slider::LinearBar);
        monitorGain.setRange(-60.0, 12.0, 0.5);
        monitorGain.setTextValueSuffix(" dB");
        monitorGain.setTooltip("Cue level: gain of the monitor mix");
        monitorGain.setTitle("Cue level");
        monitorGain.setComponentID("rec.monitorGain");
        monitorGain.setDoubleClickReturnValue(true, 0.0);
        monitorGain.setWantsKeyboardFocus(true);
        monitorGain.onValueChange = [this] { show().recorder.monitorGainDb = static_cast<float>(monitorGain.getValue()); modelEdited(); };
        kit::setupToggle(autoRec, "With transport", "Start a take when the host transport starts and stop it when the transport stops (takes started by hand are left alone)", palette().accentDim);
        autoRec.setComponentID("rec.autoRecord");
        autoRec.onClick = [this] { show().recorder.autoRecordOnTransport = autoRec.getToggleState(); kit::refreshToggleText(autoRec, "With transport"); modelEdited(); };
        kit::setupToggle(exportAuto, "Auto-export markers", "Write marker CSV and Audacity labels next to the session file when a take ends", palette().accentDim);
        exportAuto.setComponentID("rec.exportMarkers");
        exportAuto.onClick = [this] { show().recorder.exportMarkers = exportAuto.getToggleState(); kit::refreshToggleText(exportAuto, "Auto-export markers"); modelEdited(); };
        kit::styleLabel(tokenHelp, 11.0f, false, true);
        tokenHelp.setText("Tokens: {date} {time} {scene} {source} {show} {venue} {take}", juce::dontSendNotification);
        kit::styleLabel(preview, 11.0f, false, false);
        for (auto* l : { &lFormat, &lPre, &lPost, &lSplit, &lPattern, &lShow, &lVenue, &lCue, &lOpts }) { kit::styleLabel(*l, 12.0f, false, true); addAndMakeVisible(*l); }
        lCue.setText("Cue / monitor", juce::dontSendNotification);
        lOpts.setText("Options", juce::dontSendNotification);
        lFormat.setText("Format", juce::dontSendNotification);
        lPre.setText("Pre-roll", juce::dontSendNotification);
        lPost.setText("Post-roll", juce::dontSendNotification);
        lSplit.setText("Auto split", juce::dontSendNotification);
        lPattern.setText("Name", juce::dontSendNotification);
        lShow.setText("Show", juce::dontSendNotification);
        lVenue.setText("Venue", juce::dontSendNotification);
        for (auto* c2 : std::initializer_list<juce::Component*> { &format, &folderButton, &folderLabel, &preRoll, &postRoll, &splitMinutes, &splitMB,
                                                                 &pattern, &tokenHelp, &preview, &showName, &venue, &monitorPair, &monitorGain, &autoRec, &exportAuto })
            addAndMakeVisible(c2);

        // ---- markers
        for (int m = 0; m < 6; ++m) {
            auto* b = markerButtons.add(new juce::TextButton(kMarkerNames[m]));
            kit::setupButton(*b, m == 5 ? "Add a custom marker with the label typed next to it" : juce::String("Add a \"") + kMarkerNames[m] + "\" marker at the current position",
                             "rec.marker" + juce::String(m));
            b->onClick = [this, m] { dropMarker(m); };
            addAndMakeVisible(b);
        }
        customLabel.setTextToShowWhenEmpty("Custom marker label", palette().textMuted);
        customLabel.setTitle("Custom marker label");
        customLabel.setJustification(juce::Justification::centredLeft);
        customLabel.setTooltip("Label used by the Custom marker button (Enter adds the marker)");
        customLabel.onReturnKey = [this] { dropMarker(5); };
        addAndMakeVisible(customLabel);
        markerList.setModel(this);
        markerList.setRowHeight(22);
        markerList.setTitle("Markers of the current / last take");
        addAndMakeVisible(markerList);
        clearMarkers.setButtonText("Clear");
        kit::setupButton(clearMarkers, "Clear the marker list (markers already written to the session file stay there)", "rec.clearMarkers");
        clearMarkers.onClick = [this] { proc.getRecorder().clearMarkers(); refreshMarkers(true); };
        addAndMakeVisible(clearMarkers);
        exportNow.setButtonText("Export...");
        kit::setupButton(exportNow, "Write the markers of the last take as CSV and Audacity labels next to its session file", "rec.exportMarkersNow");
        exportNow.onClick = [this] { exportLastMarkers(); };
        addAndMakeVisible(exportNow);

        refreshFromModel();
        selectTrack(0);
        tick();
    }

    // ---- page lifecycle
    void pageShown() override { active = true; refreshFromModel(); refreshMarkers(true); reportPanel.setActive(true); tick(); }
    void pageHidden() override { active = false; reportPanel.setActive(false); }

    void paint(juce::Graphics& g) override {
        Page::paint(g);
        kit::paintSection(g, tracksArea, "Tracks");
        kit::paintSection(g, filesArea, "Files");
        kit::paintSection(g, markersArea, "Markers");
        kit::paintSection(g, reportArea, "Show Report");
        const auto& p = palette();
        if (proc.isRecording()) {   // REC frame around the header: state is also in text (badge + button)
            g.setColour(p.rec);
            g.drawRoundedRectangle(headerArea.toFloat().reduced(1.0f), 5.0f, 2.0f);
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        headerArea = r.removeFromTop(58);
        {
            auto h = headerArea.reduced(8, 6);
            recButton.setBounds(h.removeFromLeft(juce::jlimit(110, 170, h.getWidth() / 8)));
            h.removeFromLeft(10);
            auto badges = h.removeFromLeft(juce::jlimit(150, 230, h.getWidth() / 5));
            stateBadge.setBounds(badges.removeFromTop(badges.getHeight() / 2).reduced(0, 1));
            auto sub = badges.reduced(0, 1);
            dropBadge.setBounds(sub.removeFromLeft(sub.getWidth() / 2 - 2));
            sub.removeFromLeft(4);
            errBadge.setBounds(sub);
            h.removeFromLeft(10);
            auto buttons = h.removeFromRight(juce::jlimit(270, 420, h.getWidth() / 3));
            kit::layoutRow(buttons.reduced(0, 6), { { &recordAll, 1.0f }, { &split, 1.0f }, { &autoAssign, 1.0f } });
            h.removeFromRight(8);
            kit::layoutRow(h, { { &takeLabel, 0.7f }, { &elapsedLabel, 1.0f }, { &remainLabel, 1.2f }, { &diskLabel, 1.0f } });
        }
        r.removeFromTop(8);
        const int rightW = juce::jlimit(320, 520, r.getWidth() * 36 / 100);
        auto right = r.removeFromRight(rightW);
        r.removeFromRight(8);
        reportArea = r.removeFromBottom(ShowReportPanel::kPreferredHeight);
        r.removeFromBottom(8);
        reportPanel.setBounds(kit::sectionContent(reportArea));
        tracksArea = r;
        auto tc = kit::sectionContent(tracksArea);
        waveform.setBounds(tc.removeFromBottom(juce::jlimit(56, 140, tc.getHeight() / 6)));
        tc.removeFromBottom(6);
        auto head = tc.removeFromTop(18);
        const bool wide = tc.getWidth() > 640;
        {
            // header columns approximately follow RecordTrackRow::resized()
            const int fixedRight = 52 + 4 + 60 + 4 + 66 + 4 + 62 + 4 + 38 + 4;
            const int flex = juce::jmax(0, head.getWidth() - 6 - 34 - 4 - fixedRight - 4 * 2);
            const float wSrc = 3.2f, wName = wide ? 1.8f : 0.0f, wMeter = 2.0f;
            const float sum = wSrc + wName + wMeter;
            int x = head.getX() + 6;
            hSrc.setBounds(x, head.getY(), 34 + 4 + static_cast<int>(flex * wSrc / sum), 18);
            x += 34 + 4 + static_cast<int>(flex * wSrc / sum) + 4;
            hName.setBounds(x, head.getY(), static_cast<int>(flex * wName / sum), 18);
            hName.setVisible(wide);
            x += static_cast<int>(flex * wName / sum) + 4;
            hCh.setBounds(x, head.getY(), 38, 18);
            x += 42;
            hState.setBounds(x, head.getY(), 60 + 66 + 62 + 8, 18);
            x += 60 + 66 + 62 + 12;
            hLevel.setBounds(x, head.getY(), juce::jmax(10, static_cast<int>(flex * wMeter / sum)), 18);
        }
        trackView.setBounds(tc);
        const int rowH = juce::jlimit(28, 40, tc.getHeight() / 14);
        trackHolder.setSize(tc.getWidth() - trackView.getScrollBarThickness(), rowH * kMaxRecordTracks);
        for (int t = 0; t < rows.size(); ++t) rows[t]->setBounds(0, t * rowH, trackHolder.getWidth(), rowH);

        // files section: label column + controls, two controls per row where it fits
        const int rowH2 = juce::jlimit(24, 32, right.getHeight() / 22);
        filesArea = right.removeFromTop(kit::kSectionTitleH + rowH2 * 9 + 6 * 8 + 22 + 8);
        right.removeFromTop(8);
        markersArea = right;
        auto fc = kit::sectionContent(filesArea);
        const int labelW = 70;
        auto line = [&](juce::Label& l) { auto row = fc.removeFromTop(rowH2); fc.removeFromTop(6); l.setBounds(row.removeFromLeft(labelW)); return row; };
        {
            auto row = line(lFormat);
            kit::layoutRow(row, { { &format, 1.2f }, { &folderButton, -84.0f } });
            auto row2 = fc.removeFromTop(rowH2 - 4);
            fc.removeFromTop(6);
            folderLabel.setBounds(row2.withTrimmedLeft(labelW));
        }
        preRoll.setBounds(line(lPre));
        postRoll.setBounds(line(lPost));
        kit::layoutRow(line(lSplit), { { &splitMinutes, 1.0f }, { &splitMB, 1.0f } });
        pattern.setBounds(line(lPattern));
        tokenHelp.setBounds(fc.removeFromTop(14).withTrimmedLeft(labelW));
        preview.setBounds(fc.removeFromTop(16).withTrimmedLeft(labelW));
        fc.removeFromTop(6);
        {
            auto row = fc.removeFromTop(rowH2);
            auto half1 = row.removeFromLeft(row.getWidth() / 2 - 4);
            row.removeFromLeft(8);
            lShow.setBounds(half1.removeFromLeft(labelW));
            showName.setBounds(half1);
            lVenue.setBounds(row.removeFromLeft(48));
            venue.setBounds(row);
        }
        fc.removeFromTop(6);
        kit::layoutRow(line(lCue), { { &monitorPair, 1.0f }, { &monitorGain, 1.0f } });
        kit::layoutRow(line(lOpts), { { &autoRec, 1.0f }, { &exportAuto, 1.0f } });

        auto mc = kit::sectionContent(markersArea);
        const int bh = juce::jlimit(26, 34, mc.getHeight() / 9);
        for (int rowI = 0; rowI < 2; ++rowI) {
            auto row = mc.removeFromTop(bh);
            mc.removeFromTop(6);
            kit::layoutRow(row, { { markerButtons[rowI * 3], 1.0f }, { markerButtons[rowI * 3 + 1], 1.0f }, { markerButtons[rowI * 3 + 2], 1.0f } });
        }
        {
            auto row = mc.removeFromTop(bh);
            mc.removeFromTop(6);
            clearMarkers.setBounds(row.removeFromRight(64));
            row.removeFromRight(6);
            exportNow.setBounds(row.removeFromRight(76));
            row.removeFromRight(6);
            customLabel.setBounds(row);
        }
        markerList.setBounds(mc);
    }

    // ---- test / automation access (component IDs are the public surface; these mirror the buttons)
    void tick() override {
        ++tickCount;
        if (!active) {
            if (tickCount % 30 == 0) refreshMarkers(false);   // keep marker notifications flowing to the status bar
            return;
        }
        auto& recorder = proc.getRecorder();
        const auto st = recorder.getStatus();
        for (int t = 0; t < rows.size(); ++t) {
            const float peak = recorder.getTrackPeak(t);
            rows[t]->setLevel(peak);
            if (t == selectedTrack) waveform.push(peak);
            if (tickCount % 15 == 0) rows[t]->setTrackStatus(recorder.hasTrackError(t), recorder.getTrackDropouts(t));
        }
        // record button + badges (text + glyph, never colour only)
        const bool recording = st.recording;
        if (recording != wasRecording) { wasRecording = recording; repaint(); }
        recButton.setButtonText(st.postRoll ? juce::String::fromUTF8("\xe2\x80\xa6 POST-ROLL") : recording ? juce::String::fromUTF8("\xe2\x96\xa0 STOP") : juce::String::fromUTF8("\xe2\x97\x8f RECORD"));
        recButton.setColour(juce::TextButton::buttonColourId, recording ? palette().rec : palette().panelRaised);
        recButton.setTitle(recording ? "Stop recording" : "Start recording");
        if (st.postRoll) stateBadge.set("POST-ROLL " + kit::formatClock(st.elapsedSeconds), StatusBadge::Level::Warn);
        else if (recording) stateBadge.set("REC " + kit::formatClock(st.elapsedSeconds), StatusBadge::Level::Danger);
        else if (st.preRollSeconds > 0.0) stateBadge.set("STOPPED, PRE-ROLL " + juce::String(static_cast<int>(st.preRollSeconds)) + " s", StatusBadge::Level::Ok);
        else stateBadge.set("STOPPED", StatusBadge::Level::Off);
        dropBadge.set(st.dropouts > 0 ? "DROPOUTS " + juce::String(st.dropouts) : juce::String("NO DROPOUTS"), st.dropouts > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Off);
        if (st.diskStopped) errBadge.set("DISK FULL: STOPPED", StatusBadge::Level::Danger);
        else if (st.errors > 0) errBadge.set("DISK ERRORS " + juce::String(st.errors), StatusBadge::Level::Danger);
        else if (st.lowDisk) errBadge.set("LOW DISK", StatusBadge::Level::Warn);
        else errBadge.set("DISK OK", StatusBadge::Level::Off);
        diskLabel.setTooltip(st.lastError.isNotEmpty() ? "Last recorder error: " + st.lastError : juce::String());
        if (st.errors > lastErrors) { lastErrors = st.errors; ctx.say("Recorder error: " + st.lastError, true); }
        if (st.lowDisk && !lowDiskWarned) { lowDiskWarned = true; ctx.say("Low disk space in the record folder", true); }
        if (!st.lowDisk) lowDiskWarned = false;
        if (st.preRollClipped && !preRollWarned) { preRollWarned = true; ctx.say("Pre-roll shortened to fit the memory limit", true); }

        takeLabel.setText("Take " + juce::String(recording ? st.take : st.take + 1) + (recording ? "" : " (next)"), juce::dontSendNotification);
        elapsedLabel.setText(juce::String::fromUTF8("Elapsed ") + kit::formatClock(st.elapsedSeconds, true), juce::dontSendNotification);
        const double freeGB = st.freeDiskGB >= 0.0 ? st.freeDiskGB : folderFreeGB();
        const double bps = bytesPerSecond();
        diskLabel.setText(freeGB >= 0.0 ? "Disk " + (freeGB < 10.0 ? juce::String(freeGB, 1) : juce::String(juce::roundToInt(freeGB))) + " GB free" : juce::String("Disk: unknown"), juce::dontSendNotification);
        diskLabel.setColour(juce::Label::textColourId, freeGB >= 0.0 && freeGB < 2.0 ? palette().warn : palette().text);
        if (bps > 0.0 && freeGB >= 0.0) {
            const double secs = freeGB * 1024.0 * 1024.0 * 1024.0 / bps;
            remainLabel.setText("Left ~" + kit::formatClock(secs), juce::dontSendNotification);
            remainLabel.setTooltip("Estimated recording time left at " + juce::String(juce::roundToInt(bps / (1024.0 * 1024.0) * 60.0)) + " MB/min (armed tracks)");
            remainLabel.setColour(juce::Label::textColourId, secs < 1800.0 ? palette().warn : palette().text);
        } else {
            remainLabel.setText(bps > 0.0 ? "Left: unknown" : "Left: nothing armed", juce::dontSendNotification);
            remainLabel.setColour(juce::Label::textColourId, palette().textMuted);
        }
        split.setEnabled(recording);
        if (tickCount % 10 == 0) refreshMarkers(false);
    }

private:
    ShowModel& show() { return proc.getShow(); }

    void modelEdited() {
        proc.applyShowChanges();
        updatePreview();
        updateWaveCaption();
    }

    void updateWaveCaption() {
        const auto& s = show().recorder.tracks[static_cast<size_t>(selectedTrack)];
        waveform.setCaption("Track " + juce::String(selectedTrack + 1) + ": " + (TapMap::isValid(s.sourceId) ? tapDisplayName(show(), s.sourceId) : juce::String("unused"))
                            + "  (rolling peak of the recorded signal)");
    }

    static void setupSecondsSlider(juce::Slider& s, double lo, double hi, const juce::String& tip, const juce::String& id) {
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setRange(lo, hi, 1.0);
        s.setTextValueSuffix(" s");
        s.setTooltip(tip);
        s.setTitle(tip.upToFirstOccurrenceOf(":", false, false));
        s.setComponentID(id);
        s.setWantsKeyboardFocus(true);
    }

    void setupText(juce::TextEditor& e, const juce::String& title, const juce::String& id, std::function<void()> commit) {
        e.setTitle(title);
        e.setTooltip(title);
        e.setComponentID(id);
        e.setJustification(juce::Justification::centredLeft);
        auto apply = [this, commit] { commit(); modelEdited(); };
        e.onReturnKey = apply;
        e.onFocusLost = apply;
    }

    void refreshFromModel() {
        const auto& r = show().recorder;
        const int ins = proc.getActiveInputChannelCount(), outs = proc.getActiveOutputChannelCount();
        for (auto* row : rows) row->refreshFromModel(ins, outs);
        format.setSelectedId(r.format + 1, juce::dontSendNotification);
        folderLabel.setText(r.folder.isNotEmpty() ? r.folder : "(default) " + defaultFolder().getFullPathName(), juce::dontSendNotification);
        folderLabel.setTooltip(folderLabel.getText());
        preRoll.setValue(r.preRollSeconds, juce::dontSendNotification);
        postRoll.setValue(r.postRollSeconds, juce::dontSendNotification);
        splitMinutes.setValue(r.maxFileSeconds / 60.0, juce::dontSendNotification);
        splitMB.setValue(r.maxFileMegabytes, juce::dontSendNotification);
        monitorPair.setSelectedId(r.monitorPair + 2, juce::dontSendNotification);
        monitorGain.setValue(r.monitorGainDb, juce::dontSendNotification);
        autoRec.setToggleState(r.autoRecordOnTransport, juce::dontSendNotification);
        exportAuto.setToggleState(r.exportMarkers, juce::dontSendNotification);
        kit::refreshToggleText(autoRec, "With transport");
        kit::refreshToggleText(exportAuto, "Auto-export markers");
        if (!pattern.hasKeyboardFocus(true)) pattern.setText(r.pattern, false);
        if (!showName.hasKeyboardFocus(true)) showName.setText(r.show, false);
        if (!venue.hasKeyboardFocus(true)) venue.setText(r.venue, false);
        lastFreeCheckMs = 0;
        updatePreview();
        updateWaveCaption();
    }

    void showModelChanged() override { refreshFromModel(); }

    static juce::File defaultFolder() {
        return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("CrowdMike");
    }
    juce::File recordFolder() {
        const auto& f = show().recorder.folder;
        return juce::File::isAbsolutePath(f) ? juce::File(f) : defaultFolder();
    }

    double folderFreeGB() {
        const auto now = juce::Time::getMillisecondCounter();
        if (lastFreeCheckMs == 0 || now - lastFreeCheckMs > 3000) {
            lastFreeCheckMs = now;
            auto f = recordFolder();
            while (!f.exists() && f.getParentDirectory() != f) f = f.getParentDirectory();
            const auto bytes = f.exists() ? f.getBytesFreeOnVolume() : 0;
            cachedFreeGB = bytes > 0 ? static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) : -1.0;
        }
        return cachedFreeGB;
    }

    double bytesPerSecond() {
        const auto& r = show().recorder;
        const double bytesPerSample = r.format == 0 ? 2.0 : (r.format == 2 ? 4.0 : 3.0);   // FLAC: uncompressed worst case
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        int channels = 0;
        for (const auto& t : r.tracks)
            if (TapMap::isValid(t.sourceId) && t.armed && !t.muted) channels += TapMap::channelsFor(t.sourceId);
        return channels * sr * bytesPerSample;
    }

    void updatePreview() {
        const auto& r = show().recorder;
        juce::String name = pattern.getText().isNotEmpty() ? pattern.getText() : r.pattern;
        const auto now = juce::Time::getCurrentTime();
        juce::String src = "Master";
        for (const auto& t : r.tracks) if (TapMap::isValid(t.sourceId) && t.armed) { src = t.name.isNotEmpty() ? t.name : TapMap::defaultName(t.sourceId); break; }
        const int nextTake = proc.getRecorder().getStatus().take + 1;
        name = name.replace("{date}", now.formatted("%Y-%m-%d")).replace("{time}", now.formatted("%H-%M-%S"))
                   .replace("{scene}", proc.getCurrentSceneName()).replace("{source}", src)
                   .replace("{show}", r.show.isNotEmpty() ? r.show : "Show").replace("{venue}", r.venue.isNotEmpty() ? r.venue : "Venue")
                   .replace("{take}", juce::String(nextTake).paddedLeft('0', 2));
        preview.setText("e.g. " + juce::File::createLegalFileName(name) + (r.format == 3 ? ".flac" : ".wav"), juce::dontSendNotification);
        preview.setTooltip(preview.getText());
    }

    void chooseFolder() {
        chooser = std::make_unique<juce::FileChooser>("Recording destination folder", recordFolder());
        juce::Component::SafePointer<RecordPage> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe](const juce::FileChooser& fc) {
            if (safe == nullptr) return;
            const auto result = fc.getResult();
            if (result == juce::File()) return;
            safe->show().recorder.folder = result.getFullPathName();
            safe->ctx.showModelChanged();
            safe->ctx.say("Recording folder: " + result.getFullPathName());
        });
    }

    void toggleRecord() {
        if (proc.isRecording()) {
            proc.stopRecording();
            ctx.say(show().recorder.postRollSeconds > 0 ? "Stopping after post-roll" : "Recording stopped");
        } else if (!proc.startRecording()) {
            ctx.say("Cannot start recording: " + proc.getRecorder().getStatus().lastError, true);
        } else {
            ctx.say("Recording take " + juce::String(proc.getRecorder().getStatus().take));
        }
        tick();
    }

    void armAll() {
        int n = 0;
        for (auto& t : show().recorder.tracks)
            if (TapMap::isValid(t.sourceId) && !t.muted) { t.armed = true; ++n; }
        for (auto* row : rows) row->updateStates();
        modelEdited();
        ctx.say(n > 0 ? "Armed " + juce::String(n) + " track(s)" : juce::String("No configured tracks to arm: use Auto-assign or pick sources"), n == 0);
    }

    void autoAssignTracks() {
        auto& tracks = show().recorder.tracks;
        std::vector<int> wanted;
        for (int i = 0; i < proc.getActiveInputChannelCount(); ++i) wanted.push_back(TapMap::forRawInput(i));
        wanted.push_back(TapMap::Master);
        int added = 0;
        for (int id : wanted) {
            bool exists = false;
            for (const auto& t : tracks) exists = exists || t.sourceId == id;
            if (exists) continue;
            for (auto& t : tracks) {
                if (TapMap::isValid(t.sourceId)) continue;
                t = {};
                t.sourceId = id;
                t.numChannels = TapMap::channelsFor(id);
                t.armed = true;
                ++added;
                break;
            }
        }
        for (auto* row : rows) row->updateStates();
        modelEdited();
        ctx.say(added > 0 ? "Auto-assign: added " + juce::String(added) + " track(s)" : juce::String("Auto-assign: every raw input and the master already have a track"));
    }

    void selectTrack(int t) {
        selectedTrack = juce::jlimit(0, kMaxRecordTracks - 1, t);
        for (int i = 0; i < rows.size(); ++i) rows[i]->setSelected(i == selectedTrack);
        waveform.clear();
        updateWaveCaption();
        ctx.select({ Selection::Kind::Track, selectedTrack });
    }

    void exportLastMarkers() {
        const auto json = proc.getRecorder().getLastSessionFile();
        rec::SessionInfo info;
        if (json == juce::File() || !rec::SessionMetadata::read(json, info)) { ctx.say("No finished take to export markers from", true); return; }
        juce::String err;
        if (rec::MarkerExport::writeAll(json, info, err)) ctx.say("Markers exported next to " + json.getFileName());
        else ctx.say(err, true);
    }

    void dropMarker(int type) {
        const auto label = type == 5 ? customLabel.getText().trim() : juce::String();
        proc.addMarker(type, label);
        ownMarkers = true;
        ctx.say(juce::String("Marker: ") + (label.isNotEmpty() ? label : juce::String(kMarkerNames[type])) + (proc.isRecording() ? "" : " (not recording: kept for the next take)"));
        refreshMarkers(true);
    }

    void refreshMarkers(bool force) {
        auto list = proc.getRecorder().getMarkers();
        if (!force && list.size() == markers.size()) return;
        if (list.size() > markers.size() && !ownMarkers && markersInit) {
            const auto& m = list.back();     // added by MIDI / OSC / keyboard: surface it in the status bar
            ctx.say("Marker: " + m.label);
        }
        ownMarkers = false;
        markersInit = true;
        markers = std::move(list);
        markerList.updateContent();
        markerList.repaint();
        if (!markers.empty()) markerList.scrollToEnsureRowIsOnscreen(static_cast<int>(markers.size()) - 1);
    }

    // ---- marker list model
    int getNumRows() override { return static_cast<int>(markers.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= static_cast<int>(markers.size())) return;
        const auto& m = markers[static_cast<size_t>(row)];
        const auto& p = palette();
        if (selected) g.fillAll(p.accentDim);
        else if (row % 2 == 1) g.fillAll(p.panelRaised.withAlpha(0.4f));
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        g.setFont(monoFont(12.0f));
        g.setColour(p.textMuted);
        const int tW = juce::jmin(130, w / 4);
        g.drawText(m.wallClock.formatted("%H:%M:%S") + " " + kit::formatClock(static_cast<double>(m.frame) / sr), 4, 0, tW, h, juce::Justification::centredLeft, true);
        const int typeW = juce::jmin(110, w / 5);
        g.setColour(m.type == MarkerType::Problem ? p.danger : p.accent);
        g.setFont(font(12.0f, true));
        g.drawText(kMarkerNames[juce::jlimit(0, 5, static_cast<int>(m.type))], 8 + tW, 0, typeW, h, juce::Justification::centredLeft, true);
        g.setColour(p.text);
        g.setFont(font(12.5f));
        const int sceneW = juce::jmin(110, w / 5);
        g.drawText(m.label, 12 + tW + typeW, 0, w - (16 + tW + typeW + sceneW), h, juce::Justification::centredLeft, true);
        g.setColour(p.textMuted);
        g.drawText(m.scene, w - sceneW - 4, 0, sceneW, h, juce::Justification::centredRight, true);
    }

    bool active = false, wasRecording = false, preRollWarned = false, ownMarkers = false, markersInit = false, lowDiskWarned = false;
    int tickCount = 0, selectedTrack = 0, lastErrors = 0;
    juce::uint32 lastFreeCheckMs = 0;
    double cachedFreeGB = -1.0;
    juce::Rectangle<int> headerArea, tracksArea, filesArea, markersArea, reportArea;
    ShowReportPanel reportPanel;

    juce::TextButton recButton, recordAll, split, autoAssign;
    StatusBadge stateBadge, dropBadge, errBadge;
    juce::Label takeLabel, elapsedLabel, remainLabel, diskLabel;

    juce::Viewport trackView;
    juce::Component trackHolder;
    juce::OwnedArray<RecordTrackRow> rows;
    juce::Label hSrc, hName, hCh, hState, hLevel;
    RollingWaveform waveform;

    juce::ComboBox format;
    juce::TextButton folderButton;
    juce::Label folderLabel, tokenHelp, preview, lFormat, lPre, lPost, lSplit, lPattern, lShow, lVenue, lCue, lOpts;
    juce::ComboBox monitorPair;
    juce::Slider monitorGain;
    juce::TextButton autoRec, exportAuto, exportNow;
    juce::Slider preRoll, postRoll, splitMinutes, splitMB;
    juce::TextEditor pattern, showName, venue;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::OwnedArray<juce::TextButton> markerButtons;
    juce::TextEditor customLabel;
    juce::ListBox markerList;
    juce::TextButton clearMarkers;
    std::vector<rec::Marker> markers;
};
}
std::unique_ptr<Page> makeRecordPage(EditorContext& c) { return std::make_unique<RecordPage>(c); }
}
