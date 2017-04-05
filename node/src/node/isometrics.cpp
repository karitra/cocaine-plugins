#include <tuple>
#include <cassert>

#include "isometrics.hpp"

#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/numeric.hpp>

// #define ISOMETRICS_DEBUG
#undef ISOMETRICS_DEBUG

#ifdef ISOMETRICS_DEBUG
#define dbg(msg) std::cerr << msg << '\n'
#define DBG_DUMP_UUIDS(os, logo, container) aux::dump_uuids(os, logo, container)
#else
#define dbg(msg)
#define DBG_DUMP_UUIDS(os, logo, container)
#endif

namespace cocaine {
namespace detail {
namespace service {
namespace node {

namespace ph = std::placeholders;

namespace conf {
    constexpr auto PURGATORY_QUEUE_BOUND = 8 * 1024;
    constexpr auto SHOW_ERRORS_LIMIT = 3;

    // <name, is metric monotonically increasing per application (aggregate) level>
    const std::vector<std::pair<std::string, bool>> counter_metrics_names =
    {
        // yet abstract cpu load measurement
        { "cpu", false},

        // memory usage (in bytes)
        { "vms", false },
        { "rss", false },

        // running times
        { "uptime",    true },
        { "user_time", true },
        { "sys_time",  true },

        // disk io (in bytes)
        { "ioread",  true },
        { "iowrite", true },

        // TODO: network stats

    };
}

namespace aux {

    template<typename Val>
    auto
    uuid_value(const Val& v) -> std::string;

    template<>
    auto
    uuid_value(const std::string& v) -> std::string {
        return v;
    }

    template<>
    auto
    uuid_value(const dynamic_t& arr) -> std::string {
        return arr.as_string();
    }

    template<typename Container>
    auto
    dump_uuids(std::ostream& os, const std::string& logo, const Container& arr) -> void {
        os << logo << " size " << arr.size() << '\n';
        for(const auto& uuid : arr) {
            os << "\tuuid: " << uuid_value(uuid) << '\n';
        }
    }
}

auto
metrics_aggregate_proxy_t::operator+(const worker_metrics_t& worker_metrics) -> metrics_aggregate_proxy_t&
{
    using boost::adaptors::map_keys;
    using counters_record_type = worker_metrics_t::counters_record_type;

    dbg("worker_metrics_t::operator+() summing to proxy");

    boost::for_each(worker_metrics.common_counters, [&](const counters_record_type &worker_record) {
        const auto& name = worker_record.first;
        const auto& metric = worker_record.second;

        auto& self_record = this->common_counters[name];

        self_record.values += metric.value->load();
        self_record.deltas += metric.delta;
    });

    return *this;
}

auto
operator+(worker_metrics_t& src, metrics_aggregate_proxy_t& proxy) -> metrics_aggregate_proxy_t&
{
    return proxy + src;
}

auto
worker_metrics_t::operator=(metrics_aggregate_proxy_t&& proxy) -> worker_metrics_t& {

    dbg("worker_metrics_t::operator=() moving from proxy");

    if (proxy.common_counters.empty()) {

        // no active workers, zero out some metrics values
        for(const auto& init_metrics : conf::counter_metrics_names) {
            const auto& name = init_metrics.first;
            const auto& must_be_preserved = init_metrics.second;

            dbg("preserved " << name << " : " << must_be_preserved);

            auto self_it = this->common_counters.find(name);
            if (self_it != std::end(this->common_counters) && must_be_preserved == false) {
                    self_it->second.value->store(0);
            }
        }

    } else {

        for(const auto& proxy_metrics : proxy.common_counters) {
            const auto& name = proxy_metrics.first;
            const auto& proxy_record = proxy_metrics.second;

            auto self_it = this->common_counters.find(name);
            if (self_it != std::end(this->common_counters)) {
                auto& self_record = self_it->second;

                if (self_record.is_accumulated) { // monotonically increasing
                    self_record.value->fetch_add(proxy_record.deltas);
                } else {
                    self_record.value->store(proxy_record.values);
                }
            }

        } // for metrics in proxy
    }

    return *this;
}

// TODO: error messages processing
struct response_processor_t {
    using stats_table_type = metrics_retriever_t::stats_table_type;

    struct error_t {
        long code;
        std::string message;
    };

    response_processor_t(const std::string app_name) :
        app_name(app_name),
        processed{false}
    {}

    auto
    operator()(context_t& ctx, const dynamic_t& response, stats_table_type& stats_table) -> size_t {
        return process(ctx, response, stats_table);
    }

