import { writable, get } from 'svelte/store';
import {
	getSessionMessages,
	sendPrompt,
	cancelPrompt,
	onMessage,
	type ChatMessage,
	type ContentBlock,
	type StreamingUpdate
} from '$lib/bridge.js';
import { currentSessionId } from '$lib/stores/sessions.js';
import { sessionStates } from '$lib/stores/agentState.js';
import { handleUsageUpdate } from '$lib/stores/usage.js';
import { setAuthRequired } from '$lib/stores/auth.js';
import { sessions } from '$lib/stores/sessions.js';
import { createUUID } from '$lib/utils.js';

export const messages = writable<ChatMessage[]>([]);
export const isStreaming = writable(false);
export const messagesBySession = writable<Record<string, ChatMessage[]>>({});
const streamingBySession = writable<Record<string, boolean>>({});
let lastLoadRequestId = 0;

function isLikelyAuthError(update: StreamingUpdate, agentName: string): string | null {
	// Explicit adapter auth-required code.
	if (update.errorCode === -32000) {
		return 'Authentication is required to continue.';
	}

	const lowerText = (update.errorMessage ?? update.text ?? '').toLowerCase();
	if (!lowerText) return null;

	const isClaudeOrCodex = agentName === 'Claude Code' || agentName === 'Codex CLI';
	if (!isClaudeOrCodex) return null;

	// Common adapter/CLI failures when the local CLI session is not signed in.
	if (lowerText.includes('query closed before response received')) {
		return `Your ${agentName === 'Claude Code' ? 'Claude' : 'Codex'} CLI session may be signed out. Sign in and try again.`;
	}
	if (lowerText.includes('not authenticated') || lowerText.includes('authentication required')) {
		return `Your ${agentName === 'Claude Code' ? 'Claude' : 'Codex'} CLI needs authentication.`;
	}
	if (lowerText.includes("run 'claude'") || lowerText.includes("run 'codex'")) {
		return 'Run the CLI sign-in flow and return to the editor.';
	}

	return null;
}

function formatAgentError(update: StreamingUpdate, agentName: string): string {
	const raw = (update.errorMessage ?? update.text ?? '').trim();
	const lower = raw.toLowerCase();
	const codeSuffix = typeof update.errorCode === 'number' ? ` (code ${update.errorCode})` : '';

	if (!raw || lower === 'unknown error' || lower === 'agent error') {
		return `Agent error${codeSuffix}. Please retry, and if it repeats open setup for ${agentName || 'this agent'} to verify installation/authentication.`;
	}

	if (lower.includes('failed to connect') || lower.includes('cannot send prompt')) {
		return `${raw}${codeSuffix}. The agent process failed to start or reconnect. Open setup and verify the CLI is installed and authenticated.`;
	}

	return codeSuffix ? `${raw}${codeSuffix}` : raw;
}

function setSessionMessages(sessionId: string, msgs: ChatMessage[]): void {
	messagesBySession.update((all) => ({
		...all,
		[sessionId]: msgs
	}));
	if (get(currentSessionId) === sessionId) {
		messages.set(msgs);
	}
}

function updateSessionMessages(sessionId: string, updater: (msgs: ChatMessage[]) => ChatMessage[]): void {
	let nextMsgs: ChatMessage[] = [];
	messagesBySession.update((all) => {
		nextMsgs = updater(all[sessionId] ?? []);
		return {
			...all,
			[sessionId]: nextMsgs
		};
	});
	if (get(currentSessionId) === sessionId) {
		messages.set(nextMsgs);
	}
}

function setSessionStreaming(sessionId: string, streaming: boolean): void {
	streamingBySession.update((all) => ({
		...all,
		[sessionId]: streaming
	}));
	if (get(currentSessionId) === sessionId) {
		isStreaming.set(streaming);
	}
}

