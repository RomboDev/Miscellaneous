#include "telemetry_debug.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <mutex>

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

    // Working
    for (auto& t : workers)
        t.join();

    // End timer
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    const double elapsed_ms = static_cast<double>(duration_us) / 1000.0;
    

    // 1. Thread-specific reporting
    /*std::cout << "\n--- Per-Thread Breakdown ---\n";
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
        }
    }*/

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
