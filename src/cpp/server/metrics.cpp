#include "lemon/metrics.h"
#include <lemon/utils/aixlog.hpp>
#include <lemon/version.h>

// Prometheus-cpp includes (v1.2.0 API)
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/summary.h>
#include <prometheus/registry.h>
#include <prometheus/family.h>
#include <prometheus/text_serializer.h>

namespace lemon {

// ---------------------------------------------------------------------------
// Helper: default bucket boundaries and quantiles
// ---------------------------------------------------------------------------
static const prometheus::Histogram::BucketBoundaries kDefaultHistBuckets = {
    0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0
};

static const prometheus::Histogram::BucketBoundaries kTpsBuckets = {
    0.1, 0.5, 1.0, 2.5, 5.0, 10.0, 25.0, 50.0, 100.0
};

static const prometheus::Histogram::BucketBoundaries kSizeBuckets = {
    0, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304
};

static const prometheus::Histogram::BucketBoundaries kStreamDurationBuckets = {
    0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0, 120.0
};

static const prometheus::Summary::Quantiles kDefaultQuantiles = {
    {0.5, 0.1},   // median with 10% error
    {0.9, 0.05},  // 90th percentile with 5% error
    {0.99, 0.01}  // 99th percentile with 1% error
};

// ---------------------------------------------------------------------------
// MetricsCollector implementation
// ---------------------------------------------------------------------------

MetricsCollector::MetricsCollector() = default;
MetricsCollector::~MetricsCollector() { shutdown(); }

MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector instance;
    return instance;
}

void MetricsCollector::init() {
    LOG(INFO, "Metrics") << "Initializing Prometheus metrics collector" << std::endl;

    std::lock_guard<std::mutex> lock(registry_mutex_);

    registry_ = std::make_shared<prometheus::Registry>();

    // ---- Version info (single-sample gauge) -------------------------------
    // Standard Prometheus pattern: version_info{version="x.y.z",build_type="..."}=1
    version_info_ = &prometheus::BuildGauge()
        .Name("lemonade_version_info")
        .Help("Lemonade server version info (single-sample gauge)")
        .Register(*registry_);

    // ---- Request metrics --------------------------------------------------
    req_total_ = &prometheus::BuildCounter()
        .Name("lemonade_http_requests_total")
        .Help("Total HTTP requests by method, endpoint, and status code")
        .Register(*registry_);

    req_duration_ = &prometheus::BuildHistogram()
        .Name("lemonade_http_request_duration_seconds")
        .Help("HTTP request duration in seconds")
        .Register(*registry_);

    // Request/response body size histograms
    req_body_bytes_ = &prometheus::BuildHistogram()
        .Name("lemonade_request_body_bytes")
        .Help("HTTP request body size in bytes")
        .Register(*registry_);

    resp_body_bytes_ = &prometheus::BuildHistogram()
        .Name("lemonade_response_body_bytes")
        .Help("HTTP response body size in bytes")
        .Register(*registry_);

    // ---- Inference metrics ------------------------------------------------
    tokens_input_total_ = &prometheus::BuildCounter()
        .Name("lemonade_tokens_input_total")
        .Help("Total input tokens processed across all models")
        .Register(*registry_);

    tokens_output_total_ = &prometheus::BuildCounter()
        .Name("lemonade_tokens_output_total")
        .Help("Total output tokens generated across all models")
        .Register(*registry_);

    ttft_summary_ = &prometheus::BuildSummary()
        .Name("lemonade_time_to_first_token_seconds")
        .Help("Time to first token in seconds (p50, p90, p99)")
        .Register(*registry_);

    tps_histogram_ = &prometheus::BuildHistogram()
        .Name("lemonade_tokens_per_second")
        .Help("Token throughput in tokens per second")
        .Register(*registry_);

    // Model lifecycle
    models_loaded_ = &prometheus::BuildGauge()
        .Name("lemonade_models_loaded")
        .Help("Currently loaded models (1 = loaded, 0 = unloaded)")
        .Register(*registry_);

    model_load_total_ = &prometheus::BuildCounter()
        .Name("lemonade_model_load_total")
        .Help("Total model load attempts")
        .Register(*registry_);

    model_unload_total_ = &prometheus::BuildCounter()
        .Name("lemonade_model_unload_total")
        .Help("Total model unload operations")
        .Register(*registry_);

    // System resources
    cpu_usage_ = &prometheus::BuildGauge()
        .Name("lemonade_cpu_usage_percent")
        .Help("CPU usage percentage")
        .Register(*registry_);

    gpu_utilization_ = &prometheus::BuildGauge()
        .Name("lemonade_gpu_utilization_percent")
        .Help("GPU utilization percentage")
        .Register(*registry_);

    gpu_memory_used_ = &prometheus::BuildGauge()
        .Name("lemonade_gpu_memory_used_bytes")
        .Help("GPU memory usage in bytes")
        .Register(*registry_);

    system_memory_used_ = &prometheus::BuildGauge()
        .Name("lemonade_memory_used_bytes")
        .Help("System memory usage in bytes")
        .Register(*registry_);

    // Error metrics
    errors_total_ = &prometheus::BuildCounter()
        .Name("lemonade_errors_total")
        .Help("Total errors by endpoint and error type")
        .Register(*registry_);

    endpoint_errors_total_ = &prometheus::BuildCounter()
        .Name("lemonade_endpoint_errors_total")
        .Help("Errors broken down by endpoint, error type, and model")
        .Register(*registry_);

    // Streaming metrics
    stream_chunks_total_ = &prometheus::BuildCounter()
        .Name("lemonade_stream_chunks_total")
        .Help("Total streaming chunks sent via SSE")
        .Register(*registry_);

    stream_duration_ = &prometheus::BuildHistogram()
        .Name("lemonade_stream_duration_seconds")
        .Help("Duration of streaming responses in seconds")
        .Register(*registry_);

    // Set the version_info gauge to 1 (single-sample gauge pattern)
    version_info_->Add({{"version", LEMON_VERSION_STRING}})
        .Set(1.0);

    LOG(INFO, "Metrics") << "Version: " << LEMON_VERSION_STRING << std::endl;
}

