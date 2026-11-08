// Daisy Control Panel, JUCE standalone (GUI.md § 3; docs/dcp/SD_PAGE_WORK_ORDER.md): the
// modulation matrix page and the SD card page in one app.
//   "Daisy Control Panel"                 normal start, MIDI ports only
//   "Daisy Control Panel" --simulators    also lists the simulators (development, no Daisy)
//   "Daisy Control Panel" --selftest <dir> drives the window against the simulators, checks
//                                         the device CRC after every step, writes screenshots
#include "../ui/EditorComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
using namespace editor;
using editor::ui::EditorComponent;
constexpr const char* kAppName = "Daisy Control Panel";

class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(const juce::String& name, bool with_simulators)
        : DocumentWindow(name, ui::colour::bg, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new EditorComponent(with_simulators, true), true);
        setResizable(true, true);
        setResizeLimits(1200, 860, 4000, 3000);
        centreWithSize(getWidth(), getHeight());
    }
    EditorComponent& Editor() { return *static_cast<EditorComponent*>(getContentComponent()); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

// ------------------------------------------------------------------ self-test
#define SELFTEST_CHECK(cond) Check((cond), #cond)
class SelfTest {
  public:
    SelfTest(EditorComponent& e, juce::File dir) : e_(e), dir_(std::move(dir)) {}

    int Run() {
        e_.UseManualClock();
        e_.DisableHardwareSerial(); // simulated cables only (review finding 12)
        e_.sd_autosave_sync = true; // the manual clock has no message loop for the file worker
        dir_.createDirectory();
        log_ = dir_.getChildFile("selftest.txt");
        log_.replaceWithText({});
        e_.sd_autosave_dir = dir_.getChildFile("autosave");
        SELFTEST_CHECK(!e_.HardwareSerialActive());
        ToyPiano();
        PhysicalModeling();
        Corrections();
        Offline();
        ProjectPatch();
        AcidSeq();
        Streichwurst();
        SdNoInstrument();
        SdCycle("Simulator: Nebulae MOD", true, true, "dcp-nebulae-mod");
        SdCycle("Simulator: Looper MOD", true, false, "dcp-looper-mod");
        SdCycle("Simulator: Nebulae", false, false, "dcp-nebulae");
        SdCycle("Simulator: Looper", false, false, "dcp-looper");
        SdLooperSaving();
        SdInterruption();
        SdRestoreNotToWrongDevice();
        SdShareGate();
        SdAckEpisode();
        SdNoCardAndLongTexts();
        SdLooperFailedSave();
        SdPluginNote();
        SELFTEST_CHECK(!e_.HardwareSerialActive()); // still none at the end
        if(failures_)
            Say("FAIL: " + juce::String(failures_) + " checks");
        else
            Say("PASS: Daisy Control Panel against the simulated devices (screenshots in " + dir_.getFullPathName() + ")");
        return failures_ ? 1 : 0;
    }

  private:
    // A GUI application has no console on Windows: every line also goes to selftest.txt.
    void Say(const juce::String& line) {
        std::printf("%s\n", line.toRawUTF8());
        std::fflush(stdout);
        log_.appendText(line + "\n");
    }
    void Check(bool ok, const char* what) {
        if(!ok) {
            Say(juce::String("CHECK failed: ") + what);
            ++failures_;
        }
    }

    // The page's layout check, with the reason in the log when it fails.
    bool Visible(const ui::SdSharePanel& page) {
        const bool ok = page.AllVisibleInside();
        if(!ok)
            Say("layout: " + page.LayoutFailure() + " at " + juce::String(e_.getWidth()) + "x" + juce::String(e_.getHeight()));
        return ok;
    }

    bool Settle(uint32_t max_ms = 5000) {
        for(uint32_t i = 0; i < max_ms; ++i) {
            e_.Step(1);
            if(e_.GetSession().Idle() && e_.GetSession().State() != LinkState::Connecting) {
                e_.Step(30);
                e_.RefreshNow();
                return true;
            }
        }
        return false;
    }
    bool InSync() {
        return e_.GetSimulator() && e_.GetSimulator()->Device().CurrentCrc() == e_.GetModel().Crc();
    }
    juce::MouseEvent Mouse(juce::Point<float> pos, juce::Point<float> down, juce::ModifierKeys mods = {},
                           int clicks = 1) {
        auto& grid = e_.Grid();
        const juce::Time t = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                juce::MouseInputSource::defaultTiltY, &grid, &grid, t, down, t, clicks, pos != down);
    }
    juce::Point<float> Centre(int s, int t) { return e_.Grid().CellBounds(s * 8 + t).getCentre().toFloat(); }
    void Key(int code, juce::juce_wchar ch = 0) { e_.Grid().keyPressed(juce::KeyPress(code, {}, ch)); }
    void Shot(const char* name) {
        e_.RefreshNow();
        const juce::Image image = e_.createComponentSnapshot(e_.getLocalBounds(), true, 1.0f);
        juce::File file = dir_.getChildFile(name);
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
    }
    int PortIndex(const char* label) { return e_.PortNames().indexOf(ui::U(label)); }

    void ToyPiano() {
        e_.ConnectTo(PortIndex("Simulator: Toy Piano"));
        SELFTEST_CHECK(Settle());
        Model& m = e_.GetModel();
        SELFTEST_CHECK(e_.GetSession().Connected() && e_.GetSession().Device().key == "toy-piano");
        SELFTEST_CHECK(m.columns[0].name == "Pitch" && !m.columns[6].present && !m.Allowed(7 * 8 + 3));
        // Firmware update: a simulator has no bootloader; nothing is sent, the session stays.
        e_.StartFirmwareUpdate(PortIndex("Simulator: Toy Piano"));
        SELFTEST_CHECK(!e_.FirmwareUpdateWaiting() && e_.GetSession().Connected());
        SELFTEST_CHECK(e_.LogText().contains("has no bootloader"));

        // Keyboard: LFO 1 → Pitch +30.
        e_.Grid().Select(0);
        for(int i = 0; i < 3; ++i)
            Key(juce::KeyPress::pageUpKey);
        SELFTEST_CHECK(Settle() && m.Depth(0) == 30 && InSync());

        // Drag: Modwheel → Level 100 px down = -50.
        auto& grid = e_.Grid();
        const juce::Point<float> c = Centre(5, 1);
        grid.mouseDown(Mouse(c, c));
        for(int y = 4; y <= 100; y += 4) {
            grid.mouseDrag(Mouse(c.translated(0, float(y)), c));
            e_.Step(3);
        }
        grid.mouseUp(Mouse(c.translated(0, 100), c));
        SELFTEST_CHECK(Settle() && m.Depth(5 * 8 + 1) == -50 && InSync());

        // Mouse wheel +5, then the cell editor's slider: Poly AT → Level 80.
        for(int i = 0; i < 5; ++i)
            grid.mouseWheelMove(Mouse(Centre(7, 1), Centre(7, 1)), {0.0f, 0.2f, false, false, false});
        SELFTEST_CHECK(m.Depth(7 * 8 + 1) == 5 && m.selected == 7 * 8 + 1);
        e_.DepthSlider().setValue(80, juce::sendNotificationSync);
        SELFTEST_CHECK(Settle() && m.Depth(7 * 8 + 1) == 80 && InSync());

        // Double-click switches off and keeps the depth.
        grid.mouseDoubleClick(Mouse(Centre(5, 1), Centre(5, 1), {}, 2));
        SELFTEST_CHECK(Settle() && !m.Enabled(5 * 8 + 1) && m.Depth(5 * 8 + 1) == -50 && InSync());

        // Sources: LFO 1 S&H at MIDI clock 1/8, LFO 2 2.4 Hz, Random smooth at gate 2.
        ui::SourcePanel& lfo1 = e_.Source(0);
        lfo1.Shape().setSelectedId(mod::SampleHold + 1, juce::sendNotificationSync);
        lfo1.Sync(mod::MidiClock).onClick();
        lfo1.Division().setSelectedId(6 + 1, juce::sendNotificationSync);
        e_.Source(1).Rate().setValue(HzToRate(2.4f), juce::sendNotificationSync);
        ui::SourcePanel& rnd = e_.Source(2);
        rnd.Shape().setSelectedId(mod::Smooth + 1, juce::sendNotificationSync);
        rnd.Sync(mod::GateClock).onClick();
        rnd.Gate().setSelectedId(2, juce::sendNotificationSync);
        SELFTEST_CHECK(Settle() && InSync());
        SELFTEST_CHECK(m.patch.lfo[0].shape == mod::SampleHold && m.patch.lfo[0].division == 6);
        SELFTEST_CHECK(m.patch.random.division == mod::kDefaultGateDivision && m.patch.random.gate == 1);
        SELFTEST_CHECK(!lfo1.Rate().isVisible() && lfo1.Division().isVisible() && rnd.Gate().isVisible());

        // Limit of 8 active connections.
        for(int cell : {1, 2, 8, 9, 18, 19}) {
            grid.Select(cell);
            Key(juce::KeyPress::pageUpKey);
        }
        SELFTEST_CHECK(Settle() && m.ActiveCount() == 8 && InSync());
        grid.Select(3 * 8 + 3);
        Key(juce::KeyPress::pageUpKey);
        SELFTEST_CHECK(Settle() && m.ActiveCount() == 8 && !m.Enabled(3 * 8 + 3) && InSync());
        SELFTEST_CHECK(e_.LogText().contains(ui::U("At most 8")));
        grid.Select(18); // back to fewer than 8 for the screenshot
        Key('0', '0');
        grid.Select(19);
        Key('0', '0');

        // Name, live monitor, screenshot.
        e_.NameField().setText("Vibrato Test");
        e_.NameField().onReturnKey();
        e_.Step(800);
        SELFTEST_CHECK(Settle() && InSync() && e_.GetSimulator()->Device().CurrentPatch().name == std::string("Vibrato Test"));
        grid.Select(7 * 8 + 1);
        Shot("juce-editor-toy-piano.png");

        // File: save, clear, load, same CRC as before.
        const juce::File file = dir_.getChildFile("selftest.modpatch");
        const uint32_t before = m.Crc();
        SELFTEST_CHECK(e_.SaveFile(file));
        e_.GetSession().ClearMatrix();
        SELFTEST_CHECK(Settle() && m.ActiveCount() == 0 && InSync());
        SELFTEST_CHECK(e_.LoadFile(file));
        SELFTEST_CHECK(Settle() && m.Crc() == before && InSync());
        Say("PASS toy-piano: keyboard, drag, wheel, slider, on/off, sources, limit 8, name, files");
    }

    void PhysicalModeling() {
        e_.ConnectTo(PortIndex("Simulator: Physical Modeling"));
        SELFTEST_CHECK(Settle() && e_.GetSession().Device().key == "physical-modeling");
        Model& m = e_.GetModel();
        SELFTEST_CHECK(m.columns[5].name == "Excitation" && !m.Allowed(7 * 8 + 5));
        SELFTEST_CHECK(m.columns[6].name == "Level"); // replaced the reverb decay (mod2)
        auto& grid = e_.Grid();
        const int cells[][3] = {{0, 2, 40}, {1, 1, -30}, {2, 4, 25}, {6, 6, 60}, {7, 2, 70}, {7, 0, 12}};
        for(const auto& c : cells) {
            grid.Select(c[0] * 8 + c[1]);
            e_.GetSession().EditDepth(c[0] * 8 + c[1], c[2]);
        }
        e_.Source(0).Rate().setValue(HzToRate(0.5f), juce::sendNotificationSync);
        e_.Step(800);
        SELFTEST_CHECK(Settle() && InSync());
        grid.Select(7 * 8 + 2);
        Shot("juce-editor-physical-modeling.png");
        Say("PASS physical-modeling: columns, Poly AT per voice, Excitation greyed out");
    }

    // Correction order 2026-10-24 in the real window: FM, ranges, aftertouch, health,
    // compatibility question, port release.
    void Corrections() {
        e_.ConnectTo(PortIndex("Simulator: Physical Modeling"));
        SELFTEST_CHECK(Settle() && e_.GetSession().Device().LfoFm());
        Model& m = e_.GetModel();
        Simulator& sim = *e_.GetSimulator();
        ui::SourcePanel& lfo1 = e_.Source(0);
        SELFTEST_CHECK(lfo1.Fm().isVisible() && lfo1.Fm().isEnabled() && !e_.Source(2).Fm().isVisible());
        lfo1.Fm().setValue(35, juce::sendNotificationSync);
        e_.Source(1).Rate().setValue(HzToRate(9.0f), juce::sendNotificationSync);
        e_.GetSession().EditDepth(0 * 8 + 1, 50);
        e_.GetSession().EditDepth(1 * 8 + 1, 20);
        lfo1.Rate().setValue(HzToRate(20.0f), juce::sendNotificationSync);
        SELFTEST_CHECK(Settle() && InSync() && sim.Device().CurrentPatch().lfo[0].fm == 35);
        sim.Midi(0x90, 60, 100);
        sim.Midi(0xA0, 60, 96);
        e_.Step(300);
        SELFTEST_CHECK(e_.Grid().Monitor().has_range && e_.Grid().Monitor().at_kind == 2);
        SELFTEST_CHECK(e_.Grid().Monitor().max[0] - e_.Grid().Monitor().min[0] > 1.5f);
        e_.Grid().Select(0 * 8 + 1);
        Shot("juce-editor-ranges.png");
        lfo1.Sync(mod::MidiClock).onClick();
        SELFTEST_CHECK(Settle() && !lfo1.Fm().isEnabled() && m.patch.lfo[0].fm == 35);
        e_.Step(200);
        SELFTEST_CHECK(lfo1.ClockText().contains(ui::U("No MIDI clock")));
        lfo1.Sync(mod::Free).onClick();
        SELFTEST_CHECK(Settle() && lfo1.Fm().isEnabled());
        Say("PASS corrections: LFO FM slider, ranges, aftertouch kind, clock state");

        // Health: replies lost (port taken by another application), then back.
        sim.drop_replies = 1;
        e_.Step(1700);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.GetSession().StatusText().find("no live data") != std::string::npos);
        e_.Step(3600);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.GetSession().StatusText().find("no answer") != std::string::npos);
        Shot("juce-editor-no-replies.png");
        sim.drop_replies = 0;
        SELFTEST_CHECK(Settle() && e_.GetSession().LinkHealth() == Health::Ok && InSync());
        Say("PASS corrections: health no live data / no replies / recovered");

        // Compatibility: a Toy Piano file on Physical Modeling asks; cancel changes nothing,
        // safe load switches the affected connections off.
        Model toy;
        toy.columns[0].name = "Pitch";
        toy.SetDepth(0, 30);
        DeviceInfo toy_device;
        toy_device.key = "toy-piano";
        const juce::File file = dir_.getChildFile("selftest-toy.modpatch");
        file.replaceWithText(ui::U(SavePatch(toy, &toy_device)));
        int asked = 0;
        e_.compat_decision = [&](const Compatibility& c) {
            ++asked;
            SELFTEST_CHECK(c.key_mismatch && c.affected == Bit(0));
            return ui::EditorComponent::CompatChoice::Cancel;
        };
        const uint32_t before = m.Crc();
        e_.LoadFile(file);
        SELFTEST_CHECK(Settle() && asked == 1 && m.Crc() == before && InSync());
        e_.compat_decision = [](const Compatibility&) { return ui::EditorComponent::CompatChoice::Safe; };
        e_.LoadFile(file);
        SELFTEST_CHECK(Settle() && InSync() && !m.Enabled(0) && m.Depth(0) == 30);
        e_.compat_decision = nullptr;
        Say("PASS corrections: compatibility question (cancel, safe load)");

        // A session that gives up releases the port at once (review F2).
        e_.ConnectTo(PortIndex("Simulator: Physical Modeling"));
        e_.GetSimulator()->drop_replies = 1;
        for(int i = 0; i < 3000 && e_.GetSession().State() != LinkState::Offline; ++i)
            e_.Step(1);
        e_.Step(1);
        SELFTEST_CHECK(e_.GetSession().State() == LinkState::Offline && !e_.PortOpen());
        Say("PASS corrections: no answer to IDENTIFY releases the port");
    }

    void Offline() {
        e_.Disconnect();
        SELFTEST_CHECK(e_.GetSession().State() == LinkState::Offline);
        e_.Grid().Select(12);
        Key(juce::KeyPress::pageDownKey);
        SELFTEST_CHECK(e_.GetModel().Depth(12) == -10);
        Say("PASS offline: editing without a device");
    }

    // VST3 plugin: a patch from the project, loaded while offline, is shown at once and
    // replaces the device's state when the connection is ready; changes reach the host.
    void ProjectPatch() {
        Model& m = e_.GetModel();
        std::string reported;
        e_.patch_changed = [&reported](const std::string& t) { reported = t; };
        e_.GetSession().EditDepth(1, 40, true); // LFO 1 -> Structure, offline
        const std::string project = e_.PatchText();
        e_.GetSession().EditDepth(1, 0);
        e_.LoadProjectPatch(project);
        e_.RefreshNow();
        SELFTEST_CHECK(m.Depth(1) == 40 && reported.find("40") != std::string::npos);
        e_.ConnectTo(PortIndex("Simulator: Physical Modeling")); // a fresh device: empty matrix
        SELFTEST_CHECK(Settle());
        e_.RefreshNow();
        // The fresh device had an empty matrix: now it holds the project patch.
        SELFTEST_CHECK(Settle() && InSync() && m.Depth(1) == 40 && m.Enabled(1));
        SELFTEST_CHECK(e_.GetSimulator()->Device().CurrentPatch().depth[0][1] == 40);
        e_.patch_changed = nullptr;
        e_.Disconnect();
        Say("PASS plugin: project patch shown offline, sent to the device on connecting");
    }

    // Acid Seq (SQ protocol): pages from the device's descriptors, edits, project patch with
    // the confirmation of the device-first rule (D13).
    void AcidSeq() {
        e_.ConnectTo(PortIndex("Acid Seq (Sim)"));
        SqSession& s = e_.GetSqSession();
        for(int i = 0; i < 2000 && !s.Ready(); ++i)
            e_.Step(1);
        e_.RefreshNow();
        SELFTEST_CHECK(s.Ready() && s.Components().size() == 4 && e_.GetSqPanel().isVisible());
        SELFTEST_CHECK(e_.GetSqSimulator() && e_.GetSqSimulator()->Writes() == 0); // reading wrote nothing
        SELFTEST_CHECK(e_.GetSqPanel().ParamRows() >= 10);
        Shot("acid-seq-sequencer.png");
        e_.GetSqPanel().SelectTab(1); // voice
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("cutoff") != nullptr);
        s.SetParam(1, "cutoff", 3000);
        e_.Step(10);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.GetSqSimulator()->Value(1, 4) == 3000000);
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("cutoff")->getValue() == 3000.0);
        Shot("acid-seq-voice.png");
        // Song state: the patch text is the Acid Seq patch; a project load asks first.
        const std::string project = e_.PatchText();
        SELFTEST_CHECK(project.find("acid-seq-patch") != std::string::npos);
        s.SetParam(1, "cutoff", 500);
        e_.Step(10);
        int asked = 0;
        e_.sq_upload_decision = [&](const AcidCompat& c) {
            ++asked;
            return c.Clean() ? 0 : 0; // "Don't send": the device keeps its state
        };
        e_.LoadProjectPatch(project);
        e_.Step(50);
        SELFTEST_CHECK(asked == 1 && e_.GetSqSimulator()->Value(1, 4) == 500000);
        e_.sq_upload_decision = [&](const AcidCompat&) {
            ++asked;
            return 2; // "Always send"
        };
        e_.LoadProjectPatch(project);
        for(int i = 0; i < 400; ++i)
            e_.Step(1);
        SELFTEST_CHECK(asked == 2 && e_.GetSqSimulator()->Value(1, 4) == 3000000);
        e_.LoadProjectPatch(project); // "always": no question any more
        e_.Step(100);
        SELFTEST_CHECK(asked == 2);
        e_.sq_upload_decision = nullptr;
        e_.Disconnect();
        e_.RefreshNow();
        SELFTEST_CHECK(!e_.GetSqPanel().isVisible() && !e_.SqActive());
        Say("PASS Acid Seq: SQ pages from descriptors, edits confirmed, song patch asks first (no / always)");
    }

    // Streichwurst (SQ 1.1 + Mod on Daisy on one port): pages by contract page, the matrix
    // session alongside, edits, the product patch with embedded matrix (device first).
    void Streichwurst() {
        e_.ConnectTo(PortIndex("Streichwurst (Sim)"));
        SqSession& s = e_.GetSqSession();
        for(int i = 0; i < 4000 && !(s.Ready() && e_.GetSession().Connected()); ++i)
            e_.Step(1);
        e_.RefreshNow();
        SELFTEST_CHECK(s.Ready() && s.Identity().HasModMatrix() && s.Components().size() == 3);
        SELFTEST_CHECK(e_.GetSession().Connected()); // the matrix session on the same port
        SELFTEST_CHECK(e_.GetStreichwurstSimulator() && e_.GetStreichwurstSimulator()->Writes() == 0);
        SELFTEST_CHECK(e_.GetSqPanel().isVisible() && e_.GetSqPanel().Streichwurst());
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("registration") != nullptr); // page "Strings"
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("solo_tone") == nullptr);
        e_.GetSqPanel().ReadCounters(); // all counters readable, incl. callback peak and stack
        e_.Step(20);
        e_.RefreshNow();
        SELFTEST_CHECK(s.Stats().size() >= 17 && e_.GetSqPanel().CounterLines() == int(s.Stats().size()));
        Shot("streichwurst-strings.png");
        e_.GetSqPanel().SelectTab(3); // FX
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("reverb") != nullptr);
        s.SetParam(1, "reverb", 60);
        e_.Step(20);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.GetStreichwurstSimulator()->Value(1, 7) == 60000);
        SELFTEST_CHECK(e_.GetSqPanel().ParamSlider("reverb")->getValue() == 60.0);
        Shot("streichwurst-fx.png");
        // Matrix pages of the same device.
        e_.ShowMatrix(true);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.MatrixShown() && !e_.GetSqPanel().isVisible());
        SELFTEST_CHECK(e_.GetModel().columns[0].name == "Registr.");
        e_.GetSession().EditDepth(0, 50, true); // LFO 1 -> Registration
        e_.Step(200);
        e_.RefreshNow();
        SELFTEST_CHECK(e_.GetStreichwurstSimulator()->Matrix().CurrentPatch().depth[0][0] == 50);
        Shot("streichwurst-matrix.png");
        e_.ShowMatrix(false);
        // Song state: the Streichwurst patch with matrix; loading asks before sending.
        const std::string project = e_.PatchText();
        SELFTEST_CHECK(project.find("streichwurst-patch") != std::string::npos && project.find("\"matrix\"") != std::string::npos);
        s.SetParam(1, "reverb", 10);
        e_.GetSession().EditDepth(0, 0);
        e_.Step(200);
        int asked = 0;
        e_.sq_upload_decision = [&](const AcidCompat& c) {
            ++asked;
            return c.Clean() ? 1 : 0;
        };
        e_.LoadProjectPatch(project);
        for(int i = 0; i < 1500; ++i)
            e_.Step(1);
        // "Always send" from the Acid Seq part still holds for this instance: no question.
        SELFTEST_CHECK(asked <= 1 && e_.GetStreichwurstSimulator()->Value(1, 7) == 60000);
        SELFTEST_CHECK(e_.GetStreichwurstSimulator()->Matrix().CurrentPatch().depth[0][0] == 50);
        e_.sq_upload_decision = nullptr;
        e_.Disconnect();
        e_.RefreshNow();
        SELFTEST_CHECK(!e_.GetSqPanel().isVisible() && !e_.SqActive() && !e_.GetSession().Connected());
        Say("PASS Streichwurst: SQ pages by contract page, matrix alongside, edits, song patch with matrix");
    }

    // ------------------------------------------------------------------ SD card page
    // Steps the manual clock until the predicate holds; false after max_ms of simulated time.
    bool Wait(const std::function<bool()>& pred, uint32_t max_ms) {
        for(uint32_t t = 0; t < max_ms; t += 10) {
            if(pred())
                return true;
            e_.Step(10);
            e_.RefreshNow();
        }
        return pred();
    }

    // A Toy Piano has no SD card protocol: the page says so and offers nothing.
    void SdNoInstrument() {
        e_.ConnectTo(PortIndex("Simulator: Toy Piano"));
        SELFTEST_CHECK(Settle());
        e_.Step(2500); // the probe: the Toy Piano simulator refuses NB/LP SysEx, the port is backed off 5 s
        e_.RefreshNow();
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        SELFTEST_CHECK(wf.GetPhase() == sd::Phase::NotFound && !e_.SdPageShown() && e_.NoMatrixText().isEmpty());
        e_.ShowSdPage(true);
        e_.RefreshNow();
        ui::SdSharePanel& page = e_.GetSdPanel();
        SELFTEST_CHECK(page.Heading().startsWith("No Nebulae or Looper found"));
        SELFTEST_CHECK(!page.ShareButton().isEnabled() && !page.ReturnButton().isEnabled() && !page.AckButton().isEnabled());
        SELFTEST_CHECK(page.RowText(0) == juce::String::fromUTF8("–") && Visible(page));
        Shot("dcp-sd-page-idle.png");
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page: Toy Piano shows no instrument, matrix page unchanged");
    }

    // Connect, SHARE (with CONFIRM where the instrument needs it), SHARED, eject, RETURN,
    // patch restored, matrix live again. `matrix`: a MOD build with the matrix on the port.
    void SdCycle(const char* port, bool matrix, bool confirm_case, const char* shot) {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex(port));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000));
        sd::SimInstrument* sim = e_.GetSdSimulator();
        SELFTEST_CHECK(sim != nullptr);
        if(!sim)
            return;
        if(confirm_case)
            sim->live = true; // Nebulae: a Live loop only in RAM
        uint32_t before = 0;
        if(matrix) {
            SELFTEST_CHECK(Settle() && e_.GetSession().Connected());
            e_.GetSession().EditDepth(0 * 8 + 1, 40, true); // LFO 1 -> second column
            SELFTEST_CHECK(Settle() && InSync());
            before = e_.GetModel().Crc();
            SELFTEST_CHECK(!e_.SdPageShown() && e_.NoMatrixText().isEmpty());
            e_.ShowSdPage(true);
        } else {
            // D4: no matrix on this firmware: the app switches to the SD card page by itself.
            SELFTEST_CHECK(Wait([&] { return e_.SdPageShown(); }, 4000));
            SELFTEST_CHECK(!e_.GetSession().Connected() && e_.PortOpen());
            e_.ShowSdPage(false);
            e_.RefreshNow();
            SELFTEST_CHECK(e_.NoMatrixText().contains("has no modulation matrix"));
            e_.ShowSdPage(true);
        }
        e_.Step(1200); // a STATUS poll with the Live loop flag
        e_.RefreshNow();
        SELFTEST_CHECK(page.ShareButton().isEnabled() && !page.ReturnButton().isEnabled() && !page.AckButton().isEnabled());
        SELFTEST_CHECK(!page.EjectHelpButton().isEnabled() && !page.ConfirmButton().isVisible() && !page.CancelButton().isVisible());
        SELFTEST_CHECK(page.Heading().startsWith("Performance") && page.Heading().contains("SIMULATION"));
        SELFTEST_CHECK(page.RowText(0) == juce::String(wf.GetProfile().display_name) && page.RowText(2).startsWith("USB-MIDI"));
        SELFTEST_CHECK(page.RowText(3) == "card inserted" && page.RowText(5).contains("not a bench measurement"));
        SELFTEST_CHECK(Visible(page));
        if(wf.GetProfile().id == sd::Instrument::Looper)
            SELFTEST_CHECK(page.RowLabel(4) == "Loop:" && page.RowText(4).contains("saved in looper/loop.wav")
                           && page.ReturnButton().getButtonText() == "Return to the Looper");
        else
            SELFTEST_CHECK(page.RowLabel(4) == "Bank / sample:" && page.RowText(4).contains("bank 1 of 3")
                           && page.ReturnButton().getButtonText() == "Return to Nebulae");
        page.ShareButton().onClick();
        if(confirm_case) {
            SELFTEST_CHECK(Wait([&] { return wf.NeedsConfirmation(); }, 3000));
            SELFTEST_CHECK(page.ConfirmButton().isVisible() && page.CancelButton().isVisible() && !page.ShareButton().isEnabled());
            SELFTEST_CHECK(page.WarningText().contains("LOSES") && page.ConfirmButton().getButtonText() == "Discard Live loop and share");
            SELFTEST_CHECK(Visible(page));
            Shot((juce::String(shot) + "-confirm.png").toRawUTF8());
            page.CancelButton().onClick();
            e_.Step(50);
            e_.RefreshNow();
            SELFTEST_CHECK(wf.GetPhase() == sd::Phase::Performance && sim->restarts == 0 && !page.ConfirmButton().isVisible());
            SELFTEST_CHECK(e_.GetSession().Connected() == matrix); // cancelling kept the matrix
            page.ShareButton().onClick();
            SELFTEST_CHECK(Wait([&] { return wf.NeedsConfirmation(); }, 3000));
            page.ConfirmButton().onClick();
        }
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Shared; }, 25000));
        e_.RefreshNow();
        SELFTEST_CHECK(!e_.GetSession().Connected() && !e_.PortOpen() && sim->restarts == 1);
        SELFTEST_CHECK(page.Heading().startsWith("SD management") && page.MessageText().contains("Windows owns the card"));
        SELFTEST_CHECK(page.EjectHelpButton().isEnabled() && !page.ReturnButton().isEnabled() && !page.ShareButton().isEnabled());
        SELFTEST_CHECK(page.RowText(2).startsWith("USB CDC") && page.RowText(3) == "card reader: SHARED" && Visible(page));
        if(matrix)
            SELFTEST_CHECK(e_.LastAutosave().existsAsFile() && e_.SdRestorePending());
        else
            SELFTEST_CHECK(!e_.SdRestorePending());
        Shot((juce::String(shot) + "-shared.png").toRawUTF8());
        sim->Management().ReaderEject(); // Windows "Safely remove"
        SELFTEST_CHECK(Wait([&] { return page.ReturnButton().isEnabled(); }, 3000));
        SELFTEST_CHECK(page.MessageText().contains("ejected") && wf.Ejected());
        page.ReturnButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 25000));
        SELFTEST_CHECK(sim->restarts == 2 && sim->GetPersonality() == sd::SimInstrument::Personality::Perf);
        SELFTEST_CHECK(page.MessageText().contains(matrix && wf.GetProfile().id == sd::Instrument::Looper ? "loop.wav" : "Back in performance"));
        if(matrix) {
            SELFTEST_CHECK(Wait([&] { return e_.GetSession().Connected() && !e_.SdRestorePending(); }, 5000));
            SELFTEST_CHECK(Settle() && InSync() && e_.GetModel().Crc() == before && e_.GetModel().Depth(1) == 40);
            SELFTEST_CHECK(e_.LogText().contains("restored") || e_.LogText().contains("confirmed"));
        } else {
            e_.Step(1500);
            SELFTEST_CHECK(e_.PortOpen() && !e_.GetSession().Connected());
        }
        e_.ShowSdPage(false);
        e_.RefreshNow();
        Shot((juce::String(shot) + "-matrix.png").toRawUTF8());
        if(matrix) {
            e_.Disconnect();
        } else {
            // The header button says "Disconnect" while only the SD workflow holds the port,
            // and clicking it releases the port (review finding 3).
            SELFTEST_CHECK(e_.Busy() && e_.ConnectButton().getButtonText() == "Disconnect");
            e_.ConnectButton().onClick();
            e_.Step(50);
            e_.RefreshNow();
            SELFTEST_CHECK(!e_.PortOpen() && wf.GetPhase() == sd::Phase::NotFound && e_.ConnectButton().getButtonText() == "Connect");
        }
        e_.Step(100);
        SELFTEST_CHECK(e_.GetSdSimulator() && e_.GetSdSimulator()->Management().reader_state == ddp::ReaderState::Local);
        Say(juce::String("PASS SD page ") + port + ": connect, share" + (confirm_case ? " (confirm, cancel, confirm)" : "")
            + ", shared, eject, return" + (matrix ? ", patch restored, matrix live again" : ", no matrix (D4)"));
    }

    // Looper with an unsaved loop: the firmware saves first (SAVING), the page shows it.
    void SdLooperSaving() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Looper MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000));
        e_.GetSdSimulator()->unsaved = true;
        e_.ShowSdPage(true);
        e_.Step(1200);
        e_.RefreshNow();
        SELFTEST_CHECK(page.RowText(4).contains("not saved yet"));
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Saving; }, 2000));
        SELFTEST_CHECK(page.CancelButton().isVisible() && !page.ShareButton().isEnabled() && page.MessageText().contains("loop.wav"));
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Shared; }, 25000));
        SELFTEST_CHECK(!e_.GetSdSimulator()->unsaved && page.RowText(4).contains("reloaded from looper/loop.wav"));
        e_.GetSdSimulator()->Management().ReaderEject();
        SELFTEST_CHECK(Wait([&] { return page.ReturnButton().isEnabled(); }, 3000));
        page.ReturnButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance && e_.GetSession().Connected(); }, 25000));
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page Looper MOD: unsaved loop saved before the share (SAVING), shared, back");
    }

    // Windows access interrupted while SHARED: quarantined until acknowledged (no return,
    // no reshare), the question is asked, then RETURN works.
    void SdInterruption() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Nebulae MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000) && Settle());
        e_.ShowSdPage(true);
        e_.RefreshNow();
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Shared; }, 25000));
        e_.GetSdSimulator()->Management().ReaderBusReset();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Interrupted; }, 3000));
        SELFTEST_CHECK(page.AckButton().isEnabled() && !page.ReturnButton().isEnabled() && !page.EjectHelpButton().isEnabled());
        SELFTEST_CHECK(page.MessageText().contains("quarantined") && !wf.Ejected() && Visible(page));
        Shot("dcp-sd-page-interrupted.png");
        e_.Step(3000);
        e_.RefreshNow();
        SELFTEST_CHECK(wf.ReaderState() == ddp::ReaderState::Interrupted); // no automatic reshare or return
        e_.sd_ack_decision = [] { return false; };
        page.AckButton().onClick();
        e_.Step(300);
        e_.RefreshNow();
        SELFTEST_CHECK(wf.ReaderState() == ddp::ReaderState::Interrupted);
        e_.sd_ack_decision = [] { return true; };
        page.AckButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Local; }, 3000));
        SELFTEST_CHECK(page.ReturnButton().isEnabled() && e_.LogText().contains("acknowledged"));
        page.ReturnButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance && e_.GetSession().Connected(); }, 25000));
        e_.sd_ack_decision = nullptr;
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page interruption: quarantined, acknowledge asked (no, yes), return after LOCAL");
    }

    // D5 guard (review finding 1): the patch saved before a share goes only to the kit that
    // comes back from that share. A kit that never comes back (restart timeout) abandons the
    // restore; a device the user connects by hand never receives it.
    void SdRestoreNotToWrongDevice() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Nebulae MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000));
        SELFTEST_CHECK(Settle() && e_.GetSession().Connected());
        e_.GetSession().EditDepth(0 * 8 + 1, 40, true);
        SELFTEST_CHECK(Settle() && InSync());
        e_.ShowSdPage(true);
        e_.RefreshNow();
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Restarting; }, 3000));
        SELFTEST_CHECK(e_.SdRestorePending());
        e_.GetSdSimulator()->never_reenumerates = true; // the kit never comes back (T22)
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::NotFound; }, 25000));
        SELFTEST_CHECK(!e_.SdRestorePending() && e_.LogText().contains("did not come back"));
        // A device the user connects now gets nothing.
        e_.ShowSdPage(false);
        e_.ConnectTo(PortIndex("Simulator: Toy Piano"));
        SELFTEST_CHECK(Settle() && e_.GetSession().Connected() && e_.GetModel().Depth(1) == 0 && InSync());
        e_.Disconnect();
        // Pending restore, then a manual connect to another device: abandoned with a warning.
        e_.ConnectTo(PortIndex("Simulator: Looper MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000));
        SELFTEST_CHECK(Settle() && e_.GetSession().Connected());
        e_.GetSession().EditDepth(0 * 8 + 2, 33, true);
        SELFTEST_CHECK(Settle() && InSync());
        e_.ShowSdPage(true);
        e_.RefreshNow();
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Restarting; }, 3000));
        SELFTEST_CHECK(e_.SdRestorePending());
        e_.ShowSdPage(false);
        e_.ConnectTo(PortIndex("Simulator: Toy Piano")); // the user's own choice
        SELFTEST_CHECK(!e_.SdRestorePending()); // (the warning line is replaced by "Connecting to …" at once)
        SELFTEST_CHECK(Settle() && e_.GetSession().Connected() && e_.GetModel().Depth(2) == 0 && InSync());
        e_.Disconnect();
        Say("PASS SD page restore guard: abandoned after the restart timeout and on a manual connect; nothing sent to another device");
    }

    // Finding 5: Share waits until the matrix of the kit has been read, so the patch can be
    // saved; the confirmation takes a fresh snapshot.
    void SdShareGate() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Nebulae MOD"));
        e_.ShowSdPage(true);
        // The NB STATUS answers before the matrix is read: Share must still be blocked.
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000));
        SELFTEST_CHECK(e_.MatrixDetectionPending() || e_.GetSession().Connected());
        if(e_.MatrixDetectionPending()) {
            SELFTEST_CHECK(!page.ShareButton().isEnabled() && page.ShareButton().getButtonText().contains("reading the matrix"));
            SELFTEST_CHECK(!e_.RequestSdShare(false) && e_.GetSdSimulator()->restarts == 0);
            SELFTEST_CHECK(e_.LogText().contains("still being read"));
        }
        SELFTEST_CHECK(Settle() && e_.GetSession().Connected());
        e_.RefreshNow();
        SELFTEST_CHECK(!e_.MatrixDetectionPending() && page.ShareButton().isEnabled()
                       && page.ShareButton().getButtonText() == "Share SD card with Windows");
        // The snapshot is taken at the confirmation too: an edit made while the question is
        // open is what comes back.
        e_.GetSdSimulator()->live = true;
        e_.GetSession().EditDepth(0 * 8 + 3, 25, true);
        SELFTEST_CHECK(Settle() && InSync());
        e_.Step(1200);
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.NeedsConfirmation(); }, 3000));
        e_.GetSession().EditDepth(0 * 8 + 3, 77, true); // edited while the question is open
        SELFTEST_CHECK(Settle() && InSync());
        const uint32_t crc = e_.GetModel().Crc();
        page.ConfirmButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Shared; }, 25000));
        SELFTEST_CHECK(e_.SdRestorePending());
        e_.GetSdSimulator()->Management().ReaderEject();
        SELFTEST_CHECK(Wait([&] { return page.ReturnButton().isEnabled(); }, 3000));
        page.ReturnButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance && e_.GetSession().Connected() && !e_.SdRestorePending(); }, 25000));
        SELFTEST_CHECK(Settle() && InSync() && e_.GetModel().Crc() == crc && e_.GetModel().Depth(3) == 77);
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page share gate: blocked until the matrix is read, snapshot refreshed at the confirmation");
    }

    // Finding 3: the acknowledge question belongs to one interruption; answered after the
    // interruption changed, it sends nothing.
    void SdAckEpisode() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Nebulae MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000) && Settle());
        e_.ShowSdPage(true);
        e_.RefreshNow();
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Shared; }, 25000));
        e_.GetSdSimulator()->Management().ReaderBusReset();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Interrupted; }, 3000));
        const int episode = wf.InterruptionEpisode();
        std::function<void(bool)> answer;
        e_.sd_ack_prompt = [&answer](std::function<void(bool)> apply) { answer = std::move(apply); };
        page.AckButton().onClick();
        SELFTEST_CHECK(bool(answer)); // the question is open
        // Meanwhile the interruption is released by the kit (S4 on the kit) and happens again.
        e_.GetSdSimulator()->Management().reader_state = ddp::ReaderState::Local;
        e_.Step(1500);
        e_.RefreshNow();
        SELFTEST_CHECK(wf.ReaderState() == ddp::ReaderState::Local);
        e_.GetSdSimulator()->Management().reader_state = ddp::ReaderState::Shared;
        e_.GetSdSimulator()->Management().ReaderBusReset();
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Interrupted; }, 3000));
        SELFTEST_CHECK(wf.InterruptionEpisode() == episode + 1);
        answer(true); // Yes to the old question: nothing is sent
        e_.Step(500);
        e_.RefreshNow();
        SELFTEST_CHECK(wf.ReaderState() == ddp::ReaderState::Interrupted && e_.LogText().contains("changed while the question was open"));
        // A fresh question for the current interruption works.
        answer = nullptr;
        page.AckButton().onClick();
        SELFTEST_CHECK(bool(answer));
        answer(true);
        SELFTEST_CHECK(Wait([&] { return wf.ReaderState() == ddp::ReaderState::Local; }, 3000));
        e_.sd_ack_prompt = nullptr;
        page.ReturnButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 25000));
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page acknowledge episode: an old question releases nothing, a fresh one does");
    }

    // § 11.3: no card disables Share and says so; long texts stay inside the page.
    void SdNoCardAndLongTexts() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Nebulae MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000) && Settle());
        e_.GetSdSimulator()->card = false;
        e_.ShowSdPage(true);
        e_.Step(1500);
        e_.RefreshNow();
        SELFTEST_CHECK(!page.ShareButton().isEnabled() && page.RowText(3) == "no card");
        e_.GetSdSimulator()->card = true;
        e_.Step(1500);
        e_.RefreshNow();
        SELFTEST_CHECK(page.ShareButton().isEnabled() && page.RowText(3) == "card inserted");
        // Long texts at the smallest window the app allows: texts twice as long as the longest
        // real ones (§ 6) are shown completely; the Python test's absurd lengths (40 and 30
        // repetitions) keep every control inside the page and are truncated with an ellipsis.
        const int w = e_.getWidth(), h = e_.getHeight();
        e_.setSize(1200, 860);
        std::string error, message;
        for(int i = 0; i < 13; ++i)
            error += "Very long firmware error ";
        for(int i = 0; i < 18; ++i)
            message += "Very long instruction ";
        wf.SetDebugTexts(message, error);
        e_.RefreshNow();
        SELFTEST_CHECK(Visible(page));
        Shot("dcp-sd-page-long-texts.png");
        for(int i = 13; i < 30; ++i)
            error += "Very long firmware error ";
        for(int i = 18; i < 40; ++i)
            message += "Very long instruction ";
        wf.SetDebugTexts(message, error);
        e_.RefreshNow();
        SELFTEST_CHECK(page.AllVisibleInside(false) && page.ShareButton().getBottom() < page.getHeight());
        e_.setSize(w, h);
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page: no card disables Share, long texts stay inside the page");
    }

    // § 11.4: the Looper's failed save in the GUI: warning, confirm label, cancel, confirm.
    void SdLooperFailedSave() {
        sd::SdShareWorkflow& wf = e_.GetSdShare();
        ui::SdSharePanel& page = e_.GetSdPanel();
        e_.ConnectTo(PortIndex("Simulator: Looper MOD"));
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Performance; }, 3000) && Settle());
        e_.GetSdSimulator()->unsaved = true;
        e_.GetSdSimulator()->save_fails = true;
        e_.ShowSdPage(true);
        e_.Step(1200);
        e_.RefreshNow();
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.NeedsConfirmation(); }, 6000));
        SELFTEST_CHECK(page.ConfirmButton().isVisible() && page.ConfirmButton().getButtonText() == "Share without the unsaved changes");
        SELFTEST_CHECK(page.WarningText().contains("LOSES the unsaved changes") && page.RowText(4).contains("last card write failed"));
        page.CancelButton().onClick();
        e_.Step(100);
        e_.RefreshNow();
        SELFTEST_CHECK(!page.ConfirmButton().isVisible() && e_.GetSdSimulator()->restarts == 0);
        page.ShareButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.NeedsConfirmation(); }, 6000));
        page.ConfirmButton().onClick();
        SELFTEST_CHECK(Wait([&] { return wf.GetPhase() == sd::Phase::Management; }, 25000));
        e_.ShowSdPage(false);
        e_.Disconnect();
        Say("PASS SD page Looper failed save: warning, confirm label, cancel keeps playing, confirm shares");
    }

    // The VST3 content (D2): the SD card page carries the note only.
    void SdPluginNote() {
        EditorComponent plugin(false, false);
        plugin.ShowSdPage(true);
        plugin.RefreshNow();
        SELFTEST_CHECK(plugin.GetSdPanel().NoteText() == juce::String::fromUTF8(ui::SdSharePanel::kUnavailableNote));
        SELFTEST_CHECK(!plugin.GetSdPanel().ShareButton().isVisible());
        Say("PASS SD page in the plugin: D2 note only");
    }

    EditorComponent& e_;
    juce::File dir_, log_;
    int failures_ = 0;
};

class Application : public juce::JUCEApplication {
  public:
    const juce::String getApplicationName() override { return kAppName; }
    const juce::String getApplicationVersion() override { return MOD_EDITOR_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& command_line) override {
        const juce::StringArray args = juce::StringArray::fromTokens(command_line, true);
        const int i = args.indexOf("--selftest");
        window_ = std::make_unique<MainWindow>(getApplicationName() + " " + getApplicationVersion(),
                                               i >= 0 || args.contains("--simulators"));
        window_->Editor().SetAbout(getApplicationName() + " " + getApplicationVersion()
                                   + juce::String::fromUTF8(" · AGPL-3.0 · github.com/sscheidl/Daisy-Seed (editors/mod-on-daisy)"));
        if(i >= 0) {
            const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(
                args[i + 1].isEmpty() ? juce::String("selftest") : args[i + 1].unquoted());
            const int result = SelfTest(window_->Editor(), dir).Run();
            setApplicationReturnValue(result);
            quit();
            return;
        }
        window_->setVisible(true);
    }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }

  private:
    std::unique_ptr<MainWindow> window_;
};
}

START_JUCE_APPLICATION(Application)
