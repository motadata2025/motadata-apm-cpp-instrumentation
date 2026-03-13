/**
 * @file motadata.cpp
 * @brief Implementation of Motadata Instrumentation C++ Wrapper.
 *
 * This file contains the private internal logic and OpenTelemetry integration.
 * The implementation is hidden from the public API using the pImpl pattern.
 */

#include "motadata.h"

// OpenTelemetry API headers
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/span_context.h"

// OpenTelemetry SDK headers
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/multi_span_processor.h"

// OpenTelemetry Exporters
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"

// Resource metadata
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/semconv/service_attributes.h"

// Context propagation
#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/propagation/composite_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

// Utilities
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"

#include <iostream>
#include <atomic>
#include <memory>
#include <vector>
#include <stdexcept>

// Internal Namespace Aliases
namespace trace_api   = opentelemetry::trace;
namespace trace_sdk   = opentelemetry::sdk::trace;
namespace otlp_exp    = opentelemetry::exporter::otlp;
namespace ostream_exp = opentelemetry::exporter::trace;
namespace res_sdk     = opentelemetry::sdk::resource;
namespace propagation = opentelemetry::context::propagation;
namespace otel_ctx    = opentelemetry::context;
namespace nostd       = opentelemetry::nostd;
namespace common      = opentelemetry::common;

namespace motadata {

// Global Internal State
static nostd::shared_ptr<trace_api::TracerProvider> g_api_provider;
static std::shared_ptr<trace_sdk::TracerProvider>   g_sdk_provider;
static std::atomic<bool> g_initialized{false};

static constexpr const char* INSTRUMENTATION_NAME    = "motadata-instrumentation";
static constexpr const char* INSTRUMENTATION_VERSION = "1.0.0";

/**
 * @brief Internal helper to retrieve the global tracer instance.
 */
static nostd::shared_ptr<trace_api::Tracer> getTracer() {
    return trace_api::Provider::GetTracerProvider()
               ->GetTracer(INSTRUMENTATION_NAME, INSTRUMENTATION_VERSION);
}

// Internal implementation of SpanHandle::Context
struct SpanHandle::Context::Impl {
    otel_ctx::Context otel_ctx;
    explicit Impl(otel_ctx::Context c) : otel_ctx(std::move(c)) {}
};

// Internal implementation of SpanHandle
struct SpanHandle::Impl {
    nostd::shared_ptr<trace_api::Span> otel_span;
    std::unique_ptr<trace_api::Scope>  scope;
    otel_ctx::Context                  span_ctx;

    Impl(nostd::shared_ptr<trace_api::Span> s,
         std::unique_ptr<trace_api::Scope>  sc,
         otel_ctx::Context                  c)
        : otel_span(std::move(s))
        , scope(std::move(sc))
        , span_ctx(std::move(c))
    {}
};

// Internal carrier adapter for injecting context into HTTP headers
class WritableMapCarrier : public propagation::TextMapCarrier {
public:
    explicit WritableMapCarrier(HttpHeaders& h) : headers_(h) {}

    nostd::string_view Get(nostd::string_view) const noexcept override {
        return "";
    }
    void Set(nostd::string_view key, nostd::string_view value) noexcept override {
        headers_[std::string(key)] = std::string(value);
    }
private:
    HttpHeaders& headers_;
};

// Internal carrier adapter for extracting context from HTTP headers
class ReadableMapCarrier : public propagation::TextMapCarrier {
public:
    explicit ReadableMapCarrier(const HttpHeaders& h) : headers_(h) {}

