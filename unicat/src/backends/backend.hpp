#pragma once

#include <memory>
#include <future>
#include <tuple>

#include <system_error>

#include <boost/thread.hpp>
#include <boost/thread/scoped_thread.hpp>

#include <cocaine/context.hpp>
#include <cocaine/auth/uid.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/rpc/session.hpp>

#include "cocaine/detail/forwards.hpp"
#include "cocaine/auth/metainfo.hpp"

namespace cocaine { namespace unicat {

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

    // TODO: crudly inefficient implementation as it creates scooped thread on every
    //       < bit, entity > pair has been checked, should be redesign ASAP
    //       (probably will affect 'core' authorization stuff)
    template<typename Event>
    auto verify_rights(const std::string& entity) -> void;

    virtual auto read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t> = 0;
    virtual auto write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void> = 0;
private:
    virtual auto check_write(const std::string& entity) -> std::future<bool> = 0;
    virtual auto check_read(const std::string& entity) -> std::future<bool> = 0;
private:
    options_t options;
};

//
// TODO: All those pile of garbage just 'cause gcc seems fail to support
// variadic template expansion in lambda.
//
template<typename Event, typename License, typename... Args>
struct verify_thread_type {
    verify_thread_type(License&& license, std::shared_ptr<std::promise<bool>> promise, Args&&... args) :
        license(license),
        promise(std::move(promise)),
        args(std::forward<Args>(args)...)
    {}

    auto operator()() -> void {
        call(args);
    }
private:
    template<int... Is>
    struct seq {};

    template<int N, int... Is>
    struct gen_seq : gen_seq<N-1, N-1, Is...> {};

    template<int... Is>
    struct gen_seq<0, Is...> : seq<Is...> {};

    auto
    call(std::tuple<Args...>& tup) -> void {
        call(tup, gen_seq<sizeof...(Args)>{});
    }

    template<int... Is>
    auto
    call(std::tuple<Args...>& tup, seq<Is...>) -> void {
        // TODO: is it really thread safe?
        license-> template verify<Event>(std::get<Is>(tup)..., [=] (std::error_code ec) -> void {
            promise->set_value(ec ? false : true);
        });
    }

private:
    License license;
    std::shared_ptr<std::promise<bool>> promise;
    std::tuple<Args...> args;
};

//
// TODO: not tested at all, born ugly!
//
template<typename Event, typename License, typename... Args>
auto
async_verify(License&& license, Args&&... args) -> std::future<bool>
{
    const auto promise = std::make_shared<std::promise<bool>>();
    auto fut = promise->get_future();

    auto body = verify_thread_type<Event, License, Args...>{license, promise, std::forward<Args>(args)...};

    auto scoped = boost::scoped_thread<>{
        boost::thread{
            verify_thread_type<Event, License, Args...>{license, promise, std::forward<Args>(args)...}
        }
    };

    return fut;
}

}
}
