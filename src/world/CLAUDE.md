# engine/src/world/ — CLAUDE.md

## Terrain heightmap: raw uint16 `.r16`, не `.r32`
`game/data/terrain/world_hmap.r16` (gitignored, Kenshi-похідні дані) —
джерело `fullmap.tif` (16385², uint16); float32 подвоював розмір без
виграшу в точності. Формат + спільні хелпери: `tools/md_hmap_io.py`.

Редакторський terrain-brush (F3, "Terrain Sculpt") НЕ пише в `.r16`
напряму — пише в sparse `<base>_edits.r32` (тільки реально змінені
зони), накладається на raw-базу при завантаженні. Деталі:
`TerrainAtlas_Load`/`Save` doc-коментарі в `terrain_gen.cpp`.

**Terrain sync rule (КРИТИЧНО):** після зміни `world_hmap.r16` —
обов'язково `python3 tools/md_worldmap_gen.py` (інакше editor і game
показують різний ландшафт).

**Процедурної генерації немає** (видалено 2026-07-19, як і в оригіналі
Kenshi). `SimplexNoise2`/`FBM2` лишились лише як noise-примітиви поза
реальним Kenshi-терейном (тестові фікстури).

## Ogre-quadtree mesh-топологія (з 2026-08-19)
`terrain_quadtree_mesh.cpp`'s `BuildTerrainQuadtreeMesh` — ОДИН спільний
index buffer (17×17 regular grid + 4 skirt-смуги) для КОЖНОГО вузла на
КОЖНІЙ глибині всюди у світі; `terrain_quadtree.cpp`'s
`TerrainQuadtree::SelectVisible` — без персистентного дерева, рахує
видимі вузли з камери+frustum щокадру. Geomorph-формула (морф має
досягати 1.0 рівно там, де вузол перестав би малюватись — на межі
БАТЬКІВСЬКОГО range, не власного) — див. doc-коментар у
`RecurseNode` (`terrain_quadtree.cpp`).

## Kenshi colour overlay: BC3/DXT5, нативна роздільність
`game/data/textures/md_terrain.dds` (gitignored) — 16384², BC3, власний
numpy-encoder (`tools/md_bc3_encode.py`, жодного готового BC3-інструменту
в системі немає). Triplanar cliff-текстурування — окрема UV-вісь через
`world_pos.y`, лише для `tex_ground` cliff-шару, не для overlay.

Глибокий довідник: `docs/CLAUDE_PROC_TERRAIN.md` (історичний),
`docs/OSS_TERRAIN_METHODS.md`, `docs/CLAUDE_TERRAIN_SEAM.md`.
