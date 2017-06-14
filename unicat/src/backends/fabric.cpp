#include <memory>

#include <cocaine/idl/unicat.hpp>
#include <cocaine/errors.hpp>

#include "fabric.hpp"

#include "storage.hpp"
#include "unicorn.hpp"

namespace cocaine { namespace unicat {

const std::unordered_map<std::string, scheme_t> scheme_names_mapping =
{
    {"storage", scheme_t::storage},
    {"unicorn", scheme_t::unicorn}
};

scheme_t scheme_from_string(const std::string& scheme) {
    return scheme_names_mapping.at(scheme);
}

auto
fabric_t::make_backend(const scheme_t scheme, const backend_t::options_t& options) -> std::unique_ptr<backend_t> {
    using cocaine::error::repository_errors;
    switch (scheme) {
        case scheme_t::storage: return std::unique_ptr<backend_t>{new storage_backend_t(options)};
        case scheme_t::unicorn: return std::unique_ptr<backend_t>{new unicorn_backend_t(options)};
        default: break;
    };
    throw cocaine::error::error_t(repository_errors::component_not_found, "{}", "unknown auth backend scheme");
}

template<typename Method>
auto
fabric_t::dispatcher_t<Method>::operator()(
    const std::string& entity, const auth::identity_t& identity, const auth::flags_t& rights) const -> void
{
    return backend->alter<Method>(entity, identity, rights);
}

template class fabric_t::dispatcher_t<io::unicat::revoke>;
template class fabric_t::dispatcher_t<io::unicat::grant>;

}
}
