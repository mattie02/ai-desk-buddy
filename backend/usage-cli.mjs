import { WebSocket } from 'ws';

if (!process.env.DEVICE_TOKEN) {
  console.error('DEVICE_TOKEN is missing from the local .env file');
  process.exitCode = 1;
} else {
  const socket = new WebSocket(`ws://127.0.0.1:${process.env.PORT || 8765}/control`);
  let received = false;
  const timeout = setTimeout(() => {
    console.error('Usage report timed out; check npm start');
    process.exitCode = 1;
    socket.terminate();
  }, 5000);
  socket.on('open', () => socket.send(JSON.stringify({ type: 'usage_status', token: process.env.DEVICE_TOKEN })));
  socket.on('message', data => {
    received = true;
    const report = JSON.parse(data.toString());
    clearTimeout(timeout);
    if (!report.ok) { console.error(report.code); process.exitCode = 1; }
    else if (process.argv.includes('--json')) console.log(JSON.stringify(report, null, 2));
    else {
      const money = totals => totals.estimate_complete ? `$${totals.estimated_cost_usd.toFixed(6)} estimated` :
        `incomplete ($${totals.known_estimated_cost_usd.toFixed(6)} known estimate)`;
      console.log(`API usage since backend start: ${report.started_at}\nModel: ${report.model}`);
      console.log(`Process: ${report.totals.responses}/${report.totals.requests} response receipts, ${report.totals.input_tokens} input / ${report.totals.output_tokens} output tokens; ${money(report.totals)}`);
      for (const session of report.sessions) {
        console.log(`Session ${session.session}${session.closed ? ' (closed)' : ''}: ${money(session.totals)}`);
        for (const turn of session.recent_turns) console.log(`  Turn ${turn.turn ?? 'unattributed'}: ${turn.responses} responses; ${money(turn)}`);
      }
      console.log('USD estimates from reported usage; not an invoice. Totals reset when the backend restarts.');
    }
    socket.close();
  });
  socket.on('error', () => { clearTimeout(timeout); console.error('Could not contact the local backend'); process.exitCode = 1; });
  socket.on('close', () => { clearTimeout(timeout); if (!received) process.exitCode = 1; });
}
