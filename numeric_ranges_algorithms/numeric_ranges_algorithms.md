
---
title: Parallel and non-parallel numeric range algorithms
document: DXXXXR0
date: 2025-05-19
audience: SG1,SG9,LEWG
author:
  - name: Ruslan Arutyunyan
    email: <ruslan.arutyunyan@intel.com>

  - name: Mark Hoemmen
    email: <mhoemmen@nvidia.com>

  - name: Alexey Kukanov
    email: <alexey.kukanov@intel.com>

  - name: Bryce Adelstein Lelbach
    email: <brycelelbach@gmail.com>

  - name: Abhilash Majumder
    email: <abmajumder@nvidia.com>
toc: true
---

# Authors

* Ruslan Arutyunyan (Intel)

* Mark Hoemmen (NVIDIA)

* Alexey Kukanov (Intel)

* Bryce Adelstein Lelbach (NVIDIA)

* Abhilash Majumder (NVIDIA)

# Revision history

* Revision 0 to be submitted 2025-??-??

# Abstract

We propose ranges overloads (both parallel and nonparallel) of the following algorithms.

* `reduce`, unary `transform_reduce`, and binary `transform_reduce`

* `inclusive_scan` and `transform_inclusive_scan`

* `exclusive_scan` and `transform_exclusive_scan`

We also propose adding convenience wrappers `ranges::sum` and `ranges::product` for special cases of `reduce` with addition and multiplication, respectively.

# Design

## What algorithms to include?

### What we propose

We propose ranges overloads (both parallel and nonparallel) of the following algorithms.

* `reduce`, unary `transform_reduce`, and binary `transform_reduce`

* `inclusive_scan` and `transform_inclusive_scan`

* `exclusive_scan` and `transform_exclusive_scan`

We also propose parallel and non-parallel convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`.

The following sections explain why we propose these algorithms and not others.  This relates to other aspects of the design besides algorithm selection, such as whether to include optional projection parameters.

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

We don't have to add ranges versions of all these algorithms.  Several already have a ranges version in C++23, possibly with a different name.  Some others could be omitted because they have straightforward replacements using existing views and other ranges algorithms.  We carefully read the two proposals <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P2760R1">P2760R1</a>, "A Plan for C++26 Ranges," in order to inform our algorithm selections.  In some cases that we will explain below, usability and performance concerns led us to disagree with their conclusions.

### `transform_*` algorithms (and/or projections)

#### Summary

We propose

* providing both unary and binary `ranges::transform_reduce` as well as `ranges::reduce`,

* providing `ranges::transform_{in,ex}clusive_scan` as well as `ranges::{in,ex}clusive_scan`, and

* *not* providing projections for any of these algorithms.

#### Do we want `transform_*` algorithms and/or projections?

We start with two questions.

1. Should the existing C++17 algorithms `transform_reduce`, `transform_inclusive_scan`, and `transform_exclusive_scan` have ranges versions, or does it suffice to have ranges versions of `reduce`, `inclusive_scan`, and `exclusive_scan`?

2. Should ranges versions of `reduce`, `inclusive_scan`, and `exclusive_scan` take optional projections, just like `ranges::for_each` and other ranges algorithms do?

We use words like "should" because the ranges library doesn't actually *need* `transform_*` algorithms or projections for functional completeness.  These questions are about usability and optimization, including the way that certain kinds of ranges constructs can hinder parallelization on different kinds of hardware.

#### Unary transforms, projections, and `transform_view` are functionally equivalent

The above two questions are related, since a projection can have the same effect as a `transform_*` function.  This aligns with <a href="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4128.html#algorithms-should-take-invokable-projections">Section 13.2 of N4128</a>, which explains why ranges algorithms take optional projections "everywhere it makes sense."

> Wherever appropriate, algorithms should optionally take *INVOKE*-able *projections* that are applied to each element in the input sequence(s). This, in effect, allows users to trivially transform each input sequence for the sake of that single algorithm invocation.

Projecting the input of `reduce` has the same effect as unary `transform_reduce`.  Here is an example, in which `get_element` is a customization point object like the one proposed in <a href="https://wg21.link/p2769r3">P2769R3</a>, such that `get_element<k>` gets the `k`-th element of an object that participates in the tuple or structured binding protocol.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
const int init = 3;
auto result_proj =
  std::ranges::reduce(v1, init, std::plus{}, get_element<0>{});
assert(result_proj == 26);
auto result_xform =
  std::ranges::transform_reduce(v1, init, std::plus{}, get_element<0>{});
assert(result_xform == 26);
```

Even without projections, the `transform_*` algorithms can be replaced by a combination of `transform_view` and the non-`transform` algorithm.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
const int init = 3;
auto result_tv = std::ranges::reduce(
  std::views::transform(v1, get_element<0>{}), init, std::plus{});
