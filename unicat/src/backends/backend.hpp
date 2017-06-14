#pragma once

#include <memory>

#include <cocaine/context.hpp>
#include <cocaine/auth/uid.hpp>
#include <cocaine/logging.hpp>

#include "cocaine/detail/forwards.hpp"
#include "cocaine/auth/metainfo.hpp"

namespace cocaine { namespace unicat {

class backend_t {
public:
    struct options_t {
        context_t& ctx_ref;
        std::string name;
        std::shared_ptr<logging::logger_t> log;
    };

    explicit backend_t(const options_t& options);

    virtual ~backend_t() {}

    template<typename Method>
    auto alter(const std::string& entity, const auth::identity_t& identity, const auth::flags_t flags) -> void;

    // TODO: not implemented
    auto view(const std::string& entity) -> auth::metainfo_t;

    // TODO(1): not implemented
    // TODO(2): is it valid for unicorn to have empty acl, or should it be removed completely
    auto reset(const std::string& entity) -> void;

    auto logger() -> std::shared_ptr<logging::logger_t>;
    auto get_options() -> options_t;
private:
    virtual auto read_metainfo(const std::string& entity) -> auth::metainfo_t = 0;
    virtual auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> void = 0;
private:
    options_t options;
};

}
}
