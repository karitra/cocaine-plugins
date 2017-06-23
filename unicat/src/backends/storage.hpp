#pragma once

#include <memory>
#include <future>

#include <cocaine/api/storage.hpp>
#include <cocaine/api/authorization/storage.hpp>

#include "backend.hpp"

namespace cocaine { namespace unicat {

class storage_backend_t : public backend_t {
    api::storage_ptr backend;
    std::shared_ptr<api::authorization::storage_t> license;
public:
    explicit storage_backend_t(const options_t& options);

    auto read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t> override;
    auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void> override;

    auto check_read(const std::string& entity) -> std::future<bool> override;
    auto check_write(const std::string& entity) -> std::future<bool> override;
};

}
}
