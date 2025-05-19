
---
title: Parallel numeric range algorithms
document: DXXXXR0
date: 2025-05-19
audience: SG1,SG9,LEWG
author:
  - name: Ruslan Arutyunyan
    email: <ruslan.arutyunyan@intel.com>

  - name: Mark Hoemmen
    email: <mhoemmen@nvidia.com>

  - name: Bryce Adelstein Lelbach
    email: <brycelelbach@gmail.com>

  - name: Abhilash Majumder
    email: <abmajumder@nvidia.com>
toc: true
---

# Authors

* Ruslan Arutyunyan (Intel)

* Mark Hoemmen (NVIDIA)

* Bryce Adelstein Lelbach (NVIDIA)

* Abhilash Majumder (NVIDIA)

# Revision history

* Revision 0 to be submitted 2025-??-??

# Abstract

We propose adding both parallel and non-parallel ranges overloads of the following numeric algorithms: `reduce`, `inclusive_scan`, and `exclusive_scan`.  We also propose adding convenience wrappers `ranges::sum` and `ranges::product` for special cases of `reduce` with addition and multiplication, respectively.

# Design

## Constraining parallel ranges numeric algorithms

1. We use the same constraints as `fold_left` and `fold_right` to constrain the binary operator of `reduce` and `*_scan`.

2. We imitate C++17 parallel algorithms and [linalg] (<a href="https://wg21.link/P1673R13">P1673R13</a>) by using *GENERALIZED_NONCOMMUTATIVE_SUM* and *GENERALIZED_SUM* to describe the behavior of `reduce` and `*_scan`.

3. Otherwise, we follow the approach of <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> ("C++ Parallel Range Algorithms").

<a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a>, which is in the last stages of wording review, defines parallel versions of many ranges algorithms in the C++ Standard Library.  (The "parallel version of an algorithm" is an overload of an algorithm whose first parameter is an execution policy.)  That proposal restricts itself to adding parallel versions of existing ranges algorithms.  P3179R8 explicitly defers adding overloads to the numeric algorithms in <a href="https://eel.is/c++draft/numeric.ops">[numeric.ops]</a>, because these do not yet have ranges versions.  Our proposal fills that gap.

WG21 did not have time to propose ranges-based numeric algorithms with the initial set of ranges algorithms in C++20.  <a href="https://wg21.link/P1813R0">P1813R0</a>, "A Concept Design for the Numeric Algorithms," points out the challenge of defining ranges versions of the existing parallel numeric algorithms.  What makes this task less straightforward is that the specification of the parallel numeric algorithms permits them to reorder binary operations like addition.  This matters because many useful number types do not have associative addition.  Lack of associativity is not just a floating-point rounding error issue; one example is saturating integer arithmetic.  Ranges algorithms are constrained by concepts, but it's not clear even if it's a good idea to define concepts that can express permission to reorder terms in a sum.

C++17 takes the approach of saying that parallel numeric algorithms can reorder the binary operations however they like, but does not say whether any reordering would give the same results as any other reordering.  The Standard expresses this through the wording "macros" *GENERALIZED_NONCOMMUTATIVE_SUM* and *GENERALIZED_SUM*.  (A wording macro is a parameterized abbreviation for a longer sequence of wording in the Standard.  We put "macros" in double quotes because they are not necessarily preprocessor macros.  They might not even be implementable as such.)  Algorithms become ill-formed, no diagnostic required (IFNDR) if the element types do not define the required operations.  P1813R0 instead defines C++ concepts that represent algebraic structures, all of which involve a set with a closed binary operation.  Some of the structures require that the operation be associative and/or commutative.  P1813R0 uses those concepts to constrain the algorithms.  This means that the algorithms will not be selected for overload resolution if the element types do not define the required operations.  It further means that algorithms could (at least in theory) dispatch based on properties like whether the element type's binary operation is commutative.  The concepts include both syntactic and semantic constraints.  

