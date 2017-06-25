#pragma once

#include <functional>
#include <future>
#include <memory>
#include <tuple>
#include <utility>

#include <system_error>

#include <boost/thread.hpp>
#include <boost/thread/scoped_thread.hpp>

#include <cocaine/context.hpp>
#include <cocaine/auth/uid.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/rpc/session.hpp>

#include <cocaine/api/unicorn.hpp>
#include <cocaine/unicorn/value.hpp>

#include "cocaine/detail/forwards.hpp"
#include "cocaine/auth/metainfo.hpp"

namespace cocaine { namespace unicat {

namespace async {

struct read_handler_t {
    virtual auto on_read(std::future<std::string>) -> void = 0;
    virtual auto on_read(std::future<unicorn::versioned_value_t>) -> void = 0;

    virtual ~read_handler_t() = default;
};

struct write_handler_t {
    virtual auto on_write(std::future<void>) -> void = 0;
    virtual auto on_write(std::future<api::unicorn_t::response::put>) -> void = 0;

    virtual ~write_handler_t() = default;
};

using verify_handler_t = std::function<void(std::error_code)>;

template<typename Event, typename Access, typename... Args>
auto
verify(Access&& access, verify_handler_t hnd, Args&&... args) -> void {
    return access.template verify<Event>(std::forward<Args>(args)..., std::move(hnd));
}
} // async

class backend_t {
public:
    ///
    /// Common 'backends' options comes here.
    ///
    struct options_t {
        context_t& ctx_ref;
        std::string name; // service name
        const auth::identity_t& identity_ref;
        std::shared_ptr<logging::logger_t> log;
    };

    explicit backend_t(const options_t& options);
    virtual ~backend_t() {}

    auto logger() -> std::shared_ptr<logging::logger_t>;
    auto get_options() -> options_t;

    virtual auto async_verify_read(const std::string& entity, async::verify_handler_t) -> void = 0;
    virtual auto async_verify_write(const std::string& entity, async::verify_handler_t) -> void = 0;

    virtual auto async_read_metainfo(const std::string& entity, std::shared_ptr<async::read_handler_t>) -> void = 0;
    virtual auto async_write_metainfo(const std::string& entity, const auth::metainfo_t& meta, std::shared_ptr<async::write_handler_t>) -> void = 0;
private:
    options_t options;
};

}
}