    nostd::string_view Get(nostd::string_view key) const noexcept override {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return "";
    }
    void Set(nostd::string_view, nostd::string_view) noexcept override {
    }
private:
    const HttpHeaders& headers_;
};

bool initTelemetry(const TelemetryConfig& cfg) {
    if (g_initialized.load()) {
        std::cerr << "[motadata] WARNING: Telemetry already initialized. Ignoring.\n";
        return true;
    }

    try {
        // Step 1: Initialize OTLP HTTP Exporter
        otlp_exp::OtlpHttpExporterOptions otlp_opts;
        otlp_opts.url     = cfg.collector_url + "/v1/traces";
        otlp_opts.timeout = std::chrono::seconds(10);

        auto otlp_exporter = otlp_exp::OtlpHttpExporterFactory::Create(otlp_opts);
        if (!otlp_exporter) {
            std::cerr << "[motadata] ERROR: Failed to create instrumentation exporter.\n";
            return false;
        }

        // Step 2: Configure Span Exporters
        std::vector<std::unique_ptr<trace_sdk::SpanExporter>> exporters;
        exporters.push_back(std::move(otlp_exporter));

        if (cfg.debug_print_to_console) {
            exporters.push_back(ostream_exp::OStreamSpanExporterFactory::Create());
            std::cout << "[motadata] Debug mode enabled: spans exported to console.\n";
        }

        // Step 3: Configure Span Processors (Batching)
        std::vector<std::unique_ptr<trace_sdk::SpanProcessor>> processors;
        trace_sdk::BatchSpanProcessorOptions batch_opts;
        batch_opts.schedule_delay_millis = std::chrono::milliseconds(cfg.batch_flush_interval_ms);
        batch_opts.max_queue_size        = cfg.max_queue_size;
        batch_opts.max_export_batch_size = cfg.max_export_batch_size;

        for (auto& exp : exporters) {
            processors.push_back(trace_sdk::BatchSpanProcessorFactory::Create(std::move(exp), batch_opts));
        }

        // Step 4: Combine Processors
        std::unique_ptr<trace_sdk::SpanProcessor> final_processor;
        if (processors.size() == 1) {
            final_processor = std::move(processors[0]);
        } else {
            final_processor = std::unique_ptr<trace_sdk::SpanProcessor>(
                new trace_sdk::MultiSpanProcessor(std::move(processors))
            );
        }

        // Step 5: Configure Service Resources
        res_sdk::ResourceAttributes res_attrs;
        res_attrs[opentelemetry::semconv::service::kServiceName]    = cfg.service_name;
        res_attrs[opentelemetry::semconv::service::kServiceVersion] = cfg.service_version;
        if (!cfg.service_namespace.empty()) {
            res_attrs["service.namespace"] = cfg.service_namespace;
        }
        auto resource = res_sdk::Resource::Create(res_attrs);

        // Step 6: Create and Register Tracer Provider
        auto raw_provider = trace_sdk::TracerProviderFactory::Create(std::move(final_processor), resource);
        std::shared_ptr<trace_sdk::TracerProvider> sdk_shared(std::move(raw_provider));
        
        g_sdk_provider = sdk_shared;
        g_api_provider = nostd::shared_ptr<trace_api::TracerProvider>(
            std::static_pointer_cast<trace_api::TracerProvider>(sdk_shared)
        );
        trace_api::Provider::SetTracerProvider(g_api_provider);

        // Step 7: Configure Distributed Tracing (W3C Propagator)
        std::vector<std::unique_ptr<propagation::TextMapPropagator>> propagators;
        propagators.push_back(std::make_unique<opentelemetry::trace::propagation::HttpTraceContext>());
        auto composite_propagator = std::make_unique<propagation::CompositePropagator>(std::move(propagators));
        
        propagation::GlobalTextMapPropagator::SetGlobalPropagator(
            nostd::shared_ptr<propagation::TextMapPropagator>(composite_propagator.release())
        );

        g_initialized.store(true);
        std::cout << "[motadata] Motadata Instrumentation initialized for: " << cfg.service_name << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[motadata] CRITICAL: Initialization failed: " << e.what() << "\n";
        return false;
    }
}

void shutdown(int timeout_seconds) {
    if (!g_initialized.load()) return;

    std::cout << "[motadata] Shutting down instrumentation system...\n";
    if (g_sdk_provider) {
        g_sdk_provider->ForceFlush(std::chrono::seconds(timeout_seconds));
        g_sdk_provider->Shutdown();
        g_sdk_provider.reset();
        g_api_provider = nostd::shared_ptr<trace_api::TracerProvider>();
    }
    g_initialized.store(false);
    std::cout << "[motadata] Shutdown complete.\n";
}

bool isInitialized() {
    return g_initialized.load();
}

std::shared_ptr<SpanHandle> startSpan(
    const std::string& name,
    const SpanHandle::Context& parent_ctx)
{
    auto tracer = getTracer();
    trace_api::StartSpanOptions opts;
    opts.kind = trace_api::SpanKind::kInternal;

    if (parent_ctx.isValid()) {
        opts.parent = parent_ctx.impl->otel_ctx;
    }

    auto otel_span = tracer->StartSpan(name, {}, opts);
    auto scope = std::make_unique<trace_api::Scope>(tracer->WithActiveSpan(otel_span));
    
    auto current_ctx = otel_ctx::RuntimeContext::GetCurrent();
    auto span_ctx = trace_api::SetSpan(current_ctx, otel_span);

    auto impl = std::make_unique<SpanHandle::Impl>(
        std::move(otel_span),
        std::move(scope),
        std::move(span_ctx)
    );

    return std::shared_ptr<SpanHandle>(new SpanHandle(std::move(impl)));
}

void injectHeaders(HttpHeaders& headers) {
    auto propagator = propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto current    = otel_ctx::RuntimeContext::GetCurrent();
    WritableMapCarrier carrier(headers);
    propagator->Inject(carrier, current);
}

SpanHandle::Context extractContext(const HttpHeaders& headers) {
    auto propagator = propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto current    = otel_ctx::RuntimeContext::GetCurrent();
    ReadableMapCarrier carrier(headers);

    SpanHandle::Context ctx;
    ctx.impl = std::make_shared<SpanHandle::Context::Impl>(
        propagator->Extract(carrier, current)
    );
    return ctx;
}

// SpanHandle Method Implementations

SpanHandle::SpanHandle(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)), ended_(false)
{}

