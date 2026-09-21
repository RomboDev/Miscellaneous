#include "telemetry_debug__doublebuffer.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <mutex>

// Compile it with one of the following : 
// g++ -std=c++14 -O3 -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer (std - max perf)

// g++ -std=c++14 -g -O1 -fsanitize=thread -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer_tsan (check races etc.)

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
// g++ -std=c++14 -O3 -march=native -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer_native

// Run it with : 
// ./telemetry_tester__doublebuffer
// ./telemetry_tester__doublebuffer 16 100000 1000

using namespace ROMBO::TELEMETRY;

enum class PiMetric : std::uint32_t {
    Samples,
    InsideCircle,
    DistanceSq,
    Count_
};

using PiTelemetry = Telemetry<PiMetric>;

namespace
{
    std::mutex g_report_mutex;
    PiTelemetry::Report g_running_report;   // windowed: reset every print
    PiTelemetry::Report g_cumulative_total; // NEVER reset -- the true
                                             // running total, for the
                                             // final aggregate report

    // EXACT pattern from the atomic-array design's report function:
    // drain on EVERY call (not just at should_report() time), merge into
    // BOTH accumulators. Only g_running_report resets at print time --
    // g_cumulative_total is what makes the FINAL aggregate correct, since
    // every windowed report's data would otherwise be discarded once
    // printed, with nothing left holding the true running total.
    void report_if_due(int reporting_interval)
    {
        // Gated (not every-sample) again -- safe now, since
        // collect_active() never touches global_retired_data(), so
        // calling it less often no longer risks re-reading (and
        // re-counting) any retired thread's data.
        if (!PiTelemetry::should_report(
                static_cast<std::uint64_t>(reporting_interval)))
        {
            return;
        }

        {
            const PiTelemetry::Report snap = PiTelemetry::collect_active();
            std::lock_guard<std::mutex> lock(g_report_mutex);
            g_running_report.merge(snap.slots);
            g_cumulative_total.merge(snap.slots);
        }

        PiTelemetry::Report agg;
        {
            std::lock_guard<std::mutex> lock(g_report_mutex);
            agg = g_running_report;
            g_running_report = PiTelemetry::Report{};
        }

        const std::uint64_t samples = agg.count(PiMetric::Samples);
        if (samples == 0)
            return;

        const std::uint64_t inside = agg.count(PiMetric::InsideCircle);

        const double pi_est =
            4.0 * static_cast<double>(inside) /
            static_cast<double>(samples);

        std::cout
            << "[report] samples=" << samples
            << " avgDistanceSq="
            << std::fixed << std::setprecision(6)
            << agg.average(PiMetric::DistanceSq)
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

        // REQUIRED for this (double-buffer) architecture, unlike the
        // atomic-array design: without periodically rotating this
        // thread's buffer, NOTHING it writes becomes visible to
        // collect_all() until the thread eventually exits (its
        // destructor force-harvests on exit as a last resort, not as
        // the intended incremental-visibility mechanism). Interval
        // chosen well below reporting_interval so data is reliably
        // ready by the time should_report() fires.
        tel.rotate_every(std::max(1, reporting_interval / 10));

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
    

    // Thread-specific reporting -- each worker has its OWN unique tag
    // (== id), via set_tag()/find_by_tag(). This shows what a SINGLE
    // thread is individually collecting.
    // Works for atomic-based telemetry NOT here..
    // tagged accumulation would need to happen at every incremental drain, 
    // not just at retirement — a real per-tag running total, updated continuously, 
    // not a one-time snapshot at exit
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

    // Final drain: collect_active() catches anything not yet rotated out
    // by an incremental report; retired_total() is read here EXACTLY
    // ONCE (the historical total from every thread that has already
    // exited) and merged exactly once -- never repeatedly, which is
    // what caused the over-counting bug this split fixes.
    {
        const PiTelemetry::Report final_active = PiTelemetry::collect_active();
        const PiTelemetry::Report retired = PiTelemetry::retired_total();
        std::lock_guard<std::mutex> lock(g_report_mutex);
        g_cumulative_total.merge(final_active.slots);
        g_cumulative_total.merge(retired.slots);
    }
    const PiTelemetry::Report agg = g_cumulative_total;

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