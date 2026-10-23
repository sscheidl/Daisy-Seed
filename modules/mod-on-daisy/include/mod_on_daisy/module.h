#pragma once
// Everything an instrument needs: one static Module (ARCHITECTURE.md § 6).
//
//   audio IRQ:  hw.UpdateAllControls(); adapter::ReadHardware(mod.engine, hw);
//               mod.engine.Tick(size);  value = mod.engine.Apply(t, base);
//   main loop:  for each MIDI event: if(!mod.controller.HandleMidi(m)) instrument handles it;
//               mod.controller.Service(now_ms, sender);
#include "controller.h"

// Instruments with a full DTCM may place the module elsewhere, e.g.
// -DMOD_ON_DAISY_HOT='__attribute__((section(".sram1_bss")))'.
#ifndef MOD_ON_DAISY_HOT
#define MOD_ON_DAISY_HOT
#endif

namespace mod {
struct Module {
    LiveState live;
    Engine engine;
    Controller controller;

    void Init(const InstrumentInfo& info, float sample_rate, std::size_t block_size, uint8_t device_id = 0,
              Diagnostics* diagnostics = nullptr) {
        engine.Init(live, sample_rate, block_size);
        controller.Init(engine, live, info, sample_rate, block_size, device_id, diagnostics);
    }
};
}
