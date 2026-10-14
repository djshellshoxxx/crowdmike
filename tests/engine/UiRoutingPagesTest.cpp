// Inputs / Groups / FX / Matrix pages: drive controls programmatically and check the model, plus snapshots at
// 1100x650, 1600x900 and 2560x1440 (CM_SNAPSHOT_DIR, default temp dir) for visual review.
#include "TestHarness.h"
#include "PluginEditor.h"
#include "Core/ShowModel.h"
#include "Engine/TapMap.h"
#include "UI/EditKit.h"
#include "UI/InputAnalysis.h"
#include "UI/MatrixGrid.h"
#include "UI/Pages.h"
#include "UI/ReverseViz.h"
#include "UI/RoutingUndo.h"
#include "UI/Ticker.h"
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::ui;
using namespace crowdmike::params;

namespace {
juce::Component* findById(juce::Component* root, const juce::String& id) {
    if (root == nullptr) return nullptr;
    if (root->getComponentID() == id) return root;
    for (auto* c : root->getChildren())
        if (auto* f = findById(c, id)) return f;
    return nullptr;
}
float actual(CrowdMikeAudioProcessor& p, Mod m, int inst, int e) { return kit::actual(p, ParamBank::index(m, inst, e)); }
juce::File snapDir() {
    if (const char* d = std::getenv("CM_SNAPSHOT_DIR")) { juce::File f(d); f.createDirectory(); return f; }
    { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_snapshots"); d.createDirectory(); return d; }
}
}

CM_TEST(ui_matrix_cells_toggle_undo_keyboard_and_block) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    EditorContext ctx(p);
    auto page = makeMatrixPage(ctx);
    page->setSize(1200, 700);
    page->pageShown();
    auto* gridA = dynamic_cast<MatrixGrid*>(findById(page.get(), "matrix-grid-a"));
    auto* gridB = dynamic_cast<MatrixGrid*>(findById(page.get(), "matrix-grid-b"));
    CM_EXPECT(gridA != nullptr && gridB != nullptr);
    if (gridA == nullptr || gridB == nullptr) return;
    auto& r = p.getRouting();
    auto& um = p.getUndoManager();

    r.setAssign(0, 1, 0.0f);
    gridA->toggleCell(0, 1);
    CM_EXPECT_NEAR(r.getAssign(0, 1), 1.0f, 1e-6);
    um.undo();
    CM_EXPECT_NEAR(r.getAssign(0, 1), 0.0f, 1e-6);
    um.redo();
    CM_EXPECT_NEAR(r.getAssign(0, 1), 1.0f, 1e-6);

    // keyboard: move with arrows, Space toggles, + raises the level
    r.setAssign(1, 2, 0.0f);
    gridA->setFocusCell(0, 0);
    gridA->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    gridA->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    gridA->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CM_EXPECT(gridA->getFocusRow() == 1 && gridA->getFocusCol() == 2);
    gridA->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CM_EXPECT_NEAR(r.getAssign(1, 2), 1.0f, 1e-6);
    gridA->keyPressed(juce::KeyPress('-', juce::ModifierKeys(), '-'));
    CM_EXPECT_NEAR(juce::Decibels::gainToDecibels(r.getAssign(1, 2)), -1.0f, 0.01);
    gridA->toggleUnity(1, 2);
    CM_EXPECT_NEAR(r.getAssign(1, 2), 1.0f, 1e-6);

    // group -> group is illegal: hatched/blocked and not editable
    const int nIn = p.getActiveInputChannelCount();
    CM_EXPECT(!gridA->isEditable(nIn + 0, 1));
    gridA->toggleCell(nIn + 0, 1);
    // input -> FX send column edits the input's send parameter
    gridA->toggleCell(0, kMaxGroups + 0);
    CM_EXPECT_NEAR(actual(p, Mod::Input, 0, InputP::Fx1), 0.0f, 0.05);

