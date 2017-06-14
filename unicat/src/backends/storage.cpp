#include <iostream>
#include <cassert>

#include "storage.hpp"

namespace cocaine { namespace unicat {

namespace detail {
    // TODO: make global in core?
    const auto ACL_COLLECTION = std::string{".collection-acls"};
}

storage_backend_t::storage_backend_t(const options_t& options) :
    backend_t(options)
{
    storage = api::storage(options.ctx_ref, options.name);
}

auto
storage_backend_t::read_metainfo(const std::string& entity) -> auth::metainfo_t
{
    assert(storage);

    // const auto future = storage->get<auth::metainfo_t>(detail::ACL_COLLECTION, entity);
    std::cerr << "storage::read_metainfo\n";
    return auth::metainfo_t{};
}

auto
storage_backend_t::write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> void
{
    assert(storage);

    std::cerr << "storage::write_metainfo\n";
}

}
}
