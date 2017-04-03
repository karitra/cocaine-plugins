#include <tuple>
#include <cassert>

#include "cocaine/detail/service/node/isometrics.hpp"

#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/numeric.hpp>

#include <boost/utility/string_ref.hpp>

#if 1
#define dbg(msg) std::cerr << (msg) << '\n'
#else
#define dbg(msg)
#endif

namespace cocaine {
namespace detail {
namespace service {
namespace node {

namespace ph = std::placeholders;

namespace conf {
    const auto METRICS_POLL_INTERVAL = boost::posix_time::seconds(2);

    constexpr auto PURGATORY_QUEUE_BOUND = 8 * 1024;

    const std::vector<std::string> metrics_names =
    {
        // yet abstract cpu load measurement
        "cpu",

        // running times
        "uptime",
        "user_time",
        "sys_time",

        // memory usage (in bytes)
        "vms",
        "rss",

        // disk io (in bytes)
        "ioread",
        "iowrite",

        // threads stats
        "threads_count"
    };
}

auto
metrics_aggregate_proxy_t::operator+(const worker_metrics_t& worker_metrics) -> metrics_aggregate_proxy_t&
{
    for(const auto& name : conf::metrics_names) {

        auto self_it = common_counters.find(name);
        const auto worker_it = worker_metrics.common_counters.find(name);

        if (self_it != std::end(common_counters) && worker_it != std::end(worker_metrics.common_counters) ) {
            self_it->second += worker_it->second->load();
        }
    }

    return *this;
}

auto
operator+(worker_metrics_t& src, metrics_aggregate_proxy_t& proxy) -> metrics_aggregate_proxy_t&
{
    return proxy + src;
}

auto
worker_metrics_t::operator=(metrics_aggregate_proxy_t&& proxy) -> worker_metrics_t& {
    for(const auto& name : conf::metrics_names) {
        auto self_it = this->common_counters.find(name);
        const auto proxy_it = proxy.common_counters.find(name);

        if (self_it != std::end(this->common_counters) && proxy_it != std::end(proxy.common_counters) ) {
            self_it->second->store(proxy_it->second);
        }
    }

    return *this;
}

// TODO: hardly WIP
struct response_processor_t {
    using stats_table_type = metrics_retriever_t::stats_table_type;

    struct error_t {
        int code;
        std::string msg;
    };

    response_processor_t(const std::string app_name) :
        app_name(app_name),
        processed{false}
    {}

    auto
    operator()(context_t &ctx, const dynamic_t& response, stats_table_type& stats_table) -> size_t {
        return process(ctx, response, stats_table);
    }

