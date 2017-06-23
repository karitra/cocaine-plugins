#include <cocaine/errors.hpp>

#include "cocaine/idl/unicat.hpp"

#include "backend.hpp"

namespace cocaine { namespace unicat {

backend_t::backend_t(const options_t& options) :
    options(options)
{}

auto
backend_t::logger() -> std::shared_ptr<logging::logger_t> {
    return get_options().log;
}

auto
backend_t::get_options() -> options_t {
    return options;
}

template<>
auto
backend_t::verify_rights<io::unicat::revoke>(const std::string& entity) -> void
{
    const auto rd = check_read(entity).get();
    const auto wr = check_write(entity).get();

    if (rd == false || wr == false) {
        throw cocaine::error::error_t(error::security_errors::permission_denied, "permission denied for '{}'", entity);
    }
}

template<>
auto
backend_t::verify_rights<io::unicat::grant>(const std::string& entity) -> void
{
    return verify_rights<io::unicat::revoke>(entity);
}

template
auto
backend_t::verify_rights<io::unicat::revoke>(const std::string& entity) -> void;

template
auto
backend_t::verify_rights<io::unicat::grant>(const std::string& entity) -> void;

}
}
