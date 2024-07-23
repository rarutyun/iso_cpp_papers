#ifndef STD_PHILOX_HPP
#define STD_PHILOX_HPP

#include <type_traits>
#include <utility>
#include <cstdint>
#include <array>
#include <iostream>
#include <climits>

namespace std {

template <typename UIntType, std::size_t w, std::size_t n, std::size_t r, UIntType... consts>
struct philox_engine
{
private:
    template <typename>
    struct print;

    static_assert(n > 0);
    static_assert((n | 1) != n); // check if n is even
    static_assert((n & (n - 1)) == 0); // n is a power of two
    static_assert(sizeof...(consts) == n); // check if n is even

    static constexpr std::size_t array_size = n / 2;

    using half_index_sequence = std::make_index_sequence<array_size>;

    template <typename IndexSequence, template <std::size_t...> typename TransformFunction>
    struct transform_index_sequence;

    template <std::size_t... Is, template <std::size_t> typename TransformFunction>
    struct transform_index_sequence<std::index_sequence<Is...>, TransformFunction>
    {
        using type = std::index_sequence<TransformFunction<Is>::value...>;
    };

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

    using even_indices_sequence = typename transform_index_sequence<half_index_sequence, even_transform>::type;
    using odd_indices_sequence = typename transform_index_sequence<half_index_sequence, odd_transform>::type;

    static constexpr auto extract_elements = []<std::size_t... Is>(std::index_sequence<Is...>)
    {
        constexpr std::array<UIntType, n> temp{consts...};
        return std::array<UIntType, array_size>{{temp[Is]...}};
    };


public:
    // types
    using result_type = UIntType;

    // engine characteristics
    static constexpr size_t word_size = w;
    static constexpr size_t word_count = n;
    static constexpr size_t round_count = r;
    static constexpr array<result_type, array_size> multipliers = extract_elements(even_indices_sequence{});
    static constexpr array<result_type, array_size> round_consts = extract_elements(odd_indices_sequence{});
    static constexpr result_type min() { return 0; }
    // static constexpr result_type max() { return m - 1; }
    static constexpr result_type default_seed = 20111115u;
    // constructors and seeding functions
    philox_engine() : philox_engine(default_seed) {}

    explicit philox_engine(result_type value) : x{}, k{}, i{n - 1} {
        k[0] = value & result_mask;
    }

    template<class Sseq>
    explicit philox_engine(Sseq& q);

    void seed(result_type value = default_seed);
    
    template<class Sseq>
    void seed(Sseq& q);
    
    void set_counter(const array<result_type, n>& counter);

    // equality operators
    friend bool operator==(const philox_engine& x, const philox_engine& y) {
        return false;
    }

    // generating functions
    result_type operator()()
    {
        ++i;
        if (i == n) {
            y = philox_generate(k, x); // see below
            increment_counter();
            i = 0;
        }
        return y[i];
    }

    void discard(unsigned long long z);
    // inserters and extractors
    template<class charT, class traits>
    friend basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const philox_engine& x);

    template<class charT, class traits>
    friend basic_istream<charT, traits>&
    operator>>(basic_istream<charT, traits>& is, philox_engine& x);


private: // utilities
    using uint_types = std::tuple<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>;
    using promotion_types = std::tuple<std::uint16_t, std::uint32_t, std::uint64_t, __uint128_t>;

    static consteval std::size_t log2(std::size_t val)
    {
        return ((val <= 2) ? 1 : 1 + log2(val / 2));
    }

    static consteval std::size_t ceil_log2(std::size_t val)
    {
        std::size_t additive = static_cast<std::size_t>(!std::has_single_bit(val));

        return log2(val) + additive;
    }

    using counter_type = std::tuple_element_t<ceil_log2(w / CHAR_BIT), uint_types>;
    using promotion_type = std::tuple_element_t<ceil_log2(w / CHAR_BIT), promotion_types>;

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
            std::array<counter_type, n> v = x;
            if constexpr(n == 4)
            {
                v[0] ^= v[2] ^= v[0] ^= v[2];
            }
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

public: // state
    std::array<counter_type, n> x;
    std::array<result_type, array_size> k;
    std::array<result_type, n> y;

    std::uint32_t i;
};

using philox4x32 = philox_engine<std::uint_fast32_t, 32, 4, 10, 0xCD9E8D57, 0x9E3779B9, 0xD2511F53, 0xBB67AE85>;
using philox4x64 = philox_engine<uint_fast64_t, 64, 4, 10, 0xCA5A826395121157, 0x9E3779B97F4A7C15, 0xD2E7470EE14C6C93, 0xBB67AE8584CAA73B>;

} // namespace std

#endif // STD_PHILOX_HPP