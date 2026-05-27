#pragma once
#include "pcg_data.h"
namespace md {

static constexpr int PCG_MAX_INPUTS = 4;

// ── PcgNode — base class for all terrain graph nodes ─────────────────────────
// All concrete nodes are POD-like structs stored in PcgGraph's fixed arrays.
// No virtual dispatch overhead in hot-path: Execute() is called only when dirty.
struct PcgNode {
    uint16_t    id      = 0;
    bool        dirty   = true;
    bool        alive   = false;

    // Version of each upstream input seen at last Execute().
    // If any input.version != in_versions[i], node is re-cooked.
    uint32_t    in_versions[PCG_MAX_INPUTS] = {};

    PcgData     cached;   // result of last Execute()

    // ── Override in concrete nodes ────────────────────────────────────────
    virtual PcgData    Execute(const PcgData inputs[], int in_count) = 0;
    virtual const char* TypeName()    const = 0;
    virtual PcgDataType OutType()     const = 0;

    // Return expected input type for pin `i`. None = accepts any.
    virtual PcgDataType InType(int /*i*/) const { return PcgDataType::None; }

    // If true, Execute() should return a FieldFn (lazy) instead of a materialised array.
    // Default false; overridden by noise/math nodes in P-NG-4.
    virtual bool UseFieldMode() const { return false; }

    // ── GetOutput — cached evaluation ────────────────────────────────────
    // Checks upstream versions and dirty flag; calls Execute() only if needed.
    const PcgData& GetOutput(const PcgData inputs[], int in_count) {
        bool stale = dirty;
        if (!stale) {
            for (int i = 0; i < in_count && i < PCG_MAX_INPUTS; ++i)
                if (inputs[i].version != in_versions[i]) { stale = true; break; }
        }
        if (stale) {
            cached = Execute(inputs, in_count);
            dirty  = false;
            for (int i = 0; i < in_count && i < PCG_MAX_INPUTS; ++i)
                in_versions[i] = inputs[i].version;
        }
        return cached;
    }

    void MarkDirty() { dirty = true; }

protected:
    PcgNode() : id(0), dirty(true), alive(false), in_versions{}, cached() {}
    virtual ~PcgNode() = default;
};

} // namespace md
