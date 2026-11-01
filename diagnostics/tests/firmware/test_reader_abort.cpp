// Compile the actual production abort against observable CMSIS/RCC registers.
#include "reader_abort.h"
#include "reader_dma.h"
#include <cstdio>
unsigned checks=0, failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
void ResetFixture(SD_HandleTypeDef& sd) {
    controller={0xffffffff,1,1,0xffffffff}; irq_enabled=true; pending=true;
    reset_held=false; operations.clear(); active_handle=&sd;
}
class Backend final : public diag::ReaderDmaBackend {
 public:
    SD_HandleTypeDef sd{SDMMC1,HAL_SD_STATE_READY,0,0,0};
    uint32_t tick=0; bool stall=true;
    bool Available() override { return true; }
    uint32_t Now() override { return tick++; }
    bool StartRead(uint8_t*,uint32_t) override { return sd.State==HAL_SD_STATE_READY; }
    bool StartWrite(uint8_t*,uint32_t) override {
        if(sd.State!=HAL_SD_STATE_READY) return false;
        sd.State=HAL_SD_STATE_BUSY; return true;
    }
    bool Busy() override {
        if(!stall && sd.State==HAL_SD_STATE_BUSY) sd.State=HAL_SD_STATE_READY;
        return sd.State==HAL_SD_STATE_BUSY;
    }
    uint32_t Error() override { return sd.ErrorCode; }
    bool CardReady() override { return sd.State==HAL_SD_STATE_READY; }
    void Clean(uint8_t*) override {}
    void Invalidate(uint8_t*) override {}
    void Abort() override { diag::AbortReaderSd(sd); }
};
int main() {
    for(auto state:{HAL_SD_STATE_RESET,HAL_SD_STATE_READY,HAL_SD_STATE_BUSY}) {
        for(bool race:{false,true}) {
            SD_HandleTypeDef sd{SDMMC1,state,0xffffffff,1,0x20};
            ResetFixture(sd); complete_on_disable=race;
            diag::AbortReaderSd(sd);
            CHECK(operations=="IBRBrBP"); // same finite sequence for BUSY/READY/late IRQ
            CHECK(!irq_enabled && !pending && !reset_held);
            CHECK(controller.MASK==0 && controller.IDMACTRL==0 && controller.DCTRL==0 && controller.STA==0);
            CHECK(sd.State==HAL_SD_STATE_RESET && sd.Context==SD_CONTEXT_NONE && sd.Lock==HAL_UNLOCKED);
            CHECK(sd.ErrorCode==0x20); // no fictitious HAL-abort error clearing
            diag::AbortReaderSd(sd); CHECK(sd.State==HAL_SD_STATE_RESET);
        }
    }
    complete_on_disable=false;
    SD_HandleTypeDef uninitialized; operations.clear();
    diag::AbortReaderSd(uninitialized); CHECK(operations.empty());
    // A timed-out write uses this same reset adapter; no further start until init.
    Backend backend; ResetFixture(backend.sd);
    alignas(32) uint8_t buffer[512]{}; uint8_t source[512]{};
    diag::ReaderDma io(backend,buffer);
    CHECK(!io.Write(7,source));
    CHECK(io.Status().fault==diag::ReaderIoFault::DmaTimeout);
    CHECK(backend.sd.State==HAL_SD_STATE_RESET && controller.IDMACTRL==0);
    CHECK(!backend.StartWrite(buffer,7));
    // Explicit full-init boundary, as used by BSP_SD_Init on SHARE.
    backend.sd.State=HAL_SD_STATE_READY; backend.stall=false;
    CHECK(io.Write(7,source));
    std::printf("reader abort: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
