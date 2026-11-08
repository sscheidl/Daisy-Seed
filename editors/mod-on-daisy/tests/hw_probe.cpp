// Hardware probe of the SD card page's core against the real kit (no GUI): the same
// classes the Daisy Control Panel uses (MidiPort, SdShareWorkflow, DdpSession on the Win32
// serial ports, the Mod on Daisy Session) run a scripted share/eject/return cycle and log
// every step with wall-clock times. Read-only on the card: it lists the drive's root, writes
// nothing, and ejects through the Windows shell (the same as "Safely Remove Hardware").
//
//   DcpHardwareProbe status                   find the kit on USB-MIDI, print STATUS, leave
//   DcpHardwareProbe boot                     the app's "Firmware update" request: restart the kit
//                                             into the Daisy bootloader (for dfu-util), nothing is flashed
//   DcpHardwareProbe cycle [--confirm] [--no-matrix] [--out <dir>]
//                                             SHARE (confirming a Live loop only with --confirm),
//                                             wait for the drive, list it, eject, RETURN, verify
//                                             the matrix patch comes back
// Exit 0 only if every step succeeded. Never flashes, never writes to the card.
#include "../src/core/Bootloader.h"
#include "../src/core/SdShare.h"
#include "../src/core/SerialTransport.h"
#include "../src/core/Session.h"
#include "../src/ui/Theme.h"
#include "../src/ui/MidiPort.h"
#include "../src/ui/SerialPortWin.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
using namespace editor;
using Clock = std::chrono::steady_clock;

Clock::time_point g_start = Clock::now();
juce::File g_log;
int g_failures = 0;

void Say(const juce::String& line) {
    const double t = std::chrono::duration<double>(Clock::now() - g_start).count();
    const juce::String text = juce::String::formatted("[%8.3f] ", t) + line;
    std::printf("%s\n", text.toRawUTF8());
    std::fflush(stdout);
    if(g_log != juce::File())
        g_log.appendText(text + "\n");
}
void Check(bool ok, const juce::String& what) {
    if(ok)
        Say("PASS " + what);
    else {
        Say("FAIL " + what);
        ++g_failures;
    }
}

// The workflow's view of the one open MIDI port, as EditorComponent::SdLink does it.
struct Link : sd::MidiLink {
    ui::MidiPort* port;
    std::vector<std::vector<uint8_t>> queue;
    bool closed = false;
    explicit Link(ui::MidiPort* p) : port(p) {}
    bool Send(const uint8_t* b, std::size_t n) override { return !closed && port && port->Send(b, n); }
    bool Poll(std::vector<std::vector<uint8_t>>& out) override {
        out.swap(queue);
        queue.clear();
        return !out.empty() || (!closed && port != nullptr);
    }
    void Close() override { closed = true; }
    void Push(const uint8_t* b, std::size_t n) {
        if(queue.size() < 256)
            queue.emplace_back(b, b + n);
    }
};

struct Ports : sd::MidiPorts {
    std::function<std::vector<std::string>()> list;
    std::function<std::unique_ptr<sd::MidiLink>(const std::string&)> open;
    std::vector<std::string> List() override { return list(); }
    std::unique_ptr<sd::MidiLink> Open(const std::string& name, std::string& error) override {
        auto l = open(name);
        if(!l)
            error = "cannot open " + name;
        return l;
    }
};

struct Rig {
    std::unique_ptr<ddp::SerialPorts> serial = ddp::MakeWindowsSerialPorts();
    ddp::DdpSession session{serial.get()};
    Ports ports;
    sd::SdShareWorkflow wf{session, &ports};
    std::unique_ptr<ui::MidiPort> midi;
    Link* link = nullptr;
    Model model;
    Session md{model, 0x5D};
    bool matrix = true;
    juce::String port_name;
    std::vector<std::string> wf_log;

    Rig() {
        session.log = [](const std::string& t) { Say("ddp: " + juce::String::fromUTF8(t.c_str())); };
        wf.log = [this](const std::string& t) {
            wf_log.push_back(t);
            Say("wf:  " + juce::String::fromUTF8(t.c_str()));
        };
        md.log = [](LogKind k, const std::string& t) {
            Say(juce::String(k == LogKind::Error ? "md!  " : "md:  ") + juce::String::fromUTF8(t.c_str()));
        };
        ports.list = [this] {
            if(midi)
                return std::vector<std::string>{port_name.toStdString()};
            std::vector<std::string> out;
            for(const auto& d : juce::MidiOutput::getAvailableDevices())
                if(d.name.containsIgnoreCase("daisy"))
                    out.push_back(d.name.toStdString());
            return out;
        };
        ports.open = [this](const std::string& name) -> std::unique_ptr<sd::MidiLink> {
            if(!midi && !OpenMidi(juce::String::fromUTF8(name.c_str())))
                return nullptr;
            auto l = std::make_unique<Link>(midi.get());
            link = l.get();
            return l;
        };
    }

