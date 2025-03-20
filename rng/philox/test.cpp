#include <iostream>
#include <sstream>
#include <string>
#include <limits.h>
#include <random>

#include "philox.hpp"

// Test the conformance of the implementation with the ISO C++ standard
template <typename Engine>
void conformance_test() {
    Engine engine;
    for(int i = 0; i < 9999; i++) {
        engine();
    }
    typename Engine::result_type reference;
    if(std::is_same_v<Engine, std::philox4x32>) {
        reference = 1955073260;
    }
    else {
        reference = 3409172418970261260;
    }
    if(engine() == reference) {
        std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
    } else {
        std::cout << __PRETTY_FUNCTION__ << " failed" << std::endl;
    }
}

// Test public API
template <typename Engine>
void api_test() {
    {
        Engine engine;
        engine.seed();
    }
    {
        Engine engine(1);
        engine.seed(1);
    }
    {
        std::seed_seq s;
        Engine engine(s);
        engine.seed(s);
    }
    {
        Engine engine;
        Engine engine2;
        if(!(engine == engine2) || (engine != engine2)) {
            std::cout << __PRETTY_FUNCTION__ << " failed !=, == for the same engines" << std::endl;
            return;
        }
        engine2.seed(42);
        if((engine == engine2) || !(engine != engine2)) {
            std::cout << __PRETTY_FUNCTION__ << " failed !=, == for the different engines" << std::endl;
            return;
        }
    }
    {
        std::ostringstream os;
        Engine engine;
        os << engine << std::endl;
        Engine engine2;
        engine2();
        std::istringstream in(os.str());
        in >> engine2;
        if(engine != engine2) {
            std::cout << __PRETTY_FUNCTION__ << " failed for >> << operators" << std::endl;
            return;
        }
    }
    {
        Engine engine;
        engine.min();
        engine.max();
    }
    std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
}

template <typename Engine>
void seed_test() {
    for(int i = 1; i < 5; i++) { // make sure that the state is reset properly for all idx positions
        Engine engine;
        typename Engine::result_type res;
        for(int j = 0; j < i - 1; j++) {
            engine();
        }
        res = engine();
        engine.seed();
        for(int j = 0; j < i - 1; j++) {
            engine();
        }
        if(res != engine()) {
            std::cout << __PRETTY_FUNCTION__ << " failed while generating " << i  << " elements" << std::endl;
        }
    }
    std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
}

template <typename Engine>
void discard_test() {
    {
        constexpr size_t n = 10; // arbitrary length we want to check
        typename Engine::result_type reference[n];
        Engine engine;
        for(int i = 0; i < n; i++) {
            reference[i] = engine();
        }
        for(int i = 0; i < n; i++) {
            engine.seed();
            engine.discard(i);
            for(size_t j = i; j < n; j++) {
                if(reference[j] != engine()) {
                    std::cout << __PRETTY_FUNCTION__ << " failed with error in element " << j << " discard " << i << std::endl;
                    break;
                }
            }
        }
        std::cout << __PRETTY_FUNCTION__ << " passed step 1 discard from the intial state" << std::endl;

        for(int i = 1; i < n; i++) {
            for(int j = 1; j < i; j++) {
                engine.seed();
                for(size_t k = 0; k < i - j; k++) {
                    engine();
                }
                engine.discard(j);
                if(reference[i] != engine()) {
                    std::cout << __PRETTY_FUNCTION__ << " failed on step " << i << " " << j << std::endl;
                    break;
                }
            }
        }
        std::cout << __PRETTY_FUNCTION__ << " passed step 2 discard after generation" << std::endl;
    }
}

