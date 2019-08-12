#!/usr/bin/env python3
"""Run observable language contracts. The compiler is always a subprocess."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('CAIRN_BINARY', ROOT / 'build/cairn')).resolve()

def run_case(path):
    case = json.loads(path.read_text())
    with tempfile.TemporaryDirectory(prefix='cairn-test-') as directory:
        source = Path(directory) / 'case.crn'
        source.write_text(case['source'])
        command = [str(BINARY), *case.get('args', ['--run']), str(source)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=5)
        assert result.returncode == case.get('status', 0), f'exit {result.returncode}; {result.stderr}'
        if 'stdout' in case:
            assert result.stdout == case['stdout'], f'output {result.stdout!r}, expected {case["stdout"]!r}'
        for field in ('stdout', 'stderr'):
            for expected in case.get(field + '_contains', []):
                assert expected in getattr(result, field), f'{expected!r} absent in {getattr(result, field)!r}'
        if case.get('compare_optimized'):
            other = subprocess.run([str(BINARY), '--run', '--no-opt', str(source)], capture_output=True, text=True, timeout=5)
            assert (result.returncode, result.stdout) == (other.returncode, other.stdout), 'optimizer changed observable behavior'

def main():
    paths = [Path(p) for p in sys.argv[1:]] or sorted((ROOT / 'tests/cases').rglob('*.json'))
    failures = []
    for path in paths:
        try:
            run_case(path)
        except (AssertionError, subprocess.TimeoutExpired, OSError, ValueError) as error:
            failures.append((path.relative_to(ROOT), str(error)))
    for path, error in failures:
        print(f'FAIL {path}: {error}', file=sys.stderr)
    print(f'{len(paths)-len(failures)}/{len(paths)} contracts passed')
    return bool(failures)

if __name__ == '__main__':
    raise SystemExit(main())
