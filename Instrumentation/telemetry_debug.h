#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ROMBO {
namespace TELEMETRY {
/*
 * =============================================================================
 *                  DIRECT ATOMIC ARRAY TELEMETRY ARCHITECTURE
 * =============================================================================
 *
 * 1. MEMORY ARCHITECTURE & LAYOUT
 * -----------------------------------------------------------------------------
 * Memory is allocated statically in a single contiguous block owned by the 
 * global Telemetry instance.
 *
 *   Telemetry<Metric, MaxThreads> Instance
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │ slot_used_ [MaxThreads]  -->  [ true  |  true  | false | ... ]          │
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │ tags_      [MaxThreads]  -->  [ Tag 0 | Tag 1  | Tag 2 | ... ]          │
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │ slots_     [MaxThreads]  (Cache-Line Aligned Array)                     │
 *   │                                                                         │
 *   │  ┌───────────────────────────────────────────────────────────────────┐  │
 *   │  │ ThreadStorage [Slot 0]  (64-byte Cache Line #0)                   │  │
 *   │  │ ┌───────────────────────────────────────────────────────────────┐ │  │
 *   │  │ │ AtomicSlot[0]: count | sum | min | max                        │ │  │
 *   │  │ │ AtomicSlot[1]: count | sum | min | max                        │ │  │
 *   │  │ └───────────────────────────────────────────────────────────────┘ │  │
 *   │  └───────────────────────────────────────────────────────────────────┘  │
 *   │  ┌───────────────────────────────────────────────────────────────────┐  │
 *   │  │ ThreadStorage [Slot 1]  (64-byte Cache Line #1)                   │  │
 *   │  │ ┌───────────────────────────────────────────────────────────────┐ │  │
 *   │  │ │ AtomicSlot[0]: count | sum | min | max                        │ │  │
 *   │  │ │ AtomicSlot[1]: count | sum | min | max                        │ │  │
 *   │  │ └───────────────────────────────────────────────────────────────┘ │  │
 *   │  └───────────────────────────────────────────────────────────────────┘  │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * 2. EXECUTION MECHANICS & LIFECYCLE
 * -----------------------------------------------------------------------------
 * A. Slot Claiming (Executes ONCE per Thread Lifetime):
 *    - Worker thread calls Telemetry::local().count(...) on first entry.
 *    - get_thread_slot() checks thread_local std::size_t slot_idx.
 *    - On first run, it scans slot_used_ using compare_exchange_strong (CAS).
 *    - Once claimed, the index is cached in thread-local storage for subsequent calls.
 *
 * B. Hot Path Execution (Executes on Every Metric Record):
 *    - Retrieves cached slot index (~1-3 assembly instructions via segment register).
 *    - Directly updates target slot: slots_[slot_idx].metrics[metric_idx].
 *    - Performs lock-free fetch_add using std::memory_order_relaxed.
 *    - Operates purely in local CPU core L1 cache with zero mutex stalls or locks.
 *
 * C. Reporting & Aggregation (On-Demand Read):
 *    - Reader invokes Telemetry::collect_all().
 *    - Iterates active slots (slot_used_[i] == true) and reads relaxed atomic values.
 *    - Merges snapshots into a single Report structure without blocking writers.
 *
 * 3. KEY HARDWARE GUARANTEES
 * -----------------------------------------------------------------------------
 * - Lifetime Decoupling : Static slot storage prevents use-after-free or data loss
 *                         when transient worker threads terminate mid-run.
 * - False Sharing Fix   : alignas(64) forces each ThreadStorage block onto its own 
 *                         hardware L1 cache line, preventing CPU cache bouncing.
 * - Fixed-Point Atomics : Scale factor (1e6) converts floats to int64_t for rapid, 
 *                         lock-free hardware atomic additions.
 * =============================================================================
 */
 
/**
 * @brief High-Performance, Lock-Free, Cache-Coherent Telemetry Subsystem.
 * 
 * DESIGN ARCHITECTURE: Direct Atomic Array
 * -----------------------------------------------------------------------------
 * 1. Lock-Free & Exception-Safe: Zero mutexes, allocations, or locking primitives.
 * 2. Static Thread Registration: Transient worker threads dynamically claim a fixed 
 *    slot via CAS (`compare_exchange_strong`) on their first record call.
 * 3. Lifetime Decoupling: Thread slots are owned by the static Telemetry instance,
 *    making it impossible for worker thread exit/destruction to corrupt telemetry state.
 * 4. Cache Isolation: Each thread slot container is aligned to a 64-byte boundary 
 *    (alignas(64)), isolating per-thread slots to prevent L1 cache line false sharing.
 * 
 * @tparam Metric Enum class containing metric identifiers (must end with `Count_`).
 * @tparam MaxThreads Maximum capacity for concurrently active/registered worker threads.
 */
template <typename Metric, std::size_t MaxThreads = 128>
class Telemetry
{
public:
    /// Total number of metrics declared in the target enum class.
    static constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count_);
    static_assert(std::is_enum<Metric>::value, "Telemetry requires an enum class.");

