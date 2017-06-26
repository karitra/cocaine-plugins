#include <cocaine/idl/storage.hpp>

#include <boost/assert.hpp>

#include "storage.hpp"

namespace cocaine { namespace unicat {

namespace detail {
    // TODO: make global in core?
    const auto ACL_COLLECTION = std::string{".collection-acls"};
    const auto COLLECTION_ACLS_TAGS = std::vector<std::string>{"storage-acls"};
}

storage_backend_t::storage_backend_t(const options_t& options) :
    backend_t(options),
    backend(api::storage(options.ctx_ref, options.name)),
    // TODO: incorrect backend name
    access(api::authorization::storage(options.ctx_ref, options.name))
{}

auto
storage_backend_t::async_verify_read(const std::string& entity, async::verify_handler_t hnd) -> void
{
    BOOST_ASSERT(access);
    return async::verify<io::storage::read>(
        *access, std::move(hnd), detail::ACL_COLLECTION, entity, get_options().identity_ref);
}
auto
storage_backend_t::async_verify_write(const std::string& entity, async::verify_handler_t hnd) -> void
{
    BOOST_ASSERT(access);
    return async::verify<io::storage::write>(
        *access, std::move(hnd), detail::ACL_COLLECTION, entity, get_options().identity_ref);
}

auto
storage_backend_t::async_read_metainfo(const std::string& entity, std::shared_ptr<async::read_handler_t> hnd) -> void
{
    BOOST_ASSERT(backend);
    return backend->get<auth::metainfo_t>(detail::ACL_COLLECTION, entity,
        [=] (std::future<auth::metainfo_t> fut) {
            hnd->on_read(std::move(fut));
        }
    );
}

auto
storage_backend_t::async_write_metainfo(const std::string& entity, const auth::metainfo_t& meta, std::shared_ptr<async::write_handler_t> hnd) -> void
{
    BOOST_ASSERT(backend);
    return backend->put(detail::ACL_COLLECTION, entity, meta, detail::COLLECTION_ACLS_TAGS,
        [=] (std::future<void> fut) {
            hnd->on_write(std::move(fut));
        }
    );
}

}
}
