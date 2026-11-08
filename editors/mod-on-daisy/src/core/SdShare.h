#pragma once
// The share/return workflow of the instruments' microSD (Nebulae on Daisy, Looper): finds
// the kit on USB-MIDI by its STATUS reply, asks for SD management, follows the kit to the
// COM port, opens the card for Windows once, and brings the kit back after a Windows eject.
// Port of diagnostics/windows/daisy_diag/nebulae.py (NebulaeWorkflow); every state,
// transition, timeout and card-safety rule is listed in docs/dcp/SD_SHARE_PORT.md § 3–5, § 10.
//
// Single-threaded and clock-injected like Session: Tick(now_ms) on the GUI thread. The MIDI
// side is an interface (the control panel shares its JUCE port, tests use the simulator).
// The panel never infers an eject: only the firmware's READER state EJECTED counts.
#include "DdpSession.h"
#include "SdShareProtocol.h"
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace editor::sd {
// A MIDI link that carries the instrument's control SysEx. false from Send() or Poll():
// the link is gone (device restarted, port vanished).
class MidiLink {
  public:
    virtual ~MidiLink() = default;
    virtual bool Send(const uint8_t* bytes, std::size_t n) = 0;
    // Complete SysEx frames F0 … F7 received since the last call.
    virtual bool Poll(std::vector<std::vector<uint8_t>>& out) = 0;
    virtual void Close() = 0;
};

class MidiPorts {
  public:
    virtual ~MidiPorts() = default;
    // Ports that may be an instrument (identified afterwards by STATUS).
    virtual std::vector<std::string> List() = 0;
    virtual std::unique_ptr<MidiLink> Open(const std::string& name, std::string& error) = 0;
};

enum class Phase { NotFound, Performance, Confirm, Requesting, Saving, Restarting, Management, Returning };

inline const char* PhaseText(Phase p) {
    switch(p) {
        case Phase::NotFound: return "No Nebulae or Looper found";
        case Phase::Performance: return "Performance (USB-MIDI)";
        case Phase::Confirm: return "Confirmation required";
        case Phase::Requesting: return "Requesting SD management …";
        case Phase::Saving: return "Saving the loop before sharing …";
        case Phase::Restarting: return "Restarting into SD management …";
        case Phase::Management: return "SD management (USB CDC + mass storage)";
        case Phase::Returning: return "Returning to performance …";
    }
    return "?";
}

inline const char* PhaseName(Phase p) {
    switch(p) {
        case Phase::NotFound: return "NOT_FOUND";
        case Phase::Performance: return "PERFORMANCE";
        case Phase::Confirm: return "CONFIRM";
        case Phase::Requesting: return "REQUESTING";
        case Phase::Saving: return "SAVING";
        case Phase::Restarting: return "RESTARTING";
        case Phase::Management: return "MANAGEMENT";
        case Phase::Returning: return "RETURNING";
    }
    return "?";
}

class SdShareWorkflow {
  public:
    static constexpr uint32_t kStatusIntervalMs = 1000;  // performance: STATUS poll while connected
    static constexpr uint32_t kStatusTimeoutMs = 3000;   // performance: link considered lost without a reply
    static constexpr uint32_t kProbeTimeoutMs = 1000;    // a MIDI port that does not answer STATUS is not an instrument
    static constexpr uint32_t kShareTimeoutMs = 2000;    // SHARE reply
    static constexpr uint32_t kRestartTimeoutMs = 20000; // bootloader skip + USB enumeration on Windows
    static constexpr uint32_t kScanIntervalMs = 1000;
    static constexpr uint32_t kSaveRetryMs = 500;        // looper: ask again while the firmware saves the loop
    static constexpr uint32_t kSaveTimeoutMs = 180000;   // looper: an 87 s loop is 33 MB; card speed NOT MEASURED
    static constexpr uint32_t kFailedPortBackoffMs = 5000;

    std::function<void(const std::string&)> log;
    std::function<void()> changed;

    // `token_source` fills four bytes; they are masked to 7 bit. Default: std::random_device.
    SdShareWorkflow(ddp::DdpSession& session, MidiPorts* midi, std::function<void(uint8_t*)> token_source = {})
        : session_(session), midi_(midi), tokens_(std::move(token_source)) {
        // Only an instrument in SD management is a partner; another app's DDP device (the
        // diagnostic firmware) is skipped so the next port gets its turn.
        session_.accept = [](const ddp::DeviceInfo& d) {
            const Profile* p = ProfileForApp(d.app);
            return p != nullptr && d.Has("sdmsc") && d.Has(p->capability);
        };
        if(!tokens_) {
            auto rng = std::make_shared<std::mt19937>(std::random_device{}());
            tokens_ = [rng](uint8_t* t) {
                for(int i = 0; i < 4; ++i)
                    t[i] = uint8_t((*rng)() & 0x7F);
            };
        }
    }
    void SetMidiPorts(MidiPorts* midi) { midi_ = midi; }

    // ------------------------------------------------------------------ queries for the GUI
    Phase GetPhase() const { return phase_; }
    const Profile& GetProfile() const { return *profile_; }
    std::string Name() const { return profile_->name; }
    const std::optional<PerfStatus>& Status() const { return status_; }
    const std::string& Port() const { return port_; }
    const std::string& Message() const { return message_; }
    const std::string& Error() const { return error_; }
    bool LinkOpen() const { return link_ != nullptr; }
    bool Probing() const { return probing_; }
    // The workflow follows the kit through a restart it caused: the control panel then
    // offers its Daisy MIDI ports to Open() instead of only the connected one.
    bool Following() const { return phase_ == Phase::Restarting || phase_ == Phase::Returning; }
    // A share the user requested will continue after the restart (set by ACCEPTED only; it
    // expires with the restart timeout and dies with a lost CDC session or another instrument).
    bool ShareAfterRestart() const { return share_after_restart_; }
    // A RETURN from EJECTED whose confirmation is still awaited before PERFORM.
    bool PerformAfterReturn() const { return perform_after_return_; }
    // Counts entries into INTERRUPTED/ERROR; an acknowledge dialog belongs to one episode.
    int InterruptionEpisode() const { return interruption_episode_; }
    // Test hook (the GUI's long-text check): texts as the page shows them.
    void SetDebugTexts(const std::string& message, const std::string& error) {
        message_ = message;
        error_ = error;
        Changed();
    }

    const Profile* ManagementProfile() const {
        if(!(session_.Connected() && session_.Device()))
            return nullptr;
        return ProfileForApp(session_.Device()->app);
    }
    bool ManagementConnected() const { return ManagementProfile() != nullptr; }
    std::optional<ddp::ReaderState> ReaderState() const {
        if(!ManagementConnected() || !session_.Reader())
            return std::nullopt;
        return session_.Reader()->state;
    }
    bool CanShare() const {
        return (phase_ == Phase::Performance || phase_ == Phase::Confirm) && link_ != nullptr && status_ && status_->card
            && !status_->Busy();
    }
    bool NeedsConfirmation() const { return phase_ == Phase::Confirm; }
    bool CanShareInManagement() const {
        const std::optional<bool> card = session_.SdCard();
        return ManagementConnected() && ReaderState() == ddp::ReaderState::Local && card && *card && !session_.ReaderPending();
    }
    bool Ejected() const { return ReaderState() == ddp::ReaderState::Ejected; } // firmware truth only
    bool CanReturn() const {
        const std::optional<ddp::ReaderState> r = ReaderState();
        return ManagementConnected() && !session_.ReaderPending() && phase_ != Phase::Returning
            && (r == ddp::ReaderState::Ejected || r == ddp::ReaderState::Local)
            && !(session_.Reader() && session_.Reader()->busy);
    }
    bool CanAcknowledge() const {
        const std::optional<ddp::ReaderState> r = ReaderState();
        return ManagementConnected() && !session_.ReaderPending()
            && (r == ddp::ReaderState::Interrupted || r == ddp::ReaderState::Error);
    }

    // ------------------------------------------------------------------ actions
    // Share the card with Windows. When data would be lost (Nebulae's Live loop, a looper
    // loop that cannot be saved) the firmware answers CONFIRM_REQUIRED; the GUI shows the
    // warning and calls again with confirm = true.
    bool RequestShare(bool confirm = false) {
        if(ManagementConnected()) {
            if(!CanShareInManagement())
                return false;
            return session_.RequestReader("SHARE");
        }
        if(!CanShare() || (phase_ == Phase::Confirm && !confirm))
            return false;
        tokens_(token_);
        for(uint8_t& t : token_)
            t &= 0x7F;
        confirm_ = confirm;
        if(!SendShare())
            return false;
        phase_ = Phase::Requesting;
        deadline_ = now_ + kShareTimeoutMs;
        message_ = "Waiting for the firmware to accept SD management …";
        Changed();
        return true;
    }

    void CancelConfirmation() {
        if(phase_ == Phase::Confirm || phase_ == Phase::Saving) {
            phase_ = Phase::Performance;
            deadline_.reset();
            resend_at_.reset();
            message_ = "Cancelled: " + Name() + " keeps playing; nothing was shared.";
            Changed();
        }
    }

    // Explicit acknowledgement of an interrupted Windows access (no data recovery).
    bool AcknowledgeInterruption() {
        if(!CanAcknowledge())
            return false;
        return session_.RequestReader("RETURN");
    }

    // Back to the instrument: only after Windows ejected (or from LOCAL).
    bool ReturnToPerformance() {
        if(!CanReturn())
            return false;
        if(ReaderState() == ddp::ReaderState::Ejected) {
            if(!session_.RequestReader("RETURN"))
                return false;
            // PERFORM follows only the matching RETURN reply on this very connection (T27/T29).
            perform_after_return_ = true;
            return_generation_ = session_.Generation();
            return_failures_ = session_.ReaderFailures();
            return_completions_ = session_.ReaderCompletions();
            message_ = "Returning the card to the firmware …";
            Changed();
            return true;
        }
        return SendPerform();
    }

    // A MIDI port was just opened by the user: probe it with the next Tick() instead of
    // waiting for the scan interval.
    void ScanNow() { scan_requested_ = true; }

    // Drops the MIDI link (user disconnect, application exit). Nothing is sent to the card
    // reader; the CDC session is the caller's business.
    void ReleaseMidi() {
        CloseLink();
        if(phase_ == Phase::Performance || phase_ == Phase::Confirm || phase_ == Phase::Requesting || phase_ == Phase::Saving) {
            phase_ = Phase::NotFound;
            status_.reset();
            deadline_.reset();
            resend_at_.reset();
            Changed();
        }
    }

    // ------------------------------------------------------------------ polling
    void Tick(uint32_t now_ms) {
        now_ = now_ms;
        if(!started_) { // the clock may start anywhere (ms since boot): arm the scan now
            started_ = true;
            next_scan_ = now_;
        }
        PollManagement();
        PollMidi();
        if(deadline_ && int32_t(now_ - *deadline_) > 0)
            Timeout();
    }

  private:
    bool Elapsed(uint32_t at) const { return int32_t(now_ - at) >= 0; }

    void Log(const std::string& text) {
        if(log)
            log(text);
    }
    void Changed() {
        if(changed)
            changed();
    }

    bool SendShare() {
        std::vector<uint8_t> frame;
        ShareRequest(*profile_, confirm_, token_, frame);
        if(!link_ || !link_->Send(frame.data(), frame.size())) {
            Lost("MIDI send failed");
            return false;
        }
        return true;
    }

    bool SendPerform() {
        if(!session_.Send(profile_->perform_command))
            return false;
        perform_after_return_ = false;
        phase_ = Phase::Returning;
        deadline_ = now_ + kRestartTimeoutMs;
        message_ = profile_->texts.returning;
        Log(Name() + ": return to performance requested");
        Changed();
        return true;
    }

    void PollManagement() {
        const Profile* profile = ManagementProfile();
        if(!profile)
            return;
        if(phase_ == Phase::NotFound || phase_ == Phase::Restarting || phase_ == Phase::Performance) {
            if(share_after_restart_ && profile != profile_) {
                // Another instrument answered on CDC: the share the user asked for belonged
                // to the kit found on MIDI, not to this one.
                share_after_restart_ = false;
                Log(std::string(profile->name) + " answered in SD management instead of " + Name()
                    + "; the share request is dropped. Use Share again on this connection if you mean it.");
            }
            profile_ = profile;
            if(phase_ == Phase::Restarting)
                Log(Name() + ": SD management connected over USB CDC");
            else if(phase_ == Phase::NotFound)
                Log(Name() + " found in SD management mode");
            CloseLink();
            phase_ = Phase::Management;
            deadline_.reset();
            message_.clear();
            Changed();
        }
        if(phase_ != Phase::Management)
            return;
        const std::optional<ddp::ReaderState> state = ReaderState();
        if(share_after_restart_ && int32_t(now_ - intent_until_) > 0) {
            // The kit did not become ready within the restart window: the request is over.
            share_after_restart_ = false;
            Log(Name() + ": the share request expired before the card was ready (no card?); use Share again.");
        }
        if(share_after_restart_ && state == ddp::ReaderState::Local && CanShareInManagement()) {
            // The user asked for the share before the restart; continue it exactly once.
            share_after_restart_ = false;
            if(session_.RequestReader("SHARE"))
                message_ = "Opening the card for Windows …";
        }
        if(perform_after_return_) {
            const ddp::ReaderInfo* reader = session_.Reader() ? &*session_.Reader() : nullptr;
            if(session_.Generation() != return_generation_ || session_.ReaderFailures() != return_failures_) {
                perform_after_return_ = false;
                Log(Name() + ": the return was not confirmed by the firmware; nothing is restarted. Check the card "
                             "reader state and use Return again.");
                Changed();
            } else if(session_.ReaderCompletions() > return_completions_ && session_.LastCompletedAction() == "RETURN"
                      && reader && reader->state == ddp::ReaderState::Local && !reader->busy && !session_.ReaderPending()) {
                message_ = "Card returned by the firmware; restarting into performance …";
                SendPerform();
                return;
            }
        }
        if(state) {
            const bool quarantined = *state == ddp::ReaderState::Interrupted || *state == ddp::ReaderState::Error;
            if(quarantined && !last_quarantined_)
                ++interruption_episode_;
            last_quarantined_ = quarantined;
        }
        const Texts& t = profile_->texts;
        std::string next = message_;
        if(!state)
            next = "Waiting for the card-reader status …";
        else
            switch(*state) {
                case ddp::ReaderState::Local: next = t.local; break;
                case ddp::ReaderState::Starting: next = "Preparing the card for Windows …"; break;
                case ddp::ReaderState::Shared: next = t.shared; break;
                case ddp::ReaderState::Ejected: next = t.ejected; break;
                case ddp::ReaderState::Interrupted:
                    next = "Access was interrupted (USB reset, card removed or I/O error). Windows may have lost pending "
                           "changes. The card stays quarantined until you acknowledge.";
                    break;
                case ddp::ReaderState::Error:
                    next = "Card I/O failed. Windows may have lost pending changes. The card stays quarantined until you "
                           "acknowledge.";
                    break;
            }
        if(next != message_) {
            message_ = next;
            Changed();
        }
    }

    void PollMidi() {
        if(phase_ == Phase::Management) {
            if(session_.Connected())
                return; // the kit is on CDC: no MIDI traffic
            // CDC gone without a requested return: ownership unknown. The intents die with
            // the connection (T31). The MIDI side is probed again so a kit that was
            // power-cycled back into performance is found (finding 7); the reader state
            // stays unknown until the firmware reports it.
            const std::string lost = "SD management connection lost; card ownership is unknown.";
            if(message_ != lost) {
                message_ = lost;
                if(share_after_restart_ || perform_after_return_)
                    Log(Name() + ": the pending request is dropped with the lost connection.");
                share_after_restart_ = perform_after_return_ = false;
                Changed();
            }
        }
        if(!link_) {
            // While RESTARTING keep scanning: a device that comes back on USB-MIDI did not
            // enter SD management (boot intent lost, risk R1) and is reported.
            if(phase_ == Phase::Requesting)
                return;
            if(scan_requested_ || Elapsed(next_scan_)) {
                scan_requested_ = false;
                next_scan_ = now_ + kScanIntervalMs;
                TryOpen();
            }
            return;
        }
        std::vector<std::vector<uint8_t>> messages;
        if(!link_->Poll(messages)) {
            Lost("MIDI connection lost");
            return;
        }
        for(const std::vector<uint8_t>& m : messages) {
            Reply reply;
            const Profile* profile = ParseAny(m.data(), m.size(), reply);
            if(!profile)
                continue; // other SysEx on the same port
            if(probing_ && reply.kind == Reply::Status)
                profile_ = profile; // the instrument on this port
            else if(profile != profile_)
                continue;
            last_reply_ = now_;
            Handle(reply);
            if(!link_)
                return;
        }
        if(probing_ && now_ - last_status_sent_ > kProbeTimeoutMs) {
            failed_ports_[port_] = now_ + kFailedPortBackoffMs;
            CloseLink();
            Changed();
            return;
        }
        if(phase_ == Phase::Performance || phase_ == Phase::Confirm || phase_ == Phase::Saving) {
            if(now_ - last_reply_ > kStatusTimeoutMs) {
                Lost(Name() + " stopped answering on USB-MIDI");
                return;
            }
            if(now_ - last_status_sent_ >= kStatusIntervalMs)
                SendStatus();
        }
        if(phase_ == Phase::Saving && resend_at_ && Elapsed(*resend_at_)) {
            // Ask again with the same token until the loop on the card is current.
            resend_at_.reset();
            if(SendShare())
                phase_ = Phase::Saving;
        }
    }

    void TryOpen() {
        if(!midi_)
            return;
        for(auto it = failed_ports_.begin(); it != failed_ports_.end();) // expired entries go (finding 14)
            it = Elapsed(it->second) ? failed_ports_.erase(it) : std::next(it);
        for(const std::string& name : midi_->List()) {
            auto backoff = failed_ports_.find(name);
            if(backoff != failed_ports_.end() && !Elapsed(backoff->second))
                continue;
            std::string error;
            std::unique_ptr<MidiLink> link = midi_->Open(name, error);
            if(!link) {
                failed_ports_[name] = now_ + kFailedPortBackoffMs;
                error_ = error;
                Changed();
                continue;
            }
            link_ = std::move(link);
            port_ = name;
            probing_ = true;
            last_reply_ = now_;
            SendStatus();
            if(!link_) // the port refused the probe: back it off like a silent one (T3)
                failed_ports_[name] = now_ + kFailedPortBackoffMs;
            Changed();
            return;
        }
    }

    void SendStatus() {
        if(!link_)
            return;
        // While probing, every profile asks; each firmware ignores foreign SysEx.
        if(probing_) {
            for(const Profile* p : kProfiles) {
                const std::vector<uint8_t> request = StatusRequest(*p);
                if(!link_->Send(request.data(), request.size()))
                    return Lost("MIDI send failed");
            }
        } else {
            const std::vector<uint8_t> request = StatusRequest(*profile_);
            if(!link_->Send(request.data(), request.size()))
                return Lost("MIDI send failed");
        }
        last_status_sent_ = now_;
    }

    void Handle(const Reply& r) {
        if(r.kind == Reply::Status) {
            probing_ = false;
            status_ = r.status;
            error_.clear();
            if(phase_ == Phase::NotFound || phase_ == Phase::Returning || phase_ == Phase::Restarting
               || (phase_ == Phase::Management && !session_.Connected())) {
                if(phase_ == Phase::Returning) {
                    message_ = profile_->texts.back;
                    Log(Name() + " is back in performance");
                } else if(phase_ == Phase::Management) {
                    message_ = Name() + " is back on USB-MIDI after the lost SD management connection.";
                    Log(message_);
                } else if(phase_ == Phase::Restarting) {
                    // R1: the device came back in performance instead of SD management.
                    error_ = Name() + " restarted but did not enter SD management (boot intent not kept). Nothing was shared.";
                    share_after_restart_ = false;
                    Log(error_);
                } else {
                    Log(Name() + " " + r.status.firmware + " found on " + port_);
                }
                phase_ = Phase::Performance;
                deadline_.reset();
            }
            Changed();
        } else if(r.kind == Reply::Share) {
            if(!(phase_ == Phase::Requesting || phase_ == Phase::Saving))
                return; // stale reply
            for(int i = 0; i < 4; ++i)
                if(r.token[i] != token_[i])
                    return; // stale or foreign reply
            const bool saving = phase_ == Phase::Saving;
            deadline_.reset();
            resend_at_.reset();
            switch(r.result) {
                case ShareResult::Accepted:
                    phase_ = Phase::Restarting;
                    share_after_restart_ = true;
                    deadline_ = now_ + kRestartTimeoutMs;
                    intent_until_ = now_ + kRestartTimeoutMs; // the continuation expires with the restart window
                    message_ = Name() + " stops audio and restarts into SD management; Windows will install/open the drive …";
                    Log(Name() + " accepted SD management; restarting");
                    CloseLink();
                    break;
                case ShareResult::Saving:
                    // The firmware writes the loop now; ask again until the card is current.
                    if(!saving) {
                        save_started_ = now_;
                        Log(Name() + ": saving the loop before sharing");
                    }
                    phase_ = Phase::Saving;
                    resend_at_ = now_ + kSaveRetryMs;
                    deadline_ = save_started_ + kSaveTimeoutMs;
                    message_ = profile_->texts.saving;
                    break;
                case ShareResult::ConfirmRequired:
                    phase_ = Phase::Confirm;
                    message_ = profile_->texts.confirm;
                    break;
                default: {
                    phase_ = Phase::Performance;
                    const std::string name = ShareResultName(r.result, r.result_code);
                    if(r.result == ShareResult::Busy)
                        error_ = profile_->texts.busy;
                    else if(r.result == ShareResult::NoCard)
                        error_ = "No microSD card in the slot.";
                    else
                        error_ = "Request refused (" + name + ").";
                    Log(Name() + " refused SD management: " + name);
                    break;
                }
            }
            Changed();
        } else if(r.kind == Reply::Error) {
            char text[64];
            std::snprintf(text, sizeof(text), "Firmware rejected command 0x%02X: ", unsigned(r.error_command));
            error_ = text + ErrorCodeName(r.error_code);
            if(phase_ == Phase::Requesting || phase_ == Phase::Saving) {
                phase_ = Phase::Performance;
                deadline_.reset();
                resend_at_.reset();
            }
            Changed();
        }
    }

    void Timeout() {
        const Phase phase = phase_;
        deadline_.reset();
        switch(phase) {
            case Phase::Requesting:
                phase_ = Phase::Performance;
                error_ = "No answer to the share request; nothing was changed.";
                break;
            case Phase::Saving:
                phase_ = Phase::Performance;
                resend_at_.reset();
                error_ = "The loop was not saved within " + std::to_string(kSaveTimeoutMs / 1000)
                       + " s; nothing was shared. Check the card and try again.";
                break;
            case Phase::Restarting:
                phase_ = Phase::NotFound;
                share_after_restart_ = false;
                error_ = Name() + " did not reappear in SD management within " + std::to_string(kRestartTimeoutMs / 1000)
                       + " s. Check the USB cable; power-cycling always starts the performance mode.";
                break;
            case Phase::Returning:
                phase_ = Phase::NotFound;
                error_ = Name() + " did not reappear on USB-MIDI within " + std::to_string(kRestartTimeoutMs / 1000) + " s.";
                break;
            default: return;
        }
        Log(Name() + ": " + error_);
        Changed();
    }

    void Lost(const std::string& reason) {
        CloseLink();
        if(phase_ == Phase::Performance || phase_ == Phase::Confirm || phase_ == Phase::Requesting || phase_ == Phase::Saving) {
            phase_ = Phase::NotFound;
            status_.reset();
            deadline_.reset();
            resend_at_.reset();
            message_ = reason;
            Log(Name() + ": " + reason);
        }
        Changed();
    }

    void CloseLink() {
        if(link_) {
            link_->Close();
            link_.reset();
        }
        probing_ = false;
    }

    ddp::DdpSession& session_;
    MidiPorts* midi_;
    std::function<void(uint8_t*)> tokens_;
    const Profile* profile_ = &kNebulae; // the instrument found last
    Phase phase_ = Phase::NotFound;
    std::optional<PerfStatus> status_;
    std::string port_, message_, error_;
    std::unique_ptr<MidiLink> link_;
    bool probing_ = false, confirm_ = false, share_after_restart_ = false, perform_after_return_ = false, started_ = false;
    bool last_quarantined_ = false;
    int interruption_episode_ = 0, return_generation_ = 0, return_failures_ = 0, return_completions_ = 0;
    uint32_t now_ = 0, next_scan_ = 0, last_reply_ = 0, last_status_sent_ = 0, save_started_ = 0, intent_until_ = 0;
    std::optional<uint32_t> deadline_, resend_at_;
    uint8_t token_[4]{};
    bool scan_requested_ = false;
    std::map<std::string, uint32_t> failed_ports_;
};
}