function finishStreamingForSession(sessionId: string): void {
	updateSessionMessages(sessionId, (msgs) => {
		const last = msgs[msgs.length - 1];
		if (last?.isStreaming) {
			// Create new references so Svelte detects the change
			const finished: ChatMessage = {
				...last,
				isStreaming: false,
				contentBlocks: last.contentBlocks.map((b) => ({ ...b, isStreaming: false }))
			};
			return [...msgs.slice(0, -1), finished];
		}
		return msgs;
	});
	setSessionStreaming(sessionId, false);
}

// Keep active-view stores in sync when current session changes.
currentSessionId.subscribe((sid) => {
	if (!sid) {
		messages.set([]);
		isStreaming.set(false);
		return;
	}
	const allMessages = get(messagesBySession);
	const allStreaming = get(streamingBySession);
	messages.set(allMessages[sid] ?? []);
	isStreaming.set(allStreaming[sid] ?? false);
});

/** Load saved messages for a session */
export async function loadMessages(sessionId: string): Promise<void> {
	const requestId = ++lastLoadRequestId;
	try {
		const msgs = await getSessionMessages(sessionId);
		if (requestId !== lastLoadRequestId) return;
		setSessionMessages(sessionId, msgs);

		// Derive isStreaming from loaded data — if the last message is still
		// streaming (agent working in background), reflect that in the store
		const last = msgs[msgs.length - 1];
		setSessionStreaming(sessionId, last?.isStreaming === true);
	} catch (e) {
		if (requestId !== lastLoadRequestId) return;
		console.warn('Failed to load messages:', e);
		setSessionMessages(sessionId, []);
		setSessionStreaming(sessionId, false);
	}
}

/** Send a user message and trigger agent prompt */
export async function sendMessage(sessionId: string, text: string): Promise<boolean> {
	if (!sessionId || !text.trim()) return false;

	// Optimistically add user message to store
	const userMsg: ChatMessage = {
		messageId: createUUID(),
		role: 'user',
		isStreaming: false,
		timestamp: new Date().toISOString(),
		contentBlocks: [{ type: 'text', text: text.trim(), isStreaming: false }]
	};

	updateSessionMessages(sessionId, (msgs) => [...msgs, userMsg]);

	try {
		await sendPrompt(sessionId, text.trim());
		setSessionStreaming(sessionId, true);
		return true;
	} catch (e) {
		console.warn('Failed to send prompt:', e);
		setSessionStreaming(sessionId, false);
		// Revert the optimistic user message when prompt dispatch fails
		updateSessionMessages(sessionId, (msgs) =>
			msgs.filter((m) => m.messageId !== userMsg.messageId)
		);
		return false;
	}
}

/** Cancel the current streaming prompt */
export async function cancelCurrentPrompt(sessionId: string): Promise<void> {
	if (!sessionId) return;
	try {
		await cancelPrompt(sessionId);
	} catch (e) {
		console.warn('Failed to cancel prompt:', e);
	}
	finishStreamingForSession(sessionId);
}

/** Mark the current streaming message as complete */
export function finishStreaming(): void {
	const sid = get(currentSessionId);
	if (!sid) {
		isStreaming.set(false);
		return;
	}
	finishStreamingForSession(sid);
}

// ── Streaming Update Handler ─────────────────────────────────────────

/**
 * Get or create the streaming assistant message. Mutates `msgs` by pushing
 * if a new message is needed. Returns the message object (mutated in place).
 *
 * Key behavior: if the last message is an assistant message that was
 * prematurely closed by finishStreaming() (e.g., between sub-turns of a
 * multi-tool-call response), we re-open it instead of creating a new one.
 * A new assistant message is only created after a user/system message.
 */
function getOrCreateAssistantMessage(msgs: ChatMessage[]): ChatMessage {
	const last = msgs[msgs.length - 1];
	if (last?.role === 'assistant') {
		// Re-open if it was prematurely finished between sub-turns
		last.isStreaming = true;
		return last;
	}
	const newMsg: ChatMessage = {
		messageId: createUUID(),
		role: 'assistant',
		isStreaming: true,
		timestamp: new Date().toISOString(),
		contentBlocks: []
	};
	msgs.push(newMsg);
	return newMsg;
}

