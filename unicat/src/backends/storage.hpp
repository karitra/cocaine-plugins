#pragma once

#include <memory>
#include <future>

#include <cocaine/api/storage.hpp>
#include <cocaine/api/authorization/storage.hpp>

#include "backend.hpp"

namespace cocaine { namespace unicat {

class storage_backend_t : public backend_t {
    api::storage_ptr backend;
    std::shared_ptr<api::authorization::storage_t> access;
public:
    explicit storage_backend_t(const options_t& options);

    auto async_verify_read(const std::string& entity, async::verify_handler_t) -> void override;
    auto async_verify_write(const std::string& entity, async::verify_handler_t) -> void override;

    auto async_read_metainfo(const std::string& entity, std::shared_ptr<async::read_handler_t>) -> void override;
    auto async_write_metainfo(const std::string& entity, const auth::metainfo_t& meta, std::shared_ptr<async::write_handler_t>) -> void override;
};

}
}