WG21 has not expressed a consensus on P1813R0's approach.  LEWGI reviewed P1813R0 at the Belfast meeting in November 2019, but did not forward the proposal and wanted to see it again.  Two other proposals express something more like WG21's consensus on constraining the numeric algorithms: <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P1673R13">P1673R13</a>, "A free function linear algebra interface based on the BLAS," which defines mdspan-based analogs of the numeric algorithms.  Section 5.1.1 of P2214R2 points out that P1813R0's approach would overconstrain `fold`; P2214R2 instead suggests just constraining the operation to be binary invocable.  This was ultimately the approach taken by the Standard through the exposition-only concepts _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  Section 5.1.2 of P2214R2 says that `reduce` "calls for the kinds of constrains that P1813R0 is proposing."

<a href="https://wg21.link/P1673R13">P1673R13</a>, which was adopted into the Working Draft for C++26 as [linalg], took an entirely different approach for its set of `mdspan`-based numeric algorithms.  Section 10.8, "Constraining matrix and vector element types and scalars," explains the argument.  Here is a summary.

1. Requirements like associativity are too strict to be useful for practical types.  The only number types in the Standard with associative addition are unsigned integers.  It's not just a rounding error "epsilon" issue; sums of saturating integers can have infinite error if one assumes associativity.

2. "The algorithm may reorder sums" (which is what we want to say) means something different than "addition on the terms in the sum is associative" (which is not true for many number types of interest).  That is, permission for an algorithm to reparenthesize sums is not the same as a concept constraining the terms in the sum.

3. P1813R0 defines concepts that generalize a mathematical group.  These are only useful for describing a single set of numbers, that is, one type.  This excludes useful features like mixed precision (e.g., where the result type in `reduce` differs from the range's element type) and types that use expression templates.  One could imagine generalizing this to a set of types that have a common type, but this can be too restrictive; Section 5.1.1 of <a href="https://wg21.link/P2214R2">P2214R2</a> gives an example involving two types in a fold that do not have a common type.

P1673R13 says that algorithms have complete freedom to create temporary copies or value-initialized temporary objects, rearrange addends and partial sums arbitrarily, or perform assignments in any order, as long as this would produce the result specified by the algorithm's *Effects* and *Remarks* when operating on elements of a semiring.  The `linalg::dot` ([linalg.algs.blas1.dot]) and `linalg::vector_abs_sum` ([linalg.algs.blas1.asum]) algorithms specifically define the returned result(s) in terms of *GENERALIZED_SUM*.  Those algorithms do that because they need to constrain the precision of intermediate terms in the sum (so they need to define those terms).  In our case, the Standard already uses *GENERALIZED_SUM* and *GENERALIZED_NONCOMMUTATIVE_SUM* to define ranges algorithms like `reduce`, `inclusive_scan`, and `exclusive_scan`.  We can just adapt this wording to talk about ranges instead of iterators.  This lets us imitate the approach of <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> in adding ranges overloads.

Our approach combines the syntactic constraints used for the `fold_*` family of algorithms, with the semantic approach of P1673R13 and the C++17 parallel numeric algorithms.  For example, we constrain `reduce`'s binary operation with both _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  (This expresses that if the binary operation is called with an argument of the initial value's type `T`, then that argument can be in either the first or second position.)  We express what `reduce` does using *GENERALIZED_SUM*.

## "The lost algorithm": Noncommutative parallel reduction?

The Standard lacks an analog of `reduce` that can assume associativity but not commutativity of binary operations.  One author of this proposal refers to this as the "the lost algorithm" (in e.g., <a href="https://adspthepodcast.com/2021/05/14/Episode-25.html">Episode 25 of "ASDP: The Podcast"</a>).  To elaborate: The current numeric algorithms express a variety of permissions to reorder binary operations.

1. `accumulate` and `partial_sum` both precisely specify the order of binary operations as sequential, from left to right.  This works even if the binary operation is neither associative nor commutative.

