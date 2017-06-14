#include <iostream>

#include "unicorn.hpp"

namespace cocaine { namespace unicat {

auto unicorn_backend_t::read_metainfo(const std::string& entity) -> auth::metainfo_t
{
    std::cerr << "unicorn_backend_t::read_metainfo\n";
    return auth::metainfo_t{};
}

auto unicorn_backend_t::write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> void
{
    std::cerr << "unicorn_backend_t::write_metainfo\n";
}

}
}
