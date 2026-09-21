#pragma once

// -----------------------------------------------------------------------------------------------
// Multi-metric per-worker telemetry framework, keyed by a Metric enum 
// defined in the translation unit (see the "requirements" comment
// further down, right above `template <typename Metric> class Telemetry`,
// for the exact contract your enum needs to satisfy).
//
// This header has no domain knowledge -- no metric names, no notion of
// what's being measured. Telemetry<Metric> is a class TEMPLATE;
// instantiate it with your own enum, typically via a local alias:
//
//     enum class Metric : std::uint32_t { WalkCount, Kappa, Count_ };
//     using Telemetry = ROMBO::TELEMETRY::Telemetry<Metric>;
//
// Everything below uses bare "Telemetry" and "Metric::WalkCount" etc. as
// if that alias and enum already exist in your file -- because in your
// file, they should actually do.
//
// Intended for very hot loops called from many threads (e.g. a per-sample /
// per-walk render kernel). Each thread gets its own zero-atomic buffer via
// thread_local storage; a reporter thread walks all registered workers on
// its own throttled cadence and merges them into one aggregate Report.
//
// 
// Hot path (any worker thread, any number of calls):
//
//     Telemetry& tel = Telemetry::local();
//     tel.count(Metric::WalkCount);
//     tel.add(Metric::Kappa, kappa_value);
//     tel.track_extreme(Metric::Nu0, diffusion_length);
//     tel.rotate_every(512);   // this thread's own interval cadence
//
// No atomics are performed by count() / add() / track_extreme(). rotate()
// (via rotate_every()) touches one atomic CAS + one atomic store, but only
// on this worker's own buffers -- never on another thread's.
//
// Each thread's active buffer belongs exclusively to that thread -- no two
// threads ever touch the same Data, so there is nothing to contend on.
// 
// IMPORTANT: rotate() must only ever be called by the buffer's owning
// thread. collect_all() (below) deliberately never calls it on the
// reporter's behalf -- doing so would race with the owner. See rotate()'s
// own comment for the failure mode this avoids.
//
// 
// Cold path (reporter thread only, OR any thread via a cadence gate):
//
//     Telemetry::Report merged = Telemetry::collect_all();
//     merged.average(Metric::Kappa);
//     merged.minimum(Metric::Nu0);
//
// For the "whichever thread happens to be the Nth one reports" pattern
// (matching the original demo's inline "% 512" gate), use should_report()
// it's safe to call it concurrently from many threads, carries no metric data,
// purely decides whose turn it is:
//
//     if (Telemetry::should_report(512)) {
//         const auto agg = Telemetry::collect_all();
//         // ... print agg ...
//     }
//
// 
// Ownership per worker buffer:
//
//     FREE -> ACTIVE -> READY -> CONSUMING -> FREE
//
// 
// THREADING MODEL ASSUMED: Transients-safe double-buffer execution. Dynamic threads 
// automatically harvest remaining active/ready metrics during thread exit (~Telemetry) 
// directly into a global accumulator via heap-allocated registry nodes, protecting against 
// dangling pointers, memory corruption, and infinitely stalling flush() calls.
// -----------------------------------------------------------------------------------------------

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>