    // output matrix: direct row and a bus row (group 1 L)
    r.setDirect(0, 5, 0.0f);
    gridB->toggleCell(0, 5);
    CM_EXPECT_NEAR(r.getDirect(0, 5), 1.0f, 1e-6);
    um.undo();
    CM_EXPECT_NEAR(r.getDirect(0, 5), 0.0f, 1e-6);
    r.setBus(RoutingState::busRowForGroup(0, 0), 3, 0.0f);
    gridB->setCell(nIn + RoutingState::busRowForGroup(0, 0), 3, 0.5f);
    CM_EXPECT_NEAR(r.getBus(RoutingState::busRowForGroup(0, 0), 3), 0.5f, 1e-6);
    Tickable::tickAllForTesting();
    page->pageHidden();
}

CM_TEST(ui_routing_snapshot_action_restores_template_routing) {
    RoutingState r;
    juce::UndoManager um;
    r.setAssign(2, 3, 0.7f);
    const auto before = r.toValueTree();
    r.clearAll();
    r.setDirect(1, 1, 1.0f);
    const auto after = r.toValueTree();
    um.beginNewTransaction("t");
    um.perform(new RoutingSnapshotAction(r, before, after));
    um.undo();
    CM_EXPECT_NEAR(r.getAssign(2, 3), 0.7f, 1e-6);
    CM_EXPECT_NEAR(r.getDirect(1, 1), 0.0f, 1e-6);
    um.redo();
    CM_EXPECT_NEAR(r.getDirect(1, 1), 1.0f, 1e-6);
}

CM_TEST(ui_fx_reset_reorder_and_insert_target) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    EditorContext ctx(p);
    auto page = makeFxPage(ctx);
    page->setSize(1200, 800);
    page->pageShown();
    kit::setActual(p, ParamBank::index(Mod::FxDelay, 0, DelayP::TimeMs), 900.0f);
    kit::setActual(p, ParamBank::index(Mod::FxDelay, 0, DelayP::Bypass), 1.0f);
    auto* reset = dynamic_cast<juce::Button*>(findById(page.get(), "fx-reset-0"));
    CM_EXPECT(reset != nullptr);
    if (reset != nullptr) reset->onClick();
    CM_EXPECT_NEAR(actual(p, Mod::FxDelay, 0, DelayP::TimeMs), 350.0f, 0.01);
    CM_EXPECT_NEAR(actual(p, Mod::FxDelay, 0, DelayP::Bypass), 0.0f, 0.01);

    // insert card (compressor on input 1 by default)
    kit::setActual(p, ParamBank::index(Mod::Input, 0, InputP::InCompThr), -40.0f);
    if (auto* b = dynamic_cast<juce::Button*>(findById(page.get(), "insert-reset-1"))) b->onClick();
    else CM_EXPECT(false);
    CM_EXPECT_NEAR(actual(p, Mod::Input, 0, InputP::InCompThr), -18.0f, 0.01);
    // selecting the master retargets the inserts: the master limiter reset touches the master block
    ctx.select({ Selection::Kind::Master, 0 });
    kit::setActual(p, ParamBank::index(Mod::Master, 0, MasterP::MstLimCeil), -6.0f);
    if (auto* b = dynamic_cast<juce::Button*>(findById(page.get(), "insert-reset-2"))) b->onClick();
    else CM_EXPECT(false);
    CM_EXPECT_NEAR(actual(p, Mod::Master, 0, MasterP::MstLimCeil), -1.0f, 0.01);

    // reorder: Up on the Tape card handle moves it from slot 3 to slot 2 (display only)
    auto* handle = findById(page.get(), "fx-handle-2");
    CM_EXPECT(handle != nullptr);
    if (handle != nullptr) handle->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    const auto order = p.getShow().ui.fxOrder;
    CM_EXPECT(order[1] == 2 && order[2] == 1);
    ShowModel copy;
    CM_EXPECT(copy.fromValueTree(p.getShow().toValueTree()));
    CM_EXPECT(copy.ui.fxOrder == order);
    // invalid orders are rejected
    auto tree = p.getShow().toValueTree();
    tree.getChildWithName("Ui").setProperty("fxOrder", "0,0,1,2,3", nullptr);
    CM_EXPECT(copy.fromValueTree(tree));
    CM_EXPECT(copy.ui.fxOrder[1] == 1);

    CM_EXPECT(findById(page.get(), "reverse-viz") != nullptr);
    Tickable::tickAllForTesting();
    page->pageHidden();
}

