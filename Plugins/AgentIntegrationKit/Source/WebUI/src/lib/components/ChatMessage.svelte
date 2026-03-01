<script lang="ts">
	import { Streamdown } from 'svelte-streamdown';
	import Icon from '$lib/components/Icon.svelte';
	import ToolCallBlock from '$lib/components/ToolCallBlock.svelte';
	import { ArrowDown01Icon, ArrowUp01Icon, Alert02Icon } from '@hugeicons/core-free-icons';
	import type { ChatMessage, ContentBlock } from '$lib/bridge.js';
	import { openPath } from '$lib/bridge.js';

	let { message }: { message: ChatMessage } = $props();

	// ── Path detection for clickable code spans ─────────────────────
	// Matches UE asset paths, filesystem paths, and file:line patterns.

	const PATH_PATTERNS = [
		// UE asset paths: /Game/..., /Engine/..., /Script/...
		/^\/(?:Game|Engine|Script|Temp)\//,
		// Absolute filesystem paths
		/^\/(?:Users|home|var|tmp|opt|usr|etc)\//,
		/^[A-Z]:\\/,
		// Common source/project relative paths
		/^(?:Source|Plugins|Content|Config)\//
	];

	const FILE_EXTENSIONS = /\.(?:h|cpp|c|cs|py|js|ts|svelte|json|ini|txt|md|uasset|umap|uplugin|uproject|build\.cs)$/i;

	/** Check if a codespan looks like a clickable path */
	function isClickablePath(text: string): boolean {
		const clean = text.split(':')[0]; // strip :lineNumber
		if (PATH_PATTERNS.some((p) => p.test(clean))) return true;
		if (FILE_EXTENSIONS.test(clean)) return true;
		return false;
	}

	/** Parse path and optional line number from "path:line" format */
	function parsePath(text: string): { path: string; line: number } {
		const match = text.match(/^(.+?)(?::(\d+))?$/);
		if (match) {
			return { path: match[1], line: match[2] ? parseInt(match[2]) : 0 };
		}
		return { path: text, line: 0 };
	}

	function handlePathClick(text: string) {
		const { path, line } = parsePath(text);
		openPath(path, line);
	}

	let thoughtExpanded: Record<number, boolean> = $state({});

	function toggleThought(index: number) {
		thoughtExpanded[index] = !thoughtExpanded[index];
	}

	function wordCount(text: string): number {
		return text.split(/\s+/).filter(Boolean).length;
	}

	/** Find the matching tool_result block for a tool_call */
	function findToolResult(toolCallId: string | undefined): ContentBlock | undefined {
		if (!toolCallId) return undefined;
		return message.contentBlocks.find(
			(b) => b.type === 'tool_result' && b.toolCallId === toolCallId
		);
	}

	// ── Parent-child inference ───────────────────────────────────────
	// Works for both:
	//   - New sessions: explicit parentToolCallId from adapter
	//   - Old sessions: positional heuristic (tool_calls between a Task's
	//     tool_call and its tool_result are inferred as children)

	let parentChildMap = $derived.by(() => {
		const blocks = message.contentBlocks;
		// childId → parentId
		const childToParent: Record<string, string> = {};
		// parentId → [childIds]
		const parentToChildren: Record<string, string[]> = {};

		// Pass 1: collect explicit parentToolCallId links
		for (const b of blocks) {
			if (b.type === 'tool_call' && b.parentToolCallId && b.toolCallId) {
				childToParent[b.toolCallId] = b.parentToolCallId;
				if (!parentToChildren[b.parentToolCallId]) parentToChildren[b.parentToolCallId] = [];
				parentToChildren[b.parentToolCallId].push(b.toolCallId);
			}
		}

		// Pass 2: positional heuristic for old sessions without parentToolCallId
		// Find Task tool_calls and collect tool_calls between them and their tool_result
		for (let i = 0; i < blocks.length; i++) {
			const b = blocks[i];
			if (b.type !== 'tool_call' || b.toolName !== 'Task' || !b.toolCallId) continue;
			// Skip if this Task already has explicit children
			if (parentToChildren[b.toolCallId]?.length) continue;

			const taskId = b.toolCallId;
			// Look forward for tool_calls until we hit the Task's tool_result
			for (let j = i + 1; j < blocks.length; j++) {
				const next = blocks[j];
				// Stop at the Task's own result
				if (next.type === 'tool_result' && next.toolCallId === taskId) break;
				// Found a tool_call without an explicit parent → adopt it
				if (next.type === 'tool_call' && next.toolCallId && !next.parentToolCallId) {
					// Don't steal blocks already claimed by another parent
					if (childToParent[next.toolCallId]) continue;
					// Don't adopt duplicates of the parent itself
					if (next.toolCallId === taskId) continue;
					childToParent[next.toolCallId] = taskId;
					if (!parentToChildren[taskId]) parentToChildren[taskId] = [];
					parentToChildren[taskId].push(next.toolCallId);
				}
			}
		}

		return { childToParent, parentToChildren };
	});

	/** Get direct child tool_call blocks for a parent toolCallId */
	function getChildToolCalls(parentToolCallId: string | undefined): ContentBlock[] {
		if (!parentToolCallId) return [];
		const childIds = parentChildMap.parentToChildren[parentToolCallId];
		if (!childIds?.length) return [];
		return message.contentBlocks.filter(
			(b) => b.type === 'tool_call' && b.toolCallId && childIds.includes(b.toolCallId)
		);
	}

	/** Check if a tool_call block is a child — these are rendered nested, not at top level */
	function isChildBlock(block: ContentBlock): boolean {
		if (!block.toolCallId) return false;
		return !!parentChildMap.childToParent[block.toolCallId];
	}