assert(result_tv == 26);
```

#### Scan algorithms work like unary `transform_reduce`

Regarding scan algorithms, <a href="https://wg21.link/p2214r2">P2214R2</a> points out that `ranges::transform_inclusive_scan(r, o, f, g)` can be rewritten as `ranges::inclusive_scan(r | views::transform(g), o, f)`.  The latter formulation saves users from needing to remember which of `f` and `g` is the transform (unary) operation, and which is the binary operation.  Making the ranges version of the algorithm take an optional projection would be exactly equivalent to adding a `transform_*` version that does not take a projection: e.g., `ranges::inclusive_scan(r, o, f, g)` with `g` as the projection would do exactly the same thing as `ranges::transform_inclusive_scan(r, o, f, g)` with `g` as the transform operation.

#### Binary `transform_reduce` is functionally equivalent to `reduce` and `zip_transform_view`

Expressing binary `transform_reduce` using only `reduce` requires `zip_transform_view` or something like it.

#### Study `ranges::transform` for design hints

Questions about transforms and projections suggest studying `ranges::transform` for design hints.  This leads us to two more questions.

1. If transforms and projections are equivalent, then why does `std::ranges::transform` take an optional projection?

2. If binary transform is equivalent to unary transform of a `zip_transform_view`, then why does binary `std::ranges::transform` exist?

It can help to look at examples.  Here is a binary transform example without projections, that uses a single big lambda.  Users have to read the big lambda to see what it does.  So does the compiler, which can hinder optimization if it's not good at inlining.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
std::vector<std::pair<int, std::string>> v2{
  {13, "thirteen"}, {17, "seventeen"}, {19, "nineteen"}};
std::vector<int> out(std::from_range, std::views::repeat(0, 3));

// Without projections: Big, opaque lambda
std::ranges::transform(v1, v2, out.begin(),
  [] (auto x, auto y) { return get<0>(x) + get<0>(y); });

std::vector<int> expected{65, 119, 209};
assert(out == expected);
```

Constrast this with use of projections.  Users can read out loud what this does.  It also separates the "selection" or "query" part of the transform from the "arithmetic" or "computation" part.  The power of the ranges abstraction is that users can factor computation on a range from the logic to iterate over that range.  It's natural to extend this separation to selection logic as well.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
std::vector<std::pair<int, std::string>> v2{
  {13, "thirteen"}, {17, "seventeen"}, {19, "nineteen"}};
std::vector<int> out(std::from_range, std::views::repeat(0, 3));

// With projections: More readable
std::ranges::transform(v1, v2, out.begin(),
  std::plus{}, get_element<0>{}, get_element<0>{});

std::vector<int> expected{65, 119, 209};
assert(out == expected);
```

It's harder to avoid a lambda in the unary transform case.  Most of the named C++ Standard Library arithmetic function objects are binary.  Currying them into unary functions in C++ requires either making a lambda (which defeats the purpose somewhat) or using something like `std::bind` (which is verbose).  On the other hand, using a projection still has the benefit of separating the "selection" part of the transform from the "computation" part.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
std::vector<int> out(std::from_range, std::views::repeat(0, 3));
std::vector<int> expected{6, 8, 12};

// Unary transform without projection
std::ranges::transform(v1, out.begin(), [] (auto x) { return get<0>(x) + 1; });
assert(out == expected);

// Unary transform with projection
std::ranges::transform(v1, out.begin(), [] (auto x) { return x + 1; }, get_element<0>{});
assert(out == expected);

// Unary transform with projection and "curried" plus
std::ranges::transform(v1, out.begin(), std::bind(std::plus{}, 1), get_element<0>{});
assert(out == expected);
```

#### `reduce`: transforms and projections

We return to the `reduce` examples we showed above, but this time, we focus on their readability.

##### Unary `transform_reduce`

A `ranges::reduce` that takes a projection is functionally equivalent to unary `transform_reduce` without a projection.  If ranges algorithms take projections whenever possible, then the name `transform_reduce` is redundant here.  Readers should know that any extra function argument of a ranges algorithm is most likely a projection.  Either way -- `reduce` with projection, or unary `transform_reduce` -- is straightforward to read, and separates selection (`get_element<0>`) from computation (`std::plus`).

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
const int init = 3;

// reduce with projection get_element<0>
auto result_proj =
  std::ranges::reduce(v1, init, std::plus{}, get_element<0>{});
assert(result_proj == 26);

// transform_reduce with transform get_element<0>
auto result_xform =
  std::ranges::transform_reduce(v1, init, std::plus{}, get_element<0>{});
assert(result_xform == 26);

// reduce with transform_view (no projection)
auto result_xv = std::ranges::reduce(
  std::views::transform(v1, get_element<0>{}), init, std::plus{});
assert(result_xv == 26);
```

On the other hand, ranges algorithms take projections whenever possible, and `std::ranges::transform` takes a projection.  Why can't `transform_reduce` take a projection?  For unary `transform_reduce`, this arguably makes the order of operations less clear.  The projection happens first, but most users would have to think about that.  A lambda or named function would improve readability.

```c++
struct bar {
  std::string s;
  int i;
};
std::vector<std::tuple<int, std::string, bar>> v{
  { 5,   "five", {"x", 13}},
  { 7,  "seven", {"y", 17}},
  {11, "eleven", {"z", 19}}};
const int init = 3;