template <typename Engine>
void set_counter_conformance_test() {
    Engine engine;
    std::array<typename Engine::result_type, Engine::word_count> counter;
    for(int i = 0; i < Engine::word_count - 1; i++) {
        counter[i] = 0;
    }
    
    counter[Engine::word_count - 1] = 2499; // to get 10'000 element
    engine.set_counter(counter);

    for(int i = 0; i < Engine::word_count - 1; i++) {
        engine();
    }

    typename Engine::result_type reference;
    if(std::is_same_v<Engine, std::philox4x32>) {
        reference = 1955073260;
    }
    else {
        reference = 3409172418970261260;
    }
    if(engine() == reference) {
        std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
    } else {
        std::cout << __PRETTY_FUNCTION__ << " failed" << std::endl;
    }
}

template <typename Engine>
void skip_test() {
    using T = typename Engine::result_type;
    for(T i = 1; i <= Engine::word_count + 1; i++) {
        Engine engine1;
        std::array<T, Engine::word_count> counter = {0};
        counter[Engine::word_count - 1] = i / Engine::word_count;
        engine1.set_counter(counter);
        for(T j = 0; j < i % Engine::word_count; j++) {
            engine1();
        }

        Engine engine2;
        engine2.discard(i);

        if(engine1() != engine2()) {
            std::cout << __PRETTY_FUNCTION__ << " failed for " << i << " skip" << std::endl;
            return;
        }
    }
    std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
}

template <typename Engine>
void counter_overflow_test() {
    using T = typename Engine::result_type;
    Engine engine1;
    std::array<T, Engine::word_count> counter;
    for(int i = 0; i < Engine::word_count; i++) {
        counter[i] = std::numeric_limits<T>::max();
    }

    engine1.set_counter(counter);
    for(int i = 0; i < Engine::word_count; i++) {
        engine1();
    } // all counters overflowed == start from 0 0 0 0

    Engine engine2;

    if(engine1() == engine2()) {
        std::cout << __PRETTY_FUNCTION__ << " passed" << std::endl;
    } else {
        std::cout << __PRETTY_FUNCTION__ << " failed" << std::endl;
    }
}

template <typename Engine>
void discard_overflow_test() {
    using T = typename Engine::result_type;
    for (int overflow_position = 0; overflow_position < Engine::word_count - 1; overflow_position++) {
        Engine engine1;
        std::array<T, Engine::word_count> counter = {0};

        int raw_counter_position = (Engine::word_count - overflow_position - 2) % Engine::word_count;
        std::cout << "Testing discard overflow for position " << raw_counter_position << std::endl;
        counter[raw_counter_position] = 1;

        engine1.set_counter(counter);

        Engine engine2;

        std::array<T, Engine::word_count> counter2 = {0};
        for (int i = Engine::word_count - overflow_position - 1; i < Engine::word_count - 1; i++) {
            counter2[i] = std::numeric_limits<T>::max();
        }

        engine2.set_counter(counter2);

        for (int i = 0; i < Engine::word_count; i++) {
            engine2();
        }

        for (int i = 0; i < Engine::word_count; i++) {
            engine2.discard(engine2.max());
        }

        if (engine1() == engine2()) {
            std::cout << __PRETTY_FUNCTION__ << " passed for overflow_position " << overflow_position << std::endl;
        }
        else {
            std::cout << __PRETTY_FUNCTION__ << " failed for overflow_position " << overflow_position << std::endl;
            break;
        }
    }
}

int main() {
    conformance_test<std::philox4x32>();
    conformance_test<std::philox4x64>();

    api_test<std::philox4x32>();
    api_test<std::philox4x64>();

    seed_test<std::philox4x32>();
    seed_test<std::philox4x64>();

    discard_test<std::philox4x32>();
    discard_test<std::philox4x64>();

    set_counter_conformance_test<std::philox4x32>();
    set_counter_conformance_test<std::philox4x64>();

    skip_test<std::philox4x32>();
    skip_test<std::philox4x64>();

    counter_overflow_test<std::philox4x32>();
    counter_overflow_test<std::philox4x64>();

    discard_overflow_test<std::philox4x32>();
    discard_overflow_test<std::philox4x64>();

    return 0;
}
