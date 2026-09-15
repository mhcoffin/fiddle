import { randomUUID } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { createConnection } from 'node:net';

type Descriptor = {
  schemaVersion: number;
  transport: 'tcp';
  host: string;
  port: number;
  token: string;
  pid: number;
};

type WireResponse = {
  id: string;
  ok: boolean;
  result?: unknown;
  error?: string;
};

export function descriptorPath(environment = process.env): string {
  if (environment.FIDDLE_AGENT_CONTROL_FILE)
    return environment.FIDDLE_AGENT_CONTROL_FILE;
  if (process.platform === 'darwin')
    return join(homedir(), 'Library', 'Application Support', 'Fiddle',
      'agent-control.json');
  const dataRoot = environment.XDG_DATA_HOME ??
    join(homedir(), '.local', 'share');
  return join(dataRoot, 'Fiddle', 'agent-control.json');
}

async function loadDescriptor(): Promise<Descriptor> {
  let parsed: unknown;
  try {
    parsed = JSON.parse(await readFile(descriptorPath(), 'utf8'));
  } catch {
    throw new Error('Fiddle is not running or has not finished starting.');
  }
  if (!parsed || typeof parsed !== 'object')
    throw new Error('Fiddle published an invalid control descriptor.');
  const value = parsed as Partial<Descriptor>;
  if (value.schemaVersion !== 1 || value.transport !== 'tcp' ||
      value.host !== '127.0.0.1' || !Number.isInteger(value.port) ||
      typeof value.token !== 'string' || !value.token)
    throw new Error('Fiddle published an unsupported control descriptor.');
  return value as Descriptor;
}

export async function callFiddle(method: string,
                                 params: Record<string, unknown> = {}):
                                 Promise<unknown> {
  const descriptor = await loadDescriptor();
  const id = randomUUID();
  const request = JSON.stringify({ id, token: descriptor.token, method, params }) +
    '\n';

  return await new Promise((resolve, reject) => {
    const socket = createConnection({ host: descriptor.host,
                                      port: descriptor.port });
    let response = '';
    let settled = false;
    const finish = (error?: Error, value?: unknown) => {
      if (settled)
        return;
      settled = true;
      socket.destroy();
      if (error)
        reject(error);
      else
        resolve(value);
    };
    socket.setTimeout(12_000, () =>
      finish(new Error('Fiddle did not answer the control request.')));
    socket.on('error', () =>
      finish(new Error('Fiddle is not accepting agent control requests.')));
    socket.on('connect', () => socket.write(request));
    socket.on('data', chunk => {
      response += chunk.toString('utf8');
      if (response.length > 1024 * 1024) {
        finish(new Error('Fiddle returned an oversized response.'));
        return;
      }
      const newline = response.indexOf('\n');
      if (newline < 0)
        return;
      try {
        const message = JSON.parse(response.slice(0, newline)) as WireResponse;
        if (message.id !== id)
          throw new Error('Fiddle returned a mismatched response.');
        if (!message.ok)
          throw new Error(message.error || 'Fiddle rejected the request.');
        finish(undefined, message.result);
      } catch (error) {
        finish(error instanceof Error ? error : new Error(String(error)));
      }
    });
    socket.on('end', () => {
      if (!settled)
        finish(new Error('Fiddle closed the control connection early.'));
    });
  });
}