namespace ROMBO {
namespace TELEMETRY {

// -----------------------------------------------------------------------------------------------
// Telemetry<Metric> is deliberately generic: it knows nothing about what
// it's measuring. Define the Metric enum next to the code that actually 
// calls count()/add()/track_extreme() etc.
//
// REQUIREMENTS for Metric enum class:
//   - enum class Metric : some_integral_type { ... , Count_ };
//   - Count_ must be the LAST enumerator, equal to the number of real
//     metrics before it (the usual "sentinel" idiom -- if you don't
//     explicitly assign values, this falls out for free).
//
// Typical use, entirely in the translation unit:
//
//     #include "telemetry_debug.h"
//     using namespace ROMBO::TELEMETRY;
//
//     enum class KernelMetric : std::uint32_t
//     {
//         WalkCount,
//         Kappa,
//         Count_
//     };
//     enum class MemoryMetric : std::uint32_t
//     {
//          Alloc,
//          Free,
//          Count_
//     };
//
//     using KernelTelemetry = Telemetry<KernelMetric>;
//     using MemoryTelemetry = Telemetry<MemoryMetric>;
//
//     KernelTelemetry& ktel = KernelTelemetry::local();
//     ktel.count(KernelMetric::WalkCount);
//     ktel.add(KernelMetric::Kappa, kappa_value);
//     
//     MemoryTelemetry& mtel = MemoryTelemetry::local();
//     // mtel ..
//
// Bonus property of templating on Metric rather than hardcoding one enum:
// if two unrelated parts of a codebase each define their own Metric enum
// and both do `using Telemetry = ROMBO::TELEMETRY::Telemetry<Metric>;`,
// they get FULLY SEPARATE registries, cadence counters, and thread-local
// storage -- Telemetry<MetricA> and Telemetry<MetricB> are different
// types, so nothing is shared between them even though both live in the
// same process. You don't have to reason about cross-contamination
// between independent debug subsystems; the type system keeps them apart.
// -----------------------------------------------------------------------------------------------

// -----------------------------------------------------------------------------------------------
// ARCHITECTURE OVERVIEW: Telemetry<Metric>
// -----------------------------------------------------------------------------------------------
// A zero-contention, thread-local instrumentation framework. Each worker thread writes
// to its own private buffers, allowing high-frequency data collection without locks, 
// atomics, or cache-line bouncing.
//
// PUBLIC API:
//   - local()             : Access this thread's Telemetry instance.
//   - count() / add()     : Hot-path: Increment/Accumulate (no synchronization).
//   - track_extreme()     : Update min/max (no synchronization).
//   - rotate_every()      : Throttle-based buffer rotation.
//   - flush()             : Wait-free buffer drain (safe for transient threads).
//   - collect_all()       : Cold-path: Aggregate ready data from all threads.
//   - should_report()     : Thread-safe cadence gate for reporting logic.
//   - set_tag() / find_by_tag() : Optional thread identification.
//
// INTERNAL STATE:
//   - Buffers[2]          : Double-buffer system (FREE/ACTIVE/READY/CONSUMING state).
//   - Registry            : Lock-free intrusive node list linking all thread instances.
//   - Local State         : Per-thread ticks, active index, and error tracking.
//
// SUPPORT STRUCTURES:
//   - Slot                : Atomized metrics (count, sum, min, max).
//   - Report              : Aggregated snapshot across all workers.
// -----------------------------------------------------------------------------------------------

template <typename Metric>
class Telemetry
{
public:

    // Total number of tracked metrics, derived from YOUR enum's Count_
    // sentinel. Visible to nested Slot/Report/Data before they're even
    // declared below, thanks to C++'s complete-class context for member
    // lookup within a class body -- declaration order doesn't matter here.
    static constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count_);

    // Ensure type-safety: restrict the template to enum classes only.
    static_assert(std::is_enum<Metric>::value, "Telemetry requires an enum class.");

    // -------------------------------------------------------------------------------------------
    // Pure counters just never touch sum/min/max.
    // -------------------------------------------------------------------------------------------
    struct Slot
    {
        std::uint64_t count = 0;
        std::int64_t  sum   = 0;
        std::int64_t  min   = std::numeric_limits<std::int64_t>::max();
        std::int64_t  max   = std::numeric_limits<std::int64_t>::lowest();
    };


    // -------------------------------------------------------------------------------------------
    // A completed, merged chunk of telemetry across all worker threads.
    // Plain data -- hand it to whatever reporting system used.
    // -------------------------------------------------------------------------------------------
    struct Report
    {
        Slot slots[kMetricCount];


        std::uint64_t count(Metric m) const noexcept
        {
            return slots[index(m)].count;
        }


        double average(Metric m) const noexcept
        {
            const Slot& s = slots[index(m)];
            if (s.count == 0) return 0.0;
            return static_cast<double>(s.sum) / static_cast<double>(SCALE) / static_cast<double>(s.count);
        }