// first get bar, then get bar::i
auto result_proj = std::ranges::transform_reduce(
  v, init, std::plus{}, get_element<1>{}, get_element<2>{});
assert(result_proj == 52);

// first get bar, then get bar::i
auto getter = [] (auto t) {
  return get_element<1>{}(get_element<2>{}(t));
};
auto result_no_proj = std::ranges::transform_reduce(
  v, init, std::plus{}, getter);
assert(result_no_proj == 52);
```

##### Binary `transform_reduce`

Expressing binary `transform_reduce` using only `reduce` requires `zip_transform_view` or something like it.  The `reduce`-only version is more verbose.  On the other hand, it's a toss-up which version is easier to understand.  Users either need to learn what a "zip transform view" does, or they need to learn about `transform_reduce` and know which of the two function arguments does what.  They may also find it troublesome that `zip_view` and `zip_transform_view` are not pipeable: there is no `{v1, v2} | views::zip` syntax, for example.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
std::vector<std::pair<std::string, int>> v2{
  {"thirteen", 13}, {"seventeen", 17}, {"nineteen", 19}};
const int init = 3; 
std::vector<int> out(std::from_range, std::views::repeat(0, 3));

auto result_bztv = std::ranges::reduce(
  std::views::zip_transform(std::multiplies{},
    std::views::transform(v1, get_element<0>{}),
    std::views::transform(v2, get_element<1>{})),
  init, std::plus{});
assert(result_bztv == 396);

auto result_no_proj = std::ranges::transform_reduce(
  std::views::transform(v1, get_element<0>{}),
  std::views::transform(v2, get_element<1>{}),
  out.begin(), init, std::plus{}, std::multiplies{});
assert(result_no_proj == 396);
```

C++17 binary `transform_reduce` does not take a projection.  Instead, it takes a binary transform function, that combines elements from the two input ranges into a single element.  The algorithm then reduces these elements using the binary reduce function and the initial value.  It's perhaps misleading that this binary function is called a "transform"; it's really a kind of reduction on corresponding elements of the two input ranges.

One can imagine a ranges analog of C++17 binary `transform_reduce` that takes two projection functions, as in the example below.  It's not too hard for a casual reader to tell that the last two arguments of `reduce` apply to each of the input sequences in turn, but that's still more consecutive function arguments than any other algorithm in the C++ Standard Library.  Without projections, users would need to resort to `transform_view`, but this more verbose syntax makes it more clear which functions do what.

```c++
struct foo {};
std::vector<std::tuple<int, foo, std::string>> v1{
  {5, {}, "five"}, {7, {}, "seven"}, {11, {}, "eleven"}};
std::vector<std::pair<std::string, int>> v2{
  {"thirteen", 13}, {"seventeen", 17}, {"nineteen", 19}};
const int init = 3; 
std::vector<int> out(std::from_range, std::views::repeat(0, 3));

// With projections
auto result_proj = std::ranges::transform_reduce(v1, v2, out.begin(), init,
  std::plus{}, std::multiplies{}, get_element<0>{}, get_element<1>{});
assert(result_proj == 396);

// Without projections
auto result_no_proj = std::ranges::transform_reduce(
  std::views::transform(v1, get_element<0>{}),
  std::views::transform(v2, get_element<1>{}),
  out.begin(), init, std::plus{}, std::multiplies{});
assert(result_no_proj == 396);
```

#### Mixed guidance from the current ranges library

The current ranges library offers only mixed guidance for deciding whether `*reduce` algorithms should take projections.

The various `fold_*` algorithms take no projections.  Section 4.6 of <a href="https://wg21.link/P2322R6">P2322R6</a> explains that the `fold_left_first` algorithm does not take a projection in order to avoid an extra copy of the leftmost value, that would be required in order to support projections with a range whose iterators yield proxy reference types like `tuple<T&>` (as `views::zip` does).  P2322R6 clarifies that `fold_left_first`, `fold_right_last`, and `fold_left_first_with_iter` all have this issue.  However, the remaining two `fold_*` algorithms `fold_left` and `fold_right` do not.  This is because they never need to materialize an input value; they can just project each element at iterator `iter` via `invoke(proj, *iter)`, and feed that directly into the binary operation.  The author of P2322R6 has elected to omit projections for all five `fold_*` algorithms, so that they have a consistent interface.

A ranges version of `reduce` does not have `fold_left_first`'s design issue.  C++17 algorithms in the `reduce` family can copy results as much as they like, so that would be less of a concern here.  However, if we ever wanted a `ranges::reduce_first` algorithm, then the consistency argument would arise.

#### `*transform_view` not always trivially copyable even when function object is

Use of `transform_view` and `zip_transform_view` can make it harder for implementations to parallelize ranges algorithms.  The problem is that both views might not necessarily be trivially copyable, even if their function object is.  If a range isn't trivially copyable, then the implementation must do more work beyond just a `memcpy` or equivalent in order to get copies of the range to different parallel execution units.

Here is an example (<a href="https://godbolt.org/z/vYnzGd3js">Compiler Explorer link</a>).

