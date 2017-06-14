#pragma once

#include "cocaine/traits/enum.hpp"
#include "cocaine/traits/map.hpp"
#include "cocaine/traits/tuple.hpp"

#include <boost/mpl/list.hpp>

// TODO: taken from private core, should be moved into core source tree as
//       public interface someday
namespace cocaine { namespace auth {

    enum flags_t : std::size_t { none = 0x00, read = 0x01, write = 0x02, both = read | write };

    struct metainfo_t {
        std::map<auth::cid_t, flags_t> c_perms;
        std::map<auth::uid_t, flags_t> u_perms;

        auto
        empty() const -> bool {
            return c_perms.empty() && u_perms.empty();
        }
    };
}}

namespace cocaine {
namespace io {

template<>
struct type_traits<auth::metainfo_t> {
    typedef boost::mpl::list<
        std::map<auth::cid_t, auth::flags_t>,
        std::map<auth::uid_t, auth::flags_t>
    > underlying_type;

    template<class Stream>
    static
    void
    pack(msgpack::packer<Stream>& target, const auth::metainfo_t& source) {
        type_traits<underlying_type>::pack(target, source.c_perms, source.u_perms);
    }

    static
    void
    unpack(const msgpack::object& source, auth::metainfo_t& target) {
        type_traits<underlying_type>::unpack(source, target.c_perms, target.u_perms);
    }
};

} // namespace io
} // namespace cocaine
