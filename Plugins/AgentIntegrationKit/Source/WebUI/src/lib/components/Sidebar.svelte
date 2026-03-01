<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import Icon from '$lib/components/Icon.svelte';
	import {
		Settings02Icon,
		ArrowDown01Icon,
		MessageMultiple01Icon,
		Delete02Icon,
		ReloadIcon
	} from '@hugeicons/core-free-icons';
	import * as DropdownMenu from '$lib/components/ui/dropdown-menu/index.js';
	import * as ContextMenu from '$lib/components/ui/context-menu/index.js';
	import { agents, selectedAgent, statusDotColor, type Agent } from '$lib/stores/agents.js';
	import {
		sessions,
		currentSessionId,
		isLoadingSessions,
		isConnectingAgents,
		groupedSessions,
		createNewSession,
		selectSession,
		removeSession,
		refreshSessions,
		formatTimeAgo
	} from '$lib/stores/sessions.js';
	import { sessionStates, recentlyFinished, getSessionSidebarStatus } from '$lib/stores/agentState.js';
	import { loadModelsForAgent } from '$lib/stores/models.js';
	import { enterSetup, setupAgent } from '$lib/stores/setup.js';
	import { openSettings } from '$lib/stores/settings.js';
	import { buildContinuationDraft, getContinuationSummarySettings, exportSessionToMarkdown } from '$lib/bridge.js';
	import { queueComposerDraft } from '$lib/stores/composer.js';

	let isRefreshing = $state(false);
	let isContinuing = $state(false);
	let continuationSummaryMode = $state<'compact' | 'detailed'>('compact');
	let continuationProvider = $state<'openrouter' | 'local'>('openrouter');
	let continuationModelId = $state('x-ai/grok-4.1-fast');
	let continuationHasOpenRouterKey = $state(false);
	let continuationStatusMessage = $state('');
	let sessionActionStatusMessage = $state('');
	let sessionActionStatusTone = $state<'success' | 'error'>('success');
	let statusMessageTimeout: ReturnType<typeof setTimeout> | null = null;

	function setSessionActionStatus(message: string, tone: 'success' | 'error'): void {
		sessionActionStatusMessage = message;
		sessionActionStatusTone = tone;
		if (statusMessageTimeout) {
			clearTimeout(statusMessageTimeout);
		}
		statusMessageTimeout = setTimeout(() => {
			sessionActionStatusMessage = '';
			statusMessageTimeout = null;
		}, 5000);
	}

	async function loadContinuationSummarySettings() {
		try {
			const summarySettings = await getContinuationSummarySettings();
			continuationSummaryMode = summarySettings.defaultDetail;
			continuationProvider = summarySettings.provider;
			continuationModelId = summarySettings.modelId;
			continuationHasOpenRouterKey = summarySettings.hasOpenRouterKey;
		} catch (e) {
			console.warn('Failed to load continuation summary settings:', e);
		}
	}

	onMount(async () => {
		await loadContinuationSummarySettings();
	});

	onDestroy(() => {
		if (statusMessageTimeout) {
			clearTimeout(statusMessageTimeout);
		}
	});

	async function handleSelectAgent(agent: Agent) {
		if (agent.status !== 'available') {
			currentSessionId.set(null);
			enterSetup(agent);
		} else {
			setupAgent.set(null);
			selectedAgent.set(agent);
			await createNewSession(agent.name);
			loadModelsForAgent(agent.name);
		}
	}

	async function handleNewChat() {
		if ($selectedAgent) {
			if ($selectedAgent.status !== 'available') {
				currentSessionId.set(null);
				enterSetup($selectedAgent);
			} else {
				setupAgent.set(null);
				await createNewSession($selectedAgent.name);
			}
		}
	}

	function getAgentForSession(agentName: string): Agent | undefined {
		return $agents.find(a => a.name === agentName);
	}

	function getContinuationTargets(sourceAgentName: string): Agent[] {
		return $agents.filter(a => a.status === 'available' && a.name !== sourceAgentName);
	}

	async function handleDelete(sessionId: string) {
		await removeSession(sessionId);
	}

	async function handleExport(sessionId: string) {
		try {
			let result = await exportSessionToMarkdown(sessionId);
			let errorText = (result.error || '').toLowerCase();
			const isLoadStateError =
				errorText.includes('not loaded in memory') || errorText.includes('still loading from acp');

			if (!result.success && !result.canceled && isLoadStateError) {
				if ($currentSessionId !== sessionId) {
					await selectSession(sessionId);
				}

				const deadline = Date.now() + 7000;
				while (!result.success && !result.canceled && Date.now() < deadline) {
					await new Promise((resolve) => setTimeout(resolve, 500));
					result = await exportSessionToMarkdown(sessionId);
					errorText = (result.error || '').toLowerCase();
					if (!errorText.includes('not loaded in memory') && !errorText.includes('still loading from acp')) {
						break;
					}
				}
			}

			if (result.success) {
				const pathLabel = result.savedPath ? ` to ${result.savedPath}` : '';
				setSessionActionStatus(`Exported chat${pathLabel}`, 'success');
				return;
			}
			if (result.canceled) {
				return;
			}
			const errorMessage = result.error || 'Failed to export session.';
			setSessionActionStatus(errorMessage, 'error');
			console.warn('Failed to export session:', errorMessage);
		} catch (e) {
			setSessionActionStatus('Failed to export session.', 'error');
			console.warn('Failed to export session:', e);
		}
	}

	async function handleContinueSession(sessionId: string, targetAgent: Agent) {
		if (isContinuing) return;
		isContinuing = true;
		continuationStatusMessage = '';

		try {
			// Refresh provider/model display before launching the handoff generation.
			await loadContinuationSummarySettings();
			const providerLabel = continuationProvider === 'openrouter'
				? `OpenRouter AI (${continuationModelId})`
				: 'Local fallback summary';
			continuationStatusMessage = `Generating ${continuationSummaryMode} summary via ${providerLabel}...`;

			const draftResult = await buildContinuationDraft(
				sessionId,
				targetAgent.name,
				continuationSummaryMode
			);

			if (!draftResult.success || !draftResult.draftPrompt) {
				continuationStatusMessage = 'Failed to generate summary.';
				console.warn('Failed to build continuation draft:', draftResult.error ?? 'Unknown error');
				return;
			}

			const providerUsed = draftResult.providerUsed === 'openrouter' ? 'OpenRouter AI' : 'local fallback';
			continuationStatusMessage = `Summary ready (${providerUsed}). Creating new ${targetAgent.shortName} chat...`;

			const newSessionId = await createNewSession(targetAgent.name);
			if (!newSessionId) {
				continuationStatusMessage = 'Failed to create target session.';
				console.warn('Failed to create target session for continuation');
				return;
			}

			selectedAgent.set(targetAgent);
			queueComposerDraft(newSessionId, draftResult.draftPrompt);
			loadModelsForAgent(targetAgent.name);
		} catch (e) {
			continuationStatusMessage = 'Unexpected error while continuing chat.';
			console.warn('Failed to continue session:', e);
		} finally {
			isContinuing = false;
		}
	}

	async function handleRefresh() {
		isRefreshing = true;
		await refreshSessions();
		// Keep the spinner for at least 1s so it's visible
		setTimeout(() => { isRefreshing = false; }, 1000);
	}
