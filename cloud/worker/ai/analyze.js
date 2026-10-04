const SCHEMA = {
  type: 'object',
  additionalProperties: false,
  properties: {
    convertible: { type: 'boolean' },
    reason: { type: 'string' },
    width: { type: 'integer' },
    height: { type: 'integer' },
    frame_arrays: { type: 'array', items: { type: 'string' } },
    fps: { type: 'number' },
    frame_duration_ms: { type: 'integer' },
    loop: { type: 'boolean' },
    bitmap_format: { type: 'string' }
  },
  required: ['convertible','reason','width','height','frame_arrays','fps','frame_duration_ms','loop','bitmap_format']
};

const SYSTEM_PROMPT = `You analyze Arduino .ino source that contains an OLED animation.

Your job is identification only. The server will extract the actual bytes from the original source after your response.

Rules:
- Inspect the Arduino source carefully.
- Identify the actual byte arrays that contain complete animation frames.
- Identify width and height.
- Identify frame count from the real frame arrays.
- Identify FPS or frame duration from the source.
- Identify the bitmap/pixel format when possible.
- Determine whether reliable conversion to exactly 128x64, 1-bit, 1024 bytes per frame is possible.
- Preserve the original visual appearance as closely as possible.
- Ignore setup(), loop(), WiFi, GPIO, sensors, display initialization, and unrelated Arduino logic except where they reveal frame timing or frame ordering.
- Never invent missing frame data.
- Never fabricate frame arrays or frame names.
- Never claim convertible=true unless the named arrays really contain the complete frame bytes in the source.
- Return structured JSON only.

The server validates your answer and extracts bytes itself. If the source cannot be converted reliably, set convertible=false and explain why.`;

export async function analyzeWithOpenAI(source, env) {
  if (!env.OPENAI_API_KEY) throw new Error('Deterministic parser failed and OPENAI_API_KEY is not configured.');
  const clipped = source.length > 500_000 ? source.slice(0, 500_000) : source;
  const response = await fetch('https://api.openai.com/v1/responses', {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${env.OPENAI_API_KEY}`,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      model: env.OPENAI_MODEL || 'gpt-5-mini',
      store: false,
      input: [
        { role: 'system', content: [{ type: 'input_text', text: SYSTEM_PROMPT }] },
        { role: 'user', content: [{ type: 'input_text', text: clipped }] }
      ],
      text: { format: { type: 'json_schema', name: 'animation_analysis', strict: true, schema: SCHEMA } }
    })
  });
  if (!response.ok) throw new Error(`OpenAI fallback failed (${response.status}).`);
  const data = await response.json();
  if (!data.output_text) throw new Error('OpenAI returned no structured analysis.');
  try { return JSON.parse(data.output_text); }
  catch { throw new Error('OpenAI returned invalid structured JSON.'); }
}
