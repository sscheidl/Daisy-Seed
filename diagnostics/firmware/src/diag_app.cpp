#include "diag_app.h"

namespace diag {

void TxQueue::Append(const char* body, size_t len, Stats& stats) {
    char frame[ddp::kMaxFrame];
    const size_t n = ddp::BuildFrame(body, len, frame, sizeof(frame));
    if(n == 0 || fill_ + n > kSize) {
        ++stats.tx_drop;
        return;
    }
    for(size_t i = 0; i < n; ++i)
        buf_[active_][fill_ + i] = static_cast<uint8_t>(frame[i]);
    fill_ += n;
}

void TxQueue::Pump(UsbSink& usb, Stats& stats) {
    if(fill_ == 0)
        return;
    switch(usb.Send(buf_[active_], fill_)) {
        case SendResult::Ok:
            ++stats.tx_ok;
            active_ ^= 1;
            fill_ = 0;
            break;
        case SendResult::Busy: ++stats.tx_busy; break;
        case SendResult::NotReady: Discard(stats); break;  // nobody is listening
    }
}

void TxQueue::Discard(Stats& stats) {
    if(fill_) {
        ++stats.tx_drop;
        fill_ = 0;
    }
}

App::App(Board& board, UsbSink& usb, const char* bootloader_name, uint32_t now_ms)
    : board_(board), usb_(usb), start_ms_(now_ms), last_state_ms_(now_ms),
      last_sys_ms_(now_ms), last_led_ms_(now_ms - kLedMinIntervalMs), last_stat_ms_(now_ms),
      last_audio_ms_(now_ms) {
    session_.Init(bootloader_name);
    last_ticks_ = board_.ControlTicks();
}

void App::Send(const ddp::Writer& body) {
    if(body.Length() && body.Ok())
        tx_.Append(body.CStr(), body.Length(), stats_);
    else if(body.Length())
        ++stats_.tx_drop;
}

void App::SilenceOutputs(uint32_t now) {
    // Called before the main loop blocks (SD test, reader init): CV OUT and the test
    // tone would otherwise hold their last state for the whole blocked time.
    session_.settings.cv_mode = CvOutMode::Off;
    board_.ApplyCvOut(0, 0);
    last_cv_[0] = last_cv_[1] = 0;
    cv_applied_ = true;
    session_.settings.tone.mode = ToneMode::Off;
    board_.ApplyTone(session_.settings.tone, now);
    last_tone_ = session_.settings.tone;
    tone_applied_ = true;
    last_tone_ms_ = now;
}

static bool SameTone(const ToneSettings& a, const ToneSettings& b) {
    return a.mode == b.mode && a.freq_hz == b.freq_hz && a.amplitude == b.amplitude
           && a.channels == b.channels;
}

void App::PumpScope() {
    // Keep at most one snapshot chunk waiting behind the in-flight USB buffer.
    // Filling the queue with bulk data can otherwise crowd out the next ST frame
    // or command reply while the endpoint is briefly busy.
    if(tx_.Pending() != 0)
        return;
    uint16_t frames = 0;
    const int16_t* samples = board_.ScopeData(frames);
    if(samples == nullptr || frames == 0)
        return;  // still capturing
    const uint16_t chunks = ScopeChunks(frames);
    if(scope_next_chunk_ < chunks) {
        char body[ddp::kMaxBody + 1];
        ddp::Writer out(body, sizeof(body));
        WriteScopeChunk(samples, frames, scope_next_chunk_, chunks, out);
        Send(out);
        ++scope_next_chunk_;
    }
    if(scope_next_chunk_ >= chunks)
        scope_active_ = false;
}

void App::Step(uint32_t now) {
    uint32_t requests = kRequestNone;
    if(keepalive_after_block_) {
        // The previous step blocked (SD test, reader init). Keep-alives the host sent
        // meanwhile are still queued in the RX ring; only then does the blocked time
        // not count as host silence. A host that fell silent before the block is not
        // revived artificially.
        if(rx_.Size() > 0)
            session_.KeepAlive(now);
        keepalive_after_block_ = false;
    }

    // Commands from the host.
    uint8_t byte;
    while(rx_.Pop(byte)) {
        if(!parser_.Feed(static_cast<char>(byte)))
            continue;
        char reply_buf[ddp::kMaxBody + 1];
        ddp::Writer reply(reply_buf, sizeof(reply_buf));
        const uint32_t r = session_.Handle(parser_.Body(), now, reply);
        if(r & (kRequestReaderShare | kRequestReaderReturn | kRequestReaderStatus)) {
            // Execute in wire order, not after coalescing command bits. This also
            // prevents a queued SDTEST from running after a SHARE in the same RX batch.
            if(r & kRequestReaderShare)
                SilenceOutputs(now);  // card initialisation blocks the main loop
            board_.ReaderCommand(r, reply);
            keepalive_after_block_ = true;
        }
        if(r & kRequestBootloader) {
            if(board_.SdTestAllowed() && !boot_pending_) {
                reply.Str("ACK c=BOOT");
                boot_pending_ = true;
                boot_since_ = now;
            } else {
                reply.Str("ERR c=BOOT e=BUSY"); // the card is shared with a computer
            }
        }
        if(r & kRequestScope) {
            const Settings& s = session_.settings;
            if(!board_.AudioSupported())
                reply.Str("ERR c=SCOPE e=UNSUPPORTED");
            else if(scope_active_)
                reply.Str("ERR c=SCOPE e=BUSY");
            else if(!board_.StartScope(s.scope_frames, s.scope_trigger))
                reply.Str("ERR c=SCOPE e=BUSY");
            else {
                scope_active_ = true;
                scope_next_chunk_ = 0;
                reply.Str("ACK c=SCOPE n=").U32(s.scope_frames)
                    .Str(" t=").U32(s.scope_trigger ? 1u : 0u)
                    .Str(" k=").U32(ScopeChunks(s.scope_frames));
            }
        }
        if(r & kRequestNewSession) {
            // Output queued for a previous host (e.g. one that closed the port without
            // BYE) is stale; start the new session with a clean buffer.
            tx_.Discard(stats_);
            last_sent_ = Inputs();
        }
        requests |= r;
        Send(reply);
    }
    stats_.rx_ok = parser_.ok;
    stats_.rx_bad = parser_.bad_crc + parser_.bad_format + parser_.overflow + rx_.Overflow();

    const Inputs inputs = board_.ReadInputs();
    // The key gesture of all apps (common/bootloader). Ignored while Windows owns
    // the card; it needs a new 5 s hold after the card is returned.
    if(boot_keys_.Process(inputs.buttons, now) && board_.SdTestAllowed() && !boot_pending_) {
        boot_pending_ = true;
        boot_since_ = now;
    }

    // LEDs: commanded state only, there is no optical feedback.
    const uint16_t leds = ComputeLeds(session_.settings, now, inputs.buttons);
    const uint8_t brightness = LedBrightness(session_.settings, inputs);
    if(requests & kRequestLeds)
        leds_dirty_ = true;
    if((leds_dirty_ || leds != last_leds_ || brightness != last_brightness_)
       && now - last_led_ms_ >= kLedMinIntervalMs) {
        board_.ApplyLeds(leds, brightness);
        last_leds_ = leds;
        last_brightness_ = brightness;
        last_led_ms_ = now;
        leds_dirty_ = false;
    }

    // CV OUT and the test tone: only while a host session is active, so no voltage
    // or signal is left on patched equipment after BYE, a timeout or a closed app.
    const bool host_active = session_.HostActive(now);
    if(!host_active) {
        session_.settings.cv_mode = CvOutMode::Off;
        session_.settings.tone.mode = ToneMode::Off;
    }
    uint16_t cv[kNumCv];
    ComputeCvOut(session_.settings, now, cv);
    if(!cv_applied_ || cv[0] != last_cv_[0] || cv[1] != last_cv_[1]) {
        board_.ApplyCvOut(cv[0], cv[1]);
        last_cv_[0] = cv[0];
        last_cv_[1] = cv[1];
        cv_applied_ = true;
    }
    // The callback keeps the tone only until keep_until; refreshing it from here
    // ties the tone to a running main loop as well as to the host session.
    if(!tone_applied_ || !SameTone(session_.settings.tone, last_tone_)
       || now - last_tone_ms_ >= kToneRefreshMs) {
        board_.ApplyTone(session_.settings.tone, now + kToneKeepAliveMs);
        last_tone_ = session_.settings.tone;
        tone_applied_ = true;
        last_tone_ms_ = now;
    }

    if(now - last_stat_ms_ >= 1000) {
        const uint32_t ticks = board_.ControlTicks();
        stats_.control_rate_hz = (ticks - last_ticks_) * 1000u / (now - last_stat_ms_);
        last_ticks_ = ticks;
        last_stat_ms_ = now;
        stats_.uptime_s = (now - start_ms_) / 1000u;
    }

    if(requests & kRequestSdTest) {
        char body[96];
        ddp::Writer out(body, sizeof(body));
        if(board_.SdTestAllowed()) {
            SilenceOutputs(now);  // the test blocks: no frozen CV level or tone meanwhile
            WriteSd(board_.RunSdTest(inputs.sd_card), out);
        } else {
            out.Str("ERR c=SDTEST e=BUSY");
        }
        Send(out);
        keepalive_after_block_ = true;
    }

    // Replies (including the ACK for BYE) stay queued after the stream stops.
    if(host_active) {
        const uint32_t period = 1000u / session_.settings.rate_hz;
        const bool periodic = now - last_state_ms_ >= period;
        const bool changed = DigitalChanged(inputs, last_sent_)
                             && now - last_state_ms_ >= kMinFrameSpacingMs;
        if(periodic || changed || (requests & kRequestState)) {
            char body[ddp::kMaxBody + 1];
            ddp::Writer out(body, sizeof(body));
            Settings view = session_.settings;
            view.led_brightness = brightness;  // report the effective (PotDim) brightness
            WriteState(inputs, view, last_leds_, seq_++, now, out, last_cv_);
            Send(out);
            last_sent_ = inputs;
            last_state_ms_ = now;
        }
        if(now - last_sys_ms_ >= 1000 || (requests & kRequestState)) {
            char body[160];
            ddp::Writer out(body, sizeof(body));
            WriteSys(stats_, true, session_.settings.rate_hz, out);
            Send(out);
            last_sys_ms_ = now;
#ifdef DIAG_CARD_READER
            char reader_body[192];
            ddp::Writer reader_out(reader_body, sizeof(reader_body));
            board_.ReaderCommand(kRequestReaderStatus, reader_out);
            Send(reader_out);
#endif
        }
        if(board_.AudioSupported()
           && (now - last_audio_ms_ >= kAudioFrameMs || (requests & kRequestState))) {
            char body[128];
            ddp::Writer out(body, sizeof(body));
            WriteAudio(board_.ReadAudioLevels(), session_.settings.tone, out);
            Send(out);
            last_audio_ms_ = now;
        }
        if(scope_active_)
            PumpScope();
    } else if(scope_active_) {
        scope_active_ = false;  // session ended: drop the capture, nobody listens
    }
    tx_.Pump(usb_, stats_);
    if(boot_pending_ && (tx_.Pending() == 0 || now - boot_since_ >= kBootFlushMs)) {
        boot_pending_ = false;
        board_.EnterBootloader();
    }
}

} // namespace diag
