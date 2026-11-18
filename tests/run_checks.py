#!/usr/bin/env python3
"""Product-neutral repository checks; build directories are deliberately ignored."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
subprocess.run(['git', 'diff', '--check'], cwd=root, check=True)
paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
errors = []
for relative in filter(None, paths):
    path = root / relative
    if not path.exists():
        continue
    if '.dsh-remote-edit-' in relative or re.search(r'(^|/)build[^/]*/', relative):
        errors.append(f'tracked generated file: {relative}')
    if relative.startswith('portable_cfg_store/'):
        text = path.read_text(encoding='utf-8')
        if re.search(r'RS300|RS500|/home/|172\.16\.|password\s*=\s*[^\s]', text, re.I):
            errors.append(f'private/product-specific content: {relative}')
if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
print('repository checks: PASS')
