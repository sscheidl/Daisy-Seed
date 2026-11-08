// DDP extension of the program manager: the PM* commands and the mode gate for the
// diagnostic commands, attached to diag::App as a CommandHook. Wire format in
// docs/PROTOCOL.md. Portable; host-tested together with diag::App.
#pragma once
#include "diag_app.h"
#include "pm_manager.h"

namespace pm {

struct ProtocolConfig {
    const char* app = "DaisyProgramManager";
    const char* fw = "0.0.0";
    const char* profile = "manager";  // "manager" | "probe"
    const char* build = "unknown";    // short git revision of the build
};

class Protocol final : public diag::CommandHook {
  public:
    Protocol(Manager& manager, const ProtocolConfig& config);

    bool Handle(const char* body, const char* type, bool session_active, uint32_t now_ms,
                ddp::Writer& reply, uint32_t& requests) override;

    // Writes a complete PMST body (also used for the status frame after state changes).
    void WriteStatus(ddp::Writer& out) const;

    // Percent-encodes a display name: [A-Za-z0-9._-] verbatim, everything else %XX.
    static void EncodeName(const char* name, ddp::Writer& out);

  private:
    void WriteInfo(ddp::Writer& out) const;
    void WriteEntry(const CatalogEntry& entry, ddp::Writer& out) const;
    void HandleStart(const char* body, uint32_t now_ms, ddp::Writer& reply);
    static bool ParseHashPrefix(const char* text, uint8_t out[32], size_t& len);

    Manager& manager_;
    ProtocolConfig config_;
};

const char* UiErrorName(UiError error);

} // namespace pm