        // Returns 0.0 if no sample ever updated this slot's min.
        double minimum(Metric m) const noexcept
        {
            const Slot& s = slots[index(m)];
            if (s.min == std::numeric_limits<std::int64_t>::max()) return 0.0;
            return static_cast<double>(s.min) / static_cast<double>(SCALE);
        }


        // Returns 0.0 if no sample ever updated this slot's max.
        double maximum(Metric m) const noexcept
        {
            const Slot& s = slots[index(m)];
            if (s.max == std::numeric_limits<std::int64_t>::lowest()) return 0.0;
            return static_cast<double>(s.max) / static_cast<double>(SCALE);
        }


        // Fold one worker's completed buffer into this aggregate. 
        // Reporter-side only; not performance sensitive.
        void merge(const Slot (&other)[kMetricCount]) noexcept
        {
            for (std::size_t i = 0; i != kMetricCount; ++i)
            {
                Slot&       dst = slots[i];
                const Slot& src = other[i];

                dst.count += src.count;
                dst.sum   += src.sum;
                if (src.min < dst.min) dst.min = src.min;
                if (src.max > dst.max) dst.max = src.max;
            }
        }
    };


    // -------------------------------------------------------------------------------------------
    // Thread-local accessor -- the ONLY way to obtain a Telemetry.
    //
    // First call on a given thread constructs that thread's instance and
    // registers it (one CAS, once per thread, not on the hot path).
    // Constructor is private specifically so nothing can construct a
    // Telemetry outside of this thread_local, which is what keeps the
    // "each thread owns exactly one buffer, forever" contract enforceable
    // rather than just documented.
    // -------------------------------------------------------------------------------------------
    static Telemetry& local() noexcept
    {
        thread_local Telemetry instance;
        return instance;
    }


    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;


    // -------------------------------------------------------------------------------------------
    // DESTRUCTOR -- automatically executed when worker or transient threads terminate.
    //
    // Harvests all remaining metrics from both active and secondary buffers directly
    // into the global retired accumulator. Marks the node as retired so cold-path
    // scans bypass it cleanly, avoiding dangling memory access or stranded metrics.
    // -------------------------------------------------------------------------------------------
    ~Telemetry() noexcept
    {
        if (node_)
        {
            node_->is_retired.store(true, std::memory_order_release);
            node_->instance.store(nullptr, std::memory_order_release);
        }
        harvest_all_buffers_to_global();
    }


    // -------------------------------------------------------------------------------------------
    // HOT PATH -- called from any worker thread, any number of times, from
    // many threads simultaneously across different Telemetry::local() instances.
    //
    // No atomic operations. No CAS. No cross-thread writes.
    // -------------------------------------------------------------------------------------------
    inline void count(Metric m) noexcept
    {
        debug_check_owner();
        ++active().slots[index(m)].count;
    }


    inline void add(Metric m, float value) noexcept
    {
        debug_check_owner();
        Slot& s = active().slots[index(m)];
        const std::int64_t fv = fixed(value);
        ++s.count;
        s.sum += fv;
        if (fv < s.min) s.min = fv;
        if (fv > s.max) s.max = fv;
    }


    inline void track_extreme(Metric m, float value) noexcept
    {
        debug_check_owner();
        Slot& s = active().slots[index(m)];
        const std::int64_t fv = fixed(value);
        if (fv < s.min) s.min = fv;
        if (fv > s.max) s.max = fv;
    }