```c++
#include <ranges>
#include <type_traits>
#include <vector>

// Function object type that acts just like f2 below.
struct F3 {
  int operator() (int x) const {
    return x + y;
  }
  int y = 1;
};

int main() {
  std::vector v{1, 2, 3};

  // operator= is defaulted; lambda type is trivially copyable
  auto f1 = [] (auto x) { 
    return x + 1;
  };
  static_assert(std::is_trivially_copyable_v<decltype(f1)>);

  // Capture means that lambda's operator= is deleted,
  // but lambda type is still trivially copyable
  auto f2 = [y = 1] (auto x) { 
    return x + y;
  };
  static_assert(std::is_trivially_copyable_v<decltype(f2)>);

  // decltype(view1) is trivially copyable
  auto view1 = v | std::views::transform(f1);
  static_assert(std::is_trivially_copyable_v<decltype(view1)>);

  // decltype(view2) is NOT trivially copyable, even though f2 is
  auto view2 = v | std::views::transform(f2); 
  static_assert(!std::is_trivially_copyable_v<decltype(view2)>);

  // view3 is trivally copyable, though it behaves just like view2.
  F3 f3{};
  auto view3 = v | std::views::transform(f3);
  static_assert(std::is_trivially_copyable_v<decltype(view3)>);

  return 0;
}
```

Both lambdas `f1` and `f2` are trivially copyable, but `std::views::transform(f2)` is *not* trivally copyable.  The wording for both `transform_view` and `zip_transform_view` expresses the input function object of type `F` as stored in an exposition-only _`movable-box<F>`_ member.  `f2` has a capture that gives it a `=delete`d copy assignment operator.  Nevertheless, `f2` is still trivially copyable, because each of its default copy and move operations is either trivial or deleted, and its destructor is nontrivial and deleted.  

The problem is _`movable-box`_.  As [range.move.wrap] 1.3 explains, since `copyable<decltype(f2)>` is not modeled, _`movable-box`_`<decltype(f2)>` provides a nontrivial, not deleted copy assignment operator.  This makes _`movable-box`_`<decltype(f2)>`, and therefore `transform_view` and `zip_transform_view`, not trivially copyable.

This feels like a wording bug.  `f2` is a struct with one member, an `int`, and a call operator.  Why can't I `memcpy` `views::transform(f2)` wherever I need it to go?  Even worse, `f3` is a struct just like `f2`, yet `views::transform(f3)` is trivially copyable.

Implementations can work around this in different ways.  For example, an implementation of `std::ranges::reduce` could have a specialization for the range being `zip_transform_view<F, V1, V2>` that reaches inside the `zip_transform_view`, pulls out the function object and views, and calls the equivalent of binary `transform_reduce` with them.  However, the ranges library generally wasn't designed to make such transformations easy to implement in portable C++.  Views generally don't expose their members -- an issue that hinders all kinds of optimizations.  (For instance, it should be possible for compilers to transform `cartesian_product_view` of bounded `iota_view` into OpenACC or OpenMP multidimensional nested loops for easier optimization, but `cartesian_product_view` does not have a standard way to get at its member view(s).)  As a result, an approach based on specializing algorithms for specific view types means that implementations cannot straightforwardly depend on a third-party ranges implementation for their views.  Parallel algorithm implementers generally prefer to minimize coupling of actual parallel algorithms with Standard Library features that don't directly relate to parallel execution.

#### Review

Let's review what we learned from the above discussion.

1. In general and particularly for `ranges::transform`, projections improve readability and expose optimization potential, by separating the selection part of an algorithm from the computation part.

2. None of the existing `fold_*` ranges algorithms (the closest things the Standard Library currently has to `ranges::reduce`) take projections.

3. Ranges `reduce` with a projection and unary `transform_reduce` without a projection have the same functionality, without much usability or implementation difference.   Ditto for `{in,ex}clusive_scan` with a projection and `transform_{in,ex}clusive_scan` without.

4. Expressing binary `transform_reduce` using only `reduce` requires `zip_transform_view` *always*, even if the two input ranges are contiguous ranges of `int`.  This hinders readability and potentially also performance.

5. A ranges version of binary `transform_reduce` that takes projections is harder to use and read than a version without projections.  However, a version without projections would need `transform_view` in order to offer the same functionality.  This potentially hinders performance.

#### Conclusions

We propose

* providing both unary and binary `ranges::transform_reduce` as well as `ranges::reduce`,

* providing `ranges::transform_{in,ex}clusive_scan` as well as `ranges::{in,ex}clusive_scan`, and

* *not* providing projections for any of these algorithms.

We conclude this based on a chain of reasoning, starting with binary `transform_reduce`.

1. We want binary `transform_reduce` for usability and performance reasons.  (The "transform" of a binary `transform_reduce` is *not* the same thing as a projection.)

2. It's inconsistent to have binary `transform_reduce` without unary `transform_reduce`.

3. Projections tend to hinder usability of both unary and binary `transform_reduce`.  If we have unary `transform_reduce`, we don't need `reduce` with a projection.

4. We already have `fold_*` (effectively special cases of `reduce`) without projections, even though some of the `fold_*` algorithms _could_ have had projections.

