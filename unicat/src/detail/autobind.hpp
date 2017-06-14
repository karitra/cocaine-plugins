#pragma once

#include <functional>

namespace cocaine { namespace unicat { namespace detail {

    template<int I>
    struct int_ph {};

    template<int... S>
    struct seq {
        template<typename Fn, typename... Args>
        static
        auto
        bind(Fn&& f, Args&&... args)
            -> decltype(std::bind(std::forward<Fn>(f), std::forward<Args>(args)..., int_ph<S+1>()...)) {
            return std::bind(std::forward<Fn>(f), std::forward<Args>(args)..., int_ph<S+1>()...);
        }
    };

    template <int N, int... S>
    struct generate : generate<N-1, N-1, S...> {};

    template <int... S>
    struct generate<0, S...> {
        using placeholders = seq<S...>;
    };
}
}
}

namespace std {
    template<int I>
    struct is_placeholder<cocaine::unicat::detail::int_ph<I>> : public integral_constant<int, I> {};
}
