// Diagnostic build (MOD=1 PROFILE=1) on the production harness: cycle splits per block,
// hop detection, the budget guard, the PROFILE SysEx and its reset, the boot benchmark.
// The fake cycle counter advances by a step on every read, so the splits are deterministic
// counts of reads and a large step trips the guard. Usage: test_profile <card.img>
#include "production_harness.h"

namespace {
struct Values {uint32_t v[neb::sysex::kProfileValues];};
Values Decode(const std::vector<uint8_t>& reply) {
    CHECK(reply.size()==7+neb::sysex::kProfilePayload && reply[5]==(neb::sysex::kProfile|neb::sysex::kReplyBit));
    Values out{};
    for(size_t i=0;i<neb::sysex::kProfileValues;++i) {
        uint32_t v=0;
        for(unsigned k=0;k<5;++k) v|=uint32_t(reply[6+5*i+k])<<(7*k);
        out.v[i]=v;
    }
    return out;
}
Values ReadProfile(bool reset=false) {
    if(reset) Send({0xF0,0x7D,0x4E,0x42,0x01,0x03,0x01,0xF7}); else Send({0xF0,0x7D,0x4E,0x42,0x01,0x03,0xF7});
    Main();
    return Decode(LastReply());
}
}

int main(int argc,char** argv) {
    using namespace neb::sysex;
    CHECK(argc==2);
    LoadCard(argv[1]);
    static int16_t file_buffer[2*20000];
    Boot(file_buffer,sizeof file_buffer,8192);
    Settle();
    CHECK(bridge.published.frames==12345);  // bank 1 slot 1 plays: the vocoder has material

    // Splits: every block costs vocoder and grain reads; hop blocks add fill, FFT and
    // phase and report two hop frames (both channels in the same block, hop 512 = 2 blocks).
    fake_dwt.CYCCNT.step=1000;
    ReadProfile(true);Block();  // clear the peaks of the settle phase
    unsigned hop_blocks=0,plain_blocks=0;
    for(unsigned i=0;i<8;++i) {
        Block();
        const neb::profile::Split& s=neb::profile::block;
        CHECK(s.vocoder>0 && s.grains>0 && !s.skipped);
        if(s.hops) {CHECK(s.hops==2 && s.fill>0 && s.fft>0 && s.phase>0);++hop_blocks;}
        else {CHECK(s.fill==0 && s.fft==0 && s.phase==0);++plain_blocks;}
    }
    CHECK(hop_blocks==4 && plain_blocks==4);
    Values p=ReadProfile();
    CHECK(p.v[kProfClock]==480000000u && p.v[kProfDeadline]==2560000u);
    CHECK(p.v[kProfBlocks]>=9 && p.v[kProfHopBlocks]>=4 && p.v[kProfSkipped]==0);
    CHECK(p.v[kProfTotalPeak]>=p.v[kProfVocoderPeak]+p.v[kProfGrainsPeak]);
    CHECK(p.v[kProfOtherPeak]<=p.v[kProfTotalPeak]);
    CHECK(p.v[kProfHopVocoderMin]>0 && p.v[kProfHopVocoderMin]<=p.v[kProfVocoderPeak]);
    CHECK(p.v[kProfFftPeak]>0 && p.v[kProfFillPeak]>0 && p.v[kProfPhasePeak]>0);
    CHECK(p.v[kProfOverruns]==0 && p.v[kProfLastTotal]>0);
    CHECK(p.v[kProfEnginePlacement]==uint32_t(reinterpret_cast<uintptr_t>(engine)>>20));
    // STATUS is unchanged by the diagnostic build.
    Send({0xF0,0x7D,0x4E,0x42,0x01,0x01,0xF7});Main();
    CHECK(LastReply().size()==6+kStatusPayload+1);

    // Budget guard: a hop block beyond 80 % of the deadline leaves the vocoder out of the
    // next eight blocks (they still render grains), then the hops continue.
    ReadProfile(true);Block();        // Send() runs a block itself: arm the counter afterwards
    fake_dwt.CYCCNT.step=200000;  // ~26 reads in a hop block = 5.2 M cycles; plain ~1.2 M
    unsigned tripped=0;
    for(unsigned i=0;i<4 && !tripped;++i) {Block();if(engine->ProfileSkipped()) tripped=i+1;}
    CHECK(tripped && engine->ProfileSkipped()==8);
    fake_dwt.CYCCNT.step=1000;  // the skipped blocks themselves stay cheap
    for(unsigned i=0;i<8;++i) {
        Block();
        CHECK(neb::profile::block.skipped && neb::profile::block.hops==0 && neb::profile::block.grains>0);
        for(unsigned s=0;s<neb::kBlock;++s) CHECK(std::isfinite(output_l[s]));
    }
    CHECK(engine->ProfileSkipped()==0);
    unsigned resumed=0;
    for(unsigned i=0;i<2;++i) {Block();CHECK(!neb::profile::block.skipped);resumed+=neb::profile::block.hops;}
    CHECK(resumed==2);
    p=ReadProfile();
    CHECK(p.v[kProfSkipped]==8 && p.v[kProfOverruns]>=1 && p.v[kProfTotalPeak]>2048000u);

    // Reset clears the peaks after the reply that asked for it.
    p=ReadProfile(true);
    CHECK(p.v[kProfSkipped]==8);
    Block();
    p=ReadProfile();
    CHECK(p.v[kProfSkipped]==0 && p.v[kProfBlocks]>=1 && p.v[kProfBlocks]<=3);
    // Length and payload errors.
    Send({0xF0,0x7D,0x4E,0x42,0x01,0x03,0x02,0xF7});Main();
    CHECK(LastReply()[5]==(kError|kReplyBit) && LastReply()[7]==kErrLength);

    // Boot benchmark on the fake counter: both placements report hop and plain blocks.
    fake_dwt.CYCCNT.step=1;
    prof::RunBench(live_banks[0]);
    for(uint32_t v:prof::bench) CHECK(v>0 && v!=0xffffffffu);
    CHECK(prof::bench[0]>prof::bench[2] && prof::bench[1]>prof::bench[3]);  // hop costs more reads
    std::printf("PASS profile build: %u checks\n",checks);
}
