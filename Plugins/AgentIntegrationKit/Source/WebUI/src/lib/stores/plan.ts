import { writable, derived, get } from 'svelte/store';
import { onPlanUpdate, type PlanUpdate } from '$lib/bridge.js';
import { currentSessionId } from '$lib/stores/sessions.js';

export const currentPlan = writable<PlanUpdate | null>(null);

export const hasPlan = derived(currentPlan, ($plan) => $plan !== null && $plan.entries.length > 0);

export const planProgress = derived(currentPlan, ($plan) => {
	if (!$plan || $plan.totalCount === 0) return 0;
	return Math.round(($plan.completedCount / $plan.totalCount) * 100);
});

let planBound = false;

/** Wire up plan update callback. Call once on mount. */
export function bindPlanListener(): void {
	if (planBound) return;
	planBound = true;

	onPlanUpdate((sessionId, plan) => {
		const activeSessionId = get(currentSessionId);
		if (sessionId !== activeSessionId) return;
		currentPlan.set(plan);
	});
}

/** Clear plan state (e.g., on session switch) */
export function clearPlan(): void {
	currentPlan.set(null);
}
