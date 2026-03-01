<script lang="ts">
	import { fly } from 'svelte/transition';
	import Icon from '$lib/components/Icon.svelte';
	import {
		Tick02Icon,
		ArrowRight01Icon,
		ArrowLeft02Icon,
		SparklesIcon,
		Rocket01Icon
	} from '@hugeicons/core-free-icons';
	import { agents, type Agent } from '$lib/stores/agents.js';
	import {
		onboardingStep,
		selectedSubscriptions,
		subscriptionOptions,
		recommendedAgentName,
		alternativeAgentNames,
		goNext,
		goBack,
		toggleSubscription,
		completeOnboarding,
		skipOnboarding,
		type SubscriptionId
	} from '$lib/stores/onboarding.js';

	// Map subscription IDs to agent objects for icons/colors
	function getAgentForSubscription(agentName: string): Agent | undefined {
		return $agents.find(a => a.name === agentName);
	}

	function getRecommendedAgent(): Agent | undefined {
		return $agents.find(a => a.name === $recommendedAgentName);
	}

	function getAlternativeAgents(): Agent[] {
		return $alternativeAgentNames
			.map(name => $agents.find(a => a.name === name))
			.filter((a): a is Agent => !!a);
	}

	// Auto-detect installed agents on subscriptions step
	let autoDetected = $state(false);

	$effect(() => {
		if ($onboardingStep === 'subscriptions' && !autoDetected && $agents.length > 0) {
			autoDetected = true;
			const detected = new Set<SubscriptionId>();
			for (const opt of subscriptionOptions) {
				if (opt.agentName) {
					const agent = $agents.find(a => a.name === opt.agentName);
					if (agent && agent.status === 'available') {
						detected.add(opt.id);
					}
				}
			}
			if (detected.size > 0) {
				selectedSubscriptions.set(detected);
			}
		}
	});

	// Step indicator
	const steps: Array<{ key: typeof $onboardingStep; label: string }> = [
		{ key: 'welcome', label: 'Welcome' },
		{ key: 'subscriptions', label: 'Services' },
		{ key: 'recommendation', label: 'Setup' },
	];

	function stepIndex(step: typeof $onboardingStep): number {
		return steps.findIndex(s => s.key === step);
	}

	// Can continue from subscriptions?
	let canContinue = $derived($selectedSubscriptions.size > 0);

	// Agents for the "none" fallback cards (reactive)
	let geminiAgent = $derived($agents.find(a => a.name === 'Gemini CLI'));
	let openrouterAgent = $derived($agents.find(a => a.name === 'OpenRouter'));
</script>

