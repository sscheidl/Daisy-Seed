// The additive diag::App hooks used by the program manager: HELLO identity and the
// CommandHook (consume/decline, session flag, request merging). Default behaviour of
// the diagnostics firmware must be unchanged without them.
#include "check.h"
#include "fakes/fake_diag.h"

using namespace fakes;

namespace {

class RecordingHook final : public diag::CommandHook {
  public:
    std::vector<std::string> types;
    std::vector<bool> sessions;
    std::string consume_type;      // this type is consumed with reply "HOOKED"
    uint32_t add_requests = 0;
    bool Handle(const char* body, const char* type, bool session_active, uint32_t, ddp::Writer& reply,
                uint32_t& requests) override {
        (void)body;
        types.push_back(type);
        sessions.push_back(session_active);
        if(consume_type == type) {
            reply.Str("HOOKED c=").Str(type);
            requests |= add_requests;
            return true;
        }
        return false;
    }
};

struct Rig {
    FakeBoard board;
    FakeUsb usb;
    uint32_t now = 1000;
    diag::Identity identity;
    diag::App app;
    Rig(const diag::Identity* id = nullptr) : app(board, usb, "v6_1", 1000, id) {}
    void Host(const std::string& body) {
        const std::string f = Frame(body);
        app.OnUsbReceive(reinterpret_cast<const uint8_t*>(f.data()), static_cast<uint32_t>(f.size()));
    }
    void Run(uint32_t ms) {
        for(uint32_t i = 0; i < ms; ++i) {
            ++now;
            ++board.ticks;
            app.Step(now);
        }
    }
};

} // namespace

int main() {
    {   // Default identity is the diagnostics firmware, unchanged.
        Rig rig;
        rig.Host("HELLO proto=1");
        rig.Run(2);
        const auto bodies = rig.usb.TakeBodies();
        const std::string hello = First(bodies, "HELLO");
        CHECK_EQ(Field(hello, "app"), "DaisyDiagnostic");
        CHECK_EQ(Field(hello, "fw"), std::string(diag::kFirmwareVersion));
        CHECK_EQ(Field(hello, "boot"), "BOOT_SRAM");
        CHECK(Field(hello, "caps").find(",audio") != std::string::npos);
        CHECK(Field(hello, "caps").find(",pm") == std::string::npos);
    }
    {   // Custom identity.
        diag::Identity id;
        id.app = "DaisyProgramManager";
        id.fw = "0.1.0";
        id.boot = "BOOT_QSPI";
        id.extra_caps = ",pm,pmsd";
        Rig rig(&id);
        rig.Host("HELLO proto=1");
        rig.Run(2);
        const std::string hello = First(rig.usb.TakeBodies(), "HELLO");
        CHECK_EQ(Field(hello, "app"), "DaisyProgramManager");
        CHECK_EQ(Field(hello, "fw"), "0.1.0");
        CHECK_EQ(Field(hello, "boot"), "BOOT_QSPI");
        CHECK_EQ(Field(hello, "bl"), "v6_1");
        const std::string caps = Field(hello, "caps");
        CHECK(caps.compare(0, 4, "btn,") == 0);
        CHECK(caps.size() > 8 && caps.compare(caps.size() - 8, 8, ",pm,pmsd") == 0);
    }
    {   // Hook sees every frame with the right type and session flag; declining keeps
        // the normal handling; consuming replaces it and merges requests.
        Rig rig;
        RecordingHook hook;
        rig.app.SetHook(&hook);
        rig.Host("PING n=1");
        rig.Run(1);
        CHECK_EQ(hook.types.size(), 1u);
        CHECK_EQ(hook.types[0], "PING");
        CHECK(!hook.sessions[0]);
        CHECK(Contains(rig.usb.TakeBodies(), "PONG n=1"));
        rig.Host("HELLO proto=1");
        rig.Run(1);
        CHECK(!hook.sessions[1]);  // session state before HELLO
        rig.Host("PING n=2");
        rig.Run(1);
        CHECK(hook.sessions[2]);
        rig.usb.TakeBodies();
        hook.consume_type = "LED";
        rig.Host("LED m=ON");
        rig.Run(1);
        auto bodies = rig.usb.TakeBodies();
        CHECK(Contains(bodies, "HOOKED c=LED"));
        CHECK(!Contains(bodies, "ACK c=LED"));
        CHECK(rig.app.settings().led_mode == diag::LedMode::Follow);  // not applied
        // A consumed frame still counts as host activity? No: only the Session tracks
        // liveness, so a hook-only stream would time out. PING keeps it alive.
        hook.consume_type = "PMX";
        hook.add_requests = diag::kRequestState;
        rig.Host("PMX a=1");
        rig.Run(1);
        bodies = rig.usb.TakeBodies();
        CHECK(Contains(bodies, "HOOKED c=PMX"));
        CHECK(Contains(bodies, "ST "));  // requested state frame
        CHECK(!Contains(bodies, "ERR c=PMX"));
        hook.consume_type = "";
        rig.Host("PMX a=1");
        rig.Run(1);
        CHECK(Contains(rig.usb.TakeBodies(), "ERR c=PMX e=UNKNOWN"));
        rig.app.SetHook(nullptr);
        rig.Host("LED m=ON");
        rig.Run(1);
        CHECK(Contains(rig.usb.TakeBodies(), "ACK c=LED"));
        CHECK_EQ(hook.types.size(), 6u);
    }
    return check::Finish("test_diag_hooks");
}