5. If we have other `*reduce` algorithms without projections as well, then the most consistent thing would be for *no* reduction algorithms to have projections.

6. It's more consistent for the various `*scan` algorithms to look and act like their `*reduce` counterparts, so we provide `ranges::transform_{in,ex}clusive_scan` as well as `ranges::{in,ex}clusive_scan`, and do not provide projections for any of them.

### "The lost algorithm": Noncommutative parallel reduction?

The Standard lacks an analog of `reduce` that can assume associativity but not commutativity of binary operations.  One author of this proposal refers to this as "the lost algorithm" (in e.g., <a href="https://adspthepodcast.com/2021/05/14/Episode-25.html">Episode 25 of "ASDP: The Podcast"</a>).  To elaborate: The current numeric algorithms express a variety of permissions to reorder binary operations.

1. `accumulate` and `partial_sum` both precisely specify the order of binary operations as sequential, from left to right.  This works even if the binary operation is neither associative nor commutative.

2. The various `*_scan` algorithms can reorder binary operations as if they are associative (they may replace `a + (b + c)` with `(a + b) + c`), but not as if they are commutative (they may replace `a + b` with `b + a`).

3. `reduce` can reorder binary operations as if they are both associative and commutative.

What's missing here is a parallel analog of `reduce` with the assumptions of `*_scan`, that is, a reduction that can assume associativity but not commutativity of binary operations.  Parallel reduction operations with these assumptions exist in other programming models.  For example, MPI (the Message Passing Interface for distributed-memory parallel communication) has a function `MPI_Create_op` for defining custom reduction operators from a user's function.  `MPI_Create_op` has a parameter that specifies whether MPI may assume that the user's function is commutative.

Users could get a parallel algorithm by calling `*_scan` with an extra output sequence, and using only the last element.  However, this requires extra storage.

A concepts-based approach like P1813R0's could permit specializing `reduce` on whether the user asserts that the binary operation is commutative.  P1813R0 does not attempt to do this; it merely specializes `reduce` on whether the associative and commutative operation has a two-sided identity element.  Furthermore, P1813R0 does not offer a way for users to assert that an operation is associative or commutative, because the `magma` (nonassociative) and `semigroup` (associative) concepts do not differ syntactically.  One could imagine a refinement of this design that includes a trait for users to specialize on the type of their binary operation, say `is_commutative<BinaryOp>`.  This would be analogous to the `two_sided_identity` trait in P1813R0 that lets users declare that their set forms a monoid, a refinement of `semigroup` with a two-sided identity element.

This proposal does not attempt to fill this gap in the Standard parallel algorithms, but would welcome a separate proposal to do so.  We think the right way would be to propose a new algorithm with a distinct name.  A reasonable choice of name would be `fold` (just `fold` by itself, not `fold_left` or `fold_right`).

### Algorithms with a ranges proposal in flight

We do not propose a `partial_sum` algorithm.  This algorithm performs operations sequentially.  Its parallel analogs are `inclusive_scan` and `exclusive_scan`, which we propose here.  For the non-parallel ranges version that returns a stateful binary operator, <a href="https://wg21.link/P2760R1">P2760R1</a> suggests a view instead of an algorithm.  <a href="https://wg21.link/P3351R2">P3351R2</a>, "`views::scan`," proposes this view.  P3351R2 is currently in SG9 (Ranges Study Group) review.

### Algorithms that do not need ranges versions

The following algorithms do not need ranges versions, since they can be replaced with existing views and ranges algorithms.

#### `iota`

C++23 has `iota_view`, the view version of `iota`.  One can replace the `iota` algorithm with `iota_view` and `ranges::copy`.  In fact, one could argue that `iota_view` is the perfect use case for a view: instead of storing the entire range, users can represent it compactly with two integers.  There also should be no optimization concerns with parallel algorithms over an `iota_view`.  For example, the Standard specifies `iota_view` in a way that does not hinder it from being trivially copyable, as long as its input types are.  The iterator type of `iota_view` is a random access iterator for reasonable lower bound types (e.g., integers).

#### `accumulate`

The `accumulate` algorithm performs operations sequentially.  Its parallel version is `reduce`, which we propose here.  The non-parallel version has been translated in C++23 into `fold_left`.

#### `inner_product`

The `inner_product` algorithm performs operations sequentially.  It can be replaced with a ranges version of `transform_reduce`.  P2214R2 argues specifically against adding a ranges analog of `inner_product`, because it is less fundamental than other algorithms, and because it's not clear how to incorporate projections.

#### `adjacent_difference`

`adjacent_difference` can be replaced with a combination of `adjacent_transform_view` (which was adopted in C++23) and `ranges::copy`.  On the other hand, we argue elsewhere in this proposal that views (such as `adjacent_transform_view`) that use a _`movable-box`_`<F>` member to represent a function object may have performance issues, due to _`movable-box<F>`_ being not trivially copyable even for some cases where `F` is trivially copyable.  On the other hand, the existing `adjacent_difference` use case could be covered with the trivially copyable `std::minus` function object.

