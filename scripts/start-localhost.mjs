import { spawn } from 'node:child_process';
import { openSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const isWindows = process.platform === 'win32';

function launch(name, command, args, logPrefix) {
  const stdout = openSync(resolve(root, `${logPrefix}.log`), 'a');
  const stderr = openSync(resolve(root, `${logPrefix}.err`), 'a');
  const child = spawn(command, args, {
    cwd: root,
    detached: true,
    stdio: ['ignore', stdout, stderr],
    windowsHide: true,
  });

  child.unref();
  return { name, pid: child.pid };
}

const backendCommand = isWindows ? resolve(root, 'backend/p2p_server.exe') : resolve(root, 'backend/p2p_server');
const frontendCommand = isWindows ? 'cmd.exe' : 'npm';
const frontendArgs = isWindows
  ? ['/c', 'npm.cmd', 'run', 'dev']
  : ['run', 'dev'];

const processes = [
  launch('backend', backendCommand, ['--http', '8787', '--transfer', '8788'], 'backend-dev'),
  launch('frontend', frontendCommand, frontendArgs, 'frontend-dev'),
];

writeFileSync(resolve(root, '.localhost-pids.json'), `${JSON.stringify(processes, null, 2)}\n`);
for (const processInfo of processes) {
  console.log(`${processInfo.name}: ${processInfo.pid}`);
}
