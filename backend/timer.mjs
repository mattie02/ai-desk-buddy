import { randomUUID } from 'node:crypto';
import { WebSocket } from 'ws';
const [action, duration, suppliedId] = process.argv.slice(2);
const match = /^(\d+)(s|m|h)?$/.exec(duration || '');
const seconds = match ? Number(match[1]) * ({ s: 1, m: 60, h: 3600 }[match[2] || 's']) : NaN;
if (!['start', 'status', 'cancel'].includes(action) || (action === 'start' && (!Number.isSafeInteger(seconds) || seconds < 1 || seconds > 86400)) ||
    (action === 'cancel' && !/^[a-zA-Z0-9_-]{1,64}$/.test(duration || ''))) {
  console.error('Usage: npm run timer -- start 25m [request-id] | status | cancel TIMER-ID [request-id]');
  process.exitCode = 1;
} else if (!process.env.DEVICE_TOKEN) {
  console.error('DEVICE_TOKEN is missing from the local .env file');
  process.exitCode = 1;
} else {
  const id = suppliedId || randomUUID();
  const socket = new WebSocket(`ws://127.0.0.1:${process.env.PORT || 8765}/control`);
  let received = false;
  const timeout = setTimeout(() => {
    console.error(JSON.stringify({ ok: false, request_id: id, code: 'control_timeout', outcome: 'unknown' }));
    process.exitCode = 1;
    socket.terminate();
  }, 7000);
  socket.on('open', () => socket.send(JSON.stringify({
    type: action === 'start' ? 'start_timer' : action === 'cancel' ? 'cancel_timer' : 'timer_status', request_id: id,
    token: process.env.DEVICE_TOKEN, ...(action === 'start' ? { seconds } : {}),
    ...(action === 'cancel' ? { timer_id: duration } : {}),
  })));
  socket.on('message', data => {
    received = true;
    const result = JSON.parse(data.toString());
    console.log(JSON.stringify(result));
    if (!result.ok) process.exitCode = 1;
    clearTimeout(timeout);
    socket.close();
  });
  socket.on('error', () => {
    clearTimeout(timeout);
    console.error('Could not contact the local backend; check npm start');
    process.exitCode = 1;
  });
  socket.on('close', () => {
    clearTimeout(timeout);
    if (!received) process.exitCode = 1;
  });
}
