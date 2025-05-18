
---
title: Parallel numeric range algorithms
document: P????R0
date: 2025-05-16
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

<a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a>, which is in the last stages of wording review, defines parallel versions of many ranges algorithms in the C++ Standard Library.  (The "parallel version of an algorithm" is an overload whose first parameter is an execution policy.)  That proposal restricts itself to adding parallel versions of existing ranges algorithms.  It explicitly defers adding overloads to the numeric algorithms in [numeric.ops], because these do not yet have ranges versions.  Our proposal fills that gap.

WG21 did not have time to propose ranges-based numeric algorithms with the initial set of ranges algorithms in C++20.  <a href="https://wg21.link/P1813R0">P1813R0</a>, "A Concept Design for the Numeric Algorithms," points out the challenge of defining ranges versions of the existing parallel numeric algorithms.  What makes this task less straightforward is that the specification of the parallel numeric algorithms permits them to reorder binary operations like addition.  This matters because many useful number types do not have associative addition.  Lack of associativity is not just a floating-point rounding error issue; one example is saturating integer arithmetic.  Ranges algorithms are constrained by concepts, but it's not clear even if it's a good idea to define concepts that can express permission to reorder terms in a sum.

C++17 takes the approach of saying that parallel numeric algorithms can reorder the binary operations however they like, but does not say whether any reordering would give the same results as any other reordering.  The Standard expresses this through the wording "macros" *GENERALIZED_NONCOMMUTATIVE_SUM* and *GENERALIZED_SUM*.  (A wording macro is a convenience for wording the Standard; it's not necessarily a preprocessor macro and might not even be implementable as such.)  Algorithms become ill-formed, no diagnostic required if the element types do not define the required operations.  P1813R0 instead defines C++ concepts that represent algebraic structures, all of which involve a set with a closed binary operation.  Some of the structures require that the operation be associative and/or commutative.  P1813R0 uses those concepts to constrain the algorithms.  This means that the algorithms will not be selected for overload resolution if the element types do not define the required operations.  It further means that algorithms could dispatch based on properties like whether the element type's binary operation is commutative.

WG21 has not expressed a consensus on P1813R0's approach.  LEWGI reviewed P1813R0 at the Belfast meeting in November 2019, but did not forward the proposal and wanted to see it again.  Two other proposals express something more like WG21's consensus on constraining the numeric algorithms: <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P1673R13">P1673R13</a>, "A free function linear algebra interface based on the BLAS," which defines mdspan-based analogs of the numeric algorithms.  Section 5.1.1 of P2214R2 points out that P1813R0's approach would overconstrain `fold`; P2214R2 instead suggests just constraining the operation to be binary invocable.  This was ultimately the approach taken by the Standard through the exposition-only concepts _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  Section 5.1.2 of P2214R2 says that `reduce` "calls for the kinds of constrains that P1813R0 is proposing."

<a href="https://wg21.link/P1673R13">P1673R13</a>, which was adopted into the Working Draft for C++26 as [linalg], took an entirely different approach for its set of `mdspan`-based numeric algorithms.  Section 10.8, "Constraining matrix and vector element types and scalars," explains the argument.  Here is a summary.

1. Requirements like associativity are too strict to be useful for practical types.  The only number types in the Standard with associative addition are unsigned integers.  It's not just a rounding error "epsilon" issue; sums of saturating integers can have infinite error if one assumes associativity.

2. "The algorithm may reorder sums" (which is what we want to say) means something different than "addition on the terms in the sum is associative" (which is not true for many number types of interest).  That is, permission for an algorithm to reparenthesize sums is not the same as a concept constraining the terms in the sum.

3. P1813R0 defines concepts that generalize a mathematical group.  These are only useful for describing a single set of numbers, that is, one type.  This excludes useful features like mixed precision (e.g., where the result type in `reduce` differs from the range's element type) and types that use expression templates.  One could imagine generalizing this to a set of types that have a common type, but this can be too restrictive; Section 5.1.1 of <a href="https://wg21.link/P2214R2">P2214R2</a> gives an example involving two types in a fold that do not have a common type.

P1673R13 says that algorithms have complete freedom to create temporary copies or value-initialized temporary objects, rearrange addends and partial sums arbitrarily, or perform assignments in any order, as long as this would produce the result specified by the algorithm's *Effects* and *Remarks* when operating on elements of a semiring.  The `linalg::dot` ([linalg.algs.blas1.dot]) and `linalg::vector_abs_sum` ([linalg.algs.blas1.asum]) algorithms specifically define the returned result(s) in terms of *GENERALIZED_SUM*.  Those algorithms do that because they need to constrain the precision of intermediate terms in the sum (so they need to define those terms).  In our case, the Standard already uses *GENERALIZED_SUM* and *GENERALIZED_NONCOMMUTATIVE_SUM* to define ranges algorithms like `reduce`, `inclusive_scan`, and `exclusive_scan`.  We can just adapt this wording to talk about ranges instead of iterators.  This lets us imitate the approach of <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> in adding ranges overloads.

Our approach combines the syntactic constraints used for the `fold_*` family of algorithms, with the semantic approach of P1673R13 and the C++17 parallel numeric algorithms.  For example, we constrain `reduce`'s binary operation with both _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  (This expresses that if the binary operation is called with an argument of the initial value's type `T`, then that argument can be in either the first or second position.)  We express what `reduce` does using *GENERALIZED_SUM*.

## Noncommutative parallel reductions?

The current numeric algorithms express a variety of permissions to reorder binary operations.

1. `accumulate` and `partial_sum` both precisely specify the order of binary operations as sequential, from left to right.  This works even if the binary operation is neither associative nor commutative.

2. The various `*_scan` algorithms can reorder binary operations as if they are associative (e.g., they may replace `a + (b + c)` with `(a + b) + c`), but not as if they are commutative.

3. `reduce` can reorder binary operations as if they are both associative and commutative.

The Standard lacks an analog of `reduce` that can assume associativity but not commutativity of binary operations.  Parallel reduction operations with these assumptions exist in other programming models.  For example, MPI (the Message Passing Interface for distributed-memory parallel communication) has a function `MPI_Create_op` for defining custom reduction operators from a user's function.  `MPI_Create_op` has a parameter that specifies whether MPI may assume that the user's function is commutative.

A concepts-based approach like P1813R0's could permit specializing `reduce` on whether the user asserts that the binary operation is commutative.  P1813R0 does not attempt to do this; it merely specializes `reduce` on whether the associative and commutative operation has a two-sided identity element.  Furthermore, P1813R0 does not offer a way for users to assert that an operation is associative or commutative, because the `magma` (nonassociative) and `semigroup` (associative) concepts do not differ syntactically.  One could imagine a refinement of this design that includes a trait for users to specialize on the type of their binary operation, say `is_commutative<BinaryOp>`.  This would be analogous to the `two_sided_identity` trait in P1813R0 that lets users declare that their set forms a monoid, a refinement of `semigroup` with a two-sided identity element.

We do not propose filling this gap in the Standard parallel algorithms, but would not object to proposals that do so.  We think the right way would be to propose a new algorithm with a distinct name.  `fold` and `reduce` have different names; this new algorithm should have a different name as well.

## What algorithms to include?

We propose ranges overloads (both parallel and nonparallel) of only three algorithms: `reduce`, `inclusive_scan`, and `exclusive_scan`.  We also propose parallel and non-parallel convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`.

We base our reasoning on <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P2760R1">P2760R1</a>, "A Plan for C++26 Ranges."  As P2214R2 explains, "one of the big motivations for Ranges was the ability to actually compose algorithms."  It's idiomatic for ranges to use views and projections where possible, instead of creating new algorithms.  This means that we do not need to propose ranges versions of all the parallel numeric algorithms.

Section 5 of P2214R2 discusses prioritization of range-ifying the remaining algorithms, which it lists in a table at the start of the section.  Of these, `shift_left` and `shift_right` are not numeric algorithms, and <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> adds parallel ranges versions of them.  Five categories of numeric algorithms already have ranges versions in C++23, or do not need them.

1. `iota` already has a ranges version in C++23.  P3179R8 adds a parallel version.

2. `accumulate` performs operations sequentially, from left to right.  It has been translated in C++23 into `fold_left`.

3. `inner_product` performs operations sequentially, from left to right.  It does not need a ranges version, since it can be replaced with existing views and ranges algorithms, e.g., `ranges::fold_left(views::zip_transform(std::multiplies(), x, y), 0.0, std::plus())`.

4. `partial_sum` performs operations sequentially, from left to right.  <a href="https://wg21.link/P2760R1">P2760R1</a> suggests replacing it with a view that implements an ordered partial sum with a stateful binary operator.  <a href="https://wg21.link/P3351R2">P3351R2</a>, "`views::scan`," proposes this view.  P3351R2 is currently in SG9 (Ranges Study Group) review.

5. `transform_reduce`, `transform_inclusive_scan`, and `transform_exclusive_scan` do not need ranges versions, since they can be replaced with a combination of `transform_view` and `reduce`, `inclusive_scan`, or `exclusive_scan`.

6. `adjacent_difference` does not need a ranges version, since it can be replaced with a combination of  `adjacent_transform_view` (which was adopted in C++23) and `ranges::copy`.  `adjacent_transform_view` can be both a random access range and a sized range, if its underlying view is both.

This leaves three algorithms, which we propose here: `reduce`, `inclusive_scan`, and `exclusive_scan`.  <a href="https://wg21.link/P2760R1">P2760R1</a> proposes convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`; we propose parallel and non-parallel overloads of these here as well.

## We don't propose `reduce_first`

<a href="https://wg21.link/P2760R1">P2760R1</a> additionally asks whether there should be a `reduce_first` algorithm, analogous to `fold_left_first`, for binary operations like `min` that lack a natural initial value.  We do not propose this for three reasons.  First, P3179R8 already proposes parallel ranges overloads of `min_element`, `max_element`, and `minmax_element`.  Second, users can always extract the first element from the sequence and use it as the initial value in `reduce`.  Third, sometimes users can pick a flag or identity initial value, like `-Inf` for `min` over floating-point values, that makes sense for use in `reduce`.

## Ranges input and output

We follow the approach of P3179R8.  For existing non-ranges algorithms that take iterator pairs as input and return a value, their ranges versions also just return a value.  This covers `reduce`.  The non-ranges versions of `inclusive_scan` and `exclusive_scan` take an input range and an output iterator, and return an iterator to the element past the last element written.  This works more or less like `ranges::transform`, so we imitate the approach in P3179R8 by having those algorithms take an input range and an output range (both sized), and return an alias to `in_out_result`.  These algorithms have freedom to reorder binary operations and can copy the binary operator, so there is no value in returning it (as a way for callers to get at any of its modified state).

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

### Add declarations of parallel ranges `reduce` overloads

```
  template<class ExecutionPolicy, class ForwardIterator, class T, class BinaryOperation>
    T reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
             ForwardIterator first, ForwardIterator last, T init, BinaryOperation binary_op);
```
::: add
  namespace ranges {

  template<@_execution-policy_@ ExecutionPolicy,
           random_access_iterator I,
           sized_sentinel_for<I> S,
           class Proj = identity,
           class T = iter_value_t<I>,
           class BinaryOperation>
    requires @_indirectly-binary-left-foldable_@<BinaryOperation> &&
             @_indirectly-binary-right-foldable_@<BinaryOperation> &&
             /* TODO MAYBE OTHER CONSTRAINTS */
      T reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
               I first, S last, T init, BinaryOperation binary_op);
  template<@_execution-policy_@ ExecutionPolicy,
           @_sized-random-access-range_@ R,
           class Proj = identity,
           class T = iter_value_t<I>,
           class BinaryOperation>
    requires @_indirectly-binary-left-foldable_@<BinaryOperation> &&
             @_indirectly-binary-right-foldable_@<BinaryOperation> &&
             /* TODO MAYBE OTHER CONSTRAINTS */
      T reduce(ExecutionPolicy&& exec, // @_freestanding-deleted, see [algorithms.parallel.overloads]_@
               I first, S last, T init, BinaryOperation binary_op);

  // TODO ADD SPECIAL CASES ranges::sum and ranges::product
  
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
