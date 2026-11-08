// Production main.cpp (performance personality) with fake board/USB and the REAL
// read-only SD stack, see production_harness.h. Usage: test_callback <card.img>
#include "production_harness.h"

int main(int argc,char** argv) {
    CHECK(argc==2);
    LoadCard(argv[1]);
    static int16_t file_buffer[2*20000];
    Boot(file_buffer,sizeof file_buffer,8192);

    // Startup: mount, discover banks (loose WAVs, Drones, Neue Bank), load bank 1 slot 1.
    Settle();
    CHECK(perf.mounted && banks.BankCount()==3 && shown_bank.load()==1);
    CHECK(loader.active==1 && bridge.bank==1 && bridge.published.frames==12345 && bridge.published.channels==2);
    CHECK(occupied.load()==0b111);  // "A loose take", "Caf\x82", "Kalimba renamed"
    // Slot 2 is not a RIFF file: slot occupied, load refused with code 2, old sample kept.
    hw.pressed[1]=true;Advance(15);hw.pressed[1]=false;Settle();
    CHECK(loader.error==neb::FileError::Format && loader.active==1);

    // SW17 -> bank view: S3 selects "Neue Bank" without loading anything.
    hw.tog_sw17.state=neb::kSw17BankWhenPressed;Advance(30);CHECK(engine->ui.bank_view);
    hw.pressed[2]=true;Advance(15);hw.pressed[2]=false;Settle();
    CHECK(shown_bank.load()==3 && banks.FilesBank()==3 && occupied.load()==0b111);
    CHECK(loader.active==1 && bridge.bank==1);  // bank choice loads nothing
    CHECK(hw.led[2]==1.f && hw.led[0]==neb::kDimLevel && hw.led[1]==neb::kDimLevel && hw.led[3]==0.f);
    // A missing bank key does nothing.
    hw.pressed[9]=true;Advance(15);hw.pressed[9]=false;Settle();CHECK(shown_bank.load()==3);
    // Slot view: S1 loads "2 Kick.WAV" (mono 48 kHz, 4801 frames) from bank 3.
    hw.tog_sw17.state=!neb::kSw17BankWhenPressed;Advance(30);CHECK(!engine->ui.bank_view);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Settle();
    CHECK(loader.active==1 && bridge.bank==3 && bridge.published.channels==1 && bridge.published.frames==4801);
    CHECK(hw.led[0]==1.f);

    // KEYBOARD: 16 keys, notes and LEDs; no bootloader from a held C5+C6 octave (F7).
    hw.tog_sw18.value=2;Advance(30);CHECK(engine->ui.mode==2);
    for(unsigned i=0;i<16;++i) {
        hw.pressed[i]=true;Advance(15);CHECK(engine->notes.Current()==neb::kNotes[i]);CHECK(key_state.load()==1u<<i);
        hw.pressed[i]=false;Advance(15);CHECK(engine->notes.Current()==-1 && key_state.load()==0);
    }
    CHECK(usb.sent.size()>=32);
    using daisy::System;
    hw.pressed[8]=hw.pressed[15]=true;Advance(6000);
    CHECK(System::bootloader_resets==0);
    hw.pressed[8]=hw.pressed[15]=false;Advance(20);
    // SAMPLE mode: the same keys only select slots.
    hw.tog_sw18.value=0;Advance(30);hw.pressed[8]=hw.pressed[15]=true;Advance(6000);
    CHECK(System::bootloader_resets==0);
    hw.pressed[8]=hw.pressed[15]=false;Settle();
    // NEBULAE mode: S9+S16 alone for 5 s enters the bootloader; with another key it does not.
    hw.tog_sw18.value=1;Advance(30);CHECK(engine->ui.mode==0);usb.sent.clear();
    hw.pressed[8]=hw.pressed[15]=hw.pressed[0]=true;Advance(6000);CHECK(System::bootloader_resets==0);
    hw.pressed[0]=false;Advance(4990);CHECK(System::bootloader_resets==0);
    Advance(20);
    CHECK(System::bootloader_resets==1 && System::bootloader_mode==System::DAISY_INFINITE_TIMEOUT);
    hw.pressed[8]=hw.pressed[15]=false;Advance(15);
    if(engine->recording) {hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);}  // S1 toggled RECORD
    // The bootloader SysEx keeps working in every mode.
    usb.sent.clear();
    const std::vector<uint8_t> ack(std::begin(bootloader::kAck),std::end(bootloader::kAck));
    Send({0xf0,0x7d,0x42,0x4c,0x01,0x01,0xf8,'B','O','O','T',0xf7,0x90,60,100});Main();
    CHECK(System::bootloader_resets==2 && usb.sent.size()>=1 && usb.sent[0]==ack);
    Send({0x80,60,0});

    // MIDI regression (raw parser, running status, real time, SysEx, panic, disconnect).
    Send({0x90,60,100,0xf8,64,100});CHECK(engine->notes.Current()==64);
    Send({0x80,60,0});CHECK(engine->notes.Current()==64);Send({0x90,64,0});CHECK(engine->notes.Current()==-1);
    Send({0xb0,20,90,0xf8,21,80});CHECK(engine->ui.params.primary[neb::Start]>.7f);
    Send({0x90,60,100});Send({0xb0,123,0});CHECK(engine->notes.Current()==-1);
    Send({0xf0,1,2,3,0xf7,0x90,67,100,0xff});CHECK(engine->notes.Current()==-1);
    Send({0x90,67,100});hUsbDeviceHS.dev_state=0;ServiceUsb();Block();CHECK(engine->notes.Current()==-1);
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;ServiceUsb();Block();
    std::vector<uint8_t> flood;
    for(unsigned i=0;i<180;++i) {flood.push_back(0x90);flood.push_back(60);flood.push_back(100);}
    flood.insert(flood.end(),{0x80,60,0});
    ReceiveUsb(flood.data(),flood.size(),nullptr);
    for(unsigned i=0;i<10;++i) {ServiceUsb();Block();}
    CHECK(engine->notes.Current()==-1);

    // Control SysEx STATUS: performance, card, bank 3 of 3, playing bank 3 slot 1.
    engine->Act(neb::Action::Clear);Block();  // S1 presses above recorded a Live loop
    usb.sent.clear();
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x01,0xf7});Main();
    auto r=LastReply();
    CHECK(r.size()==26 && r[5]==0x41 && r[6]==kVersion[0] && r[7]==kVersion[1] && r[8]==kVersion[2] && r[9]==0);
    CHECK((r[10]&1) && !(r[10]&2) && r[11]==3 && r[12]==3 && r[13]==3 && r[14]==1);
    // SHARE: with a Live loop it needs confirmation, while recording it is busy.
    engine->live=true;
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(30);  // RECORD on (8192-frame test capacity)
    CHECK(engine->recording);
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x01,1,2,3,4,0xf7});Main();
    r=LastReply();CHECK(r[5]==0x42 && r[6]==neb::sysex::kBusy && !perf.restarting);
    hw.pressed[0]=true;Advance(15);hw.pressed[0]=false;Advance(15);     // RECORD off
    CHECK(!engine->recording && engine->LiveLength()>0);
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x00,1,2,3,4,0xf7});Main();
    r=LastReply();CHECK(r[6]==neb::sysex::kConfirmRequired && r[7]==1 && r[10]==4 && !perf.restarting);
    // Hold a key so the device has a note to release on the host side.
    hw.tog_sw18.value=2;Advance(30);hw.pressed[4]=true;Advance(15);CHECK(sent_notes[neb::kNotes[4]]);
    usb.sent.clear();
    CHECK(System::bootloader_resets==2);
    Send({0xf0,0x7d,0x4e,0x42,0x01,0x02,0x01,9,9,9,9,0xf7});Main();
    // PREPARE_SHARE ran: ACK first, then note-offs + CC123, audio stopped, SD released.
    CHECK(perf.restarting && hw.seed.audio_stopped && releases==1 && !perf.mounted);
    CHECK(usb.sent.size()>=3 && usb.sent[0][5]==0x42 && usb.sent[0][6]==neb::sysex::kAccepted);
    bool note_off=false,all_off=false;
    for(auto& m:usb.sent) {note_off|=m.size()==3 && m[0]==0x80 && m[1]==neb::kNotes[4];all_off|=m.size()==3 && m[0]==0xb0 && m[1]==123;}
    CHECK(note_off && all_off);
    CHECK(System::bootloader_resets==3 && System::bootloader_mode==System::DAISY_SKIP_TIMEOUT);
    neb::BootIntent next=neb::TakeBootIntent(intent_words);
    CHECK(next.personality==neb::Personality::SdManagement && next.bank==3 && next.slot==1);
    for(unsigned i=0;i<3;++i) Block();
    for(float x:output_l) CHECK(x==0.f);  // faded to silence before the stop
    CHECK(neb::TakeBootIntent(intent_words).personality==neb::Personality::Performance);  // one-shot

    // Dispatch in main(): a valid SD-management intent enters management, anything else
    // starts the performance personality.
    neb::StoreBootIntent(intent_words,{neb::Personality::SdManagement,2,3});
    bool entered=false;
    try {firmware_main();} catch(const std::runtime_error&) {entered=true;}
    CHECK(entered && intent_words[0]==0);

    // Card removal clears the tables; the next insertion rescans.
    perf.restarting=false;fade_out.store(false);faded.store(false);master_gain=1;
    hw.sd_detect.state=false;Advance(20);
    CHECK(!perf.mounted && banks.BankCount()==0 && occupied.load()==0);
    hw.sd_detect.state=true;Advance(20);Settle();
    CHECK(perf.mounted && banks.BankCount()==3);
    CHECK(aborts==0);
    std::printf("PASS production callback/main: %u checks; real FatFs R0.16 + ro_sd banks/slots/SW17/LED dimming, F7 gesture only in NEBULAE, MIDI, control SysEx, PREPARE_SHARE, intent dispatch, card swap\n",checks);
}
