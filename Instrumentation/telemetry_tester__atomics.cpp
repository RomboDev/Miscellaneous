#include "telemetry_debug__atomics.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <mutex>

// Compile it with : 
// g++ -std=c++14 -O3 -pthread telemetry_tester__atomics.cpp -o telemetry_tester__atomics                             (std - max perf)
// g++ -std=c++14 -g -O1 -fsanitize=thread -pthread telemetry_tester__atomics.cpp -o telemetry_tester__atomics_tsan   (check races etc.)

// -march=native lets the compiler target every instruction extension
// (AVX2, AVX-512, BMI2, etc.) the CURRENT BUILD MACHINE'S CPU supports,
// plus its scheduling model. Benefit depends entirely on whether the hot
// path has vectorizable, non-atomic arithmetic for the compiler to
// exploit -- atomic RMW operations (fetch_add, compare_exchange) do not
// benefit, since each is a serialization point, not something AVX
// operates on. Cache-line sizes used via alignas(...) are fixed
// compile-time constants; -march=native does not detect or adapt to the
// actual L1 line size of the build machine.
// Portability cost: the resulting binary is tied to the exact CPU
// features present at build time and may fail with "illegal
// instruction" if run on different, less capable hardware.
// g++ -std=c++14 -O3 -march=native -pthread telemetry_tester__atomics.cpp -o telemetry_tester__atomics_native

// Run it with : 
// ./telemetry_tester__atomics
// ./telemetry_tester__atomics 16 100000 1000

using namespace ROMBO::TELEMETRY;

enum class PiMetric : std::uint32_t {
    Samples,
    InsideCircle,
    DistanceSq,
    Count_
};

using PiTelemetry = Telemetry<PiMetric>;

// -----------------------------------------------------------------------------
// DEMO: Telemetry::local(key) -- explicit-key access, as opposed to the
// tel.set_tag(id) + find_by_tag(id) pattern used above for PiTelemetry.
//
// The distinguishing feature: a key is NOT the same thing as a thread.
// Several different OS threads calling local(SAME key) all land on the
// exact same slot and share one accumulator -- no tagging step needed,
// because the key IS the identity, not a label attached after the fact.
// Below, EVERY worker shares ONE fixed key (1) -- not id % 2, not one
// key per thread. All N worker threads race to claim the SAME slot at
// startup, and every one of them writes into it for the whole run. This
// is the starkest version of "a key is not a thread": N different OS
// threads, one shared accumulator, no coordination needed beyond what
// local(key) already does internally.
// -----------------------------------------------------------------------------
enum class GroupMetric : std::uint32_t {
    GroupSamples,
    GroupInsideCircle,
    GroupDistanceSq,
    Count_
};

using GroupTelemetry = Telemetry<GroupMetric>;

namespace
{
    std::mutex g_report_mutex;
    PiTelemetry::Report g_prev_snapshot{};

    // Reporter fnc
    void report_if_due(int reporting_interval)
    {
        if (!PiTelemetry::should_report(
                static_cast<std::uint64_t>(reporting_interval)))
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_report_mutex);

        // IMPORTANT:
        // Snapshot acquisition is serialized with the previous-snapshot
        // Prevent snapshots to never be applied out of order.
        const PiTelemetry::Report curr_snapshot =
            PiTelemetry::collect_all();

        PiTelemetry::Report delta{};

        for (std::size_t i = 0;
             i < PiTelemetry::kMetricCount;
             ++i)
        {
            delta.slots[i].count =
                curr_snapshot.slots[i].count -
                g_prev_snapshot.slots[i].count;

            delta.slots[i].sum =
                curr_snapshot.slots[i].sum -
                g_prev_snapshot.slots[i].sum;

            delta.slots[i].min =
                curr_snapshot.slots[i].min;

            delta.slots[i].max =
                curr_snapshot.slots[i].max;
        }

        g_prev_snapshot = curr_snapshot;

        const std::uint64_t samples =
            delta.count(PiMetric::Samples);

        if (samples == 0)
            return;

        const std::uint64_t inside =
            delta.count(PiMetric::InsideCircle);

        const double pi_est =
            4.0 * static_cast<double>(inside) /
            static_cast<double>(samples);

