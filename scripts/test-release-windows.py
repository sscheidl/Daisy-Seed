#!/usr/bin/env python3
"""Run unchanged release guards through Git Bash, preserving multiline arguments.

Windows CreateProcess can select System32's WSL bash before PATH. MSYS argv also
needs a quoted shell/environment hop to preserve a test version containing LF.
Only the test launcher is adapted; production script and fake API are unchanged.
"""
import os
import pathlib
import subprocess
import sys
import unittest

os.chdir(pathlib.Path(__file__).resolve().parent.parent)
bash = pathlib.Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'Git/usr/bin/bash.exe'
assert bash.is_file(), 'Install Git for Windows with Git Bash'
os.environ['PATH'] = str(bash.parent) + os.pathsep + os.environ['PATH']
original_run = subprocess.run


def run(args, *a, **kw):
    if isinstance(args, list) and args[0] == 'bash':
        env = dict(kw.get('env', os.environ))
        env['NEB_TEST_VERSION'] = args[2]
        kw['env'] = env
        args = [str(bash), '-c', 'exec "$BASH" "$1" "$NEB_TEST_VERSION"', 'release-test', args[1]]
    return original_run(args, *a, **kw)


subprocess.run = run
suite = unittest.defaultTestLoader.discover('tests', pattern='test_release_guard.py')
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(not result.wasSuccessful())
