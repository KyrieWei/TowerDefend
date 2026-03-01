<script lang="ts">
	import Icon from '$lib/components/Icon.svelte';
	import {
		CheckmarkCircle02Icon,
		AlertCircleIcon,
		ArrowReloadHorizontalIcon,
		Delete02Icon,
		CloudIcon,
		Link01Icon,
		Tick02Icon
	} from '@hugeicons/core-free-icons';
	import {
		getIndexingSettings,
		setIndexingProvider,
		setIndexingEndpointUrl,
		setIndexingApiKey,
		setIndexingModel,
		setIndexingDimensions,
		setIndexingScopeEnabled,
		setAutoIndex,
		startIndexing,
		clearIndex,
		getIndexingStatus,
		type IndexingSettings,
		type IndexingStatus
	} from '$lib/bridge.js';

	// ── State ──────────────────────────────────────────────────────────
	let settings = $state<IndexingSettings>({
		provider: 'openrouter',
		endpointUrl: '',
		apiKey: '',
		model: 'google/gemini-embedding-001',
		dimensions: 768,
		autoIndex: false,
		scope: { blueprints: true, cppFiles: true, assets: true, levels: true, config: false },
		hasOpenRouterKey: false
	});

	let status = $state<IndexingStatus>({
		state: 'idle',
		totalChunks: 0,
		indexedChunks: 0,
		lastIndexedAt: '',
		indexSizeBytes: 0,
		errorMessage: '',
		breakdown: { blueprints: 0, cppFiles: 0, assets: 0, levels: 0, config: 0 },
		embeddingModel: '',
		embeddingDimensions: 0
	});

	let loading = $state(true);
	let indexing = $state(false);
	let progress = $state(0);
	let confirmClear = $state(false);
	let dims = $state(768);

	// Toast
	let toastMsg = $state('');
	let toastTimer: ReturnType<typeof setTimeout> | undefined;

	// Mismatch: index was built with different model/dims
	let mismatch = $derived(
		status.state === 'ready' &&
		status.embeddingModel !== '' &&
		(status.embeddingModel !== (settings.model || 'google/gemini-embedding-001') ||
		 status.embeddingDimensions !== dims)
	);

	const SCOPES = [
		{ key: 'blueprints' as const, label: 'Blueprints', desc: 'Graphs, functions, events, variables, components' },
		{ key: 'cppFiles' as const, label: 'C++ Source', desc: 'Headers and implementation files' },
		{ key: 'assets' as const, label: 'Assets', desc: 'Materials, animations, data tables, sounds' },
		{ key: 'levels' as const, label: 'Levels', desc: 'Actor hierarchy and placement data' },
		{ key: 'config' as const, label: 'Config', desc: 'Project .ini configuration' }
	] as const;

	const DIM_OPTIONS = [256, 384, 768, 1024, 3072] as const;

	// ── Init ───────────────────────────────────────────────────────────
	export async function load() {
		loading = true;
		try {
			const [s, st] = await Promise.all([getIndexingSettings(), getIndexingStatus()]);
			settings = s;
			status = st;
			dims = s.dimensions;
			indexing = st.state === 'indexing';
			if (indexing && st.totalChunks > 0) {
				progress = Math.round((st.indexedChunks / st.totalChunks) * 100);
			}
		} catch (e) {
			console.warn('Failed to load indexing settings:', e);
		} finally {
			loading = false;
		}
	}

	// ── Helpers ─────────────────────────────────────────────────────────
	function toast(msg: string) {
		toastMsg = msg;
		clearTimeout(toastTimer);
		toastTimer = setTimeout(() => (toastMsg = ''), 2200);
	}

	function fmtBytes(b: number): string {
		if (b === 0) return '0 B';
		const k = 1024;
		const s = ['B', 'KB', 'MB', 'GB'];
		const i = Math.floor(Math.log(b) / Math.log(k));
		return parseFloat((b / Math.pow(k, i)).toFixed(1)) + ' ' + s[i];
	}

	function fmtAgo(iso: string): string {
		if (!iso) return 'Never';
		const m = Math.floor((Date.now() - new Date(iso).getTime()) / 60000);
		if (m < 1) return 'Just now';
		if (m < 60) return `${m}m ago`;
		const h = Math.floor(m / 60);
		if (h < 24) return `${h}h ago`;
		return `${Math.floor(h / 24)}d ago`;
	}

	let debounceTimer: ReturnType<typeof setTimeout> | undefined;
	function debounced(fn: () => Promise<void>) {
		clearTimeout(debounceTimer);
		debounceTimer = setTimeout(async () => { await fn(); toast('Saved'); }, 500);
	}

	// ── Handlers ────────────────────────────────────────────────────────
	async function setProvider(p: 'openrouter' | 'custom') {
		settings = { ...settings, provider: p };
		toast('Saved');
		try { await setIndexingProvider(p); } catch (e) { console.warn(e); }
	}

	async function setDims(d: number) {
		dims = d;
		settings = { ...settings, dimensions: d };
		toast('Saved');
		try { await setIndexingDimensions(d); } catch (e) { console.warn(e); }
	}

	async function toggleScope(key: keyof IndexingSettings['scope']) {
		const val = !settings.scope[key];
		settings = { ...settings, scope: { ...settings.scope, [key]: val } };
		toast('Saved');
		try { await setIndexingScopeEnabled(key, val); } catch (e) { console.warn(e); }
	}

	async function toggleAutoIndex() {
		const val = !settings.autoIndex;
		settings = { ...settings, autoIndex: val };
		toast('Saved');
		try { await setAutoIndex(val); } catch (e) { console.warn(e); }
	}

	async function buildIndex() {
		if (indexing) return;
		indexing = true;
		progress = 0;
		status = { ...status, state: 'indexing', errorMessage: '' };
		try {
			await startIndexing();
			const poll = setInterval(async () => {
				const s = await getIndexingStatus();
				status = s;
				if (s.totalChunks > 0) progress = Math.round((s.indexedChunks / s.totalChunks) * 100);
				if (s.state !== 'indexing') {
					clearInterval(poll);
					indexing = false;
					progress = s.state === 'ready' ? 100 : 0;
				}
			}, 1000);
		} catch {
			indexing = false;
			status = { ...status, state: 'error', errorMessage: 'Failed to start indexing' };
		}
	}

	async function doClear() {
		confirmClear = false;
		try {
			await clearIndex();
			status = { state: 'idle', totalChunks: 0, indexedChunks: 0, lastIndexedAt: '', indexSizeBytes: 0, errorMessage: '', breakdown: { blueprints: 0, cppFiles: 0, assets: 0, levels: 0, config: 0 }, embeddingModel: '', embeddingDimensions: 0 };
			progress = 0;
			toast('Index cleared');
		} catch { console.warn('Failed to clear index'); }
	}
