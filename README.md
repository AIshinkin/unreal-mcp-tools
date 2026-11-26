# unreal-mcp-tools

Editor toolsets for Unreal Engine 5.8 that AI agents call through the
`ToolsetRegistry` / `unreal-mcp` server. Each domain lives in its own module
inside this one plugin.

| Module | Toolset | What it covers |
|---|---|---|
| `TerrainToolset` | `UTerrainToolset` | Landscape creation (World Partition aware), heightmap/weightmap import & export per edit layer, edit/paint layers, height sampling |

## Install into a project

```powershell
git submodule add https://github.com/AIshinkin/unreal-mcp-tools.git Plugins/UnrealMcpTools
```

Requirements:
- UE 5.8 with the `ToolsetRegistry` and `ModelContextProtocol` plugins enabled.
- MSVC toolchain (VS 2026 Build Tools / VS 2022 17.14) to compile the plugin.

Build from the command line (editor closed for the first build, since the module DLL doesn't exist yet):

```powershell
& "C:\Games\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="<path>\<Project>.uproject" -WaitMutex
```

After the first build, iterate with Live Coding (`LiveCodingToolset.CompileLiveCoding` over MCP).

## Terrain toolset

Tools (`call_tool` with `toolset_name` = the registered `TerrainToolset` name):

- `CreateLandscape(Settings)` – spawns a landscape, imports an optional heightmap into the base edit layer,
  creates extra edit layers and splits it into streaming proxies in World Partition levels.
- `GetLandscapeInfo(Landscape)` – resolution, components, scale, world bounds, edit/paint layers.
- `ImportHeightmap` / `ExportHeightmap` – 16-bit `.png` / `.r16`, per edit layer (export: empty layer = blended result).
- `AddEditLayer` / `RemoveEditLayer`
- `AddPaintLayer` / `ImportWeightmap` – 8-bit `.png` / `.r8`.
- `SampleHeights` – final world Z at XY points.

Height mapping: heightmap value `32768` = landscape zero level (`Settings.Center.Z`);
full 16-bit range spans `HeightRangeMeters` (`ZScale = HeightRangeMeters * 100 / 512`).

### Tests

Automation spec `AI.Toolsets.UnrealMcpTools.TerrainToolset`. Run over MCP with
`AutomationTestToolset` (`DiscoverTests` → `RunTests`) in a sandbox level: the tests create and
destroy temporary landscapes in the currently open level.