In our experience, adjacent differences or their generalization are often used in combination with other ranges.  For example, finite-difference methods (such as Runge-Kutta schemes) for solving time-dependent differential equations may need to add together multiple ranges, each of which is an adjacent difference possibly composed with other functions.  If users want to express that as a one-pass algorithm, they might need to combine more than two input ranges, possibly using a combination of `transform_view`s and `adjacent_transform_view`s.  This ultimately would be hard to express as a single "`ranges::adjacent_transform`" algorithm invocation.  Furthermore, `ranges::adjacent_transform` is necessarily single-dimensional.  It could not be used straightforwardly for finite-difference methods for solving partial differential equations, for example.  All this makes an `adjacent_transform` algorithm a lower-priority task.

### We don't propose `reduce_first`

Section 5.1 of <a href="https://wg21.link/P2760R1">P2760R1</a> asks whether there should be a `reduce_first` algorithm, analogous to `fold_left_first`, for binary operations that lack a natural identity element to serve as the initial value.  An example would be `min` on a range of `int` values, where callers would have no way to tell if `INT_MAX` represents a value in the range, or an arbitrary stand-in for the (nonexistent) identity element.  We do not propose `reduce_first` for the following reasons.

1. P3179R8 already proposes parallel ranges overloads of `min_element`, `max_element`, and `minmax_element`.

2. `fold_left_first` and `fold_right_last` makes more sense, because these algorithms are ordered.  It matters which element of the sequence the user extracts.  `reduce` is unordered, so there's no reason to privilege one element over another.  Why should it be the first one?

3. Users can always extract the first element from the sequence and use it as the initial value in `reduce`.

The decision to add `reduce_first` depends on whether `reduce` takes a projection.  The `reduce_first` algorithm could not straightforwardly support projections.  If `reduce` takes a projection, then it would be inconsistent with `reduce_first`.  The only reason `fold_left` and `fold_right` do not take projections is for consistency with `fold_left_first`, `fold_left_with_iter`, and `fold_right_last`, which cannot take projections.  The only way for us to leave `reduce_first` for a later proposal is if `reduce` does not take a projection.

### We don't propose `reduce_with_iter`

A `reduce_with_iter` algorithm would look like `fold_left_with_iter`, but would permit reordering of binary operations.  It would return both an iterator to one past the last element, and the computed value.  A hypothetical `reduce_with_iter` algorithm would also return an iterator to one past the last element, and the computed value, but would share `reduce`'s permission to reorder binary operations.

We do not propose the analogous `reduce_with_iter` here, though we would not oppose someone else proposing it.  That algorithm would serve users who are writing code generic enough to work with single-pass input iterators, _and_ who want to expose potential binary operation reordering opportunities.

Just like `fold_left`, the `reduce` algorithm should return just the computed value.  Section 4.4 of <a href="https://wg21.link/P2322R6">P2322R6</a> argues that this makes it easier to use, and improves consistency with other ranges algorithms like `ranges::count` and `ranges::any_of`.  It is also consistent with P3179R8.  The algorithms `fold_left_with_iter` and `fold_left_first_with_iter` exist for users who want both the iterator and the value.  Section 4.4 of P2322R6 further elaborates that `fold_left` should not be specified in terms of `fold_left_with_iter`, for performance reasons: it would "incur an extra move of the accumulated result, due to lack of copy elision (we have different return types)."  The `*_with_iter` algorithms are separate algorithms that need separate specifications.

### Summary: Algorithms that we propose here

We propose providing parallel and non-parallel overloads of

* unary and binary `ranges::transform_reduce` as well as `ranges::reduce`,

* `ranges::transform_{in,ex}clusive_scan` as well as `ranges::{in,ex}clusive_scan`, and

* the convenience wrappers `ranges::sum(r)` as `ranges::reduce(r, plus{}, range_value_t<R>())` and `ranges::product(r)` as `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`, as proposed in <a href="https://wg21.link/P2760R1">P2760R1</a>.
  
## Range categories and return types

* Our parallel algorithms take sized random access ranges.

* Our non-parallel algorithms take sized forward ranges.

* Our scans' return type is an alias of `in_out_result`.

* Our reductions just return the reduction value, not `in_value_result` with an input iterator.

P3179R8 does not aim for perfect consistency with the range categories accepted by existing ranges algorithms.  The algorithms proposed by P3179R8 differ in the following ways.

1. P3179R8 uses a range, not an iterator, as the output parameter (see Section 2.7).

2. P3179R8 requires that the ranges be sized (see Section 2.8).

3. P3179R8 requires random access ranges (see Section 2.6).

Of these differences, (1) and (2) could apply generally to all ranges algorithms, so we adopt them for this proposal.

Regarding (1), this would make our proposal the first to add non-parallel range-as-output ranges algorithms to the Standard.  For arguments in favor of non-parallel algorithms taking a range as output, please refer to <a href="https://wg21.link/P3490R0">P3490R0</a>, "Justification for ranges as the output of parallel range algorithms."  (Despite the title, it has things to say about non-parallel algorithms too.)  Taking a range as output would prevent use of existing output-only iterators that do not have a separate sized sentinel type, like `std::back_insert_iterator`.  However, all the algorithms we propose require at least forward iterators (see below).  P3490R0 shows that it is possible for both iterator-as-output and range-as-output overloads to coexist, so we follow P3179R8 by not proposing iterator-as-output algorithms here.