</script>

<!-- ═══════════════════════════════════════════════════════════════════
     HEADER
     ═══════════════════════════════════════════════════════════════════ -->
<div class="mb-6">
	<h2 class="mb-1 text-[18px] font-medium text-foreground">Project Index</h2>
	<p class="text-[13px] text-muted-foreground/60">
		Index your project for semantic search. Agents find relevant Blueprints, code,
		and assets by meaning — not just file names.
	</p>
	<p class="mt-1 text-[11px] text-muted-foreground/30">
		Recommended for larger projects. Small projects with a few Blueprints typically don't need this.
	</p>
</div>

{#if loading}
	<div class="flex items-center gap-2 py-12 text-[13px] text-muted-foreground/50">
		<span class="inline-block h-4 w-4 animate-spin rounded-full border-2 border-muted-foreground/30 border-t-muted-foreground"></span>
		Loading…
	</div>
{:else}

<!-- ═══════════════════════════════════════════════════════════════════
     STATUS CARD
     ═══════════════════════════════════════════════════════════════════ -->
<section class="idx-card mb-5">
	<!-- Top row -->
	<div class="flex items-center justify-between px-4 py-3.5">
		<div class="flex items-center gap-3">
			<!-- Status dot -->
			<span class="relative flex h-2.5 w-2.5 shrink-0">
				{#if status.state === 'ready'}
					<span class="absolute inline-flex h-full w-full animate-ping rounded-full bg-emerald-400 opacity-50"></span>
					<span class="relative inline-flex h-2.5 w-2.5 rounded-full bg-emerald-400"></span>
				{:else if status.state === 'indexing'}
					<span class="absolute inline-flex h-full w-full animate-ping rounded-full bg-blue-400 opacity-50"></span>
					<span class="relative inline-flex h-2.5 w-2.5 rounded-full bg-blue-400"></span>
				{:else if status.state === 'error'}
					<span class="relative inline-flex h-2.5 w-2.5 rounded-full bg-red-400"></span>
				{:else}
					<span class="relative inline-flex h-2.5 w-2.5 rounded-full bg-foreground/20"></span>
				{/if}
			</span>

			<div>
				<span class="text-[13px] font-medium text-foreground">
					{#if status.state === 'ready'}Index Ready
					{:else if status.state === 'indexing'}Indexing…
					{:else if status.state === 'error'}Error
					{:else}Not Indexed
					{/if}
				</span>
				<p class="text-[11px] text-muted-foreground/45">
					{#if status.state === 'ready'}
						{status.totalChunks.toLocaleString()} chunks &middot; {fmtBytes(status.indexSizeBytes)} &middot; {fmtAgo(status.lastIndexedAt)}
					{:else if status.state === 'indexing'}
						{status.indexedChunks.toLocaleString()} / {status.totalChunks.toLocaleString()} chunks
					{:else if status.state === 'error'}
						{status.errorMessage}
					{:else}
						Configure settings below, then build an index
					{/if}
				</p>
			</div>
		</div>

		<!-- Actions -->
		<div class="flex items-center gap-2">
			{#if status.state === 'ready'}
				<button class="idx-btn-ghost" onclick={buildIndex} disabled={indexing}>
					<Icon icon={ArrowReloadHorizontalIcon} size={13} strokeWidth={1.5} />
					Rebuild
				</button>
				<button
					class="idx-btn-ghost text-muted-foreground/40 hover:text-red-400"
					onclick={() => (confirmClear = true)}
					aria-label="Clear index"
				>
					<Icon icon={Delete02Icon} size={13} strokeWidth={1.5} />
				</button>
			{:else if status.state === 'indexing'}
				<!-- progress shown below -->
			{:else}
				<button class="idx-btn" onclick={buildIndex} disabled={indexing}>Build Index</button>
			{/if}
		</div>
	</div>

	<!-- Progress -->
	{#if status.state === 'indexing'}
		<div class="h-[3px] w-full bg-foreground/5">
			<div
				class="h-full bg-blue-400 transition-all duration-500 ease-out"
				style="width: {progress}%"
			></div>
		</div>
	{/if}

	<!-- Breakdown (when ready) -->
	{#if status.state === 'ready'}
		{@const bd = status.breakdown}
		{@const cats = [
			{ k: 'blueprints', n: bd.blueprints, l: 'Blueprints' },
			{ k: 'cppFiles', n: bd.cppFiles, l: 'C++' },
			{ k: 'assets', n: bd.assets, l: 'Assets' },
			{ k: 'levels', n: bd.levels, l: 'Levels' },
			{ k: 'config', n: bd.config, l: 'Config' }
		].filter(c => c.n > 0)}
		{#if cats.length > 0}
			<div class="flex items-center gap-4 border-t border-border/25 px-4 py-2.5">
				{#each cats as cat}
					<span class="text-[11px] text-muted-foreground/40">
						<span class="font-medium tabular-nums text-muted-foreground/60">{cat.n}</span> {cat.l}
					</span>
				{/each}
				{#if status.embeddingModel}
					<span class="ml-auto font-mono text-[10px] text-muted-foreground/25">
						{status.embeddingModel} · {status.embeddingDimensions}d
					</span>
				{/if}
			</div>
		{/if}
	{/if}

	<!-- Clear confirm -->
	{#if confirmClear}
		<div class="flex items-center justify-between border-t border-border/30 px-4 py-2.5">
			<span class="text-[12px] text-muted-foreground/60">Delete the entire index?</span>
			<div class="flex gap-2">
				<button class="idx-btn-ghost text-[12px]" onclick={() => (confirmClear = false)}>Cancel</button>
				<button
					class="rounded-md bg-red-500/15 px-2.5 py-1 text-[12px] text-red-400 transition-colors hover:bg-red-500/25"
					onclick={doClear}
				>Delete</button>
			</div>
		</div>
	{/if}
</section>

<!-- Mismatch warning -->
{#if mismatch}
	<div class="idx-card mb-5 border-amber-500/20 bg-amber-500/[0.04] px-4 py-3">
		<p class="text-[12px] leading-relaxed text-amber-300/80">
			Index was built with
			<span class="font-mono text-amber-200/90">{status.embeddingModel}</span> at
			<span class="font-mono text-amber-200/90">{status.embeddingDimensions}d</span>.
			Changing the model or dimensions requires a full rebuild — embeddings from different models can't be mixed.
		</p>
		<div class="mt-2.5 flex items-center gap-3">
			<button class="idx-btn text-[12px]" onclick={buildIndex}>
				<Icon icon={ArrowReloadHorizontalIcon} size={12} strokeWidth={1.5} />
				Rebuild Index
			</button>
			<button
				class="text-[12px] text-amber-400/50 transition-colors hover:text-amber-300"
				onclick={() => (confirmClear = true)}
			>Clear instead</button>
		</div>
	</div>
{/if}

<!-- ═══════════════════════════════════════════════════════════════════
     EMBEDDING CONFIGURATION
     ═══════════════════════════════════════════════════════════════════ -->
<section class="idx-card mb-5 px-4 py-4">
	<h3 class="mb-1 text-[13px] font-medium text-foreground">Embedding Provider</h3>
	<p class="mb-4 text-[11px] text-muted-foreground/40">
		Any service supporting the OpenAI
		<code class="rounded bg-foreground/[0.06] px-1 py-px font-mono text-[10px] text-foreground/50">/v1/embeddings</code>
		format works.
	</p>

	<!-- Provider segmented control -->
	<div class="mb-4 inline-flex rounded-lg border border-border/50 bg-foreground/[0.02] p-0.5">
		<button
			class="flex items-center gap-1.5 rounded-md px-3 py-1.5 text-[12px] transition-all
				{settings.provider === 'openrouter'
					? 'bg-foreground/[0.08] text-foreground shadow-sm'
					: 'text-muted-foreground/50 hover:text-muted-foreground'}"
			onclick={() => setProvider('openrouter')}
		>
			<Icon icon={CloudIcon} size={13} strokeWidth={1.5} />
			OpenRouter
		</button>
		<button
			class="flex items-center gap-1.5 rounded-md px-3 py-1.5 text-[12px] transition-all
				{settings.provider === 'custom'
					? 'bg-foreground/[0.08] text-foreground shadow-sm'
					: 'text-muted-foreground/50 hover:text-muted-foreground'}"
			onclick={() => setProvider('custom')}
		>
			<Icon icon={Link01Icon} size={13} strokeWidth={1.5} />
			Custom Endpoint
		</button>
	</div>

	<div class="flex flex-col gap-3.5">
		{#if settings.provider === 'openrouter'}
			<!-- OpenRouter model -->
			<div>
				<label for="idx-model" class="idx-label">Model</label>
				<input
					id="idx-model"
					type="text"
					value={settings.model || 'google/gemini-embedding-001'}
					oninput={(e) => debounced(() => {
						const v = (e.currentTarget as HTMLInputElement).value;
						settings = { ...settings, model: v };
						return setIndexingModel(v);
					})}
					class="idx-input"
					placeholder="google/gemini-embedding-001"
				/>
				<p class="idx-hint">
					Uses your OpenRouter API key. Gemini Embedding supports flexible dimensions up to 3072.
				</p>
			</div>

			{#if !settings.hasOpenRouterKey}
				<div class="rounded-md border border-amber-500/20 bg-amber-500/[0.06] px-3 py-2 text-[12px] text-amber-300/80">
					No OpenRouter API key configured. Set one in <span class="text-amber-200/80">Project Settings → Plugins → Agent Integration Kit</span>.
				</div>
			{/if}
		{:else}
			<!-- Custom endpoint -->
			<div>
				<label for="idx-endpoint" class="idx-label">Endpoint URL</label>
				<input
					id="idx-endpoint"
					type="text"
					value={settings.endpointUrl}
					oninput={(e) => debounced(() => {
						const v = (e.currentTarget as HTMLInputElement).value;
						settings = { ...settings, endpointUrl: v };
						return setIndexingEndpointUrl(v);
					})}
					class="idx-input font-mono"
					placeholder="http://localhost:11434/v1/embeddings"
				/>
				<p class="idx-hint">
					Compatible with Ollama · LM Studio · vLLM · HF TEI · LocalAI
				</p>
			</div>

			<div>
				<label for="idx-key" class="idx-label">API Key <span class="font-normal text-muted-foreground/25">(optional)</span></label>
				<input
					id="idx-key"
					type="password"
					value={settings.apiKey}
					oninput={(e) => debounced(() => {
						const v = (e.currentTarget as HTMLInputElement).value;
						settings = { ...settings, apiKey: v };
						return setIndexingApiKey(v);
					})}
					class="idx-input"
					placeholder="sk-..."
				/>
				<p class="idx-hint">Leave empty for local servers without auth.</p>
			</div>

			<div>
				<label for="idx-cmodel" class="idx-label">Model Name</label>
				<input
					id="idx-cmodel"
					type="text"
					value={settings.model}
					oninput={(e) => debounced(() => {
						const v = (e.currentTarget as HTMLInputElement).value;
						settings = { ...settings, model: v };
						return setIndexingModel(v);
					})}
					class="idx-input"
					placeholder="nomic-embed-text"
				/>
			</div>
		{/if}

		<!-- Dimensions -->
		<div>
			<span class="idx-label">Dimensions</span>
			<div class="flex gap-1.5">
				{#each DIM_OPTIONS as d}
					<button
						class="rounded-md border px-3 py-1.5 font-mono text-[12px] transition-all
							{dims === d
								? 'border-foreground/15 bg-foreground/[0.07] text-foreground'
								: 'border-transparent text-muted-foreground/35 hover:bg-foreground/[0.03] hover:text-muted-foreground/60'}"
						onclick={() => setDims(d)}
					>
						{d}{#if d === 768}<span class="ml-1 font-sans text-[10px] text-muted-foreground/30">default</span>{/if}
					</button>
				{/each}
			</div>
			<p class="idx-hint">
				Lower dimensions = faster search, less storage.
				<span class="text-muted-foreground/40">768</span> is a good balance.
			</p>
		</div>
	</div>
</section>

<!-- ═══════════════════════════════════════════════════════════════════
     INDEX SCOPE
     ═══════════════════════════════════════════════════════════════════ -->
<section class="idx-card mb-5 px-4 py-4">
	<h3 class="mb-1 text-[13px] font-medium text-foreground">Index Scope</h3>
	<p class="mb-3.5 text-[11px] text-muted-foreground/40">
		More content = better search but longer indexing.
	</p>

	<div class="flex flex-col gap-0.5">
		{#each SCOPES as scope}
			<label
				class="group flex cursor-pointer items-center gap-3 rounded-md px-2.5 py-2 transition-colors hover:bg-foreground/[0.025]"
			>
				<!-- Custom checkbox -->
				<span class="relative flex h-4 w-4 shrink-0 items-center justify-center">
					<input
						type="checkbox"
						checked={settings.scope[scope.key]}
						onchange={() => toggleScope(scope.key)}
						class="peer sr-only"
					/>
					<span
						class="h-4 w-4 rounded border transition-all
							{settings.scope[scope.key]
								? 'border-foreground/30 bg-foreground/10'
								: 'border-foreground/12 bg-transparent'}"
					></span>
					{#if settings.scope[scope.key]}
						<Icon icon={Tick02Icon} size={10} strokeWidth={2.5} class="absolute text-foreground/80" />
					{/if}
				</span>

				<div class="flex flex-1 items-baseline gap-2">
					<span class="text-[13px] text-foreground/80">{scope.label}</span>
					<span class="text-[11px] text-muted-foreground/30">{scope.desc}</span>
					{#if status.state === 'ready' && status.breakdown[scope.key] > 0}
						<span class="ml-auto tabular-nums text-[10px] text-muted-foreground/25">{status.breakdown[scope.key]}</span>
					{/if}
				</div>
			</label>
		{/each}
	</div>
</section>

<!-- ═══════════════════════════════════════════════════════════════════
     AUTO-INDEX
     ═══════════════════════════════════════════════════════════════════ -->
<section class="idx-card px-4 py-4">
	<div class="flex items-center justify-between">
		<div>
			<h3 class="text-[13px] font-medium text-foreground">Auto-Index</h3>
			<p class="mt-0.5 text-[11px] text-muted-foreground/40">
				Re-index incrementally when assets change.
			</p>
		</div>

		<!-- Toggle switch -->
		<button
			role="switch"
			aria-checked={settings.autoIndex}
			class="idx-toggle"
			class:idx-toggle-on={settings.autoIndex}
			onclick={toggleAutoIndex}
		>
			<span
				class="idx-toggle-thumb"
				class:idx-toggle-thumb-on={settings.autoIndex}
			></span>
		</button>
	</div>

	{#if settings.autoIndex && status.state === 'idle'}
		<p class="mt-3 rounded-md border border-amber-500/15 bg-amber-500/[0.04] px-3 py-2 text-[11px] text-amber-300/70">
			No index exists yet — build one first. Changes will be indexed automatically after that.
		</p>
	{/if}
</section>

<!-- ═══════════════════════════════════════════════════════════════════
     TOAST
     ═══════════════════════════════════════════════════════════════════ -->
{#if toastMsg}
	<div class="idx-toast">
		<Icon icon={CheckmarkCircle02Icon} size={12} strokeWidth={1.5} />
		{toastMsg}
	</div>
{/if}

{/if}

<style>
	/* ── Shared card ─────────────────────────────────────────────── */
	.idx-card {
		@apply overflow-hidden rounded-lg border bg-[#2a2a2a];
		border-color: color-mix(in srgb, var(--border) 50%, transparent);
	}

	/* ── Buttons ─────────────────────────────────────────────────── */
	.idx-btn {
		@apply flex items-center gap-1.5 rounded-md px-3 py-1.5 text-[12px] font-medium text-foreground transition-colors;
		background: color-mix(in srgb, var(--foreground) 8%, transparent);
	}
	.idx-btn:hover {
		background: color-mix(in srgb, var(--foreground) 14%, transparent);
	}
	.idx-btn:disabled {
		@apply cursor-not-allowed opacity-40;
	}
	.idx-btn-ghost {
		@apply flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-[12px] text-muted-foreground transition-colors;
	}
	.idx-btn-ghost:hover {
		background: color-mix(in srgb, var(--foreground) 5%, transparent);
		color: var(--foreground);
	}

	/* ── Inputs ──────────────────────────────────────────────────── */
	.idx-input {
		@apply w-full rounded-md border bg-[#1e1e1e] px-3 py-2 text-[13px] text-foreground;
		border-color: color-mix(in srgb, var(--border) 40%, transparent);
	}
	.idx-input::placeholder {
		color: color-mix(in srgb, var(--muted-foreground) 25%, transparent);
	}
	.idx-input:focus {
		border-color: color-mix(in srgb, var(--foreground) 20%, transparent);
		outline: none;
	}
	.idx-label {
		@apply mb-1.5 block text-[12px] font-medium;
		color: color-mix(in srgb, var(--muted-foreground) 60%, transparent);
	}
	.idx-hint {
		@apply mt-1.5 text-[11px] leading-relaxed;
		color: color-mix(in srgb, var(--muted-foreground) 30%, transparent);
	}

	/* ── Toggle switch ───────────────────────────────────────────── */
	.idx-toggle {
		@apply relative inline-flex h-[22px] w-[40px] shrink-0 cursor-pointer items-center rounded-full border bg-[#1e1e1e] transition-colors;
		border-color: color-mix(in srgb, var(--foreground) 10%, transparent);
	}
	.idx-toggle-on {
		background: rgb(16 185 129 / 0.7);
		border-color: rgb(16 185 129 / 0.3);
	}
	.idx-toggle-thumb {
		@apply pointer-events-none inline-block h-[16px] w-[16px] translate-x-[3px] rounded-full shadow-sm transition-transform;
		background: color-mix(in srgb, var(--foreground) 40%, transparent);
	}
	.idx-toggle-thumb-on {
		@apply translate-x-[19px] bg-white;
	}

	/* ── Toast ────────────────────────────────────────────────────── */
	.idx-toast {
		@apply pointer-events-none fixed bottom-6 left-1/2 z-50 flex -translate-x-1/2 items-center gap-1.5 rounded-full border bg-[#1e1e1e] px-3.5 py-1.5 text-[12px] shadow-lg;
		border-color: rgb(16 185 129 / 0.2);
		color: rgb(52 211 153 / 0.8);
		animation: toast-pop 2.2s ease-out forwards;
	}

	@keyframes toast-pop {
		0% { opacity: 0; transform: translate(-50%, 8px); }
		10% { opacity: 1; transform: translate(-50%, 0); }
		80% { opacity: 1; }
		100% { opacity: 0; transform: translate(-50%, -4px); }
	}
</style>
