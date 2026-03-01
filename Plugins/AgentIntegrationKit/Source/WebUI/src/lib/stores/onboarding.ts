import { writable, get } from 'svelte/store';
import { getOnboardingCompleted, setOnboardingCompleted } from '$lib/bridge.js';
import { agents, selectedAgent, type Agent } from '$lib/stores/agents.js';
import { enterSetup } from '$lib/stores/setup.js';
import { createNewSession } from '$lib/stores/sessions.js';

// ── Types ───────────────────────────────────────────────────────────

export type OnboardingStep = 'welcome' | 'subscriptions' | 'recommendation';

export type SubscriptionId =
	| 'claude'
	| 'chatgpt'
	| 'gemini'
	| 'copilot'
	| 'cursor'
	| 'none';

export type SubscriptionOption = {
	id: SubscriptionId;
	label: string;
	sublabel: string;
	agentName: string;
};

// ── Subscription → Agent Mapping ────────────────────────────────────

export const subscriptionOptions: SubscriptionOption[] = [
	{ id: 'claude',  label: 'Claude Pro or Max',   sublabel: 'Uses your Anthropic account (no Betide credits needed)',  agentName: 'Claude Code' },
	{ id: 'chatgpt', label: 'ChatGPT Plus or Pro', sublabel: 'Uses your OpenAI account (no Betide credits needed)',     agentName: 'Codex CLI' },
	{ id: 'gemini',  label: 'Google Account',      sublabel: 'Free — works with any Google account', agentName: 'Gemini CLI' },
	{ id: 'copilot', label: 'GitHub Copilot',      sublabel: 'GitHub subscription',        agentName: 'Copilot CLI' },
	{ id: 'cursor',  label: 'Cursor IDE',          sublabel: 'Cursor subscription',        agentName: 'Cursor Agent' },
	{ id: 'none',    label: 'None of these',       sublabel: "We'll recommend free options", agentName: '' },
];

// Priority: higher index = preferred when multiple subs selected
const agentPriority: string[] = [
	'OpenRouter',
	'Copilot CLI',
	'Cursor Agent',
	'OpenCode',
	'Codex CLI',
	'Gemini CLI',
	'Claude Code',
];

// ── Stores ──────────────────────────────────────────────────────────

export const showOnboarding = writable<boolean>(false);
export const onboardingStep = writable<OnboardingStep>('welcome');
export const selectedSubscriptions = writable<Set<SubscriptionId>>(new Set());
export const recommendedAgentName = writable<string>('');
export const alternativeAgentNames = writable<string[]>([]);
export const onboardingLoading = writable<boolean>(true);

// ── Recommendation Engine ───────────────────────────────────────────

const subscriptionToAgent: Record<SubscriptionId, string> = {
	claude:  'Claude Code',
	chatgpt: 'Codex CLI',
	gemini:  'Gemini CLI',
	copilot: 'Copilot CLI',
	cursor:  'Cursor Agent',
	none:    'OpenRouter',
};

function computeRecommendation(): void {
	const subs = get(selectedSubscriptions);
	const allAgents = get(agents);

	if (subs.size === 0 || subs.has('none')) {
		// No subscriptions: recommend Gemini (free) if installed, else OpenRouter
		const gemini = allAgents.find(a => a.name === 'Gemini CLI');
		if (gemini && gemini.status === 'available') {
			recommendedAgentName.set('Gemini CLI');
			alternativeAgentNames.set(['OpenRouter']);
		} else {
			recommendedAgentName.set('OpenRouter');
			alternativeAgentNames.set(['Gemini CLI']);
		}
		return;
	}

	const candidateNames = [...subs]
		.filter(s => s !== 'none')
		.map(s => subscriptionToAgent[s])
		.filter(Boolean);

	// Prefer already-installed agents, then by priority rank
	const available = candidateNames.filter(name => {
		const a = allAgents.find(ag => ag.name === name);
		return a && a.status === 'available';
	});

	const pool = available.length > 0 ? available : candidateNames;
	const sorted = [...pool].sort((a, b) =>
		agentPriority.indexOf(b) - agentPriority.indexOf(a)
	);

	const primary = sorted[0];
	const alts = candidateNames.filter(n => n !== primary);

	recommendedAgentName.set(primary);
	alternativeAgentNames.set(alts);
}

// ── Navigation ──────────────────────────────────────────────────────

export async function checkOnboarding(): Promise<void> {
	onboardingLoading.set(true);
	try {
		const completed = await getOnboardingCompleted();
		showOnboarding.set(!completed);
	} catch {
		showOnboarding.set(false); // On error, don't block the user
	} finally {
		onboardingLoading.set(false);
	}
}

export function goNext(): void {
	const current = get(onboardingStep);
	if (current === 'welcome') {
		onboardingStep.set('subscriptions');
	} else if (current === 'subscriptions') {
		computeRecommendation();
		onboardingStep.set('recommendation');
	}
}

export function goBack(): void {
	const current = get(onboardingStep);
	if (current === 'subscriptions') {
		onboardingStep.set('welcome');
	} else if (current === 'recommendation') {
		onboardingStep.set('subscriptions');
	}
}

export function toggleSubscription(id: SubscriptionId): void {
	selectedSubscriptions.update(subs => {
		const next = new Set(subs);
		if (id === 'none') {
			// "None" is exclusive — clears everything else
			next.clear();
			next.add('none');
		} else {
			next.delete('none');
			if (next.has(id)) {
				next.delete(id);
			} else {
				next.add(id);
			}
		}
		return next;
	});
}

// ── Completion ──────────────────────────────────────────────────────

export async function completeOnboarding(agentName?: string): Promise<void> {
	await setOnboardingCompleted();

	const name = agentName || get(recommendedAgentName);
	const allAgents = get(agents);
	const agent = allAgents.find(a => a.name === name);

	showOnboarding.set(false);
	onboardingStep.set('welcome');
	selectedSubscriptions.set(new Set());

	if (agent) {
		selectedAgent.set(agent);
		if (agent.status === 'available') {
			await createNewSession(agent.name);
		} else {
			enterSetup(agent);
		}
	}
}

export async function skipOnboarding(): Promise<void> {
	await setOnboardingCompleted();
	showOnboarding.set(false);
	onboardingStep.set('welcome');
	selectedSubscriptions.set(new Set());
}
