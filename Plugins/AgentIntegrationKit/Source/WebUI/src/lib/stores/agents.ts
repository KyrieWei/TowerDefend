import { writable, get } from 'svelte/store';
import { getAgents, getLastUsedAgent, isInUnreal, type AgentInfo } from '$lib/bridge.js';
import { ClaudeIcon, ChatGptIcon, GoogleGeminiIcon } from '@hugeicons/core-free-icons';
import { GithubCopilotIcon, CursorIcon, OpenCodeIcon, OpenRouterIcon, KimiIcon } from '$lib/icons/brands.js';

type IconElement = readonly [string, Readonly<Record<string, string | number>>];
type IconData = readonly IconElement[];

export type AgentStatus = 'available' | 'not_installed' | 'missing_key' | 'unknown';

export type Agent = {
	id: string;
	name: string;
	shortName: string;
	provider: string;
	description: string;
	color: string;
	letter: string;
	icon?: IconData;
	status: AgentStatus;
	statusMessage: string;
	isConnected: boolean;
};

type AgentMeta = { shortName: string; provider: string; description: string; color: string; letter: string; icon?: IconData };

const agentMeta: Record<string, AgentMeta> = {
	'Claude Code':   { shortName: 'Claude Code', provider: 'Anthropic',   description: 'Agentic coding via CLI',          color: '#D97757', letter: 'C',  icon: ClaudeIcon },
	'OpenRouter':    { shortName: 'OpenRouter',   provider: 'Built-in',    description: 'Multi-model API gateway',         color: '#FFFFFF', letter: 'OR', icon: OpenRouterIcon },
	'Gemini CLI':    { shortName: 'Gemini',       provider: 'Google',      description: 'Agentic coding via CLI',          color: '#4285F4', letter: 'G',  icon: GoogleGeminiIcon },
	'Codex CLI':     { shortName: 'Codex',        provider: 'OpenAI',      description: 'Agentic coding via CLI',          color: '#FFFFFF', letter: 'Cx', icon: ChatGptIcon },
	'Kimi CLI':      { shortName: 'Kimi',         provider: 'Moonshot AI', description: 'Agentic coding via CLI',          color: '#027AFF', letter: 'Km', icon: KimiIcon },
	'OpenCode':      { shortName: 'OpenCode',     provider: 'Open source', description: 'Open-source coding agent',        color: '#FFFFFF', letter: 'OC', icon: OpenCodeIcon },
	'Cursor Agent':  { shortName: 'Cursor',       provider: 'Anysphere',   description: 'AI-powered code editor agent',    color: '#FFFFFF', letter: 'Cu', icon: CursorIcon },
	'Copilot CLI':   { shortName: 'Copilot',      provider: 'GitHub',      description: 'GitHub\'s coding assistant CLI',  color: '#6E7681', letter: 'Cp', icon: GithubCopilotIcon }
};

// Generate a color from agent name (for unknown agents)
function hashColor(name: string): string {
	let hash = 0;
	for (let i = 0; i < name.length; i++) {
		hash = name.charCodeAt(i) + ((hash << 5) - hash);
	}
	const h = Math.abs(hash) % 360;
	return `hsl(${h}, 55%, 55%)`;
}

function agentInfoToAgent(info: AgentInfo): Agent {
	const meta = agentMeta[info.name] ?? {
		shortName: info.name,
		provider: '',
		description: '',
		color: hashColor(info.name),
		letter: info.name.slice(0, 2).toUpperCase()
	};
	return {
		id: info.id,
		name: info.name,
		shortName: meta.shortName,
		provider: meta.provider,
		description: meta.description,
		color: meta.color,
		letter: meta.letter,
		icon: meta.icon,
		status: info.status,
		statusMessage: info.statusMessage || '',
		isConnected: info.isConnected
	};
}

export const agents = writable<Agent[]>([]);
export const selectedAgent = writable<Agent | null>(null);
export const agentsLoaded = writable(false);

/** Fetch agents from the UE bridge and update stores, restoring last-used agent */
export async function loadAgents(): Promise<void> {
	try {
		const [infos, lastUsedName] = await Promise.all([getAgents(), getLastUsedAgent()]);
		const mapped = infos.map(agentInfoToAgent);
		agents.set(mapped);
		if (mapped.length > 0) {
			const current = get(selectedAgent);
			const stillExists = current && mapped.find(a => a.id === current.id);
			if (!stillExists) {
				// Restore last-used agent if available, otherwise fall back to first
				const lastUsed = lastUsedName ? mapped.find(a => a.name === lastUsedName) : null;
				selectedAgent.set(lastUsed ?? mapped[0]);
			}
		}
		agentsLoaded.set(true);
	} catch (e) {
		console.warn('Failed to load agents:', e);
		agentsLoaded.set(true);
	}
}

/** Convert a hex or hsl color to one with alpha for Chrome 90 compat (no color-mix/oklch) */
export function withAlpha(color: string, alpha: number): string {
	if (color.startsWith('#')) {
		const hex = color.replace('#', '');
		const r = parseInt(hex.substring(0, 2), 16);
		const g = parseInt(hex.substring(2, 4), 16);
		const b = parseInt(hex.substring(4, 6), 16);
		return `rgba(${r}, ${g}, ${b}, ${alpha})`;
	}
	if (color.startsWith('hsl(')) {
		return color.replace('hsl(', 'hsla(').replace(')', `, ${alpha})`);
	}
	return color;
}

export function statusDotColor(status: AgentStatus): string {
	switch (status) {
		case 'available': return 'bg-emerald-500';
		case 'missing_key': return 'bg-amber-500';
		case 'not_installed': return 'bg-red-500/60';
		default: return 'bg-zinc-500/60';
	}
}
