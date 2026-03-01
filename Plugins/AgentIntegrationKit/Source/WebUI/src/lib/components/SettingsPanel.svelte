<script lang="ts">
	import Icon from '$lib/components/Icon.svelte';
	import {
		Settings02Icon,
		ArrowLeft02Icon,
		Wrench01Icon,
		UserIcon,
		Notification03Icon,
		InformationCircleIcon,
		Add01Icon,
		Delete02Icon,
		Tick02Icon,
		ArrowDown01Icon,
		ArrowRight01Icon,
		Edit02Icon,
		Cancel01Icon,
		TextIcon,
		Database02Icon
	} from '@hugeicons/core-free-icons';
	import CustomSelect from '$lib/components/ui/custom-select/CustomSelect.svelte';
	import ProjectIndexPanel from '$lib/components/ProjectIndexPanel.svelte';
	import { settingsTab, closeSettings } from '$lib/stores/settings.js';
	import {
		getTools,
		getProfiles,
		getProfileDetail,
		setActiveProfile,
		setToolEnabled,
		setProfileToolEnabled,
		createProfile,
		deleteProfile,
		updateProfile,
		setToolDescriptionOverride,
		getContinuationSummarySettings,
		setContinuationSummaryProvider,
		setContinuationSummaryModel,
		setContinuationSummaryDefaultDetail,
		checkForPluginUpdate,
		type ToolInfo,
		type ProfileInfo,
		type ProfileDetail,
		type ContinuationSummarySettings
	} from '$lib/bridge.js';

	const tabs = [
		{ id: 'general', label: 'General', icon: Settings02Icon },
		// { id: 'indexing', label: 'Project Index', icon: Database02Icon },
		{ id: 'tools', label: 'Tool Profiles', icon: Wrench01Icon },
		{ id: 'agents', label: 'Agents', icon: UserIcon },
		{ id: 'notifications', label: 'Notifications', icon: Notification03Icon },
		{ id: 'about', label: 'About', icon: InformationCircleIcon }
	];

	// ── Tool Profiles State ──────────────────────────────────────────
	let profiles = $state<ProfileInfo[]>([]);
	let activeProfileId = $state('');
	let tools = $state<ToolInfo[]>([]);
	let isLoadingTools = $state(false);

	// Which profile's tools are we viewing? Empty = global (DisabledTools)
	let viewingProfileId = $state('');

	// Profile detail editor
	let profileDetail = $state<ProfileDetail | null>(null);
	let isEditingProfile = $state(false);
	let editName = $state('');
	let editDescription = $state('');
	let editInstructions = $state('');
	let showProfileEditor = $state(false);

	// Tool description override editing
	let editingToolOverride = $state(''); // tool name being edited
	let overrideText = $state('');

	// Grouped tools by category
	let toolsByCategory = $derived(
		tools.reduce<Record<string, ToolInfo[]>>((acc, tool) => {
			if (!acc[tool.category]) acc[tool.category] = [];
			acc[tool.category].push(tool);
			return acc;
		}, {})
	);
	let categoryNames = $derived(Object.keys(toolsByCategory).sort());

	// Collapsed categories
	let collapsedCategories = $state<Set<string>>(new Set());

	// New profile input
	let showNewProfile = $state(false);
	let newProfileName = $state('');
	let newProfileInputEl = $state<HTMLInputElement>();

	// Stats
	let enabledCount = $derived(tools.filter(t => t.enabled).length);
	let totalCount = $derived(tools.length);

	// Is the current viewing profile a custom (editable) one?
	let isCustomProfile = $derived(() => {
		if (!viewingProfileId) return false;
		const p = profiles.find(p => p.profileId === viewingProfileId);
		return p ? !p.isBuiltIn : false;
	});

	// Search / filter
	let searchQuery = $state('');
	let filteredTools = $derived(
		searchQuery.trim()
			? tools.filter(t =>
				t.displayName.toLowerCase().includes(searchQuery.toLowerCase()) ||
				t.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
				t.description.toLowerCase().includes(searchQuery.toLowerCase())
			)
			: tools
	);
	let filteredToolsByCategory = $derived(
		filteredTools.reduce<Record<string, ToolInfo[]>>((acc, tool) => {
			if (!acc[tool.category]) acc[tool.category] = [];
			acc[tool.category].push(tool);
			return acc;
		}, {})
	);
	let filteredCategoryNames = $derived(Object.keys(filteredToolsByCategory).sort());

	// ── General Settings: Chat Handoff Summary ──────────────────────
	let continuationSummarySettings = $state<ContinuationSummarySettings>({
		provider: 'openrouter',
		modelId: 'x-ai/grok-4.1-fast',
		defaultDetail: 'compact',
		hasOpenRouterKey: false
	});
	let isLoadingGeneralSettings = $state(false);
	let generalSettingsError = $state('');
	let isCheckingForUpdates = $state(false);
	let updateCheckMessage = $state('');

	// ── Indexing Panel ref ───────────────────────────────────────────
	let indexPanel: ProjectIndexPanel | undefined = $state();

	$effect(() => {
		const tab = $settingsTab;
		queueMicrotask(() => {
			if (tab === 'tools') {
				void loadProfilesAndTools();
			} else if (tab === 'general') {
				void loadGeneralSettings();
			} else if (tab === 'indexing') {
				void indexPanel?.load();
			}
		});
	});

	async function loadProfilesAndTools() {
		isLoadingTools = true;
		try {
			const profileState = await getProfiles();
			profiles = profileState.profiles;
			activeProfileId = profileState.activeProfileId;

			viewingProfileId = activeProfileId;
			tools = await getTools(viewingProfileId);
			tools.sort((a, b) => a.category.localeCompare(b.category) || a.displayName.localeCompare(b.displayName));

			// Load detail if viewing a profile
			if (viewingProfileId) {
				await loadProfileDetail(viewingProfileId);
			} else {
				profileDetail = null;
				showProfileEditor = false;
			}
		} catch (e) {
			console.warn('Failed to load tools/profiles:', e);
		} finally {
			isLoadingTools = false;
		}
	}

	async function loadGeneralSettings() {
		if (isLoadingGeneralSettings) return;
		isLoadingGeneralSettings = true;
		generalSettingsError = '';
		try {
			continuationSummarySettings = await getContinuationSummarySettings();
		} catch (e) {
			console.warn('Failed to load continuation summary settings:', e);
			generalSettingsError = 'Failed to load summary settings.';
		} finally {
			isLoadingGeneralSettings = false;
		}
	}

	async function handleSummaryProviderChange(provider: 'openrouter' | 'local') {
		continuationSummarySettings = { ...continuationSummarySettings, provider };
		try {
			await setContinuationSummaryProvider(provider);
		} catch (e) {
			console.warn('Failed to save summary provider:', e);
		}
	}

	async function handleSummaryModelChange(modelId: string) {
		continuationSummarySettings = { ...continuationSummarySettings, modelId };
		try {
			await setContinuationSummaryModel(modelId);
		} catch (e) {
			console.warn('Failed to save summary model:', e);
		}
	}

	async function handleSummaryDetailChange(detail: 'compact' | 'detailed') {
		continuationSummarySettings = { ...continuationSummarySettings, defaultDetail: detail };
		try {
			await setContinuationSummaryDefaultDetail(detail);
		} catch (e) {
			console.warn('Failed to save summary detail:', e);
		}
	}

	async function handleCheckForUpdates() {
		if (isCheckingForUpdates) return;
		isCheckingForUpdates = true;
		updateCheckMessage = '';
		try {
			await checkForPluginUpdate();
			updateCheckMessage = 'Update check started. If an update is available, Unreal will show a notification.';
		} catch (e) {
			console.warn('Failed to trigger update check:', e);
			updateCheckMessage = 'Failed to start update check.';
		} finally {
			isCheckingForUpdates = false;
		}
	}

	async function loadProfileDetail(profileId: string) {
		if (!profileId) {
			profileDetail = null;
			return;
		}
		const detail = await getProfileDetail(profileId);
		if (detail.found) {
			profileDetail = detail;
			editName = detail.displayName;
			editDescription = detail.description;
			editInstructions = detail.customInstructions;
		} else {
			profileDetail = null;
		}
	}

	async function handleActivateProfile(profileId: string) {
		const newId = profileId === activeProfileId ? '' : profileId;
		activeProfileId = newId;
		viewingProfileId = newId;
		await setActiveProfile(newId);
		tools = await getTools(newId);
		tools.sort((a, b) => a.category.localeCompare(b.category) || a.displayName.localeCompare(b.displayName));

		if (newId) {
			await loadProfileDetail(newId);
		} else {
			profileDetail = null;
			showProfileEditor = false;
		}
	}

	async function handleToggleTool(toolName: string, enabled: boolean) {
		tools = tools.map(t => t.name === toolName ? { ...t, enabled } : t);
		if (viewingProfileId) {
			await setProfileToolEnabled(viewingProfileId, toolName, enabled);
		} else {
			await setToolEnabled(toolName, enabled);
		}
	}

	async function handleEnableAll() {
		tools = tools.map(t => ({ ...t, enabled: true }));
		for (const tool of tools) {
			if (viewingProfileId) {
				await setProfileToolEnabled(viewingProfileId, tool.name, true);
			} else {
				await setToolEnabled(tool.name, true);
			}
		}
	}

	async function handleDisableAll() {
		tools = tools.map(t => ({ ...t, enabled: false }));
		for (const tool of tools) {
			if (viewingProfileId) {
				await setProfileToolEnabled(viewingProfileId, tool.name, false);
			} else {
				await setToolEnabled(tool.name, false);
			}
		}
	}

	async function handleCreateProfile() {
		if (!newProfileName.trim()) return;
		const id = await createProfile(newProfileName.trim(), '');
		if (id) {
			showNewProfile = false;
			newProfileName = '';
			// Activate the newly created profile
			activeProfileId = id;
			viewingProfileId = id;
			await setActiveProfile(id);
			await loadProfilesAndTools();
			showProfileEditor = true;
		}
	}

	async function handleDeleteProfile(profileId: string) {
		const ok = await deleteProfile(profileId);
		if (ok) {
			showProfileEditor = false;
			profileDetail = null;
			await loadProfilesAndTools();
		}
	}

	async function handleSaveProfile() {
		if (!viewingProfileId || !profileDetail) return;
		const ok = await updateProfile(viewingProfileId, editName, editDescription, editInstructions);
		if (ok) {
			// Reload profiles to reflect name changes
			const profileState = await getProfiles();
			profiles = profileState.profiles;
			profileDetail = { ...profileDetail, displayName: editName, description: editDescription, customInstructions: editInstructions };
		}
	}

	async function handleSaveToolOverride(toolName: string) {
		if (!viewingProfileId) return;
		await setToolDescriptionOverride(viewingProfileId, toolName, overrideText.trim());
		// Update local state
		tools = tools.map(t => t.name === toolName ? { ...t, descriptionOverride: overrideText.trim() } : t);
		editingToolOverride = '';
		overrideText = '';
	}

	function startEditingOverride(tool: ToolInfo) {
		editingToolOverride = tool.name;
		overrideText = tool.descriptionOverride || tool.description;
	}

	function cancelEditingOverride() {
		editingToolOverride = '';
		overrideText = '';
	}

	async function clearToolOverride(toolName: string) {
		if (!viewingProfileId) return;
		await setToolDescriptionOverride(viewingProfileId, toolName, '');
		tools = tools.map(t => t.name === toolName ? { ...t, descriptionOverride: '' } : t);
	}

	function toggleCategory(cat: string) {
		const next = new Set(collapsedCategories);
		if (next.has(cat)) next.delete(cat);
		else next.add(cat);
		collapsedCategories = next;
	}

	// Debounce helper for auto-save
	let saveTimeout: ReturnType<typeof setTimeout> | undefined;
	function debouncedSaveProfile() {
		clearTimeout(saveTimeout);
		saveTimeout = setTimeout(() => handleSaveProfile(), 600);
	}