    bool OpenMidi(const juce::String& name) {
        juce::MidiDeviceInfo out, in;
        for(const auto& d : juce::MidiOutput::getAvailableDevices())
            if(d.name == name)
                out = d;
        for(const auto& d : juce::MidiInput::getAvailableDevices())
            if(d.name == name || (out.identifier.isNotEmpty() && d.name.containsIgnoreCase("daisy")))
                if(in.identifier.isEmpty() || d.name == name)
                    in = d;
        if(out.identifier.isEmpty() || in.identifier.isEmpty())
            return false;
        juce::String error;
        midi = ui::MidiPort::Open(out, in, error);
        if(!midi) {
            Say("MIDI open failed: " + error);
            return false;
        }
        port_name = name;
        Say("MIDI port open: " + out.name + " / " + in.name);
        if(matrix)
            md.Connect(*midi, Now());
        return true;
    }

    void CloseMidi() {
        if(link)
            link->port = nullptr;
        link = nullptr;
        md.Disconnect("");
        midi.reset();
    }

    static uint32_t Now() { return juce::Time::getMillisecondCounter(); }

    // One 10 ms step: pump JUCE, route SysEx, tick everything.
    void Step() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        const uint32_t now = Now();
        if(midi) {
            for(const std::vector<uint8_t>& m : midi->Take()) {
                if(sd::IsSdShareSysEx(m.data(), m.size())) {
                    if(link)
                        link->Push(m.data(), m.size());
                } else {
                    md.OnMessage(m.data(), m.size());
                }
            }
            md.Tick(now);
        }
        session.Tick(now);
        const sd::Phase before = wf.GetPhase();
        wf.Tick(now);
        if(before != wf.GetPhase()) {
            Say(juce::String("phase ") + sd::PhaseName(before) + " -> " + sd::PhaseName(wf.GetPhase()));
            if(wf.GetPhase() == sd::Phase::Restarting)
                CloseMidi(); // the kit restarts: release the port as the app does
        }
    }

    template <class Pred>
    bool Wait(const char* what, Pred pred, double seconds) {
        const Clock::time_point t0 = Clock::now();
        while(std::chrono::duration<double>(Clock::now() - t0).count() < seconds) {
            Step();
            if(pred()) {
                Say(juce::String("      ") + what + juce::String::formatted(" after %.2f s", std::chrono::duration<double>(Clock::now() - t0).count()));
                return true;
            }
        }
        return pred();
    }
};

#if defined(_WIN32)
std::set<char> RemovableDrives() {
    std::set<char> out;
    const DWORD mask = GetLogicalDrives();
    for(int i = 0; i < 26; ++i)
        if(mask & (1u << i)) {
            char root[] = {char('A' + i), ':', '\\', 0};
            if(GetDriveTypeA(root) == DRIVE_REMOVABLE)
                out.insert(char('A' + i));
        }
    return out;
}
juce::String ListRoot(char drive) {
    juce::File root(juce::String::charToString(drive) + ":\\");
    juce::String text;
    int n = 0;
    for(const auto& f : root.findChildFiles(juce::File::findFilesAndDirectories, false)) {
        text << (n ? ", " : "") << f.getFileName();
        if(++n >= 12) {
            text << ", …";
            break;
        }
    }
    return text.isEmpty() ? juce::String("(empty root)") : text;
}
bool EjectDrive(char drive) {
    // The same verb as Explorer's "Eject" / "Safely Remove Hardware".
    const juce::String cmd = juce::String("powershell.exe -NoProfile -Command \"(New-Object -ComObject Shell.Application).Namespace(17).ParseName('")
                             + juce::String::charToString(drive) + ":').InvokeVerb('Eject')\"";
    juce::ChildProcess p;
    if(!p.start(cmd))
        return false;
    p.waitForProcessToFinish(15000);
    return p.getExitCode() == 0;
}
#endif

juce::String StatusText(const sd::PerfStatus& s) {
    juce::String t = juce::String(s.instrument == sd::Instrument::Nebulae ? "Nebulae" : "Looper") + " " + s.firmware
                     + (s.card ? " card" : " NO CARD");
    if(s.instrument == sd::Instrument::Nebulae)
        t << " live=" << int(s.live) << " rec=" << int(s.recording) << " load=" << int(s.loading) << " bank " << s.bank << "/" << s.banks
          << " playing " << s.playing_bank << ":" << s.playing_slot << " banks=0x" << juce::String::toHexString(s.bank_mask)
          << " slots=0x" << juce::String::toHexString(s.slot_mask) << " loaderr=" << s.load_error;
    else
        t << " loop=" << int(s.loop) << " unsaved=" << int(s.unsaved) << " saving=" << int(s.saving) << " rec=" << int(s.recording)
          << " import=" << int(s.importing) << " savefail=" << int(s.save_failed) << " fault=" << s.fault << " ms=" << s.loop_ms;
    t << " overruns=" << s.overruns << " peak=" << s.peak_load << "%" << (s.overrun ? " OVERRUN" : "");
    return t;
}

