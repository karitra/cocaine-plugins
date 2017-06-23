#include "cocaine/auth/metainfo.hpp"
#include "cocaine/idl/unicat.hpp"

namespace cocaine { namespace auth {

namespace detail {
    using base_type = std::underlying_type<auth::flags_t>::type;

    auth::flags_t operator|=(auth::flags_t& a, const auth::flags_t b) {
        return a = static_cast<auth::flags_t>(
            static_cast<base_type>(a) | static_cast<base_type>(b));
    }

    auth::flags_t operator|=(auth::flags_t& a, const base_type b) {
        return a = static_cast<auth::flags_t>(static_cast<base_type>(a) | b);
    }

    auth::flags_t operator&=(auth::flags_t& a, const auth::flags_t b) {
        return a = static_cast<auth::flags_t>(
                static_cast<base_type>(a) & static_cast<base_type>(b));
    }

    auth::flags_t operator&=(auth::flags_t& a, const base_type b) {
        return a = static_cast<auth::flags_t>(static_cast<base_type>(a) & b);
    }
}

template<>
auto
alter<io::unicat::revoke>(auth::metainfo_t& metainfo,
    const std::vector<cid_t>& cids, const std::vector<uid_t>& uids, const auth::flags_t flags) -> void
{
    using namespace detail;
    for(const auto& cid : cids) {
        auto it = metainfo.c_perms.find(cid);
        if (it != std::end(metainfo.c_perms)) {
            it->second &= ~ static_cast<base_type>(flags);
        }
    }

    for(const auto& uid : uids) {
        auto it = metainfo.u_perms.find(uid);
        if (it != std::end(metainfo.u_perms)) {
            it->second &= ~ static_cast<base_type>(flags);
        }
    }
}

template<>
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo,
    const std::vector<cid_t>& cids, const std::vector<uid_t>& uids, const auth::flags_t flags) -> void
{
    using namespace detail;
    for(const auto& cid : cids) {
        metainfo.c_perms[cid] |= flags;
    }

    for(const auto& uid : uids) {
        metainfo.c_perms[uid] |= flags;
    }
}


template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo,
    const std::vector<cid_t>& cids, const std::vector<uid_t>& uids, const auth::flags_t flags) -> void;

template
auto
alter<io::unicat::grant>(auth::metainfo_t& metainfo,
    const std::vector<cid_t>& cids, const std::vector<uid_t>& uids, const auth::flags_t flags) -> void;

}}
