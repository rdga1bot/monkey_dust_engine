# engine/src/ai/ — CLAUDE.md

**Без FSM/switch для AI** — усе через `BehaviorTree.h` (stackless VM,
24-byte `BTNode`, cache-friendly детермінізм; відхилено: рекурсивний BT,
FSM). Типи (`BTNodeType`, 30+ значень) винесені в окремий `bt_types.h`
щоб не роздувати include-вартість `behavior_tree.h`.

## `.inc`-файли — навмисний спліт
`bt_json_core/ai/ext.inc` + `bt_vm_core/ai.inc` — великі switch/parser
таблиці, розбиті на `.inc` для компіляційного часу (unity build), НЕ
самостійні translation units. Редагувати як частину файлу, що їх
`#include`, не окремо.

## Реєстрація біндингів — порядок критичний
`RegisterAllBTBindings()` МУСИТЬ викликатись ПЕРЕД
`BTLoader::Get().LoadFromFile()` — інакше JSON-дерево посилається на
ще незареєстровані action/condition імена, тихий no-op вузол.

## BTActionFunc сигнатура
```cpp
BTStatus(*)(md::EngineContext&, flecs::entity)
// game-сторона: static_cast<GameState&>(ctx)
```

## Флаги — фіксовані масиви, без malloc
`AgentBlackboard` MAX_ENTRIES=24 (FNV-1a key від string literal, НЕ
random uint). `BTNodePool` — flat 32KB bump arena, без malloc у game
loop. `AgentTimers[26]` + `lcflags` + `frame_flags` — POD, без heap.

Глибокий довідник (домен-специфічні деталі, RE-джерела): `docs/CLAUDE_AI.md`.