Regarding (2), we make the parallel algorithms proposed here take sized random access ranges, as P3179R8 does.  For consistency, we also propose that the output ranges be sized.  As a result, any parallel algorithms with an output range need to return both an iterator to one past the last element of the input, and an iterator to one past the last element of the input.  This tells callers whether there was enough room in the output, and if not, where to start when processing the rest of the input.  This includes all the `*{ex,in}clusive_scan` algorithms we propose.

Difference (3) relates to P3179R8 only proposing parallel algorithms.  It would make sense for us to relax this requirement for the non-parallel algorithms we propose.  This leaves us with two possibilities:

* (single-pass) input and output ranges, the most general; or

* (multipass) forward ranges.

The various reduction and scan algorithms we propose can combine the elements of the range in any order.  For this reason, we make the non-parallel algorithms take (multipass) forward ranges, even though this is not consistent with the existing non-parallel `<numeric>` algorithms.  If users have single-pass iterators, they should just call one of the `fold_*` algorithms, or use the `views::scan` proposed elsewhere.  This has the benefit of letting us specify `ranges::reduce` to return just the value.  We don't propose a separate algorithm `reduce_with_iter`, as we explain elsewhere in this proposal.

## Constexpr parallel algorithms?

<a href="https://wg21.link/p2902r1">P2902R1</a> proposes to add `constexpr` to the parallel algorithms.  P3179R8 does not object to this; see Section 2.10.  We continue the approach of P3179R8 in not opposing P2902R1's approach, but also not depending on it.

## `ranges::reduce` design

In this section, we focus on `ranges::reduce`'s design.  The discussion here applies generally to the other algorithms we propose.

### No default parameters

Section 5.1 of P2760R1 states:

> One thing is clear: `ranges::reduce` should *not* take a default binary operation *nor* a default initial parameter. The user needs to supply both.

This motivates the convenience wrappers

* `ranges::sum(r)` for `ranges::reduce(r, plus{}, range_value_t<R>())`, and

* `ranges::product(r)` for `ranges::reduce(r, multiplies{}, range_value_t<R>(1))`.

One argument *for* a default initial value in `std::reduce` is that `int` literals like `0` or `1` do not behave in the expected way with a sequence of `float` or `double`.  For `ranges::reduce`, however, making its return value type imitate `ranges::fold_left` instead of `std::reduce` fixes that.

### For return type, imitate `ranges::fold_left`, not `std::reduce`

Both `std::reduce` and `std::ranges::fold_left` return the reduction result as a single value.  However, they deduce the return type differently.  For `ranges::reduce`, we deduce the return type like `std::ranges::fold_left` does, instead of always returning the initial value type `T` like `std::reduce`.

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

It's notable that `reduce`-like `mdspan` algorithms in [linalg] -- `dot`, `vector_sum_of_squares`, `vector_two_norm`, `vector_abs_sum`, `matrix_frob_norm`, `matrix_one_norm`, and `matrix_inf_norm` -- all have the same return type behavior as C++17 `std::reduce`.  However, the authors of [linalg] expect typical users of their library to prefer complete control of the return type, even if it means they have to type `1.0` instead of `1`.  These [linalg] algorithms also have more precise wording about precision of intermediate terms in sums when the element types and the initial value are all floating-point types or specializations of `complex`.  (See e.g., [linalg.algs.blas1.dot] 7.)  For ranges reduction algorithms, we expect a larger audience of users and thus prefer consistency with `fold_*`'s return type.

## Constraining parallel ranges numeric algorithms

1. We use the same constraints as `fold_left` and `fold_right` to constrain the binary operator of `reduce` and `*_scan`.

2. We imitate C++17 parallel algorithms and [linalg] (<a href="https://wg21.link/P1673R13">P1673R13</a>) by using *GENERALIZED_NONCOMMUTATIVE_SUM* and *GENERALIZED_SUM* to describe the behavior of `reduce` and `*_scan`.

3. Otherwise, we follow the approach of <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> ("C++ Parallel Range Algorithms").

<a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a>, which is in the last stages of wording review, defines parallel versions of many ranges algorithms in the C++ Standard Library.  (The "parallel version of an algorithm" is an overload of an algorithm whose first parameter is an execution policy.)  That proposal restricts itself to adding parallel versions of existing ranges algorithms.  P3179R8 explicitly defers adding overloads to the numeric algorithms in <a href="https://eel.is/c++draft/numeric.ops">[numeric.ops]</a>, because these do not yet have ranges versions.  Our proposal fills that gap.

WG21 did not have time to propose ranges-based numeric algorithms with the initial set of ranges algorithms in C++20.  <a href="https://wg21.link/P1813R0">P1813R0</a>, "A Concept Design for the Numeric Algorithms," points out the challenge of defining ranges versions of the existing parallel numeric algorithms.  What makes this task less straightforward is that the specification of the parallel numeric algorithms permits them to reorder binary operations like addition.  This matters because many useful number types do not have associative addition.  Lack of associativity is not just a floating-point rounding error issue; one example is saturating integer arithmetic.  Ranges algorithms are constrained by concepts, but it's not clear even if it's a good idea to define concepts that can express permission to reorder terms in a sum.