    // -------------------------------------------------------------------------------------------
    // HOT PATH (still) -- but a rare-path operation. Call this from the
    // OWNING worker thread, on its OWN Telemetry::local(), to publish its
    // current buffer and switch to the other one. This mirrors the demo's
    // "% 512" cadence: increments a plain (non-atomic, thread-local -- no
    // cost beyond a compare) counter and rotates every `interval` calls.
    //
    // CRITICAL: rotate() must NEVER be called on behalf of another thread's
    // Telemetry -- see rotate()'s own comment for why. This is the only
    // sanctioned way to trigger a rotation; collect_all() deliberately does
    // NOT rotate anything.
    // -------------------------------------------------------------------------------------------
    void rotate_every(std::uint64_t interval) noexcept
    {
        debug_check_owner();
        if (interval == 0) return;
        if (++local_tick_ >= interval)
        {
            local_tick_ = 0;
            rotate();
        }
    }


    // -------------------------------------------------------------------------------------------
    // SHUTDOWN / FLUSH -- call from the OWNING worker thread when it has no more
    // samples to contribute (e.g. end of a render session, or transient thread exit).
    //
    // Extended for transient thread safety: attempts standard rotation first. If the 
    // secondary buffer is currently held by a lagging reporter thread, it bypasses
    // spin-wait stalling by directly harvesting the active buffer into the central 
    // retired accumulator. Guaranteed non-blocking.
    // -------------------------------------------------------------------------------------------
    void flush() noexcept
    {
        debug_check_owner();
        if (rotate()) return;

        // Reporter still owns secondary buffer; directly drain active buffer into fallback
        harvest_active_buffer_to_global();
    }


    // -------------------------------------------------------------------------------------------
    // COLD PATH -- reporter thread only.
    //
    // Walks every registered worker and report()s each one, folding
    // whatever is currently READY into the returned Report. Deliberately
    // does NOT call rotate() on anyone's behalf -- see rotate()'s comment.
    // A worker that hasn't rotated recently (or is between rotate_every()
    // triggers) simply contributes nothing THIS call; its data is still
    // sitting in its own active buffer and will show up once that worker
    // rotates on its own thread.
    //
    // Automatically merges persistent aggregate totals accumulated from retired transient threads.
    // -------------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------------
    // collect_active() / retired_total() / collect_all() -- three
    // distinct primitives, deliberately separated. They exist because
    // "gather everything right now" mixes two genuinely different
    // safety properties, and treating them as one operation is exactly
    // what caused a real, confirmed bug: an incremental reporter that
    // called the combined operation repeatedly and merged every result
    // into a running total over-counted by 3-4x the moment any thread
    // retired mid-run, because the retired portion re-appeared, in
    // full, in every subsequent call.
    //
    // collect_active(): walks the registry, draining ONLY still-active
    // (non-retired) threads' rotated buffers via report() -- the same
    // one-time-consuming mechanism as before. NEVER touches
    // global_retired_data(). Safe to call as often as you like and
    // merge every result into a running accumulator -- each call's
    // data is genuinely fresh, non-overlapping with any prior call.
    // -------------------------------------------------------------------------------------------
    static Report collect_active() noexcept
    {
        Report out{};

        for (Node* curr = registry_head().load(std::memory_order_acquire);
             curr != nullptr;
             curr = curr->next.load(std::memory_order_acquire))
        {
            if (curr->is_retired.load(std::memory_order_acquire)) continue;

            Telemetry* t = curr->instance.load(std::memory_order_acquire);
            if (!t) continue;

            Slot snapshot[kMetricCount];
            if (t->report(snapshot))
            {
                out.merge(snapshot);
            }
        }

        return out;
    }

    // -------------------------------------------------------------------------------------------
    // retired_total(): a pure peek at the historical, ever-growing total
    // accumulated from threads that have already exited. Safe to READ at
    // any time, from any thread -- but it is a snapshot of CURRENT
    // state, not a delta, so it must be merged into an accumulator AT
    // MOST ONCE per logical total you're computing. Merging it
    // repeatedly reproduces the exact over-counting bug this split
    // exists to prevent. Typical safe use: read it once, at the very
    // end of a run, after every worker thread has exited.
    // -------------------------------------------------------------------------------------------
    static Report retired_total() noexcept
    {
        Report out{};
        std::lock_guard<std::mutex> lock(global_retired_mutex());
        out.merge(global_retired_data().slots);
        return out;
    }

