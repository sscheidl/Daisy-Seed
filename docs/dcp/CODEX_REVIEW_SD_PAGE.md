# Independent review of the Daisy Control Panel SD card page

Reviewed on 2026-11-08. PR #106 at `4b65faf`. Looper 0.6.1 checked against
`origin/main` at `6e390c7`, which contains `b7517b5`. **HOLD** for card-safety
and patch-preservation findings below. Hardware and the real Win32 COM path are
**NOT MEASURED**.

## Priority for one-kit operation

For the expected one-kit setup, fix the D5 backup gate in finding 5 first.
SHARE can reach a MOD kit while its matrix is still being read. The late
actions in findings 1 and 2 also reproduced with one simulated kit. A card
inserted after the restart window was shared automatically. After the RETURN
command timed out and reader ownership became unknown, restoring the same CDC
connection triggered PERFORM and a restart. The latter case started with an
explicit Return on that kit, so its single-kit impact is a delayed restart.

Findings 1–4 include risks when a second kit is present or takes the first
kit's place during a pending operation. The wrong-kit restore in finding 4 has
lower practical priority in the expected setup. The P1 labels record the
potential consequence when the conditions occur. Finding 5 alone still
warrants the HOLD for patch preservation.

## P1

1. **An accepted SHARE can expose a different kit's card.** In
   [SdShare.h:269](../../editors/mod-on-daisy/src/core/SdShare.h#L269), T20 takes the first
   recognized CDC profile and clears the restart deadline. T24 at
   [line 288](../../editors/mod-on-daisy/src/core/SdShare.h#L288) consumes a bare
   `share_after_restart_` flag against whichever CDC session is now LOCAL with a card.
   Reproduced with two simulators of the same profile. A accepts SHARE and never
   reappears. B comes up in management and receives `READER a=SHARE`. Also reproduced
   with one kit that enters management without a card. Inserting a card after 21 s
   still shares it automatically. The flag can survive a CDC loss, since there is no
   longer a deadline and T31 leaves it set. This breaks the scope of the explicit
   action in T24 and §10. The Python reference carries the same bare flag.
   **Fix.** Carry the initiating hardware identity and deadline through T24. Verify the
   CDC endpoint belongs to the initiating kit, expire the intent and discard it on a
   lost management session or a conflicting user selection. If the MIDI-to-CDC
   identity cannot be established, show the discovered reader state and require an
   explicit Share on that CDC connection.

2. **A failed or disconnected RETURN can leave a PERFORM armed for a later session.**
   [SdShare.h:191](../../editors/mod-on-daisy/src/core/SdShare.h#L191) sets
   `perform_after_return_` on enqueue. [DdpSession.h:270](../../editors/mod-on-daisy/src/core/DdpSession.h#L270)
   clears the reader command on drop, and the timeout and ERR paths also clear it.
   [SdShare.h:294](../../editors/mod-on-daisy/src/core/SdShare.h#L294) interprets any later
   LOCAL with no pending command as authorization to restart. Reproduced by dropping
   A's CDC connection before consuming its RETURN reply and replacing it with a LOCAL
   B. B received PERFORM and restarted. A later acknowledgement or ordinary STATUS can
   also satisfy the condition after a failed RETURN on the same session. T27–T31 and
   §10 require the original return to complete, and acknowledgement has its own
   RETURN-only path. The Python reference shares this gap. The initial UI message also
   claims the card was returned as soon as the command was queued.
   **Fix.** Advance only on the matching successful `a=RETURN state=LOCAL busy=0`
   reply from the same CDC connection generation. Invalidate the continuation on
   reader error, timeout, loss or cancellation. Show a pending message until firmware
   confirms the return.

3. **The acknowledgement dialog can release the wrong interruption.**
   [EditorComponent.cpp:718](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L718)
   opens a nonblocking confirmation that captures only the editor. When Yes arrives,
   the callback checks the current workflow state. A CDC reconnect or a different
   interrupted kit can therefore receive `READER a=RETURN` under a dialog opened for
   the previous interruption. This is a code-path finding. The existing self-test
   answers synchronously and cannot exercise the gap. T30 and §10 require explicit
   acknowledgement of the quarantined access being released.
   **Fix.** Bind the dialog to a CDC generation and a reader interruption episode. On
   Yes, validate both before sending. Invalidate the dialog on loss or ownership
   change and guard the callback with a component lifetime check.

4. **D5 can overwrite the patch on another kit running the same firmware.**
   [EditorComponent.cpp:562](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L562)
   falls back to every Daisy port once the old label is absent, and
   [line 603](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L603) marks an
   automatic open as the returning kit. At
   [line 698](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L698), the only device
   match is the product key. Two kits running Nebulae MOD both report
   [`nebulae`](../../instruments/granular/src/modulation.h#L38), and two Looper MOD kits
   both report [`looper`](../../instruments/looper/src/modulation.h#L90). A silent A and
   a responding B with the same key can therefore lead to an upload over B's live
   matrix, including on the R1 fallback. The code also allows two identically named
   ports to resolve to the last matching entry. This is a code-path finding against
   D5 and §§7–8.
   **Fix.** Pair input/output and track the actual USB device across personalities.
   Require both verified hardware identity and the expected workflow transition
   before applying the snapshot. If identity is ambiguous, retain the autosave for
   manual loading. Add a test with two automatic candidates using the same product
   key and distinct patches.

5. **SHARE can reset a MOD kit before its volatile matrix has been saved.**
   Real MIDI connection starts SQ detection and sets `md_pending_` at
   [EditorComponent.cpp:347](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L347).
   NB/LP can already answer STATUS and enable Share while SQ or the subsequent Mod on
   Daisy read is in progress. At
   [line 649](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L649), an unconnected
   matrix is logged as absent and the action goes ahead. No snapshot exists to recover
   the kit's patch after restart. The confirm path can also reuse an earlier snapshot
   if the user edits the matrix while the confirmation is open. This is a code-path
   finding against D5. The simulator cycles settle the MOD connection before clicking.
   **Fix.** Gate performance SHARE while matrix detection and the initial read are
   pending. Distinguish confirmed absence from an unread matrix and explain the
   consequence before allowing a restart with unknown patch state. Refresh the saved
   snapshot before the confirmation that can restart the kit.

## P2

6. **MIDI enumeration can still discard the final SHARE reply.**
   [EditorComponent.cpp:494](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L494)
   drains JUCE's callback queue before the timer calls `RefreshPorts()`.
   [Line 244](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L244) disconnects if an
   endpoint disappeared. An ACCEPTED delivered by the callback between those steps is
   left in the old port's queue and discarded. `SdLink::Poll` protects frames that have
   already been handed to it. The simulator bypasses this enumeration path. This
   ordering gap can prevent the continuation and D5 restore in T10 and §7 even when
   firmware restarted. **Fix.** Serialize endpoint removal with delivery of received
   frames to the workflow. Preserve the pending transaction until the final frames have
   been drained or the reply window expires, and report an unconfirmed outcome if the
   port disappears without the reply.

7. **A kit that power-cycles out of management can stay undiscoverable on the SD page.**
   [SdShare.h:323](../../editors/mod-on-daisy/src/core/SdShare.h#L323) stops MIDI polling
   throughout Management, including after CDC loss.
   [EditorComponent.cpp:369](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L369)
   calls `ReleaseMidi()` on a manual Connect, which preserves Management. In the
   two-kit harness the phase remained Management after unplug. For a real plain
   instrument that boots back to USB-MIDI, even a manual Connect can fail D4 discovery
   and then release the port after matrix detection fails. T31 correctly preserves
   unknown ownership on loss. The missing part is recovery after a verified new
   performance connection. **Fix.** Permit read-only STATUS probing of the selected
   MIDI port after CDC loss, while keeping the previous reader state unknown. Switch to
   Performance when a valid STATUS identifies the selected kit.

8. **A compatible DDP device from another app can starve management discovery.**
   [DdpSession.h:291](../../editors/mod-on-daisy/src/core/DdpSession.h#L291) keeps any
   protocol-1 HELLO and pings it indefinitely. The workflow then filters only the
   HELLO app name at [SdShare.h:115](../../editors/mod-on-daisy/src/core/SdShare.h#L115).
   If the first candidate is a DaisyDiagnostic CDC device, the real Nebulae or Looper
   candidate behind it is never tried. The work order phase 3 requires identification
   by HELLO app and capability. **Fix.** Pass an app/capability filter into the
   handshake. Close and back off unmatched candidates and continue scanning. Add a
   test with an unrelated, responsive DDP device enumerated first.

9. **The message-thread latency requirement is unfulfilled by construction.**
   [SerialTransport.h:187](../../editors/mod-on-daisy/src/core/SerialTransport.h#L187)
   enumerates and opens serial devices synchronously from `Tick()`. Close calls the
   backend's `Cancel()` on that thread at
   [line 86](../../editors/mod-on-daisy/src/core/SerialTransport.h#L86), outside the two
   timed waits. On MIDI, [EditorComponent.cpp:614](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L614)
   sends synchronously through [MidiPort.h:36](../../editors/mod-on-daisy/src/ui/MidiPort.h#L36).
   Pinned JUCE 9.0.4's WinMM `juce_Midi_windows.cpp:2920` can wait about five seconds
   for a SysEx completion flag. Patch file creation and writes also run on the
   message thread. §7 documents the serial open limitation, and work order §§4–5
   require bounded message-thread work. The fake timing test starts with a constructed
   backend and uses a fast `Cancel()`. Real driver latency is NOT MEASURED.
   **Fix.** Put enumeration, open/configuration, send and shutdown on the owning I/O
   workers, and put autosave on a file worker before issuing SHARE. Test the message
   thread with deliberately stalled discovery, open, send and cancellation.

10. **Win32 reports a successful open even when COM configuration fails.**
    [SerialPortWin.h:35](../../editors/mod-on-daisy/src/ui/SerialPortWin.h#L35) ignores
    `GetCommState` failure and drops the results of `SetCommState`,
    `SetCommTimeouts` and `PurgeComm`. The code requests DTR through the DCB.
    Firmware output depends on DTR, so a failed setup can masquerade as a silent
    DDP device. Unapplied timeouts also invalidate the worker's assumptions about
    the normal read path. **Fix.** Close and report the exact failing configuration
    call and Win32 error before publishing the link. Add an injected setup-failure
    case. [Microsoft SetCommState documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcommstate)

11. **A contradictory READER message can authorize a clean return.**
    [Ddp.h:150](../../editors/mod-on-daisy/src/core/Ddp.h#L150) returns the first
    occurrence of a key, and [line 189](../../editors/mod-on-daisy/src/core/Ddp.h#L189)
    accepts duplicates. A host probe parsed
    `READER state=EJECTED state=INTERRUPTED sectors=1 busy=0 a=STATUS` as EJECTED.
    The Python parser at
    [protocol.py:179](../../diagnostics/windows/daisy_diag/protocol.py#L179) uses the
    last value. A CRC-valid contradictory message is outside normal firmware output,
    but the C++ interpretation enables Return and bypasses the intended
    acknowledgement guard. **Fix.** Reject duplicate ownership fields as an invalid
    READER update, invalidate the cached reader state and verify the button guards
    with a framed malformed response.

12. **The simulator self-test can open a real CDC port.**
    [Main.cpp:34](../../editors/mod-on-daisy/src/app/Main.cpp#L34) stops the timer and
    advances the editor manually. That still reaches `ddp_session_.Tick()` at
    [EditorComponent.cpp:536](../../editors/mod-on-daisy/src/ui/EditorComponent.cpp#L536).
    The Windows serial provider remains installed until the first SD simulator is
    selected. A connected CDC kit can receive HELLO, PING and STATUS during the
    earlier non-SD tests, and can change the state tested by `SdNoInstrument`. The
    no-autoconnect environment variable protects the plugin's MIDI path.
    **Fix.** Inject an empty serial provider for self-test from construction and let
    it select simulated providers only. Assert that the hardware factory is never
    invoked in the self-test.

## P3

13. **The bounded BYE drain is best-effort even with a responsive backend.**
    [SerialTransport.h:83](../../editors/mod-on-daisy/src/core/SerialTransport.h#L83)
    waits for the send queue to be empty. The worker clears it before calling
    `Write()` at [line 131](../../editors/mod-on-daisy/src/core/SerialTransport.h#L131).
    In addition, an idle Win32 read can wait 20 ms while the drain budget is 15 ms.
    A host probe held the backend in Read until cancellation and observed zero BYE
    writes. The current fake's Write ignores its cancelled flag, masking that failure
    mode. BYE affects session shutdown. Card ownership stays with the firmware.
    **Fix.** Track write completion separately from queue removal and wake the
    worker for shutdown. Make the BYE test control the read/write order and have the
    fake refuse writes after cancellation.

14. **Old failed-port entries can become active again after half a clock cycle.**
    [SdShare.h:391](../../editors/mod-on-daisy/src/core/SdShare.h#L391) and
    [DdpSession.h:228](../../editors/mod-on-daisy/src/core/DdpSession.h#L228) retain
    expired entries. The signed deadline comparison only orders times within half a
    `uint32_t` cycle. A fake-clock probe brought back a failed MIDI port after about
    25 days of absence. The workflow still skipped it. The same comparison is used for
    CDC. **Fix.** Prune expired backoff entries during periodic scans, including when
    their ports are absent. Test an expired entry across more than half a cycle.

15. **Several §11 test mappings overstate their coverage.**
    - §11.3 `test_refusal_and_interruption_messages` checks that a missing card
      disables Share and displays "no card". [SdInterruption](../../editors/mod-on-daisy/src/app/Main.cpp#L608)
      begins with a shareable card. `TestNebulaeRefusals` checks a firmware refusal,
      leaving the GUI assertion uncovered.
    - §11.3 `test_long_texts_stay_inside_the_panel` injects very long text at a
      specified window size. [AllVisibleInside](../../editors/mod-on-daisy/src/ui/SdSharePanel.h#L205)
      only checks rectangles carved from the page bounds. It can pass for a clipped
      label or a button whose rectangle was reduced to zero height. The self-test
      uses ordinary workflow text.
    - §11.4 `test_failed_save_warning_and_confirmation` exercises the Looper
      failed-save warning, confirm label and both decisions in the GUI.
      [SdLooperSaving](../../editors/mod-on-daisy/src/app/Main.cpp#L582) covers a
      successful save. [TestLooperSaveFailedConfirm](../../editors/mod-on-daisy/tests/test_sd_share.cpp#L535)
      covers the core. The Looper GUI's failed-save path and confirm text have no
      corresponding assertion.
    - §11.5 `test_timeout_never_reports_successful_eject` in the Python reference
      establishes SHARED first and then times out STATUS. The C++ test times out
      SHARE from LOCAL. The unknown-state assertion is useful in both cases.
      The Python precondition, request and expected ownership are uncovered as a
      single scenario. The frame test at
      [test_sd_share.cpp:232](../../editors/mod-on-daisy/tests/test_sd_share.cpp#L232)
      checks oversized output rejection. It does not feed raw 400/401-byte inputs
      through the receive parser.
    - §11.6's clock test is a successful cycle near rollover. Its mapping claims
      coverage of every timer, though it does not exercise expired backoff, reader
      timeout or save timeout at rollover. The restore guard covers a restart
      timeout and a manually selected different product. It leaves automatic
      reconnection to the same product on another kit uncovered.
    **Fix.** Add the missing scenarios at the claimed layer and narrow the mapping
    where a test deliberately exercises a different precondition.

## Other checks

- A valid READER report is the source for EJECTED. Return is disabled for
  EJECTED with `busy=1`, while LOCAL intentionally permits direct PERFORM under
  T28. The basic timeout and lost-link paths clear reader state. The continuation
  and malformed-message exceptions are listed above.
- The COM namespace prefix supports COM10 and above. The SetupAPI class and
  instance-ID parsing are consistent with a COM interface under
  `USB\VID_0483&PID_5740&MI_00\…` and a registry `PortName`. Composite
  enumeration on the kit is NOT MEASURED.
  [Microsoft CreateFileA documentation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea)
  [Microsoft COM device interface documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/guid-devinterface-comport)
- After detach the worker retains the backend, its events and buffers, and its
  stack retains each pending OVERLAPPED. If Cancel lands before a pending read or
  write is issued, the worker observes the cancel flag during the sliced wait and
  cancels that individual operation. `GetOverlappedResult(..., TRUE)` protects
  the lifetime until completion and can leave the detached worker blocked if a
  driver fails to complete cancellation. The 15 ms and 30 ms waits only bound the
  application waits inside Close.
  [Microsoft CancelIoEx documentation](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- Looper 0.6.1 at `origin/main`, `instruments/looper/src/main.cpp:506` and
  `:520`, holds edits and re-evaluates before writing the SysEx reply. The first
  tentative ACCEPTED is internal. A final SAVING or ACCEPTED carries the original
  token and fits the C++ state machine. A foreign token in a SHARE reply is ignored.
- The D5 default path resolves under `%LOCALAPPDATA%\Daisy Control Panel\autosave\`.
  A failed disk write currently falls back to the in-memory copy under §8.

## Validation

**MEASURED 2026-11-08, local Windows.** Editor host suite and Python vector check
passed under normal flags and ASan/UBSan. JUCE Release build of the editor and
VST3 succeeded in the existing build tree. A concurrent local CMake addition for a
hardware probe was outside this review. CTest passed 3/3, standalone `--selftest`
exited 0 with 20 PASS lines, and `ModOnDaisyPluginCheck` exited 0 with
`MOD_ON_DAISY_NO_AUTOCONNECT=1`. Separate ASan/UBSan host probes reproduced the
two-kit SHARE and PERFORM continuations, delayed SHARE after insertion, sticky
Management, duplicate reader state, aged backoff and dropped BYE. These probes ran
against the repository headers with simulated devices or a fake serial backend.

**Count: 5 P1, 7 P2, 3 P3.**
