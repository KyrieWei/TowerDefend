<script lang="ts">
	import { tick } from 'svelte';
	import Sidebar from '$lib/components/Sidebar.svelte';
	import ChatMessageComponent from '$lib/components/ChatMessage.svelte';
	import PermissionDialog from '$lib/components/PermissionDialog.svelte';
	import AskUserDialog from '$lib/components/AskUserDialog.svelte';
	import ContextPopup from '$lib/components/ContextPopup.svelte';
	import CommandPopup from '$lib/components/CommandPopup.svelte';
	import PlanPanel from '$lib/components/PlanPanel.svelte';
	import AgentSetup from '$lib/components/AgentSetup.svelte';
	import OnboardingWizard from '$lib/components/OnboardingWizard.svelte';
	import AuthBanner from '$lib/components/AuthBanner.svelte';
	import UsageBar from '$lib/components/UsageBar.svelte';
	import AttachmentChips from '$lib/components/AttachmentChips.svelte';
	import SettingsPanel from '$lib/components/SettingsPanel.svelte';
	import Icon from '$lib/components/Icon.svelte';
	import {
		MoreHorizontalIcon,
		Add01Icon,
		GitBranchIcon,
		SidebarLeftIcon,
		ArrowDown01Icon,
		ChangeScreenModeIcon,
		ArrowUp01Icon,
		StopIcon
	} from '@hugeicons/core-free-icons';
	import * as DropdownMenu from '$lib/components/ui/dropdown-menu/index.js';
	import { agents, selectedAgent, agentsLoaded, statusDotColor, type Agent } from '$lib/stores/agents.js';
	import { showOnboarding, onboardingLoading, checkOnboarding } from '$lib/stores/onboarding.js';
	import { sessions, currentSessionId, createNewSession } from '$lib/stores/sessions.js';
	import {
		models,
		currentModelId,
		isLoadingModels,
		reasoningLevel,
		modelBrowserOpen,
		allModels,
		isLoadingAllModels,
		changeModel,
		changeReasoningLevel,
		openModelBrowser,
		closeModelBrowser,
		loadModelsForAgent,
		loadReasoningLevel,
		reasoningLabels,
		type ReasoningLevel
	} from '$lib/stores/models.js';
	import { currentState, activeSessionId, stateDisplay, currentMcpStatus } from '$lib/stores/agentState.js';
	import {
		messages,
		messagesBySession,
		isStreaming,
		loadMessages,
		sendMessage,
		cancelCurrentPrompt,
		finishStreaming
	} from '$lib/stores/messages.js';
	import { pendingPermission } from '$lib/stores/permissions.js';
	import { availableModes, currentModeId, loadModesForAgent, changeMode, isInPlanMode } from '$lib/stores/modes.js';
	import { sessionUsage, hasUsage, contextPercent, formatTokens, formatCost, resetUsage } from '$lib/stores/usage.js';
	import { setupAgent, enterSetup } from '$lib/stores/setup.js';
	import { pendingComposerDraft, clearComposerDraft } from '$lib/stores/composer.js';
	import { loadAgentUsage } from '$lib/stores/rateLimits.js';
	import { availableCommands } from '$lib/stores/commands.js';
	import { currentPlan, hasPlan, clearPlan } from '$lib/stores/plan.js';
	import { hasAttachments, pasteImage, pickAttachments, addDroppedFile } from '$lib/stores/attachments.js';
	import { resetAuth } from '$lib/stores/auth.js';
	import { settingsOpen } from '$lib/stores/settings.js';
	import { scEnabled, branchName, hasChanges, changesLabel } from '$lib/stores/sourceControl.js';
	import {
		openSourceControlChangelist,
		openSourceControlSubmit,
		type SlashCommand
	} from '$lib/bridge.js';
	import * as Tooltip from '$lib/components/ui/tooltip/index.js';

	let sidebarOpen = $state((() => {
		try { const v = localStorage.getItem('sidebar_open'); return v !== null ? v === 'true' : true; }
		catch { return true; }
	})());
	let sidebarMounted = $state(false);
	$effect(() => { requestAnimationFrame(() => { sidebarMounted = true; }); });
	let inputText = $state('');
	let messageContainer: HTMLDivElement | undefined = $state();
	let textareaEl: HTMLTextAreaElement | undefined = $state();
	let isTextComposing = $state(false);
	let userNearBottom = $state(true);
	// Per-session retained panes: keeps each visited chat UI mounted and hidden when inactive.
	let openedSessionIds = $state<string[]>([]);

	// @ mention popup state
	let contextPopupVisible = $state(false);
	let contextQuery = $state('');
	let contextPopupRef: ContextPopup | undefined = $state();
	let mentionStartPos = $state(-1);

	// / command popup state
	let commandPopupVisible = $state(false);
	let commandQuery = $state('');
	let commandPopupRef: CommandPopup | undefined = $state();
	let commandStartPos = $state(-1);

	// Drag-drop state
	let dragOver = $state(false);
	const COMMON_FILESYSTEM_ROOTS = new Set([
		'Applications',
		'Users',
		'private',
		'tmp',
		'var',
		'etc',
		'System',
		'Volumes',
		'home',
		'opt',
		'usr',
		'bin',
		'sbin',
		'dev'
	]);

	// Check onboarding status once agents are loaded
	$effect(() => {
		if ($agentsLoaded) {
			checkOnboarding();
		}
	});

	// Sync activeSessionId for agent state tracking
	$effect(() => {
		activeSessionId.set($currentSessionId);
	});

	// Track visited sessions so each can keep its own mounted UI pane.
	$effect(() => {
		const sid = $currentSessionId;
		if (!sid || openedSessionIds.includes(sid)) return;
		openedSessionIds = [...openedSessionIds, sid];
	});

	// Load models and modes when session is active (requires both agent + session)
	$effect(() => {
		const agent = $selectedAgent;
		const sid = $currentSessionId;
			if (agent && sid) {
				loadModelsForAgent(agent.name);
				loadReasoningLevel(agent.name);
				loadModesForAgent(agent.name);
			} else {
			// No session — clear agent-specific UI state
			models.set([]);
			currentModelId.set('');
			availableModes.set([]);
			currentModeId.set('');
		}
	});

	// Load rate limit data when selected agent changes
	$effect(() => {
		const agent = $selectedAgent;
		if (agent && agent.status === 'available') {
			loadAgentUsage(agent.name);
		}
	});

	// Load messages when session changes
	$effect(() => {
		const sid = $currentSessionId;
		// Clean up previous session's streaming state before loading new data
		finishStreaming();
		if (sid) {
			// loadMessages will derive the correct isStreaming from loaded data
			loadMessages(sid);
		}
		resetUsage();
		clearPlan();
		resetAuth();
	});

	// Prefill composer for continuation drafts (do not auto-send).
	$effect(() => {
		const sid = $currentSessionId;
		const draft = $pendingComposerDraft;
		if (!sid || !draft || draft.sessionId !== sid) return;

		inputText = draft.text;
		clearComposerDraft();
		tick().then(() => resizeTextarea());
	});

	// Drop panes/cache entries for sessions no longer present.
	$effect(() => {
		const valid = new Set($sessions.map((s) => s.sessionId));
		const nextOpened = openedSessionIds.filter((id) => valid.has(id));
		if (nextOpened.length !== openedSessionIds.length) {
			openedSessionIds = nextOpened;
		}
	});

	// Auto-scroll to bottom on new messages or permission dialogs
	$effect(() => {
		const _msgs = $messages;
		const _perm = $pendingPermission;
		if (userNearBottom && messageContainer) {
			tick().then(() => {
				if (messageContainer) {
					messageContainer.scrollTop = messageContainer.scrollHeight;
				}
			});
		}
	});

	// Current session title for the header
	let currentSession = $derived($sessions.find(s => s.sessionId === $currentSessionId));
	let headerTitle = $derived($currentSessionId ? (currentSession?.title || 'New chat') : 'Agent Chat');
	let headerAgent = $derived($currentSessionId ? (currentSession?.agentName ?? $selectedAgent?.name ?? '') : '');

	// Model picker
	let hasModels = $derived($models.length > 0);
	let currentModel = $derived($models.find(m => m.id === $currentModelId));
	let modelDisplayName = $derived(currentModel?.name ?? $currentModelId ?? 'Model');
	let currentModelSupportsReasoning = $derived(currentModel?.supportsReasoning ?? false);

	// Connection state
	let connectionInfo = $derived($currentState ? stateDisplay[$currentState.state] : null);

	// Mode selector
	let hasModes = $derived($availableModes.length > 0);
	let currentMode = $derived($availableModes.find(m => m.id === $currentModeId));
	let modeDisplayName = $derived(currentMode?.name ?? $currentModeId ?? 'Default');
	let isPlanMode = $derived(isInPlanMode($currentModeId));

	// Usage display
	let usageHasContext = $derived($sessionUsage.contextSize > 0);
	let usageHasTokens = $derived($sessionUsage.inputTokens > 0 || $sessionUsage.outputTokens > 0);
	let usageHasCost = $derived($sessionUsage.costAmount > 0);
	let usageContextLabel = $derived(
		usageHasContext
			? `${$contextPercent}% context`
			: usageHasTokens
				? `${formatTokens($sessionUsage.inputTokens + $sessionUsage.outputTokens)} tokens`
				: ''
	);

	// Input state
	let waitingForMcp = $derived($currentMcpStatus === 'waiting');
	let modelSearchQuery = $state('');
	let modelOptions = $derived($models.filter(m => m.id !== 'special:browse_all'));
	let hasBrowseAllModelsAction = $derived($models.some(m => m.id === 'special:browse_all'));
	let filteredAllModels = $derived.by(() => {
		const q = modelSearchQuery.trim().toLowerCase();
		if (!q) return $allModels;
		return $allModels.filter(model =>
			model.name.toLowerCase().includes(q) ||
			model.id.toLowerCase().includes(q) ||
			(model.description ?? '').toLowerCase().includes(q)
		);
	});
	// Agent is ready when state says so, OR when we're switching sessions on
	// the same agent and models are already loaded (state callback hasn't arrived yet)
	let agentReady = $derived.by(() => {
		if (waitingForMcp) return false;
		const state = $currentState?.state;
		if (state === 'ready' || state === 'in_session' || state === 'prompting') return true;
		// If state is null/undefined (switching sessions), check if models are
		// already loaded for this agent — means the agent is connected, just
		// the per-session state callback hasn't fired yet
		if (!state && $currentSessionId && $models.length > 0) return true;
		return false;
	});
	let canSend = $derived(inputText.trim().length > 0 && !!$currentSessionId && !$isStreaming && agentReady);
	let inputPlaceholder = $derived(
		waitingForMcp
			? 'Connecting to Unreal Editor tools\u2026'
			: !agentReady
				? ($currentState?.state === 'connecting' || $currentState?.state === 'initializing'
					? 'Waiting for agent to connect\u2026'
					: $currentState?.state === 'ready'
						? 'Ready to send a message'
						: $currentState?.state === 'error'
							? 'Agent error \u2014 check connection'
							: 'Waiting for agent\u2026')
				: $messages.length > 0
					? 'Ask for follow-up changes'
					: 'Describe a task or ask a question'
	);

	async function handleBrowseAllModels() {
		if (!$selectedAgent) return;
		modelSearchQuery = '';
		await openModelBrowser($selectedAgent.name);
	}

	async function handleSelectModelFromBrowser(modelId: string) {
		if (!$selectedAgent) return;
		await changeModel($selectedAgent.name, modelId);
		closeModelBrowser();
	}

	const reasoningLevels: ReasoningLevel[] = ['none', 'low', 'medium', 'high', 'max'];

	function toggleSidebar() {
		sidebarOpen = !sidebarOpen;
		try { localStorage.setItem('sidebar_open', String(sidebarOpen)); } catch {}
	}

	async function selectAgent(agent: Agent) {
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
				loadModelsForAgent($selectedAgent.name);
			}
		}
	}

	async function handleSend() {
		if (!canSend || !$currentSessionId) return;
		const text = inputText.trim();
		inputText = '';
		await tick();
		resizeTextarea();
		const bSent = await sendMessage($currentSessionId, text);
		if (!bSent) {
			inputText = text;
			await tick();
			resizeTextarea();
		}
	}

	function handleCancel() {
		if ($currentSessionId) {
			cancelCurrentPrompt($currentSessionId);
		}
	}

	function isImeCompositionEvent(e: KeyboardEvent): boolean {
		// Some embedded webviews report IME keys as "Process" / keyCode 229.
		return isTextComposing || e.isComposing || e.key === 'Process' || e.keyCode === 229;
	}

	function handleKeydown(e: KeyboardEvent) {
		if (isImeCompositionEvent(e)) {
			return;
		}

		// Prevent non-modifier typing keys from bubbling to Unreal editor hotkeys
		// (e.g. Shift+2 => Landscape mode, P => viewport toggles) while typing in chat.
		if (!e.metaKey && !e.ctrlKey && !e.altKey) {
			e.stopPropagation();
		}

		// Let active popups handle navigation keys first
		if (contextPopupVisible && contextPopupRef?.handleKeydown(e)) {
			return;
		}
		if (commandPopupVisible && commandPopupRef?.handleKeydown(e)) {
			return;
		}
		if (e.key === 'Enter' && !e.shiftKey) {
			e.preventDefault();
			handleSend();
		}
	}

	function handleKeyup(e: KeyboardEvent) {
		if (isImeCompositionEvent(e)) {
			return;
		}

		// Match keydown behavior so key-up events don't leak to editor hotkeys.
		if (!e.metaKey && !e.ctrlKey && !e.altKey) {
			e.stopPropagation();
		}
	}

	function handleCompositionStart() {
		isTextComposing = true;
	}

	function handleCompositionEnd() {
		isTextComposing = false;
		detectAtMention();
	}

	function handleInput() {
		resizeTextarea();
		detectAtMention();
	}

	function detectAtMention() {
		if (!textareaEl) return;

		const cursorPos = textareaEl.selectionStart;
		const text = inputText;

		// Detect / command at start of input
		if (text.startsWith('/') && $availableCommands.length > 0) {
			// Only trigger if cursor is still in the first "word" (no spaces yet)
			const firstSpace = text.indexOf(' ');
			if (firstSpace === -1 || cursorPos <= firstSpace) {
				commandStartPos = 0;
				commandQuery = text.slice(1, cursorPos);
				commandPopupVisible = true;
				contextPopupVisible = false;
				return;
			}
		}
		commandPopupVisible = false;
		commandStartPos = -1;

		// Search backwards from cursor for an unmatched @
		let atPos = -1;
		for (let i = cursorPos - 1; i >= 0; i--) {
			const ch = text[i];
			if (ch === '@') {
				// Check if this @ is at start of text or preceded by whitespace
				if (i === 0 || /\s/.test(text[i - 1])) {
					atPos = i;
				}
				break;
			}
			// Stop searching if we hit whitespace (means no @ in this "word")
			if (/\s/.test(ch)) break;
		}

		if (atPos >= 0) {
			mentionStartPos = atPos;
			contextQuery = text.slice(atPos + 1, cursorPos);
			contextPopupVisible = true;
		} else {
			contextPopupVisible = false;
			mentionStartPos = -1;
		}
	}

	function handleContextSelect(item: import('$lib/bridge.js').ContextItem) {
		if (!textareaEl || mentionStartPos < 0) return;

		const cursorPos = textareaEl.selectionStart;
		const before = inputText.slice(0, mentionStartPos);
		const after = inputText.slice(cursorPos);
		const mention = `@${item.path} `;

		inputText = before + mention + after;
		contextPopupVisible = false;
		mentionStartPos = -1;

		// Restore cursor position after the inserted mention
		tick().then(() => {
			if (textareaEl) {
				const newPos = before.length + mention.length;
				textareaEl.selectionStart = newPos;
				textareaEl.selectionEnd = newPos;
				textareaEl.focus();
				resizeTextarea();
			}
		});
	}

	function handleContextDismiss() {
		contextPopupVisible = false;
		mentionStartPos = -1;
	}

	function handleCommandSelect(cmd: SlashCommand) {
		if (!textareaEl) return;

		const replacement = `/${cmd.name}${cmd.inputHint ? ' ' : ''}`;
		inputText = replacement;
		commandPopupVisible = false;
		commandStartPos = -1;

		tick().then(() => {
			if (textareaEl) {
				const pos = replacement.length;
				textareaEl.selectionStart = pos;
				textareaEl.selectionEnd = pos;
				textareaEl.focus();
				resizeTextarea();
			}
		});
	}

	function handleCommandDismiss() {
		commandPopupVisible = false;
		commandStartPos = -1;
	}

	function handleScroll() {
		if (!messageContainer) return;
		const { scrollTop, scrollHeight, clientHeight } = messageContainer;
		userNearBottom = scrollHeight - scrollTop - clientHeight < 80;
	}

	function handlePaste(e: ClipboardEvent) {
		const items = e.clipboardData?.items;
		if (items) {
			for (const item of items) {
				if (item.type.startsWith('image/')) {
					e.preventDefault();
					pasteImage();
					return;
				}
			}
		}
		// Normal text paste — let browser handle it
	}

	function handleDragOver(e: DragEvent) {
		e.preventDefault();
		dragOver = true;
	}

	function handleDragLeave() {
		dragOver = false;
	}

	function handleDrop(e: DragEvent) {
		e.preventDefault();
		dragOver = false;
		const dataTransfer = e.dataTransfer;
		if (!dataTransfer) return;

		const files = dataTransfer.files;
		if (files) {
			for (const file of files) {
				addDroppedFile(file);
			}
		}

		const droppedMentionPaths = extractDroppedMentionPaths(dataTransfer);
		if (droppedMentionPaths.length > 0) {
			insertMentionTags(droppedMentionPaths);
		}
	}

	function resizeTextarea() {
		if (!textareaEl) return;
		textareaEl.style.height = 'auto';
		textareaEl.style.height = Math.min(textareaEl.scrollHeight, 200) + 'px';
	}

	function extractDroppedMentionPaths(dataTransfer: DataTransfer): string[] {
		const found = new Set<string>();

		const addFromText = (text: string) => {
			if (!text) return;
			const matches = text.match(/(?:Source\/[^\s,'"()]+|\/[^\s,'"()]+)/g);
			if (!matches) return;

			for (const match of matches) {
				const normalized = normalizeDroppedMentionPath(match);
				if (normalized) {
					found.add(normalized);
				}
			}
		};

		for (let i = 0; i < dataTransfer.types.length; i++) {
			const type = dataTransfer.types[i];
			const raw = dataTransfer.getData(type);
			addFromText(raw);
		}

		return Array.from(found);
	}

	function normalizeDroppedMentionPath(rawPath: string): string | null {
		let path = rawPath.trim();
		if (!path) return null;

		// Strip wrapping punctuation/quotes often present in asset drag payloads.
		path = path.replace(/^[([{"']+/, '').replace(/[)\]},"';:.]+$/, '');

		const sourceIndex = path.indexOf('Source/');
		if (sourceIndex > 0) {
			path = path.slice(sourceIndex);
		}

		path = path.replace(/\\/g, '/');
		if (path.startsWith('Source/')) {
			return path;
		}

		if (!path.startsWith('/')) {
			return null;
		}

		const firstSegment = path.split('/')[1];
		if (!firstSegment || COMMON_FILESYSTEM_ROOTS.has(firstSegment)) {
			return null;
		}

		return path;
	}

	function insertMentionTags(paths: string[]) {
		if (paths.length === 0) return;

		const mentionText = `${paths.map((path) => `@${path}`).join(' ')} `;
		if (!textareaEl) {
			const needsSpace = inputText.length > 0 && !/\s$/.test(inputText);
			inputText = `${inputText}${needsSpace ? ' ' : ''}${mentionText}`;
			return;
		}

		const start = textareaEl.selectionStart ?? inputText.length;
		const end = textareaEl.selectionEnd ?? start;
		const before = inputText.slice(0, start);
		const after = inputText.slice(end);
		const needsSpace = before.length > 0 && !/\s$/.test(before);
		inputText = `${before}${needsSpace ? ' ' : ''}${mentionText}${after}`;

		tick().then(() => {
			if (!textareaEl) return;
			const cursorPos = before.length + (needsSpace ? 1 : 0) + mentionText.length;
			textareaEl.selectionStart = cursorPos;
			textareaEl.selectionEnd = cursorPos;
			textareaEl.focus();
			resizeTextarea();
		});
	}
</script>

{#if $settingsOpen}
<SettingsPanel />
{:else}
<!-- Sidebar with animated width -->
<div
	class="shrink-0 overflow-hidden {sidebarMounted ? 'transition-all duration-250 ease-out' : ''}"
	style="width: {sidebarOpen ? '280px' : '0px'};"
>
	<div class="h-full w-[280px]">
		<Sidebar />
	</div>
</div>

<!-- Main chat area -->
	<main class="flex min-w-0 flex-1 flex-col">
		<!-- Top header bar — matches UE toolbar style -->
		<header class="flex h-10 shrink-0 items-center justify-between border-b border-border bg-[#222] px-3">
			<div class="flex min-w-0 flex-1 items-center gap-2 text-[13px]">
				<!-- Sidebar toggle -->
				<button
					class="shrink-0 rounded p-1 text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
					onclick={toggleSidebar}
					title={sidebarOpen ? 'Hide sidebar' : 'Show sidebar'}
				>
					<Icon icon={SidebarLeftIcon} size={16} strokeWidth={1.5} />
				</button>
				<div class="min-w-0 flex flex-1 items-center gap-2">
					<span class="min-w-0 flex-1 truncate font-medium text-foreground" title={headerTitle}>
						{headerTitle}
					</span>
					{#if headerAgent}
						<span class="max-w-[120px] truncate text-muted-foreground" title={headerAgent}>
							{headerAgent}
						</span>
					{/if}
				</div>
				<button class="shrink-0 rounded p-0.5 text-muted-foreground hover:text-foreground">
					<Icon icon={MoreHorizontalIcon} size={16} strokeWidth={1.5} />
				</button>
			</div>
			<div class="ml-2 flex shrink-0 items-center gap-2">
				<UsageBar />
				{#if connectionInfo}
					<span
					class="flex items-center gap-1.5 rounded-md border border-border/80 bg-secondary/30 px-2 py-1 text-[11px] text-muted-foreground"
				>
					<span class="h-2 w-2 shrink-0 rounded-full {connectionInfo.dotClass} {connectionInfo.pulse ? 'animate-pulse' : ''}"></span>
					{connectionInfo.label}
				</span>
			{/if}
			<!-- New thread button (visible when sidebar is collapsed) -->
			{#if !sidebarOpen}
				<div class="flex items-stretch">
					<button
						class="flex items-center gap-1.5 rounded-l-md border border-border/80 bg-secondary/50 px-2 py-1 text-[12px] text-sidebar-foreground transition-colors hover:bg-secondary"
						title="New {$selectedAgent?.shortName ?? ''} chat"
						onclick={handleNewChat}
					>
						{#if $selectedAgent?.icon}
							<span class="flex h-4 w-4 items-center justify-center shrink-0" style="color: {$selectedAgent.color};">
								<Icon icon={$selectedAgent.icon} size={14} strokeWidth={1.5} />
							</span>
						{:else if $selectedAgent}
							<span
								class="flex h-4 w-4 items-center justify-center rounded text-[7px] font-bold text-white"
								style="background-color: {$selectedAgent.color};"
							>
								{$selectedAgent.letter}
							</span>
						{/if}
						<span>New</span>
					</button>
					<DropdownMenu.Root>
						<DropdownMenu.Trigger
							class="flex items-center rounded-r-md border border-l-0 border-border/80 bg-secondary/50 px-1 transition-colors hover:bg-secondary"
						>
							<Icon icon={ArrowDown01Icon} size={12} strokeWidth={1.5} class="text-muted-foreground" />
						</DropdownMenu.Trigger>
						<DropdownMenu.Content class="w-[220px]" side="bottom" align="end" sideOffset={4}>
							<DropdownMenu.Label class="text-[11px] text-muted-foreground">Start with</DropdownMenu.Label>
							{#each $agents as agent}
								<DropdownMenu.Item
									class="flex items-center gap-2.5 px-2 py-1.5"
									onclick={() => selectAgent(agent)}
								>
									{#if agent.icon}
										<span class="flex h-5 w-5 items-center justify-center shrink-0" style="color: {agent.color};">
											<Icon icon={agent.icon} size={16} strokeWidth={1.5} />
										</span>
									{:else}
										<span
											class="flex h-5 w-5 items-center justify-center rounded text-[8px] font-bold text-white"
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
											<div class="text-[11px] text-amber-400/60 truncate">Click to set up</div>
										{:else if agent.status === 'missing_key'}
											<div class="text-[11px] text-amber-400/60 truncate">API key needed</div>
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
			{/if}
		</div>
	</header>

	{#if $currentSessionId}
	<!-- Content area with floating input overlay -->
	<div class="relative flex-1 overflow-hidden">
		<!-- Message area (scrollable, extra bottom padding for floating input) -->
			<div
				class="chat-scroll-area h-full overflow-y-auto px-6 pt-6 pb-52"
				bind:this={messageContainer}
				onscroll={handleScroll}
			>
				<AuthBanner />
				{#if openedSessionIds.length === 0}
					{#each $messages as message (message.messageId)}
						<ChatMessageComponent {message} />
					{/each}
				{/if}
					{#each openedSessionIds as paneSessionId (paneSessionId)}
						{@const paneMessages = paneSessionId === $currentSessionId
							? $messages
							: ($messagesBySession[paneSessionId] ?? [])}
					<div class={paneSessionId === $currentSessionId ? 'block' : 'hidden'}>
						{#if paneSessionId === $currentSessionId && paneMessages.length === 0 && !$isStreaming}
							<!-- Empty state for active session -->
							<div class="flex h-full flex-col items-center justify-center gap-3 text-muted-foreground/40">
								{#if $selectedAgent?.icon}
									<span style="color: {$selectedAgent.color}; opacity: 0.3;">
										<Icon icon={$selectedAgent.icon} size={40} strokeWidth={1} />
									</span>
								{/if}
								<span class="text-[14px]">Send a message to get started</span>
							</div>
						{:else}
							{#each paneMessages as message (message.messageId)}
								<ChatMessageComponent {message} />
							{/each}
						{/if}
					</div>
				{/each}
				<!-- TTFT waiting indicator — shows between send and first token -->
				{#if $isStreaming && $messages[$messages.length - 1]?.role !== 'assistant'}
					<div class="mb-4 flex justify-start">
						<div class="flex items-center gap-2 text-[12px]">
							<span class="generating-shimmer">Generating</span>
						</div>
					</div>
				{/if}
			</div>

		<!-- Floating input overlay -->
		<div class="pointer-events-none absolute inset-x-0 bottom-0 z-10 px-6 pb-3 pt-8" style="background: linear-gradient(to bottom, transparent, var(--background) 40%);">
		<div class="pointer-events-auto mx-auto max-w-3xl">
			<!-- Input card (with integrated plan panel) -->
			<!-- svelte-ignore a11y_no_static_element_interactions -->
				<div
					class="relative rounded-2xl border transition-colors focus-within:border-[var(--ue-accent-muted)] {dragOver ? 'border-dashed border-blue-400/60 bg-blue-500/5' : 'border-border bg-card'}"
					style="box-shadow: 0 0 0 1px rgba(0,0,0,0.2), 0 2px 8px rgba(0,0,0,0.15);"
					role="region"
					aria-label="Message input with file drop"
				ondragover={handleDragOver}
				ondragleave={handleDragLeave}
				ondrop={handleDrop}
			>
					<!-- Plan/todo panel (integrated into input card) -->
					{#if $hasPlan}
						<PlanPanel />
					{/if}
					{#if $pendingPermission}
						<div class="p-3">
							{#if $pendingPermission.isAskUserQuestion}
								<AskUserDialog request={$pendingPermission} />
							{:else}
								<PermissionDialog request={$pendingPermission} />
							{/if}
						</div>
					{:else}
						<!-- @ context mention popup -->
						<ContextPopup
							bind:this={contextPopupRef}
							query={contextQuery}
							visible={contextPopupVisible}
							onselect={handleContextSelect}
							ondismiss={handleContextDismiss}
						/>
						<!-- / command popup -->
						<CommandPopup
							bind:this={commandPopupRef}
							query={commandQuery}
							visible={commandPopupVisible}
							commands={$availableCommands}
							onselect={handleCommandSelect}
							ondismiss={handleCommandDismiss}
						/>
						<!-- MCP tools connecting indicator -->
						{#if waitingForMcp}
							<div class="flex items-center gap-2 px-4 pt-3 pb-1">
								<span class="inline-block h-2 w-2 animate-pulse rounded-full bg-amber-500"></span>
								<span class="text-[13px] text-amber-400/80">Connecting to Unreal Editor tools…</span>
							</div>
						{/if}
						<!-- Attachment chips -->
						<AttachmentChips />
							<textarea
								bind:this={textareaEl}
								bind:value={inputText}
							placeholder={inputPlaceholder}
							disabled={!$currentSessionId || !agentReady}
							rows={1}
								class="w-full resize-none bg-transparent px-4 pt-3.5 pb-3 text-[14px] leading-normal text-foreground placeholder:text-muted-foreground/60 focus:outline-none disabled:opacity-50"
								onkeydown={handleKeydown}
								onkeyup={handleKeyup}
								oncompositionstart={handleCompositionStart}
								oncompositionend={handleCompositionEnd}
								oninput={handleInput}
								onpaste={handlePaste}
							></textarea>
						<div class="flex items-center justify-between px-3 pb-2.5">
							<div class="flex items-center gap-1.5">
								<button
									class="rounded-lg p-1.5 text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
									onclick={pickAttachments}
									title="Attach file"
								>
									<Icon icon={Add01Icon} size={18} strokeWidth={1.5} />
								</button>
								<!-- Model picker (when agent has models or loading) -->
								{#if hasModels || $isLoadingModels}
									<DropdownMenu.Root>
										<DropdownMenu.Trigger
											class="flex items-center gap-1 rounded-lg px-2 py-1 text-[12px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
											disabled={$isLoadingModels}
										>
											{#if $isLoadingModels}
												<span class="inline-block h-3 w-3 animate-spin rounded-full border-2 border-muted-foreground/30 border-t-muted-foreground"></span>
												<span class="text-muted-foreground/60">Loading…</span>
											{:else}
												{modelDisplayName}
												<Icon icon={ArrowDown01Icon} size={10} strokeWidth={1.5} class="opacity-50" />
											{/if}
										</DropdownMenu.Trigger>
										<DropdownMenu.Content class="max-h-[300px] w-[280px] overflow-y-auto" side="top" align="start" sideOffset={4}>
											<DropdownMenu.Label class="text-[11px] text-muted-foreground">Model</DropdownMenu.Label>
											{#each modelOptions as model}
												<DropdownMenu.Item
													class="flex items-center gap-2 px-2 py-1.5"
													onclick={() => $selectedAgent && changeModel($selectedAgent.name, model.id)}
												>
													<div class="flex-1 min-w-0">
														<div class="text-[13px] truncate">{model.name}</div>
														{#if model.description}
															<div class="text-[11px] text-muted-foreground/40 truncate">{model.description}</div>
														{/if}
													</div>
													{#if model.id === $currentModelId}
														<span class="h-1.5 w-1.5 shrink-0 rounded-full bg-foreground"></span>
													{/if}
												</DropdownMenu.Item>
											{/each}
											{#if hasBrowseAllModelsAction}
												<DropdownMenu.Separator />
												<DropdownMenu.Item
													class="px-2 py-1.5 text-[12px] text-[var(--ue-accent)]"
													onclick={handleBrowseAllModels}
												>
													Browse all models...
												</DropdownMenu.Item>
											{/if}
										</DropdownMenu.Content>
									</DropdownMenu.Root>
								{/if}
								<!-- Reasoning level picker (only when model supports it and not loading) -->
								{#if hasModels && !$isLoadingModels && currentModelSupportsReasoning}
									<DropdownMenu.Root>
										<DropdownMenu.Trigger
											class="flex items-center gap-1 rounded-lg px-2 py-1 text-[12px] transition-colors {$reasoningLevel !== 'none'
												? 'text-[var(--ue-accent)] hover:text-[var(--ue-accent-hover)]'
												: 'text-muted-foreground hover:bg-accent hover:text-foreground'}"
										>
											{reasoningLabels[$reasoningLevel]}
											<Icon icon={ArrowDown01Icon} size={10} strokeWidth={1.5} class="opacity-50" />
										</DropdownMenu.Trigger>
										<DropdownMenu.Content class="w-[160px]" side="top" align="start" sideOffset={4}>
											<DropdownMenu.Label class="text-[11px] text-muted-foreground">Effort</DropdownMenu.Label>
											{#each reasoningLevels as level}
												<DropdownMenu.Item
													class="flex items-center justify-between px-2 py-1.5"
													onclick={() => changeReasoningLevel(level)}
												>
													<span class="text-[13px]">{reasoningLabels[level]}</span>
													{#if level === $reasoningLevel}
														<span class="h-1.5 w-1.5 shrink-0 rounded-full bg-foreground"></span>
													{/if}
												</DropdownMenu.Item>
											{/each}
										</DropdownMenu.Content>
									</DropdownMenu.Root>
								{/if}
							</div>
							<div class="flex items-center gap-1.5">
								{#if $isStreaming}
									<!-- Cancel button while streaming -->
									<button
										class="flex h-8 w-8 items-center justify-center rounded-full bg-red-500/90 text-white transition-all hover:bg-red-500 hover:scale-105 active:scale-95"
										style="box-shadow: 0 0 10px rgba(220,80,40,0.3);"
										onclick={handleCancel}
										title="Stop generating"
									>
										<Icon icon={StopIcon} size={16} strokeWidth={2.5} />
									</button>
								{:else}
									<!-- Send button -->
									<button
										class="flex h-8 w-8 items-center justify-center rounded-full transition-all {canSend
											? 'text-white hover:scale-105 active:scale-95'
											: 'bg-muted-foreground/20 text-muted-foreground/40 cursor-not-allowed'}"
										style={canSend ? 'background: var(--ue-accent); box-shadow: 0 0 12px rgba(50,130,230,0.35);' : ''}
										onclick={handleSend}
										disabled={!canSend}
									>
										<Icon icon={ArrowUp01Icon} size={18} strokeWidth={2.5} />
									</button>
								{/if}
							</div>
						</div>
					{/if}
				</div>

			<!-- Bottom status bar -->
			<div
				class="mt-2.5 flex items-center justify-between px-1 text-[12px] text-muted-foreground/70"
			>
				<div class="flex items-center gap-4">
					{#if hasModes}
						<DropdownMenu.Root>
							<DropdownMenu.Trigger
								class="flex items-center gap-1.5 transition-colors hover:text-foreground {isPlanMode ? 'text-blue-400' : ''}"
							>
								<Icon icon={ChangeScreenModeIcon} size={14} strokeWidth={1.5} />
								{modeDisplayName}
								<Icon icon={ArrowDown01Icon} size={10} strokeWidth={1.5} class="opacity-50" />
							</DropdownMenu.Trigger>
							<DropdownMenu.Content class="w-[220px]" side="top" align="start" sideOffset={4}>
								<DropdownMenu.Label class="text-[11px] text-muted-foreground">Mode</DropdownMenu.Label>
								{#each $availableModes as mode}
									<DropdownMenu.Item
										class="flex items-center gap-2 px-2 py-1.5"
										onclick={() => $selectedAgent && changeMode($selectedAgent.name, mode.id)}
									>
										<div class="flex-1 min-w-0">
											<div class="text-[13px] truncate">{mode.name}</div>
											{#if mode.description}
												<div class="text-[11px] text-muted-foreground/40 truncate">{mode.description}</div>
											{/if}
										</div>
										{#if mode.id === $currentModeId}
											<span class="h-1.5 w-1.5 shrink-0 rounded-full bg-foreground"></span>
										{/if}
									</DropdownMenu.Item>
								{/each}
							</DropdownMenu.Content>
						</DropdownMenu.Root>
					{/if}
				</div>
				<div class="flex items-center gap-3">
					{#if $hasUsage}
						<Tooltip.Root>
							<Tooltip.Trigger class="flex items-center gap-1.5 transition-colors hover:text-foreground">
								{@const pct = $contextPercent}
								{@const strokeColor = pct < 50 ? '#22c55e' : pct < 80 ? '#eab308' : '#ef4444'}
								{@const circumference = 2 * Math.PI * 7}
								{@const dashOffset = circumference * (1 - pct / 100)}
								<svg width="16" height="16" viewBox="0 0 18 18">
									<circle cx="9" cy="9" r="7" fill="none" stroke="currentColor" stroke-width="2.5" opacity="0.15" />
									<circle cx="9" cy="9" r="7" fill="none" stroke={strokeColor} stroke-width="2.5"
										stroke-dasharray={circumference} stroke-dashoffset={dashOffset}
										stroke-linecap="round" transform="rotate(-90 9 9)" />
								</svg>
								<span class="text-[12px]">{usageContextLabel}</span>
							</Tooltip.Trigger>
							<Tooltip.Content
								side="top"
								sideOffset={6}
								class="w-[260px] p-3 text-[12px] text-foreground"
							>
								<div class="mb-1.5 text-[13px] font-medium">Context window</div>
								<div class="mb-2.5 text-[11px] leading-relaxed text-muted-foreground/50">
									How much of the conversation the AI can "remember" right now. As it fills up, older messages get summarized to make room. This is not your plan limit.
								</div>
								{#if usageHasContext}
									{@const pctVal = $contextPercent}
									{@const remaining = 100 - pctVal}
									<div class="mb-1 text-muted-foreground">{pctVal}% used ({remaining}% left)</div>
									<div class="mb-2 h-1.5 w-full overflow-hidden rounded-full bg-muted-foreground/15">
										<div
											class="h-full rounded-full transition-all duration-300"
											style="width: {pctVal}%; background-color: {pctVal < 50 ? '#22c55e' : pctVal < 80 ? '#eab308' : '#ef4444'};"
										></div>
									</div>
									<div class="text-muted-foreground">
										{formatTokens($sessionUsage.contextUsed)} / {formatTokens($sessionUsage.contextSize)} tokens used
									</div>
								{:else if usageHasTokens}
									<div class="text-muted-foreground">
										{formatTokens($sessionUsage.inputTokens)} input, {formatTokens($sessionUsage.outputTokens)} output
									</div>
								{/if}
								{#if $sessionUsage.cacheReadTokens > 0}
									<div class="mt-1 text-muted-foreground/60">
										{formatTokens($sessionUsage.cacheReadTokens)} cached
									</div>
								{/if}
								{#if usageHasCost}
									<div class="mt-1.5 border-t border-border/40 pt-1.5 text-muted-foreground">
										Session cost: {formatCost($sessionUsage.costAmount, $sessionUsage.costCurrency)}
										{#if $sessionUsage.turnCostUSD > 0}
											<span class="text-muted-foreground/50">(last turn: {formatCost($sessionUsage.turnCostUSD)})</span>
										{/if}
									</div>
								{/if}
								{#if $sessionUsage.numTurns > 0}
									<div class="mt-1 text-muted-foreground/50">
										{$sessionUsage.numTurns} turn{$sessionUsage.numTurns !== 1 ? 's' : ''}
										{#if $sessionUsage.durationMs > 0}
											&middot; {($sessionUsage.durationMs / 1000).toFixed(1)}s
										{/if}
									</div>
								{/if}
							</Tooltip.Content>
						</Tooltip.Root>
					{/if}
					{#if $scEnabled}
						<DropdownMenu.Root>
							<DropdownMenu.Trigger class="flex items-center gap-1.5 transition-colors hover:text-foreground">
								<Icon icon={GitBranchIcon} size={14} strokeWidth={1.5} />
								{$branchName || '...'}
								{#if $hasChanges}
									<span class="rounded-full bg-amber-500/20 px-1.5 text-[10px] font-medium text-amber-400">{$changesLabel}</span>
								{/if}
								<Icon icon={ArrowDown01Icon} size={10} strokeWidth={1.5} class="opacity-50" />
							</DropdownMenu.Trigger>
							<DropdownMenu.Content class="w-[200px]" side="top" align="end" sideOffset={4}>
								<DropdownMenu.Label class="text-[11px] text-muted-foreground">Source Control</DropdownMenu.Label>
								<DropdownMenu.Item
									class="flex items-center gap-2 px-2 py-1.5"
									onclick={() => openSourceControlChangelist()}
								>
									<span class="text-[13px]">View Changelists</span>
								</DropdownMenu.Item>
								<DropdownMenu.Item
									class="flex items-center gap-2 px-2 py-1.5"
									onclick={() => openSourceControlSubmit()}
								>
									<span class="text-[13px]">Submit Changes</span>
								</DropdownMenu.Item>
							</DropdownMenu.Content>
						</DropdownMenu.Root>
					{:else}
						<span class="flex items-center gap-1.5 text-muted-foreground/40">
							<Icon icon={GitBranchIcon} size={14} strokeWidth={1.5} />
							No VCS
						</span>
					{/if}
				</div>
			</div>
		</div>
	</div>
	</div>
	{:else if !$onboardingLoading && $showOnboarding}
	<!-- First-launch onboarding wizard -->
	<OnboardingWizard />

	{:else if $setupAgent}
	<!-- Agent setup flow — agent not installed or missing key -->
	<AgentSetup agent={$setupAgent} />

	{:else}
	<!-- Welcome screen — no active session -->
	<div class="flex flex-1 flex-col items-center justify-center gap-8 px-6">
		<div class="flex flex-col items-center gap-3">
			{#if $selectedAgent?.icon}
				<span style="color: {$selectedAgent.color}; opacity: 0.4;">
					<Icon icon={$selectedAgent.icon} size={48} strokeWidth={1} />
				</span>
			{/if}
			<h2 class="text-lg font-light text-foreground/70">Start a new conversation</h2>
			<p class="text-[13px] text-muted-foreground/50">Choose an agent below to begin</p>
		</div>

		<div class="flex flex-wrap justify-center gap-2.5">
			{#each $agents.filter(a => a.status === 'available') as agent}
				<button
					class="flex items-center gap-2.5 rounded-xl border border-border bg-card/40 px-4 py-2.5 text-[13px] text-foreground/80 transition-all hover:border-[var(--ue-accent-muted)] hover:bg-card/70 active:scale-[0.98]"
					onclick={async () => { selectedAgent.set(agent); await createNewSession(agent.name); }}
				>
					{#if agent.icon}
						<span class="flex h-5 w-5 items-center justify-center" style="color: {agent.color};">
							<Icon icon={agent.icon} size={18} strokeWidth={1.5} />
						</span>
					{:else}
						<span
							class="flex h-5 w-5 items-center justify-center rounded text-[8px] font-bold text-white"
							style="background-color: {agent.color};"
						>
							{agent.letter}
						</span>
					{/if}
					{agent.shortName}
				</button>
			{/each}
		</div>

		{#if $agents.some(a => a.status !== 'available')}
			<div class="flex flex-wrap justify-center gap-2">
				{#each $agents.filter(a => a.status !== 'available') as agent}
					<button
						class="flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-[12px] text-muted-foreground/40 transition-colors hover:bg-card/30 hover:text-muted-foreground/60 cursor-pointer"
						onclick={() => enterSetup(agent)}
					>
						{#if agent.icon}
							<span style="color: {agent.color}; opacity: 0.35;">
								<Icon icon={agent.icon} size={14} strokeWidth={1.5} />
							</span>
						{/if}
						{agent.shortName}
						<span class="text-[10px]">({agent.status === 'not_installed' ? 'set up' : agent.status === 'missing_key' ? 'configure' : 'unavailable'})</span>
					</button>
				{/each}
			</div>
		{/if}
	</div>
	{/if}

	{#if $modelBrowserOpen}
		<div class="fixed inset-0 z-50 flex items-center justify-center p-4">
			<button
				class="absolute inset-0 bg-black/55 backdrop-blur-[1px]"
				onclick={closeModelBrowser}
				aria-label="Close model browser"
			></button>

			<div class="relative z-10 flex h-[min(78vh,720px)] w-full max-w-3xl flex-col overflow-hidden rounded-2xl border border-border bg-card shadow-2xl">
				<div class="flex items-center justify-between border-b border-border px-4 py-3">
					<div>
						<div class="text-[15px] font-medium text-foreground">Browse OpenRouter Models</div>
						<div class="text-[12px] text-muted-foreground">Search and select from the full catalog</div>
					</div>
					<button
						class="rounded-md px-2 py-1 text-[12px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
						onclick={closeModelBrowser}
					>
						Close
					</button>
				</div>

				<div class="border-b border-border px-4 py-3">
					<input
						type="text"
						bind:value={modelSearchQuery}
						placeholder="Search by name or model id (e.g. minimax, m1.5)"
						class="w-full rounded-lg border border-border bg-secondary/40 px-3 py-2 text-[13px] text-foreground placeholder:text-muted-foreground/55 focus:border-[var(--ue-accent-muted)] focus:outline-none"
					/>
				</div>

				<div class="min-h-0 flex-1 overflow-y-auto px-2 py-2">
					{#if $isLoadingAllModels}
						<div class="flex items-center justify-center py-10 text-[13px] text-muted-foreground/70">Loading models...</div>
					{:else if filteredAllModels.length === 0}
						<div class="flex items-center justify-center py-10 text-[13px] text-muted-foreground/70">No models match your search</div>
					{:else}
						{#each filteredAllModels as model}
							<button
								class="flex w-full items-center gap-3 rounded-lg px-3 py-2 text-left transition-colors hover:bg-accent/60"
								onclick={() => handleSelectModelFromBrowser(model.id)}
							>
								<div class="min-w-0 flex-1">
									<div class="truncate text-[13px] text-foreground">{model.name}</div>
									<div class="truncate font-mono text-[11px] text-muted-foreground/70">{model.id}</div>
									{#if model.description}
										<div class="truncate text-[11px] text-muted-foreground/55">{model.description}</div>
									{/if}
								</div>
								{#if model.id === $currentModelId}
									<span class="h-2 w-2 shrink-0 rounded-full bg-foreground"></span>
								{/if}
							</button>
						{/each}
					{/if}
				</div>
			</div>
		</div>
	{/if}
</main>
{/if}