</script>

<div class="flex h-full w-full">
	<!-- Left tab nav -->
	<nav class="flex w-[200px] shrink-0 flex-col border-r border-border bg-sidebar">
		<div class="flex items-center gap-2 px-4 pt-4 pb-3">
			<button
				class="rounded p-1 text-muted-foreground transition-colors hover:bg-sidebar-accent hover:text-foreground"
				onclick={closeSettings}
				title="Back to chat"
			>
				<Icon icon={ArrowLeft02Icon} size={16} strokeWidth={1.5} />
			</button>
			<span class="text-[14px] font-medium text-foreground">Settings</span>
		</div>

		<div class="flex flex-col gap-0.5 px-2">
			{#each tabs as tab}
				<button
					class="flex items-center gap-2.5 rounded-md px-3 py-2 text-[13px] transition-colors {$settingsTab === tab.id
						? 'bg-sidebar-accent text-foreground'
						: 'text-muted-foreground hover:bg-sidebar-accent/60 hover:text-foreground'}"
					onclick={() => settingsTab.set(tab.id)}
				>
					<Icon icon={tab.icon} size={15} strokeWidth={1.5} />
					{tab.label}
				</button>
			{/each}
		</div>
	</nav>

	<!-- Right content area -->
	<div class="flex-1 overflow-y-auto p-8">
		<div class="mx-auto max-w-3xl">
			{#if $settingsTab === 'tools'}
				<!-- ── Tool Profiles Tab ──────────────────────────── -->
				<div class="mb-6">
					<h2 class="mb-1 text-[18px] font-medium text-foreground">Tool Profiles</h2>
					<p class="text-[13px] text-muted-foreground/60">Create profiles to control which tools agents can use, customize tool descriptions, and add custom system instructions.</p>
				</div>

				{#if isLoadingTools}
					<div class="flex items-center gap-2 py-8 text-muted-foreground/50">
						<span class="inline-block h-4 w-4 animate-spin rounded-full border-2 border-muted-foreground/30 border-t-muted-foreground"></span>
						Loading tools...
					</div>
				{:else}
					<!-- Profile cards -->
					<div class="mb-6 flex flex-wrap gap-2">
						<!-- "All Tools" card (no profile) -->
						<button
							class="flex items-center gap-2 rounded-lg border px-3 py-2 text-[13px] transition-colors {activeProfileId === ''
								? 'border-foreground/30 bg-foreground/5 text-foreground'
								: 'border-border/60 text-muted-foreground hover:border-border hover:text-foreground'}"
							onclick={() => handleActivateProfile('')}
						>
							{#if activeProfileId === ''}
								<Icon icon={Tick02Icon} size={14} strokeWidth={2} class="text-emerald-500" />
							{/if}
							All Tools
						</button>
						{#each profiles as profile}
							<button
								class="group flex items-center gap-2 rounded-lg border px-3 py-2 text-[13px] transition-colors {profile.isActive
									? 'border-foreground/30 bg-foreground/5 text-foreground'
									: 'border-border/60 text-muted-foreground hover:border-border hover:text-foreground'}"
								onclick={() => handleActivateProfile(profile.profileId)}
							>
								{#if profile.isActive}
									<Icon icon={Tick02Icon} size={14} strokeWidth={2} class="text-emerald-500" />
								{/if}
								{profile.displayName}
								{#if profile.enabledToolCount > 0}
									<span class="text-[11px] text-muted-foreground/40">{profile.enabledToolCount}</span>
								{/if}
								{#if !profile.isBuiltIn}
									<!-- svelte-ignore a11y_no_static_element_interactions -->
									<span
										role="button"
										tabindex="-1"
										class="ml-0.5 rounded p-0.5 text-muted-foreground/30 opacity-0 transition-all hover:text-red-400 group-hover:opacity-100"
										onclick={(e) => { e.stopPropagation(); handleDeleteProfile(profile.profileId); }}
										onkeydown={(e) => { if (e.key === 'Enter') { e.stopPropagation(); handleDeleteProfile(profile.profileId); } }}
										title="Delete profile"
									>
										<Icon icon={Delete02Icon} size={12} strokeWidth={1.5} />
									</span>
								{/if}
							</button>
						{/each}
						<!-- New profile button -->
						{#if showNewProfile}
							<div class="flex items-center gap-1">
								<input
									bind:this={newProfileInputEl}
									bind:value={newProfileName}
									class="h-[36px] w-[140px] rounded-lg border border-border bg-transparent px-2.5 text-[13px] text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
									placeholder="Profile name"
									onkeydown={(e) => { if (e.key === 'Enter') handleCreateProfile(); if (e.key === 'Escape') { showNewProfile = false; newProfileName = ''; } }}
								/>
								<button
									class="rounded-md px-2 py-1.5 text-[12px] text-foreground transition-colors hover:bg-accent"
									onclick={handleCreateProfile}
								>Save</button>
							</div>
						{:else}
							<button
								class="flex items-center gap-1.5 rounded-lg border border-dashed border-border/60 px-3 py-2 text-[13px] text-muted-foreground/50 transition-colors hover:border-border hover:text-muted-foreground"
								onclick={() => { showNewProfile = true; requestAnimationFrame(() => newProfileInputEl?.focus()); }}
							>
								<Icon icon={Add01Icon} size={14} strokeWidth={1.5} />
								New Profile
							</button>
						{/if}
					</div>

					<!-- Profile editor (only for custom profiles) -->
					{#if profileDetail && !profileDetail.isBuiltIn && viewingProfileId}
						<div class="mb-6 rounded-lg border border-border/60 bg-card">
							<button
								class="flex w-full items-center justify-between px-4 py-3 text-left"
								onclick={() => showProfileEditor = !showProfileEditor}
							>
								<div class="flex items-center gap-2">
									<Icon icon={Edit02Icon} size={14} strokeWidth={1.5} class="text-muted-foreground" />
									<span class="text-[13px] font-medium text-foreground">Profile Settings</span>
									{#if profileDetail.customInstructions || Object.keys(profileDetail.toolDescriptionOverrides).length > 0}
										<span class="rounded-full bg-foreground/10 px-1.5 py-0.5 text-[10px] text-muted-foreground">customized</span>
									{/if}
								</div>
								<Icon icon={showProfileEditor ? ArrowDown01Icon : ArrowRight01Icon} size={14} strokeWidth={1.5} class="text-muted-foreground/50" />
							</button>

							{#if showProfileEditor}
								<div class="border-t border-border/40 px-4 py-4">
									<div class="flex flex-col gap-4">
										<!-- Profile name -->
										<div>
											<span class="mb-1.5 block text-[12px] font-medium text-muted-foreground">Profile Name</span>
											<input
												type="text"
												bind:value={editName}
												oninput={debouncedSaveProfile}
												class="w-full rounded-md border border-border/60 bg-transparent px-3 py-2 text-[13px] text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
												placeholder="Profile name"
											/>
										</div>

										<!-- Description -->
										<div>
											<span class="mb-1.5 block text-[12px] font-medium text-muted-foreground">Description</span>
											<input
												type="text"
												bind:value={editDescription}
												oninput={debouncedSaveProfile}
												class="w-full rounded-md border border-border/60 bg-transparent px-3 py-2 text-[13px] text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
												placeholder="Brief description of this profile's purpose"
											/>
										</div>

										<!-- Custom Instructions -->
										<div>
											<span class="mb-1.5 block text-[12px] font-medium text-muted-foreground">Custom System Instructions</span>
											<p class="mb-2 text-[11px] text-muted-foreground/50">These instructions are prepended to the agent's system prompt when this profile is active. Use this to guide agent behavior, set constraints, or provide project-specific context.</p>
											<textarea
												bind:value={editInstructions}
												oninput={debouncedSaveProfile}
												class="w-full resize-y rounded-md border border-border/60 bg-transparent px-3 py-2 text-[13px] leading-relaxed text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
												placeholder="e.g., Always explain changes before making them. Focus on Blueprint-based solutions. Do not modify C++ code."
												rows={4}
											></textarea>
										</div>

										<!-- Overrides summary -->
										{#if tools.some(t => t.descriptionOverride)}
											{@const overrideCount = tools.filter(t => t.descriptionOverride).length}
											<div class="flex items-center gap-2 text-[12px] text-muted-foreground/50">
												<Icon icon={TextIcon} size={13} strokeWidth={1.5} />
												{overrideCount} tool description {overrideCount === 1 ? 'override' : 'overrides'} active
											</div>
										{/if}
									</div>
								</div>
							{/if}
						</div>
					{/if}

					<!-- Viewing indicator + bulk actions + search -->
					<div class="mb-4 flex flex-col gap-3">
						<div class="flex items-center justify-between">
							<div class="flex items-center gap-3">
								<span class="text-[13px] text-muted-foreground">
									{#if viewingProfileId}
										{@const vp = profiles.find(p => p.profileId === viewingProfileId)}
										Showing tools for <span class="font-medium text-foreground">{vp?.displayName ?? 'Profile'}</span>
									{:else}
										Showing <span class="font-medium text-foreground">global</span> tool settings
									{/if}
								</span>
								<span class="text-[12px] text-muted-foreground/40">{enabledCount}/{totalCount} enabled</span>
							</div>
							<div class="flex items-center gap-1.5">
								<button
									class="rounded-md px-2 py-1 text-[12px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
									onclick={handleEnableAll}
								>Enable all</button>
								<button
									class="rounded-md px-2 py-1 text-[12px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
									onclick={handleDisableAll}
								>Disable all</button>
							</div>
						</div>
						<!-- Search bar -->
						<input
							type="text"
							bind:value={searchQuery}
							class="w-full rounded-md border border-border/40 bg-transparent px-3 py-1.5 text-[13px] text-foreground placeholder:text-muted-foreground/30 focus:border-foreground/20 focus:outline-none"
							placeholder="Search tools..."
						/>
					</div>

					<!-- Tools by category -->
					<div class="flex flex-col gap-1">
						{#each filteredCategoryNames as category}
							{@const catTools = filteredToolsByCategory[category]}
							{@const catEnabled = catTools.filter(t => t.enabled).length}
							{@const isCollapsed = collapsedCategories.has(category)}
							<!-- Category header -->
							<button
								class="flex items-center gap-2 rounded-md px-2 py-1.5 text-[12px] font-medium uppercase tracking-wider text-muted-foreground/60 transition-colors hover:bg-accent/40"
								onclick={() => toggleCategory(category)}
							>
								<Icon icon={isCollapsed ? ArrowRight01Icon : ArrowDown01Icon} size={12} strokeWidth={1.5} />
								{category}
								<span class="font-normal normal-case tracking-normal text-muted-foreground/30">{catEnabled}/{catTools.length}</span>
							</button>
							{#if !isCollapsed}
								<div class="mb-2 flex flex-col">
									{#each catTools as tool}
										<div class="group rounded-md px-3 py-2 transition-colors hover:bg-accent/30">
											<div class="flex items-start gap-3">
												<input
													type="checkbox"
													checked={tool.enabled}
													onchange={() => handleToggleTool(tool.name, !tool.enabled)}
													class="mt-0.5 h-4 w-4 shrink-0 cursor-pointer rounded border-border accent-foreground"
												/>
												<div class="min-w-0 flex-1">
													<div class="flex items-baseline gap-2">
														<span class="text-[13px] font-medium text-foreground">{tool.displayName}</span>
														<span class="text-[11px] text-muted-foreground/40">{tool.name}</span>
														{#if tool.descriptionOverride}
															<span class="rounded bg-blue-500/10 px-1 py-0.5 text-[10px] text-blue-400">overridden</span>
														{/if}
													</div>
													<!-- Show override if present, else default description -->
													{#if tool.descriptionOverride}
														<p class="mt-0.5 text-[12px] leading-relaxed text-blue-300/70">{tool.descriptionOverride}</p>
														<p class="mt-0.5 text-[11px] leading-relaxed text-muted-foreground/30 line-through">{tool.description}</p>
													{:else}
														<p class="mt-0.5 text-[12px] leading-relaxed text-muted-foreground/60">{tool.description}</p>
													{/if}

													<!-- Description override editor (only for custom profiles) -->
													{#if viewingProfileId && profileDetail && !profileDetail.isBuiltIn}
														{#if editingToolOverride === tool.name}
															<div class="mt-2 flex flex-col gap-1.5">
																<textarea
																	bind:value={overrideText}
																	class="w-full resize-y rounded border border-border/60 bg-transparent px-2.5 py-1.5 text-[12px] leading-relaxed text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
																	placeholder="Custom description for this tool (leave empty to use default)"
																	rows={2}
																	onkeydown={(e) => { if (e.key === 'Escape') cancelEditingOverride(); }}
																></textarea>
																<div class="flex gap-1.5">
																	<button
																		class="rounded px-2 py-1 text-[11px] font-medium text-foreground transition-colors hover:bg-accent"
																		onclick={() => handleSaveToolOverride(tool.name)}
																	>Save</button>
																	<button
																		class="rounded px-2 py-1 text-[11px] text-muted-foreground transition-colors hover:bg-accent"
																		onclick={cancelEditingOverride}
																	>Cancel</button>
																</div>
															</div>
														{:else}
															<div class="mt-1 flex items-center gap-1.5 opacity-0 transition-opacity group-hover:opacity-100">
																<button
																	class="flex items-center gap-1 rounded px-1.5 py-0.5 text-[11px] text-muted-foreground/50 transition-colors hover:bg-accent hover:text-foreground"
																	onclick={() => startEditingOverride(tool)}
																>
																	<Icon icon={Edit02Icon} size={11} strokeWidth={1.5} />
																	{tool.descriptionOverride ? 'Edit override' : 'Override description'}
																</button>
																{#if tool.descriptionOverride}
																	<button
																		class="flex items-center gap-1 rounded px-1.5 py-0.5 text-[11px] text-muted-foreground/50 transition-colors hover:bg-red-500/10 hover:text-red-400"
																		onclick={() => clearToolOverride(tool.name)}
																	>
																		<Icon icon={Cancel01Icon} size={11} strokeWidth={1.5} />
																		Clear
																	</button>
																{/if}
															</div>
														{/if}
													{/if}
												</div>
											</div>
										</div>
									{/each}
								</div>
							{/if}
						{/each}
					</div>
				{/if}
			{:else if $settingsTab === 'general'}
				<div class="mb-6">
					<h2 class="mb-1 text-[18px] font-medium text-foreground">General</h2>
					<p class="text-[13px] text-muted-foreground/60">Configure how chat handoff summaries are generated when continuing a conversation in another agent.</p>
				</div>

				{#if isLoadingGeneralSettings}
					<div class="flex items-center gap-2 py-8 text-muted-foreground/50">
						<span class="inline-block h-4 w-4 animate-spin rounded-full border-2 border-muted-foreground/30 border-t-muted-foreground"></span>
						Loading summary settings...
					</div>
				{:else}
					<div class="rounded-lg border border-border/60 bg-card p-4">
						<div class="mb-4">
							<h3 class="text-[14px] font-medium text-foreground">Chat Handoff Summary</h3>
							<p class="mt-1 text-[12px] text-muted-foreground/60">Used by the session context menu action <span class="text-foreground/80">Continue In...</span>.</p>
						</div>

						<div class="grid gap-4">
							<div>
								<label for="continuation-provider" class="mb-1.5 block text-[12px] font-medium text-muted-foreground">Provider</label>
								<CustomSelect
									id="continuation-provider"
									value={continuationSummarySettings.provider}
									options={[
										{ value: 'openrouter', label: 'OpenRouter (AI summary)' },
										{ value: 'local', label: 'Local fallback (deterministic)' }
									]}
									onchange={(v) => handleSummaryProviderChange(v as 'openrouter' | 'local')}
								/>
								<p class="mt-1 text-[11px] text-muted-foreground/50">
									OpenRouter summarizes the full transcript using your selected model. Local fallback is faster but less accurate.
								</p>
							</div>

							<div>
								<label for="continuation-model" class="mb-1.5 block text-[12px] font-medium text-muted-foreground">OpenRouter Model</label>
								<input
									id="continuation-model"
									type="text"
									value={continuationSummarySettings.modelId}
									onchange={(e) => handleSummaryModelChange((e.currentTarget as HTMLInputElement).value)}
									class="w-full rounded-md border border-border/60 bg-transparent px-3 py-2 text-[13px] text-foreground placeholder:text-muted-foreground/40 focus:border-foreground/30 focus:outline-none"
									placeholder="x-ai/grok-4.1-fast"
								/>
								<p class="mt-1 text-[11px] text-muted-foreground/50">
									Use a large-context model for better handoff quality.
								</p>
							</div>

							<div>
								<label for="continuation-detail" class="mb-1.5 block text-[12px] font-medium text-muted-foreground">Default Detail</label>
								<CustomSelect
									id="continuation-detail"
									value={continuationSummarySettings.defaultDetail}
									options={[
										{ value: 'compact', label: 'Compact' },
										{ value: 'detailed', label: 'Detailed' }
									]}
									onchange={(v) => handleSummaryDetailChange(v as 'compact' | 'detailed')}
								/>
							</div>
						</div>

						{#if continuationSummarySettings.provider === 'openrouter' && !continuationSummarySettings.hasOpenRouterKey}
							<div class="mt-4 rounded-md border border-amber-500/30 bg-amber-500/10 px-3 py-2 text-[12px] text-amber-300">
								OpenRouter provider selected, but no OpenRouter API key is configured. Continuation summaries will fall back to local mode until a key is set.
							</div>
						{/if}

						{#if generalSettingsError}
							<div class="mt-3 text-[12px] text-red-400">{generalSettingsError}</div>
						{/if}
					</div>
				{/if}
			{:else if $settingsTab === 'indexing'}
				<ProjectIndexPanel bind:this={indexPanel} />
			{:else if $settingsTab === 'about'}
				<div class="mb-6">
					<h2 class="mb-1 text-[18px] font-medium text-foreground">About</h2>
					<p class="text-[13px] text-muted-foreground/60">Plugin information and update actions.</p>
				</div>

				<div class="rounded-lg border border-border/60 bg-card p-4">
					<div class="mb-3">
						<h3 class="text-[14px] font-medium text-foreground">Updates</h3>
						<p class="mt-1 text-[12px] text-muted-foreground/60">Run a manual update check without restarting the editor.</p>
					</div>

					<button
						class="rounded-md border border-border/60 px-3 py-2 text-[13px] text-foreground transition-colors hover:bg-accent disabled:cursor-not-allowed disabled:opacity-60"
						onclick={handleCheckForUpdates}
						disabled={isCheckingForUpdates}
					>
						{isCheckingForUpdates ? 'Checking...' : 'Check for Updates'}
					</button>

					{#if updateCheckMessage}
						<p class="mt-2 text-[12px] text-muted-foreground/70">{updateCheckMessage}</p>
					{/if}
				</div>
			{:else}
				<h2 class="mb-1 text-[18px] font-medium text-foreground">
					{tabs.find(t => t.id === $settingsTab)?.label ?? 'Settings'}
				</h2>
				<p class="text-[13px] text-muted-foreground/60">Coming soon</p>
			{/if}
		</div>
	</div>
</div>
