#pragma once
// SPDX-License-Identifier: MIT
// Test harness around the production Ambient control adapter (no alternative DSP).
#include "../../../effects/reverb/src/rate_control.h"
#include <functional>

class AmbientControlSim {
  public:
    explicit AmbientControlSim(bool matrix = false)
        : control(matrix ? "toy-piano-amb" : "ambient-reverb", matrix ? "0.1.3" : AMBIENT_REVERB_VERSION,
                  matrix, Send) { current_ = this; }
    void SetOut(std::function<void(const uint8_t*, std::size_t)> out) { out_ = out; }
    void Receive(const uint8_t* b, std::size_t n, uint32_t now) { control.device.Handle(b, n, now); }
    void Advance(uint32_t now) {
        control.AudioTick(physical_);
        physical_ = -1;
        control.Service(now, true);
    }
    void TurnKnob(uint8_t, uint16_t, int32_t value) { physical_ = value; }
    ambient::RateControl control;
  private:
    static void Send(const uint8_t* b, std::size_t n) { if(current_->out_) current_->out_(b, n); }
    inline static AmbientControlSim* current_ = nullptr;
    std::function<void(const uint8_t*, std::size_t)> out_;
    int32_t physical_ = -1;
};
