import { callFiddle } from './fiddle-client.js';

type JsonObject = Record<string, unknown>;

export function toolResult(value: unknown) {
  const structuredContent: JsonObject =
    value && typeof value === 'object' && !Array.isArray(value)
      ? value as JsonObject
      : { value };
  return {
    content: [{ type: 'text' as const,
                text: JSON.stringify(structuredContent, null, 2) }],
    structuredContent
  };
}

export async function callFiddleTool(method: string,
                              params: Record<string, unknown> = {}) {
  try {
    return toolResult(await callFiddle(method, params));
  } catch (error) {
    return {
      content: [{ type: 'text' as const,
                  text: error instanceof Error ? error.message : String(error) }],
      isError: true
    };
  }
}

export async function localTool(read: () => Promise<unknown>) {
  try {
    return toolResult(await read());
  } catch (error) {
    return {
      content: [{ type: 'text' as const,
                  text: error instanceof Error ? error.message : String(error) }],
      isError: true
    };
  }
}