2. The various `*_scan` algorithms can reorder binary operations as if they are associative (they may replace `a + (b + c)` with `(a + b) + c`), but not as if they are commutative (they may replace `a + b` with `b + a`).

3. `reduce` can reorder binary operations as if they are both associative and commutative.

What's missing here is a parallel analog of `reduce` with the assumptions of `*_scan`, that is, a reduction that can assume associativity but not commutativity of binary operations.  Parallel reduction operations with these assumptions exist in other programming models.  For example, MPI (the Message Passing Interface for distributed-memory parallel communication) has a function `MPI_Create_op` for defining custom reduction operators from a user's function.  `MPI_Create_op` has a parameter that specifies whether MPI may assume that the user's function is commutative.

Users could get a parallel algorithm by calling `*_scan` with an extra output sequence, and using only the last element.  However, this requires extra storage.

A concepts-based approach like P1813R0's could permit specializing `reduce` on whether the user asserts that the binary operation is commutative.  P1813R0 does not attempt to do this; it merely specializes `reduce` on whether the associative and commutative operation has a two-sided identity element.  Furthermore, P1813R0 does not offer a way for users to assert that an operation is associative or commutative, because the `magma` (nonassociative) and `semigroup` (associative) concepts do not differ syntactically.  One could imagine a refinement of this design that includes a trait for users to specialize on the type of their binary operation, say `is_commutative<BinaryOp>`.  This would be analogous to the `two_sided_identity` trait in P1813R0 that lets users declare that their set forms a monoid, a refinement of `semigroup` with a two-sided identity element.

This proposal does not attempt to fill this gap in the Standard parallel algorithms, but would welcome a separate proposal to do so.  We think the right way would be to propose a new algorithm with a distinct name.  A reasonable choice of name would be `fold` (just `fold` by itself, not `fold_left` or `fold_right`).

## What algorithms to include?