</script>

<!-- Custom codespan snippet: makes paths clickable -->
{#snippet codespan({ children, token }: { children: import('svelte').Snippet; token: { text: string } })}
	{#if isClickablePath(token.text)}
		<button
			class="cursor-pointer rounded bg-muted px-1.5 py-0.5 font-mono text-[0.9em] text-blue-400 underline decoration-blue-400/30 transition-colors hover:text-blue-300 hover:decoration-blue-300/50"
			onclick={() => handlePathClick(token.text)}
		>
			{token.text}
		</button>
	{:else}
		{@render children()}
	{/if}
{/snippet}

{#if message.role === 'user'}
	<!-- User message — right-aligned bubble -->
	<div class="mb-4 flex justify-end">
		<div
			class="max-w-[70%] rounded-2xl rounded-br-md border border-border/50 bg-card px-4 py-2.5 text-[14px] text-card-foreground"
		>
			{message.contentBlocks[0]?.text ?? ''}
		</div>
	</div>
{:else if message.role === 'system'}
	<!-- System message — centered divider -->
	<div class="my-4 flex items-center gap-3">
		<div class="h-px flex-1 bg-border/40"></div>
		<span class="text-[12px] italic text-muted-foreground/60">
			{message.contentBlocks[0]?.text ?? ''}
		</span>
		<div class="h-px flex-1 bg-border/40"></div>
	</div>
{:else}
	<!-- Assistant message — left-aligned, renders content blocks -->
	<div class="mb-4 flex justify-start">
		<div class="w-full max-w-[85%] min-w-0">
			{#each message.contentBlocks as block, i}
				{#if block.type === 'text'}
					<!-- Text block with streaming markdown -->
					<div class="max-w-none text-[14px] leading-relaxed text-foreground">
						<Streamdown
							content={block.text}
							baseTheme="shadcn"
							parseIncompleteMarkdown={block.isStreaming ?? false}
							class="text-[14px] leading-relaxed"
							shikiTheme="github-dark"
							{codespan}
						/>
											</div>
				{:else if block.type === 'thought'}
					<!-- Thinking block — collapsible -->
					<div class="my-2 rounded-lg border border-border/60 bg-secondary/25">
						<button
							class="flex w-full items-center gap-2 px-3 py-1.5 text-left text-[12px] text-muted-foreground transition-colors hover:text-foreground"
							onclick={() => toggleThought(i)}
						>
							{#if block.isStreaming}
								<span
									class="inline-block h-3 w-3 animate-spin rounded-full border-2 border-muted-foreground/30 border-t-muted-foreground"
								></span>
								<span>Thinking…</span>
							{:else}
								<Icon
									icon={thoughtExpanded[i] ? ArrowUp01Icon : ArrowDown01Icon}
									size={12}
									strokeWidth={1.5}
								/>
								<span>Thought</span>
								<span class="text-muted-foreground/40">({wordCount(block.text)} words)</span>
							{/if}
						</button>
						{#if thoughtExpanded[i] || block.isStreaming}
							<div
								class="border-t border-border/20 px-3 py-2 text-[13px] italic leading-relaxed text-muted-foreground/70"
							>
								{block.text}
								{#if block.isStreaming}
									<span
										class="hidden"
									></span>
								{/if}
							</div>
						{/if}
					</div>
				{:else if block.type === 'tool_call'}
					<!-- Tool call block — only render top-level (non-child) tool calls -->
					{#if !isChildBlock(block)}
						<ToolCallBlock
							{block}
							resultBlock={findToolResult(block.toolCallId)}
							childBlocks={getChildToolCalls(block.toolCallId)}
							allBlocks={message.contentBlocks}
						/>
					{/if}
				{:else if block.type === 'tool_result'}
					<!-- Tool results are rendered as part of their paired tool_call — skip standalone -->
				{:else if block.type === 'error'}
					<!-- Error block -->
					<div
						class="my-2 flex items-start gap-2 rounded-lg border border-red-500/20 bg-red-500/5 px-3 py-2 text-[13px] text-red-400"
					>
						<Icon icon={Alert02Icon} size={16} strokeWidth={1.5} class="mt-0.5 shrink-0" />
						<span>{block.text}</span>
					</div>
				{:else if block.type === 'system'}
					<!-- Inline system status (compaction, etc.) -->
					{#if block.systemStatus === 'compacting'}
						<div class="my-3 flex items-center gap-3">
							<div class="h-px flex-1 bg-border/30"></div>
							<div class="flex items-center gap-2 text-[12px] text-muted-foreground/60">
								<span class="inline-block h-1.5 w-1.5 rounded-full bg-muted-foreground/40 animate-pulse"></span>
								<span class="compacting-shimmer">{block.text}</span>
							</div>
							<div class="h-px flex-1 bg-border/30"></div>
						</div>
					{:else}
						<div class="my-3 flex items-center gap-3">
							<div class="h-px flex-1 bg-border/30"></div>
							<span class="text-[12px] text-muted-foreground/50">{block.text}</span>
							<div class="h-px flex-1 bg-border/30"></div>
						</div>
					{/if}
				{/if}
			{/each}
			<!-- Streaming indicator -->
			{#if message.isStreaming}
				<div class="mt-2 flex items-center gap-2 text-[12px]">
					<span class="generating-shimmer">Generating</span>
				</div>
			{/if}
		</div>
	</div>
{/if}
