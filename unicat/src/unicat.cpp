#include <functional>
#include <memory>
#include <map>
#include <type_traits>
#include <tuple>
#include <vector>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>
#include <blackhole/wrapper.hpp>

#include <cocaine/api/authentication.hpp>
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

#if 0
#include <iostream>
#define dbg(msg) std::cerr << msg << '\n'
#else
#define dbg(msg)
#endif

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

    template<typename Deferred>
    auto
    abort_deferred(std::shared_ptr<cu::backend_t>& backend, Deferred&& deferred, const std::string& log_message, const std::string& deferred_message) -> void {
        auto log = backend->logger();
        backend.reset();
        COCAINE_LOG_WARNING(log, "{} {}", log_message, deferred_message);
        deferred.set_exception(std::make_exception_ptr(
            error_t(error::permission_denied, "'{}' '{}'", log_message, deferred_message)));
    }

    template<typename R>
    auto
    unpack(const unicorn::versioned_value_t value) -> std::tuple<R, cu::version_t> {
        dbg("value ver: " << value.version() << " contains: " << (value.exists() ? boost::lexical_cast<std::string>(value.value()) : "none"));

        if (value.exists() && value.value().convertible_to<R>()) {
            return std::make_tuple(value.value().to<R>(), value.version());
        }

        // Such error code make sense only in unicat context, should be less
        // specific in general case.
        throw std::system_error(make_error_code(error::invalid_acl_framing));
    }

    template<typename Completions>
    auto wait_all(Completions&& completions) -> std::vector<std::exception_ptr> {
        std::vector<std::exception_ptr> errors;
        for(auto& fut : completions) {
            try {
                fut.get();
            } catch(...) {
                errors.push_back(std::current_exception());
            }
        }
        return errors;
    }
} // detail

namespace dbg {
    auto
    log_requested_entities(std::shared_ptr<logging::logger_t> log, const std::vector<cu::entity_type>& entities)
        -> void
    {
        for(const auto& ent : entities) {
            const auto& scheme = std::get<0>(ent);
            const auto& service = std::get<1>(ent);
            const auto& entity = std::get<2>(ent);

            COCAINE_LOG_DEBUG(log, "alter slot for {}::{}::{}",
                scheme, service && service->empty() == false ? *service : detail::DEFAULT_SERVICE_NAME,
                entity);
        }
    }
} // dbg

struct on_write_t : unicat::async::write_handler_t {
    std::shared_ptr<std::promise<void>> completion;

    on_write_t(std::shared_ptr<std::promise<void>> completion) :
        completion(completion)
    {
        dbg("on_write_t");
    }

    ~on_write_t() {
        dbg("~on_write_t");
    }

    virtual auto on_write(std::future<void> fut) -> void override {
        on_done(std::move(fut));
    }

    virtual auto on_write(std::future<api::unicorn_t::response::put> fut) -> void override {
        on_done(std::move(fut));
    }

private:
    template<typename R>
    auto on_done(std::future<R> fut) -> void {
        try {
            fut.get();
            completion->set_value();
        } catch(...) {
            // Can also throw!
            completion->set_exception(std::current_exception());
        }
    }
};

template<typename Event>
struct on_read_t : public unicat::async::read_handler_t {
    std::shared_ptr<cu::backend_t> backend;
    const std::string entity;
    const std::shared_ptr<auth::identity_t> identity;
    auth::alter_data_t alter_data;
    std::shared_ptr<std::promise<void>> promise;

    on_read_t(
        std::shared_ptr<cu::backend_t> backend,
        const std::string& entity,
        const std::shared_ptr<auth::identity_t>& identity,
        auth::alter_data_t alter_data,
        std::shared_ptr<std::promise<void>> promise) :
            backend(std::move(backend)),
            entity(entity),
            identity(identity),
            alter_data(std::move(alter_data)),
            promise(std::move(promise))
    {
        dbg("on_read_t()");
    }

    ~on_read_t() {
        dbg("~on_read_t()");
    }

    auto on_read(std::future<unicorn::versioned_value_t> fut) -> void override {
        try {
            auto data = fut.get();

            auto meta = auth::metainfo_t{};
            auto version = cu::version_t{};

            std::tie(meta, version) = detail::unpack<auth::metainfo_t>(data);
            on_read(meta, version);
        } catch(...) {
            on_exception(std::current_exception());
        }
    }

    auto on_read(std::future<auth::metainfo_t> fut) -> void override {
        try {
            on_read(fut.get());
        } catch(...) {
            on_exception(std::current_exception());
        }
    }

private:
    auto on_read(auth::metainfo_t metainfo, const cu::version_t version = cocaine::unicorn::not_existing_version) -> void {
        dbg("on_read metainfo (before alter):\n" << metainfo);
        auth::alter<Event>(metainfo, alter_data);
        dbg("on_read metainfo (after alter):\n" << metainfo);

        auto backend_copy = backend;
        auto promise_copy = promise;
        auto entity_copy = entity;

        auto on_verify = cu::async::verify_handler_t{
            identity,
            [=] (std::error_code ec) mutable -> void {
                dbg("write verify error_code => " << ec);
                if (ec) {
                    return detail::abort_deferred(backend_copy, *promise_copy,
                        "failed to complete 'write' operation", "Permission denied");
                }

                auto on_write = std::make_shared<on_write_t>(promise_copy);
                backend_copy->async_write_metainfo(entity_copy, version, metainfo, std::move(on_write));
            }
        };

        backend_copy->async_verify_write(entity, std::move(on_verify));
    }