CM_TEST(ui_inputs_rec_arm_groups_advanced_tap) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    EditorContext ctx(p);
    auto page = makeInputsPage(ctx);
    page->setSize(1200, 700);
    page->pageShown();
    auto* rec = dynamic_cast<juce::Button*>(findById(page.get(), "input-rec-0"));
    CM_EXPECT(rec != nullptr);
    if (rec == nullptr) return;
    const auto count = [&] {
        int n = 0;
        for (const auto& t : p.getShow().recorder.tracks) n += t.sourceId == TapMap::forRawInput(0) ? 1 : 0;
        return n;
    };
    rec->onClick();
    CM_EXPECT(count() == 1);
    rec->onClick();
    CM_EXPECT(count() == 0);

    auto* cells = dynamic_cast<MatrixGrid*>(findById(page.get(), "input-groups-0"));
    CM_EXPECT(cells != nullptr);
    if (cells != nullptr) {
        p.getRouting().setAssign(0, 3, 0.0f);
        cells->toggleCell(0, 3);
        CM_EXPECT_NEAR(p.getRouting().getAssign(0, 3), 1.0f, 1e-6);
    }
    auto* adv = dynamic_cast<juce::Button*>(findById(page.get(), "inputs-advanced-toggle"));
    CM_EXPECT(adv != nullptr);
    if (adv != nullptr) {
        p.setFeedbackTap(0);
        ctx.select({ Selection::Kind::Input, 1 });
        adv->setToggleState(true, juce::dontSendNotification);
        adv->onClick();
        CM_EXPECT(p.getEngine().getFeedbackTap() == 2);
        for (int k = 0; k < 4; ++k) Tickable::tickAllForTesting();
        page->pageHidden();
        CM_EXPECT(p.getEngine().getFeedbackTap() == 0);
    }
}

CM_TEST(ui_input_response_and_alignment_units) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    const auto set = [&](int e, float v) { kit::setActual(p, ParamBank::index(Mod::Input, 0, e), v); };
    set(InputP::HpOn, 1.0f); set(InputP::HpHz, 100.0f); set(InputP::HpSlope, 1.0f);
    set(InputP::NotchOn, 1.0f); set(InputP::NotchHz, 2000.0f); set(InputP::NotchDepth, -12.0f);
    const auto db = computeInputResponseDb(p, 0, 48000.0, { 20.0f, 100.0f, 700.0f, 2000.0f });
    CM_EXPECT(db[0] < -40.0f);
    CM_EXPECT_NEAR(db[1], -3.01, 0.2);
    CM_EXPECT_NEAR(db[2], 0.0, 0.5);
    CM_EXPECT_NEAR(db[3], -12.0, 0.3);

    CM_EXPECT(kit::alignToText(10.0, 1, 48000.0) == "480 smp");
    CM_EXPECT(kit::alignToText(10.0, 2, 48000.0) == "3.43 m");
    CM_EXPECT_NEAR(kit::alignFromText("3.43 m", 0, 48000.0), 10.0, 1e-6);
    CM_EXPECT_NEAR(kit::alignFromText("480", 1, 48000.0), 10.0, 1e-9);
    CM_EXPECT_NEAR(kit::alignFromText("12.5", 0, 48000.0), 12.5, 1e-9);

    // compressor transfer: below threshold unity, far above it compresses by the ratio
    const int comp = ParamBank::index(Mod::Input, 0, InputP::InCompOn);
    set(InputP::InCompOn, 1.0f); set(InputP::InCompThr, -20.0f); set(InputP::InCompRatio, 4.0f); set(InputP::InCompKnee, 0.0f);
    set(InputP::InCompMakeup, 0.0f); set(InputP::InCompAuto, 0.0f); set(InputP::InCompMix, 1.0f);
    CM_EXPECT_NEAR(compressorOutputDb(p, comp, -40.0f), -40.0f, 0.01);
    CM_EXPECT_NEAR(compressorOutputDb(p, comp, 0.0f), -15.0f, 0.05);
    const float a[] = { 1, 2, 3, 4 }, b[] = { 2, 4, 6, 8 }, c[] = { 4, 3, 2, 1 };
    CM_EXPECT_NEAR(sequenceCorrelation(a, b, 4), 1.0, 1e-6);
    CM_EXPECT_NEAR(sequenceCorrelation(a, c, 4), -1.0, 1e-6);
}