        std::cout
            << "[report] samples=" << samples
            << " avgDistanceSq="
            << std::fixed << std::setprecision(6)
            << delta.average(PiMetric::DistanceSq)
            << " pi=" << pi_est
            << '\n';
    }
}

void monte_carlo_pi_estimator(
    int id,
    int iterations,
    int reporting_interval)
{
    auto& tel = PiTelemetry::local();

    tel.set_tag(static_cast<std::uint64_t>(id));

    // DEMO: local(key) with a FIXED key -- every worker, regardless of
    // its own id, resolves to the SAME slot here. Resolved once, reused
    // every iteration below exactly like `tel` above.
    auto group_tel = GroupTelemetry::local(1);

    std::mt19937 gen(1337 + id);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < iterations; ++i)
    {
        const float x = dist(gen);
        const float y = dist(gen);
        const float d2 = x * x + y * y;

        tel.count(PiMetric::Samples);
        tel.add(PiMetric::DistanceSq, d2);

        if (d2 <= 1.0f)
            tel.count(PiMetric::InsideCircle);

        // DEMO: writing through the keyed handle -- identical call shape
        // to tel's calls above, just bound to the shared group slot
        // instead of this thread's own. Feeding the SAME x/y/d2 this
        // thread already computed means the group's Pi estimate is a
        // genuine, independent cross-check against PiTelemetry's --
        // same underlying draws, two completely separate accumulators.
        group_tel.count(GroupMetric::GroupSamples);
        group_tel.add(GroupMetric::GroupDistanceSq, d2);
        if (d2 <= 1.0f)
            group_tel.count(GroupMetric::GroupInsideCircle);

        report_if_due(reporting_interval);
    }
}

