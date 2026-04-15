# Project Memory - Tower Defense (Flecs ECS)

## Project Overview
- 3D Tower Defense game built with Flecs ECS framework (C++)
- Location: D:\code\ecs\tower_defense
- Uses flecs.components (transform, graphics, geometry, physics, gui, input) and flecs.systems (transform, physics, sokol), flecs.game
- Assets defined in .flecs script files under etc/assets/
- Rendering via Sokol backend

## Architecture
- `src/main.cpp` - All game logic (components, systems, level building)
- `etc/assets/*.flecs` - Prefab definitions (materials, enemies, turrets, particles, etc.)
- `include/tower_defense.h` - Auto-generated header
- Prefabs use `tower_defense.prefabs` module in .flecs files
- Particles inherit from `Particle` prefab (has ParticleLifespan, Rgb, Box)

## Game Design
- **Scoring**: +1 per enemy killed (added 2026-04-14)
- **Levels**: 5 levels total (changed from 10, 2026-04-14)
- **Enemy count**: 5 + (level-1) * 2 per level
- **Victory**: After level 5, fireworks celebration + score display
- **Firework prefab**: etc/assets/firework.flecs, inherits from Particle

## Key Components
- `Game` (singleton): current_level, max_level(5), score, enemies_*, failed, won, fireworks_timer, billboard_created, displayed_score
- `Enemy`, `Health`, `Direction`, `Bullet`, `Turret`, `Target`, `Laser`
- `Particle`, `ParticleLifespan`, `ExplosionLight`, `HitCooldown`, `Recoil`
- `ScoreDigit` (tag): marks HUD score pixel entities for cleanup/update
- `Billboard` (tag): marks billboard text entities
- `CameraControl`: mouse-driven camera state (dragging, rotating, initial pos/rot, right_was_down)

## Key Systems
- SpawnEnemy, MoveEnemy, DestroyEnemy (with scoring)
- TurretControl, FindTarget, AimTarget, FireAtTarget, BeamControl
- ProgressParticle, HitTarget, UpdateExplosionLight
- VictoryCelebration (fireworks on win), VictoryBillboard, GameOverBillboard
- ScoreDisplay (3D pixel font score HUD, rebuilds only on score change)

## 3D Pixel Font System
- `build_text_3d()`: creates 3D box entities to form text characters
- 5x3 bitmap glyph definition (digit_glyphs[], char_to_glyph())
- Supports: A-Z (partial), 0-9, space, exclamation
- HUD scope (`hud`) used as parent for text entities
- Flecs sokol renderer has NO 2D text rendering - must use 3D approach
- Score HUD: gold color, high emissive, above map
- Billboard: large pixels, very high emissive, centered above map

## Controls
- B: Build cannon turret, L: Build laser turret, X/Delete: Remove turret
- **Mouse drag**: Left drag to pan camera, Middle drag to orbit, Right click to reset

## Camera Control
- `CameraControl` component: stores dragging/rotating state, initial position/rotation, mouse delta
- `CameraControlSystem`: mouse-driven camera (pan/orbit/reset)
- Uses Win32 `GetAsyncKeyState` API for mouse button detection (more reliable than EcsInput)
- Pan speed: 0.03, Orbit speed: 0.005, Vertical rotation clamped to ±1.2 radians

## Critical Notes
- **NEVER add C++ custom components in .flecs scripts** - Flecs script engine cannot parse them, causes runtime crash
- Add custom components dynamically in C++ via `ecs.lookup("entity_name")` + `.add<Component>()`
- `bake run` may fail with Sokol renderer errors (window context issue); direct exe run is reliable