    /**
     * @brief Immutable, non-atomic snapshot container for a single metric slot.
     */
    struct Slot
    {
        std::uint64_t count = 0;
        std::int64_t  sum   = 0;
        std::int64_t  min   = std::numeric_limits<std::int64_t>::max();
        std::int64_t  max   = std::numeric_limits<std::int64_t>::lowest();
    };

    /**
     * @brief Aggregated metric report spanning thread slots or tagged slices.
     */
    struct Report
    {
        Slot slots[kMetricCount];

        /// Returns the total occurrence count for a specific metric.
        std::uint64_t count(Metric m) const noexcept {
            return slots[index(m)].count;
        }

        /// Computes the average value for a metric, unpacked from fixed-point representation.
        double average(Metric m) const noexcept {
            const Slot& s = slots[index(m)];
            if (s.count == 0) return 0.0;
            return static_cast<double>(s.sum) / static_cast<double>(SCALE) / static_cast<double>(s.count);
        }

        /// Unpacks and returns the recorded minimum value for a metric.
        double minimum(Metric m) const noexcept {
            const Slot& s = slots[index(m)];
            if (s.min == std::numeric_limits<std::int64_t>::max()) return 0.0;
            return static_cast<double>(s.min) / static_cast<double>(SCALE);
        }

        /// Unpacks and returns the recorded maximum value for a metric.
        double maximum(Metric m) const noexcept {
            const Slot& s = slots[index(m)];
            if (s.max == std::numeric_limits<std::int64_t>::lowest()) return 0.0;
            return static_cast<double>(s.max) / static_cast<double>(SCALE);
        }

        /// Merges an external per-thread array of raw slots into this aggregate report.
        void merge(const Slot (&other)[kMetricCount]) noexcept {
            for (std::size_t i = 0; i != kMetricCount; ++i) {
                Slot&       dst = slots[i];
                const Slot& src = other[i];
                dst.count += src.count;
                dst.sum   += src.sum;
                if (src.min < dst.min) dst.min = src.min;
                if (src.max > dst.max) dst.max = src.max;
            }
        }
    };

    /// Singleton accessor for the Telemetry instance associated with this Metric enum type.
    static Telemetry& instance() noexcept {
        static Telemetry global_instance;
        return global_instance;
    }

    /// Alias for instance() to support ergonomic `Telemetry::local().count(...)` syntax.
    static Telemetry& local() noexcept {
        return instance();
    }

