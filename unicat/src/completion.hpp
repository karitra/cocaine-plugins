#pragma once

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include <blackhole/logger.hpp>
#include <blackhole/scope/holder.hpp>
#include <blackhole/wrapper.hpp>

#include <cocaine/format.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/locked_ptr.hpp>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/sliced.hpp>

#include "backend/fabric.hpp"

namespace cocaine { namespace unicat {

namespace detail {
    constexpr auto TAKE_EXCEPT_TRACE = size_t{10};
}

struct completion_t final {
    enum Opcode { Nop, ReadOp, WriteOp };

    url_t url;

    bool done;
    int error_code;
    Opcode opcode;
    std::exception_ptr eptr;

    explicit completion_t(const url_t);
    completion_t(const url_t, const int ec, const Opcode);
    completion_t(const url_t, std::exception_ptr);

    auto make_access_error() const -> std::string;
    auto make_exception_error() const -> std::string;

    auto has_error() const -> bool;
    auto has_error_code() const -> bool;
};

struct base_completion_state_t {
    virtual ~base_completion_state_t() = default;
    virtual auto set_completion(completion_t completion) -> void = 0;
};

template<typename Upstream, typename Protocol>
struct async_completion_state_t : public base_completion_state_t {
    using compl_state_type = std::vector<completion_t>;
    synchronized<compl_state_type> state;
    Upstream upstream;

    std::unique_ptr<logging::logger_t> log;

    explicit async_completion_state_t(Upstream upstream, std::unique_ptr<logging::logger_t> log) :
        upstream(std::move(upstream)),
        log(std::move(log))
    {}

    virtual ~async_completion_state_t() {
        auto count = state.synchronize()->size();
        COCAINE_LOG_INFO(log, "async completion with {} handlers", count);
        finalize();
    }

    auto set_completion(completion_t completion) -> void override {
        state.apply([=] (compl_state_type& state) {
            state.push_back(std::move(completion));
        });
    }

    auto finalize() -> void {
        using namespace boost::adaptors;

        auto errors_list = state.apply([&] (compl_state_type& state) {
            // TODO: make errors with <code, string> pairs
            std::vector<std::string> errors;

            for(auto& comp : state) {
                if (comp.has_error()) {
                    errors.push_back(comp.make_exception_error());
                } else if (comp.has_error_code()) {
                    errors.push_back(comp.make_access_error());
                }
            }

            return errors;
        });

        const auto count = state.synchronize()->size();

        if (errors_list.empty()) {
            COCAINE_LOG_INFO(log, "all {} completions has been done with success, report result to user", count);
            upstream.template send<typename Protocol::value>();
            return;
        }

        const auto to_take = std::min(count, detail::TAKE_EXCEPT_TRACE);
        COCAINE_LOG_WARNING(log, "there was {} exceptions, reporting first {} to client", errors_list.size(), to_take);

        upstream.template send<typename Protocol::error>(
            error::uncaught_error,
            boost::join(errors_list | sliced(0, to_take), ", "));
    }
};

}
}
