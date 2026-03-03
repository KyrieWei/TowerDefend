---
paths:
  - "Source/TowerDefend/Core/TDPlayerController.*"
  - "Source/TowerDefend/Core/TDServerValidation.*"
  - "Source/TowerDefend/Core/TDNetworkTypes.*"
  - "Source/TowerDefend/HexGrid/TDHexGridReplication.*"
---

# Network Architecture Rules

Server-authoritative networking using UE5 Dedicated Server mode with Replication.

## Architecture

```
                    +------------------+
                    | Dedicated Server |  <- Authoritative server
                    | (UE5 DS)         |
                    +--------+---------+
                             |
            +----------------+----------------+
            |                |                |
     +------+------+  +-----+------+  +------+------+
     |  Client 1   |  |  Client 2  |  |  Client N   |
     |  (Player)   |  |  (Player)  |  |  (Player)   |
     +-------------+  +------------+  +-------------+
```

## Core Principles

- **Server-authoritative**: All critical logic runs on the dedicated server
- **Preparation phase**: Each client operates independently; results sent to server via RPC for validation
- **Battle phase**: Server runs combat simulation; clients play back results (prevents cheating)
- **Turn-based strategy**: Low bandwidth requirement; no need for frame-sync

## State Synchronization

States that must be replicated:
- Player resources (gold, research points, HP)
- Building layouts
- Army configurations
- Tech progress
- **Terrain state** (height levels, terrain types)

## Terrain Sync Protocol

Terrain changes are synchronized via RPC:
```
Client requests terrain change -> Server validates -> Server applies -> Server broadcasts result to all clients
```

## Validation Requirements

Server must validate all client requests:
- Building placement validation (terrain, ownership, resources)
- Resource spending validation (sufficient gold/research points)
- Tech research validation (prerequisites, availability, cost)
- Terrain modification validation (height constraints, cooldown, resources, tech unlock level)

## UE5 Networking Facilities

- Use `Replication` + `GameState`/`PlayerState` system
- `UPROPERTY(Replicated)` / `UPROPERTY(ReplicatedUsing)` for state sync
- `UFUNCTION(Server, Reliable)` for client-to-server RPCs
- `UFUNCTION(NetMulticast, Reliable)` for server-to-all broadcasts
- `GetLifetimeReplicatedProps()` for registering replicated properties

## Module Dependencies (Future)

When networking is fully implemented, `TowerDefend.Build.cs` may need:
```
"NetCore", "OnlineSubsystem", "OnlineSubsystemUtils"
```

## Anti-Cheat

- All critical logic on dedicated server
- Client sends requests, server validates and executes
- Numerical balance via DataAsset/DataTable (hot-tunable)
- Consider ELO or similar rating system for matchmaking
