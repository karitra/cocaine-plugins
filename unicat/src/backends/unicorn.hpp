#pragma once

#include <memory>
#include <future>

#include <cocaine/api/unicorn.hpp>
#include <cocaine/api/authorization/unicorn.hpp>

#include "backend.hpp"

namespace cocaine { namespace unicat {

class unicorn_backend_t : public backend_t {
    api::unicorn_ptr backend;
    std::shared_ptr<api::authorization::unicorn_t> license;
public:
    explicit unicorn_backend_t(const options_t& options);

    auto read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t> override;
    auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void> override;
private:
    auto check_write(const std::string& entity) -> std::future<bool> override;
    auto check_read(const std::string& entity) -> std::future<bool> override;
};

}
}