int main(int argc, char* argv[])
{
    int num_workers = 8;
    int samples_per_worker = 100000;
    int reporting_interval = 10000;

    if (argc > 1)
        num_workers = std::atoi(argv[1]);

    if (argc > 2)
        samples_per_worker = std::atoi(argv[2]);

    if (argc > 3)
        reporting_interval = std::atoi(argv[3]);

    // Keep command-line arguments sane.
    if (num_workers <= 0)
        num_workers = 1;

    if (samples_per_worker <= 0)
        samples_per_worker = 1;

    if (reporting_interval <= 0)
        reporting_interval = samples_per_worker;

    std::cout
        << "\nLaunching " << num_workers << " workers...\n"
        << "Samples per worker: " << samples_per_worker << '\n'
        << "Reporting interval: " << reporting_interval << '\n' << '\n';


    // Start work timer
    const auto start = std::chrono::high_resolution_clock::now();

    // Thread setup
    std::vector<std::thread> workers;
    workers.reserve(num_workers);

    // Work to do
    for (int i = 0; i < num_workers; ++i)
    {
        workers.emplace_back(
            monte_carlo_pi_estimator,
            i,
            samples_per_worker,
            reporting_interval);
    }

    // Working ...
    for (auto& t : workers)
        t.join();

    // End timer
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    const double elapsed_ms = static_cast<double>(duration_us) / 1000.0;
    

    // 1. Thread-specific reporting -- each worker has its OWN unique tag
    // (== id), unlike GroupTelemetry's demo below where every worker
    // deliberately shares ONE key. This shows what a SINGLE thread is
    // individually collecting, via the exact same find_by_tag() read
    // path -- just keyed by a value nobody else shares.
    std::cout << "\n--- Per-Thread Breakdown (PiTelemetry, unique tag per worker) ---\n";
    std::uint64_t per_thread_sum = 0;
    for (int i = 0; i < num_workers; ++i)
    {
        PiTelemetry::Report t_report;

        if (PiTelemetry::find_by_tag(i, t_report))
        {
            std::cout
                << "Thread " << i
                << " -> "
                << t_report.count(PiMetric::Samples)
                << " samples\n";
            per_thread_sum += t_report.count(PiMetric::Samples);
        }
    }
    std::cout
        << "Sum of per-thread breakdowns: " << per_thread_sum
        << " \n(should match SamplingCalls below exactly)\n";

    // DEMO: reading back GroupTelemetry's data for the single shared key
    // (1) that every worker wrote into, from main()'s own thread -- which
    // never wrote a single sample itself. A key is looked up the same
    // way from ANY thread, unlike set_tag()'s pattern, where the tag only
    // ever labels whichever thread called it.
    //
    // Two ways to read it, shown side by side:
    //   - find_by_tag(): re-scans tags_[] from scratch every call.
    //   - local(1).snapshot(): local(1) does that SAME scan internally
    //     the first time any given thread calls it with a given key --
    //     main() has never called local(1) before, so this first call is
    //     no cheaper than find_by_tag() here. The real saving is on
    //     REPEATED calls with the same key (exactly what every worker
    //     above already does, once per sample): the second and later
    //     calls hit local()'s own thread-local cache and skip the scan
    //     entirely -- something find_by_tag() can never do, since it has
    //     no memory between calls. snapshot() itself then reads straight
    //     off the Handle's already-known slot, no scan at all.
    std::cout << "\n--- Local Thread: all workers share ONE key (1) ---\n";

    GroupTelemetry::Report via_tag;
    if (GroupTelemetry::find_by_tag(1, via_tag))
    {
        std::cout
            << "via find_by_tag(1)       -> "
            << via_tag.count(GroupMetric::GroupSamples)
            << " samples (every worker combined)\n";
    }

    auto group_tel = GroupTelemetry::local(1);
    const GroupTelemetry::Report via_snapshot = group_tel.snapshot();
    std::cout
        << "via local(1).snapshot()  -> "
        << via_snapshot.count(GroupMetric::GroupSamples)
        << " samples (should match exactly)\n";

    // Real cross-check, not just a sample count: the SAME x/y/d2 draws
    // feed both PiTelemetry (unique tag per worker) and GroupTelemetry
    // (one shared key for everyone) -- so the group's OWN Pi estimate,
    // computed entirely independently through a different accumulator,
    // should land on essentially the same value as PiTelemetry's below.
    const std::uint64_t group_samples = via_snapshot.count(GroupMetric::GroupSamples);
    const std::uint64_t group_inside  = via_snapshot.count(GroupMetric::GroupInsideCircle);
    const double group_pi_est = group_samples > 0
        ? 4.0 * static_cast<double>(group_inside) / static_cast<double>(group_samples)
        : 0.0;
    std::cout
        << "GroupTelemetry: \navgDistanceSq = "
        << std::fixed << std::setprecision(6)
        << via_snapshot.average(GroupMetric::GroupDistanceSq)
        << "  \nPi estimate = " << group_pi_est
        << "  \n(should match PiTelemetry's below)\n";

    // 2. Final cumulative aggregate
    const PiTelemetry::Report agg =
        PiTelemetry::collect_all();

    const std::uint64_t total_samples =
        agg.count(PiMetric::Samples);

    const double throughput_mhz =
        duration_us > 0
            ? static_cast<double>(total_samples) /
              static_cast<double>(duration_us)
            : 0.0;

    std::cout << "\n--------------------------------\n";
    std::cout << "Final Telemetry Report\n";
    std::cout << "--------------------------------\n";

    std::cout
        << "SamplingCalls     "
        << total_samples << '\n';

    std::cout
        << "SamplingResult    average distanceSq = "
        << std::fixed << std::setprecision(6)
        << agg.average(PiMetric::DistanceSq) << '\n';

    std::cout
        << "                  min = "
        << agg.minimum(PiMetric::DistanceSq) << '\n';

    std::cout
        << "                  max = "
        << agg.maximum(PiMetric::DistanceSq) << '\n';

    const double pi_est =
        total_samples > 0
            ? 4.0 *
              static_cast<double>(
                  agg.count(PiMetric::InsideCircle)) /
              static_cast<double>(total_samples)
            : 0.0;

    std::cout
        << "\nPi Estimate:      "
        << pi_est << '\n';

    std::cout
        << "Elapsed:          "
        << std::fixed << std::setprecision(2)
        << elapsed_ms << " ms\n";

    std::cout
        << "Throughput:       "
        << std::fixed << std::setprecision(2)
        << throughput_mhz << " MHz\n\n";

    return 0;
}