We propose ranges overloads (both parallel and nonparallel) of only three algorithms: `reduce`, `inclusive_scan`, and `exclusive_scan`.  We also propose parallel and non-parallel convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`.

### Current set of numeric algorithms

<a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a>, "C++ Parallel Range Algorithms," is in the last stages of wording review as of the publication date.  P3179R8 explicitly defers adding ranges versions of the numeric algorithms.  This proposal does that.  As such, we focus on the numeric algorithms, that is, the 11 algorithms in <a href="https://eel.is/c++draft/numeric.ops">[numeric.ops]</a>.

* `iota`

* `accumulate`

* `inner_product`

* `partial_sum`

* `adjacent_difference`

* `reduce`

* `inclusive_scan`

* `exclusive_scan`

* `transform_reduce`

* `transform_inclusive_scan`

* `transform_exclusive_scan`

We don't have to add ranges versions of all these algorithms.  Several already have a ranges version in C++23, possibly with a different name.  Some others could be omitted because they have straightforward replacements using existing views and other ranges algorithms.  We base our algorithm selection decisions on <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P2760R1">P2760R1</a>, "A Plan for C++26 Ranges."  As P2214R2 explains, "one of the big motivations for Ranges was the ability to actually compose algorithms."  It's idiomatic for ranges to use views and projections where possible, instead of creating new algorithms.  For P2214, see in particular Section 5.  (Note that the table at the start of the section lists `shift_left` and `shift_right`.  These are not numeric algorithms, and <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> adds parallel ranges versions of them.)

### Algorithms that already have ranges versions

The following numeric algorithms already have ranges versions in C++23.

* `iota` already has a ranges version in C++23.  P3179R8 adds a parallel version.

* `accumulate` performs operations sequentially.  Its parallel version is `reduce`, which we propose here.  The non-parallel version has been translated in C++23 into `fold_left`.

### Algorithms with a ranges proposal in flight

The following algorithm has a proposal in flight to add a ranges analog.

* `partial_sum` performs operations sequentially.  Its parallel analogs are `inclusive_scan` and `exclusive_scan`, which we propose here.  For the non-parallel ranges version that returns a stateful binary operator, <a href="https://wg21.link/P2760R1">P2760R1</a> suggests a view instead of an algorithm.  <a href="https://wg21.link/P3351R2">P3351R2</a>, "`views::scan`," proposes this view.  P3351R2 is currently in SG9 (Ranges Study Group) review.

### Algorithms that do not need ranges versions

The following algorithms do not need ranges versions, since they can be replaced with existing views and ranges algorithms.

* `inner_product` performs operations sequentially.  It can be replaced with existing views and ranges algorithms, e.g., `ranges::fold_left(views::zip_transform(std::multiplies(), x, y), 0.0, std::plus())`.  P2214R2 argues against adding a ranges analog of `inner_product`, because it is less fundamental than other algorithms, and because it's not clear how to incorporate projections.

* `adjacent_difference` can be replaced with a combination of  `adjacent_transform_view` (which was adopted in C++23) and `ranges::copy`.  In our experience, adjacent differences or their generalization are often used in combination with other ranges.  For example, finite-difference methods for solving time-dependent differential equations may need to add together multiple ranges, each of which is an adjacent difference possibly composed with other functions.  One could represent the spatial finite difference scheme for a partial differential equation as a weighted sum of adjacent differences in each spatial degree of freedom.  Thus, for us a view would make more sense than a "terminal" algorithm.  The actual algorithm is a transform or copy from a complicated view into an output range.

* `transform_reduce`, `transform_inclusive_scan`, and `transform_exclusive_scan` can be replaced with a combination of `transform_view` and `reduce`, `inclusive_scan`, or `exclusive_scan`.  P2214R2 points out that `ranges::transform_inclusive_scan(r, o, f, g)` can be rewritten as `ranges::inclusive_scan(r | views::transform(g), o, f)`, and that the latter saves users from needing to remember which of `f` and `g` is the transform (unary) operation, and which is the binary operation.

### We don't propose `reduce_first`

<a href="https://wg21.link/P2760R1">P2760R1</a> additionally asks whether there should be a `reduce_first` algorithm, analogous to `fold_left_first`, for binary operations like `min` that lack a natural initial value.  We do not propose this for three reasons.  First, P3179R8 already proposes parallel ranges overloads of `min_element`, `max_element`, and `minmax_element`.  Second, users can always extract the first element from the sequence and use it as the initial value in `reduce`.  Third, sometimes users can pick a flag or identity initial value, like `-Inf` for `min` over floating-point values, that makes sense for use in `reduce`.

### Algorithms that we propose here

This leaves three algorithms, which we propose here: `reduce`, `inclusive_scan`, and `exclusive_scan`.  <a href="https://wg21.link/P2760R1">P2760R1</a> proposes convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`; we propose parallel and non-parallel overloads of these here as well.

## `ranges::reduce` design

### No default parameters

"One thing is clear: `ranges::reduce` should *not* take a default binary operation *nor* a default initial parameter. The user needs to supply both" -- Section 5.1 of P2760R1.  This motivates the convenience wrappers

* `ranges::sum(r)` for `ranges::reduce(r, plus{}, range_value_t<R>())`, and

* `ranges::product(r)` for `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`.

One argument *for* a default initial value in `std::reduce` is that `int` literals like `0` or `1` do not behave in the expected way with a sequence of `float` or `double`.  Changing the return value type behavior of `ranges::reduce` to imitate `ranges::fold_left` instead of `std::reduce` fixes that.

### Return type?

#### Imitate `ranges::fold_left`, not `std::reduce`

For `ranges::reduce`, we deduce the return type (of the reduction result, a single value) in the same way that `std::ranges::fold_left`, instead of always returning the initial value type `T` like `std::reduce`.