    auto on_exception(std::exception_ptr eptr) -> void {
        dbg("on_read::on_exception for entity " << entity);
        backend.reset();
        promise->set_exception(std::move(eptr));
    }
};

template<typename Event>
struct alter_slot_t :
    public io::basic_slot<Event>
{
    using tuple_type = typename io::basic_slot<Event>::tuple_type;
    using upstream_type = typename io::basic_slot<Event>::upstream_type;
    using result_type = typename io::basic_slot<Event>::result_type;

    using protocol = typename io::aux::protocol_impl<typename io::event_traits<Event>::upstream_type>::type;

    context_t& context;
    std::shared_ptr<logging::logger_t> log;

    alter_slot_t(context_t& context, const std::string& name) :
        context(context),
        log(context.log("audit", {{"service", name}}))
    {}

    auto
    operator()(const std::vector<hpack::header_t>& headers, tuple_type&& args, upstream_type&& upstream)
        -> result_type
    try {
        const auto& entities = std::get<0>(args);
        const auto& cids = std::get<1>(args);
        const auto& uids = std::get<2>(args);
        const auto& flags = std::get<3>(args);

        // dbg::log_requested_entities(log, entities);

        COCAINE_LOG_INFO(log, "alter slot with cids {} and uids {} set flags {}", cids, uids, flags);

        const auto alter_data = auth::alter_data_t{cids, uids, flags};

        // Store backend for later release in those thread, it helps to avoid
        // authorization structure (within backend) to be deleted inside self
        // executer thread due race conditions on async function exception:
        // smart pointer to backend is held by async callback and on exception
        // it was last owner of pointer, so backend was deleted after exit from
        // callback thread (and in callback thread), but callback thread itself
        // was owned by authorization which in turn was a callback holder, so it
        // lead to 'self join exception' in thread deletion.
        std::vector<std::shared_ptr<cu::backend_t>> basket;
        std::vector<std::future<void>> completions;

        for(const auto& it: detail::separate_by_scheme(entities)) {
            const auto& scheme = it.first;
            const auto& services = it.second;

            for(const auto& srv : services) {
                const auto& name = srv.first;
                const auto& entities = srv.second;

                COCAINE_LOG_INFO(log, "alter metainfo for scheme {} and service {}",
                    cu::scheme_to_string(scheme), name);
#if 1
                auto auth = api::authentication(context, "core", name);
                const auto identity = std::make_shared<auth::identity_t>(auth->identify(headers));
#else // for local debug
                const auto identity = std::make_shared<auth::identity_t>(auth::identity_t::builder_t()
                    .cids({1,13}).uids({4,6}).build());
#endif

                auto backend = cu::fabric::make_backend(scheme,
                    cu::backend_t::options_t{context, name, context.log("audit", {{"service", name}})}
                );

                basket.push_back(backend);

                for(const auto entity : entities) {
                    auto promise = std::make_shared<std::promise<void>>();
                    completions.push_back(promise->get_future());

                    auto backend_copy = backend;
                    auto promise_copy = promise;

                    auto on_verify = cu::async::verify_handler_t{
                        identity,
                        [=] (std::error_code ec) mutable -> void {
                            dbg("read verify with code " << ec);
                            if (ec) {
                                return detail::abort_deferred(backend_copy, *promise_copy,
                                    "failed to complete 'read' operation", "Permission denied");
                            }

                            auto on_read = std::make_shared<on_read_t<Event>>(backend_copy, entity, identity, alter_data, promise_copy);
                            backend_copy->async_read_metainfo(entity, std::move(on_read));
                        }
                    };

                    backend_copy->async_verify_read(entity, std::move(on_verify));
                } // for entities
            } // for services
        } // for schemes

        COCAINE_LOG_DEBUG(log, "waiting for async completions...");
        const auto exceptions = detail::wait_all(completions);
        if (exceptions.empty() == false) {
            dbg("async exceptions, count " << exceptions.size());
            std::rethrow_exception(exceptions.front());
        }

        upstream.template send<typename protocol::value>();
        COCAINE_LOG_DEBUG(log, "altering completed");
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
};

struct bind {
    template<typename Event, typename Self, typename... Args>
    static
    auto on_alter(Self&& self, Args&&... args) -> void {
        self.template on<Event>(std::make_shared<alter_slot_t<Event>>(std::forward<Args>(args)...));
    }
};

unicat_t::unicat_t(context_t& context, asio::io_service& asio, const std::string& service_name, const dynamic_t& args) :
    service_t(context, asio, service_name, args),
    dispatch<io::unicat_tag>(service_name)
{
    bind::on_alter<io::unicat::grant>(*this, context, service_name);
    bind::on_alter<io::unicat::revoke>(*this, context, service_name);
}

}  // namespace service
}  // namespace cocaine
