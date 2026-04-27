
## 9. Streaming & WebSocket Metrics (Additional)

### 9.1 Streaming Response Metrics
For SSE streaming (`chat_completion_stream`, `completion_stream`):
- Track per-chunk timing
- Record total stream duration
- Count chunks per stream
- Track stream errors

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `lemonade_stream_chunks_total` | Counter | `model`, `endpoint` | Total streaming chunks sent |
| `lemonade_stream_duration_seconds` | Histogram | `model`, `endpoint` | Total streaming duration |
| `lemonade_stream_errors_total` | Counter | `model`, `endpoint`, `error_type` | Streaming errors |

### 9.2 WebSocket Realtime API Metrics
For the realtime session (`realtime_session.cpp`):
- Track active websocket connections
- Track audio transcription tokens
- Track VAD events

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `lemonade_websocket_connections_active` | Gauge | — | Currently active WebSocket connections |
| `lemonade_websocket_messages_total` | Counter | `direction`, `type` | WebSocket message counts |
| `lemonade_vad_events_total` | Counter | — | VAD (Voice Activity Detection) events |
| `lemonade_realtime_audio_duration_seconds` | Histogram | `model` | Audio transcription duration |

## 10. Error Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `lemonade_errors_total` | Counter | `endpoint`, `error_type`, `model` | Total errors by type |
| `lemonade_model_load_errors_total` | Counter | `model`, `backend` | Model load failures |
| `lemonade_backend_unavailable_total` | Counter | `backend` | Backend process crashes |

## 11. Detailed Integration Points

### 11.1 Request Timing Middleware
Add to `server.cpp` `setup_routes()`:
```cpp
// Wrap each handler with timing middleware
auto timed_handler = [this](httplib::Request& req, httplib::Response& res, auto handler) {
    auto start = std::chrono::steady_clock::now();
    handler(req, res);
    auto end = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
    
    // Record request duration histogram
    MetricsCollector::instance().record_request_duration(
        req.path, duration_ms, req.get_param_value("model")
    );
};
```

### 11.2 Router Integration
In `router.cpp`, track:
- Model load/unload events
- Backend selection decisions
- Inference latency per backend

### 11.3 Telemetry Enhancement
Convert existing `WrappedServer::Telemetry` to Prometheus metrics:
```cpp
// In wrapped_server.cpp, after each inference call:
MetricsCollector::instance().record_inference_telemetry(
    server_name_,
    telemetry_.input_tokens,
    telemetry_.output_tokens,
    telemetry_.time_to_first_token,
    telemetry_.tokens_per_second
);
```

## 12. Platform-Specific Implementation Details

### 12.1 Linux GPU Metrics
- Use `nvidia-ml.h` (NVML) for NVIDIA GPUs
- Use `rocm-smi-lib` for AMD GPUs (ROCm)
- Fallback to `/sys/class/drm/card0/device/memory_used` for basic info

### 12.2 Windows GPU Metrics
- Use WMI (Windows Management Instrumentation)
- Query `Win32_VideoController` for VRAM
- Query `Win32_PerfFormattedData_NVIDIADriver` for utilization

### 12.3 macOS GPU Metrics
- Use `IOKit` for Apple Silicon GPU info
- Use `sysctl` for CPU metrics

### 12.4 System Memory
- Linux: `/proc/meminfo`
- Windows: `GlobalMemoryStatusEx`
- macOS: `sysctl` with `hw.memsize`

## 13. Testing Strategy

### 13.1 Unit Tests (test/cpp/test_prometheus_metrics.cpp)
```cpp
// Test metric registration
TEST(Metrics, CanRegisterCounter) {
    auto& collector = MetricsCollector::instance();
    collector.init(0); // Port 0 = don't bind, just init
    
    auto registry = collector.get_registry();
    ASSERT_NE(registry, nullptr);
}

// Test metric export format
TEST(Metrics, ExportsPrometheusFormat) {
    // Make some requests
    // Verify /metrics output contains expected lines
}
```

### 13.2 Integration Tests (test/test_prometheus.py)
```python
def test_metrics_endpoint():
    """Verify /metrics endpoint returns valid Prometheus format"""
    resp = requests.get("http://localhost:9105/metrics")
    assert resp.status_code == 200
    assert "# HELP" in resp.text
    assert "# TYPE" in resp.text

def test_request_counter():
    """Verify request counter increments"""
    # Make 10 requests
    for _ in range(10):
        requests.post("http://localhost:13305/v1/chat/completions", ...)
    
    # Verify counter increased
    metrics = requests.get("http://localhost:9105/metrics")
    assert "lemonade_http_requests_total" in metrics.text
```

### 13.3 Performance Tests
- Measure CPU overhead with and without metrics enabled
- Verify no memory leaks under sustained load
- Test with high request rate (100+ req/s)

## 14. Documentation Updates

### 14.1 README.md Addition
Add a section:
```markdown
## Monitoring & Metrics

Lemonade exposes Prometheus-compatible metrics on port `9105` (configurable):
- `GET /metrics` — Full Prometheus metric export
- Request counts, latencies, token throughput
- GPU/CPU/memory utilization
- Model loading status

Example Prometheus config:
```yaml
scrape_configs:
  - job_name: 'lemonade'
    static_configs:
      - targets: ['localhost:9105']
```
```

### 14.2 config.json Documentation
Document new `metrics` section in configuration reference.

