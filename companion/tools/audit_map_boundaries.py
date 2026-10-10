"""Map-only source/diff guardrails. Does not claim native runtime correctness."""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--baseline', default='e349d619fd7ae69c7d1fb4e8cf195ead8536f594')
args = parser.parse_args()
root = args.source_root.resolve()
patterns = {
    'frame/context': r'\b(?:CreateContext|DestroyContext|NewFrame|Render|ImGui_Impl\w+)\s*\(',
    'graphics/input owner': r'\b(?:Present1?|ResizeBuffers|ResizeTarget|WndProc|SetWindowLong\w*|'
                            r'ClipCursor|SetCursorPos|ShowCursor|GetAsyncKeyState|GetKeyState|'
                            r'CreateDevice|D3D11CreateDevice|GetSwapChain)\s*\(',
    'hooks/detached worker': r'\b(?:LL_\w*HOOK|MH_CreateHook|DetourAttach)\b|\.detach\s*\(',
    'unbounded actor materialization': r'\b(?:getRuntimeActorList|fetchEntities\d*)\s*\(',
}
files = sorted((root / 'companion/src/map').glob('*'))
errors = []
for path in files:
    if path.suffix not in {'.h', '.cpp'}:
        continue
    source = path.read_text(encoding='utf-8')
    source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', lambda m: '\n' * m.group().count('\n'), source)
    for label, pattern in patterns.items():
        for match in re.finditer(pattern, source):
            line = source[:match.start()].count('\n') + 1
            errors.append(f'{path.relative_to(root)}:{line}: {label}: {match.group()}')
command = ['git', '-C', str(root), 'diff', '--name-only', args.baseline]
changed = subprocess.check_output(command, encoding='utf-8').splitlines()
allowed = ('companion/src/map/', 'companion/tests/map/', 'companion/docs/MAP_',
           'companion/tools/verify_map_native.py', 'companion/tools/audit_map_boundaries.py')
for path in changed:
    if not path.startswith(allowed):
        errors.append(f'out-of-scope change: {path}')
subprocess.run(['git', '-C', str(root), 'diff', '--check', args.baseline], check=True)
if errors:
    raise SystemExit('\n'.join(errors))
print(f'PASS {len(files)} Map production files; {len(patterns)} forbidden-call groups: 0 matches')
print(f'PASS {len(changed)} changed paths within Map source/tests/docs/tools; diff --check')
print('PASS no Schematic/LHolo/shared-host path changed; runtime/lifecycle acceptance remains NOT_RUN')