<a href="https://wg21.link/P2322R6">P2322R6</a>, "`ranges::fold`," added the various `fold_*` ranges algorithms to C++23.  This proposal explains why `std::ranges::fold_left` may return a different reduction type than `std::reduce` for the same input range, initial value, and binary operation.  Consider the following example, adapted from Section 3 of P2322R6 (<a href="https://godbolt.org/z/3q71EMTPa">Compiler Explorer link</a>).

```c++
#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <ranges>
#include <type_traits>
#include <vector>

int main() {
  std::vector<double> v = {0.25, 0.75};
  {
    auto r = std::reduce(v.begin(), v.end(), 1, std::plus());
    static_assert(std::is_same_v<decltype(r), int>);
    assert(r == 1);
  }
  {
    auto r = std::ranges::fold_left(v, 1, std::plus());
    static_assert(std::is_same_v<decltype(r), double>);
    assert(r == 2.0);
  }
  return 0;  
}
```

The `std::reduce` part of the example expresses a common user error.  `ranges::fold_*` instead returns "the decayed result of invoking the binary operation with `T` (the initial value) and the reference type of the range."  For the above example, this likely expresses what the user meant.  It also works for other common cases, like proxy reference types with an unambiguous conversion to a common type with the initial value.

#### Return just the value, not `in_value_result` like `fold_*`

The reason for an algorithm to return the input iterator is because it's an `input_iterator` -- that is, it doesn't offer the multipass guarantee, so the state of the iterator may matter to the caller.  The parallel algorithms take forward iterators at least, so this question only applies to non-parallel algorithms.  Our view is that `reduce` can combine the elements of the range in any order, so if users have iterators that aren't random access iterators, they probably should just call `fold_left`.  As a result, we specify `ranges::reduce` just to return the value, not to return `in_value_result` like `fold_left`.

### Support projections

We propose that `ranges::reduce` take a projection parameter, unlike `ranges::fold_left`.  Section 4.6 of <a href="https://wg21.link/P2322R6">P2322R6</a> explains that the only reason `ranges::fold_left` does *not* take a projection is for consistency with `ranges::fold_left_first`.  The latter does not take a projection in order to avoid an extra copy of the leftmost value, that would be required in order to support projections with a range whose iterators yield proxy reference types like `tuple<T&>` (as `views::zip` does).  P2322R6 clarifies that `ranges::fold_left` does not have this problem, because it never needs to materialize an input value; it can just project each element at iterator `iter` via `invoke(proj, *iter)`, and feed that directly into the binary operation.

## Input and output ranges

We follow the approach of P3179R8.  For existing non-ranges algorithms that take iterator pairs as input and return a value, their ranges versions just return a value.  This covers `reduce`.  The non-ranges versions of `inclusive_scan` and `exclusive_scan` take an input range and an output iterator, and return an iterator to the element past the last element written.  This works more or less like `ranges::transform`, so we imitate the approach in P3179R8 by having those algorithms take an input range and an output range (both sized), and return an alias to `in_out_result`.  (If the output doesn't suffice to contain the input, then callers need to know what part of the input hasn't yet been processed.)  These algorithms have freedom to reorder binary operations and can copy the binary operator, so there is no value in returning it (as a way for callers to get at any of its modified state).

## Constexpr parallel algorithms?

<a href="https://wg21.link/p2902r1">P2902R1</a> proposes to add `constexpr` to the parallel algorithms.  P3179R8 does not object to this; see Section 2.10.  We continue the approach of P3179R8 in not opposing P2902R1's approach, but also not depending on it.

# Implementation

TODO

The oneDPL library has deployment experience.

# Wording

> Text in blockquotes is not proposed wording, but rather instructions for generating proposed wording.
>
> Assume that <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> has been applied to the Working Draft.

## Update feature test macro

> In [version.syn], increase the value of the `__cpp_lib_parallel_algorithm` macro by replacing YYYMML below with the integer literal encoding the appropriate year (YYYY) and month (MM).

