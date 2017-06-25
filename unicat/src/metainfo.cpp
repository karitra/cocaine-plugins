#include "cocaine/auth/metainfo.hpp"
#include "cocaine/idl/unicat.hpp"

namespace cocaine { namespace auth {

namespace detail {
    using base_type = std::underlying_type<auth::flags_t>::type;

    template<typename Flags>
    auto
    flags_to_uint(Flags&& fl) -> base_type {
        return static_cast<base_type>(fl);
    }

    auto
    uint_to_flags(const base_type base) -> auth::flags_t {
        return static_cast<auth::flags_t>(base);
    }

    auth::flags_t operator|=(auth::flags_t& a, const auth::flags_t b) {
        return a = uint_to_flags(flags_to_uint(a) | flags_to_uint(b));
    }

    auth::flags_t operator|=(auth::flags_t& a, const base_type b) {
        return a = uint_to_flags(flags_to_uint(a) | b);
    }

    auth::flags_t operator&=(auth::flags_t& a, const auth::flags_t b) {
        return a = uint_to_flags(flags_to_uint(a) & flags_to_uint(b));
    }

    auth::flags_t operator&=(auth::flags_t& a, const base_type b) {
        return a = uint_to_flags(flags_to_uint(a) & b);
    }

    auth::flags_t operator~(const auth::flags_t a) {
        return uint_to_flags( ~flags_to_uint(a) );
    }
}

template<>
auto
alter<io::unicat::revoke>(auth::metainfo_t& metainfo, const auth::alter_data_t& data) -> void
{
    using namespace detail;
    for(const auto& cid : data.cids) {
        auto it = metainfo.c_perms.find(cid);
        if (it != std::end(metainfo.c_perms)) {
            it->second &= ~ data.flags;
        }
    }

    for(const auto& uid : data.uids) {
        auto it = metainfo.u_perms.find(uid);
        if (it != std::end(metainfo.u_perms)) {
            it->second &= ~ data.flags;
        }
    }
}

template<>
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t& data) -> void
{
    using namespace detail;
    for(const auto& cid : data.cids) {
        metainfo.c_perms[cid] |= data.flags;
    }

    for(const auto& uid : data.uids) {
        metainfo.c_perms[uid] |= data.flags;
    }
}


template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t&) -> void;

template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo, const auth::alter_data_t&) -> void;

}}
