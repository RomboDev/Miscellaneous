#include "telemetry_debug.h"
#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstdlib> // for std::atoi

// Compile it with : g++ -std=c++14 -O3 -pthread telemetry_tester.cpp -o telemetry_tester

using namespace ROMBO::TELEMETRY;

enum class PiMetric : std::uint32_t {
    Samples,
    InsideCircle,
    DistanceSq,
    Count_
};

using PiTelemetry = Telemetry<PiMetric>;

void monte_carlo_worker(int id, int iterations) 
{
    auto& tel = PiTelemetry::local();
    tel.set_tag(static_cast<std::uint64_t>(id));

    std::mt19937 gen(1337 + id);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < iterations; ++i) 
    {
        float x = dist(gen);
        float y = dist(gen);
        float d2 = x * x + y * y;

        tel.count(PiMetric::Samples);
        tel.add(PiMetric::DistanceSq, d2);
        if (d2 <= 1.0f) tel.count(PiMetric::InsideCircle);

        // Throttle to keep rotation frequent but not per-sample
        tel.rotate_every(1024);
    }
    tel.flush();
}

// Runner ......................
int main(int argc, char* argv[]) 
{
    // Defaults
    int num_workers = 8;
    int samples_per_worker = 100000;
    int throttling_every = 1024;

    // Parse optional arguments
    if (argc > 1) num_workers = std::atoi(argv[1]);
    if (argc > 2) samples_per_worker = std::atoi(argv[2]);
    if (argc > 3) throttling_every = std::atoi(argv[3]);
    
    throttling_every = throttling_every < samples_per_worker ? samples_per_worker / 2 : throttling_every;

    std::cout << "Launching " << num_workers << " workers..." << std::endl;
    std::cout << "Samples per worker: " << samples_per_worker << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (int i = 0; i < num_workers; ++i) {
        workers.emplace_back(monte_carlo_worker, i, samples_per_worker);
    }

    for (auto& t : workers) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    
    // Measure in microseconds to prevent integer division loss on fast sub-ms runs
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double elapsed_ms = static_cast<double>(duration_us) / 1000.0;

    // 1. Thread-specific reporting
    std::cout << "\n--- Per-Thread Breakdown ---\n";
    for (int i = 0; i < num_workers; ++i) {
        PiTelemetry::Report t_report;
        if (PiTelemetry::find_by_tag(i, t_report)) 
            std::cout << "Thread " << i << " -> " << t_report.count(PiMetric::Samples) << " samples\n";
    }

    // 2. Aggregate reporting
    PiTelemetry::Report agg = PiTelemetry::collect_all();
    
    std::uint64_t total_samples = agg.count(PiMetric::Samples);
    
    // Total operations / duration in microseconds directly yields Operations per Microsecond (MHz)
    double throughput_mhz = (duration_us > 0) 
        ? static_cast<double>(total_samples) / static_cast<double>(duration_us) 
        : 0.0;

    std::cout << "\n--------------------------------\n";
    std::cout << "Telemetry Report\n";
    std::cout << "--------------------------------\n";
    std::cout << "SamplingCalls     " << total_samples << "\n";
    std::cout << "SamplingResult    average distanceSq = " << std::fixed << std::setprecision(6) << agg.average(PiMetric::DistanceSq) << "\n";
    std::cout << "                  min = " << agg.minimum(PiMetric::DistanceSq) << "\n";
    std::cout << "                  max = " << agg.maximum(PiMetric::DistanceSq) << "\n";
    
    double pi_est = 4.0 * (double)agg.count(PiMetric::InsideCircle) / (double)total_samples;
    std::cout << "\nPi Estimate:      " << pi_est << "\n";
    std::cout << "Elapsed:          " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
    std::cout << "Throughput:       " << std::fixed << std::setprecision(2) << throughput_mhz << " MHz\n";

    return 0;
}