#pragma once

#include <memory>
#include <vector>
#include <unordered_map>

#include "backend.hpp"

namespace cocaine { namespace unicat {

enum class scheme_t : unsigned {
    storage,
    unicorn,
    schemes_count
};

scheme_t scheme_from_string(const std::string& scheme);

struct fabric_t {

    template<typename Method>
    struct dispatcher_t {
        auto
        operator()(const std::string& entity, const auth::identity_t&, const auth::flags_t&) const -> void;

        std::unique_ptr<backend_t> backend;
    };

    static
    auto
    make_backend(const scheme_t scheme, const backend_t::options_t& options) -> std::unique_ptr<backend_t>;

    template<typename Method>
    static
    auto
    make_dispatcher(const scheme_t scheme, const backend_t::options_t& options) -> dispatcher_t<Method> {
        return dispatcher_t<Method>{ make_backend(scheme, options) };
    }

    fabric_t() = delete;
    fabric_t(const fabric_t&) = delete;
    void operator=(const fabric_t&) = delete;
};

}
}
