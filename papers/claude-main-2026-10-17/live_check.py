#!/usr/bin/env python3
"""Explicit live development check: owned OAuth only, at most eight requests."""
import argparse
import json
from pathlib import Path
import signal
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', required=True, type=Path)
parser.add_argument('--artifacts', required=True, type=Path)
parser.add_argument('--model', required=True)
args = parser.parse_args()
binary = args.binary.resolve(strict=True)
status = json.loads(subprocess.check_output([str(binary), 'auth', 'status'], text=True))
accounts = [a for a in status['accounts'] if a['provider'] == 'anthropic' and a['active']]
if len(accounts) != 1 or accounts[0]['kind'] != 'oauth' or accounts[0]['state'] not in ('ready', 'expired'):
    raise SystemExit('Live check requires a selected owned Anthropic OAuth account; no API/CLI fallback.')

root = args.artifacts.resolve()
root.mkdir(parents=True, exist_ok=False)
workflow = root / 'bounded-turn.lua'
workflow.write_text('''for step = 1, 4 do
  local response = blackbird.request({retry_policy={max_attempts=1}})
  local calls = 0
  for _, item in ipairs(response.output) do
    if item.type == 'function_call' then
      assert(item.name == 'exec' or item.name == 'read_file', 'Unexpected live check tool')
      calls = calls + 1
      local result = blackbird.call(item.name, item.arguments)
      blackbird.append({{type='function_call_output', call_id=item.call_id, output=result}})
    elseif item.type == 'message' then
      blackbird.present(item)
    end
  end
  if calls == 0 then return end
end
error('Live check request budget reached; context retained')
''')
session = root / 'session'
marker = root / 'marker.txt'
outcomes = []
for number, word in enumerate(('ONE', 'TWO'), start=1):
    prompt = (
        'This is a bounded native tool-use qualification in an empty scratch directory. '
        + ('Continue our saved conversation. ' if number == 2 else '')
        + f'Use exec exactly once to append the literal line CLAUDE_NATIVE_{word} '
        'to marker.txt in the current working directory. Read marker.txt with read_file. '
        'Do not read other files, use other tools, contact the network, or repeat an effect. '
        f'Then answer exactly CLAUDE_NATIVE_{word}_OK.'
    )
    command = [str(binary), '--plain', '--session', str(session), '--provider', 'anthropic',
               '--model', args.model, '--effort', 'medium', '--workflow', str(workflow),
               '--once', prompt]
    process = subprocess.Popen(command, cwd=root, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=240)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.send_signal(signal.SIGINT)
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
    (root / f'turn-{number}.stdout').write_text(stdout)
    (root / f'turn-{number}.stderr').write_text(stderr)
    expected = ['CLAUDE_NATIVE_ONE'] + (['CLAUDE_NATIVE_TWO'] if number == 2 else [])
    actual = marker.read_text().splitlines() if marker.exists() else []
    passed = (not timed_out and process.returncode == 0 and actual == expected
              and f'CLAUDE_NATIVE_{word}_OK' in stdout)
    outcomes.append(dict(turn=number, exit=process.returncode, timed_out=timed_out,
                         marker_lines=actual, passed=passed))
    (root / 'result.json').write_text(json.dumps(dict(model=args.model, turns=outcomes), indent=2) + '\n')
    if not passed:
        raise SystemExit(f'Live turn {number} did not qualify; preserve {root}; do not replay automatically.')
print(f'Owned Claude live tools and saved-session continuation passed: {root}')
