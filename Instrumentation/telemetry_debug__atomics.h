#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
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
     * @brief Lightweight accessor bound to ONE specific slot, returned by
     * local(key). Cheap to copy (just a pointer + an index) and every
     * accessor call operates directly on the bound slot -- no key is
     * needed again after local(key) resolves it once. This is deliberate:
     * an alternative design could stash the key in thread_local "ambient"
     * state and have count()/add()/etc. re-resolve it on every call, but
     * that's action-at-a-distance -- easy to get subtly wrong if any call
     * site forgets to (re)establish the ambient key first. Binding the
     * slot into the returned value instead makes "which slot am I
     * touching" a property of the object in your hand, not of some
     * thread-local state you have to trust is still correct.
     */
    class Handle
    {
    public:
        inline void count(Metric m) noexcept {
            Telemetry::do_count(owner_->slots_[slot_], index(m));
        }
        inline void add(Metric m, float value) noexcept {
            Telemetry::do_add(owner_->slots_[slot_], index(m), value);
        }
        inline void track_extreme(Metric m, float value) noexcept {
            Telemetry::do_track_extreme(owner_->slots_[slot_], index(m), value);
        }

        /**
         * @brief Reads back this Handle's OWN bound slot directly -- no
         * key rescan needed, unlike find_by_tag(key, ...), which re-walks
         * tags_[] from scratch even when you already hold a Handle that
         * knows exactly which slot it is. Cheaper, and the natural
         * counterpart to count()/add()/track_extreme() above: this Handle
         * both writes AND reads the one slot it's bound to.
         */
        inline Report snapshot() const noexcept {
            return Telemetry::read_slot(owner_->slots_[slot_]);
        }

    private:
        friend class Telemetry;
        Handle(Telemetry* owner, std::size_t slot) noexcept : owner_(owner), slot_(slot) {}
        Telemetry* owner_;
        std::size_t slot_;
    };

    /**
     * @brief Explicit-key accessor: claim-or-find the slot bound to `key`
     * (e.g. sd->tid), independent of which OS thread happens to be
     * calling. Returns a Handle already bound to that slot -- every
     * subsequent .count()/.add()/.track_extreme() call on the returned
     * Handle needs no key repeated.
     *
     * This shares the SAME underlying storage (slot_used_, slots_,
     * tags_) as the plain no-arg local() -- a keyed slot and a thread-
     * implicit slot are just two different ways of reaching into the same
     * fixed MaxThreads-sized pool. collect_all()/find_by_tag() already
     * iterate every claimed slot regardless of how it was claimed, so
     * neither needs any change to see keyed slots too.
     *
     * Fast path (the common case: this key has already been registered by
     * someone, possibly even by this exact call on a prior invocation) is
     * a lock-free scan over tags_[], the same cost as find_by_tag(). A
     * thread_local cache of "last key this thread asked about -> slot"
     * skips even that scan on repeat calls with an unchanging key -- the
     * expected pattern for something like sd->tid, which stays constant
     * for a given worker thread's lifetime.
     *
     * Slow path (the FIRST time a given key is ever seen, by anyone) takes
     * a mutex: without it, two threads could both observe "key not found
     * yet" and each claim a SEPARATE slot for what should be one shared
     * key. Paid once per distinct key, ever -- not once per call. Slot
     * claiming inside the lock still uses compare_exchange_strong (not a
     * plain load-then-store) so it stays correctly arbitrated even against
     * a concurrent thread-implicit claim (via the plain local() path)
     * landing on the same slot_used_[t] at the same moment; the mutex only
     * serializes THIS function against itself, not against the other
     * claiming path, so the CAS is still what actually decides who wins
     * a given index.
     */
    static Handle local(std::uint64_t key) noexcept {
        thread_local std::uint64_t cached_key   = 0;
        thread_local bool          cached_valid = false;
        thread_local std::size_t   cached_slot  = 0;

        Telemetry& self = instance();

        if (cached_valid && cached_key == key) {
            return Handle(&self, cached_slot);
        }

        // Fast path: lock-free scan for an existing registration.
        {
            const std::size_t t = self.find_slot_for_key(key);
            if (t != MaxThreads) {
                cached_key = key; cached_valid = true; cached_slot = t;
                return Handle(&self, t);
            }
        }

        // Slow path: this key has never been seen before (or we lost a
        // race to another thread registering it moments ago -- re-checked
        // below, under the lock).
        {
            std::lock_guard<std::mutex> lock(self.key_claim_mutex());

            const std::size_t existing = self.find_slot_for_key(key);
            if (existing != MaxThreads) {
                cached_key = key; cached_valid = true; cached_slot = existing;
                return Handle(&self, existing);
            }

            for (std::size_t t = 0; t < MaxThreads; ++t) {
                bool expected = false;
                if (self.slot_used_[t].compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                    self.tags_[t].value.store(key, std::memory_order_relaxed);
                    self.tags_[t].set.store(true, std::memory_order_release);
                    cached_key = key; cached_valid = true; cached_slot = t;
                    return Handle(&self, t);
                }
            }
        }

        // Capacity exhausted -- same treatment as get_thread_slot()'s own
        // fallback: assert in debug builds, observable counter always.
        // Deliberately NOT retagging slot 0 with `key` here -- slot 0
        // already belongs to whoever legitimately claimed it, and
        // overwriting its tag would ALSO break find_by_tag() for THAT
        // owner on top of the corruption this fallback already causes.
        // This degraded state isn't something worth engineering to
        // "behave well" -- the fix is raising MaxThreads, not making
        // exhaustion graceful.
        const std::size_t fallback = self.on_slot_exhausted();
        cached_key = key; cached_valid = true; cached_slot = fallback;
        return Handle(&self, fallback);
    }

    /**
     * @brief Configures a single designated key, once, typically from a
     * setup block (before workers start) alongside where Metric/Telemetry
     * themselves are declared. This is what makes local_thread() below
     * mean something -- before set_local() is ever called, is_local()
     * safely returns false for everything (nothing is "the" designated
     * key yet) and local_thread() is not meant to be called at all.
     *
     * Publish-once pattern (relaxed value store, then release flag store)
     * -- NOT because concurrent calls to set_local() itself are expected
     * or supported (they aren't; call this once, from setup, before
     * workers touch is_local()/local_thread()), but because the class
     * cannot assume that discipline holds. A worker thread calling
     * is_local()/local_thread() shortly after setup, on a different
     * thread, still needs to safely observe whatever set_local() wrote,
     * with no assumptions about how your specific render pipeline
     * happens to sequence setup versus worker startup.
     */
    static void set_local(std::uint64_t key) noexcept {
        auto& state = designated_key_state();
        state.value.store(key, std::memory_order_relaxed);
        state.set.store(true, std::memory_order_release);
    }

    /**
     * @brief Is `candidate` (e.g. sd->tid) the designated key set via
     * set_local()? Safe to call at any time, from any thread, whether or
     * not set_local() has been called yet -- returns false, not garbage
     * or a crash, if nothing has been designated. This is meant to be
     * the GATE at a call site:
     *
     *     if (ThTelemetry::is_local(sd->tid)) {
     *         auto th = ThTelemetry::local_thread();
     *         ...
     *     }
     *
     * Both this and local_thread() now read from the SAME stored value
     * set once via set_local() -- there is exactly one place the actual
     * key is ever written, instead of one literal at the gate and a
     * second, independent literal at the local(key) call that has to be
     * kept in sync with it by hand.
     */
    static bool is_local(std::uint64_t candidate) noexcept {
        auto& state = designated_key_state();
        if (!state.set.load(std::memory_order_acquire)) return false;
        return candidate == state.value.load(std::memory_order_relaxed);
    }

    /**
     * @brief local(key) for the designated key set via set_local(), so
     * call sites read as `ThTelemetry::local_thread()` instead of
     * repeating the literal. Asserts in debug builds if called before
     * set_local() -- unlike is_local() (a gate, meant to be checked
     * unconditionally and safely say "no"), calling local_thread() at all
     * implies the caller already believes it's configured, typically
     * because an is_local() check just passed. In release builds (assert
     * compiled out) this falls back to whatever the designated key's
     * default value is (0) rather than crashing -- matching this file's
     * existing philosophy elsewhere: assert loudly during development,
     * never abort a real render over a caller misconfiguration.
     */
    static Handle local_thread() noexcept {
        auto& state = designated_key_state();
        assert(state.set.load(std::memory_order_acquire) &&
               "Telemetry::local_thread() called before set_local() -- "
               "the designated key was never configured. Call "
               "Telemetry::set_local(key) once, from setup, before using "
               "local_thread() or gating on is_local().");
        return local(state.value.load(std::memory_order_relaxed));
    }

    /**
     * @brief Hot-Path: Increments the occurrence count for a metric.
     * Uses std::memory_order_relaxed for maximum hardware throughput.
     */
    inline void count(Metric m) noexcept {
        do_count(slots_[get_thread_slot()], index(m));
    }

    /**
     * @brief Hot-Path: Records a floating-point sample into count, sum, min, and max.
     * Converts values into 64-bit fixed-point integers to allow lock-free atomic additions.
     */
    inline void add(Metric m, float value) noexcept {
        do_add(slots_[get_thread_slot()], index(m), value);
    }

    /**
     * @brief Hot-Path: Tracks extrema (min/max) without incrementing count or sum.
     */
    inline void track_extreme(Metric m, float value) noexcept {
        do_track_extreme(slots_[get_thread_slot()], index(m), value);
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
        const std::size_t t = self.find_slot_for_key(tag);
        if (t == MaxThreads) return false;
        out = read_slot(self.slots_[t]);
        return true;
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
            const Report r = read_slot(self.slots_[t]);
            out.merge(r.slots);
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

    /**
     * @brief How many times MaxThreads has been exhausted -- i.e. how many
     * times a thread or key had nowhere left to go and got silently
     * aliased onto slot 0, sharing (and corrupting) another thread's or
     * key's data. Should be 0 in a correctly-sized program; any non-zero
     * value means MaxThreads needs to be raised and every report produced
     * since the first occurrence should be treated as unreliable. In
     * debug builds this same condition also fires an assert immediately
     * when it happens, rather than waiting to be checked here.
     */
    static std::uint64_t slot_exhaustion_count() noexcept {
        return exhaustion_counter().load(std::memory_order_relaxed);
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
            // Capacity exhausted -- see on_slot_exhausted()'s comment for
            // why this fires an assert in debug builds and increments an
            // observable counter regardless of build type.
            return self.on_slot_exhausted();
        }();
        return slot_idx;
    }

    /**
     * @brief Scans tags_[] for the slot registered under `key`.
     * @return the slot index if found, or MaxThreads (never a valid index)
     * if no slot is currently tagged with this key.
     *
     * Single source of truth for this scan -- previously hand-copied
     * three times (find_by_tag(), and twice inside local(key)'s fast/slow
     * paths). Non-static: reads this instance's own tags_[], called via
     * self.find_slot_for_key(key) from static callers that already hold
     * `self`.
     */
    std::size_t find_slot_for_key(std::uint64_t key) const noexcept {
        for (std::size_t t = 0; t < MaxThreads; ++t) {
            if (tags_[t].set.load(std::memory_order_acquire) &&
                tags_[t].value.load(std::memory_order_relaxed) == key) {
                return t;
            }
        }
        return MaxThreads;
    }

    /**
     * @brief Reads one slot's atomics into a plain Report snapshot.
     * Single source of truth for this read, shared by find_by_tag(),
     * collect_all(), and Handle::snapshot() -- previously duplicated
     * between find_by_tag() and collect_all() with no third copy needed
     * for Handle, since Handle had no read path at all before this.
     */
    static Report read_slot(ThreadStorage& storage) noexcept {
        Report out{};
        for (std::size_t i = 0; i < kMetricCount; ++i) {
            out.slots[i].count = storage.metrics[i].count.load(std::memory_order_relaxed);
            out.slots[i].sum   = storage.metrics[i].sum.load(std::memory_order_relaxed);
            out.slots[i].min   = storage.metrics[i].min.load(std::memory_order_relaxed);
            out.slots[i].max   = storage.metrics[i].max.load(std::memory_order_relaxed);
        }
        return out;
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

    /**
     * @brief Shared atomic-update logic, factored out of count()/add()/
     * track_extreme() so that Handle (the local(key) accessor) performs
     * EXACTLY the same operations on its bound slot, from one source of
     * truth -- not a second, hand-copied implementation that could drift.
     * `storage` is whichever ThreadStorage the caller has already resolved
     * (via get_thread_slot() for the plain path, or a Handle's bound slot
     * for the keyed path).
     */
    static inline void do_count(ThreadStorage& storage, std::size_t idx) noexcept {
        storage.metrics[idx].count.fetch_add(1, std::memory_order_relaxed);
    }

    static inline void do_add(ThreadStorage& storage, std::size_t idx, float value) noexcept {
        const std::int64_t fv = fixed(value);
        storage.metrics[idx].count.fetch_add(1, std::memory_order_relaxed);
        storage.metrics[idx].sum.fetch_add(fv, std::memory_order_relaxed);
        atomic_min(storage.metrics[idx].min, fv);
        atomic_max(storage.metrics[idx].max, fv);
    }

    static inline void do_track_extreme(ThreadStorage& storage, std::size_t idx, float value) noexcept {
        const std::int64_t fv = fixed(value);
        atomic_min(storage.metrics[idx].min, fv);
        atomic_max(storage.metrics[idx].max, fv);
    }

    /// Global atomic cadence counter for periodic reporting triggers.
    static std::atomic<std::uint64_t>& report_cadence() noexcept {
        static std::atomic<std::uint64_t> cadence{0};
        return cadence;
    }

    /// Serializes ONLY the slow (first-time-a-key-is-seen) path of
    /// local(key) against itself -- not against the plain thread-implicit
    /// claiming path, which stays fully lock-free via CAS as before.
    static std::mutex& key_claim_mutex() noexcept {
        static std::mutex m;
        return m;
    }

    /// Backing storage for set_local()/is_local()/local_thread(). Same
    /// atomic-backed publish-once shape as TagState (used by set_tag()/
    /// tag()) -- a plain std::uint64_t here would be a genuine data race
    /// between set_local() (written once, from setup) and is_local()/
    /// local_thread() (read from arbitrary worker threads afterward).
    struct DesignatedKeyState {
        std::atomic<bool> set{false};
        std::atomic<std::uint64_t> value{0};
    };

    static DesignatedKeyState& designated_key_state() noexcept {
        static DesignatedKeyState state;
        return state;
    }

    /// Backing counter for slot_exhaustion_count() -- see that public
    /// method's doc comment for what this means and why it exists.
    static std::atomic<std::uint64_t>& exhaustion_counter() noexcept {
        static std::atomic<std::uint64_t> counter{0};
        return counter;
    }

    /// Single point of failure for BOTH get_thread_slot() and local(key)
    /// when MaxThreads has been exhausted. Fires an assert in debug
    /// builds (compiled out under NDEBUG, matching standard assert()
    /// semantics) -- exhaustion is virtually always a configuration bug
    /// (MaxThreads too small for the program's real usage), and finding
    /// out immediately during development beats discovering it later as
    /// mysteriously-wrong numbers. Always increments the observable
    /// counter regardless of build type, so release builds -- where the
    /// assert compiles out and the code must keep running rather than
    /// abort a real render -- still have a way to detect it happened.
    static std::size_t on_slot_exhausted() noexcept {
        assert(false && "Telemetry: MaxThreads exhausted -- two or more "
                         "threads/keys are now silently aliased onto the "
                         "same slot, corrupting each other's data. "
                         "Increase the MaxThreads template parameter.");
        exhaustion_counter().fetch_add(1, std::memory_order_relaxed);
        return static_cast<std::size_t>(0);
    }

    // Storage layout: Slot ownership flags, tags, and cache-line aligned metric arrays.
    std::atomic<bool> slot_used_[MaxThreads]{};
    TagState tags_[MaxThreads];
    ThreadStorage slots_[MaxThreads];
};

} // namespace TELEMETRY
} // namespace ROMBO