import {
  Agent,
  AgentSideConnection,
  AuthenticateRequest,
  AvailableCommand,
  CancelNotification,
  ClientCapabilities,
  ForkSessionRequest,
  ForkSessionResponse,
  InitializeRequest,
  InitializeResponse,
  LoadSessionRequest,
  LoadSessionResponse,
  ListSessionsRequest,
  ListSessionsResponse,
  ndJsonStream,
  NewSessionRequest,
  NewSessionResponse,
  PromptRequest,
  PromptResponse,
  ReadTextFileRequest,
  ReadTextFileResponse,
  RequestError,
  ResumeSessionRequest,
  ResumeSessionResponse,
  SessionInfo,
  SessionModelState,
  SessionNotification,
  SetSessionModelRequest,
  SessionConfigOption,
  SetSessionConfigOptionRequest,
  SetSessionConfigOptionResponse,
  SetSessionModelResponse,
  SetSessionModeRequest,
  SetSessionModeResponse,
  TerminalHandle,
  TerminalOutputResponse,
  WriteTextFileRequest,
  WriteTextFileResponse,
} from "@agentclientprotocol/sdk";
import { SettingsManager } from "./settings.js";
import {
  CanUseTool,
  McpServerConfig,
  ModelInfo,
  Options,
  PermissionMode,
  Query,
  query,
  SDKPartialAssistantMessage,
  SDKUserMessage,
  SlashCommand,
} from "@anthropic-ai/claude-agent-sdk";
import * as fs from "node:fs";
import * as path from "node:path";
import * as readline from "node:readline";
import * as os from "node:os";
import {
  encodeProjectPath,
  normalizeProjectCwd,
  nodeToWebReadable,
  nodeToWebWritable,
  Pushable,
  unreachable,
} from "./utils.js";
import { createMcpServer } from "./mcp-server.js";
import { EDIT_TOOL_NAMES, acpToolNames } from "./tools.js";
import {
  toolInfoFromToolUse,
  planEntries,
  toolUpdateFromToolResult,
  ClaudePlanEntry,
  registerHookCallback,
  createPostToolUseHook,
  createPreToolUseHook,
} from "./tools.js";
import { ContentBlockParam } from "@anthropic-ai/sdk/resources";
import { BetaContentBlock, BetaRawContentBlockDelta } from "@anthropic-ai/sdk/resources/beta.mjs";
import packageJson from "../package.json" with { type: "json" };
import { randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";

export const CLAUDE_CONFIG_DIR =
  process.env.CLAUDE_CONFIG_DIR ?? path.join(os.homedir(), ".claude");

function sessionFilePath(cwd: string, sessionId: string): string {
  return path.join(CLAUDE_CONFIG_DIR, "projects", encodeProjectPath(cwd), `${sessionId}.jsonl`);
}

const MAX_TITLE_LENGTH = 128;

function sanitizeTitle(text: string): string {
  // Replace newlines and collapse whitespace
  const sanitized = text
    .replace(/[\r\n]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  if (sanitized.length <= MAX_TITLE_LENGTH) {
    return sanitized;
  }
  return sanitized.slice(0, MAX_TITLE_LENGTH - 1) + "…";
}

/**
 * Logger interface for customizing logging output
 */
export interface Logger {
  log: (...args: any[]) => void;
  error: (...args: any[]) => void;
}

type Session = {
  query: Query;
  input: Pushable<SDKUserMessage>;
  cancelled: boolean;
  permissionMode: PermissionMode;
  settingsManager: SettingsManager;
  /** Context window size in tokens (updated from modelUsage on result) */
  contextWindow: number;
  /** Cumulative session cost in USD across all prompt turns */
  cumulativeCostUSD: number;
  /** Track processed message IDs to deduplicate usage (same ID = same API call) */
  processedMessageIds: Set<string>;
  /** Last known context window usage from the most recent assistant message (per API call, not cumulative) */
  lastContextUsed: number;
};

type SessionHistoryEntry = {
  type?: string;
  subtype?: string;
  content?: string;
  isSidechain?: boolean;
  isMeta?: boolean;
  isCompactSummary?: boolean;
  sessionId?: string;
  parent_tool_use_id?: string | null;
  compactMetadata?: { trigger?: string; preTokens?: number };
  microcompactMetadata?: { trigger?: string; preTokens?: number; tokensSaved?: number };
  message?: {
    role?: string;
    content?: unknown;
    model?: string;
  };
};

type BackgroundTerminal =
  | {
      handle: TerminalHandle;
      status: "started";
      lastOutput: TerminalOutputResponse | null;
    }
  | {
      status: "aborted" | "exited" | "killed" | "timedOut";
      pendingOutput: TerminalOutputResponse;
    };

/**
 * Extra metadata that can be given to Claude Code when creating a new session.
 */
export type NewSessionMeta = {
  claudeCode?: {
    /**
     * Options forwarded to Claude Code when starting a new session.
     * Those parameters will be ignored and managed by ACP:
     *   - cwd
     *   - includePartialMessages
     *   - allowDangerouslySkipPermissions
     *   - permissionMode
     *   - canUseTool
     *   - executable
     * Those parameters will be used and updated to work with ACP:
     *   - hooks (merged with ACP's hooks)
     *   - mcpServers (merged with ACP's mcpServers)
     *   - disallowedTools (merged with ACP's disallowedTools)
     */
    options?: Options;
  };
};

/**
 * Extra metadata that the agent provides for each tool_call / tool_update update.
 */
export type ToolUpdateMeta = {
  claudeCode?: {
    /* The name of the tool that was used in Claude Code. */
    toolName: string;
    /* The structured output provided by Claude Code. */
    toolResponse?: unknown;
    /* If this tool call was made inside a subagent (Task), the parent Task's toolCallId. */
    parentToolCallId?: string | null;
  };
};

export type ToolUseCache = {
  [key: string]: {
    type: "tool_use" | "server_tool_use" | "mcp_tool_use";
    id: string;
    name: string;
    input: unknown;
  };
};

// Bypass Permissions doesn't work if we are a root/sudo user
const IS_ROOT = (process.geteuid?.() ?? process.getuid?.()) === 0;
const ALLOW_BYPASS = !IS_ROOT || !!process.env.IS_SANDBOX;
const MIN_THINKING_BUDGET_TOKENS = 1024;

// Implement the ACP Agent interface
export class ClaudeAcpAgent implements Agent {
  sessions: {
    [key: string]: Session;
  };
  client: AgentSideConnection;
  toolUseCache: ToolUseCache;
  backgroundTerminals: { [key: string]: BackgroundTerminal } = {};
  clientCapabilities?: ClientCapabilities;
  logger: Logger;
  /** Current effort level — applied at query creation. "off" means thinking disabled. */
  effortLevel: "off" | "low" | "medium" | "high" | "max" = "high";

  constructor(client: AgentSideConnection, logger?: Logger) {
    this.sessions = {};
    this.client = client;
    this.toolUseCache = {};
    this.logger = logger ?? console;
  }

  async initialize(request: InitializeRequest): Promise<InitializeResponse> {
    this.clientCapabilities = request.clientCapabilities;

    // Default authMethod
    const authMethod: any = {
      description: "Run `claude /login` in the terminal",
      name: "Log in with Claude Code",
      id: "claude-login",
    };

    // If client supports terminal-auth capability, use that instead.
    if (request.clientCapabilities?._meta?.["terminal-auth"] === true) {
      authMethod._meta = {
        "terminal-auth": {
          command: "claude-internal",
          args: ["/login"],
          label: "Claude Code Login",
        },
      };
    }

    return {
      protocolVersion: 1,
      agentCapabilities: {
        promptCapabilities: {
          image: true,
          embeddedContext: true,
        },
        mcpCapabilities: {
          http: true,
          sse: true,
        },
        loadSession: true,
        sessionCapabilities: {
          fork: {},
          list: {},
          resume: {},
        },
      },
      agentInfo: {
        name: packageJson.name,
        title: "Claude Code",
        version: packageJson.version,
      },
      authMethods: [authMethod],
    };
  }

  async newSession(params: NewSessionRequest): Promise<NewSessionResponse> {
    if (
      fs.existsSync(path.resolve(os.homedir(), ".claude.json.backup")) &&
      !fs.existsSync(path.resolve(os.homedir(), ".claude.json"))
    ) {
      throw RequestError.authRequired();
    }

    const response = await this.createSession(params, {
      // Revisit these meta values once we support resume
      resume: (params._meta as NewSessionMeta | undefined)?.claudeCode?.options?.resume,
    });
    // Needs to happen after we return the session
    setTimeout(() => {
      this.sendAvailableCommandsUpdate(response.sessionId);
    }, 0);
    return response;
  }

  async unstable_forkSession(params: ForkSessionRequest): Promise<ForkSessionResponse> {
    const response = await this.createSession(
      {
        cwd: params.cwd,
        mcpServers: params.mcpServers ?? [],
        _meta: params._meta,
      },
      {
        resume: params.sessionId,
        forkSession: true,
      },
    );
    // Needs to happen after we return the session
    setTimeout(() => {
      this.sendAvailableCommandsUpdate(response.sessionId);
    }, 0);
    return response;
  }

  async unstable_resumeSession(params: ResumeSessionRequest): Promise<ResumeSessionResponse> {
    const response = await this.createSession(
      {
        cwd: params.cwd,
        mcpServers: params.mcpServers ?? [],
        _meta: params._meta,
      },
      {
        resume: params.sessionId,
      },
    );
    // Needs to happen after we return the session
    setTimeout(() => {
      this.sendAvailableCommandsUpdate(response.sessionId);
    }, 0);
    return response;
  }

  /**
   * Find a session file by ID, first checking the given cwd's project directory,
   * then falling back to scanning all project directories.
   * Returns the absolute file path if found, or null if not found.
   */
  private async findSessionFile(sessionId: string, cwd: string): Promise<string | null> {
    const fileName = `${sessionId}.jsonl`;

    // Fast path: check the expected location based on cwd
    const expectedPath = sessionFilePath(cwd, sessionId);
    try {
      await fs.promises.access(expectedPath);
      return expectedPath;
    } catch {
      // Not found at expected path, scan all project directories
    }

    const claudeDir = path.join(CLAUDE_CONFIG_DIR, "projects");
    try {
      const projectDirs = await fs.promises.readdir(claudeDir);
      for (const encodedPath of projectDirs) {
        const projectDir = path.join(claudeDir, encodedPath);
        const stat = await fs.promises.stat(projectDir);
        if (!stat.isDirectory()) continue;

        const candidatePath = path.join(projectDir, fileName);
        try {
          await fs.promises.access(candidatePath);
          return candidatePath;
        } catch {
          continue;
        }
      }
    } catch {
      // projects directory doesn't exist or isn't readable
    }

    return null;
  }

  async loadSession(params: LoadSessionRequest): Promise<LoadSessionResponse> {
    const filePath = await this.findSessionFile(params.sessionId, params.cwd);
    if (!filePath) {
      throw new Error("Session not found");
    }

    const response = await this.createSession(
      {
        cwd: params.cwd,
        mcpServers: params.mcpServers ?? [],
        _meta: params._meta,
      },
      {
        resume: params.sessionId,
      },
    );

    await this.replaySessionHistory(params.sessionId, filePath);

    // Send available commands after replay so it doesn't interleave with history
    setTimeout(() => {
      this.sendAvailableCommandsUpdate(params.sessionId);
    }, 0);

    return {
      modes: response.modes,
      models: response.models,
    };
  }

  /**
   * List Claude Code sessions by parsing JSONL files
   * Sessions are stored in ~/.claude/projects/<path-encoded>/
   * Implements the draft session/list RFD spec
   */
  async unstable_listSessions(params: ListSessionsRequest): Promise<ListSessionsResponse> {
    // Note: We load all sessions into memory for sorting, so pagination here is for
    // API response size limits rather than memory efficiency. This matches the RFD spec.
    const PAGE_SIZE = 50;
    const claudeDir = path.join(CLAUDE_CONFIG_DIR, "projects");

    try {
      await fs.promises.access(claudeDir);
    } catch {
      this.logger.error(`[listSessions] Claude projects dir not accessible: ${claudeDir}`);
      return { sessions: [] };
    }

    // Collect all sessions across all project directories
    const allSessions: SessionInfo[] = [];
    const normalizedCwd = params.cwd ? normalizeProjectCwd(params.cwd) : null;
    const encodedCwdFilter = normalizedCwd ? encodeProjectPath(normalizedCwd) : null;
    const isWindowsCwd = normalizedCwd ? /^[A-Z]:\//.test(normalizedCwd) : false;

    this.logger.error(`[listSessions] claudeDir=${claudeDir}, params.cwd=${params.cwd}, normalizedCwd=${normalizedCwd}, encodedFilter=${encodedCwdFilter}`);

    try {
      const projectDirs = await fs.promises.readdir(claudeDir);
      this.logger.error(`[listSessions] Found ${projectDirs.length} project dirs`);

      // Prefer encoded-folder matches when available (fast path), but if no match
      // is found, fall back to scanning all folders and rely on per-entry cwd checks.
      const projectDirsToScan = (() => {
        if (!encodedCwdFilter) {
          return projectDirs;
        }
        const matches = projectDirs.filter((dir) => {
          if (dir === encodedCwdFilter) {
            return true;
          }
          if (isWindowsCwd) {
            return dir.toLowerCase() === encodedCwdFilter.toLowerCase();
          }
          return false;
        });
        return matches.length > 0 ? matches : projectDirs;
      })();

      for (const encodedPath of projectDirsToScan) {
        const projectDir = path.join(claudeDir, encodedPath);
        const stat = await fs.promises.stat(projectDir);
        if (!stat.isDirectory()) continue;
        this.logger.error(`[listSessions] Matched project dir: ${encodedPath}`);

        const files = await fs.promises.readdir(projectDir);
        // Filter to user session files only. Skip agent-*.jsonl files which contain
        // internal agent metadata and system logs, not user-visible conversation sessions.
        const jsonlFiles = files.filter((f) => f.endsWith(".jsonl") && !f.startsWith("agent-"));
        this.logger.error(`[listSessions] Found ${jsonlFiles.length} session files in ${encodedPath}`);

        for (const file of jsonlFiles) {
          const filePath = path.join(projectDir, file);
          try {
            const content = await fs.promises.readFile(filePath, "utf-8");
            const lines = content.trim().split("\n").filter(Boolean);

            const sessionId = file.replace(".jsonl", "");
            let parsedAnyEntry = false;
            let sessionCwd: string | undefined;

            // Find first user message for title
            let title: string | undefined;
            for (const line of lines) {
              try {
                const entry = JSON.parse(line);
                parsedAnyEntry = true;
                if (entry.isSidechain === true) {
                  continue;
                }
                const entrySessionId =
                  typeof entry.sessionId === "string" ? entry.sessionId : undefined;
                if (typeof entry.sessionId === "string" && entry.sessionId !== entrySessionId) {
                  continue;
                }
                if (typeof entry.cwd === "string") {
                  sessionCwd = entry.cwd;
                }
                if (!title && entry.type === "user" && entry.message?.content) {
                  const msgContent = entry.message.content;
                  if (typeof msgContent === "string") {
                    title = sanitizeTitle(msgContent);
                  }
                  if (Array.isArray(msgContent) && msgContent.length > 0) {
                    const first = msgContent[0];
                    const text =
                      typeof first === "string"
                        ? first
                        : first && typeof first === "object" && typeof first.text === "string"
                          ? first.text
                          : undefined;
                    if (text) {
                      title = sanitizeTitle(text);
                    }
                  }
                }

                // Continue scanning until we have both fields, since cwd can appear
                // in later entries even after the first user title-bearing message.
                if (title && sessionCwd) {
                  break;
                }
              } catch {
                // Skip malformed lines
              }
            }
            if (!parsedAnyEntry) {
              this.logger.error(`[listSessions] ${sessionId}: no parseable entries, skipping`);
              continue;
            }

            // SessionInfo.cwd is currently required. For entries that do not
            // include an explicit cwd in the session JSONL (typically metadata-only files),
            // we skip them instead of decoding folder names because path encoding is lossy.
            if (!sessionCwd) {
              this.logger.error(`[listSessions] ${sessionId}: no cwd found, skipping`);
              continue;
            }

            // Path encoding is lossy. Always verify against normalized entry cwd.
            const normalizedSessionCwd = normalizeProjectCwd(sessionCwd);
            if (normalizedCwd && normalizedSessionCwd !== normalizedCwd) {
              this.logger.error(`[listSessions] ${sessionId}: cwd mismatch: session="${normalizedSessionCwd}" vs filter="${normalizedCwd}", skipping`);
              continue;
            }

            // Get file modification time as updatedAt
            const fileStat = await fs.promises.stat(filePath);
            const updatedAt = fileStat.mtime.toISOString();

            allSessions.push({
              sessionId,
              cwd: sessionCwd,
              title: title ?? null,
              updatedAt,
            });
          } catch (err) {
            this.logger.error(
              `[unstable_listSessions] Failed to parse session file: ${filePath}`,
              err,
            );
          }
        }
      }
    } catch (err) {
      this.logger.error("[listSessions] Failed to list sessions", err);
      return { sessions: [] };
    }

    this.logger.error(`[listSessions] Total sessions found: ${allSessions.length}`);

    // Sort by updatedAt descending (most recent first)
    allSessions.sort((a, b) => {
      const timeA = a.updatedAt ? new Date(a.updatedAt).getTime() : 0;
      const timeB = b.updatedAt ? new Date(b.updatedAt).getTime() : 0;
      return timeB - timeA;
    });

    // Handle pagination with cursor
    let startIndex = 0;
    if (params.cursor) {
      try {
        const decoded = Buffer.from(params.cursor, "base64").toString("utf-8");
        const cursorData = JSON.parse(decoded);
        startIndex = cursorData.offset ?? 0;
      } catch {
        // Invalid cursor, start from beginning
      }
    }

    const pageOfSessions = allSessions.slice(startIndex, startIndex + PAGE_SIZE);
    const hasMore = startIndex + PAGE_SIZE < allSessions.length;

    const response: ListSessionsResponse = {
      sessions: pageOfSessions,
    };

    if (hasMore) {
      const nextCursor = Buffer.from(JSON.stringify({ offset: startIndex + PAGE_SIZE })).toString(
        "base64",
      );
      response.nextCursor = nextCursor;
    }

    return response;
  }

  async authenticate(_params: AuthenticateRequest): Promise<void> {
    throw new Error("Method not implemented.");
  }

  async prompt(params: PromptRequest): Promise<PromptResponse> {
    if (!this.sessions[params.sessionId]) {
      throw new Error("Session not found");
    }

    this.sessions[params.sessionId].cancelled = false;

    const { query, input } = this.sessions[params.sessionId];

    input.push(promptToClaude(params));
    while (true) {
      const { value: message, done } = await query.next();
      if (done || !message) {
        if (this.sessions[params.sessionId].cancelled) {
          return { stopReason: "cancelled" };
        }
        break;
      }

      switch (message.type) {
        case "system":
          switch (message.subtype) {
            case "init": {
              // Log MCP server connection status for diagnostics
              if (message.mcp_servers && message.mcp_servers.length > 0) {
                for (const server of message.mcp_servers) {
                  this.logger.log(`[init] MCP server "${server.name}": ${server.status}`);
                }
              }
              this.logger.log(
                `[init] Claude Code ${message.claude_code_version}, model: ${message.model}, ` +
                `tools: ${message.tools?.length ?? 0}, MCP servers: ${message.mcp_servers?.length ?? 0}`
              );

              // Forward MCP diagnostics to ACP client so the plugin can display status
              const mcpStatus = (message.mcp_servers ?? []).map((s: { name: string; status: string }) => ({
                name: s.name,
                status: s.status,
              }));
              const failedServers = mcpStatus.filter((s: { status: string }) => s.status !== "connected");
              if (failedServers.length > 0) {
                this.logger.error(
                  `[init] WARNING: ${failedServers.length} MCP server(s) failed to connect: ` +
                  failedServers.map((s: { name: string; status: string }) => `${s.name} (${s.status})`).join(", ")
                );
              }
              break;
            }
            case "compact_boundary": {
              // Notify client that compaction occurred (context was reduced)
              const preTokens = message.compact_metadata?.pre_tokens ?? 0;
              const trigger = message.compact_metadata?.trigger ?? "auto";
              this.logger.log(
                `[usage] Compaction completed (${trigger}), pre_tokens: ${preTokens}`,
              );

              // Send compaction complete notification with token details
              await this.client.sessionUpdate({
                sessionId: params.sessionId,
                update: {
                  sessionUpdate: "agent_message_chunk",
                  _meta: {
                    systemStatus: "compacted",
                    preTokens,
                    trigger,
                  },
                  content: {
                    type: "text",
                    text: `Context compacted (${trigger}): ${preTokens > 0 ? Math.round(preTokens / 1000) + "K tokens before" : "completed"}`,
                  },
                },
              });
              break;
            }
            case "status": {
              // Forward compaction status to UI
              if (message.status === "compacting") {
                await this.client.sessionUpdate({
                  sessionId: params.sessionId,
                  update: {
                    sessionUpdate: "agent_message_chunk",
                    _meta: { systemStatus: "compacting" },
                    content: { type: "text", text: "Compacting context window..." },
                  },
                });
              } else if (message.status === null) {
                // Status cleared — compaction finished (compact_boundary follows with details)
              }
              break;
            }
            case "hook_started":
            case "task_notification":
            case "hook_progress":
            case "hook_response":
            case "files_persisted":
              // Todo: process via status api: https://docs.claude.com/en/docs/claude-code/hooks#hook-output
              break;
            default:
              unreachable(message, this.logger);
              break;
          }
          break;
        case "result": {
          if (this.sessions[params.sessionId].cancelled) {
            return { stopReason: "cancelled" };
          }

          // Send final usage update with cost and per-model breakdown
          {
            const session = this.sessions[params.sessionId];
            if (session) {
              session.cumulativeCostUSD += message.total_cost_usd;

              // Build per-model breakdown and update context window
              const modelBreakdown: Record<string, unknown> = {};
              for (const [modelName, mu] of Object.entries(message.modelUsage)) {
                if (mu.contextWindow > 0) {
                  session.contextWindow = mu.contextWindow;
                }
                modelBreakdown[modelName] = {
                  inputTokens: mu.inputTokens,
                  outputTokens: mu.outputTokens,
                  cacheReadTokens: mu.cacheReadInputTokens,
                  cacheCreationTokens: mu.cacheCreationInputTokens,
                  costUSD: mu.costUSD,
                  contextWindow: mu.contextWindow,
                  maxOutputTokens: mu.maxOutputTokens,
                  webSearchRequests: mu.webSearchRequests,
                };
              }

              // result.usage is CUMULATIVE across all agentic iterations — NOT the current context size.
              // Use lastContextUsed from the most recent assistant message for accurate context window fill.
              const u = message.usage;
              await this.client.sessionUpdate({
                sessionId: params.sessionId,
                update: {
                  sessionUpdate: "usage_update",
                  used: session.lastContextUsed,
                  size: session.contextWindow,
                  cost: {
                    amount: session.cumulativeCostUSD,
                    currency: "USD",
                  },
                  _meta: {
                    // These are cumulative totals for the whole prompt turn (for display in tooltip)
                    inputTokens: u.input_tokens,
                    outputTokens: u.output_tokens,
                    cacheReadTokens: u.cache_read_input_tokens ?? 0,
                    cacheCreationTokens: u.cache_creation_input_tokens ?? 0,
                    totalCostUSD: session.cumulativeCostUSD,
                    turnCostUSD: message.total_cost_usd,
                    numTurns: message.num_turns,
                    durationMs: message.duration_ms,
                    modelUsage: modelBreakdown,
                  },
                },
              });
            }
          }

          switch (message.subtype) {
            case "success": {
              if (message.result.includes("Please run /login")) {
                throw RequestError.authRequired();
              }
              if (message.is_error) {
                throw RequestError.internalError(undefined, message.result);
              }
              return { stopReason: "end_turn" };
            }
            case "error_during_execution":
              if (message.is_error) {
                throw RequestError.internalError(
                  undefined,
                  message.errors.join(", ") || message.subtype,
                );
              }
              return { stopReason: "end_turn" };
            case "error_max_budget_usd":
            case "error_max_turns":
            case "error_max_structured_output_retries":
              if (message.is_error) {
                throw RequestError.internalError(
                  undefined,
                  message.errors.join(", ") || message.subtype,
                );
              }
              return { stopReason: "max_turn_requests" };
            default:
              unreachable(message, this.logger);
              break;
          }
          break;
        }
        case "stream_event": {
          for (const notification of streamEventToAcpNotifications(
            message,
            params.sessionId,
            this.toolUseCache,
            this.client,
            this.logger,
          )) {
            await this.client.sessionUpdate(notification);
          }
          break;
        }
        case "user":
        case "assistant": {
          if (this.sessions[params.sessionId].cancelled) {
            break;
          }

          // Slash commands like /compact can generate invalid output... doesn't match
          // their own docs: https://docs.anthropic.com/en/docs/claude-code/sdk/sdk-slash-commands#%2Fcompact-compact-conversation-history
          if (
            typeof message.message.content === "string" &&
            message.message.content.includes("<local-command-stdout>")
          ) {
            // Handle /context by sending its reply as regular agent message.
            if (message.message.content.includes("Context Usage")) {
              for (const notification of toAcpNotifications(
                message.message.content
                  .replace("<local-command-stdout>", "")
                  .replace("</local-command-stdout>", ""),
                "assistant",
                params.sessionId,
                this.toolUseCache,
                this.client,
                this.logger,
                { parentToolCallId: message.parent_tool_use_id },
              )) {
                await this.client.sessionUpdate(notification);
              }
            }
            this.logger.log(message.message.content);
            break;
          }

          if (
            typeof message.message.content === "string" &&
            message.message.content.includes("<local-command-stderr>")
          ) {
            this.logger.error(message.message.content);
            break;
          }
          // Skip these user messages for now, since they seem to just be messages we don't want in the feed
          if (
            message.type === "user" &&
            (typeof message.message.content === "string" ||
              (Array.isArray(message.message.content) &&
                message.message.content.length === 1 &&
                message.message.content[0].type === "text"))
          ) {
            break;
          }

          if (
            message.type === "assistant" &&
            message.message.model === "<synthetic>" &&
            Array.isArray(message.message.content) &&
            message.message.content.length === 1 &&
            message.message.content[0].type === "text" &&
            message.message.content[0].text.includes("Please run /login")
          ) {
            throw RequestError.authRequired();
          }

          const content =
            message.type === "assistant"
              ? // Handled by stream events above
                message.message.content.filter((item) => !["text", "thinking"].includes(item.type))
              : message.message.content;

          for (const notification of toAcpNotifications(
            content,
            message.message.role,
            params.sessionId,
            this.toolUseCache,
            this.client,
            this.logger,
            { parentToolCallId: message.parent_tool_use_id },
          )) {
            await this.client.sessionUpdate(notification);
          }

          // Send usage update from assistant message (deduplicated by message ID)
          if (message.type === "assistant" && message.message.usage) {
            const session = this.sessions[params.sessionId];
            const msgId = message.message.id;
            if (session && !session.processedMessageIds.has(msgId)) {
              session.processedMessageIds.add(msgId);
              const u = message.message.usage;
              // Total context consumed = non-cached + cache read + cache creation
              // This is per-API-call (not cumulative), so it's the actual prompt size
              const contextUsed = u.input_tokens
                + (u.cache_read_input_tokens ?? 0)
                + (u.cache_creation_input_tokens ?? 0);
              session.lastContextUsed = contextUsed;
              await this.client.sessionUpdate({
                sessionId: params.sessionId,
                update: {
                  sessionUpdate: "usage_update",
                  used: contextUsed,
                  size: session.contextWindow,
                  ...(session.cumulativeCostUSD > 0 && {
                    cost: { amount: session.cumulativeCostUSD, currency: "USD" },
                  }),
                  _meta: {
                    inputTokens: u.input_tokens,
                    outputTokens: u.output_tokens,
                    cacheReadTokens: u.cache_read_input_tokens ?? 0,
                    cacheCreationTokens: u.cache_creation_input_tokens ?? 0,
                  },
                },
              });
            }
          }
          break;
        }
        case "tool_progress":
        case "tool_use_summary":
          break;
        case "auth_status":
          break;
        default:
          unreachable(message);
          break;
      }
    }
    throw new Error("Session did not end in result");
  }

  async cancel(params: CancelNotification): Promise<void> {
    if (!this.sessions[params.sessionId]) {
      throw new Error("Session not found");
    }
    this.sessions[params.sessionId].cancelled = true;
    await this.sessions[params.sessionId].query.interrupt();
  }

  async unstable_setSessionModel(
    params: SetSessionModelRequest,
  ): Promise<SetSessionModelResponse | void> {
    if (!this.sessions[params.sessionId]) {
      throw new Error("Session not found");
    }
    await this.sessions[params.sessionId].query.setModel(params.modelId);
  }

  async setSessionConfigOption(
    params: SetSessionConfigOptionRequest,
  ): Promise<SetSessionConfigOptionResponse> {
    const session = this.sessions[params.sessionId];
    if (!session) {
      throw new Error("Session not found");
    }

    if (params.configId === "thinking") {
      const validLevels = ["off", "low", "medium", "high", "max"];
      const level = validLevels.includes(params.value) ? params.value : "high";

      // Store effort level for next query creation
      this.effortLevel = level as typeof this.effortLevel;

      // Toggle thinking on/off immediately via deprecated but functional API
      if (level === "off") {
        await session.query.setMaxThinkingTokens(0); // disables thinking
      } else {
        // Anthropic rejects enabled thinking budgets below 1024.
        await session.query.setMaxThinkingTokens(MIN_THINKING_BUDGET_TOKENS);
      }
      this.logger.log(`[effort] Set effort to '${level}' (immediate thinking ${level === "off" ? "disabled" : "enabled"}, effort applies next conversation)`);

      const configOptions = [buildEffortConfigOption(level)];
      return { configOptions };
    }

    throw new Error(`Unknown config option: ${params.configId}`);
  }

  async setSessionMode(params: SetSessionModeRequest): Promise<SetSessionModeResponse> {
    if (!this.sessions[params.sessionId]) {
      throw new Error("Session not found");
    }

    switch (params.modeId) {
      case "default":
      case "acceptEdits":
      case "bypassPermissions":
      case "dontAsk":
      case "plan":
        this.sessions[params.sessionId].permissionMode = params.modeId;
        try {
          await this.sessions[params.sessionId].query.setPermissionMode(params.modeId);
        } catch (error) {
          const errorMessage =
            error instanceof Error && error.message ? error.message : "Invalid Mode";

          throw new Error(errorMessage);
        }
        return {};
      default:
        throw new Error("Invalid Mode");
    }
  }

  private async replaySessionHistory(sessionId: string, filePath: string): Promise<void> {
    const toolUseCache: ToolUseCache = {};
    const stream = fs.createReadStream(filePath, { encoding: "utf-8" });
    const reader = readline.createInterface({ input: stream, crlfDelay: Infinity });

    try {
      for await (const line of reader) {
        const trimmed = line.trim();
        if (!trimmed) {
          continue;
        }

        let entry: SessionHistoryEntry;
        try {
          entry = JSON.parse(trimmed) as SessionHistoryEntry;
        } catch {
          continue;
        }

        // Skip internal meta messages (local command caveats, etc.)
        if (entry.isMeta) {
          continue;
        }

        // Handle system events (compaction markers) — replay them as systemStatus updates
        // so the UI renders proper dividers instead of dropping them silently
        if (entry.type === "system") {
          if (entry.subtype === "compact_boundary") {
            const preTokens = entry.compactMetadata?.preTokens ?? 0;
            const trigger = entry.compactMetadata?.trigger ?? "auto";
            await this.client.sessionUpdate({
              sessionId,
              update: {
                sessionUpdate: "agent_message_chunk",
                _meta: { systemStatus: "compacted", preTokens, trigger },
                content: {
                  type: "text",
                  text: `Context compacted (${trigger})${preTokens > 0 ? `: ${Math.round(preTokens / 1000)}K tokens before` : ""}`,
                },
              },
            });
          } else if (entry.subtype === "microcompact_boundary") {
            const tokensSaved = entry.microcompactMetadata?.tokensSaved ?? 0;
            await this.client.sessionUpdate({
              sessionId,
              update: {
                sessionUpdate: "agent_message_chunk",
                _meta: { systemStatus: "compacted" },
                content: {
                  type: "text",
                  text: `Context optimized${tokensSaved > 0 ? ` (${Math.round(tokensSaved / 1000)}K tokens freed)` : ""}`,
                },
              },
            });
          }
          continue;
        }

        if (entry.type !== "user" && entry.type !== "assistant") {
          continue;
        }

        if (entry.isSidechain) {
          continue;
        }

        if (entry.sessionId && entry.sessionId !== sessionId) {
          continue;
        }

        // Compact summaries are internal context for the model — the compact_boundary
        // above already shows the divider, so skip the summary user message
        if (entry.isCompactSummary) {
          continue;
        }

        const message = entry.message;
        if (!message) {
          continue;
        }

        const role =
          message.role === "assistant" ? "assistant" : message.role === "user" ? "user" : null;
        if (!role) {
          continue;
        }

        const content = message.content;
        if (typeof content !== "string" && !Array.isArray(content)) {
          continue;
        }

        // Skip internal CLI command artifacts (slash commands and their output)
        if (typeof content === "string" && /^<(?:command-name|local-command-)/.test(content)) {
          continue;
        }

        for (const notification of toAcpNotifications(
          content,
          role,
          sessionId,
          toolUseCache,
          this.client,
          this.logger,
          { registerHooks: false, parentToolCallId: entry.parent_tool_use_id ?? null },
        )) {
          await this.client.sessionUpdate(notification);
        }
      }
    } finally {
      reader.close();
    }
  }

  async readTextFile(params: ReadTextFileRequest): Promise<ReadTextFileResponse> {
    const response = await this.client.readTextFile(params);
    return response;
  }

  async writeTextFile(params: WriteTextFileRequest): Promise<WriteTextFileResponse> {
    const response = await this.client.writeTextFile(params);
    return response;
  }

  canUseTool(sessionId: string): CanUseTool {
    return async (toolName, toolInput, { signal, suggestions, toolUseID }) => {
      const session = this.sessions[sessionId];
      if (!session) {
        return {
          behavior: "deny",
          message: "Session not found",
          interrupt: true,
        };
      }

      if (toolName === "ExitPlanMode") {
        const response = await this.client.requestPermission({
          options: [
            {
              kind: "allow_always",
              name: "Yes, and auto-accept edits",
              optionId: "acceptEdits",
            },
            { kind: "allow_once", name: "Yes, and manually approve edits", optionId: "default" },
            { kind: "reject_once", name: "No, keep planning", optionId: "plan" },
          ],
          sessionId,
          toolCall: {
            toolCallId: toolUseID,
            rawInput: toolInput,
            title: toolInfoFromToolUse({ name: toolName, input: toolInput }).title,
          },
        });

        if (signal.aborted || response.outcome?.outcome === "cancelled") {
          throw new Error("Tool use aborted");
        }
        if (
          response.outcome?.outcome === "selected" &&
          (response.outcome.optionId === "default" || response.outcome.optionId === "acceptEdits")
        ) {
          session.permissionMode = response.outcome.optionId;
          await this.client.sessionUpdate({
            sessionId,
            update: {
              sessionUpdate: "current_mode_update",
              currentModeId: response.outcome.optionId,
            },
          });

          return {
            behavior: "allow",
            updatedInput: toolInput,
            updatedPermissions: suggestions ?? [
              { type: "setMode", mode: response.outcome.optionId, destination: "session" },
            ],
          };
        } else {
          return {
            behavior: "deny",
            message: "User rejected request to exit plan mode.",
            interrupt: true,
          };
        }
      }

      // Handle AskUserQuestion: render questions in the UI via permission flow with _meta
      if (toolName === "AskUserQuestion") {
        const response = await this.client.requestPermission({
          _meta: {
            askUserQuestion: {
              questions: (toolInput as Record<string, unknown>).questions ?? [],
            },
          },
          options: [
            { kind: "allow_once", name: "Submit", optionId: "submit" },
            { kind: "reject_once", name: "Skip", optionId: "skip" },
          ],
          sessionId,
          toolCall: {
            toolCallId: toolUseID,
            rawInput: toolInput as Record<string, unknown>,
            title: "Claude has questions for you",
          },
        });

        if (signal.aborted || response.outcome?.outcome === "cancelled") {
          throw new Error("Tool use aborted");
        }

        if (
          response.outcome?.outcome === "selected" &&
          response.outcome.optionId === "submit"
        ) {
          const answers =
            ((response.outcome as Record<string, unknown>)._meta as Record<string, unknown>)
              ?.answers as Record<string, string> ?? {};
          return {
            behavior: "allow",
            updatedInput: {
              questions: (toolInput as Record<string, unknown>).questions,
              answers,
            },
          };
        }

        return {
          behavior: "deny",
          message: "User skipped the questions",
        };
      }

      if (
        session.permissionMode === "bypassPermissions" ||
        (session.permissionMode === "acceptEdits" && EDIT_TOOL_NAMES.includes(toolName))
      ) {
        return {
          behavior: "allow",
          updatedInput: toolInput,
          updatedPermissions: suggestions ?? [
            { type: "addRules", rules: [{ toolName }], behavior: "allow", destination: "session" },
          ],
        };
      }

      const response = await this.client.requestPermission({
        options: [
          {
            kind: "allow_always",
            name: "Always Allow",
            optionId: "allow_always",
          },
          { kind: "allow_once", name: "Allow", optionId: "allow" },
          { kind: "reject_once", name: "Reject", optionId: "reject" },
        ],
        sessionId,
        toolCall: {
          toolCallId: toolUseID,
          rawInput: toolInput,
          title: toolInfoFromToolUse({ name: toolName, input: toolInput }).title,
        },
      });
      if (signal.aborted || response.outcome?.outcome === "cancelled") {
        throw new Error("Tool use aborted");
      }
      if (
        response.outcome?.outcome === "selected" &&
        (response.outcome.optionId === "allow" || response.outcome.optionId === "allow_always")
      ) {
        // If Claude Code has suggestions, it will update their settings already
        if (response.outcome.optionId === "allow_always") {
          return {
            behavior: "allow",
            updatedInput: toolInput,
            updatedPermissions: suggestions ?? [
              {
                type: "addRules",
                rules: [{ toolName }],
                behavior: "allow",
                destination: "session",
              },
            ],
          };
        }
        return {
          behavior: "allow",
          updatedInput: toolInput,
        };
      } else {
        return {
          behavior: "deny",
          message: "User refused permission to run tool",
          interrupt: true,
        };
      }
    };
  }

  private async sendAvailableCommandsUpdate(sessionId: string): Promise<void> {
    const session = this.sessions[sessionId];
    if (!session) return;
    const commands = await session.query.supportedCommands();
    await this.client.sessionUpdate({
      sessionId,
      update: {
        sessionUpdate: "available_commands_update",
        availableCommands: getAvailableSlashCommands(commands),
      },
    });
  }

  private async createSession(
    params: NewSessionRequest,
    creationOpts: { resume?: string; forkSession?: boolean } = {},
  ): Promise<NewSessionResponse> {
    // We want to create a new session id unless it is resume,
    // but not resume + forkSession.
    let sessionId;
    if (creationOpts.forkSession) {
      sessionId = randomUUID();
    } else if (creationOpts.resume) {
      sessionId = creationOpts.resume;
    } else {
      sessionId = randomUUID();
    }

    const input = new Pushable<SDKUserMessage>();

    const settingsManager = new SettingsManager(params.cwd, {
      logger: this.logger,
    });
    await settingsManager.initialize();

    const mcpServers: Record<string, McpServerConfig> = {};
    if (Array.isArray(params.mcpServers)) {
      for (const server of params.mcpServers) {
        if ("type" in server) {
          mcpServers[server.name] = {
            type: server.type,
            url: server.url,
            headers: server.headers
              ? Object.fromEntries(server.headers.map((e) => [e.name, e.value]))
              : undefined,
          };
        } else {
          mcpServers[server.name] = {
            type: "stdio",
            command: server.command,
            args: server.args,
            env: server.env
              ? Object.fromEntries(server.env.map((e) => [e.name, e.value]))
              : undefined,
          };
        }
      }
    }

    // Only add the acp MCP server if built-in tools are not disabled
    if (!params._meta?.disableBuiltInTools) {
      const server = createMcpServer(this, sessionId, this.clientCapabilities);
      mcpServers["acp"] = {
        type: "sdk",
        name: "acp",
        instance: server,
      };
    }

    let systemPrompt: Options["systemPrompt"] = { type: "preset", preset: "claude_code" };
    if (params._meta?.systemPrompt) {
      const customPrompt = params._meta.systemPrompt;
      if (typeof customPrompt === "string") {
        systemPrompt = customPrompt;
      } else if (
        typeof customPrompt === "object" &&
        "append" in customPrompt &&
        typeof customPrompt.append === "string"
      ) {
        systemPrompt.append = customPrompt.append;
      }
    }

    const permissionMode = "default";

    // Extract options from _meta if provided
    const userProvidedOptions = (params._meta as NewSessionMeta | undefined)?.claudeCode?.options;

    // Configure thinking/effort based on stored effort level
    const envMaxThinkingTokens = process.env.MAX_THINKING_TOKENS
      ? parseInt(process.env.MAX_THINKING_TOKENS, 10)
      : undefined;
    const maxThinkingTokens =
      envMaxThinkingTokens && envMaxThinkingTokens > 0
        ? Math.max(envMaxThinkingTokens, MIN_THINKING_BUDGET_TOKENS)
        : envMaxThinkingTokens;

    // Determine thinking and effort from stored effortLevel
    const userMaxThinkingTokens = userProvidedOptions?.maxThinkingTokens;
    const normalizedUserMaxThinkingTokens =
      userMaxThinkingTokens && userMaxThinkingTokens > 0
        ? Math.max(userMaxThinkingTokens, MIN_THINKING_BUDGET_TOKENS)
        : userMaxThinkingTokens;
    const resolvedThinkingTokens = normalizedUserMaxThinkingTokens ?? maxThinkingTokens;
    let thinkingOption: Options["thinking"];
    let effortOption: Options["effort"];
    if (resolvedThinkingTokens && resolvedThinkingTokens > 0) {
      // Explicit token budget from env/meta overrides effort-based config
      thinkingOption = { type: "enabled", budgetTokens: resolvedThinkingTokens };
    } else if (this.effortLevel === "off") {
      thinkingOption = { type: "disabled" };
    } else {
      // Adaptive thinking with effort level
      thinkingOption = { type: "adaptive" };
      effortOption = this.effortLevel as Options["effort"];
    }

    const options: Options = {
      systemPrompt,
      settingSources: ["user", "project", "local"],
      stderr: (err) => this.logger.error(err),
      ...(maxThinkingTokens !== undefined && { maxThinkingTokens }),
      ...userProvidedOptions,
      // Set thinking and effort via the new API
      ...(thinkingOption && { thinking: thinkingOption }),
      ...(effortOption && { effort: effortOption }),
      // Override certain fields that must be controlled by ACP
      cwd: params.cwd,
      includePartialMessages: true,
      mcpServers: { ...(userProvidedOptions?.mcpServers || {}), ...mcpServers },
      // If we want bypassPermissions to be an option, we have to allow it here.
      // But it doesn't work in root mode, so we only activate it if it will work.
      allowDangerouslySkipPermissions: ALLOW_BYPASS,
      permissionMode,
      canUseTool: this.canUseTool(sessionId),
      // note: although not documented by the types, passing an absolute path
      // here works to find zed's managed node version.
      executable: process.execPath as any,
      ...(process.env.CLAUDE_CODE_EXECUTABLE && {
        pathToClaudeCodeExecutable: process.env.CLAUDE_CODE_EXECUTABLE,
      }),
      tools: { type: "preset", preset: "claude_code" },
      hooks: {
        ...userProvidedOptions?.hooks,
        PreToolUse: [
          ...(userProvidedOptions?.hooks?.PreToolUse || []),
          {
            hooks: [createPreToolUseHook(settingsManager, this.logger)],
          },
        ],
        PostToolUse: [
          ...(userProvidedOptions?.hooks?.PostToolUse || []),
          {
            hooks: [
              createPostToolUseHook(this.logger, {
                onEnterPlanMode: async () => {
                  const session = this.sessions[sessionId];
                  if (session) {
                    session.permissionMode = "plan";
                  }
                  await this.client.sessionUpdate({
                    sessionId,
                    update: {
                      sessionUpdate: "current_mode_update",
                      currentModeId: "plan",
                    },
                  });
                },
              }),
            ],
          },
        ],
      },
      ...creationOpts,
    };

    if (creationOpts?.resume === undefined || creationOpts?.forkSession) {
      // Set our own session id if not resuming an existing session.
      options.sessionId = sessionId;
    }

    // IMPORTANT: Do NOT use allowedTools here. Setting allowedTools creates an exhaustive
    // whitelist in the SDK, which blocks ALL tools not explicitly listed — including MCP tools
    // from external servers (e.g., Unreal MCP). Instead, use only disallowedTools to block
    // specific built-in tools that are replaced by ACP equivalents via mcpServers["acp"].
    const disallowedTools: string[] = [];

    // Check if built-in tools should be disabled
    const disableBuiltInTools = params._meta?.disableBuiltInTools === true;

    if (!disableBuiltInTools) {
      // Replace built-in tools with ACP equivalents (exposed via mcpServers["acp"]).
      // Only need to disallow the built-ins; the ACP MCP tools are available automatically.
      if (this.clientCapabilities?.fs?.readTextFile) {
        disallowedTools.push("Read");
      }
      if (this.clientCapabilities?.fs?.writeTextFile) {
        disallowedTools.push("Write", "Edit");
      }
      if (this.clientCapabilities?.terminal) {
        disallowedTools.push("Bash", "BashOutput", "KillShell");
      }
    } else {
      // When built-in tools are disabled, explicitly disallow all of them
      disallowedTools.push(
        acpToolNames.read,
        acpToolNames.write,
        acpToolNames.edit,
        acpToolNames.bash,
        acpToolNames.bashOutput,
        acpToolNames.killShell,
        "Read",
        "Write",
        "Edit",
        "Bash",
        "BashOutput",
        "KillShell",
        "Glob",
        "Grep",
        "Task",
        "TodoWrite",
        "ExitPlanMode",
        "WebSearch",
        "WebFetch",
        "SlashCommand",
        "Skill",
        "NotebookEdit",
      );
    }

    if (disallowedTools.length > 0) {
      options.disallowedTools = [...(options.disallowedTools || []), ...disallowedTools];
    }

    // Handle abort controller from meta options
    const abortController = userProvidedOptions?.abortController;
    if (abortController?.signal.aborted) {
      throw new Error("Cancelled");
    }

    const q = query({
      prompt: input,
      options,
    });

    this.sessions[sessionId] = {
      query: q,
      input: input,
      cancelled: false,
      permissionMode,
      settingsManager,
      contextWindow: 200000,
      cumulativeCostUSD: 0,
      processedMessageIds: new Set(),
      lastContextUsed: 0,
    };

    const initializationResult = await q.initializationResult();

    const models = await getAvailableModels(q, initializationResult.models, settingsManager);

    const availableModes = [
      {
        id: "default",
        name: "Default",
        description: "Standard behavior, prompts for dangerous operations",
      },
      {
        id: "acceptEdits",
        name: "Accept Edits",
        description: "Auto-accept file edit operations",
      },
      {
        id: "plan",
        name: "Plan Mode",
        description: "Planning mode, no actual tool execution",
      },
      {
        id: "dontAsk",
        name: "Don't Ask",
        description: "Don't prompt for permissions, deny if not pre-approved",
      },
    ];
    // Only works in non-root mode
    if (ALLOW_BYPASS) {
      availableModes.push({
        id: "bypassPermissions",
        name: "Bypass Permissions",
        description: "Bypass all permission checks",
      });
    }

    return {
      sessionId,
      models,
      modes: {
        currentModeId: permissionMode,
        availableModes,
      },
      configOptions: [buildEffortConfigOption(this.effortLevel)],
    };
  }
}

function buildEffortConfigOption(currentValue: string): SessionConfigOption {
  return {
    type: "select",
    id: "thinking",
    name: "Effort",
    category: "thought_level",
    currentValue,
    options: [
      { value: "off", name: "Off" },
      { value: "low", name: "Low" },
      { value: "medium", name: "Medium" },
      { value: "high", name: "High" },
      { value: "max", name: "Max" },
    ],
  };
}

async function getAvailableModels(
  query: Query,
  models: ModelInfo[],
  settingsManager: SettingsManager,
): Promise<SessionModelState> {
  const settings = settingsManager.getSettings();

  let currentModel = models[0];

  if (settings.model) {
    const match = models.find(
      (m) =>
        m.value === settings.model ||
        m.value.includes(settings.model!) ||
        settings.model!.includes(m.value) ||
        m.displayName.toLowerCase() === settings.model!.toLowerCase() ||
        m.displayName.toLowerCase().includes(settings.model!.toLowerCase()),
    );
    if (match) {
      currentModel = match;
    }
  }

  await query.setModel(currentModel.value);

  return {
    availableModels: models.map((model) => ({
      modelId: model.value,
      name: model.displayName,
      description: model.description,
    })),
    currentModelId: currentModel.value,
  };
}

function getAvailableSlashCommands(commands: SlashCommand[]): AvailableCommand[] {
  const UNSUPPORTED_COMMANDS = [
    "cost",
    "keybindings-help",
    "login",
    "logout",
    "output-style:new",
    "release-notes",
    "todos",
  ];

  return commands
    .map((command) => {
      const input = command.argumentHint
        ? {
            hint: Array.isArray(command.argumentHint)
              ? command.argumentHint.join(" ")
              : command.argumentHint,
          }
        : null;
      let name = command.name;
      if (command.name.endsWith(" (MCP)")) {
        name = `mcp:${name.replace(" (MCP)", "")}`;
      }
      return {
        name,
        description: command.description || "",
        input,
      };
    })
    .filter((command: AvailableCommand) => !UNSUPPORTED_COMMANDS.includes(command.name));
}

function formatUriAsLink(uri: string): string {
  try {
    if (uri.startsWith("file://")) {
      const path = uri.slice(7); // Remove "file://"
      const name = path.split("/").pop() || path;
      return `[@${name}](${uri})`;
    } else if (uri.startsWith("zed://")) {
      const parts = uri.split("/");
      const name = parts[parts.length - 1] || uri;
      return `[@${name}](${uri})`;
    }
    return uri;
  } catch {
    return uri;
  }
}

export function promptToClaude(prompt: PromptRequest): SDKUserMessage {
  const content: any[] = [];
  const context: any[] = [];

  for (const chunk of prompt.prompt) {
    switch (chunk.type) {
      case "text": {
        let text = chunk.text;
        // change /mcp:server:command args -> /server:command (MCP) args
        const mcpMatch = text.match(/^\/mcp:([^:\s]+):(\S+)(\s+.*)?$/);
        if (mcpMatch) {
          const [, server, command, args] = mcpMatch;
          text = `/${server}:${command} (MCP)${args || ""}`;
        }
        content.push({ type: "text", text });
        break;
      }
      case "resource_link": {
        const formattedUri = formatUriAsLink(chunk.uri);
        content.push({
          type: "text",
          text: formattedUri,
        });
        break;
      }
      case "resource": {
        if ("text" in chunk.resource) {
          const formattedUri = formatUriAsLink(chunk.resource.uri);
          content.push({
            type: "text",
            text: formattedUri,
          });
          context.push({
            type: "text",
            text: `\n<context ref="${chunk.resource.uri}">\n${chunk.resource.text}\n</context>`,
          });
        }
        // Ignore blob resources (unsupported)
        break;
      }
      case "image":
        if (chunk.data) {
          content.push({
            type: "image",
            source: {
              type: "base64",
              data: chunk.data,
              media_type: chunk.mimeType,
            },
          });
        } else if (chunk.uri && chunk.uri.startsWith("http")) {
          content.push({
            type: "image",
            source: {
              type: "url",
              url: chunk.uri,
            },
          });
        }
        break;
      // Ignore audio and other unsupported types
      default:
        break;
    }
  }

  content.push(...context);

  return {
    type: "user",
    message: {
      role: "user",
      content: content,
    },
    session_id: prompt.sessionId,
    parent_tool_use_id: null,
  };
}

/**
 * Convert an SDKAssistantMessage (Claude) to a SessionNotification (ACP).
 * Only handles text, image, and thinking chunks for now.
 */
export function toAcpNotifications(
  content: string | ContentBlockParam[] | BetaContentBlock[] | BetaRawContentBlockDelta[],
  role: "assistant" | "user",
  sessionId: string,
  toolUseCache: ToolUseCache,
  client: AgentSideConnection,
  logger: Logger,
  options?: { registerHooks?: boolean; parentToolCallId?: string | null },
): SessionNotification[] {
  const parentToolCallId = options?.parentToolCallId ?? null;
  const registerHooks = options?.registerHooks !== false;
  if (typeof content === "string") {
    return [
      {
        sessionId,
        update: {
          sessionUpdate: role === "assistant" ? "agent_message_chunk" : "user_message_chunk",
          content: {
            type: "text",
            text: content,
          },
        },
      },
    ];
  }

  const output = [];
  // Only handle the first chunk for streaming; extend as needed for batching
  for (const chunk of content) {
    let update: SessionNotification["update"] | null = null;
    switch (chunk.type) {
      case "text":
      case "text_delta":
        update = {
          sessionUpdate: role === "assistant" ? "agent_message_chunk" : "user_message_chunk",
          content: {
            type: "text",
            text: chunk.text,
          },
        };
        break;
      case "image":
        update = {
          sessionUpdate: role === "assistant" ? "agent_message_chunk" : "user_message_chunk",
          content: {
            type: "image",
            data: chunk.source.type === "base64" ? chunk.source.data : "",
            mimeType: chunk.source.type === "base64" ? chunk.source.media_type : "",
            uri: chunk.source.type === "url" ? chunk.source.url : undefined,
          },
        };
        break;
      case "thinking":
      case "thinking_delta":
        update = {
          sessionUpdate: "agent_thought_chunk",
          content: {
            type: "text",
            text: chunk.thinking,
          },
        };
        break;
      case "tool_use":
      case "server_tool_use":
      case "mcp_tool_use": {
        toolUseCache[chunk.id] = chunk;
        if (chunk.name === "TodoWrite") {
          // @ts-expect-error - sometimes input is empty object
          if (Array.isArray(chunk.input.todos)) {
            update = {
              sessionUpdate: "plan",
              entries: planEntries(chunk.input as { todos: ClaudePlanEntry[] }),
            };
          }
        } else {
          if (registerHooks) {
            registerHookCallback(chunk.id, {
              onPostToolUseHook: async (toolUseId, toolInput, toolResponse) => {
                const toolUse = toolUseCache[toolUseId];
                if (toolUse) {
                  const update: SessionNotification["update"] = {
                    _meta: {
                      claudeCode: {
                        toolResponse,
                        toolName: toolUse.name,
                      },
                    } satisfies ToolUpdateMeta,
                    toolCallId: toolUseId,
                    sessionUpdate: "tool_call_update",
                  };
                  await client.sessionUpdate({
                    sessionId,
                    update,
                  });
                } else {
                  logger.error(
                    `[claude-code-acp] Got a tool response for tool use that wasn't tracked: ${toolUseId}`,
                  );
                }
              },
            });
          }

          let rawInput;
          try {
            rawInput = JSON.parse(JSON.stringify(chunk.input));
          } catch {
            // ignore if we can't turn it to JSON
          }
          update = {
            _meta: {
              claudeCode: {
                toolName: chunk.name,
                ...(parentToolCallId ? { parentToolCallId } : {}),
              },
            } satisfies ToolUpdateMeta,
            toolCallId: chunk.id,
            sessionUpdate: "tool_call",
            rawInput,
            status: "pending",
            ...toolInfoFromToolUse(chunk),
          };
        }
        break;
      }

      case "tool_result":
      case "tool_search_tool_result":
      case "web_fetch_tool_result":
      case "web_search_tool_result":
      case "code_execution_tool_result":
      case "bash_code_execution_tool_result":
      case "text_editor_code_execution_tool_result":
      case "mcp_tool_result": {
        const toolUse = toolUseCache[chunk.tool_use_id];
        if (!toolUse) {
          logger.error(
            `[claude-code-acp] Got a tool result for tool use that wasn't tracked: ${chunk.tool_use_id}`,
          );
          break;
        }

        if (toolUse.name !== "TodoWrite") {
          update = {
            _meta: {
              claudeCode: {
                toolName: toolUse.name,
                ...(parentToolCallId ? { parentToolCallId } : {}),
              },
            } satisfies ToolUpdateMeta,
            toolCallId: chunk.tool_use_id,
            sessionUpdate: "tool_call_update",
            status: "is_error" in chunk && chunk.is_error ? "failed" : "completed",
            rawOutput: chunk.content,
            ...toolUpdateFromToolResult(chunk, toolUseCache[chunk.tool_use_id]),
          };
        }
        break;
      }

      case "document":
      case "search_result":
      case "redacted_thinking":
      case "input_json_delta":
      case "citations_delta":
      case "signature_delta":
      case "container_upload":
      case "compaction":
      case "compaction_delta":
        break;

      default:
        unreachable(chunk, logger);
        break;
    }
    if (update) {
      output.push({ sessionId, update });
    }
  }

  return output;
}

export function streamEventToAcpNotifications(
  message: SDKPartialAssistantMessage,
  sessionId: string,
  toolUseCache: ToolUseCache,
  client: AgentSideConnection,
  logger: Logger,
): SessionNotification[] {
  const parentToolCallId = message.parent_tool_use_id ?? null;
  const event = message.event;
  switch (event.type) {
    case "content_block_start":
      return toAcpNotifications(
        [event.content_block],
        "assistant",
        sessionId,
        toolUseCache,
        client,
        logger,
        { parentToolCallId },
      );
    case "content_block_delta":
      return toAcpNotifications(
        [event.delta],
        "assistant",
        sessionId,
        toolUseCache,
        client,
        logger,
        { parentToolCallId },
      );
    // No content
    case "message_start":
    case "message_delta":
    case "message_stop":
    case "content_block_stop":
      return [];

    default:
      unreachable(event, logger);
      return [];
  }
}

export function runAcp() {
  const input = nodeToWebWritable(process.stdout);
  const output = nodeToWebReadable(process.stdin);

  const stream = ndJsonStream(input, output);
  new AgentSideConnection((client) => new ClaudeAcpAgent(client), stream);
}
