#!/usr/bin/env python3
"""Extract the checked-in Otto choreography into a nonblocking plan builder.

No hardware calls, source downloads, USB, or generated arbitrary servo commands.
The generated include is committed and checked with --check in the host gate.
"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'main/boards/gosha-v1'
NAMES = ('Home Jump Walk Turn Bend ShakeLeg Sit UpDown Swing Moonwalker '
         'WhirlwindLeg HandsUp HandsDown HandWave Windmill Takeoff Fitness '
         'Greeting Shy RadioCalisthenics MagicCircle Showcase').split()

def generate():
    source = (BOARD / 'otto_movements.cc').read_text()
    output = ['// Generated from otto_movements.cc by import_legacy_motion_routines.py.',
              '// Original amplitudes, periods, phases and postures; no RTOS/hardware calls.',
              '// Only the right arm is available. See LEGACY_MOVEMENTS_RESTORE_RU.md.\n']
    for name in NAMES:
        start = source.index('void Otto::' + name + '(')
        brace = source.index('{', start)
        depth, end = 1, brace + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        body = source[start:end].replace('Otto::', 'LegacyMotionPlan::')
        body = re.sub(r'//[^\n]*', '', body)
        body = re.sub(r'if \(!has_hands_\)\s*\{\s*return;\s*\}', '', body)
        body = body.replace('has_hands_', 'right_arm_available_')
        body = re.sub(r'servo_\[([^]]+)\]\.GetPosition\(\)', r'Position(\1)', body)
        body = re.sub(r'vTaskDelay\(pdMS_TO_TICKS\((.*?)\)\);', r'Hold(\1);', body)
        # These three routines mistakenly put absolute hand angles in offsets.
        # Keep the right hand at its accepted 135 degree neutral, never 225.
        if name in ('UpDown', 'Swing', 'Moonwalker'):
            body = body.replace('HAND_HOME_POSITION, 180 - HAND_HOME_POSITION',
                                'HAND_HOME_POSITION - 90, 90 - HAND_HOME_POSITION')
        if name == 'Showcase':
            body = body.replace('HandWave(LEFT)', 'HandWave(RIGHT)')
        if name == 'Jump':
            body = body.replace('{', '{\n    (void)steps;', 1)
        body = '\n'.join(line.rstrip() for line in body.splitlines())
        body = re.sub(r'\n\s*\n\s*\n', '\n\n', body)
        output.append(body + '\n')
    return '\n'.join(output)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = BOARD / 'legacy_motion_routines.inc'
    result = generate()
    if args.check:
        assert target.read_text() == result, 'Legacy choreography differs from source extraction'
    else:
        target.write_text(result)
