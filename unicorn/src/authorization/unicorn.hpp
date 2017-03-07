#include <cocaine/api/authorization/unicorn.hpp>

#include <atomic>
#include <map>

#include <cocaine/api/executor.hpp>
#include <cocaine/forwards.hpp>
#include <cocaine/locked_ptr.hpp>

#include <blackhole/logger.hpp>

namespace cocaine {
namespace authorization {
namespace unicorn {

using cocaine::unicorn::versioned_value_t;

enum flags_t: std::size_t {
    read = 0x01,
    both = read | 0x02,
};

class enabled_t : public api::authorization::unicorn_t {
    using node_t = std::tuple<auth::uid_t, flags_t>;
    using metainfo_t = std::vector<node_t>;

    std::unique_ptr<logging::logger_t> log;
    std::shared_ptr<api::unicorn_t> backend;
    std::unique_ptr<api::executor_t> executor;

    std::atomic<std::uint64_t> counter;
    synchronized<std::multimap<std::uint64_t, std::shared_ptr<api::unicorn_scope_t>>> scopes;

public:
    enabled_t(context_t& context, const std::string& service, const dynamic_t& args);

    auto
    verify(std::size_t event, const std::string& path, const auth::identity_t& identity, callback_type callback)
        -> void override;

private:
    auto
    make_path(const std::string& prefix) const -> std::string;

    auto
    upload_permissions(const std::string& prefix, metainfo_t metainfo, const versioned_value_t& value, callback_type callback)
        -> std::shared_ptr<api::unicorn_scope_t>;

    auto
    create_permissions(const std::string& prefix, metainfo_t metainfo, callback_type callback)
        -> std::shared_ptr<api::unicorn_scope_t>;

    auto
    update_permissions(const std::string& prefix, metainfo_t metainfo, std::size_t version, callback_type callback)
        -> std::shared_ptr<api::unicorn_scope_t>;
};

} // namespace unicorn
} // namespace authorization
} // namespace cocaine
