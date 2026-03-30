#pragma once

/**
 * @file motadata.h
 * @brief Motadata Instrumentation C++ Wrapper
 *
 * This is the primary header for the Motadata Instrumentation library.
 * It provides a clean, opaque C++ interface for distributed tracing,
 * abstracting away the underlying OpenTelemetry implementation.
 *
 * Client usage pattern:
 * @code
 *    #include "motadata.h"
 *
 *    int main() {
 *        motadata::initTelemetry("payment-service");
 *
 *        auto span = motadata::startSpan("processPayment");
 *        span->setAttr("order.id", "ORD-001");
 *        processPayment();
 *        span->end();
 *
 *        motadata::shutdown();
 *    }
 * @endcode
 */

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <utility>

namespace motadata {

/**
 * @class SpanHandle
 * @brief A handle to an active instrumentation span.
 *
 * This class uses the pImpl (Pointer to Implementation) pattern to hide 
 * internal instrumentation types from the client. It represents a single 
 * unit of work or operation within a trace.
 */
class SpanHandle {
public:
    /**
     * @brief Destructor.
     * Warns if the span was not explicitly ended via end().
     */
    ~SpanHandle();

    /**
     * @brief Set a metadata attribute on the span.
     * Attributes provide context about the operation being performed.
     *
     * @param key   The attribute key (e.g., "user.id").
     * @param value The attribute value.
     * @return Reference to self for chaining.
     */
    SpanHandle& setAttr(const std::string& key, const std::string& value);
    SpanHandle& setAttr(const std::string& key, const char* value);
    SpanHandle& setAttr(const std::string& key, int64_t value);
    SpanHandle& setAttr(const std::string& key, double value);
    SpanHandle& setAttr(const std::string& key, bool value);

    /**
     * @brief Add a timestamped event to the span.
     * Events represent meaningful milestones within an operation.
     *
     * @param name  The name of the event.
     * @param attrs Optional key-value attributes for the event.
     * @return Reference to self for chaining.
     */
    SpanHandle& addEvent(const std::string& name,
                         const std::map<std::string, std::string>& attrs = {});

    /**
     * @brief Record exception information on the span.
     * Automatically sets the span status to Error.
     *
     * @param e The exception to record.
     * @return Reference to self for chaining.
     */
    SpanHandle& recordException(const std::exception& e);

    /**
     * @brief Mark the operation as successful.
     */
    SpanHandle& setOk();

    /**
     * @brief Mark the operation as failed with an optional error description.
     * @param description A brief message explaining the error.
     */
    SpanHandle& setError(const std::string& description = "");

    /**
     * @brief End the span.
     * Records the end time and prepares the span for export. 
     * Must be called exactly once per span.
     */
    void end();

    /**
     * @brief Check if the span has been ended.
     */
    bool isEnded() const;

    /**
     * @struct Context
     * @brief Opaque container for span propagation context.
     * Used to link parent and child spans across different execution contexts.
     */
    struct Context {
        struct Impl;
        std::shared_ptr<Impl> impl;
        bool isValid() const { return impl != nullptr; }
    };

    /**
     * @brief Retrieve the context for cross-thread or cross-process linking.
     */
    Context getContext() const;

    /// @private - Internal implementation details
    struct Impl;
    explicit SpanHandle(std::unique_ptr<Impl> impl);

    // Spans are unique entities and cannot be copied.
    SpanHandle(const SpanHandle&)            = delete;
    SpanHandle& operator=(const SpanHandle&) = delete;
    
    // Support move semantics.
    SpanHandle(SpanHandle&&)                 = default;
    SpanHandle& operator=(SpanHandle&&)      = default;

private:
    std::unique_ptr<Impl> impl_;
    bool ended_ = false;
};

/**
 * @struct TelemetryConfig
 * @brief Configuration parameters for Motadata Instrumentation.
 */
struct TelemetryConfig {
    /// The name of the service (e.g., "order-processor").
    std::string service_name      = "cpp-service";
    /// The version of the service.
    std::string service_version   = "1.0.0";
    /// Optional logical namespace for the service.
    std::string service_namespace = "";

    /// URL of the instrumentation collector (e.g., "http://localhost:4318").
    std::string collector_url     = "http://localhost:4318";

    /// Interval in milliseconds to flush batched spans to the collector.
    int batch_flush_interval_ms   = 5000;
    /// Maximum number of spans to queue in memory.
    int max_queue_size            = 2048;
    /// Maximum number of spans to export in a single batch.
    int max_export_batch_size     = 512;

    /// If true, span information is also printed to the standard output.
    bool debug_print_to_console   = false;

    /**
     * @brief Additional resource attributes applied to every span.
     *
     * These are stamped on the Resource (not individual spans), so they appear on ALL traces
     *
     * Example:
     * @code
     *   cfg.custom_resource_attrs = {
     *       {"deployment.environment", "production"},
     *       {"host.name",              "web-01"},
     *       {"apm.team",               "backend"},
     *   };
     * @endcode
     */
    std::vector<std::pair<std::string, std::string>> custom_resource_attrs;
};

/**
 * @brief Initialize the Motadata Instrumentation system.
 * Must be called exactly once at application startup.
 *
 * @param config Configuration settings.
 * @return true if initialization was successful.
 */
bool initTelemetry(const TelemetryConfig& config);

/**
 * @brief Convenience overload for standard initialization.
 * @param service_name Name of the service.
 * @param version      Version of the service.
 */
inline bool initTelemetry(const std::string& service_name,
                           const std::string& version = "1.0.0") {
    TelemetryConfig cfg;
    cfg.service_name    = service_name;
    cfg.service_version = version;
    return initTelemetry(cfg);
}

/**
 * @brief Create and start a new span.
 * @param name       The name of the operation.
 * @param parent_ctx Optional parent context for distributed traces.
 * @return A shared pointer to the newly created SpanHandle.
 */
std::shared_ptr<SpanHandle> startSpan(
    const std::string& name,
    const SpanHandle::Context& parent_ctx = SpanHandle::Context{});

/**
 * @brief Shutdown the instrumentation system and flush all pending data.
 * Should be called before application exit.
 *
 * @param timeout_seconds Time to wait for the final flush to complete.
 */
void shutdown(int timeout_seconds = 10);

/**
 * @brief Check if the instrumentation system is initialized.
 */
bool isInitialized();

/**
 * @typedef HttpHeaders
 * @brief Map for storing HTTP header keys and values.
 */
using HttpHeaders = std::map<std::string, std::string>;

/**
 * @brief Inject trace context into outgoing HTTP headers.
 * Used for distributed tracing to propagate span IDs.
 */
void                  injectHeaders(HttpHeaders& headers);

/**
 * @brief Extract trace context from incoming HTTP headers.
 * @return The extracted context for starting child spans.
 */
SpanHandle::Context   extractContext(const HttpHeaders& headers);

} // namespace motadata