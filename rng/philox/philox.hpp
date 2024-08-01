#ifndef STD_PHILOX_HPP
#define STD_PHILOX_HPP

#include <type_traits>
#include <tuple>
#include <utility>
#include <cstdint>
#include <array>
#include <limits>
#include <climits>
#include <istream>
#include <ostream>
#include <algorithm>
#include <bit>

namespace std {
namespace detail {
    template <typename IndexSequence, template <std::size_t...> typename TransformFunction>
    struct transform_index_sequence_impl;

    template <std::size_t... Is, template <std::size_t> typename TransformFunction>
    struct transform_index_sequence_impl<std::index_sequence<Is...>, TransformFunction>
    {
        using type = std::index_sequence<TransformFunction<Is>::value...>;
    };

    template <typename IndexSequence, template <std::size_t...> typename TransformFunction>
    using transform_index_sequence = typename transform_index_sequence_impl<IndexSequence, TransformFunction>::type;

    template <std::size_t Is>
    struct odd_transform
    {
        static constexpr std::size_t value = Is * 2 + 1;
    };

    template <std::size_t Is>
    struct even_transform
    {
        static constexpr std::size_t value = Is * 2;
    };
} // namespace detail

template <typename UIntType, std::size_t w, std::size_t n, std::size_t r, UIntType... consts>
struct philox_engine
{
private:
    static_assert(n == 2 || n == 4 || n == 8 || n == 16);
    static_assert(r > 0);
    static_assert(sizeof...(consts) == n); // check if n is even
    static_assert(w > 0 && w <= std::numeric_limits<UIntType>::digits);

    static constexpr std::size_t array_size = n / 2;

    using half_index_sequence = std::make_index_sequence<array_size>;

    using even_indices_sequence = detail::transform_index_sequence<half_index_sequence, detail::even_transform>;
    using odd_indices_sequence = detail::transform_index_sequence<half_index_sequence, detail::odd_transform>;

    static constexpr auto extract_elements = []<std::size_t... Is>(std::index_sequence<Is...>)
    {
        constexpr std::array<UIntType, n> temp{consts...};
        return std::array<UIntType, array_size>{{temp[Is]...}};
    };

public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr std::size_t word_size = w;
    static constexpr std::size_t word_count = n;
    static constexpr std::size_t round_count = r;
    static constexpr std::array<result_type, array_size> multipliers = extract_elements(even_indices_sequence{});
    static constexpr std::array<result_type, array_size> round_consts = extract_elements(odd_indices_sequence{});
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return max_impl(); }
    static constexpr result_type default_seed = 20111115u;
    // constructors and seeding functions
    philox_engine() : philox_engine(default_seed) {}

    explicit philox_engine(result_type value) : x{}, k{}, state_i{n - 1} {
        k[0] = value & result_mask;
    }

    template<class Sseq>
    explicit philox_engine(Sseq& q)
    {
        seed(q);
    }

    void seed(result_type value = default_seed) {
        k[0] = value & result_mask;
        for (std::size_t j = 1; j < array_size; ++j) {
            k[j] = 0;
        }
        reset_counter();
    }

    template<class Sseq>
    void seed(Sseq& q)
    {
        constexpr std::size_t p = (w - 1) / 32 + 1; // ceil division
        std::array<result_type, n / 2 * p> a;
        q.generate(a.begin(), a.end());

        for (std::size_t i = 0; i < (n / 2); ++i)
        {
            result_type sum = 0;
            for (std::size_t j = 0; j < p; ++j)
            {
                sum += a[i * p + j] << (32 * j);
            }
            k[i] = sum & result_mask;
        }

        reset_counter();
    }

    void set_counter(const array<result_type, n>& counter) {
        for (std::size_t j = 0; j < n; ++j) {
            x[n - j - 1] = counter[j] & result_mask;
        }
    }

    // equality operators
    friend bool operator==(const philox_engine& left, const philox_engine& right) {
        bool result = std::ranges::equal(left.x, right.x)
            && std::ranges::equal(left.k, right.k)
            && left.state_i == right.state_i;
        for (auto i = left.state_i + 1; (i < n) && result; ++i)
        {
            result = result && (left.y[i] == right.y[i]);
        }
        return result;
    }

    // generating functions
    result_type operator()()
    {
        ++state_i;
        if (state_i == n) {
            y = philox_generate(k, x); // see below
            increment_counter();
            state_i = 0;
        }
        return y[state_i];
    }

