#pragma once

#include <memory>
#include <future>

#include <cocaine/api/unicorn.hpp>
#include <cocaine/api/authorization/unicorn.hpp>

#include "backend.hpp"

namespace cocaine { namespace unicat {

class unicorn_backend_t : public backend_t {
    api::unicorn_ptr backend;
    std::shared_ptr<api::authorization::unicorn_t> access;
public:
    explicit unicorn_backend_t(const options_t& options);

    auto async_verify_read(const std::string& entity, async::verify_handler_t) -> void override;
    auto async_verify_write(const std::string& entity, async::verify_handler_t) -> void override;

    auto async_read_metainfo(const std::string& entity, std::shared_ptr<async::read_handler_t>) -> void override;
    auto async_write_metainfo(const std::string& entity, const auth::metainfo_t& meta, std::shared_ptr<async::write_handler_t>) -> void override;
};

}
}
