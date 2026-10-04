export const FRAME_BYTES = 1024;
export const MAX_FRAMES = 600;

export function validateAnimation(metadata, bytes) {
  if (!metadata || metadata.width !== 128 || metadata.height !== 64) throw new Error('Invalid animation dimensions.');
  if (metadata.bitsPerPixel !== 1 || metadata.bytesPerFrame !== FRAME_BYTES) throw new Error('Invalid animation pixel format.');
  if (!Number.isInteger(metadata.frames) || metadata.frames < 1 || metadata.frames > MAX_FRAMES) throw new Error('Invalid frame count.');
  if (!(bytes instanceof Uint8Array)) throw new Error('Invalid frame data.');
  if (bytes.byteLength !== metadata.frames * FRAME_BYTES) throw new Error(`Frame binary length mismatch: got ${bytes.byteLength}, expected ${metadata.frames * FRAME_BYTES}.`);
  if (!Number.isFinite(metadata.fps) || metadata.fps <= 0 || metadata.fps > 60) throw new Error('Invalid FPS.');
  return true;
}
