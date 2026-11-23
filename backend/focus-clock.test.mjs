import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
test('firmware timer deadline, duplicate commands, busy rejection, expiry and clock boundaries', () => {
  const directory = mkdtempSync(join(tmpdir(), 'buddy-clock-'));
  try {
    const binary = join(directory, 'clock-test');
    const compile = spawnSync('c++', ['-std=c++17', '-Wall', '-Wextra', '-I',
      fileURLToPath(new URL('../include', import.meta.url)),
      fileURLToPath(new URL('../test/focus_clock_test.cpp', import.meta.url)), '-o', binary], { encoding: 'utf8' });
    assert.equal(compile.status, 0, compile.stderr);
    const run = spawnSync(binary, [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
