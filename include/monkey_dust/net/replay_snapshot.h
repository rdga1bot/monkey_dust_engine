#pragma once
#include <cstdint>

// ── M50: ReplaySnapshot — deterministic replay serializer ─────────────────────
// Captures one logic-tick per ReplayFrame into a fixed-size ring buffer.
// Frame CRC32 enables determinism verification (same inputs → same CRC).
// File format: magic + version + frame_count + ReplayFrame[].
//
// Usage (game code):
//   ReplayBuffer buf{};
//   ReplaySerializer::CaptureFromECS(buf, px, pz, frame_idx, now_ms);
//   ...
//   ReplaySerializer::WriteToFile(buf, "replays/game.rply");

static constexpr int REPLAY_MAX_NPCS_PER_FRAME = 64;
static constexpr int REPLAY_MAX_FRAMES         = 300; // 30 s at 10 TPS

// Per-NPC state captured each frame (16 bytes, POD).
struct ReplayNpcState {
    float    x, z;       // world position
    uint32_t entity_id;  // entt raw entity value
    uint8_t  motivation; // MotivationType
    uint8_t  awareness;  // AwarenessState
    uint8_t  alertness;  // AlertnessState
    uint8_t  flags;      // lcflags & 0xFF (low 8 bits, key BT flags)
};
static_assert(sizeof(ReplayNpcState) == 16, "ReplayNpcState must be 16 bytes");

// One logic-tick snapshot.
struct ReplayFrame {
    uint32_t       frame_index;
    uint32_t       timestamp_ms;
    float          player_x, player_z;
    uint16_t       npc_count;
    uint16_t       _pad;
    uint32_t       npc_crc32; // CRC of npc[0..npc_count) — determinism check
    ReplayNpcState npcs[REPLAY_MAX_NPCS_PER_FRAME];
};
static_assert(sizeof(ReplayFrame) == 1048, "ReplayFrame must be 1048 bytes");

// Ring buffer holding up to REPLAY_MAX_FRAMES snapshots.
// Oldest frame is overwritten when full.
struct ReplayBuffer {
    ReplayFrame frames[REPLAY_MAX_FRAMES]; // 300 * 1048 = 314400 bytes BSS
    uint32_t    write_pos; // next write index (wraps at MAX_FRAMES)
    uint32_t    count;     // valid frame count (capped at MAX_FRAMES)
};

namespace ReplaySerializer {

static constexpr uint32_t REPLAY_MAGIC   = 0x52504C59u; // "RPLY"
static constexpr uint32_t REPLAY_VERSION = 1u;

// CRC32 of the npc array (0..npc_count). Poly 0xEDB88320 (PKZIP).
uint32_t ComputeFrameCRC(const ReplayFrame& frame) noexcept;

// Append a manually-built frame to the ring buffer.
void CaptureFrame(ReplayBuffer& buf, const ReplayFrame& frame) noexcept;

// Build a frame from the live ECS (WorldTransform + AgentState) and capture it.
void CaptureFromECS(ReplayBuffer& buf,
                    float player_x, float player_z,
                    uint32_t frame_index, uint32_t now_ms) noexcept;

// Write all valid frames to a binary file. Returns true on success.
bool WriteToFile(const ReplayBuffer& buf, const char* path) noexcept;

// Read frames from a binary file into buf (overwrites existing content).
// Returns true on success.
bool ReadFromFile(ReplayBuffer& buf, const char* path) noexcept;

// Clear the buffer (write_pos=0, count=0).
void Reset(ReplayBuffer& buf) noexcept;

} // namespace ReplaySerializer
