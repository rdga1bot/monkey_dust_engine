#include <monkey_dust/platform/job_graph.h>
#include <monkey_dust/platform/job_system.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>

namespace md {

JobGraph& JobGraph::Get() {
    static JobGraph inst;
    return inst;
}

bool JobGraph::AddBatch(const char* name, const JobBatchDesc& desc, JobBatchFn fn, void* user) {
    if (!name || !name[0] || !fn) return false;
    if (count_ >= MAX_BATCHES) {
        MD_LOG(MD_LOG_WARNING, "[JobGraph] MAX_BATCHES=%d reached, cannot register '%s'",
               MAX_BATCHES, name);
        return false;
    }
    Entry& e = entries_[count_++];
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.desc = desc;
    e.fn   = fn;
    e.user = user;
    return true;
}

bool JobGraph::Conflicts(const JobBatchDesc& a, const JobBatchDesc& b) {
    auto overlaps = [](const uint32_t* xs, int xn, const uint32_t* ys, int yn) {
        for (int i = 0; i < xn; ++i)
            for (int j = 0; j < yn; ++j)
                if (xs[i] == ys[j]) return true;
        return false;
    };
    // a writes vs b reads/writes
    if (overlaps(a.writes, a.write_count, b.reads,  b.read_count))  return true;
    if (overlaps(a.writes, a.write_count, b.writes, b.write_count)) return true;
    // b writes vs a reads
    if (overlaps(b.writes, b.write_count, a.reads,  a.read_count))  return true;
    return false;
}

void JobGraph::Run() {
    // Validate: for each batch P reading/writing resource R, the last
    // batch that WRITES R must be registered before P (same check shape
    // as RenderPassGraph::Validate() — catches wrong registration order,
    // does not by itself make anything run in parallel, see header note).
    for (int pi = 0; pi < count_; ++pi) {
        const Entry& P = entries_[pi];
        for (int ri = 0; ri < P.desc.read_count; ++ri) {
            uint32_t res = P.desc.reads[ri];
            int writer_idx = -1;
            for (int wi = 0; wi < count_; ++wi) {
                const Entry& W = entries_[wi];
                for (int k = 0; k < W.desc.write_count; ++k)
                    if (W.desc.writes[k] == res) writer_idx = wi;  // take latest
            }
            if (writer_idx < 0 || writer_idx == pi) continue;  // no writer, or self (RMW)
            if (writer_idx > pi) {
                MD_LOG(MD_LOG_WARNING,
                       "[JobGraph] '%s' reads resource 0x%08x but writer '%s' "
                       "is registered AFTER it", P.name, res, entries_[writer_idx].name);
            }
        }
    }

    // Wave assignment: wave[i] = 1 + max(wave[j]) over every j < i this
    // batch Conflicts() with, or 0 if none. Same-wave batches have no
    // declared conflict with each other or with anything in an earlier
    // wave, so they're independent enough to run concurrently (see the
    // header's structural-op caveat — declared-independent is necessary,
    // not sufficient, for a batch that touches MdRegistry directly).
    int wave[MAX_BATCHES] = {};
    int max_wave = 0;
    for (int i = 0; i < count_; ++i) {
        int w = 0;
        for (int j = 0; j < i; ++j)
            if (Conflicts(entries_[j].desc, entries_[i].desc) && wave[j] + 1 > w)
                w = wave[j] + 1;
        wave[i] = w;
        if (w > max_wave) max_wave = w;
    }

    // Run wave by wave: a wave with 2+ batches goes through JobSystem
    // (each batch's fn() runs on a worker thread, same contract as any
    // other JobSystem::Submit caller — no registry access mid-flight
    // unless hand-verified safe); a lone batch just runs inline, same
    // NumWorkers()==0 fallback shape used elsewhere (e.g. logic_tick.cpp's
    // TickNavigation) so test binaries without JobSystem::Init() still work.
    for (int w = 0; w <= max_wave; ++w) {
        int wave_count = 0;
        for (int i = 0; i < count_; ++i) if (wave[i] == w) ++wave_count;

        if (wave_count > 1 && JobSystem::Get().NumWorkers() > 0) {
            for (int i = 0; i < count_; ++i)
                if (wave[i] == w) JobSystem::Get().Submit(entries_[i].fn, entries_[i].user);
            JobSystem::Get().Flush();
        } else {
            for (int i = 0; i < count_; ++i)
                if (wave[i] == w) entries_[i].fn(entries_[i].user);
        }
    }

    count_ = 0;
}

} // namespace md
