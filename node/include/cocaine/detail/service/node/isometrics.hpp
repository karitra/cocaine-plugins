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

struct worker_metrics_t {
    // running times
    metrics::shared_metric<std::atomic<std::uint64_t>> uptime;
    metrics::shared_metric<std::atomic<std::uint64_t>> user_time;
    metrics::shared_metric<std::atomic<std::uint64_t>> sys_time;

    // memory usage (in bytes)
    metrics::shared_metric<std::atomic<std::uint64_t>> vms;
    metrics::shared_metric<std::atomic<std::uint64_t>> rss;

    // disk io (in bytes)
    metrics::shared_metric<std::atomic<std::uint64_t>> ioread;
    metrics::shared_metric<std::atomic<std::uint64_t>> iowrite;

    // threads stats
    metrics::shared_metric<std::atomic<std::uint64_t>> threads_count;

    //
    // TODO: more to come
    // ...

    worker_metrics_t(context_t& ctx, const std::string& name_prefix);
    worker_metrics_t(context_t& ctx, const std::string& app_name, const std::string& id);
};

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

    // <uuid, metrics>
    synchronized<stats_table_type> metrics;

    struct self_metrics_t {
        metrics::shared_metric<std::atomic<std::uint64_t>> uuid_requested;
        metrics::shared_metric<std::atomic<std::uint64_t>> uuid_recieved;
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
        asio::io_service& loop, Observers& observers) -> std::shared_ptr<metrics_retriever_t>;

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

        auto
        spawned() -> void override {}

        auto
        despawned() -> void override {}

        auto
        despawned_with_id(const std::string& id) -> void override {
            parent.add_post_mortem(id);
        }

    private:
        metrics_retriever_t &parent;
    };

}; // metrics_retriever_t

}  // namespace node
}  // namespace service
}  // namespace detail
}  // namespace cocaine
