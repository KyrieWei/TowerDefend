<script lang="ts">
	import { searchContextItems, type ContextItem } from '$lib/bridge.js';
	import Icon from '$lib/components/Icon.svelte';
	import {
		File01Icon,
		Folder01Icon,
		GridIcon,
		PaintBoardIcon,
		Database01Icon,
		CodeIcon,
		Loading03Icon
	} from '@hugeicons/core-free-icons';

	let {
		query,
		visible,
		onselect,
		ondismiss
	}: {
		query: string;
		visible: boolean;
		onselect: (item: ContextItem) => void;
		ondismiss: () => void;
	} = $props();

	let results = $state<ContextItem[]>([]);
	let isLoading = $state(false);
	let selectedIndex = $state(0);
	let popupEl: HTMLDivElement | undefined = $state();

	// Debounced search
	let searchTimeout: ReturnType<typeof setTimeout> | undefined;

	$effect(() => {
		if (!visible) {
			results = [];
			selectedIndex = 0;
			return;
		}

		const q = query;
		clearTimeout(searchTimeout);

		if (q.length === 0) {
			// Show all results when @ is typed with no query
			isLoading = true;
			searchContextItems('').then((items) => {
				results = items;
				selectedIndex = 0;
				isLoading = false;
			});
			return;
		}

		isLoading = true;
		searchTimeout = setTimeout(() => {
			searchContextItems(q).then((items) => {
				results = items;
				selectedIndex = 0;
				isLoading = false;
			});
		}, 150);
	});

	function categoryIcon(type: string) {
		switch (type) {
			case 'blueprint':
			case 'anim_blueprint':
				return GridIcon;
			case 'widget':
				return PaintBoardIcon;
			case 'behavior_tree':
				return Folder01Icon;
			case 'material':
				return PaintBoardIcon;
			case 'data_table':
				return Database01Icon;
			case 'cpp_header':
			case 'cpp_source':
				return CodeIcon;
			default:
				return File01Icon;
		}
	}

	function categoryColor(type: string): string {
		switch (type) {
			case 'blueprint':
				return 'text-blue-400';
			case 'anim_blueprint':
				return 'text-purple-400';
			case 'widget':
				return 'text-emerald-400';
			case 'behavior_tree':
				return 'text-amber-400';
			case 'material':
				return 'text-green-400';
			case 'data_table':
				return 'text-cyan-400';
			case 'cpp_header':
			case 'cpp_source':
				return 'text-orange-400';
			default:
				return 'text-muted-foreground';
		}
	}

	// Group results by category
	let groupedResults = $derived(() => {
		const groups = new Map<string, ContextItem[]>();
		for (const item of results) {
			const cat = item.category;
			if (!groups.has(cat)) groups.set(cat, []);
			groups.get(cat)!.push(item);
		}
		return groups;
	});

	// Flat list for keyboard navigation
	let flatResults = $derived(results);

	export function handleKeydown(e: KeyboardEvent): boolean {
		if (!visible || results.length === 0) return false;

		if (e.key === 'ArrowDown') {
			e.preventDefault();
			selectedIndex = (selectedIndex + 1) % flatResults.length;
			scrollToSelected();
			return true;
		}
		if (e.key === 'ArrowUp') {
			e.preventDefault();
			selectedIndex = (selectedIndex - 1 + flatResults.length) % flatResults.length;
			scrollToSelected();
			return true;
		}
		if (e.key === 'Enter' || e.key === 'Tab') {
			e.preventDefault();
			if (flatResults[selectedIndex]) {
				onselect(flatResults[selectedIndex]);
			}
			return true;
		}
		if (e.key === 'Escape') {
			e.preventDefault();
			ondismiss();
			return true;
		}
		return false;
	}

	function scrollToSelected() {
		if (!popupEl) return;
		const item = popupEl.querySelector(`[data-index="${selectedIndex}"]`);
		item?.scrollIntoView({ block: 'nearest' });
	}
</script>

{#if visible}
	<div
		bind:this={popupEl}
		class="absolute bottom-full left-0 z-50 mb-1 max-h-[320px] w-[400px] overflow-y-auto rounded-xl border border-border bg-[#222] shadow-2xl"
	>
		{#if isLoading && results.length === 0}
			<div class="flex items-center gap-2 px-3 py-3 text-[12px] text-muted-foreground/60">
				<Icon icon={Loading03Icon} size={14} strokeWidth={1.5} class="animate-spin" />
				Searching...
			</div>
		{:else if results.length === 0}
			<div class="px-3 py-3 text-[12px] text-muted-foreground/40">
				No matching assets or files
			</div>
		{:else}
			{@const groups = groupedResults()}
			{#each [...groups.entries()] as [category, items]}
				<div class="px-3 pt-2 pb-0.5 text-[10px] font-medium uppercase tracking-wider text-muted-foreground/40">
					{category}
				</div>
				{#each items as item}
					{@const idx = flatResults.indexOf(item)}
					<button
						data-index={idx}
						class="flex w-full items-center gap-2.5 px-3 py-1.5 text-left text-[13px] transition-colors
							{idx === selectedIndex ? 'bg-accent/40 text-foreground' : 'text-foreground/80 hover:bg-accent/20'}"
						onclick={() => onselect(item)}
						onmouseenter={() => (selectedIndex = idx)}
					>
						<span class="flex h-4 w-4 shrink-0 items-center justify-center {categoryColor(item.type)}">
							<Icon icon={categoryIcon(item.type)} size={14} strokeWidth={1.5} />
						</span>
						<div class="min-w-0 flex-1">
							<div class="truncate font-medium">{item.name}</div>
							<div class="truncate text-[11px] text-muted-foreground/40">{item.path}</div>
						</div>
					</button>
				{/each}
			{/each}
		{/if}
	</div>
{/if}
