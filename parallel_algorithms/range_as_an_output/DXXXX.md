---
title: "Parallel range algorithms should write to ranges"
document: DXXXXR0
date: today
audience: SG1, SG9
author:
  - name: Alexey Kukanov
    email: <alexey.kukanov@intel.com>
  - name: Ruslan Arutyunyan
    email: <ruslan.arutyunyan@intel.com>
toc: true
toc-depth: 2
---

# Abstract {- .unlisted}

This paper elaborates on the question of using a range as the output for parallel range algorithms
that we proposed in [@P3179R2], addressing the raised concerns. The paper neither proposes
nor discusses using ranges as the output for serial range algorithms.

# Introduction # {#introduction}

In [@P3179R2] we proposed to add overloads taking an execution policy to the functions in [algorithms]{.sref}
defined in the `std::ranges` namespace. We refer to these overloads as *parallel range algorithms*.
The proposed design in particular suggested that:

- Parallel range algorithms take a range, not an iterator, as an output for the overloads with ranges,
  and additionally take an output sentinel for the overloads with iterators and sentinels.
- Parallel range algorithms require random_access_{iterator,range}.
- At least one of the input sequences as well as the output sequence must be bounded.

The last two of these design decisions were [approved](#sg1_sg9_st_louis_2024) at the joint SG1 and SG9
meeting in St. Louis. However, the idea of using ranges for the output sequences of parallel range algorithms
did not gain sufficient support. The following major concerns were expressed during the meeting
as well as in personal communication:

- That would introduce a mismatch between serial and parallel range algorithms so that
  switching between those will always require more code changes than just adding an execution policy.
- There might be a perception of semantical ambiguity when a whole container is used as the output
  for certain algorithms, such as in `std::ranges::copy(policy, vector1, vector2)`.
- That could complicate improvements on the output of serial range algorithms, which are considered
  but will not be ready for C++26.

Consequently, in the [@P3179R3] revision we switched back to a single iterator for representing
an output sequence. However, with this paper we would like to discuss the raised concerns one more time, given
that we have a new information, seeking an opportunity to go forward with the original idea,
which we believe to be a notable improvement.

# Motivation # {#motivation}

We would like to propose a range as the output for the overloads that take ranges for input. Similarly, we propose
a sentinel for output where input is passed as "iterator and sentinel". See [Proposed API](#proposed_api) for the examples.

The reasons for that are:

- It creates a safer API where all the data sequences have known limits.
- Not for all algorithms the output size is defined by the input size. An example is `copy_if`
    (and similar algorithms with *filtering* semantics), where the output sequence is allowed to be shorter than the input
    one. Knowing the expected size of the output may open opportunities for more efficient parallel implementations.
- Passing a range for output makes code a bit simpler in the cases typical for parallel execution.

It is worth noting that to various degrees these reasons are also applicable to serial algorithms.

We think that in practice parallel algorithms mainly write the output data into a container or storage
with preallocated space, for efficiency reasons. So, typically parallel algorithms receive
`std::begin(v)` or `v.begin()` or `v.data()` for output, where `v` is an instance of `std::vector` or `std::array`.
Allowing `v` to be passed directly for output in the same way as for input results in a slightly simpler code. Here is the
example compared to the current [@P3179R3] approach:

```cpp
void normalize_parallel(range auto&& v) {
  auto mx = reduce(execution::par, v, ranges::max{});
  transform(execution::par, v, views::repeat(mx), @[`std::ranges::begin`(]{.rm}`v`[`)`]{.rm}@, divides);
}
```

In addition, using classes such as `std::back_insert_iterator` or `std::ostream_iterator`, which do not have a range underneath,
is already not possible with C++17 parallel algorithms that require at least forward iterators.
Migrating such code to use algorithms with execution policies will require modifications in any case.

All in all, we think for parallel algorithms taking ranges and sentinels for output makes more sense than only taking an
iterator.

# Addressing concerns # {#addressing_concerns}

## Parallel vs serial range algorithms mismatch ## {#parallel_serial_mismatch}

The main concern we have heard about this approach is the mismatch between serial and parallel variations.
That is, if serial range algorithms only take iterators for output and parallel range algorithms only take ranges,
switching between those will always require code changes. That can be resolved by:

- (A) adding *output-as-range* to serial range algorithms,
- (B) adding *output-as-iterator* to parallel range algorithms

or both.

The option (A) gives some of the described benefits to serial range algorithms as well; one could argue that it
would be a useful addition on its own.
The option (B) does not seem to have benefits besides the aligned semantics, while it has the downside of not enforcing
the requirements we propose in ["Requiring ranges to be
bounded"](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3179r3.html#require_bounded_ranges) section of
[@P3179R3].

With either (A) or (B), the output parameter for range algorithm overloads could be both a range and an iterator.
In the formal wording, this could be represented either as two separate overloads with different requirements
on that parameter, or with an exposition-only `@*range-or-iterator*@` concept that combines the requirements
by logical disjunction, as its name suggest. We did not explore which makes more sense; at glance, there seems
to be little practical difference for library implementors.

For "iterator and sentinel" overloads we prefer to always require a sentinel for output, despite the mismatch with
the corresponding serial overloads because we expect those overloads rarer used compared to range ones.

## Semantical ambiguity for certain algorithms ## {#semantical_ambiguity}

The semantical ambiguity might appear for the following code:

```cpp
std::vector<int> vec1;
std::vector<int> vec2;

// Some initialization

std::ranges::copy(policy, vec1, vec2);
```

because there might be uncertainty what is the behavior if `vec2.size() < vec1.size()`. One might think that
`std::ranges::copy` would allocate in the described scenario to make `vec2` the exact copy of `vec1`.

However, we already have the precedent in the standard for serial version of `std::ranges::uninitialized_copy` and
`std::ranges::uninitialized_move`, which have *range-as-an-output* and the exactly same semantics as we propose:

- They have sentinel for both input and output sequences
- They don't allocate
- They exit as soon as any of their sequences reaches the end copying the minimal elements number of two sizes

There is another precedent of range-as-an-output case: `std::ranges::partial_sort_copy`, which is also semantically close to
`std::ranges::uninitialized_copy`.

it's also worth noting that there are other range algorithms - `fill`, `generate`, and `iota` - that take a range or an
"iterator and sentinel" pair for their output. Their specifics is absence of input sequences, so the output sequence needs
a boundary. Nevertheless, these are precedents of specifying output as a range, and extending it from algorithms with zero
input sequences to those with one or more seems appropriate.

## Range as an output for serial range algorithms ## {#range_for_serial}

// TODO

# Proposed API # {#proposed_api}

```cpp
// binary transform with an output range and an output sentinel
template<typename ExecutionPolicy,
         random_access_iterator I1, sentinel_for<I1> S1,
         random_access_iterator I2, sentinel_for<I2> S2,
         random_access_iterator O, @[`sized_sentinel_for<O> SO,`]{.add}@
         copy_constructible F,
         class Proj1 = identity, class Proj2 = identity>
requires indirectly_writable<O,
             indirect_result_t<F&, projected<I1, Proj1>, projected<I2, Proj2>>>
         && (sized_sentinel_for<S1, I1> || sized_sentinel_for<S2, I2>)
constexpr binary_transform_result<I1, I2, O>
    transform(ExecutionPolicy&& policy, I1 first1, S1 last1, I2 first2, S2 last2, O result
              @[`SO result_last`]{.add}@, F binary_op, Proj1 proj1 = {}, Proj2 proj2 = {});

template<typename ExecutionPolicy,
         ranges::random_access_range R1,
         ranges::random_access_range R2,
         @[`random_access_iterator O`, ]{.rm}[`ranges::random_access_range OR`]{.add}@,
         copy_constructible F,
         class Proj1 = identity, class Proj2 = identity>
requires indirectly_writable<@[`O`]{.rm}[`ranges::iterator_t<OR>`]{.add}@,
             indirect_result_t<F&,
                 projected<ranges::iterator_t<R1>, Proj1>,
                 projected<ranges::iterator_t<R2>, Proj2>>>
         && (sized_range<R1> || sized_range<R2>)
         @[`&& sized_range<OR>`]{.add}@
constexpr binary_transform_result<ranges::borrowed_iterator_t<R1>,
                                  ranges::borrowed_iterator_t<R2>,
                                  @[`O`]{.rm}[`ranges::borrowed_iterator_t<OR>`]{.add}@>
    transform(ExecutionPolicy&& policy, R1&& r1, R2&& r2, @[`O`]{.rm}[`OR&&`]{.add}@ result, F binary_op,
              Proj1 proj1 = {}, Proj2 proj2 = {});
```

# Polls # {#polls}

## Joint SG1 + SG9, St. Louis, 2024 ## {#sg1_sg9_st_louis_2024}

**Poll**: Continue work on P3179R2 for IS'26 with these notes:

1. RandomAccess for inputs and outputs
1. Iterators for outputs
1. We believe the overloads are worth it

+----+----+----+----+----+
| SF |  F |  N |  A | SA |
+:==:+:==:+:==:+:==:+:==:+
|  7 |  4 |  2 |  1 |  0 |
+----+----+----+----+----+
