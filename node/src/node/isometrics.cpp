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

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string &app_name, const std::string &id) :
    // running times
    uptime{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.{}.{}.uptime", app_name, "worker", id))},
    user_time{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.{}.{}.user_time", app_name, "worker", id))},
    sys_time{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.{}.{}.sys_time", app_name, "worker", id))},

    // memory gages
    vms{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.{}.{}.vms", app_name, "worker", id))},
    rss{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.{}.{}.rss", app_name, "worker", id))},

    // threads
    threads_count{ctx.metrics_hub().counter<uint64_t>(cocaine::format("{}.{}.{}.threads_count", app_name, "worker", id))}
{}

metrics_retriever_t::self_metrics_t(context_t &ctx, const std::string &name) :
   uuid_requested{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.requested", name))},
   uuid_recieved{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.recieved", name))},
   receive_errors{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.errors.count", name))},
   posmortem_queue_size{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.postmortem.queue.size", name))}
{}

metrics_retriever_t::metrics_retriever_t(
    context_t &ctx,
    const std::string &name, // app name
    std::shared_ptr<api::isolate_t> isolate,
    synchronized<engine_t::pool_type> &pool,
    asio::io_service &loop) :
        context(ctx),
        metrics_poll_timer(loop),
        isolate(std::move(isolate)),
        pool(pool),
        log(ctx.log(format("{}/workers_metrics", name))),
        self_metrics(ctx, cocaine::format("node.metrics.sampler.{}.", name)),
        app_name(name)
{
    COCAINE_LOG_INFO(log, "worker metrics retriever has been initialized");
}

auto
metrics_retriever_t::ignite_poll() -> void {
    metrics_poll_timer.expires_from_now(conf::METRICS_POLL_INTERVAL);
    metrics_poll_timer.async_wait(std::bind(&metrics_retriever_t::poll_metrics, shared_from_this(), ph::_1));
}

auto
metrics_retriever_t::add_post_mortem(const std::string &id) -> void {
    if (purgatory.size() >= conf::PURGATORY_QUEUE_BOUND) {
        // on high despawn rates stat of some unlucky workers will be lost
        return;
    }

    self_metrics.posmortem_queue_size->store(purgatory.size());

    purgatory.apply([&] (purgatory_pot_type &pot) {
        pot.emplace(id);
    });
}

auto
metrics_retriever_t::poll_metrics(const std::error_code& ec) -> void {
    if (ec) {
        // cancelled
        COCAINE_LOG_WARNING(log, "workers metrics polling was cancelled");
        return;
    }

    if (!isolate) {
        ignite_poll();
        return;
    }

    auto alive_uuids = pool.apply([] (const pool_type& pool) {
        std::vector<std::string> alive;
        alive.reserve(pool.size());

        // TODO: should we take active workers only?

        boost::copy(pool | boost::adaptors::map_keys, std::back_inserter(alive));
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

    purgatory.apply([&] (purgatory_pot_type &pot) {
        boost::set_union(alive_uuids, pot, std::back_inserter(query_array));
        pot.clear();
    });

#if 1
    std::cerr << "query array size: " << query_array.size() << '\n';
    for(const auto &el : query_array) {
        std::cerr << "\tuuid: " << el.as_string() << '\n';
    }
#endif

    dynamic_t::object_t query;
    query["uuids"] = query_array;
    isolate->metrics(query, std::make_shared<metrics_handle_t>(shared_from_this()));

    self_metrics.uuid_requested->fetch_add(query_array.size());

    ignite_poll();
}

auto
metrics_retriever_t::metrics_handle_t::on_data(const dynamic_t& data) -> voido {
    std::cerr << "metrics_handle_t:::on_data TBD\n";
    std::cerr << "get json: " << boost::lexical_cast<std::string>(data) << '\n';

    // TODO
    parent->self_metrics.uuid_recieved->fetch_add(1);
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