    /**
     * @brief Hot-Path: Increments the occurrence count for a metric.
     * Uses std::memory_order_relaxed for maximum hardware throughput.
     */
    inline void count(Metric m) noexcept {
        const std::size_t slot = get_thread_slot();
        slots_[slot].metrics[index(m)].count.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Hot-Path: Records a floating-point sample into count, sum, min, and max.
     * Converts values into 64-bit fixed-point integers to allow lock-free atomic additions.
     */
    inline void add(Metric m, float value) noexcept {
        const std::size_t slot = get_thread_slot();
        const std::size_t idx  = index(m);
        const std::int64_t fv  = fixed(value);

        slots_[slot].metrics[idx].count.fetch_add(1, std::memory_order_relaxed);
        slots_[slot].metrics[idx].sum.fetch_add(fv, std::memory_order_relaxed);
        atomic_min(slots_[slot].metrics[idx].min, fv);
        atomic_max(slots_[slot].metrics[idx].max, fv);
    }

    /**
     * @brief Hot-Path: Tracks extrema (min/max) without incrementing count or sum.
     */
    inline void track_extreme(Metric m, float value) noexcept {
        const std::size_t slot = get_thread_slot();
        const std::size_t idx  = index(m);
        const std::int64_t fv  = fixed(value);

        atomic_min(slots_[slot].metrics[idx].min, fv);
        atomic_max(slots_[slot].metrics[idx].max, fv);
    }

    /**
     * @brief Binds a user-defined identifier (e.g., worker ID) to the calling thread's slot.
     * Uses release memory order to make tag publication visible to reader threads.
     */
    inline void set_tag(std::uint64_t tag) noexcept {
        const std::size_t slot = get_thread_slot();
        tags_[slot].value.store(tag, std::memory_order_relaxed);
        tags_[slot].set.store(true, std::memory_order_release);
    }

    /// Legacy API Stubs kept for drop-in backward compatibility with buffered interface.
    inline void rotate_every(std::uint64_t) noexcept {}
    inline void flush() noexcept {}

    /**
     * @brief Queries metrics recorded specifically by a thread registered under the given tag.
     * @return true if a matching slot was found and copied into `out`; false otherwise.
     */
    static bool find_by_tag(std::uint64_t tag, Report& out) noexcept {
        Telemetry& self = instance();

        for (std::size_t t = 0; t < MaxThreads; ++t) {
            if (!self.tags_[t].set.load(std::memory_order_acquire)) continue;
            if (self.tags_[t].value.load(std::memory_order_relaxed) != tag) continue;

            out = Report{};
            for (std::size_t i = 0; i < kMetricCount; ++i) {
                out.slots[i].count = self.slots_[t].metrics[i].count.load(std::memory_order_relaxed);
                out.slots[i].sum   = self.slots_[t].metrics[i].sum.load(std::memory_order_relaxed);
                out.slots[i].min   = self.slots_[t].metrics[i].min.load(std::memory_order_relaxed);
                out.slots[i].max   = self.slots_[t].metrics[i].max.load(std::memory_order_relaxed);
            }
            return true;
        }
        return false;
    }

    /**
     * @brief Scans all active slots across all registered threads and aggregates their data.
     * Lock-free snapshot: reader thread iterates over active slots without blocking writers.
     */
    static Report collect_all() noexcept {
        Report out{};
        Telemetry& self = instance();

        for (std::size_t t = 0; t < MaxThreads; ++t) {
            if (!self.slot_used_[t].load(std::memory_order_relaxed)) continue;

            Slot snapshot[kMetricCount];
            for (std::size_t i = 0; i < kMetricCount; ++i) {
                snapshot[i].count = self.slots_[t].metrics[i].count.load(std::memory_order_relaxed);
                snapshot[i].sum   = self.slots_[t].metrics[i].sum.load(std::memory_order_relaxed);
                snapshot[i].min   = self.slots_[t].metrics[i].min.load(std::memory_order_relaxed);
                snapshot[i].max   = self.slots_[t].metrics[i].max.load(std::memory_order_relaxed);
            }
            out.merge(snapshot);
        }
        return out;
    }

    /**
     * @brief Resets all atomic metrics across all currently registered slots to zero.
     */
    static void clear() noexcept {
        Telemetry& self = instance();
        for (std::size_t t = 0; t < MaxThreads; ++t) {
            if (!self.slot_used_[t].load(std::memory_order_relaxed)) continue;
            for (std::size_t i = 0; i < kMetricCount; ++i) {
                self.slots_[t].metrics[i].count.store(0, std::memory_order_relaxed);
                self.slots_[t].metrics[i].sum.store(0, std::memory_order_relaxed);
                self.slots_[t].metrics[i].min.store(std::numeric_limits<std::int64_t>::max(), std::memory_order_relaxed);
                self.slots_[t].metrics[i].max.store(std::numeric_limits<std::int64_t>::lowest(), std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Helper to regulate reporting frequency across tight inner loops.
     */
    static bool should_report(std::uint64_t interval) noexcept {
        if (interval == 0) return false;
        const std::uint64_t prev = report_cadence().fetch_add(1, std::memory_order_relaxed);
        return (prev + 1) % interval == 0;
    }

private:
    /// Fixed-point scaling factor (1e6 gives micro-unit precision for floating-point metric sums).
    static constexpr std::int64_t SCALE = 1000000;

    /// Atomic primitive container for hot-path counter updates.
    struct AtomicSlot {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::int64_t>  sum{0};
        std::atomic<std::int64_t>  min{std::numeric_limits<std::int64_t>::max()};
        std::atomic<std::int64_t>  max{std::numeric_limits<std::int64_t>::lowest()};
    };

    /// Thread identification tag state.
    struct TagState {
        std::atomic<bool> set{false};
        std::atomic<std::uint64_t> value{0};
    };

    /**
     * @brief Cache line isolation wrapper.
     * Enforces 64-byte alignment on every per-thread block, preventing hardware 
     * false sharing across adjacent CPU cores.
     */
    struct alignas(64) ThreadStorage {
        AtomicSlot metrics[kMetricCount];
        // Explicit padding to ensure the array stays perfectly aligned if 
        // sizeof(AtomicSlot) * kMetricCount is not a multiple of 64.
        char padding[64 - (sizeof(AtomicSlot) * kMetricCount % 64)];
    };

    Telemetry() = default;

    /// Helper to cast enum metric identifiers to array indices.
    static std::size_t index(Metric m) noexcept { return static_cast<std::size_t>(m); }

    /// Converts floating-point sample values to scaled 64-bit fixed-point integers.
    static std::int64_t fixed(float v) noexcept { return static_cast<std::int64_t>(v * static_cast<float>(SCALE)); }

    /**
     * @brief Lazily retrieves or assigns a static slot index for the calling thread.
     * Executes the slot-claiming loop once per thread; subsequent calls hit cached TLS value.
     */
    static std::size_t get_thread_slot() noexcept {
        thread_local std::size_t slot_idx = []() {
            Telemetry& self = instance();
            for (std::size_t i = 0; i < MaxThreads; ++i) {
                bool expected = false;
                // Attempt to claim an unused slot atomically
                if (self.slot_used_[i].compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                    return i;
                }
            }
            // Fallback safety index if thread limit is exceeded
            return static_cast<std::size_t>(0);
        }();
        return slot_idx;
    }

    /// Lock-free compare-and-swap loop for updating tracked minimum values.
    static void atomic_min(std::atomic<std::int64_t>& target, std::int64_t val) noexcept {
        std::int64_t current = target.load(std::memory_order_relaxed);
        while (val < current && !target.compare_exchange_weak(current, val, std::memory_order_relaxed)) {}
    }

    /// Lock-free compare-and-swap loop for updating tracked maximum values.
    static void atomic_max(std::atomic<std::int64_t>& target, std::int64_t val) noexcept {
        std::int64_t current = target.load(std::memory_order_relaxed);
        while (val > current && !target.compare_exchange_weak(current, val, std::memory_order_relaxed)) {}
    }

    /// Global atomic cadence counter for periodic reporting triggers.
    static std::atomic<std::uint64_t>& report_cadence() noexcept {
        static std::atomic<std::uint64_t> cadence{0};
        return cadence;
    }

    // Storage layout: Slot ownership flags, tags, and cache-line aligned metric arrays.
    std::atomic<bool> slot_used_[MaxThreads]{};
    TagState tags_[MaxThreads];
    ThreadStorage slots_[MaxThreads];
};

} // namespace TELEMETRY
} // namespace ROMBO