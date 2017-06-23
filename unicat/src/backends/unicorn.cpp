#include <boost/assert.hpp>

#include "unicorn.hpp"

namespace cocaine { namespace unicat {

namespace detail {
    // TODO: make global in core?
    // const auto ACL_COLLECTION = std::string{".collection-acls"};
    // const auto COLLECTION_ACLS_TAGS = std::vector<std::string>{"storage-acls"};
}

unicorn_backend_t::unicorn_backend_t(const options_t& options) :
    backend_t(options),
    backend(api::unicorn(options.ctx_ref, options.name)),
    license(api::authorization::unicorn(options.ctx_ref, options.name))
{}

auto
unicorn_backend_t::read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t>
{
    BOOST_ASSERT(backend);
    // backend->
}

auto
unicorn_backend_t::write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void>
{
    BOOST_ASSERT(backend);
}

auto
unicorn_backend_t::check_write(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
}

auto
unicorn_backend_t::check_read(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
}


}
}