int Status(Rig& r) {
    Check(r.Wait("STATUS on USB-MIDI", [&] { return r.wf.GetPhase() == sd::Phase::Performance; }, 8.0), "kit found on USB-MIDI");
    if(r.wf.Status())
        Say("STATUS: " + StatusText(*r.wf.Status()));
    if(r.matrix) {
        const bool connected = r.Wait("matrix connected", [&] { return r.md.Connected() && r.md.Idle(); }, 6.0);
        Check(connected || !r.md.Connected(), connected ? "matrix session connected" : "matrix session not connected (plain image?)");
        if(r.md.Connected()) {
            juce::String cols;
            for(const Column& c : r.model.columns)
                cols << (c.present ? juce::String(c.name) : juce::String("-")) << " ";
            Say("matrix: " + juce::String(r.md.Device().key) + " " + r.md.Device().version + ", columns " + cols);
        }
    }
    return g_failures ? 1 : 0;
}

// The "Firmware update …" button of the app: common/bootloader request over the MIDI port.
int Boot(Rig& r) {
    r.matrix = false;
    Check(r.Wait("STATUS on USB-MIDI", [&] { return r.wf.GetPhase() == sd::Phase::Performance; }, 8.0), "kit found on USB-MIDI");
    if(!r.midi)
        return 1;
    BootRequest boot;
    ui::MidiPort* port = r.midi.get();
    Check(boot.Start(Rig::Now(), [port](const uint8_t* b, std::size_t n) { return port->Send(b, n); }), "bootloader request sent");
    BootRequest::Result result = BootRequest::Result::None;
    const Clock::time_point t0 = Clock::now();
    while(result == BootRequest::Result::None && std::chrono::duration<double>(Clock::now() - t0).count() < 3.0) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if(r.midi)
            for(const std::vector<uint8_t>& m : r.midi->Take())
                boot.OnMessage(m.data(), m.size());
        result = boot.Poll(Rig::Now());
    }
    Check(result == BootRequest::Result::Acknowledged, "kit acknowledged the bootloader request");
    r.CloseMidi();
    return g_failures ? 1 : 0;
}