    // -------------------------------------------------------------------------------------------
    // collect_all(): unchanged signature and behavior from before this
    // split -- a one-shot snapshot of everything, active and retired
    // combined. Correct and sufficient for a caller that calls it
    // EXACTLY ONCE (e.g. a final report after every worker has already
    // joined). Do NOT call this repeatedly and merge each result into a
    // running accumulator -- that is precisely the misuse that caused
    // the over-counting bug. For incremental/repeated accumulation, use
    // collect_active() instead, and read retired_total() separately,
    // exactly once, when you actually need the historical total folded
    // in (typically only at the very end).
    // -------------------------------------------------------------------------------------------
    static Report collect_all() noexcept
    {
        Report out = collect_active();
        out.merge(retired_total().slots);
        return out;
    }


    // -------------------------------------------------------------------------------------------
    // OPTIONAL per-thread tagging -- entirely opt-in.
    // -------------------------------------------------------------------------------------------
    void set_tag(std::uint64_t tag) noexcept
    {
        debug_check_owner();

        tag_value_.store(tag, std::memory_order_relaxed);
        tag_set_.store(true, std::memory_order_release);
    }

    struct Tag
    {
        bool has_value = false;
        std::uint64_t value = 0;
    };

    Tag tag() const noexcept
    {
        if (!tag_set_.load(std::memory_order_acquire))
            return Tag{};

        return Tag{true, tag_value_.load(std::memory_order_relaxed)};
    }


    // -------------------------------------------------------------------------------------------
    // Look up ONE registered thread by its tag and drain just that one buffer.
    // -------------------------------------------------------------------------------------------
    static bool find_by_tag(std::uint64_t tag, Report& out) noexcept
    {
        for (Node* curr = registry_head().load(std::memory_order_acquire);
             curr != nullptr;
             curr = curr->next.load(std::memory_order_acquire))
        {
            if (curr->is_retired.load(std::memory_order_acquire)) continue;

            Telemetry* t = curr->instance.load(std::memory_order_acquire);
            if (!t) continue;

            const auto t_tag = t->tag();
            if (!t_tag.has_value || t_tag.value != tag) continue;

            Slot snapshot[kMetricCount];
            if (!t->report(snapshot)) return false;

            out = Report{};
            for (std::size_t i = 0; i != kMetricCount; ++i)
                out.slots[i] = snapshot[i];
            return true;
        }

        return false;
    }


    // -------------------------------------------------------------------------------------------
    // CADENCE GATE -- call from ANY thread to throttle reporting logic.
    //
    // FIXED: the previous version added an extra "single-winner" CAS
    // retry loop (last_reported_step, guarded by `while (expected <
    // current)`) on top of the fetch_add below -- but that extra loop
    // was not just unnecessary, it was actively buggy. It assumed
    // multiples of `interval` always arrive at the CAS in increasing
    // order. Under real concurrency they don't: if a "fast" thread's
    // LARGER current value wins its CAS before a "slower" thread's
    // smaller (but still legitimately valid) current value gets a
    // chance, the slow thread's retry sees `expected >= current`, the
    // while-loop condition goes false, and it silently exits without
    // reporting -- a real, valid cadence trigger just vanishes, no
    // signal, no crash. Confirmed both in production output (6 reports
    // instead of ~80, with deltas 10-20x larger than the configured
    // interval) and reproduced deterministically in isolation.
    //
    // The fix is to DELETE that extra logic entirely. fetch_add's return
    // value is already, by itself, unique per caller -- no two threads
    // can ever receive the same `current` from an atomic fetch_add. That
    // uniqueness alone guarantees at most one thread's `current` can
    // ever equal any specific multiple of `interval`, with no additional
    // cross-thread ordering enforcement needed. Simpler, and correct.
    // -------------------------------------------------------------------------------------------
    static bool should_report(std::uint64_t interval) noexcept
    {
        if (interval == 0) return false;
        const std::uint64_t current = report_cadence().fetch_add(1, std::memory_order_relaxed) + 1;
        return (current % interval) == 0;
    }