SpanHandle::~SpanHandle() {
    if (impl_ && !ended_) {
        std::cerr << "[motadata] WARNING: Span destroyed without being ended. Data may be lost.\n";
    }
}

SpanHandle& SpanHandle::setAttr(const std::string& key, const std::string& value) {
    if (impl_ && !ended_) impl_->otel_span->SetAttribute(key, value);
    return *this;
}

SpanHandle& SpanHandle::setAttr(const std::string& key, int64_t value) {
    if (impl_ && !ended_) impl_->otel_span->SetAttribute(key, value);
    return *this;
}

SpanHandle& SpanHandle::setAttr(const std::string& key, double value) {
    if (impl_ && !ended_) impl_->otel_span->SetAttribute(key, value);
    return *this;
}

SpanHandle& SpanHandle::setAttr(const std::string& key, bool value) {
    if (impl_ && !ended_) impl_->otel_span->SetAttribute(key, value);
    return *this;
}

SpanHandle& SpanHandle::addEvent(const std::string& name,
                                  const std::map<std::string, std::string>& attrs)
{
    if (!impl_ || ended_) return *this;

    if (attrs.empty()) {
        impl_->otel_span->AddEvent(name);
        return *this;
    }

    std::vector<std::string> keys;
    keys.reserve(attrs.size());
    std::vector<std::pair<nostd::string_view, common::AttributeValue>> kv_pairs;
    kv_pairs.reserve(attrs.size());

    for (const auto& kv : attrs) {
        keys.push_back(kv.first);
        kv_pairs.emplace_back(keys.back(), kv.second);
    }

    impl_->otel_span->AddEvent(
        name,
        common::SystemTimestamp{},
        opentelemetry::common::KeyValueIterableView<
            std::vector<std::pair<nostd::string_view, common::AttributeValue>>
        >{kv_pairs}
    );
    return *this;
}

SpanHandle& SpanHandle::recordException(const std::exception& e) {
    if (impl_ && !ended_) {
        std::vector<std::string> keys;
        keys.reserve(2);
        std::vector<std::pair<nostd::string_view, common::AttributeValue>> kv;
        kv.reserve(2);

        keys.push_back("exception.type");
        kv.emplace_back(keys.back(), std::string("std::exception"));
        keys.push_back("exception.message");
        kv.emplace_back(keys.back(), std::string(e.what()));

        impl_->otel_span->AddEvent(
            "exception",
            common::SystemTimestamp{},
            opentelemetry::common::KeyValueIterableView<
                std::vector<std::pair<nostd::string_view, common::AttributeValue>>
            >{kv}
        );
        impl_->otel_span->SetStatus(trace_api::StatusCode::kError, e.what());
    }
    return *this;
}

SpanHandle& SpanHandle::setOk() {
    if (impl_ && !ended_)
        impl_->otel_span->SetStatus(trace_api::StatusCode::kOk);
    return *this;
}

SpanHandle& SpanHandle::setError(const std::string& description) {
    if (impl_ && !ended_)
        impl_->otel_span->SetStatus(trace_api::StatusCode::kError, description);
    return *this;
}

SpanHandle::Context SpanHandle::getContext() const {
    SpanHandle::Context ctx;
    if (impl_) {
        ctx.impl = std::make_shared<SpanHandle::Context::Impl>(impl_->span_ctx);
    }
    return ctx;
}

void SpanHandle::end() {
    if (!impl_ || ended_) return;
    ended_ = true;
    impl_->scope.reset();
    impl_->otel_span->End();
}

bool SpanHandle::isEnded() const {
    return ended_;
}

} // namespace motadata