void MetricsCollector::shutdown() {
    registry_.reset();
    LOG(INFO, "Metrics") << "Prometheus metrics collector shut down" << std::endl;
}

// ---- Metric accessors -----------------------------------------------------

std::shared_ptr<prometheus::Registry> MetricsCollector::get_registry() {
    return registry_;
}

std::string MetricsCollector::export_metrics() {
    if (!registry_) return "";
    auto metrics = registry_->Collect();
    std::ostringstream oss;
    prometheus::TextSerializer serializer;
    serializer.Serialize(oss, metrics);
    return oss.str();
}

// ---- Convenience method implementations -----------------------------------

void MetricsCollector::record_request(const std::string& method,
                                       const std::string& endpoint,
                                       int status_code) {
    if (!req_total_) return;
    req_total_->Add({{"method", method},
                     {"endpoint", endpoint},
                     {"status", std::to_string(status_code)}})
        .Increment();
}

void MetricsCollector::record_request_duration(const std::string& endpoint,
                                                  double duration_seconds,
                                                  const std::string& model) {
    if (!req_duration_) return;
    std::string model_label = model.empty() ? "unknown" : model;
    req_duration_->Add({{"endpoint", endpoint},
                        {"model", model_label}}, kDefaultHistBuckets)
        .Observe(duration_seconds);
}

void MetricsCollector::record_inference_telemetry(const std::string& model,
                                                    int input_tokens,
                                                    int output_tokens,
                                                    double ttft_seconds,
                                                    double tokens_per_second,
                                                    const std::string& backend,
                                                    const std::string& backend_version) {
    if (!tokens_input_total_ || !tokens_output_total_ || !ttft_summary_ || !tps_histogram_) return;

    // Build label sets — use "unknown" for empty labels so prometheus-cpp doesn't reject them
    std::string model_label = model.empty() ? "unknown" : model;
    std::string backend_label = backend.empty() ? "unknown" : backend;
    std::string version_label = backend_version.empty() ? "unknown" : backend_version;

    // Token counters with backend/version labels
    tokens_input_total_->Add({{"model", model_label},
                              {"backend", backend_label},
                              {"version", version_label}})
        .Increment(static_cast<double>(input_tokens));
    tokens_output_total_->Add({{"model", model_label},
                               {"backend", backend_label},
                               {"version", version_label}})
        .Increment(static_cast<double>(output_tokens));

    // TTFT summary with backend/version labels
    ttft_summary_->Add({{"model", model_label},
                        {"backend", backend_label},
                        {"version", version_label}},
                       kDefaultQuantiles,
                       std::chrono::milliseconds{60000}, 5)
        .Observe(ttft_seconds);

    // Tokens/sec histogram with backend/version labels
    tps_histogram_->Add({{"model", model_label},
                         {"backend", backend_label},
                         {"version", version_label}}, kTpsBuckets)
        .Observe(tokens_per_second);
}

