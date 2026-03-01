import { writable, get } from 'svelte/store';
import {
	getModes,
	setMode,
	onModesAvailable,
	onModeChanged,
	type ModeInfo,
	type ModeState
} from '$lib/bridge.js';
import { selectedAgent } from '$lib/stores/agents.js';
import { currentSessionId } from '$lib/stores/sessions.js';

/** Available modes for the current agent */
export const availableModes = writable<ModeInfo[]>([]);

/** Currently selected mode ID */
export const currentModeId = writable<string>('');
let lastModeLoadRequestId = 0;

/** Load modes for an agent (called when agent changes) */
export async function loadModesForAgent(agentName: string): Promise<void> {
	const requestId = ++lastModeLoadRequestId;
	const isStillActiveTarget = () => {
		const activeAgent = get(selectedAgent);
		const activeSession = get(currentSessionId);
		return !!activeSession && !!activeAgent && activeAgent.name === agentName;
	};

	try {
		const state = await getModes(agentName);
		if (requestId !== lastModeLoadRequestId) return;
		if (!isStillActiveTarget()) return;
		availableModes.set(state.modes);
		currentModeId.set(state.currentModeId);
	} catch (e) {
		if (requestId !== lastModeLoadRequestId) return;
		console.warn('Failed to load modes:', e);
		if (isStillActiveTarget()) {
			availableModes.set([]);
			currentModeId.set('');
		}
	}
}

/** Change the active mode */
export async function changeMode(agentName: string, modeId: string): Promise<void> {
	currentModeId.set(modeId);
	await setMode(agentName, modeId);
}

/** Check if currently in plan mode */
export function isInPlanMode(modeId: string): boolean {
	const lower = modeId.toLowerCase();
	return lower === 'plan' || lower === 'architect' || lower.includes('plan');
}

// ── Binding ──────────────────────────────────────────────────────────

let bound = false;

/** Wire up mode callbacks. Call once on mount. */
export function bindModeListener(): void {
	if (bound) return;
	bound = true;

	onModesAvailable((agentName, modeState) => {
		// Only update if this is for the currently selected agent
		const agent = get(selectedAgent);
		if (agent && agentName === agent.name) {
			availableModes.set(modeState.modes);
			if (modeState.currentModeId) {
				currentModeId.set(modeState.currentModeId);
			}
		}
	});

	onModeChanged((agentName, modeId) => {
		const agent = get(selectedAgent);
		if (agent && agentName === agent.name) {
			currentModeId.set(modeId);
		}
	});
}