    auto
    process(context_t& ctx, const dynamic_t& response, stats_table_type& stats_table) -> size_t {
        const auto apps = response.as_object();

        auto uuids_processed = size_t{};

        for(const auto& app : apps) {

            // TODO: restore app_name assertion check
            //
            // strictly, we should have one app at all within current protocol
            // if (app_name != app.first) {
            //     dbg("JSON app is " << app.first);
            //     continue;
            // }

            const auto uuids = app.second.as_object();

            for(const auto& item : uuids) {
                const auto& uuid = item.first;
                const auto& metrics = item.second.as_object();

                if (uuid.empty()) {
                    dbg("[response] 'uuid' value is empty, ignoring");
                    continue;
                }

                if (uuid.compare("error") == 0) {
                    parse_error_record(metrics);
                    continue;
                }

                auto stat_it = stats_table.find(uuid);
                if (stat_it == std::end(stats_table)) {
                    // If isolate daemon sends us uuid we don't know about,
                    // add it to the stats (monitoring) table. If it was send by error, it
                    // would be removed from request list and from stats table
                    // on next poll iteration preparation.
                    dbg("[response] inserting new metrics record with uuid" << uuid);
                    tie(stat_it, std::ignore) = stats_table.emplace(uuid, worker_metrics_t{ctx, app_name, uuid});
                }

                auto common_it = metrics.find("common");
                if (common_it == std::end(metrics)) {
                    dbg("[response] no `common` section");
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
    parse_error_record(const dynamic_t::object_t& metrics) -> void {
        const auto& error_message = metrics.at("error.message", "none").as_string();
        const auto& error_code    = metrics.at("error.code", 0).as_int();

        errors.emplace_back(error_t{error_code, error_message});

        dbg("[response] got an error: " << error_message << " code: " << error_code);
    }

    auto
    fill_metrics(const dynamic_t::object_t& metrics, worker_metrics_t::counters_table_type& result) -> void {

        if (metrics.count("error.message")) {
            parse_error_record(metrics);
        }

        for(const auto& metric : metrics) {
            const auto& name = metric.first;

            dbg("[response] metrics name: " << name);

            // cut off type from name
            const auto pos = name.find('.');
            const auto nm = name.substr(0, pos);

            if (nm.empty()) {
                // protocol error: ingore silently
                // type not checked (for now)
                continue;
            }

            try {
                auto r = result.find(nm);
                if (r != std::end(result)) {
                    // we are interested in this metrics, its name was registered
                    dbg("[response] found metrics record for " << nm << ", updating...");

                    auto& record = r->second;
                    const auto& incoming_value = metric.second.as_uint();

                    if (record.is_accumulated) {
                        const auto& current = record.value->load();
                        if (incoming_value >= current) {
                            record.delta = incoming_value - current;
                        } // else: client misbehavior, shouldn't try to store negative deltas
                    }

                    record.value->store(incoming_value);
                }
            // note: e is boost::bad_get in current implementaion
            } catch (const std::exception& e) {
                dbg("[response] parsing exception: " << e.what());
                continue;
            }

        } // for each metric

        dbg("[response] fill_metrics done");
    }

private:
    std::string app_name;

    bool processed;
    std::vector<error_t> errors;
};

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& name_prefix)
{
    for(const auto& metrics_init : conf::counter_metrics_names) {
        const auto& name = metrics_init.first;
        const auto& is_aggregate = metrics_init.second;

        common_counters.emplace(name,
            counter_metric_t{
                is_aggregate,
                ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.{}", name_prefix, name)),
                0 });
    }
}

worker_metrics_t::worker_metrics_t(context_t& ctx, const std::string& app_name, const std::string& id) :
    worker_metrics_t(ctx, cocaine::format("{}.isolate.{}", app_name, id))
{}

metrics_retriever_t::self_metrics_t::self_metrics_t(context_t& ctx, const std::string& name) :
   uuid_requested{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.uuid_requested", name))},
   uuid_recieved{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.uuid_recieved", name))},
   requests_send{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.requests", name))},
   responses_received{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.responses", name))},
   receive_errors{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.recieve.errors", name))},
   posmortem_queue_size{ctx.metrics_hub().counter<std::uint64_t>(cocaine::format("{}.postmortem.queue.size", name))}
{}

metrics_retriever_t::metrics_retriever_t(
    context_t& ctx,
    const std::string& name, // app name
    std::shared_ptr<api::isolate_t> isolate,
    synchronized<engine_t::pool_type>& pool,
    asio::io_service& loop,
    const std::uint64_t poll_interval) :
        context(ctx),
        metrics_poll_timer(loop),
        isolate(std::move(isolate)),
        pool(pool),
        log(ctx.log(format("{}/workers_metrics", name))),
        self_metrics(ctx, "node.isolate.poll.metrics"),
        app_name(name),
        poll_interval(poll_interval),
        app_aggregate_metrics(ctx, cocaine::format("{}.isolate", name))
{
    COCAINE_LOG_INFO(log, "worker metrics retriever has been initialized");
}

auto
metrics_retriever_t::ignite_poll() -> void {
    metrics_poll_timer.expires_from_now(poll_interval);
    metrics_poll_timer.async_wait(std::bind(&metrics_retriever_t::poll_metrics, shared_from_this(), ph::_1));
}

auto
metrics_retriever_t::add_post_mortem(const std::string& id) -> void {
    if (purgatory->size() >= conf::PURGATORY_QUEUE_BOUND) {
        // on high despawn rates stat of some unlucky workers will be lost
        COCAINE_LOG_INFO(log, "worker metrics retriever: dead worker stat will be discarded for {}", id);
        return;
    }

    purgatory.apply([&](purgatory_pot_type& pot) {
        pot.emplace(id);
        self_metrics.posmortem_queue_size->store(pot.size());
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

    DBG_DUMP_UUIDS(std::cerr, "metrics.pool", alive_uuids);

#ifdef ISOMETRICS_DEBUG
    purgatory.apply([&](const purgatory_pot_type& pot) {
        DBG_DUMP_UUIDS(std::cerr, "purgatory", pot);
    });
#endif

    dynamic_t::array_t query_array;
    query_array.reserve(alive_uuids.size() + purgatory->size());

    // Note: it is promised by devs that active list should be quite
    // small ~ hundreds of workers, so it seems reasonable to pay a little for
    // sorting here, but code should be redisigned if average alive count
    // will increase significantly
    boost::sort(alive_uuids);

    purgatory.apply([&](purgatory_pot_type& pot) {
        boost::set_union(alive_uuids, pot, std::back_inserter(query_array));
        pot.clear();
        self_metrics.posmortem_queue_size->store(0);
    });

    DBG_DUMP_UUIDS(std::cerr, "query array", query_array);

    dynamic_t::object_t query;
    query["uuids"] = query_array;
    isolate->metrics(query, std::make_shared<metrics_handle_t>(shared_from_this()));

    // At this point query is posted and we have gathered uuids of available
    // (alived, pooled) workers and dead recently workers, so we can clear
    // stat_table out of garbage `neither alive nor dead` uuids
    stats_table_type preserved_metrics;
    preserved_metrics.reserve(metrics->size());

    metrics.apply([&](stats_table_type& table) {
        for(const auto to_preserve : query_array) {
            const auto it = table.find(to_preserve.as_string());

            if (it != std::end(table)) {
                preserved_metrics.emplace(std::move(*it));
            }
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

    dbg("metrics_handle_t:::on_data");
    dbg("[response] get json: " << boost::lexical_cast<std::string>(data) << '\n');

    COCAINE_LOG_DEBUG(parent->log, "processing isolation metrics response");

    response_processor_t processor(parent->app_name);

    // should not harm performance, as this handler would be called from same
    // poll loop, within same thread on each poll iteration
    const auto processed_count = parent->metrics.apply([&](metrics_retriever_t::stats_table_type& table) {

        // fill workers current `slice` state of metrics table
        const auto processed_count = processor(parent->context, data, table);

        // update application-wide aggregate of metrics
        parent->app_aggregate_metrics = boost::accumulate( table | map_values, metrics_aggregate_proxy_t());

        return processed_count;
    });

    parent->self_metrics.uuid_recieved->fetch_add(processed_count);
    parent->self_metrics.responses_received->fetch_add(1);

    if (processor.has_errors()) {
            const auto& errors = processor.errors_ref();
            auto break_counter = int{};

            for(const auto& e : errors) {
                if (++break_counter > conf::SHOW_ERRORS_LIMIT) {
                    break;
                }
                COCAINE_LOG_DEBUG(parent->log, "isolation metrics got an error {} {}", e.code, e.message);
            }
    }
}

auto
metrics_retriever_t::metrics_handle_t::on_error(const std::error_code& error, const std::string& what) -> void {
    assert(parent);
    dbg("metrics_handle_t::on_error: " << what);

    // TODO: start crying?
    COCAINE_LOG_WARNING(parent->log, "worker metrics recieve error {}:{}", error, what);
    parent->self_metrics.receive_errors->fetch_add(1);
}

} // namespace node
} // namespace service
} // namespace detail
} // namespace cocaine