int Cycle(Rig& r, bool confirm) {
    if(Status(r))
        return 1;
    const bool has_matrix = r.md.Connected();
    uint32_t crc_before = 0;
    mod::Patch saved{};
    if(has_matrix) {
        // A route the restore has to bring back: LFO 1 -> column 1, depth 40.
        r.md.EditDepth(0 * 8 + 1, 40, true);
        Check(r.Wait("edit confirmed", [&] { return r.md.Idle(); }, 5.0) && r.model.Depth(1) == 40, "matrix edit LFO1 -> column 1 = 40 confirmed");
        crc_before = r.model.Crc();
        saved = r.model.patch;
        Say("patch CRC before the share " + juce::String(Hex32(crc_before)));
    }
#if defined(_WIN32)
    const std::set<char> drives_before = RemovableDrives();
#endif
    Check(r.wf.CanShare(), "CanShare()");
    const Clock::time_point t_share = Clock::now();
    Check(r.wf.RequestShare(false), "SHARE requested");
    bool proceeded = r.Wait("SHARE answered", [&] { return r.wf.GetPhase() != sd::Phase::Requesting; }, 5.0);
    if(r.wf.GetPhase() == sd::Phase::Confirm) {
        Say("CONFIRM_REQUIRED: " + juce::String(r.wf.Message()));
        if(!confirm) {
            Say("not confirming (run with --confirm to discard the Live loop); cancelling");
            r.wf.CancelConfirmation();
            Check(r.wf.GetPhase() == sd::Phase::Performance, "cancel keeps playing");
            return g_failures ? 1 : 2;
        }
        Check(r.wf.RequestShare(true), "SHARE confirmed");
        proceeded = r.Wait("confirmed SHARE answered", [&] { return r.wf.GetPhase() != sd::Phase::Requesting; }, 5.0);
    }
    while(r.wf.GetPhase() == sd::Phase::Saving)
        r.Wait("saving", [&] { return r.wf.GetPhase() != sd::Phase::Saving; }, 200.0);
    Check(proceeded && (r.wf.GetPhase() == sd::Phase::Restarting || r.wf.GetPhase() == sd::Phase::Management),
          "SHARE accepted (phase " + juce::String(sd::PhaseName(r.wf.GetPhase())) + ")" + (r.wf.Error().empty() ? "" : " error: " + juce::String(r.wf.Error())));
    if(g_failures)
        return 1;
    Check(r.Wait("SD management over USB CDC", [&] { return r.wf.GetPhase() == sd::Phase::Management; }, 25.0),
          "kit back in SD management (CDC) " + juce::String(r.session.Port()));
    Check(r.Wait("READER SHARED", [&] { return r.wf.ReaderState() == ddp::ReaderState::Shared; }, 25.0), "card reader SHARED");
    Say("message: " + juce::String(r.wf.Message()));
    char drive = 0;
#if defined(_WIN32)
    Check(r.Wait("Windows drive", [&] {
        for(char d : RemovableDrives())
            if(!drives_before.count(d)) {
                drive = d;
                return true;
            }
        return false;
    }, 40.0), drive ? juce::String("drive ") + juce::String::charToString(drive) + ": appeared" : "a new removable drive appeared");
    Say(juce::String::formatted("time SHARE click -> drive letter: %.1f s", std::chrono::duration<double>(Clock::now() - t_share).count()));
    if(drive) {
        r.Wait("mount settle", [] { return false; }, 2.0);
        Say(juce::String("root of ") + juce::String::charToString(drive) + ": " + ListRoot(drive));
        Check(!r.wf.CanReturn(), "Return disabled while Windows owns the card");
        Say("ejecting through the Windows shell …");
        const bool ejected = EjectDrive(drive);
        Say(ejected ? "eject verb returned" : "eject verb failed");
    }
#endif
    Check(r.Wait("READER EJECTED", [&] { return r.wf.ReaderState() == ddp::ReaderState::Ejected; }, 30.0), "firmware reports EJECTED");
    Check(r.wf.Ejected() && r.wf.CanReturn(), "Ejected() and CanReturn()");
    Say("message: " + juce::String(r.wf.Message()));
    const Clock::time_point t_return = Clock::now();
    Check(r.wf.ReturnToPerformance(), "RETURN requested");
    Check(r.Wait("PERFORM sent", [&] { return r.wf.GetPhase() == sd::Phase::Returning; }, 25.0), "READER RETURN then PERFORM");
    Check(r.Wait("USB-MIDI back", [&] { return r.wf.GetPhase() == sd::Phase::Performance; }, 30.0), "kit back in performance on USB-MIDI");
    Say(juce::String::formatted("time RETURN click -> STATUS on MIDI: %.1f s", std::chrono::duration<double>(Clock::now() - t_return).count()));
    Say("message: " + juce::String(r.wf.Message()));
    if(r.wf.Status())
        Say("STATUS: " + StatusText(*r.wf.Status()));
    if(has_matrix) {
        Check(r.Wait("matrix reconnected", [&] { return r.md.Connected() && r.md.Idle(); }, 8.0), "matrix session reconnected");
        Say("matrix after the restart: depth(1)=" + juce::String(r.model.Depth(1)) + " (expected empty), CRC " + juce::String(Hex32(r.model.Crc())));
        r.md.Upload(saved); // what RestoreAfterShare() does in the app
        Check(r.Wait("restore confirmed", [&] { return r.md.Idle(); }, 8.0) && r.model.Crc() == crc_before && r.model.Depth(1) == 40,
              "patch restored, CRC " + juce::String(Hex32(r.model.Crc())) + " == " + juce::String(Hex32(crc_before)));
    }
    return g_failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    std::string mode = argc > 1 ? argv[1] : "status";
    bool confirm = false, matrix = true;
    juce::File out = juce::File::getCurrentWorkingDirectory();
    for(int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if(a == "--confirm")
            confirm = true;
        else if(a == "--no-matrix")
            matrix = false;
        else if(a == "--out" && i + 1 < argc)
            out = juce::File(juce::String::fromUTF8(argv[++i]));
    }
    out.createDirectory();
    g_log = out.getChildFile("hw-probe-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".log");
    Say("Daisy Control Panel hardware probe, mode " + juce::String(mode) + (confirm ? " --confirm" : "") + (matrix ? "" : " --no-matrix"));
    Rig r;
    r.matrix = matrix;
    int code = 0;
    if(mode == "status")
        code = Status(r);
    else if(mode == "boot")
        code = Boot(r);
    else if(mode == "cycle")
        code = Cycle(r, confirm);
    else {
        std::printf("usage: DcpHardwareProbe status | boot | cycle [--confirm] [--no-matrix] [--out <dir>]\n");
        return 2;
    }
    r.CloseMidi();
    r.session.Disconnect("probe finished");
    Say(juce::String(code == 0 ? "RESULT PASS" : code == 2 ? "RESULT STOPPED (confirmation needed)" : "RESULT FAIL") + ", log " + g_log.getFullPathName());
    return code;
}
