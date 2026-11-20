// Preset capture with the user (docs/mcp-control.md, "Choosing presets with
// the user"). Players such as Synchron Player choose presets only in their
// own editor: Fiddle opens the patch's player window, the user picks the
// preset there, and Fiddle captures what the window then holds.
import {
  CLIENT_CAPABILITIES_META_KEY, inputRequired, inputResponse
} from '@modelcontextprotocol/server';
import type { McpServer } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';

import { callFiddle } from './fiddle-client.js';
import { callFiddleTool, toolResult } from './tool-results.js';

type Era = 'legacy' | 'modern';
type PlayerState = {
  open?: boolean; session?: string; status?: string; error?: string; windowTitle?: string;
  expectedPresetName?: string; patchName?: string;
};

const RESPONSE_KEY = 'preset';
const LOAD_TIMEOUT_MS = 90_000;
const ANSWER_TIMEOUT_MS = 30 * 60_000;

const presetQuestion = {
  type: 'object' as const,
  properties: {
    already_loaded: {
      type: 'boolean' as const,
      title: 'The preset was already loaded',
      description: 'Tick this if the window already showed the right preset, so nothing changed.',
      default: false
    }
  }
};

function errorResult(message: string) {
  return { content: [{ type: 'text' as const, text: message }], isError: true };
}

/**
 * Wait for the window this call opened to load. Another window for the patch
 * (closed and reopened meanwhile) is reported as replaced, never adopted: its
 * patch may now expect a different preset than this call would ask for.
 */
async function waitForPlayer(patchId: string, session: string): Promise<PlayerState> {
  const deadline = Date.now() + LOAD_TIMEOUT_MS;
  for (;;) {
    const state = await callFiddle('patch.player.status', { patchId }) as PlayerState;
    if (!state.open || state.session !== session)
      return { ...state, status: 'replaced' };
    if (state.status !== 'loading' || Date.now() >= deadline)
      return state;
    await new Promise(resolve => setTimeout(resolve, 300));
  }
}

function question(player: PlayerState): string {
  const preset = player.expectedPresetName
    ? `the preset "${player.expectedPresetName}"`
    : 'the preset this patch should use';
  return `In Fiddle's player window "${player.windowTitle ?? player.patchName ?? 'player'}", ` +
    `choose ${preset}. Then accept here, or decline to skip this patch.`;
}

/** The player window a question was about, carried through a retry. */
function encodeState(patchId: string, session: string): string {
  return JSON.stringify({ patchId, session });
}

function decodeState(raw: unknown, patchId: string): string | undefined {
  if (typeof raw !== 'string') return undefined;
  try {
    const state = JSON.parse(raw) as { patchId?: unknown; session?: unknown };
    return state.patchId === patchId && typeof state.session === 'string' && state.session
      ? state.session : undefined;
  } catch {
    return undefined;
  }
}

/**
 * What the user answered: capture, or close the window and say why not. The
 * session ties the answer to the window it was about: Fiddle refuses it for a
 * window that has since closed or reopened, and recognises a repeated answer
 * for a capture that already happened.
 */
async function finish(patchId: string, session: string, action: string,
                      content: Record<string, unknown> | undefined) {
  if (action !== 'accept') {
    await callFiddle('patch.player.close', { patchId, session });
    return toolResult({
      captured: false, patchId,
      reason: action === 'decline' ? 'The user skipped this patch.'
                                   : 'The user cancelled; the player window was closed.'
    });
  }
  try {
    const result = await callFiddle('patch.player.capture', {
      patchId, session, allowUnchanged: content?.already_loaded === true
    });
    return toolResult(result);
  } catch (error) {
    // The window stays open, so the agent can ask again in chat and capture.
    return toolResult({
      captured: false, patchId, playerStillOpen: true,
      reason: error instanceof Error ? error.message : String(error)
    });
  }
}

