# unreal-mcp-tools

Editor toolsets for Unreal Engine 5.8 that AI agents call through the
`ToolsetRegistry` / `unreal-mcp` server. Each domain lives in its own module
inside this one plugin.

| Module | Toolset | What it covers |
|---|---|---|
| `TerrainToolset` | `UTerrainToolset` | Landscape creation (World Partition aware), heightmap/weightmap import & export per edit layer, edit/paint layers, height sampling |
| `WorldToolset` | `UWorldToolset` | Creating levels (incl. World Partition), loading World Partition regions in the editor |
| `PerfToolset` | `UPerfToolset` | Frame timing over a window (avg/p95 frame, game/render/RHI/GPU), console commands for A/B comparisons |

Outside the editor:

| Tool | What it does |
|---|---|
| `Tools/heightgen` | Spec-driven heightmap generator (JSON → `.r16` + hillshade preview + slope report) |

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

## Perf toolset

- `MeasureFrames(DurationSeconds, WarmupSeconds)` is async: it samples every frame and completes with
  average / p95 frame time, average game, render, RHI and GPU times and the bottleneck.
- `RunConsoleCommand(Command)` runs in the PIE world if one is running, else the editor world.
- Turn off **Editor Preferences → Performance → Use Less CPU when in Background** while measuring,
  otherwise the editor throttles itself when it isn't the foreground window.

## heightgen

```powershell
python -m venv Tools/heightgen/.venv
Tools/heightgen/.venv/Scripts/python -m pip install numpy
Tools/heightgen/.venv/Scripts/python -I Tools/heightgen/heightgen.py <Terrain>/World.spec.json
```

The spec describes resolution, height range, noise layers, distance-based roughness rings and
features (`hill`, `flatten`, `calm`, `water`, `boundary`) in Unreal world meters (+X right, +Y down
in the top view). Outputs `<name>.r16`, `<name>_preview.png`, `<name>_report.json`.

Never name a spec `<name>.json` next to `<name>.r16`: Unreal reads that file as raw-heightmap
metadata (`width`/`height`/`bpp`) and rejects the import.

### Tests

Automation specs under `AI.Toolsets.UnrealMcpTools` (Terrain and World). Run over MCP with
`AutomationTestToolset` (`DiscoverTests` → `RunTests`) in a sandbox level: the tests create and
destroy temporary landscapes in the currently open level.