    // -------------------------------------------------------------------------------------------
    // Diagnostic: how many times THIS worker's rotate() found the other buffer busy.
    // -------------------------------------------------------------------------------------------
    std::uint64_t failed_rotates() const noexcept
    {
        return failed_rotates_.load(std::memory_order_relaxed);
    }


private:

    static constexpr std::int64_t SCALE = 1000000; // 1e6 fixed-point scale

    static std::size_t index(Metric m) noexcept
    {
        return static_cast<std::size_t>(m);
    }

    static std::int64_t fixed(float value) noexcept
    {
        return static_cast<std::int64_t>(value * static_cast<float>(SCALE));
    }


    // -------------------------------------------------------------------------------------------
    // Intrusive Heap Node for safely tracking transient threads across exit boundaries
    // -------------------------------------------------------------------------------------------
    struct Node
    {
        std::atomic<bool> is_retired{false};
        std::atomic<Node*> next{nullptr};
        // Was a plain Telemetry*, confirmed racy: written non-atomically
        // by ~Telemetry() and read non-atomically by collect_all()/
        // find_by_tag() from other threads, with is_retired's own
        // acquire/release doing nothing to protect this SEPARATE field.
        std::atomic<Telemetry*> instance{nullptr};
    };


    // -------------------------------------------------------------------------------------------
    // Internal double-buffer machinery.
    // -------------------------------------------------------------------------------------------
    enum class State : std::uint8_t { FREE, ACTIVE, READY, CONSUMING };

    struct Data
    {
        Slot slots[kMetricCount];
    };

    struct alignas(64) Buffer
    {
        alignas(64) Data data;
        alignas(64) std::atomic<State> state{State::FREE};
    };


    Telemetry() noexcept
    {
        buffers_[0].state.store(State::ACTIVE, std::memory_order_relaxed);
        buffers_[1].state.store(State::FREE,   std::memory_order_relaxed);

#ifndef NDEBUG
        owner_thread_ = std::this_thread::get_id();
#endif

        node_ = new Node();
        node_->instance.store(this, std::memory_order_release);
        register_self();
    }


    Data& active() noexcept
    {
        return buffers_[active_buffer_].data;
    }


