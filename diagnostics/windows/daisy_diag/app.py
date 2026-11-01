"""Command line entry point."""
from __future__ import annotations

import argparse
import sys


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="DaisyDiagnostic",
        description='Hardware diagnostics for the Daisy Seed3 Desktop Dev Kit')
    parser.add_argument("--sim", action="store_true",
                        help='start in simulation mode (no hardware)')
    parser.add_argument("--sim-nebulae", action="store_true",
                        help='simulate Nebulae on Daisy 0.2 (USB-MIDI performance and SD management)')
    parser.add_argument("--sim-looper", action="store_true",
                        help='simulate the Looper 0.5 (USB-MIDI performance and SD management)')
    parser.add_argument("--port", help='use a fixed COM port, e.g. COM5')
    parser.add_argument("--selftest", type=float, metavar='SECONDS',
                        help='exit after N seconds; exit code 0 if device data was received (for automated startup tests)')
    parser.add_argument("--screenshot", metavar='FILE',
                        help='save a window screenshot before exiting (--selftest)')
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import QApplication

    from .gui import theme
    from .gui.main_window import MainWindow

    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName("DaisyDiagnostic")
    theme.apply(app)
    sim_device = "nebulae" if args.sim_nebulae else "looper" if args.sim_looper else "diagnostics"
    window = MainWindow(simulation=args.sim or args.sim_nebulae or args.sim_looper, port=args.port,
                        sim_device=sim_device)
    window.show()
    result = {"code": 0}
    if args.selftest is not None:
        def finish() -> None:
            ok = window.session is not None and (window.session.board is not None or (
                window.nebulae is not None and window.nebulae.status is not None))
            if args.screenshot:
                window.grab().save(args.screenshot)
            print(f"selftest: {'PASS' if ok else 'FAIL'} state={window.session.state.name}"
                  f" frames={window.session.frames_ok}", flush=True)
            result["code"] = 0 if ok else 1
            window.close()
            app.quit()
        QTimer.singleShot(int(args.selftest * 1000), finish)
    rc = app.exec()
    return result["code"] if args.selftest is not None else rc
