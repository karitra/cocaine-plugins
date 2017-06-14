#pragma once

#include "backend.hpp"

namespace cocaine { namespace unicat {

class unicorn_backend_t : public backend_t {
public:
    using backend_t::backend_t;
private:
    virtual auto read_metainfo(const std::string& entity) -> auth::metainfo_t override;
    virtual auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> void override;
};

}
}
