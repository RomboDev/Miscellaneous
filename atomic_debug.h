
// -----------------------------------------------------------------------------------------------
// Debugging hot loops : instrumenting high-frequency loops and multi-threaded simulations 
// where basic prints would flood the console, skew timing, and interleave garbage across threads. 
//
// Aggregate counters answer one question: "Is this sane across millions of samples?"
// Lock-free atomics, fixed-point float sums, and a CAS min/max tracker -- embedded directly
// in the hot path, with the actual print throttled down to a readable trickle.
// -----------------------------------------------------------------------------------------------
#include <atomic>
#include <cstdio>
#include <limits>

// -----------------------------------------------------------------------------------------------
// Global low-overhead telemetry storage (Lock-free & zero-mutex)
// -----------------------------------------------------------------------------------------------

// 1) Lock-free event counter.
// Use `std::memory_order_relaxed` because we only care about atomic increments, 
// not synchronization or ordering relative to other memory writes.
static std::atomic<uint64_t> g_count{0};

// 2) Fixed-point scalar accumulator for float metrics.
// Standard C++ pre-C++20 lacks atomic operations on floating-point types.
// We scale floats to `int64_t` fixed-point integers to leverage fast atomic additions.
// NOTE: Watch for overflow! `int64_t` max is ~9.22e18. At 1e6 scale, total sum caps at ~9.22e12.
static std::atomic<int64_t> g_sum{0};

// 3) Extremes tracking (Atomic Min/Max).
// No native atomic min/max instruction exists on standard x86/ARM hardware.
// A CAS (Compare-And-Swap) loop is the standard lock-free substitute:
// - Read current minimum (`prev`).
// - Check if `fixed_val < prev`. If not, we're done.
// - Attempt CAS: "If g_min is still `prev`, replace it with `fixed_val`."
// - If another thread updated g_min mid-flight, `compare_exchange_weak` fails, 
//   refreshes `prev` with the new actual value, and retries until success or target lost.
static std::atomic<int64_t> g_min{std::numeric_limits<int64_t>::max()};

constexpr float SCALE = 1e6f;

void trace_value(float value) {
    // -------------------------------------------------------------------------------------------
    // Instrumentation Phase (Executes in hot loop, nanosecond overhead)
    // -------------------------------------------------------------------------------------------

    // Count sample
    g_count.fetch_add(1, std::memory_order_relaxed);

    // Accumulate sum in fixed-point representation
    int64_t fixed_val = static_cast<int64_t>(value * SCALE);
    g_sum.fetch_add(fixed_val, std::memory_order_relaxed);

    // Update global minimum using a non-blocking CAS loop
    int64_t prev = g_min.load(std::memory_order_relaxed);
    while (fixed_val < prev && !g_min.compare_exchange_weak(prev, fixed_val, std::memory_order_relaxed)) {
        // Loop body intentionally left empty: 'prev' is automatically reloaded on CAS failure
    }

    // -------------------------------------------------------------------------------------------
    // Throttled Downsampled Logging (Fires rarely, minimal console overhead)
    // -------------------------------------------------------------------------------------------

    // Gate the actual print behind a modulo -- millions of events become a readable trickle.
    uint64_t count = g_count.load(std::memory_order_relaxed);
    
    // Guard against potential zero-division if count resets or initializes asynchronously
    // Tip: We could move the count % 512 logic inside a std::atomic<uint64_t> last_printed_count 
    // check using compare_exchange so that only one thread performs the expensive printf and division logic
    if (count > 0 && count % 512 == 0) {
        // NOTE ON CONSISTENCY: Reading multiple relaxed atomics here means data is "eventually consistent."
        // Under heavy contention, g_sum or g_min might advance slightly ahead of 'count'. 
        // For real-time telemetry and trend spotting, minor snapshot skew is completely fine.
        float avg = (static_cast<float>(g_sum.load(std::memory_order_relaxed)) / SCALE) / static_cast<float>(count);
        float min = static_cast<float>(g_min.load(std::memory_order_relaxed)) / SCALE;

        std::fprintf(stderr, "[walk] count=%llu avg=%.4f min=%.4f\n", 
                     static_cast<unsigned long long>(count), avg, min);
    }
}

