#include <functional>
#include <type_traits>

#include <memory>
#include <map>
#include <vector>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>
#include <blackhole/wrapper.hpp>

#include <cocaine/errors.hpp>
#include <cocaine/format.hpp>
#include <cocaine/format/vector.hpp>
#include <cocaine/logging.hpp>

#include <cocaine/traits/optional.hpp>

#include "cocaine/service/unicat.hpp"
#include "cocaine/idl/unicat.hpp"

#include "cocaine/detail/forwards.hpp"
#include "cocaine/context.hpp"

#include "backends/fabric.hpp"

namespace cocaine {
namespace service {

namespace cu = cocaine::unicat;

namespace detail {

    const auto DEFAULT_SERVICE_NAME = std::string{"core"};

    using entities_by_scheme_type = std::map<
        cu::scheme_t,
        std::map<
            std::string,                // service, set by user or defaults to 'scheme'
            std::vector<std::string>    // entities
        >>;

    auto
    separate_by_scheme(const std::vector<cu::entity_type>& entities) -> entities_by_scheme_type
    {
        std::string scheme;
        std::string entity;
        boost::optional<std::string> service;

        entities_by_scheme_type separated;

        for(const auto& el : entities) {
            std::tie(scheme, service, entity) = el;
            separated[cu::scheme_from_string(scheme)]
                     [service && service->empty() == false ? *service : DEFAULT_SERVICE_NAME]
                     .push_back(entity);
        }

        return separated;
    }

    // for debug only
    auto
    log_requested_entities(std::shared_ptr<logging::logger_t> log, const std::vector<cu::entity_type>& entities)
        -> void
    {
        for(const auto& ent : entities) {
            const auto& scheme = std::get<0>(ent);
            const auto& service = std::get<1>(ent);
            const auto& entity = std::get<2>(ent);

            COCAINE_LOG_DEBUG(log, "alter slot for {}::{}::{}",
                scheme, service && service->empty() == false ? *service : DEFAULT_SERVICE_NAME,
                entity);
        }
    }
} // detail

template<typename Event>
struct alter_slot_t :
    public io::basic_slot<Event>
{
    using tuple_type = typename io::basic_slot<Event>::tuple_type;
    using upstream_type = typename io::basic_slot<Event>::upstream_type;
    using result_type = typename io::basic_slot<Event>::result_type;
    using protocol = typename io::aux::protocol_impl<typename io::event_traits<Event>::upstream_type>::type;

    context_t& ctx;
    std::shared_ptr<logging::logger_t> log;

    alter_slot_t(context_t& ctx, const std::string& name) :
        ctx(ctx),
        log(ctx.log("audit", {{"service", name}}))
    {}

    virtual
    auto
    operator()(const std::vector<hpack::header_t>& headers, tuple_type&& args, upstream_type&& upstream)
        -> result_type
    try {
        const auto& entities = std::get<0>(args);
        const auto& cids = std::get<1>(args);
        const auto& uids = std::get<2>(args);
        const auto& flags = std::get<3>(args);

        COCAINE_LOG_INFO(log, "alter slot with cids {} and uids {} set flags {}",
            cids, uids, flags);

        // detail::log_requested_entities(log, entities);

        const auto identity = cocaine::auth::identity_t::builder_t()
            .cids(cids)
            .uids(uids)
            .build();

        for(const auto& it: detail::separate_by_scheme(entities)) {
            const auto& scheme = it.first;
            const auto& services = it.second;

            for(const auto& srv : services) {
                const auto& name = srv.first;
                const auto& entities = srv.second;

                COCAINE_LOG_INFO(log, "alter metainfo for scheme {} and service {}",
                    cu::scheme_to_string(scheme), name);

                auto backend = cu::fabric::make_backend(scheme,
                    cu::backend_t::options_t{ctx, name, identity, log});

                using read_future_type = std::future<auth::metainfo_t>;
                using named_future_type = std::pair<std::string, read_future_type>;
                std::vector<named_future_type> read_futures;

                for(const auto& entity : entities) {
                    COCAINE_LOG_DEBUG(log, "reading metainfo for '{}'", entity);

                    // TODO: better interface to ensure verification
                    backend->template verify_rights<Event>(entity);
                    read_futures.emplace_back(entity, backend->read_metainfo(entity));
                }

                std::vector<std::future<void>> write_futures;
                for(auto& result : read_futures) {
                    auto& entity = result.first;
                    auto& rd_fut = result.second;

                    auto metainfo = rd_fut.get();
                    cocaine::auth::alter<Event>(metainfo, cids, uids, flags);

                    COCAINE_LOG_DEBUG(log, "writing metainfo back for '{}'", entity);
                    write_futures.emplace_back(backend->write_metainfo(entity, metainfo));
                }

                wait_all(std::move(write_futures));
            } // for services
        } // for schemes

        upstream.template send<typename protocol::value>();
        COCAINE_LOG_DEBUG(log, "completed altering");
        return boost::none;
    } catch(const std::system_error& err) {
       COCAINE_LOG_WARNING(log, "failed to complete '{}' operation", Event::alias(), blackhole::attribute_list{
           {"code", err.code().value()},
           {"error", error::to_string(err)},
       });

       upstream.template send<typename protocol::error>(err.code(), error::to_string(err));
       return boost::none;
   } catch(const std::exception& err) {
        COCAINE_LOG_WARNING(log, "failed to complete '{}' operation", Event::alias(), blackhole::attribute_list{
            {"error", err.what()},
        });
        upstream.template send<typename protocol::error>(error::uncaught_error, err.what());
        return boost::none;
    }

    template<typename Container>
    auto
    wait_all(Container&& container) -> void {
        for(auto& fut : container) {
            fut.get();
        }
    }
};

struct bind {
    template<typename Event, typename Self>
    static
    auto on_alter(Self&& self, context_t& context, const std::string& name) -> void {
        self.template on<Event>(std::make_shared<alter_slot_t<Event>>(context, name));
    }
};

unicat_t::unicat_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args) :
    service_t(context, asio, name, args),
    dispatch<io::unicat_tag>(name)
{
    bind::on_alter<io::unicat::grant>(*this, context, name);
    bind::on_alter<io::unicat::revoke>(*this, context, name);
}

}  // namespace service
}  // namespace cocaine