    void discard(unsigned long long z) {
        std::uint32_t available_in_buffer = n - 1 - state_i;
        if (z <= available_in_buffer) {
            state_i += z;
        }
        else {
            z -= available_in_buffer;
            int tail = z % n;
            if (tail == 0) {
                increment_counter(z / n);
                state_i = n - 1;
            }
            else
            {
                if (z > n) {
                    increment_counter((z - 1) / n);
                }
                y = philox_generate(k, x);
                increment_counter();
                state_i = tail - 1;
            }
        }
    }

    // inserters and extractors
    template<class charT, class traits>
    friend std::basic_ostream<charT, traits>&
    operator<<(std::basic_ostream<charT, traits>& os, const philox_engine& x);

    template<class charT, class traits>
    friend std::basic_istream<charT, traits>&
    operator>>(std::basic_istream<charT, traits>& is, philox_engine& x);


private: // utilities
    using uint_types = std::tuple<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>;
    using promotion_types = std::tuple<std::uint16_t, std::uint32_t, std::uint64_t, __uint128_t>;
public:
    static consteval std::size_t get_log_index(std::size_t val)
    {
        auto z = std::max(val, std::size_t(8));
        return std::bit_width(z - 1u) - 3u;
    }
private:

    using counter_type = std::tuple_element_t<get_log_index(w), uint_types>;
    using promotion_type = std::tuple_element_t<get_log_index(w), promotion_types>;

    static constexpr counter_type counter_mask = ~counter_type(0) >> (sizeof(counter_type) * CHAR_BIT - w);
    static constexpr result_type result_mask = ~result_type(0) >> (sizeof(result_type) * CHAR_BIT - w);


private: // functions

    static std::pair<counter_type, counter_type> mulhilo(result_type a, result_type b)
    {
        constexpr std::size_t shift = std::numeric_limits<promotion_type>::digits - w;
        promotion_type promoted_a = a;
        promotion_type promoted_b = b;
        promotion_type result = promoted_a * promoted_b;
        counter_type mulhi = result >> shift;
        counter_type mullo = (result << shift) >> shift;
        return {mulhi, mullo};
    }

    static std::array<result_type, n> philox_generate(std::array<result_type, array_size> keys, std::array<counter_type, n> x)
    {
        for (std::size_t q = 0; q != r; ++q)
        {
            static constexpr std::array<std::array<std::size_t, 16>, 4> permute_indices
            {{
                {0, 1},
                {2, 1, 0, 3},
                {0, 5, 2, 7, 6, 3, 4, 1},
                {2, 1, 4, 9, 6, 15, 0, 3, 10, 13, 12, 11, 14, 7, 8, 5}
            }};
            auto row = get_log_index(n * CHAR_BIT) - 1;
            std::array<counter_type, n> v = [&x, row]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::array<counter_type, n>{x[permute_indices[row][Is]]...};
            }(std::make_index_sequence<n>{});

            for (std::size_t k = 0; k < array_size; ++k)
            {
                auto [mulhi, mullo] = mulhilo(v[2 * k], multipliers[k]);
                x[2 * k + 1] = mullo;
                x[2 * k] = mulhi ^ keys[k] ^ v[2 * k + 1];
                keys[k] = (keys[k] + round_consts[k]) & result_mask;
            }
        }
        return [&x]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::array<result_type, n>{x[Is]...};
        }(std::make_index_sequence<n>{});
    }

    void increment_counter()
    {
        for (auto& elem : x)
        {
            ++elem;
            elem &= counter_mask;
            if (elem != 0)
            {
                break;
            }
        }
    }

    void increment_counter(unsigned long long z)
    {
        using increment_type = __uint128_t;

        increment_type tmp = z;
        for (std::size_t j = 0; j < n; j++)
        {
            tmp += x[j];
            x[j] = tmp & counter_mask;
            tmp = tmp >> w;
        }
    }

    void reset_counter()
    {
        for (std::size_t j = 0; j < n; ++j) {
            x[j] = 0;
        }
        state_i = n - 1;
    }

    static constexpr result_type max_impl()
    {
        return w == std::numeric_limits<result_type>::digits
            ? std::numeric_limits<result_type>::digits - 1
            : (result_type(1) << w) - 1;
    }

public: // state
    std::array<counter_type, n> x;
    std::array<result_type, array_size> k;
    std::array<result_type, n> y;

    std::uint32_t state_i;
};

using philox4x32 = philox_engine<std::uint_fast32_t, 32, 4, 10, 0xCD9E8D57, 0x9E3779B9, 0xD2511F53, 0xBB67AE85>;
using philox4x64 = philox_engine<std::uint_fast64_t, 64, 4, 10, 0xCA5A826395121157, 0x9E3779B97F4A7C15, 0xD2E7470EE14C6C93, 0xBB67AE8584CAA73B>;

} // namespace std

#endif // STD_PHILOX_HPP


