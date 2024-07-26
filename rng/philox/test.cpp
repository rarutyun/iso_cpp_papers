#include <iostream>
#include <string>
#include <limits.h>

#include "philox.hpp"

// Test the conformance of the implementation with the ISO C++ standard
template <typename Engine>
void conformance_test() {
    {
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
            engine.seed();
            for(size_t j = 0; j < i - 1; j++) {
                engine();
            }
            engine.discard(1);
            if(reference[i] != engine()) {
                std::cout << __PRETTY_FUNCTION__ << " failed on step " << i << std::endl;
                break;
            }
        }
        std::cout << __PRETTY_FUNCTION__ << " passed step 2 discard after generation" << std::endl;
    }
}


int main() {
    conformance_test<std::philox4x32>();
    conformance_test<std::philox4x64>();

    seed_test<std::philox4x32>();
    seed_test<std::philox4x64>();

    discard_test<std::philox4x32>();
    discard_test<std::philox4x64>();

    return 0;
}