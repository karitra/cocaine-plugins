#include <cocaine/format.hpp>
#include <cocaine/errors.hpp>

#include <cocaine/dynamic.hpp>
#include <cocaine/dynamic/converters.hpp>

#include <boost/assert.hpp>

#include "cocaine/idl/unicorn.hpp"
#include "unicorn.hpp"

// Namespace section copy-pasted form uncorn plugin:
// unicorn/src/authorization/unicorn.cpp
namespace cocaine {

template<>
struct dynamic_converter<auth::metainfo_t> {
    using result_type = auth::metainfo_t;

    using underlying_type = std::tuple<
        std::map<auth::cid_t, auth::flags_t>,
        std::map<auth::uid_t, auth::flags_t>
    >;

    static
    result_type
    convert(const dynamic_t& from) {
        auto& tuple = from.as_array();
        if (tuple.size() != 2) {
            throw std::bad_cast();
        }

        return result_type{
            dynamic_converter::convert<auth::cid_t, auth::flags_t>(tuple.at(0)),
            dynamic_converter::convert<auth::uid_t, auth::flags_t>(tuple.at(1)),
        };
    }

    static
    bool
    convertible(const dynamic_t& from) {
        return from.is_array() && from.as_array().size() == 2;
    }

private:
    // TODO: Temporary until `dynamic_t` teaches how to convert into maps with non-string keys.
    template<typename K, typename T>
    static
    std::map<K, auth::flags_t>
    convert(const dynamic_t& from) {
        std::map<K, auth::flags_t> result;
        const dynamic_t::object_t& object = from.as_object();

        for(auto it = object.begin(); it != object.end(); ++it) {
            result.insert(std::make_pair(boost::lexical_cast<K>(it->first), it->second.to<T>()));
        }

        return result;
    }
};

} // namespace cocaine

namespace cocaine { namespace unicat {

namespace detail {
    // TODO: make global in core?
    const auto ACL_NODE = std::string{".acls"};

    auto
    make_dynamic_from_meta(const auth::metainfo_t& metainfo) -> dynamic_t {
        return std::make_tuple(
            std::move(metainfo.c_perms),
            std::move(metainfo.u_perms));
    }

    template<typename R, typename Backend>
    auto
    async_get(Backend&& backend, const std::string& path) -> std::future<R> {
        std::shared_ptr<std::promise<R>> promise;
        auto fut = promise->get_future();
        backend->get([=](std::future<api::unicorn_t::response::get> fut) {
            try {
                auto value = fut.get();

                R data;
                if (value.exists()) {
                    if (!value.value().convertible_to<R>()) {
                        throw std::system_error(make_error_code(error::invalid_acl_framing));
                    }
                    promise->set_value(value.value().to<R>());
                } else {
                    promise->set_value(std::move(data)); // <-- data MOVED!!!
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        }, path);

        return fut;
    }

    template<typename Backend>
    auto
    async_put(Backend&& backend, const std::string& path, const auth::metainfo_t& metainfo)
        -> std::future<void>
    {
        std::shared_ptr<std::promise<void>> promise;
        auto fut = promise->get_future();
        backend->put([=](std::future<api::unicorn_t::response::put> fut) {
            try {
                fut.get();
                promise->set_value();
            } catch (...) {
                promise->set_exception(std::current_exception());
            }

            },
            path,
            make_dynamic_from_meta(metainfo),
            // TODO: make real object verson, if it exist!
            unicorn::version_t{});

        return fut;
    }
}

unicorn_backend_t::unicorn_backend_t(const options_t& options) :
    backend_t(options),
    backend(api::unicorn(options.ctx_ref, options.name)),
    // TODO: incorrect backend name
    license(api::authorization::unicorn(options.ctx_ref, options.name))
{}

auto
unicorn_backend_t::read_metainfo(const std::string& entity) -> std::future<auth::metainfo_t>
{
    BOOST_ASSERT(backend);
    return detail::async_get<auth::metainfo_t>(
        backend, cocaine::format("{}/{}", entity, detail::ACL_NODE));
}

auto
unicorn_backend_t::write_metainfo(const std::string& entity, auth::metainfo_t& meta) -> std::future<void>
{
    BOOST_ASSERT(backend);
    return detail::async_put(
        backend, cocaine::format("{}/{}", entity, detail::ACL_NODE), meta);
}

auto
unicorn_backend_t::check_write(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
    return async_verify<io::unicorn::put>(
        license, cocaine::format("{}/{}", entity, detail::ACL_NODE), get_options().identity_ref);
}

auto
unicorn_backend_t::check_read(const std::string& entity) -> std::future<bool>
{
    BOOST_ASSERT(license);
    return async_verify<io::unicorn::get>(
        license, cocaine::format("{}/{}", entity, detail::ACL_NODE), get_options().identity_ref);
}

}
}