C++17 takes the approach of saying that parallel numeric algorithms can reorder the binary operations however they like, but does not say whether any reordering would give the same results as any other reordering.  The Standard expresses this through the wording "macros" *GENERALIZED_NONCOMMUTATIVE_SUM* and *GENERALIZED_SUM*.  (A wording macro is a parameterized abbreviation for a longer sequence of wording in the Standard.  We put "macros" in double quotes because they are not necessarily preprocessor macros.  They might not even be implementable as such.)  Algorithms become ill-formed, no diagnostic required (IFNDR) if the element types do not define the required operations.  P1813R0 instead defines C++ concepts that represent algebraic structures, all of which involve a set with a closed binary operation.  Some of the structures require that the operation be associative and/or commutative.  P1813R0 uses those concepts to constrain the algorithms.  This means that the algorithms will not be selected for overload resolution if the element types do not define the required operations.  It further means that algorithms could (at least in theory) dispatch based on properties like whether the element type's binary operation is commutative.  The concepts include both syntactic and semantic constraints.  

WG21 has not expressed a consensus on P1813R0's approach.  LEWGI reviewed P1813R0 at the Belfast meeting in November 2019, but did not forward the proposal and wanted to see it again.  Two other proposals express something more like WG21's consensus on constraining the numeric algorithms: <a href="https://wg21.link/P2214R2">P2214R2</a>, "A Plan for C++23 Ranges," and <a href="https://wg21.link/P1673R13">P1673R13</a>, "A free function linear algebra interface based on the BLAS," which defines mdspan-based analogs of the numeric algorithms.  Section 5.1.1 of P2214R2 points out that P1813R0's approach would overconstrain `fold`; P2214R2 instead suggests just constraining the operation to be binary invocable.  This was ultimately the approach taken by the Standard through the exposition-only concepts _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  Section 5.1.2 of P2214R2 says that `reduce` "calls for the kinds of constraints that P1813R0 is proposing."

<a href="https://wg21.link/P1673R13">P1673R13</a>, which was adopted into the Working Draft for C++26 as [linalg], took an entirely different approach for its set of `mdspan`-based numeric algorithms.  Section 10.8, "Constraining matrix and vector element types and scalars," explains the argument.  Here is a summary.

1. Requirements like associativity are too strict to be useful for practical types.  The only number types in the Standard with associative addition are unsigned integers.  It's not just a rounding error "epsilon" issue; sums of saturating integers can have infinite error if one assumes associativity.

2. "The algorithm may reorder sums" (which is what we want to say) means something different than "addition on the terms in the sum is associative" (which is not true for many number types of interest).  That is, permission for an algorithm to reparenthesize sums is not the same as a concept constraining the terms in the sum.

3. P1813R0 defines concepts that generalize a mathematical group.  These are only useful for describing a single set of numbers, that is, one type.  This excludes useful features like mixed precision (e.g., where the result type in `reduce` differs from the range's element type) and types that use expression templates.  One could imagine generalizing this to a set of types that have a common type, but this can be too restrictive; Section 5.1.1 of <a href="https://wg21.link/P2214R2">P2214R2</a> gives an example involving two types in a fold that do not have a common type.

P1673R13 says that algorithms have complete freedom to create temporary copies or value-initialized temporary objects, rearrange addends and partial sums arbitrarily, or perform assignments in any order, as long as this would produce the result specified by the algorithm's *Effects* and *Remarks* when operating on elements of a semiring.  The `linalg::dot` ([linalg.algs.blas1.dot]) and `linalg::vector_abs_sum` ([linalg.algs.blas1.asum]) algorithms specifically define the returned result(s) in terms of *GENERALIZED_SUM*.  Those algorithms do that because they need to constrain the precision of intermediate terms in the sum (so they need to define those terms).  In our case, the Standard already uses *GENERALIZED_SUM* and *GENERALIZED_NONCOMMUTATIVE_SUM* to define ranges algorithms like `reduce`, `inclusive_scan`, and `exclusive_scan`.  We can just adapt this wording to talk about ranges instead of iterators.  This lets us imitate the approach of <a href="https://isocpp.org/files/papers/P3179R8.html">P3179R8</a> in adding ranges overloads.

Our approach combines the syntactic constraints used for the `fold_*` family of algorithms, with the semantic approach of P1673R13 and the C++17 parallel numeric algorithms.  For example, we constrain `reduce`'s binary operation with both _`indirectly-binary-left-foldable`_ and _`indirectly-binary-right-foldable`_.  (This expresses that if the binary operation is called with an argument of the initial value's type `T`, then that argument can be in either the first or second position.)  We express what `reduce` does using *GENERALIZED_SUM*.

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

TODO: Check that using `projected<I, Proj>` instead of `I` in _`indirectly-binary-foldable`_ is the right way to handle projections.

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
