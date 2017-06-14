#include <functional>
#include <type_traits>

#include <map>
#include <vector>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>
#include <blackhole/wrapper.hpp>

#include <cocaine/traits/optional.hpp>

#include "cocaine/service/unicat.hpp"
#include "cocaine/idl/unicat.hpp"

#include "cocaine/detail/forwards.hpp"
#include "cocaine/context.hpp"

#include "backends/fabric.hpp"
#include "detail/autobind.hpp"

namespace cocaine {
namespace service {

namespace cu = cocaine::unicat;

namespace detail {

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
                     [service ? *service : scheme].push_back(entity);
        }

        return separated;
    }
} // detail

struct unicat_t::impl_t {

    impl_t(context_t& ctx, const std::string& name) :
        ctx(ctx),
        log(ctx.log("audit", {{"service", name}}))
    {}

    template<typename Method>
    auto stroke(
        const std::vector<cu::entity_type>& entities,
        const std::vector<auth::cid_t>& cids,
        const std::vector<auth::uid_t>& uids,
        const auth::flags_t access_mask) -> void {

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

                const auto backend_options = cu::backend_t::options_t{ctx, name, log};
                const auto& disp = cu::fabric_t::make_dispatcher<Method>(scheme, backend_options);

                for(const auto& entity : entities) {
                    disp(entity, identity, access_mask);
                }
            } // for services
        } // for records
    } // stroke

    context_t &ctx;
    std::shared_ptr<logging::logger_t> log;
};

struct bind {
    template<typename Method, typename Self, typename ImplPtr>
    static
    auto
    on(Self&& self, ImplPtr&& impl) -> void {
        using namespace cocaine::unicat::detail;

        using pure_impl_type = typename std::decay<ImplPtr>::type;
        using base_impl_type = typename pure_impl_type::element_type;

        using size = boost::mpl::size<typename Method::argument_type>;
        using ph = typename generate<size::value>::placeholders;

        self->on<Method>(
            ph::bind(&base_impl_type::template stroke<Method>, std::forward<ImplPtr>(impl)));
    }
};

unicat_t::unicat_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args) :
    service_t(context, asio, name, args),
    dispatch<io::unicat_tag>(name),
    pimpl(new unicat_t::impl_t(context, name))
{
    bind::on<io::unicat::grant>(this, pimpl);
    bind::on<io::unicat::revoke>(this, pimpl);
}

}  // namespace service
}  // namespace cocaine
