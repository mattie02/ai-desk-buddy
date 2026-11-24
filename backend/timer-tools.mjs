import { randomUUID } from 'node:crypto';

function spokenDuration(totalSeconds) {
  if (!Number.isInteger(totalSeconds) || totalSeconds < 0) return undefined;
  const parts = [
    [Math.floor(totalSeconds / 3600), 'hour'],
    [Math.floor(totalSeconds / 60) % 60, 'minute'],
    [totalSeconds % 60, 'second'],
  ].filter(([value]) => value > 0).map(([value, unit]) => `${value} ${unit}${value === 1 ? '' : 's'}`);
  if (!parts.length) return 'no time';
  return parts.length === 1 ? parts[0] : `${parts.slice(0, -1).join(', ')} and ${parts.at(-1)}`;
}

export const timerTools = [
  {
    type: 'function', name: 'start_focus_timer',
    description: 'Start the ESP32 focus timer for an explicitly requested duration. Never replace a running timer. Convert the requested duration to whole seconds; ask if missing or ambiguous.',
    parameters: { type: 'object', properties: { seconds: { type: 'integer', minimum: 1, maximum: 86400 } }, required: ['seconds'], additionalProperties: false },
  },
  {
    type: 'function', name: 'get_focus_timer',
    description: 'Read current focus timer state from the ESP32. Always use this for time-left questions. Speak remaining_spoken, which uses hours, minutes and seconds with zero units omitted.',
    parameters: { type: 'object', properties: {}, required: [], additionalProperties: false },
  },
  {
    type: 'function', name: 'cancel_focus_timer',
    description: 'Cancel the current focus countdown when explicitly requested. Reads device state first and cancels only that timer. Cannot silence an alarm that has already expired.',
    parameters: { type: 'object', properties: {}, required: [], additionalProperties: false },
  },
];

export const planningInstructions = 'Select at most one focus timer tool for the latest user request. Do not execute an action twice or retry an uncertain action. If no tool is needed, or the duration is missing or ambiguous, output only NO_TOOL. This is a silent planning step; do not answer the user or claim success yet.';

// All actions use the same request/receipt path as the terminal client.
export async function executeTimerTool(name, argumentsText, dispatch) {
  const request = async command => {
    const result = await dispatch(command);
    if (!result.ok) return result;
    return { ...result, remaining_spoken: spokenDuration(result.remaining_seconds), duration_spoken: spokenDuration(result.seconds) };
  };
  let args;
  try { args = JSON.parse(argumentsText); } catch { return { ok: false, code: 'invalid_tool_arguments', outcome: 'not_sent' }; }
  if (!args || typeof args !== 'object' || Array.isArray(args) ||
      !timerTools.some(tool => tool.name === name) ||
      Object.keys(args).some(key => name !== 'start_focus_timer' || key !== 'seconds') ||
      (name === 'start_focus_timer' && (!Number.isInteger(args.seconds) || args.seconds < 1 || args.seconds > 86400))) {
    return { ok: false, code: 'invalid_tool_arguments', outcome: 'not_sent' };
  }
  if (name === 'start_focus_timer') return request({ type: 'start_timer', request_id: randomUUID(), seconds: args.seconds });
  const status = await request({ type: 'timer_status', request_id: randomUUID() });
  if (name === 'get_focus_timer' || !status.ok) return status;
  if (!status.running) return { ...status, ok: false, code: 'timer_not_running' };
  return request({ type: 'cancel_timer', request_id: randomUUID(), timer_id: status.timer_id });
}