void MetricsCollector::record_model_event(const std::string& model,
                                            const std::string& backend,
                                            bool loaded) {
    if (!models_loaded_ || !model_load_total_ || !model_unload_total_) return;

    if (loaded) {
        models_loaded_->Add({{"model", model},
                              {"backend", backend},
                              {"device", "auto"}})
            .Increment(1.0);
        model_load_total_->Add({{"model", model},
                                 {"backend", backend},
                                 {"success", "true"}})
            .Increment();
    } else {
        models_loaded_->Add({{"model", model},
                              {"backend", backend},
                              {"device", "auto"}})
            .Decrement(1.0);
        model_unload_total_->Add({{"model", model}})
            .Increment();
    }
}

void MetricsCollector::record_system_resources(double cpu_percent,
                                                 double gpu_utilization,
                                                 uint64_t gpu_memory_used_bytes,
                                                 uint64_t system_memory_used_bytes) {
    if (!cpu_usage_ || !gpu_utilization_ || !gpu_memory_used_ || !system_memory_used_) return;

    // Use {"instance", "localhost"} instead of empty labels - prometheus-cpp v1.2.0
    // requires at least one label per metric instance
    cpu_usage_->Add({{"instance", "localhost"}})
        .Set(cpu_percent);
    gpu_utilization_->Add({{"device", "auto"}})
        .Set(gpu_utilization);
    gpu_memory_used_->Add({{"device", "auto"}, {"type", "used"}})
        .Set(static_cast<double>(gpu_memory_used_bytes));
    system_memory_used_->Add({{"instance", "localhost"}})
        .Set(static_cast<double>(system_memory_used_bytes));
}

// ---- New metric helpers ---------------------------------------------------

void MetricsCollector::record_request_size(const std::string& endpoint,
                                            size_t body_size_bytes) {
    if (!req_body_bytes_) return;
    req_body_bytes_->Add({{"endpoint", endpoint}}, kSizeBuckets)
        .Observe(static_cast<double>(body_size_bytes));
}

void MetricsCollector::record_response_size(const std::string& endpoint,
                                              size_t body_size_bytes) {
    if (!resp_body_bytes_) return;
    resp_body_bytes_->Add({{"endpoint", endpoint}}, kSizeBuckets)
        .Observe(static_cast<double>(body_size_bytes));
}

void MetricsCollector::record_stream_chunk(const std::string& model,
                                            const std::string& endpoint) {
    if (!stream_chunks_total_) return;
    stream_chunks_total_->Add({{"model", model.empty() ? "unknown" : model},
                               {"endpoint", endpoint}})
        .Increment();
}

void MetricsCollector::record_stream_duration(const std::string& model,
                                                 const std::string& endpoint,
                                                 double duration_seconds) {
    if (!stream_duration_) return;
    stream_duration_->Add({{"model", model.empty() ? "unknown" : model},
                           {"endpoint", endpoint}}, kStreamDurationBuckets)
        .Observe(duration_seconds);
}

void MetricsCollector::record_error(const std::string& endpoint,
                                     const std::string& error_type,
                                     const std::string& model) {
    if (!errors_total_ || !endpoint_errors_total_) return;

    std::string model_label = model.empty() ? "unknown" : model;
    std::string error_label = error_type.empty() ? "unknown" : error_type;

    errors_total_->Add({{"endpoint", endpoint},
                        {"error_type", error_label}})
        .Increment();

    endpoint_errors_total_->Add({{"endpoint", endpoint},
                                  {"error_type", error_label},
                                  {"model", model_label}})
        .Increment();
}

void MetricsCollector::record_version_info(const std::string& version_string) {
    if (!version_info_) return;
    version_info_->Add({{"version", version_string.empty() ? "unknown" : version_string}})
        .Set(1.0);
}

} // namespace lemon
