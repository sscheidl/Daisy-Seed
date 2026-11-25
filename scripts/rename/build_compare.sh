#!/usr/bin/env bash
set -euo pipefail

# Prefer python on Windows, where python3 may refer to the Store launcher.
python=python
command -v "$python" >/dev/null 2>&1 || python=python3
exec "$python" "$(dirname -- "${BASH_SOURCE[0]}")/build_compare.py" "$@"
