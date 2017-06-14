#pragma once

#include <cocaine/api/storage.hpp>

#include <iostream>

#include "backend.hpp"

namespace cocaine { namespace unicat {

class storage_backend_t : public backend_t {
public:
    explicit storage_backend_t(const options_t& options);
private:
    virtual auto read_metainfo(const std::string& entity) -> auth::metainfo_t override;
    virtual auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> void override;
private:
    api::storage_ptr storage;
};

}
}
