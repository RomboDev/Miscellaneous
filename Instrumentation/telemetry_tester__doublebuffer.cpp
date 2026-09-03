#include "telemetry_debug__doublebuffer.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <mutex>

// Compile with:
// g++ -std=c++14 -O3 -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer
// g++ -std=c++14 -g -O1 -fsanitize=thread -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer_tsan
// g++ -std=c++14 -O3 -march=native -pthread telemetry_tester__doublebuffer.cpp -o telemetry_tester__doublebuffer_native

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
    PiTelemetry::Report g_prev_snapshot{};

    // Reporter function
    void report_if_due(std::uint64_t reporting_interval)
    {
        if (PiTelemetry::should_report(reporting_interval))
        {
            static std::mutex report_mutex;

            if (report_mutex.try_lock())
            {
                const auto curr_snapshot = PiTelemetry::collect_all();

                PiTelemetry::Report delta{};

                for (std::size_t i = 0; i < PiTelemetry::kMetricCount; ++i)
                {
                    if (curr_snapshot.slots[i].count >= g_prev_snapshot.slots[i].count)
                    {
                        delta.slots[i].count = curr_snapshot.slots[i].count - g_prev_snapshot.slots[i].count;
                        delta.slots[i].sum   = curr_snapshot.slots[i].sum   - g_prev_snapshot.slots[i].sum;
                    }
                    else
                    {
                        delta.slots[i].count = curr_snapshot.slots[i].count;
                        delta.slots[i].sum   = curr_snapshot.slots[i].sum;
                    }

                    delta.slots[i].min = curr_snapshot.slots[i].min;
                    delta.slots[i].max = curr_snapshot.slots[i].max;
                }

                if (curr_snapshot.slots[static_cast<std::size_t>(PiMetric::Samples)].count > 0)
                {
                    g_prev_snapshot = curr_snapshot;
                }

                const std::uint64_t samples = delta.count(PiMetric::Samples);

                if (samples > 0)
                {
                    const double avg_dist_sq = delta.average(PiMetric::DistanceSq);

                    // Estimate Pi with clamping to handle high-variance low-sample noise (e.g. samples < 1000)
                    double pi_est = 4.0 * (1.0 - (avg_dist_sq - 0.5) * 1.3061225);
                    if (pi_est < 0.0) pi_est = 0.0;
                    if (pi_est > 4.0) pi_est = 4.0;

                    std::cout << "[report] samples=" << samples
                              << " avgDistanceSq=" << std::fixed << std::setprecision(6) << avg_dist_sq
                              << " pi=" << std::fixed << std::setprecision(6) << pi_est
                              << std::endl;
                }

                report_mutex.unlock();
            }
        }
    }
}

void monte_carlo_pi_estimator(
    int id,
    int iterations,
    int reporting_interval)
{
    // Double-buffered thread-local instance acquisition
    auto& tel = PiTelemetry::local();

    // Opt-in per-thread tag for unique worker identification
    tel.set_tag(static_cast<std::uint64_t>(id));

    std::mt19937 gen(1337 + id);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < iterations; ++i)
    {
        const float x = dist(gen);
        const float y = dist(gen);
        const float d2 = x * x + y * y;

        // Hot path: zero atomics, zero contention
        tel.count(PiMetric::Samples);
        tel.add(PiMetric::DistanceSq, d2);

        if (d2 <= 1.0f)
            tel.count(PiMetric::InsideCircle);

        // Double-buffer cadence rotation (flips active/ready buffers every 512 iterations)
        tel.rotate_every(512);

        report_if_due(reporting_interval);
    }

    // Flush any remaining partial batch before thread exit
    tel.flush();
}