```c++
#define __cpp_lib_parallel_algorithm YYYYMML // also in <algorithm>
```

## Change [numeric.ops.overview]

> Change [numeric.ops.overview] (the `<numeric>` header synopsis) as follows.

### Add declaration of exposition-only concepts

> Add declarations of exposition-only concepts _`indirectly-binary-foldable-impl`_ and _`indirectly-binary-foldable`_ to [numeric.ops.overview] (the `<numeric>` header synopsis) as follows.

```
    template<class F, class T, class I, class U>
      concept @_indirectly-binary-left-foldable-impl_@ =  // @_exposition only_@
        movable<T> && movable<U> &&
        convertible_to<T, U> && invocable<F&, U, iter_reference_t<I>> &&
        assignable_from<U&, invoke_result_t<F&, U, iter_reference_t<I>>>;
```
::: add
    template<class F, class T, class I, class U>
      concept @_indirectly-binary-foldable-impl_@ =       // @_exposition only_@
        movable<T> && movable<U> &&
        convertible_to<T, U> &&
        invocable<F&, U, iter_reference_t<I>> &&
        assignable_from<U&, invoke_result_t<F&, U, iter_reference_t<I>>> &&
        invocable<F&, iter_reference_t<I>, U> &&
        assignable_from<U&, invoke_result_t<F&, iter_reference_t<I>, U>>;
:::
```
    template<class F, class T, class I>
      concept @_indirectly-binary-left-foldable_@ =      // @_exposition only_@
        copy_constructible<F> && indirectly_readable<I> &&
        invocable<F&, T, iter_reference_t<I>> &&
        convertible_to<invoke_result_t<F&, T, iter_reference_t<I>>,
               decay_t<invoke_result_t<F&, T, iter_reference_t<I>>>> &&
        @_indirectly-binary-left-foldable-impl_@<F, T, I,
                        decay_t<invoke_result_t<F&, T, iter_reference_t<I>>>>;

    template<class F, class T, class I>
      concept @_indirectly-binary-right-foldable_@ =    // @_exposition only_@
        indirectly-binary-left-foldable<@_flipped_@<F>, T, I>;
```
::: add
    template<class F, class T, class I>
      concept @_indirectly-binary-foldable_@ =           // @_exposition only_@
        copy_constructible<F> && indirectly_readable<I> &&
        invocable<F&, T, iter_reference_t<I>> &&
        convertible_to<invoke_result_t<F&, T, iter_reference_t<I>>,
          decay_t<invoke_result_t<F&, T, iter_reference_t<I>>>> &&
        invocable<F&, iter_reference_t<I>, T> &&
        convertible_to<invoke_result_t<F&, iter_reference_t<I>, T>,
          decay_t<invoke_result_t<F&, iter_reference_t<I>, T>>> &&
        @_indirectly-binary-foldable-impl_@<F, T, I,
          decay_t<invoke_result_t<F&, T, iter_reference_t<I>>>>;
:::
```
    template<input_iterator I, sentinel_for<I> S, class T = iter_value_t<I>,
             indirectly-binary-left-foldable<T, I> F>
      constexpr auto fold_left(I first, S last, T init, F f);
```

### Add declarations of parallel ranges `reduce` overloads

> Add declarations of ranges overloads of `reduce`, `sum`, and `product` algorithms to [numeric.ops.overview] (the `<numeric>` header synopsis) as follows.

TODO: Check that using `projected<I, Proj>` instead of `I` in _`indirectly-binary_foldable`_ is the right way to handle projections.

