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
    license(api::authorization::storage(options.ctx_ref, options.name))
{}

auto
storage_backend_t::read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t>
{
    BOOST_ASSERT(backend);
    return backend->get<auth::metainfo_t>(detail::ACL_COLLECTION, entity);
}

auto
storage_backend_t::write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void>
{
    BOOST_ASSERT(backend);
    return backend->put<auth::metainfo_t>(detail::ACL_COLLECTION, entity, meta, detail::COLLECTION_ACLS_TAGS);
}

auto
storage_backend_t::check_read(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
    return async_verify<io::storage::read>(
        license, detail::ACL_COLLECTION, entity, get_options().identity_ref);
}


auto
storage_backend_t::check_write(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
    return async_verify<io::storage::write>(
        license, detail::ACL_COLLECTION, entity, get_options().identity_ref);
}

}
}