export function registerPresetTools(server: McpServer, era: Era): void {
  const patchId = z.object({ patch_id: z.string().min(1).describe('Patch ID from fiddle_get_library or fiddle_find_patches') });
  const writes = {
    readOnlyHint: false, destructiveHint: false, idempotentHint: false,
    openWorldHint: false
  };

  server.registerTool('fiddle_open_patch_player', {
    title: 'Open patch player window',
    description: 'Open a patch\'s player in a Fiddle window, outside the mixer, starting from its saved state, so the user can choose a preset there. Then fiddle_capture_patch_player.',
    inputSchema: patchId,
    annotations: { ...writes, idempotentHint: true }
  }, async ({ patch_id }) =>
    callFiddleTool('patch.player.open', { patchId: patch_id }));

  server.registerTool('fiddle_get_patch_player', {
    title: 'Get patch player window',
    description: 'Whether a patch\'s player window is open, loading, loaded or failed, and whether its state changed since it loaded.',
    inputSchema: patchId,
    annotations: {
      readOnlyHint: true, destructiveHint: false, idempotentHint: true,
      openWorldHint: false
    }
  }, async ({ patch_id }) =>
    callFiddleTool('patch.player.status', { patchId: patch_id }));

  server.registerTool('fiddle_capture_patch_player', {
    title: 'Capture patch player',
    description: 'Save the player window\'s state into its patch, mark the patch finished and close the window. Refused if the player hasn\'t changed, unless allow_unchanged (the preset was already right). One library-history step.',
    inputSchema: patchId.extend({
      allow_unchanged: z.boolean().optional()
    }),
    annotations: writes
  }, async ({ patch_id, allow_unchanged }) =>
    callFiddleTool('patch.player.capture', {
      patchId: patch_id, ...(allow_unchanged ? { allowUnchanged: true } : {})
    }));

  server.registerTool('fiddle_close_patch_player', {
    title: 'Close patch player window',
    description: 'Close a patch\'s player window without saving.',
    inputSchema: patchId,
    annotations: { ...writes, idempotentHint: true }
  }, async ({ patch_id }) =>
    callFiddleTool('patch.player.close', { patchId: patch_id }));

  server.registerTool('fiddle_set_patch_preset_with_user', {
    title: 'Choose a preset with the user',
    description: 'Open the patch\'s player window, ask the user to choose its preset there, and capture it. If the client can\'t ask, returns waitingForUser: ask in chat, then fiddle_capture_patch_player.',
    inputSchema: patchId,
    annotations: writes
  }, async ({ patch_id }, ctx) => {
    try {
      // The 2026-07-28 protocol asks by returning input_required; the client
      // calls again with the answer.
      const answer = inputResponse(ctx.mcpReq.inputResponses, RESPONSE_KEY);
      if (answer.kind === 'elicit') {
        const session = decodeState(ctx.mcpReq.requestState(), patch_id);
        if (!session)
          return errorResult('The answer did not say which player window it was about; nothing was captured.');
        return await finish(patch_id, session, answer.action, answer.content);
      }

      const opened = await callFiddle('patch.player.open', { patchId: patch_id }) as PlayerState;
      if (!opened.session)
        return errorResult('Fiddle did not report the player window\'s session.');
      const session = opened.session;
      // The question uses what this call opened (expected preset, title); only
      // the loading status comes from the wait.
      const loaded = await waitForPlayer(patch_id, session);
      const player = { ...opened, status: loaded.status, error: loaded.error };
      if (player.status === 'replaced')
        return errorResult('The player window was closed or reopened while it loaded; nothing was asked. Try again.');
      if (player.status === 'failed')
        return errorResult(`The player failed to open: ${player.error ?? 'unknown error'}`);
      if (player.status !== 'loaded')
        return errorResult('The player is still loading. Try fiddle_set_patch_preset_with_user again shortly.');
      const message = question(player);

      if (era === 'modern') {
        const envelope = ctx.mcpReq.envelope as Record<string, unknown> | undefined;
        const capabilities = envelope?.[CLIENT_CAPABILITIES_META_KEY] as
          { elicitation?: unknown } | undefined;
        if (capabilities?.elicitation)
          return inputRequired({
            inputRequests: {
              [RESPONSE_KEY]: inputRequired.elicit({ message, requestedSchema: presetQuestion })
            },
            requestState: encodeState(patch_id, session)
          });
      } else if (server.server.getClientCapabilities()?.elicitation) {
        const reply = await ctx.mcpReq.elicitInput(
          { mode: 'form', message, requestedSchema: presetQuestion },
          { timeout: ANSWER_TIMEOUT_MS });
        return await finish(patch_id, session, reply.action, reply.content);
      }
      return toolResult({
        captured: false, waitingForUser: true, patchId: patch_id,
        instructions: `${message.replace(' Then accept here, or decline to skip this patch.', '')} ` +
          'Ask the user in chat; when they say it is done, call fiddle_capture_patch_player.'
      });
    } catch (error) {
      return errorResult(error instanceof Error ? error.message : String(error));
    }
  });
}
