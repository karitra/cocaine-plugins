#include <cocaine/format.hpp>
#include <cocaine/errors.hpp>

#include <boost/assert.hpp>

#include "cocaine/idl/unicorn.hpp"
#include "unicorn.hpp"

namespace cocaine { namespace unicat {

namespace detail {
    // TODO: make global in core?
    const auto ACL_NODE = std::string{".acls"};

    auto
    make_dynamic_from_meta(const auth::metainfo_t& metainfo) -> dynamic_t {
        return std::make_tuple(
            std::move(metainfo.c_perms),
            std::move(metainfo.u_perms));
    }
}

unicorn_backend_t::unicorn_backend_t(const options_t& options) :
    backend_t(options),
    backend(api::unicorn(options.ctx_ref, options.name)),
    // TODO: incorrect backend name
    access(api::authorization::unicorn(options.ctx_ref, options.name))
{}

auto
unicorn_backend_t::async_verify_read(const std::string& entity, async::verify_handler_t hnd) -> void
{
    BOOST_ASSERT(access);
    return async::verify<io::unicorn::get>(
        *access, std::move(hnd), cocaine::format("{}/{}", entity, detail::ACL_NODE), get_options().identity_ref);
}

auto
unicorn_backend_t::async_verify_write(const std::string& entity, async::verify_handler_t hnd) -> void
{
    BOOST_ASSERT(access);
    return async::verify<io::unicorn::put>(
        *access, std::move(hnd), cocaine::format("{}/{}", entity, detail::ACL_NODE), get_options().identity_ref);
}

auto
unicorn_backend_t::async_read_metainfo(const std::string& entity, std::shared_ptr<async::read_handler_t> hnd) -> void
{
    BOOST_ASSERT(backend);
    backend->get(
        [=] (std::future<unicorn::versioned_value_t> fut) {
            hnd->on_read(std::move(fut));
        },
        cocaine::format("{}/{}", entity, detail::ACL_NODE));
}

auto
unicorn_backend_t::async_write_metainfo(const std::string& entity, const auth::metainfo_t& meta, std::shared_ptr<async::write_handler_t> hnd) -> void
{
    BOOST_ASSERT(backend);
    backend->put(
        [=] (std::future<api::unicorn_t::response::put> fut) {
            hnd->on_write(std::move(fut));
        },
        cocaine::format("{}/{}", entity, detail::ACL_NODE),
        detail::make_dynamic_from_meta(meta),
        // TODO: make real object verson, if it exist!
        unicorn::version_t{});
}

}
}
