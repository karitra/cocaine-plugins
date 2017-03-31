#include "cocaine/detail/service/node/isometrics.hpp"

#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>

namespace cocaine {
namespace detail {
namespace service {
namespace node {

namespace ph = std::placeholders;

namespace conf {
    const auto METRICS_POLL_INTERVAL = boost::posix_time::seconds(2);

    constexpr auto PURGATORY_QUEUE_BOUND = 8 * 1024;
}

// TODO: hardly WIP
struct response_processor_t {
    using stats_table_type = metrics_retriever_t::stats_table_type;

    struct error_t {
        int code;
        std::string msg;
    };

    response_processor_t(
        const std::string app_name,
        dynamic_t& response,
        stats_table_type &stats_table) :
            app_name(app_name),
            response(response),
            stats_table(stats_table),
            processed{false}
    {
        this->process();
    }

    auto
    process() -> void {
        const auto apps = response.as_object();

        // strictly, we should have one app at all by current protocol
        for(const auto &app : apps) {

            if (app_name != app.first) {
                continue;
            }

            const auto uuids = app.second.as_array();

            for(const auto &it : uuids) {
                const auto records = it.as_object();

                // TODO: do funny things

            }
        }

        processed = true;
    }

    auto
    is_processed() const -> bool {
        return processed;
    }

    auto
    errors_count() const -> std::size_t {
        return errors.size();
    }

    auto
    metrics_ref() -> const stats_table_type& {
        return stats_table;
    }

    auto
    errors_ref() -> const std::vector<error_t>& {
        return errors;
    }

private:
    std::string app_name;

    dynamic_t& response;
    stats_table_type& stats_table;

    bool processed;

    std::vector<error_t> errors;
};


worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& name_prefix) :
    // running times
    uptime{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.uptime", name_prefix))},
    user_time{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.user_time", name_prefix))},
    sys_time{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.sys_time", name_prefix))},

    // memory usage
    vms{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.vms", name_prefix))},
    rss{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.rss", name_prefix))},

    // io disk
    ioread{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.ioread", name_prefix))},
    iowrite{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.iowrite", name_prefix))},

    // threads
    threads_count{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.threads_count", name_prefix))}
{}

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& app_name, const std::string& id) :
    worker_metrics_t(ctx, cocaine::format("{}.{}", app_name, id))
{}

metrics_retriever_t::self_metrics_t::self_metrics_t(context_t& ctx, const std::string& name) :
   uuid_requested{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.requested", name))},
   uuid_recieved{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.recieved", name))},
   receive_errors{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.errors.count", name))},
   posmortem_queue_size{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.postmortem.queue.size", name))}
{}

metrics_retriever_t::metrics_retriever_t(
    context_t& ctx,
    const std::string& name, // app name
    std::shared_ptr<api::isolate_t> isolate,
    synchronized<engine_t::pool_type>& pool,
    asio::io_service& loop) :
        context(ctx),
        metrics_poll_timer(loop),
        isolate(std::move(isolate)),
        pool(pool),
        log(ctx.log(format("{}/workers_metrics", name))),
        self_metrics(ctx, cocaine::format("node.metrics.sampler.{}.", name)),
        app_name(name),
        app_aggregate_metrics(ctx, name)
{
    COCAINE_LOG_INFO(log, "worker metrics retriever has been initialized");
}

auto
metrics_retriever_t::ignite_poll() -> void {
    metrics_poll_timer.expires_from_now(conf::METRICS_POLL_INTERVAL);
    metrics_poll_timer.async_wait(std::bind(&metrics_retriever_t::poll_metrics, shared_from_this(), ph::_1));
}

auto
metrics_retriever_t::add_post_mortem(const std::string& id) -> void {
    if (purgatory->size() >= conf::PURGATORY_QUEUE_BOUND) {
        // on high despawn rates stat of some unlucky workers will be lost
        return;
    }

    self_metrics.posmortem_queue_size->store(purgatory->size());

    purgatory.apply([&] (purgatory_pot_type &pot) {
        pot.emplace(id);
    });
}

auto
metrics_retriever_t::poll_metrics(const std::error_code& ec) -> void {
    using boost::adaptors::map_keys;

    if (ec) {
        // cancelled
        COCAINE_LOG_WARNING(log, "workers metrics polling was cancelled");
        return;
    }

    if (!isolate) {
        ignite_poll();
        return;
    }

    auto alive_uuids = pool.apply([] (const engine_t::pool_type& pool) {
        std::vector<std::string> alive;
        alive.reserve(pool.size());

        // TODO: should we take active workers only?

        boost::copy(pool | map_keys, std::back_inserter(alive));
        return alive;
    });

#if 1
    std::cerr << "metrics.pool.size " << alive_uuids.size() << '\n';
    for(const auto &el : alive_uuids) {
        std::cerr << "\tuuid: " << el << '\n';
    }
#endif

    dynamic_t::array_t query_array;
    query_array.reserve(alive_uuids.size() + purgatory->size());

    // Note: it is promised by devs that active list should be quite
    // small ~ hundreds of workers, so it seems reasonable to pay a little for
    // sorting here, but code should be redisigned if average alive count
    // will increase significantly
    boost::sort(alive_uuids);

    purgatory.apply([&] (purgatory_pot_type& pot) {
        boost::set_union(alive_uuids, pot, std::back_inserter(query_array));
        pot.clear();
    });

#if 1
    std::cerr << "query array size: " << query_array.size() << '\n';
    for(const auto& el : query_array) {
        std::cerr << "\tuuid: " << el.as_string() << '\n';
    }
#endif

    dynamic_t::object_t query;
    query["uuids"] = query_array;
    isolate->metrics(query, std::make_shared<metrics_handle_t>(shared_from_this()));

    self_metrics.uuid_requested->fetch_add(query_array.size());

    ignite_poll();
}

//// metrics_handle_t //////////////////////////////////////////////////////////

auto
metrics_retriever_t::metrics_handle_t::on_data(const dynamic_t& data) -> void {
    std::cerr << "metrics_handle_t:::on_data TBD\n";
    std::cerr << "get json: " << boost::lexical_cast<std::string>(data) << '\n';

    // TODO
    parent->self_metrics.uuid_recieved->fetch_add(1);
}

template<typename Observers>
auto
metrics_retriever_t::make_and_ignite(
    context_t& ctx,
    const std::string& name,
    std::shared_ptr<api::isolate_t> isolate,
    synchronized<engine_t::pool_type>& pool,
    asio::io_service& loop, Observers& observers) -> std::shared_ptr<metrics_retriever_t>
{
    auto retriever = std::make_shared<metrics_retriever_t>(ctx, name, std::move(isolate), pool, loop);

    observers->emplace_back(retriever->make_observer());
    retriever->ignite_poll();

    return retriever;
}

auto
metrics_retriever_t::make_observer() -> std::shared_ptr<pool_observer> {
    return std::make_shared<metrics_pool_observer_t>(*this);
}

auto
metrics_retriever_t::metrics_handle_t::on_error(const std::error_code&, const std::string& what) -> void {
    std::cerr << "metrics_handle_t::on_error: " << what << '\n';
    // TODO
    parent->self_metrics.receive_errors->fetch_add(1);
}

} // namespace node
} // namespace service
} // namespace detail
} // namespace cocaine