<div class="flex flex-1 flex-col items-center justify-center px-6">
	<div class="flex w-full max-w-md flex-col items-center">

		{#if $onboardingStep === 'welcome'}
			<!-- ═══════════════════════════════════════════════════════ -->
			<!--  Step 1: Welcome                                       -->
			<!-- ═══════════════════════════════════════════════════════ -->
			<div
				class="flex flex-col items-center gap-8"
				in:fly={{ x: 60, duration: 250, delay: 80 }}
				out:fly={{ x: -60, duration: 200 }}
			>
				<!-- Animated icon cluster -->
				<div class="relative flex items-center justify-center">
					<div class="absolute h-24 w-24 rounded-full bg-gradient-to-br from-violet-500/8 to-amber-500/8 blur-xl"></div>
					<div class="relative flex h-20 w-20 items-center justify-center rounded-2xl border border-border/30 bg-card/40 shadow-lg shadow-black/20 backdrop-blur-sm">
						<Icon icon={SparklesIcon} size={36} strokeWidth={1.2} class="text-foreground/50" />
					</div>
				</div>

				<div class="flex flex-col items-center gap-3 text-center">
					<h1 class="text-[22px] font-semibold tracking-tight text-foreground/90">
						Welcome to Agent Integration Kit
					</h1>
					<p class="max-w-xs text-[13.5px] leading-relaxed text-muted-foreground/50">
						AI-powered Unreal Engine editing. Let's get you set up in under a minute.
					</p>
				</div>

				<div class="flex flex-col items-center gap-3">
					<button
						class="group flex items-center gap-2 rounded-xl bg-foreground/90 px-7 py-2.5 text-[14px] font-medium text-background transition-all hover:bg-foreground"
						onclick={goNext}
					>
						Get Started
						<Icon icon={ArrowRight01Icon} size={16} strokeWidth={2} class="transition-transform group-hover:translate-x-0.5" />
					</button>
					<button
						class="text-[12px] text-muted-foreground/30 transition-colors hover:text-muted-foreground/50"
						onclick={skipOnboarding}
					>
						Skip — I'll figure it out
					</button>
				</div>
			</div>

		{:else if $onboardingStep === 'subscriptions'}
			<!-- ═══════════════════════════════════════════════════════ -->
			<!--  Step 2: What subscriptions do you have?               -->
			<!-- ═══════════════════════════════════════════════════════ -->
			<div
				class="flex w-full flex-col items-center gap-6"
				in:fly={{ x: 60, duration: 250, delay: 80 }}
				out:fly={{ x: -60, duration: 200 }}
			>
				<div class="flex flex-col items-center gap-2 text-center">
					<h2 class="text-lg font-medium text-foreground/85">What do you already have?</h2>
					<p class="max-w-xs text-[13px] leading-relaxed text-muted-foreground/45">
						Select any subscriptions or tools you use. We'll recommend the best agent for you.
					</p>
				</div>

				<!-- Subscription cards -->
				<div class="flex w-full flex-col gap-2">
					{#each subscriptionOptions as opt (opt.id)}
						{@const agent = opt.agentName ? getAgentForSubscription(opt.agentName) : undefined}
						{@const isSelected = $selectedSubscriptions.has(opt.id)}
						{@const isInstalled = agent?.status === 'available'}
						{@const isNone = opt.id === 'none'}
						<button
							class="group flex w-full items-center gap-3 rounded-xl border px-4 py-3 text-left transition-all duration-150
								{isSelected
									? isNone
										? 'border-muted-foreground/20 bg-muted-foreground/5'
										: 'border-white/12 bg-white/[0.04]'
									: 'border-border/40 bg-transparent hover:border-border/60 hover:bg-white/[0.02]'
								}"
							onclick={() => toggleSubscription(opt.id)}
						>
							<!-- Agent icon or letter badge -->
							{#if agent?.icon}
								<span
									class="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg transition-all duration-150"
									style="color: {agent.color}; opacity: {isSelected ? 0.9 : 0.4}; background-color: {agent.color}10;"
								>
									<Icon icon={agent.icon} size={20} strokeWidth={1.5} />
								</span>
							{:else if agent}
								<span
									class="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg text-[10px] font-bold text-white transition-opacity duration-150"
									style="background-color: {agent.color}; opacity: {isSelected ? 0.9 : 0.4};"
								>
									{agent.letter}
								</span>
							{:else}
								<!-- "None" option — neutral icon -->
								<span
									class="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg bg-muted-foreground/5 text-[16px] text-muted-foreground/30 transition-opacity duration-150"
									style="opacity: {isSelected ? 0.8 : 0.4};"
								>
									—
								</span>
							{/if}

							<!-- Label + sublabel -->
							<div class="min-w-0 flex-1">
								<div class="flex items-center gap-2">
									<span class="text-[13.5px] font-medium text-foreground/80 {isSelected ? 'text-foreground/90' : ''} truncate">
										{opt.label}
									</span>
									{#if isInstalled}
										<span class="flex items-center gap-1 rounded-full bg-emerald-500/10 px-1.5 py-0.5 text-[10px] font-medium text-emerald-400/80">
											<span class="h-1.5 w-1.5 rounded-full bg-emerald-500"></span>
											Installed
										</span>
									{/if}
								</div>
								<span class="text-[12px] text-muted-foreground/35">{opt.sublabel}</span>
							</div>

							<!-- Check indicator -->
							<div
								class="flex h-5 w-5 shrink-0 items-center justify-center rounded-md border transition-all duration-150
									{isSelected
										? 'border-foreground/60 bg-foreground/80'
										: 'border-muted-foreground/15 bg-transparent'
									}"
							>
								{#if isSelected}
									<Icon icon={Tick02Icon} size={12} strokeWidth={2.5} class="text-background" />
								{/if}
							</div>
						</button>
					{/each}
				</div>

				<!-- Navigation -->
				<div class="flex w-full items-center justify-between pt-1">
					<button
						class="flex items-center gap-1.5 rounded-lg px-3 py-2 text-[13px] text-muted-foreground/40 transition-colors hover:text-muted-foreground/60"
						onclick={goBack}
					>
						<Icon icon={ArrowLeft02Icon} size={14} strokeWidth={1.5} />
						Back
					</button>
					<button
						class="flex items-center gap-2 rounded-xl px-5 py-2 text-[13.5px] font-medium transition-all
							{canContinue
								? 'bg-foreground/90 text-background hover:bg-foreground'
								: 'cursor-not-allowed bg-foreground/10 text-foreground/20'
							}"
						onclick={goNext}
						disabled={!canContinue}
					>
						Continue
						<Icon icon={ArrowRight01Icon} size={14} strokeWidth={2} />
					</button>
				</div>
			</div>

		{:else if $onboardingStep === 'recommendation'}
			<!-- ═══════════════════════════════════════════════════════ -->
			<!--  Step 3: Recommendation                                -->
			<!-- ═══════════════════════════════════════════════════════ -->
			{@const recAgent = getRecommendedAgent()}
			{@const altAgents = getAlternativeAgents()}
			{@const isReady = recAgent?.status === 'available'}

			<div
				class="flex w-full flex-col items-center gap-7"
				in:fly={{ x: 60, duration: 250, delay: 80 }}
				out:fly={{ x: -60, duration: 200 }}
			>
				{#if recAgent}
					<!-- Agent icon with glow -->
					<div class="relative flex items-center justify-center">
						<div
							class="absolute h-20 w-20 rounded-full blur-xl"
							style="background-color: {recAgent.color}; opacity: 0.08;"
						></div>
						{#if recAgent.icon}
							<span style="color: {recAgent.color};">
								<Icon icon={recAgent.icon} size={52} strokeWidth={1} />
							</span>
						{:else}
							<span
								class="flex h-14 w-14 items-center justify-center rounded-2xl text-lg font-bold text-white"
								style="background-color: {recAgent.color};"
							>
								{recAgent.letter}
							</span>
						{/if}
					</div>

					<!-- Title -->
					<div class="flex flex-col items-center gap-2 text-center">
						{#if isReady}
							<h2 class="text-lg font-medium text-emerald-400/90">You're all set!</h2>
							<p class="max-w-xs text-[13px] leading-relaxed text-muted-foreground/50">
								<span class="font-medium text-foreground/70">{recAgent.name}</span> is installed and ready to help you build in Unreal Engine.
							</p>
						{:else}
							<h2 class="text-lg font-medium text-foreground/85">
								We recommend {recAgent.name}
							</h2>
							<p class="max-w-xs text-[13px] leading-relaxed text-muted-foreground/50">
								{recAgent.name} needs a quick setup before you can start. We'll walk you through it.
							</p>
						{/if}
					</div>

					<!-- Primary action -->
					<button
						class="group flex items-center gap-2.5 rounded-xl px-6 py-2.5 text-[14px] font-medium text-white transition-all hover:brightness-110"
						style="background-color: {recAgent.color};"
						onclick={() => completeOnboarding(recAgent.name)}
					>
						{#if isReady}
							<Icon icon={Rocket01Icon} size={16} strokeWidth={1.5} />
							Start chatting with {recAgent.shortName}
						{:else}
							Set up {recAgent.shortName}
							<Icon icon={ArrowRight01Icon} size={14} strokeWidth={2} class="transition-transform group-hover:translate-x-0.5" />
						{/if}
					</button>

					<!-- Alternative agents -->
					{#if altAgents.length > 0}
						<div class="flex w-full flex-col items-center gap-2 pt-2">
							<span class="text-[11px] text-muted-foreground/25">or try</span>
							<div class="flex flex-wrap justify-center gap-2">
								{#each altAgents as alt}
									<button
										class="flex items-center gap-2 rounded-lg border border-border/30 bg-transparent px-3 py-1.5 text-[12px] text-muted-foreground/50 transition-all hover:border-border/50 hover:bg-white/[0.02] hover:text-muted-foreground/70"
										onclick={() => completeOnboarding(alt.name)}
									>
										{#if alt.icon}
											<span style="color: {alt.color}; opacity: 0.5;">
												<Icon icon={alt.icon} size={14} strokeWidth={1.5} />
											</span>
										{/if}
										{alt.shortName}
										{#if alt.status === 'available'}
											<span class="h-1.5 w-1.5 rounded-full bg-emerald-500/60"></span>
										{/if}
									</button>
								{/each}
							</div>
						</div>
					{/if}
				{:else}
					<!-- Fallback: "none" selected with no specific agent recommended -->
					<div class="flex flex-col items-center gap-3 text-center">
						<h2 class="text-lg font-medium text-foreground/85">Free options to get started</h2>
						<p class="max-w-xs text-[13px] leading-relaxed text-muted-foreground/50">
							No subscription needed — pick one below.
						</p>
					</div>

					<!-- Two side-by-side option cards -->
					<div class="grid w-full grid-cols-2 gap-3">
						{#if geminiAgent}
							<button
								class="flex flex-col items-center gap-3 rounded-xl border border-border/40 bg-card/20 p-5 text-center transition-all hover:border-border/60 hover:bg-card/40"
								onclick={() => completeOnboarding('Gemini CLI')}
							>
								{#if geminiAgent.icon}
									<span style="color: {geminiAgent.color};">
										<Icon icon={geminiAgent.icon} size={28} strokeWidth={1.2} />
									</span>
								{/if}
								<div>
									<div class="text-[13px] font-medium text-foreground/80">Gemini CLI</div>
									<div class="mt-0.5 text-[11px] text-muted-foreground/40">Free with Google account</div>
								</div>
								{#if geminiAgent.status === 'available'}
									<span class="flex items-center gap-1 text-[10px] text-emerald-400/70">
										<span class="h-1.5 w-1.5 rounded-full bg-emerald-500"></span>
										Ready
									</span>
								{/if}
							</button>
						{/if}

						{#if openrouterAgent}
							<button
								class="flex flex-col items-center gap-3 rounded-xl border border-border/40 bg-card/20 p-5 text-center transition-all hover:border-border/60 hover:bg-card/40"
								onclick={() => completeOnboarding('OpenRouter')}
							>
								{#if openrouterAgent.icon}
									<span style="color: {openrouterAgent.color};">
										<Icon icon={openrouterAgent.icon} size={28} strokeWidth={1.2} />
									</span>
								{/if}
								<div>
									<div class="text-[13px] font-medium text-foreground/80">OpenRouter</div>
									<div class="mt-0.5 text-[11px] text-muted-foreground/40">Pay-per-use, built-in</div>
								</div>
								{#if openrouterAgent.status === 'available'}
									<span class="flex items-center gap-1 text-[10px] text-emerald-400/70">
										<span class="h-1.5 w-1.5 rounded-full bg-emerald-500"></span>
										Ready
									</span>
								{:else}
									<span class="text-[10px] text-muted-foreground/30">API key needed</span>
								{/if}
							</button>
						{/if}
					</div>
				{/if}

				<!-- Back button -->
				<div class="flex w-full items-center pt-1">
					<button
						class="flex items-center gap-1.5 rounded-lg px-3 py-2 text-[13px] text-muted-foreground/40 transition-colors hover:text-muted-foreground/60"
						onclick={goBack}
					>
						<Icon icon={ArrowLeft02Icon} size={14} strokeWidth={1.5} />
						Back
					</button>
				</div>
			</div>
		{/if}

		<!-- Step indicator dots -->
		<div class="mt-8 flex items-center gap-2">
			{#each steps as step, i}
				{@const current = stepIndex($onboardingStep)}
				<div
					class="h-1.5 rounded-full transition-all duration-300
						{i === current
							? 'w-5 bg-foreground/50'
							: i < current
								? 'w-1.5 bg-foreground/25'
								: 'w-1.5 bg-foreground/10'
						}"
				></div>
			{/each}
		</div>
	</div>
</div>