    auto
    process(context_t &ctx, const dynamic_t& response, stats_table_type& stats_table) -> size_t {
        const auto apps = response.as_object();

        auto uuids_processed = size_t{};

        // strictly, we should have one app at all within current protocol
        for(const auto& app : apps) {

            if (app_name != app.first) {
                continue;
            }

            const auto uuids = app.second.as_object();

            for(const auto& item : uuids) {
                const auto& uuid = item.first;
                const auto& metrics = item.second.as_object();

                if (uuid.empty()) {
                    continue;
                }

                auto stat_it = stats_table.find(uuid);
                if (stat_it == std::end(stats_table)) {
                    tie(stat_it, std::ignore) = stats_table.emplace(uuid, worker_metrics_t{ctx, app_name, uuid});
                }

                auto common_it = metrics.find("common");
                if (common_it == std::end(metrics)) {
                    continue;
                }

                fill_metrics(common_it->second.as_object(), stat_it->second.common_counters);

                ++uuids_processed;
            }
        }

        processed = true;

        return uuids_processed;
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
    has_errors() -> bool {
        return ! errors.empty();
    }

    auto
    errors_ref() -> const std::vector<error_t>& {
        return errors;
    }

private:

    auto
    fill_metrics(const dynamic_t::object_t& metrics, worker_metrics_t::counters_table_type& result) -> void {
        for(const auto& metric : metrics) {
            const auto& name = metric.first;

            const auto pos = name.find('.');
            if (pos == boost::string_ref::npos) {
                // protocol error: ingore silently
                continue;
            }

            const auto nm   = name.substr(0, pos);
            const auto type = name.substr(pos+1);

            if (nm.empty() || type.empty()) {
                // protocol error: ingore silently
                continue;
            }

            auto r = result.find(nm);
            if (r != std::end(result)) {
                r->second->store(metric.second.as_double());
            }
        }
    }

private:
    std::string app_name;

    bool processed;

    std::vector<error_t> errors;
};

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& name_prefix)
{
    for(const auto &name : conf::metrics_names) {
        common_counters.emplace(name, ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.{}", name_prefix, name)));
    }
}

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& app_name, const std::string& id) :
    worker_metrics_t(ctx, cocaine::format("{}.{}", app_name, id))
{}

metrics_retriever_t::self_metrics_t::self_metrics_t(context_t& ctx, const std::string& name) :
   uuid_requested{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.uuid_requested", name))},
   uuid_recieved{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.uuid_recieved", name))},
   requests_send{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.requests", name))},
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

        boost::copy(pool | map_keys, std::back_inserter(alive));
        return alive;
    });

#if 1
    std::cerr << "metrics.pool.size " << alive_uuids.size() << '\n';
    for(const auto &el : alive_uuids) {
        std::cerr << "\tuuid: " << el << '\n';
    }
#endif

#if 1
    std::cerr << "purgatory.pot.size " << purgatory->size() << '\n';
    purgatory.apply([&](const purgatory_pot_type &pot) {
        for(const auto &sinner : pot) {
            std::cerr << "\tuuid: " << sinner << '\n';
        }
    });
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

    // At this point query is posted and we have gathered uuids of available
    // (alived, pooled) workers and dead recently workers, so we can clear
    // stat_table out of garbage `neither alive nor dead` uuids
    stats_table_type preserved_metrics;
    preserved_metrics.reserve(metrics->size());

    metrics.apply([&](stats_table_type &table) {
        for(const auto to_preserve : query_array) {
            const auto it = table.find(to_preserve.as_string());
            if (it == std::end(table)) {
                continue;
            }

            preserved_metrics.emplace(std::move(*it));
        }

        table.clear();
        table.swap(preserved_metrics);
    });

    // Update self stat
    self_metrics.uuid_requested->fetch_add(query_array.size());
    self_metrics.requests_send->fetch_add(1);

    ignite_poll();
}

auto
metrics_retriever_t::make_observer() -> std::shared_ptr<pool_observer> {
    return std::make_shared<metrics_pool_observer_t>(*this);
}

//// metrics_handle_t //////////////////////////////////////////////////////////

auto
metrics_retriever_t::metrics_handle_t::on_data(const dynamic_t& data) -> void {
    using namespace boost::adaptors;

    assert(parent);

    std::cerr << "metrics_handle_t:::on_data TBD\n";
    std::cerr << "get json: " << boost::lexical_cast<std::string>(data) << '\n';

    // should not harm performance, as this handler would be called from same
    // poll loop, within same thread on each poll iteration
    const auto processed_count = parent->metrics.apply([&](metrics_retriever_t::stats_table_type& table) {
        response_processor_t processor(parent->app_name);

        // fill worker current `slice` metrics table
        const auto processed_count = processor(parent->context, data, table);

        // update application-wide aggregate of metrics
        parent->app_aggregate_metrics = boost::accumulate( table | map_values, metrics_aggregate_proxy_t());

        return processed_count;
    });

    parent->self_metrics.uuid_recieved->fetch_add(processed_count);
}

auto
metrics_retriever_t::metrics_handle_t::on_error(const std::error_code&, const std::string& what) -> void {
    assert(parent);

    std::cerr << "metrics_handle_t::on_error: " << what << '\n';
    // TODO
    parent->self_metrics.receive_errors->fetch_add(1);
}

} // namespace node
} // namespace service
} // namespace detail
} // namespace cocaine
