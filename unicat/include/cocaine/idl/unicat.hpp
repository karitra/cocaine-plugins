//
// TODO:
//   - not tested at all
//   - value version supprt
//   - ensure correct rights assigment to various (malfunctional) paths
//
#pragma once

#include <string>
#include <vector>

#include <cstdint>

#include <cocaine/auth/uid.hpp>
#include <cocaine/rpc/protocol.hpp>

#include <cocaine/detail/forwards.hpp>

#include <boost/mpl/list.hpp>

#include "cocaine/auth/metainfo.hpp"

namespace cocaine {
namespace io {

struct unicat_tag;

struct unicat {

    // TODO: aggregate cids and uids vectors into structural type in order to
    // avoid user argument placement mistakes and to reduce arguments count.
    using common_arguments_type = boost::mpl::list<
        std::vector<cocaine::unicat::entity_type>,  // (1) entities in tuple:  (scheme, uri)
        std::vector<auth::cid_t>,                   // (2) array of client ids
        std::vector<auth::uid_t>,                   // (3) array of user ids
        auth::flags_t                               // (4) access bitmask: None - 0, R - 1, W - 2, ALL - 3,
    >::type;

    // Create rights ACL record for specified entity.
    // Requires RW access to acl table(s).
    // Not implemented.
    struct create {
        typedef unicat_tag tag;

        static constexpr const char* alias() noexcept {
            return "create";
        }

        using argument_type = common_arguments_type;
    };

    // Set specified rights in addition to existent one
    // Requires RW access to acl table(s).
    struct grant {
        typedef unicat_tag tag;

        static constexpr const char* alias() noexcept {
            return "grant";
        }

        using argument_type = common_arguments_type;
    };

    // Unset specified rights from existent one
    // Requires RW access to acl table(s).
    struct revoke {
        typedef unicat_tag tag;

        static constexpr const char* alias() noexcept {
            return "revoke";
        }

        using argument_type = common_arguments_type;
    };

    // Requires R access to acl table(s).
    // TODO: not implemented, experimental.
    struct view {
        typedef unicat_tag tag;

        static constexpr const char* alias() noexcept {
            return "view";
        }

        using arguments_type = boost::mpl::list<
            // (1) entities in tuple:  (scheme, entity)
            std::vector<cocaine::unicat::entity_type>
        >;

        // TODO: return mask for queried entity
        typedef option_of <
            std::map<cocaine::unicat::entity_type, auth::metainfo_t>
        >::tag upstream_type;
    };

    // Remove specified record(s) from acl table(s).
    // Requires W access to acl table(s).
    // TODO: not implemented, experimental.
    struct reset {
        typedef unicat_tag tag;

        static constexpr const char* alias() noexcept {
            return "reset";
        }

        using arguments_type = boost::mpl::list<
            // (1) entities in tuple:  (scheme, entity)
            std::vector<cocaine::unicat::entity_type>
        >;

        typedef void upstream_type;
    };
};

template <>
struct protocol<unicat_tag> {
    typedef unicat type;

    typedef boost::mpl::int_<1>::type version;

    typedef boost::mpl::list<
        unicat::create,
        unicat::grant,
        unicat::revoke,
        unicat::view,
        unicat::reset
    >::type messages;
};

}  // namespace io
}  // namespace cocaine