    bool rotate() noexcept
    {
        debug_check_owner();

        const std::uint32_t current = active_buffer_;
        const std::uint32_t next    = current ^ 1u;

        Buffer& current_buffer = buffers_[current];
        Buffer& next_buffer    = buffers_[next];

        State expected = State::FREE;
        if (!next_buffer.state.compare_exchange_strong(
                expected, State::ACTIVE,
                std::memory_order_acquire, std::memory_order_relaxed))
        {
            failed_rotates_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        current_buffer.state.store(State::READY, std::memory_order_release);
        active_buffer_ = next;
        return true;
    }


    bool report(Slot (&out)[kMetricCount]) noexcept
    {
        for (std::uint32_t i = 0; i != 2; ++i)
        {
            Buffer& buffer = buffers_[i];
            State expected = State::READY;

            if (!buffer.state.compare_exchange_strong(
                    expected, State::CONSUMING,
                    std::memory_order_acquire, std::memory_order_relaxed))
            {
                continue;
            }

            for (std::size_t s = 0; s != kMetricCount; ++s)
                out[s] = buffer.data.slots[s];

            reset(buffer.data);

            buffer.state.store(State::FREE, std::memory_order_release);
            return true;
        }

        return false;
    }


    static void reset(Data& data) noexcept
    {
        for (std::size_t i = 0; i != kMetricCount; ++i)
            data.slots[i] = Slot{};
    }


    // -------------------------------------------------------------------------------------------
    // Force-harvest active buffer directly into global retired storage during non-blocking flush.
    // -------------------------------------------------------------------------------------------
    void harvest_active_buffer_to_global() noexcept
    {
        Report temp{};
        Data& act = active();
        for (std::size_t i = 0; i < kMetricCount; ++i)
        {
            temp.slots[i] = act.slots[i];
        }
        reset(act);

        std::lock_guard<std::mutex> lock(global_retired_mutex());
        global_retired_data().merge(temp.slots);
    }


    // -------------------------------------------------------------------------------------------
    // Harvest both buffers during thread destruction (~Telemetry).
    //
    // FIXED: previously read buffers_[b].data.slots directly, for BOTH
    // buffers, with ZERO regard for their state -- if the non-active
    // buffer happened to be READY or (worse) CONSUMING at this exact
    // moment, this raced directly against a concurrent report() call
    // from another thread's collect_all(), which reads AND writes
    // (reset()) that same data. Confirmed via TSan with a precisely
    // orchestrated interleaving (5 separate field-level races caught).
    //
    // The active buffer is exclusively ours -- no other thread ever
    // touches an ACTIVE buffer (an invariant this class relies on
    // everywhere else too), so reading it directly here is safe.
    //
    // The OTHER buffer is claimed via the EXACT SAME CAS protocol
    // report() uses. If we win (it was READY), we safely own it. If we
    // lose -- it was FREE (nothing to harvest) or already CONSUMING (a
    // reporter got there first) -- we simply don't touch it. Losing
    // costs nothing: that data isn't lost, it reaches the reporter's own
    // collect_all() output instead. This is what makes it safe to run
    // concurrently with an in-flight report() on this same instance,
    // instead of racing against it.
    // -------------------------------------------------------------------------------------------
    void harvest_all_buffers_to_global() noexcept
    {
        Report temp{};

        temp.merge(active().slots);

        const std::uint32_t other = active_buffer_ ^ 1u;
        Buffer& other_buffer = buffers_[other];
        State expected = State::READY;
        if (other_buffer.state.compare_exchange_strong(
                expected, State::CONSUMING,
                std::memory_order_acquire, std::memory_order_relaxed))
        {
            temp.merge(other_buffer.data.slots);
            // No reset() / no restoring FREE needed -- the whole object
            // is being destroyed; nothing will ever look at this
            // buffer's state again after this function returns.
        }

        std::lock_guard<std::mutex> lock(global_retired_mutex());
        global_retired_data().merge(temp.slots);
    }


    // -------------------------------------------------------------------------------------------
    // Registry: lock-free intrusive stack of Nodes linking every thread.
    // -------------------------------------------------------------------------------------------
    void register_self() noexcept
    {
        Node* old_head = registry_head().load(std::memory_order_relaxed);
        do
        {
            node_->next.store(old_head, std::memory_order_relaxed);
        }
        while (!registry_head().compare_exchange_weak(
                    old_head, node_,
                    std::memory_order_release, std::memory_order_relaxed));
    }


    static std::atomic<Node*>& registry_head() noexcept
    {
        static std::atomic<Node*> head{nullptr};
        return head;
    }


    static std::atomic<std::uint64_t>& report_cadence() noexcept
    {
        static std::atomic<std::uint64_t> cadence{0};
        return cadence;
    }


    // -------------------------------------------------------------------------------------------
    // Global fallback storage for harvesting data from dead transient threads.
    // -------------------------------------------------------------------------------------------
    static Report& global_retired_data() noexcept
    {
        static Report data{};
        return data;
    }

    static std::mutex& global_retired_mutex() noexcept
    {
        static std::mutex m;
        return m;
    }


    Node* node_ = nullptr;
    Buffer buffers_[2];

    std::uint32_t active_buffer_ = 0;
    std::uint64_t local_tick_ = 0;

    std::atomic<std::uint64_t> failed_rotates_{0};

    std::atomic<bool> tag_set_{false};
    std::atomic<std::uint64_t> tag_value_{0};

#ifndef NDEBUG
    std::thread::id owner_thread_{};

    void debug_check_owner() const noexcept
    {
        assert(std::this_thread::get_id() == owner_thread_);
    }
#else
    void debug_check_owner() const noexcept {}
#endif
};

} // namespace TELEMETRY
} // namespace ROMBO