</script>

<aside class="flex h-full w-[280px] shrink-0 flex-col border-r border-border bg-sidebar">
	<!-- New chat split button -->
	<div class="px-3 pt-2.5 pb-1">
		<div class="flex items-stretch">
			<button
				class="flex flex-1 items-center gap-2 rounded-l-lg border border-border/80 bg-secondary/50 px-3 py-1.5 text-[13px] text-sidebar-foreground transition-colors hover:bg-secondary"
				onclick={handleNewChat}
			>
				{#if $selectedAgent?.icon}
					<span class="flex h-5 w-5 items-center justify-center shrink-0" style="color: {$selectedAgent.color};">
						<Icon icon={$selectedAgent.icon} size={16} strokeWidth={1.5} />
					</span>
				{:else if $selectedAgent}
					<span
						class="flex h-5 w-5 items-center justify-center rounded text-[9px] font-bold text-white"
						style="background-color: {$selectedAgent.color};"
					>
						{$selectedAgent.letter}
					</span>
				{/if}
				<span class="truncate">New {$selectedAgent?.shortName ?? 'Chat'}</span>
			</button>
			<DropdownMenu.Root>
				<DropdownMenu.Trigger
					class="flex items-center rounded-r-lg border border-l-0 border-border/80 bg-secondary/50 px-1.5 transition-colors hover:bg-secondary"
				>
					<Icon icon={ArrowDown01Icon} size={14} strokeWidth={1.5} class="text-muted-foreground" />
				</DropdownMenu.Trigger>
				<DropdownMenu.Content class="w-[248px]" side="bottom" align="start" sideOffset={4}>
					<DropdownMenu.Label class="text-[11px] text-muted-foreground">Start with</DropdownMenu.Label>
					{#each $agents as agent}
						<DropdownMenu.Item
							class="flex items-center gap-2.5 px-2 py-1.5"
							onclick={() => handleSelectAgent(agent)}
						>
							{#if agent.icon}
								<span class="flex h-6 w-6 items-center justify-center shrink-0" style="color: {agent.color};">
									<Icon icon={agent.icon} size={18} strokeWidth={1.5} />
								</span>
							{:else}
								<span
									class="flex h-6 w-6 items-center justify-center rounded text-[9px] font-bold text-white"
									style="background-color: {agent.color};"
								>
									{agent.letter}
								</span>
							{/if}
							<div class="flex-1 min-w-0">
								<div class="flex items-baseline gap-1.5">
									<span class="text-[13px] truncate">{agent.name}</span>
									{#if agent.provider}
										<span class="text-[11px] text-muted-foreground/50 shrink-0">{agent.provider}</span>
									{/if}
								</div>
								{#if agent.status === 'not_installed'}
									<div class="text-[11px] text-amber-400/60 truncate">{agent.statusMessage || 'Click to set up'}</div>
								{:else if agent.status === 'missing_key'}
									<div class="text-[11px] text-amber-400/60 truncate">{agent.statusMessage || 'API key needed'}</div>
								{:else if agent.description}
									<div class="text-[11px] text-muted-foreground/40 truncate">{agent.description}</div>
								{/if}
							</div>
							<span class="h-2 w-2 shrink-0 rounded-full {statusDotColor(agent.status)}"></span>
						</DropdownMenu.Item>
						{#if agent.id === 'OpenRouter'}
							<DropdownMenu.Separator />
						{/if}
					{/each}
				</DropdownMenu.Content>
			</DropdownMenu.Root>
		</div>
	</div>

	<!-- Sessions header -->
	<div class="flex items-center justify-between px-4 pt-3 pb-1">
		<span class="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">Sessions</span>
		<button
			class="flex items-center justify-center h-5 w-5 rounded text-muted-foreground/50 transition-colors hover:text-muted-foreground hover:bg-sidebar-accent/60"
			onclick={handleRefresh}
			title="Refresh session list"
		>
			<Icon
				icon={ReloadIcon}
				size={12}
				strokeWidth={1.5}
				class={isRefreshing || $isConnectingAgents ? 'animate-spin' : ''}
			/>
		</button>
	</div>
	{#if sessionActionStatusMessage}
		<div class="px-4 pb-1 text-[11px] {sessionActionStatusTone === 'success' ? 'text-emerald-400/80' : 'text-red-400/85'}">
			{sessionActionStatusMessage}
		</div>
	{/if}

	<!-- Session list (scrollable, grouped by date) -->
	<div class="min-h-0 flex-1 overflow-y-auto px-2 pb-2">
		{#if $isLoadingSessions}
			<div class="flex flex-col gap-0.5 pt-0.5">
				{#each Array(4) as _}
					<div class="flex items-center gap-2 rounded-md px-2 py-1.5 animate-pulse">
						<div class="h-4 w-4 shrink-0 rounded bg-muted-foreground/10"></div>
						<div class="flex-1 h-3.5 rounded bg-muted-foreground/10"></div>
						<div class="h-3 w-6 shrink-0 rounded bg-muted-foreground/10"></div>
					</div>
				{/each}
			</div>
		{:else if $sessions.length === 0 && $isConnectingAgents}
			<!-- Agents are connecting, sessions haven't arrived yet -->
			<div class="flex flex-col items-center justify-center gap-2 py-8 text-muted-foreground/50">
				<div class="animate-spin">
					<Icon icon={ReloadIcon} size={20} strokeWidth={1.5} />
				</div>
				<span class="text-[12px]">Loading sessions...</span>
			</div>
		{:else if $sessions.length === 0}
			<div class="flex flex-col items-center justify-center gap-3 py-8 text-muted-foreground/50">
				<Icon icon={MessageMultiple01Icon} size={24} strokeWidth={1.5} />
				<span class="text-[12px]">No sessions yet</span>
				<button
					class="text-[11px] text-muted-foreground/60 hover:text-muted-foreground transition-colors underline underline-offset-2"
					onclick={handleRefresh}
				>
					Retry loading
				</button>
			</div>
		{:else}
			{#each $groupedSessions as group}
				<!-- Date group header -->
				<div class="px-2 pt-3 pb-1 first:pt-1">
					<span class="text-[11px] font-medium text-muted-foreground/50">{group.label}</span>
				</div>
				<!-- Sessions in group -->
				<div class="flex flex-col gap-0.5">
					{#each group.sessions as session}
						{@const agent = getAgentForSession(session.agentName)}
						{@const sessionStatus = getSessionSidebarStatus(session.sessionId, $sessionStates, $recentlyFinished)}
						<ContextMenu.Root>
							<ContextMenu.Trigger>
								<button
									class="flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left transition-colors {$currentSessionId === session.sessionId
										? 'bg-sidebar-accent text-sidebar-foreground'
										: 'text-sidebar-foreground/80 hover:bg-sidebar-accent/60'}"
									onclick={() => { setupAgent.set(null); selectSession(session.sessionId); }}
								>
									{#if agent?.icon}
										<span class="flex h-4 w-4 items-center justify-center shrink-0" style="color: {agent.color};">
											<Icon icon={agent.icon} size={14} strokeWidth={1.5} />
										</span>
									{:else}
										<span
											class="flex h-4 w-4 items-center justify-center rounded text-[7px] font-bold text-white shrink-0"
											style="background-color: {agent?.color ?? '#666'};"
										>
											{agent?.letter ?? '?'}
										</span>
									{/if}
									<span class="flex-1 truncate text-[13px]">{session.title || 'New chat'}</span>
									<span class="flex items-center gap-1.5 ml-1 shrink-0">
										{#if sessionStatus === 'working'}
											<span class="h-2 w-2 shrink-0 rounded-full bg-blue-500 animate-pulse" title="Working"></span>
										{:else if sessionStatus === 'finished'}
											<span class="h-2 w-2 shrink-0 rounded-full bg-emerald-500 transition-opacity" title="Finished"></span>
										{/if}
										<span class="text-[11px] text-muted-foreground">{formatTimeAgo(session.lastModifiedAt)}</span>
									</span>
								</button>
							</ContextMenu.Trigger>
							<ContextMenu.Content class="w-[220px]">
								<ContextMenu.Sub>
									<ContextMenu.SubTrigger class="text-[13px]" disabled={sessionStatus === 'working' || isContinuing}>
										Continue In...
									</ContextMenu.SubTrigger>
									<ContextMenu.SubContent class="w-[240px]">
										<ContextMenu.Label class="text-[11px] text-muted-foreground">Target agent</ContextMenu.Label>
										<ContextMenu.Item disabled class="text-[11px] leading-relaxed">
											{#if continuationProvider === 'openrouter'}
												Using: OpenRouter AI ({continuationModelId})
											{:else}
												Using: Local fallback summary
											{/if}
										</ContextMenu.Item>
										{#if continuationProvider === 'openrouter' && !continuationHasOpenRouterKey}
											<ContextMenu.Item disabled class="text-[11px] text-amber-300">
												No OpenRouter key detected. Will fall back to local summary.
											</ContextMenu.Item>
										{/if}
										{#if isContinuing}
											<ContextMenu.Item disabled class="flex items-center gap-2 text-[11px]">
												<Icon icon={ReloadIcon} size={12} strokeWidth={1.5} class="animate-spin" />
												<span class="truncate">{continuationStatusMessage || 'Generating summary...'}</span>
											</ContextMenu.Item>
										{/if}
										<ContextMenu.Separator />
										{@const continuationTargets = getContinuationTargets(session.agentName)}
										{#if continuationTargets.length === 0}
											<ContextMenu.Item disabled class="text-[12px]">No available target agents</ContextMenu.Item>
										{:else}
											{#each continuationTargets as targetAgent}
												<ContextMenu.Item
													class="flex items-center gap-2 text-[13px]"
													disabled={sessionStatus === 'working' || isContinuing}
													closeOnSelect={false}
													onclick={() => handleContinueSession(session.sessionId, targetAgent)}
												>
													{#if targetAgent.icon}
														<span class="flex h-4 w-4 items-center justify-center shrink-0" style="color: {targetAgent.color};">
															<Icon icon={targetAgent.icon} size={13} strokeWidth={1.5} />
														</span>
													{:else}
														<span
															class="flex h-4 w-4 items-center justify-center rounded text-[7px] font-bold text-white shrink-0"
															style="background-color: {targetAgent.color};"
														>
															{targetAgent.letter}
														</span>
													{/if}
													<span class="truncate">{targetAgent.name}</span>
												</ContextMenu.Item>
											{/each}
										{/if}
										<ContextMenu.Separator />
										<ContextMenu.Label class="text-[11px] text-muted-foreground">Summary detail</ContextMenu.Label>
										<ContextMenu.RadioGroup bind:value={continuationSummaryMode}>
											<ContextMenu.RadioItem value="compact" class="text-[12px]" closeOnSelect={false}>Compact</ContextMenu.RadioItem>
											<ContextMenu.RadioItem value="detailed" class="text-[12px]" closeOnSelect={false}>Detailed</ContextMenu.RadioItem>
										</ContextMenu.RadioGroup>
									</ContextMenu.SubContent>
								</ContextMenu.Sub>
									<ContextMenu.Separator />
									<ContextMenu.Item
										class="text-[13px]"
										disabled={sessionStatus === 'working'}
										onclick={() => handleExport(session.sessionId)}
									>
										Export as Markdown
									</ContextMenu.Item>
									<ContextMenu.Separator />
									<ContextMenu.Item
										class="flex items-center gap-2 text-[13px] text-red-400 data-[highlighted]:text-red-400"
										onclick={() => handleDelete(session.sessionId)}
								>
									<Icon icon={Delete02Icon} size={14} strokeWidth={1.5} />
									Delete
								</ContextMenu.Item>
							</ContextMenu.Content>
						</ContextMenu.Root>
					{/each}
				</div>
			{/each}
		{/if}
	</div>

	<!-- Bottom: Settings -->
	<div class="border-t border-border px-3 py-2">
		<button
			class="flex w-full items-center gap-2.5 rounded-md px-2 py-1.5 text-[13px] text-sidebar-foreground transition-colors hover:bg-sidebar-accent"
			onclick={openSettings}
		>
			<Icon icon={Settings02Icon} size={16} strokeWidth={1.5} />
			<span>Settings</span>
		</button>
	</div>
</aside>
