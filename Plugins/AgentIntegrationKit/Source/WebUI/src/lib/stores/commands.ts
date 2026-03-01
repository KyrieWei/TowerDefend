import { writable, get } from 'svelte/store';
import { onCommandsAvailable, type SlashCommand } from '$lib/bridge.js';
import { currentSessionId } from '$lib/stores/sessions.js';

export const availableCommands = writable<SlashCommand[]>([]);

let commandsBound = false;

/** Wire up commands availability callback. Call once on mount. */
export function bindCommandsListener(): void {
	if (commandsBound) return;
	commandsBound = true;

	onCommandsAvailable((sessionId, commands) => {
		const activeSessionId = get(currentSessionId);
		if (sessionId !== activeSessionId) return;
		availableCommands.set(commands);
	});

	// Prevent stale command lists from a previously selected session.
	currentSessionId.subscribe(() => {
		availableCommands.set([]);
	});
}
