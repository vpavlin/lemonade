#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>
#include <sstream>
#include <nlohmann/json.hpp>

// Forward declarations for Prometheus types
namespace prometheus {
    class Registry;
    template <typename T>
    class Family;
    class Counter;
    class Gauge;
    class Histogram;
    class Summary;
}

namespace lemon {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// MetricsCollector — singleton that owns all Prometheus metrics
//
// Design decisions:
//   • Metrics are registered once at server startup (init()).
//   • All update() calls are lock-free for hot paths; registration uses a
//     single mutex.
// ---------------------------------------------------------------------------

class MetricsCollector {
public:
    // --- Singleton access ---------------------------------------------------
    static MetricsCollector& instance();

    // --- Lifecycle ---------------------------------------------------------
    /// Initialise the collector. Must be called before any metric updates.
    void init();

    /// Shut down (called from server destructor).
    void shutdown();

    // --- Convenience methods for common Lemonade metrics --------------------

    /// Increment request counter (called from every HTTP handler)
    void record_request(const std::string& method,
                        const std::string& endpoint,
                        int status_code);

    /// Record request duration in seconds
    void record_request_duration(const std::string& endpoint,
                                 double duration_seconds,
                                 const std::string& model = "");

    /// Record inference telemetry (called after each backend call)
    void record_inference_telemetry(const std::string& model,
                                    int input_tokens,
                                    int output_tokens,
                                    double ttft_seconds,
                                    double tokens_per_second);

    /// Record model load/unload event
    void record_model_event(const std::string& model,
                            const std::string& backend,
                            bool loaded);

    /// Record system resource snapshot (GPU, CPU, memory)
    void record_system_resources(double cpu_percent,
                                 double gpu_utilization,
                                 uint64_t gpu_memory_used_bytes,
                                 uint64_t system_memory_used_bytes);

    // --- Access to Prometheus registry -------------------------------------
    std::shared_ptr<prometheus::Registry> get_registry();

    /// Export all metrics as Prometheus text format
    std::string export_metrics();

private:
    MetricsCollector();
    ~MetricsCollector();
    MetricsCollector(const MetricsCollector&) = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    std::shared_ptr<prometheus::Registry> registry_;
    std::mutex registry_mutex_;

    // --- Metric families (raw pointers, owned by registry) -----------------
    // Register() returns Family<T>* which we store as raw pointers
    prometheus::Family<prometheus::Counter>* req_total_ = nullptr;
    prometheus::Family<prometheus::Histogram>* req_duration_ = nullptr;

    // Inference metrics
    prometheus::Family<prometheus::Counter>* tokens_input_total_ = nullptr;
    prometheus::Family<prometheus::Counter>* tokens_output_total_ = nullptr;
    prometheus::Family<prometheus::Summary>* ttft_summary_ = nullptr;
    prometheus::Family<prometheus::Histogram>* tps_histogram_ = nullptr;

    // Model lifecycle
    prometheus::Family<prometheus::Gauge>* models_loaded_ = nullptr;
    prometheus::Family<prometheus::Counter>* model_load_total_ = nullptr;
    prometheus::Family<prometheus::Counter>* model_unload_total_ = nullptr;

    // System resources
    prometheus::Family<prometheus::Gauge>* cpu_usage_ = nullptr;
    prometheus::Family<prometheus::Gauge>* gpu_utilization_ = nullptr;
    prometheus::Family<prometheus::Gauge>* gpu_memory_used_ = nullptr;
    prometheus::Family<prometheus::Gauge>* system_memory_used_ = nullptr;

    // Error metrics
    prometheus::Family<prometheus::Counter>* errors_total_ = nullptr;
};

} // namespace lemon
