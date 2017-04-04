#pragma once

#include <memory>

// #include <boost/optional.hpp>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>

#include <cocaine/format.hpp>
#include <cocaine/context.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/repository.hpp>

#include "cocaine/service/node/manifest.hpp"
#include "cocaine/service/node/profile.hpp"

#include "cocaine/api/isolate.hpp"

#include "cocaine/detail/service/node/engine.hpp"

#include <metrics/factory.hpp>
#include <metrics/registry.hpp>

#include "node/pool_observer.hpp"

namespace cocaine {
namespace detail {
namespace service {
namespace node {

struct metrics_aggregate_proxy_t;

struct worker_metrics_t {
    using shared_counter_type = metrics::shared_metric<std::atomic<std::uint64_t>>;

    using counters_table_type = std::unordered_map<std::string, shared_counter_type>;
    counters_table_type common_counters;

    //
    // TODO: more to come
    // ...

    worker_metrics_t(context_t& ctx, const std::string& name_prefix);
    worker_metrics_t(context_t& ctx, const std::string& app_name, const std::string& id);

    friend auto
    operator+(worker_metrics_t& src, metrics_aggregate_proxy_t& proxy) -> metrics_aggregate_proxy_t&;

    auto
    operator=(metrics_aggregate_proxy_t&& init) -> worker_metrics_t&;
};

struct metrics_aggregate_proxy_t {
    std::unordered_map<std::string, std::uint64_t> common_counters;

    auto
    operator+(const worker_metrics_t& worker_metrics) -> metrics_aggregate_proxy_t&;
};

/// Isolation daemon's workers metrics sampler.
///
/// Poll sequence should be initialized explicitly with
/// metrics_retriever_t::ignite_poll method or implicitly
/// within metrics_retriever_t::make_and_ignite.
class metrics_retriever_t :
    public std::enable_shared_from_this<metrics_retriever_t>
{
public:
    using stats_table_type = std::unordered_map<std::string, worker_metrics_t>;
private:
    using pool_type = engine_t::pool_type;

    context_t& context;

    asio::deadline_timer metrics_poll_timer;
    std::shared_ptr<api::isolate_t> isolate;

    synchronized<engine_t::pool_type> &pool;

    const std::unique_ptr<cocaine::logging::logger_t> log;

    //
    // Poll intervals could be quite large (should be configurable), so the
    // Isolation Daemon supports metrics `in memory` persistance for dead workers
    // (at least for 30 seconds) and it will be possible to query for workers which
    // have passed away not too long ago. Their uuids will be taken and added to
    // request from `purgatory`.
    //
    using purgatory_pot_type = std::set<std::string>;
    synchronized<purgatory_pot_type> purgatory;

    // `synchronized` not needed within current design, but shouldn't do any harm
    // <uuid, metrics>
    synchronized<stats_table_type> metrics;

    struct self_metrics_t {
        metrics::shared_metric<std::atomic<std::uint64_t>> uuid_requested;
        metrics::shared_metric<std::atomic<std::uint64_t>> uuid_recieved;
        metrics::shared_metric<std::atomic<std::uint64_t>> requests_send;
        metrics::shared_metric<std::atomic<std::uint64_t>> receive_errors;
        metrics::shared_metric<std::atomic<std::uint64_t>> posmortem_queue_size;
        self_metrics_t(context_t& ctx, const std::string& name);
    } self_metrics;

    std::string app_name;

    worker_metrics_t app_aggregate_metrics;
public:

    metrics_retriever_t(
        context_t& ctx,
        const std::string& name,
        std::shared_ptr<api::isolate_t> isolate,
        synchronized<engine_t::pool_type>& pool,
        asio::io_service& loop);

    template<typename Observers>
    static
    auto
    make_and_ignite(
        context_t& ctx,
        const std::string& name,
        std::shared_ptr<api::isolate_t> isolate,
        synchronized<engine_t::pool_type>& pool,
        asio::io_service& loop,
        synchronized<Observers>& observers) -> std::shared_ptr<metrics_retriever_t>;

    auto
    ignite_poll() -> void;

    auto
    make_observer() -> std::shared_ptr<pool_observer>;

    // should be called on every pool::erase(id) invocation
    //
    // usually isolation daemon will hold metrics of despawned workers for some
    // reasonable period (at least 30 sec), so it is allowed to request metrics
    // of dead workers on next `poll` invocation.
    //
    // Note: on high despawn rates stat of some unlucky workers will be lost
    //
    auto
    add_post_mortem(const std::string& id) -> void;

private:

    auto
    poll_metrics(const std::error_code& ec) -> void;

private:

    // TODO: wip, possibility of redesign
    struct metrics_handle_t : public api::metrics_handle_base_t
    {
        metrics_handle_t(std::shared_ptr<metrics_retriever_t> parent) :
            parent{parent}
        {}

        auto
        on_data(const dynamic_t& data) -> void override;

        auto
        on_error(const std::error_code&, const std::string& what) -> void override;

        std::shared_ptr<metrics_retriever_t> parent;
    };

    // TODO: wip, possibility of redesign
    struct metrics_pool_observer_t : public pool_observer {

        metrics_pool_observer_t(metrics_retriever_t &p) :
            parent(p)
        {}

        virtual
        auto
        spawned() -> void override {}

        virtual
        auto
        despawned() -> void override {}

        virtual
        auto
        despawned(const std::string& id) -> void override {
            parent.add_post_mortem(id);
        }

    private:
        metrics_retriever_t &parent;
    };

}; // metrics_retriever_t

template<typename Observers>
auto
metrics_retriever_t::make_and_ignite(
    context_t& ctx,
    const std::string& name,
    std::shared_ptr<api::isolate_t> isolate,
    synchronized<engine_t::pool_type>& pool,
    asio::io_service& loop, synchronized<Observers>& observers) -> std::shared_ptr<metrics_retriever_t>
{
    auto retriever = std::make_shared<metrics_retriever_t>(ctx, name, std::move(isolate), pool, loop);

    observers->emplace_back(retriever->make_observer());
    retriever->ignite_poll();

    return retriever;
}

}  // namespace node
}  // namespace service
}  // namespace detail
}  // namespace cocaine