CM_TEST(ui_reverse_viz_renders_states) {
    juce::ScopedJuceInitialiser_GUI gui;
    ReverseVizView v;
    v.setSize(600, 140);
    v.chunkSeconds = [] { return 4.0f; };
    dsp::ReverseChunk::Viz z {};
    z.writeHead01 = 0.95f; z.playHead01 = 0.7f; z.storedStart01 = 0.6f; z.storedLen01 = 0.25f;
    z.state = 2; z.pending = true; z.crossfade01 = 0.01f; z.wet01 = 0.8f;
    v.setViz(z);
    CM_EXPECT(ReverseVizView::stateName(2, true).contains("PLAYING"));
    CM_EXPECT(ReverseVizView::stateName(2, true).contains("PENDING"));
    const auto img = v.createComponentSnapshot(v.getLocalBounds());
    CM_EXPECT(img.isValid());
    // wrapped stored region must not crash either
    z.storedStart01 = 0.9f; z.storedLen01 = 0.3f;
    v.setViz(z);
    CM_EXPECT(v.createComponentSnapshot(v.getLocalBounds()).isValid());
}

CM_TEST(ui_routing_pages_snapshots_three_sizes) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(3);
    p.getShow().input(0).zone = "front";
    p.getShow().output(0).zone = "front";
    p.getRouting().setDirect(0, 0, 1.4f);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 40; ++blk) {
        for (int s = 0; s < 256; ++s) { const float v = 0.3f * std::sin(0.05f * static_cast<float>(blk * 256 + s)); buf.setSample(0, s, v); buf.setSample(1, s, 0.5f * v); }
        p.processBlock(buf, midi);
    }
    const auto dir = snapDir();
    editor->getContext().select({ Selection::Kind::Input, 0 });
    for (const auto size : { juce::Point<int>(1100, 650), juce::Point<int>(1600, 900), juce::Point<int>(2560, 1440) }) {
        editor->setSize(size.x, size.y);
        for (const auto id : { PageId::Inputs, PageId::Groups, PageId::Fx, PageId::Matrix }) {
            editor->showPage(id);
            if (id == PageId::Inputs)
                if (auto* adv = dynamic_cast<juce::Button*>(findById(editor->getPage(id), "inputs-advanced-toggle"))) {
                    adv->setToggleState(true, juce::dontSendNotification);
                    adv->onClick();
                }
            Tickable::tickAllForTesting();
            Tickable::tickAllForTesting();
            Tickable::tickAllForTesting();
            const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            CM_EXPECT(img.isValid());
            const auto file = dir.getChildFile(juce::String(pageName(id)) + "_" + juce::String(size.x) + "x" + juce::String(size.y) + ".png");
            file.deleteFile();
            juce::FileOutputStream out(file);
            CM_EXPECT(out.openedOk() && juce::PNGImageFormat().writeImageToStream(img, out));
        }
    }
}
