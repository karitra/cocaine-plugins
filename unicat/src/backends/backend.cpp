
#include "cocaine/idl/unicat.hpp"

#include "backend.hpp"

namespace cocaine { namespace unicat {

namespace detail {
template<typename Method>
auto
modify_rights(auth::metainfo_t&, const auth::identity_t&, const auth::flags_t) -> void;

template<>
auto
modify_rights<io::unicat::grant>(
    auth::metainfo_t& meta, const auth::identity_t& identity, const auth::flags_t flags) -> void
{
}

template<>
auto
modify_rights<io::unicat::revoke>(
    auth::metainfo_t& meta, const auth::identity_t& identity, const auth::flags_t flags) -> void
{
}

} // namespace detail

backend_t::backend_t(const options_t& options) :
    options(options)
{}

template<typename Method>
auto
backend_t::alter(const std::string& entity, const auth::identity_t& identity, const auth::flags_t flags) -> void {
    auto meta = read_metainfo(entity);
    detail::modify_rights<Method>(meta, identity, flags);
    write_metainfo(entity, meta);
}

template
auto
backend_t::alter<cocaine::io::unicat::revoke>(
    const std::string&, const auth::identity_t&, const auth::flags_t) -> void;

template
auto
backend_t::alter<cocaine::io::unicat::grant>(
    const std::string&, const auth::identity_t&, const auth::flags_t) -> void;

auto
backend_t::view(const std::string& entity) -> auth::metainfo_t {
    return read_metainfo(entity);
}

auto
backend_t::reset(const std::string& entity) -> void
{
    // TODO: not emplemented
}

auto
backend_t::logger() -> std::shared_ptr<logging::logger_t> {
    return get_options().log;
}

auto
backend_t::get_options() -> options_t {
    return options;
}

}
}