function appendToBlock(msg: ChatMessage, blockType: 'text' | 'thought', chunk: string): void {
	const blocks = msg.contentBlocks;
	const lastBlock = blocks[blocks.length - 1];

	// Append to the last block of the same type, even if it was prematurely
	// closed by finishStreaming() between sub-turns. Re-open it.
	if (lastBlock?.type === blockType) {
		lastBlock.text += chunk;
		lastBlock.isStreaming = true;
	} else {
		blocks.push({
			type: blockType,
			text: chunk,
			isStreaming: true
		});
	}
}

/**
 * Clone a message and all its content blocks to create new object references.
 * Svelte 5's keyed {#each} uses reference equality — same reference = no re-render.
 * We must produce new references for any mutated message so the UI updates.
 */
function cloneMessage(msg: ChatMessage): ChatMessage {
	return {
		...msg,
		contentBlocks: msg.contentBlocks.map((b) => ({ ...b }))
	};
}

function stopStreamingOnMessage(msg: ChatMessage): void {
	msg.isStreaming = false;
	for (const b of msg.contentBlocks) {
		if (b.isStreaming) b.isStreaming = false;
	}
}

function handleStreamingUpdate(sessionId: string, update: StreamingUpdate): void {
	const activeId = get(currentSessionId);

	// Usage updates don't create or modify message blocks — handle separately
	if (update.type === 'usage') {
		if (sessionId === activeId) {
			handleUsageUpdate(update);
		}
		return;
	}

	// user_message_chunk (from history replay) — finish any in-progress assistant
	// message and add the user message directly
	if (update.type === 'user_message_chunk') {
		finishStreamingForSession(sessionId);
		const userMsg: ChatMessage = {
			messageId: createUUID(),
			role: 'user',
			isStreaming: false,
			timestamp: new Date().toISOString(),
			contentBlocks: [{ type: 'text', text: update.text, isStreaming: false }]
		};
		updateSessionMessages(sessionId, (msgs) => [...msgs, userMsg]);
		return;
	}

		updateSessionMessages(sessionId, (msgs) => {
			const working = [...msgs];
			const msg = getOrCreateAssistantMessage(working);
			let shouldSessionStream = true;

			switch (update.type) {
			case 'text_chunk':
				if (update.systemStatus) {
					// System status (compaction, etc.) — create or update a system block
					const existingSystem = msg.contentBlocks.find(
						(b) => b.type === 'system' && b.systemStatus === 'compacting'
					);
					if (update.systemStatus === 'compacted' && existingSystem) {
						// Compaction finished — update the existing "compacting" block
						existingSystem.text = update.text;
						existingSystem.systemStatus = 'compacted';
						existingSystem.isStreaming = false;
					} else {
						// New system status block
						msg.contentBlocks.push({
							type: 'system',
							text: update.text,
							isStreaming: update.systemStatus === 'compacting',
							systemStatus: update.systemStatus
						});
					}
				} else {
					appendToBlock(msg, 'text', update.text);
				}
				break;

			case 'thought_chunk':
				appendToBlock(msg, 'thought', update.text);
				break;

			case 'tool_call': {
				// Ensure a unique toolCallId — prevents undefined===undefined collisions
				// when two parallel tool calls arrive without IDs
				const tcId = update.toolCallId || `gen_${createUUID()}`;

				const existing = msg.contentBlocks.find(
					(b) => b.type === 'tool_call' && b.toolCallId === tcId
				);
				if (existing) {
					if (update.toolArguments) {
						existing.toolArguments = update.toolArguments;
					}
					if (update.toolName && !existing.toolName) {
						existing.toolName = update.toolName;
					}
					if (update.parentToolCallId && !existing.parentToolCallId) {
						existing.parentToolCallId = update.parentToolCallId;
					}
				} else {
					for (const b of msg.contentBlocks) {
						if ((b.type === 'text' || b.type === 'thought') && b.isStreaming) {
							b.isStreaming = false;
						}
					}
					msg.contentBlocks.push({
						type: 'tool_call',
						text: '',
						isStreaming: true,
						toolCallId: tcId,
						toolName: update.toolName,
						toolArguments: update.toolArguments,
						parentToolCallId: update.parentToolCallId
					});
				}
				break;
			}

			case 'tool_result': {
				const resultTcId = update.toolCallId;

				let toolCall = msg.contentBlocks.find(
					(b) => b.type === 'tool_call' && b.toolCallId === resultTcId
				);

				// If no matching tool_call exists, create a synthetic one so the
				// result is visible. This handles lost/out-of-order tool_call
				// notifications (the Slate UI renders orphaned results; we should too).
				if (!toolCall && resultTcId) {
					toolCall = {
						type: 'tool_call',
						text: '',
						isStreaming: false,
						toolCallId: resultTcId,
						toolName: update.toolName || 'tool',
						toolArguments: '',
						parentToolCallId: update.parentToolCallId
					};
					msg.contentBlocks.push(toolCall);
				}

				if (toolCall) {
					toolCall.isStreaming = false;
				}

				const existingResult = msg.contentBlocks.find(
					(b) => b.type === 'tool_result' && b.toolCallId === resultTcId
				);
				if (existingResult) {
					existingResult.toolResult = update.toolResult;
					existingResult.toolSuccess = update.toolSuccess;
					if (update.images) existingResult.images = update.images;
				} else {
					msg.contentBlocks.push({
						type: 'tool_result',
						text: '',
						isStreaming: false,
						toolCallId: resultTcId,
						toolResult: update.toolResult,
						toolSuccess: update.toolSuccess,
						images: update.images,
						parentToolCallId: update.parentToolCallId
					});
				}
				break;
			}

			case 'error': {
					const sessionAgent = update.agentName
						|| get(sessions).find(s => s.sessionId === sessionId)?.agentName
						|| '';
					const authReason = isLikelyAuthError(update, sessionAgent);
					if (authReason) {
						setAuthRequired(sessionAgent, authReason);
						stopStreamingOnMessage(msg);
						shouldSessionStream = false;
						break;
					}
					msg.contentBlocks.push({
						type: 'error',
						text: formatAgentError(update, sessionAgent),
						isStreaming: false
					});
					stopStreamingOnMessage(msg);
					shouldSessionStream = false;
					break;
				}

			default:
				break;
		}

			setSessionStreaming(sessionId, shouldSessionStream);

		// Build new array with cloned modified message — new references
		// are required so Svelte 5's keyed {#each} detects the change.
		return working.map((m) => (m === msg ? cloneMessage(m) : m));
	});
}

// ── Binding ──────────────────────────────────────────────────────────

let messageBound = false;
let stateUnsubscribe: (() => void) | null = null;

/** Wire up streaming callbacks. Call once on mount. */
export function bindMessageListener(): void {
	if (messageBound) return;
	messageBound = true;

	// Bind streaming updates from C++ bridge
	onMessage(handleStreamingUpdate);

	// Watch per-session state transitions to detect streaming completion.
	// We track previous states per session ID so transitions on background
	// sessions don't leak into the active session's streaming state.
	const prevStates: Record<string, string> = {};
	stateUnsubscribe = sessionStates.subscribe((states) => {
		for (const [sessionId, sessionState] of Object.entries(states)) {
			const prev = prevStates[sessionId];
			const cur = sessionState.state;

			// When any session transitions from prompting → idle, finish streaming.
			if (prev === 'prompting' && (cur === 'ready' || cur === 'in_session')) {
				finishStreamingForSession(sessionId);
			}

			prevStates[sessionId] = cur;
		}
	});
}
