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

#include "backends/backend.hpp"
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

    template<typename Deferred, typename Log>
    auto
    abort_deferred(Deferred defered, Log&& log, const std::string& log_message, const std::string& deferred_message) -> void {
        // TODO: Refactor
    }

    template<typename R>
    auto
    unpack(const std::string& blob) -> R {
        R result;
        msgpack::unpacked unpacked;

        msgpack::unpack(&unpacked, blob.data(), blob.size());
        io::type_traits<R>::unpack(unpacked.get(), result);
        return result;
    }

    template<typename R>
    auto
    unpack(const unicorn::versioned_value_t value) -> R {
        R result;
        if (value.exists()) {
            if (!value.value().convertible_to<R>()) {
                // Such error code make sense only in unicat context, should be less
                // specific in general case.
                throw std::system_error(make_error_code(error::invalid_acl_framing));
            }

            result = value.value().to<R>();
        }

        return result;
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

    struct on_write_t : unicat::async::write_handler_t {
        std::shared_ptr<std::promise<void>> completion;

        on_write_t(std::shared_ptr<std::promise<void>> completion) :
            completion(completion)
        {}

        virtual auto on_write(std::future<void>) -> void override {
            on_done();
        }

        virtual auto on_write(std::future<api::unicorn_t::response::put>) -> void {
            on_done();
        }

    private:
        auto on_done() -> void {
            // TODO: better logging
            // TODO: signal via deferred?
            completion->set_value();
        }
    };

    struct on_read_t : unicat::async::read_handler_t {
        using deferred_type = deferred<typename result_of<Event>::type>;

        std::shared_ptr<cu::backend_t> backend;
        deferred_type result;
        const std::string entity;
        auth::alter_data_t alter_data;
        std::shared_ptr<std::promise<void>> promise;

        on_read_t(
            std::shared_ptr<cu::backend_t> backend,
            deferred_type result,
            const std::string& entity,
            auth::alter_data_t alter_data,
            std::shared_ptr<std::promise<void>> promise) :
                backend(std::move(backend)),
                result(std::move(result)),
                entity(entity),
                alter_data(std::move(alter_data)),
                promise(std::move(promise))
        {}

        auto on_read(std::future<std::string> fut) -> void override {
            on_read(detail::unpack<auth::metainfo_t>(fut.get()));
        }

        auto on_read(std::future<unicorn::versioned_value_t> fut) -> void override {
            on_read(detail::unpack<auth::metainfo_t>(fut.get()));
        }

    private:

        auto on_read(auth::metainfo_t metainfo) -> void {
            auth::alter<Event>(metainfo, alter_data);
            backend->async_verify_write(entity,
                [=] (std::error_code ec) {
                    if (ec) {
                        detail::abort_deferred(result, backend->logger(),
                            "failed to complete 'write' operation", "Permission denied");
                        return;
                    }

                    backend->async_write_metainfo(entity, metainfo, std::make_shared<on_write_t>(promise));
            });
        }
    };

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

        auth::alter_data_t alter_data{cids, uids, flags};

        deferred<typename result_of<Event>::type> deferred;

        COCAINE_LOG_INFO(log, "alter slot with cids {} and uids {} set flags {}",
            cids, uids, flags);

        // detail::log_requested_entities(log, entities);

        auto identity = cocaine::auth::identity_t::builder_t()
            .cids(cids)
            .uids(uids)
            .build();

        std::vector<std::future<void>> completions;

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

                auto promise = std::make_shared<std::promise<void>>();
                completions.push_back(promise->get_future());

                for(const auto entity : entities) {
                    backend->async_verify_read(entity, [=] (std::error_code ec) {
                        if (ec) {
                            detail::abort_deferred(deferred, backend->logger(),
                            "failed to complete 'read' operation", "Permission denied");
                            return;
                        }

                        backend->async_read_metainfo(entity,
                            std::make_shared<on_read_t>(backend, deferred, entity, alter_data, promise));
                    });
                }

            } // for services
        } // for schemes

        wait_all(completions);

        // TODO:
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

    template<typename Completions>
    auto wait_all(Completions&& completions) -> void {
        for(auto& fut : completions) {
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