```
  template<class ExecutionPolicy, class ForwardIterator, class T, class BinaryOperation>
    T reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
             ForwardIterator first, ForwardIterator last, T init, BinaryOperation binary_op);
```
::: add
```
  namespace ranges {

  // Non-parallel overloads of reduce

  template<random_access_iterator I,
           sized_sentinel_for<I> S,
           class T,
           class Proj = identity,
           @_indirectly-binary-foldable_@<T, projected<I, Proj>> F>
      constexpr auto reduce(I first, S last, T init, F binary_op,
                            Proj proj = {}) -> /* @_see below_@ */;
  template<random_access_iterator I,
           @_sized-random-access-range_@ R,
           class Proj = identity,
           @_indirectly-binary-foldable_@<T, projected<ranges::iterator_t<R>, Proj>> F>
      constexpr auto reduce(R&& r, T init, F binary_op,
                            Proj proj = {}) -> /* @_see below_@ */;

  // Parallel overloads of reduce

  template<@_execution-policy_@ ExecutionPolicy,
           random_access_iterator I,
           sized_sentinel_for<I> S,
           class T,
           class Proj = identity,
           @_indirectly-binary-foldable_@<T, projected<I, Proj>> F>
      auto reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
                  I first, S last, T init, F binary_op,
                  Proj proj = {}) -> /* @_see below_@ */;
  template<@_execution-policy_@ ExecutionPolicy,
           @_sized-random-access-range_@ R,
           class T,
           class Proj = identity,
           @_indirectly-binary-foldable_@<T, projected<ranges::iterator_t<R>, Proj>> F>
      auto reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
                  R&& r, T init, F binary_op,
                  Proj proj = {}) -> /* @_see below_@ */;

  // Non-parallel overloads of sum

  template<random_access_iterator I,
           sized_sentinel_for<I> S,
           class Proj = identity>
    requires /* @_see below_@ */
      constexpr auto sum(I first, S last, Proj proj = {})
        -> /* @_see below_@ */;
  template<@_sized-random-access-range_@ R>
    requires /* @_see below_@ */
      constexpr auto sum(R&& r, Proj proj = {})
        -> /* @_see below_@ */;

  // Parallel overloads of sum

  template<@_execution-policy_@ ExecutionPolicy,
           random_access_iterator I,
           sized_sentinel_for<I> S,
           class Proj = identity>
    requires /* @_see below_@ */
      auto sum(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
               I first, S last, Proj proj = {})
        -> /* @_see below_@ */;
  template<@_execution-policy_@ ExecutionPolicy,
           @_sized-random-access-range_@ R,
           class T,
           class Proj = identity>
    requires /* @_see below_@ */
      auto sum(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
               R&& r, Proj proj = {})
        -> /* @_see below_@ */;

  // Non-parallel overloads of product

  template<random_access_iterator I,
           sized_sentinel_for<I> S,
           class Proj = identity>
    requires /* @_see below_@ */
      constexpr auto product(I first, S last, Proj proj = {})
        -> /* @_see below_@ */;
  template<@_sized-random-access-range_@ R>
    requires /* @_see below_@ */
      constexpr auto product(R&& r, Proj proj = {})
        -> /* @_see below_@ */;

  // Parallel overloads of product

  template<@_execution-policy_@ ExecutionPolicy,
           random_access_iterator I,
           sized_sentinel_for<I> S,
           class Proj = identity>
    requires /* @_see below_@ */
      auto product(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
                   I first, S last, Proj proj = {})
        -> /* @_see below_@ */;
  template<@_execution-policy_@ ExecutionPolicy,
           @_sized-random-access-range_@ R,
           class Proj = identity>
    requires /* @_see below_@ */
      auto product(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
                   R&& r, Proj proj = {})
        -> /* @_see below_@ */;

  } // namespace ranges
```
:::
```
  // [inner.product], inner product
  template<class InputIterator1, class InputIterator2, class T>
    constexpr T inner_product(InputIterator1 first1, InputIterator1 last1,
                              InputIterator2 first2, T init);
```

### Add declarations of parallel ranges `inclusive_scan`

TODO

### Add declarations of parallel ranges `exclusive_scan`

TODO

### Add wording for algorithms

TODO
