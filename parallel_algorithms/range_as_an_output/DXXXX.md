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
nor discusses in detail using ranges as the output for serial range algorithms.

# Introduction # {#introduction}

In [@P3179R2] we proposed to add overloads taking an execution policy to the functions from [algorithms]{.sref}
defined in the `std::ranges` namespace. We refer to these overloads as *parallel range algorithms*.
The proposed design in particular suggested that:

- Parallel range algorithms take a range, not an iterator, as an output for the overloads with ranges,
  and additionally take an output sentinel for the overloads with iterators and sentinels.
- Parallel range algorithms require `random_access_{iterator,range}`.
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
  for the future but will not be ready for C++26.

Consequently, in the [@P3179R3] revision we switched back to a single iterator for representing
an output sequence. However, with this paper we would like to provide new information and discuss
the raised concerns one more time, seeking an opportunity to go forward with the original idea,
which we believe will be a notable improvement.

# Recap and motivation # {#recap_motivation}

As a recap, we would like to propose taking a range as the output for the overloads that take ranges for input.
Similarly, we propose a sentinel for output where input is passed as "iterator and sentinel".
See [Proposed API](#proposed_api) for the examples.

We also propose output ranges to have boundaries set independently of the input ranges. An algorithm should
stop as soon as the end is reached for the shortest range. The main motivation is to follow established practices
of secure coding, which recommend or even require to always specify the size of the output in order to prevent
out-of-range data modifications. We think this will not impose any practical limitation on which ranges can be
used for the output of a parallel algorithm, as we could not find or invent an example of a random-access writable
range which would also be unbounded.

This *range-as-the-output* approach should not be confused with the `output_range` concept already used
with a few serial range algorithms. `output_range` would not be enough for parallel range algorithms
which would require random access ranges for the output, same as for the input.

The benefits of this approach, comparing to taking a single iterator for the output, are:

- It creates a safer API where the modified data sequences have known bounds. Specifically, the `sized_range`
    and `sized_sentinel_for` concepts will be applied to the output sequences in the same way as [@P3179R3]
    applies those to the input sequences.
- Not for all algorithms the output size is defined by the input size. An example is `copy_if` (and similar
    algorithms with *filtering* semantics), where the output sequence might be shorter than the input one.
    Knowing the expected size of the output may open opportunities for more efficient implementations.
- Passing a range for the output makes code a bit simpler in the cases typical for parallel execution.

It is worth noting that to various degrees these reasons are also applicable to serial range algorithms.

We think that in practice parallel algorithms mainly write the output data into a container or storage
with preallocated space, for efficiency reasons. So, typically parallel algorithms receive `std::begin(v)`
or `v.begin()` or `v.data()` for output, where `v` is an instance of `std::vector` or `std::array`.
Allowing `v` to be passed directly for output in the same way as for input results in a slightly simpler code.
Here is an example compared to the approach in [@P3179R3]:

::: cmptable

### P3179R3
```cpp
void normalize_parallel(random_access_range auto&& v) {
  auto mx = max(execution::par, v);
  transform(execution::par, v, views::repeat(mx), ranges::begin(v), divides);
}
```

### This paper
```cpp
void normalize_parallel(random_access_range auto&& v) {
  auto mx = max(execution::par, v);
  transform(execution::par, v, views::repeat(mx), v, divides);
}
```

:::

In addition, classes such as `std::back_insert_iterator` or `std::ostream_iterator`, which have no meaningful
range interpretation, already cannot be used with C++17 parallel algorithms that require at least forward
iterators. Codes using these types will in any case require modifications to migrate to parallel algorithms.

All in all, we think for parallel algorithms taking ranges and sentinels for output makes more sense
than only taking an iterator.

Of the 89 parallel range algorithms proposed in [@P3179R3] (as counted by names, not overloads), the changes
we discuss here will affect only the following 17 algorithms: `copy`, `copy_if`, `move`, `transform`,
`replace_copy`, `replace_copy_if`, `remove_copy`, `remove_copy_if`, `unique_copy`, `reverse_copy`, `rotate_copy`,
`partition_copy`, `merge`, `set_union`, `set_intersection`, `set_difference`, `set_symmetric_difference`.

# Addressing the concerns # {#addressing_concerns}

## Mismatch of parallel and serial range algorithms ## {#parallel_serial_mismatch}

The first concern we have heard about this approach is the mismatch between serial and parallel variations.
That is, if serial range algorithms only take iterators for output and parallel range algorithms only take ranges,
switching between those will always require code changes. That can be resolved by:

- (A) adding *range-as-the-output* to serial range algorithms,
- (B) adding *iterator-as-the-output* to parallel range algorithms

or both.

The option (A) would give some of the described benefits to serial range algorithms as well; one could argue
that it would be a useful addition on its own. However it is obviously too late to pursue this option for C++26.

The option (B) does not seem to have benefits besides the aligned semantics, while it has the downside of
some algorithm variations not being strengthened with the requirement for an output sequence to be bounded.
Nevertheless, given that the option (A) is not considered for C++26, we prefer the option (B) to the status quo
of [@P3179R3].

For the "iterator and sentinel" overloads we prefer to always require a sentinel for output, despite the mismatch
with the corresponding serial overloads. We expect those overloads to be rarerly used in general, and in many
cases existing C++17 parallel algorithms can be used instead. Adding a sentinel for output, on the other hand,
preserves the existing approach of expressing a range as the "iterator and sentinel" pair in algorithms
descriptions, as well as allows to specify how the end of sequence is computed for the option (B).

## Semantical ambiguity for certain algorithms ## {#semantical_ambiguity}

The potential semantical ambiguity can be illustrated by the following code:

```cpp
std::vector<int> vec1;
std::vector<int> vec2;

// Some initialization

std::ranges::copy(policy, vec1, vec2);
```

for which there might be uncertainty what the behavior is when `vec2.size() != vec1.size()`. Some might expect
that `std::ranges::copy` resizes `vec2` and makes it an exact copy of `vec1`.

However, the standard already has a precedent in serial versions of `std::ranges::uninitialized_copy` and
`std::ranges::uninitialized_move`, which have the *range-as-the-output* semantics exactly as we propose:

- They use ranges (or sentinels) for both input and output sequences.
- They don't resize the output sequence.
- They stop as soon as any of the sequences reaches its end, copying/moving elements to the minimum of two sizes.

Giving another semantics to `std::ranges::copy` etc. would be inconsistent with these already existing
algorithms. Moreover, it would be generally inconsistent with the current *elementwise* semantics
of both iterator-based and range algorithms. For example, when `vec2.size()` is greater than `vec1.size()`,
`std::ranges::copy(vec1, vec2.begin())` does not modify the elements of `vec2` beyond the size of `vec1`,
and so should not `std::ranges::copy(policy, vec1, vec2)`.

Alternatively, potentially ambiguous names can be modified with some prefix, such as `partial_`. It would
follow `std::ranges::partial_sort_copy`, another range-as-the-output algorithm that already uses the semantics
we propose. That is, we could keep `copy` with iterator-as-the-output and introduce `partial_copy` with
range-as-the-output semantics, and similarly for other algorithms. We do not recommend this however,
as there is no serial algorithms with such names, and also because it would create very sophisticated names:
`partial_remove_copy_if`, really?

Speaking of precedents, it is worth noting that there are more existing range-as-the-output algorithms -
`fill`, `generate`, and `iota`. Their specifics is absence of input sequences, so the output sequence needs
a boundary. However, extending this principle from algorithms with zero input sequences to those with one
or more seems appropriate.

## Range as an output for serial range algorithms ## {#range_for_serial}

// TODO

# Proposed API # {#proposed_api}

Below is an example of modifications to be made to the wording proposed in [@P3179R3]. The example adjusts
the binary `transform` algorithm to use an output sentinel and adds a new overload taking an output range.
Similar modifications will be made to all the mentioned 17 algorithms if this paper is supported.

Alternatively, we can use an exposition-only `@*range-or-iterator*@` concept that combines the requirements
for both a range and an iterator by logical disjunction, as its name suggests. We did not explore which way
makes more sense; at glance, there seems to be little practical difference for library implementors.

```cpp
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
    transform(ExecutionPolicy&& policy, I1 first1, S1 last1, I2 first2, S2 last2, O result,
              @[`SO result_last,`]{.add}@ F binary_op, Proj1 proj1 = {}, Proj2 proj2 = {});

template<typename ExecutionPolicy,
         ranges::random_access_range R1,
         ranges::random_access_range R2,
         random_access_iterator O,
         copy_constructible F,
         class Proj1 = identity, class Proj2 = identity>
requires indirectly_writable<O,
             indirect_result_t<F&,
                 projected<ranges::iterator_t<R1>, Proj1>,
                 projected<ranges::iterator_t<R2>, Proj2>>>
         && (sized_range<R1> || sized_range<R2>)
constexpr binary_transform_result<ranges::borrowed_iterator_t<R1>,
                                  ranges::borrowed_iterator_t<R2>,
                                  O>
    transform(ExecutionPolicy&& policy, R1&& r1, R2&& r2, O result, F binary_op,
              Proj1 proj1 = {}, Proj2 proj2 = {});
```
:::add
```cpp
template<typename ExecutionPolicy,
         ranges::random_access_range R1,
         ranges::random_access_range R2,
         ranges::random_access_range OutR,
         copy_constructible F,
         class Proj1 = identity, class Proj2 = identity>
requires indirectly_writable<ranges::iterator_t<OutR>,
             indirect_result_t<F&,
                 projected<ranges::iterator_t<R1>, Proj1>,
                 projected<ranges::iterator_t<R2>, Proj2>>>
         && (sized_range<R1> || sized_range<R2>) && sized_range<OutR>
constexpr binary_transform_result<ranges::borrowed_iterator_t<R1>,
                                  ranges::borrowed_iterator_t<R2>,
                                  ranges::borrowed_iterator_t<OutR>>
    transform(ExecutionPolicy&& policy, R1&& r1, R2&& r2, OutR&& result, F binary_op,
              Proj1 proj1 = {}, Proj2 proj2 = {});
```
:::

# Polls # {#polls}

## Joint SG1 + SG9, St. Louis, 2024 ## {#sg1_sg9_st_louis_2024}

**Poll**: Continue work on P3179R2 for IS'26 with these notes:

1. RandomAccess for inputs and outputs
2. Iterators for outputs
3. We believe the overloads are worth it

+----+----+----+----+----+
| SF |  F |  N |  A | SA |
+:==:+:==:+:==:+:==:+:==:+
|  7 |  4 |  2 |  1 |  0 |
+----+----+----+----+----+