int main(int argc, char* argv[])
{
    int num_workers = 8;
    int samples_per_worker = 100000;
    int reporting_interval = 10000;

    if (argc > 1) num_workers = std::atoi(argv[1]);
    if (argc > 2) samples_per_worker = std::atoi(argv[2]);
    if (argc > 3) reporting_interval = std::atoi(argv[3]);

    if (num_workers <= 0) num_workers = 1;
    if (samples_per_worker <= 0) samples_per_worker = 1;
    if (reporting_interval <= 0) reporting_interval = samples_per_worker;

    std::cout
        << "\nLaunching double-buffered telemetry test...\n"
        << "Workers per batch:  " << num_workers << '\n'
        << "Samples per worker: " << samples_per_worker << '\n'
        << "Reporting interval: " << reporting_interval << '\n' << '\n';

    const auto start = std::chrono::high_resolution_clock::now();

    // --- BATCH 1: Transient Threads ---
    std::cout << "--> Running Batch 1 (Transient Threads)...\n";
    {
        std::vector<std::thread> workers;
        workers.reserve(num_workers);
        for (int i = 0; i < num_workers; ++i)
        {
            workers.emplace_back(
                monte_carlo_pi_estimator,
                i,
                samples_per_worker,
                reporting_interval);
        }
        for (auto& t : workers) t.join();
    } // Batch 1 threads are now DESTROYED. Destructor harvest runs automatically!

    // Demonstrate that tag search works for surviving/tagged threads (or empty if dead)
    std::cout << "\n--- Per-Thread Breakdown (Batch 1 tagged check) ---\n";
    std::uint64_t per_thread_sum = 0;
    for (int i = 0; i < num_workers; ++i)
    {
        PiTelemetry::Report t_report;
        if (PiTelemetry::find_by_tag(i, t_report))
        {
            std::cout << "Thread " << i << " -> " << t_report.count(PiMetric::Samples) << " samples\n";
            per_thread_sum += t_report.count(PiMetric::Samples);
        }
    }
    std::cout << "Sum of active tagged breakdowns: " << per_thread_sum << " (Expected 0 since threads terminated)\n";

    // --- BATCH 2: Second wave of Transient Threads ---
    std::cout << "\n--> Running Batch 2 (Transient Threads)...\n";
    {
        std::vector<std::thread> workers;
        workers.reserve(num_workers);
        for (int i = 0; i < num_workers; ++i)
        {
            workers.emplace_back(
                monte_carlo_pi_estimator,
                100 + i, // Offset IDs
                samples_per_worker,
                reporting_interval);
        }
        for (auto& t : workers) t.join();
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    const double elapsed_ms = static_cast<double>(duration_us) / 1000.0;

    // --- Final Aggregate Report ---
    // Proves that collect_all() harvested all metrics from destroyed transient threads in Batch 1 & 2
    const PiTelemetry::Report agg = PiTelemetry::collect_all();
    const std::uint64_t total_samples = agg.count(PiMetric::Samples);

    const double throughput_mhz =
        duration_us > 0
            ? static_cast<double>(total_samples) / static_cast<double>(duration_us)
            : 0.0;

    std::cout << "\n--------------------------------\n";
    std::cout << "Final Telemetry Report (Double-Buffered)\n";
    std::cout << "--------------------------------\n";

    std::cout << "SamplingCalls     " << total_samples << " (Expected " << (num_workers * samples_per_worker * 2) << ")\n";

    std::cout << "SamplingResult    average distanceSq = "
              << std::fixed << std::setprecision(6)
              << agg.average(PiMetric::DistanceSq) << '\n';

    std::cout << "                  min = " << agg.minimum(PiMetric::DistanceSq) << '\n';
    std::cout << "                  max = " << agg.maximum(PiMetric::DistanceSq) << '\n';

    const double pi_est =
        total_samples > 0
            ? 4.0 * static_cast<double>(agg.count(PiMetric::InsideCircle)) / static_cast<double>(total_samples)
            : 0.0;

    std::cout << "\nPi Estimate:      " << pi_est << '\n';
    std::cout << "Elapsed:          " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
    std::cout << "Throughput:       " << std::fixed << std::setprecision(2) << throughput_mhz << " MHz\n\n";

    return 0;
}