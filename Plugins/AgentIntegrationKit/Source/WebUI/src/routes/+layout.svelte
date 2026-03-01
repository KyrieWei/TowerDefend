<script lang="ts">
	import './layout.css';
	import '$lib/polyfills.js';
	import { onMount } from 'svelte';
	import { loadAgents } from '$lib/stores/agents.js';
	import { loadSessions, bindSessionListListener } from '$lib/stores/sessions.js';
	import { bindAgentStateListener } from '$lib/stores/agentState.js';
	import { bindMessageListener } from '$lib/stores/messages.js';
	import { bindPermissionListener } from '$lib/stores/permissions.js';
	import { bindModeListener } from '$lib/stores/modes.js';
	import { bindInstallListeners } from '$lib/stores/setup.js';
	import { bindCommandsListener } from '$lib/stores/commands.js';
	import { bindPlanListener } from '$lib/stores/plan.js';
	import { bindModelsListener } from '$lib/stores/models.js';
	import { bindUsageListener } from '$lib/stores/rateLimits.js';
	import { bindAttachmentsListener } from '$lib/stores/attachments.js';
	import { bindLoginListener } from '$lib/stores/auth.js';
	import { loadSourceControlStatus } from '$lib/stores/sourceControl.js';
	import { isInUnreal, copyToClipboard, getClipboardText, pasteClipboardImage, waitForBridge } from '$lib/bridge.js';
	import * as Tooltip from '$lib/components/ui/tooltip/index.js';

	let { children } = $props();

	onMount(async () => {
		// Wait for UE bridge — CEF starts page load before BindUObject() completes
		await waitForBridge();

		loadAgents();
		loadSessions();
		bindAgentStateListener();
		bindMessageListener();
		bindPermissionListener();
		bindModeListener();
		bindInstallListeners();
		bindCommandsListener();
		bindPlanListener();
		bindModelsListener();
		bindUsageListener();
		bindAttachmentsListener();
		bindLoginListener();
		bindSessionListListener();
		loadSourceControlStatus();
	});

	/**
	 * CEF in off-screen rendering mode doesn't execute default clipboard/selection
	 * actions for keyboard shortcuts. We handle them explicitly via the UE bridge.
	 */
	function handleGlobalKeydown(e: KeyboardEvent) {
		if (!e.metaKey && !e.ctrlKey) return;

		const key = e.key.toLowerCase();
		const el = document.activeElement as HTMLInputElement | HTMLTextAreaElement | null;
		const isInput = el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA');

		if (key === 'a') {
			// Select all
			if (isInput) {
				e.preventDefault();
				el.select();
			}
		} else if (key === 'c') {
			// Copy
			const selection = isInput
				? el.value.substring(el.selectionStart ?? 0, el.selectionEnd ?? 0)
				: window.getSelection()?.toString() ?? '';
			if (selection) {
				e.preventDefault();
				copyToClipboard(selection);
			}
		} else if (key === 'x') {
			// Cut
			if (isInput && el.selectionStart !== el.selectionEnd) {
				const start = el.selectionStart ?? 0;
				const end = el.selectionEnd ?? 0;
				const selection = el.value.substring(start, end);
				e.preventDefault();
				copyToClipboard(selection);
				// Delete the selected text
				el.setRangeText('', start, end, 'end');
				el.dispatchEvent(new Event('input', { bubbles: true }));
			}
		} else if (key === 'v') {
			// Paste
			if (isInput) {
				e.preventDefault();
				getClipboardText().then(async (text) => {
					// Text has priority for normal paste. If clipboard text is empty and the
					// focused field is a textarea, try image paste into chat attachments.
					if (text === '' && el.tagName === 'TEXTAREA') {
						await pasteClipboardImage();
						return;
					}

					if (!text) return;
					const start = el.selectionStart ?? 0;
					const end = el.selectionEnd ?? 0;
					el.setRangeText(text, start, end, 'end');
					el.dispatchEvent(new Event('input', { bubbles: true }));
				});
			}
		}
	}
</script>

<svelte:window onkeydown={handleGlobalKeydown} />

<svelte:head>
	<title>Agent Chat</title>
</svelte:head>

<Tooltip.Provider delayDuration={0} skipDelayDuration={0}>
	<div class="flex h-screen w-screen overflow-hidden">
		{@render children()}
	</div>
</Tooltip.Provider>
