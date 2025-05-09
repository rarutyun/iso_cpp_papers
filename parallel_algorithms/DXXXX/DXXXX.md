---
title: "Reconsider parallel `ranges::rotate_copy` and `ranges::reverse_copy`"
document: P3333R0
date: today
audience: LEWG, LWG
author:
  - name: Ruslan Arutyunyan
    email: <ruslan.arutyunyan@intel.com>
  - name: Alexey Kukanov
    email: <alexey.kukanov@intel.com>
toc: true
toc-depth: 2
---

# Abstract {- .unlisted}

This paper proposes reconsidering the design for parallel `ranges::rotate_copy` and `ranges::reverse_copy` from [@P3179R7]
with regard to return type because the concerns about having the same return type but doing something different compared to
serial range algorithms were raised.

# Introduction {#introduction}

LEWG approved "range-as-the-output" design aspect for parallel range algorithms proposal [@P3179R7]. Two algorithms -
`ranges::rotate_copy` and `ranges::reverse_copy` - in the mentioned proposal require a special thinking (compared to the
rest with the output) what to return when the output size is not sufficient. The reason for that is that the `reverse_copy`
goes in reverse and `rotate_copy` has two "subranges" within one range split by `middle`. The proposed design in [@P3179R7]
returns past the last copied iterator and it is absolutely logical to do in isolation because users want to know where the
algorithm stops if the output size is not sufficient to copy all the elements from input. Also, doing that is consistent
with other algorithms with "range-as-the-output". The potential problem, however, comes from the combinations of two factors:

- serial range algorithms return `last` for both `reverse_copy` and `rotate_copy`.
- the return type for serial and parallel range algorithms is the same, however parallel range algorithms return a different
  iterator for input if the output size is sufficient.

To elaborate more on serial range algorithms: the current strategy for them (for the vast majority) is returning as
much information is possible. For example: `ranges::for_each` returns past the last iterator
because users might not have one at before the algorithm call; they have `sentinel`. On the other hand, we envision serial
range algorithms with "range-as-the-output" design in the future, for which users are also interested where the algorithm
stops if the output size is not sufficient. It would be very unfortunate if "rangified" serial range algorithms end up
returning something different compared to parallel range algorithms. Thus, parallel range algorithms should accept the fact
that they need to calculate more than necessary for `random_access_range`.

One of the design goals for [@P3179R7] was to keep the same return type for the proposed algorithms compared to existing
range algorithms and there are no concerns about the rest of algorithms with "range-as-the-output" because they return
`last` if the output size is sufficient. However, there is a clear indication that we need to do something different
for `reverse_copy` and `rotate_copy`.

# `rotate_copy` recommended design {#rotate_copy_design}

In [@P3179R7] `rotate_copy` returns past the last inserted copied element for input. By design it never returns `last` and
returns `middle` for two scenarios:

- output size is sufficient to copy everything from input.
- output size is empty.

The raised concern is that the users might be surprised at runtime by completely different return value with just adding an
execution policy as the first function argument. Consider the following example:

::: cmptable

> Serial vs Parallel `rotate_copy`

### iterator-as-the-output `rotate_copy`
```cpp
std::list v{1,2,3,4,5,6};
std::vector<int> out_v(6);

// view is not common_range
auto view = std::views::counted(v.begin(), 6);

static_assert(!std::ranges::common_range<decltype(view)>);

auto it = view.begin();
std::ranges::advance(it, 3);

// iterator as the output
auto res = std::ranges::rotate_copy(view, it, out_v.begin());

// res.in is last

auto val = std::reduce(view.begin(), res.in);

// val is 21
```

### range-as-the-output `rotate_copy`
```cpp
std::list v{1,2,3,4,5,6};
std::vector<int> out_v(6);

// view is not common_range
auto view = std::views::counted(v.begin(), 6);

static_assert(!std::ranges::common_range<decltype(view)>);

auto it = view.begin();
std::ranges::advance(it, 3);

// range as the output
auto res = std::ranges::rotate_copy(view, it, out_v);

// res.in is middle

auto val = std::reduce(view.begin(), res.in);

// val is 6
```
:::

Please note that in the table above we don't use algorithm overloads with execution policy. Instead, we show the future
serial range algorithms with range-as-the-output. The reason for that is because we require `random_access_range` for
parallel algorithms but applying `counted_view` on top of `std::list` gives us non-`random_access_range`. I am not aware
of any view in C++ standard library that would give a non common range for random access container and this is important
because there is no point to calculate `last` as the iterator (not `sentinel`) for `random_access_range`.

While we could say that in case of sufficient output size we would return `last` there two problems with that:

- it is inconsistent with other algorithms with "range-as-the-output", the rest returns past the last.
- we envision serial range algorithms with "range-as-the-output" where output size is allowed to be less than the input
  size. We calculated `last` as the iterator but don't return that. See [](#introduction) for more information.

Also, if we don't want to surprise users with the different behavior of parallel `rotate_copy` at runtime, we need to make
it fail to compile when the returned value for serial `rotate_copy` was stored and used. When we introduce
"range-as-the-output" with its own bound we take into account that the output size may be less than input size. Since
`rotate_copy` has two "subranges" - from `middle` to `last` and from `first` to `middle` - we want to say where the
algorithm stops for both of them, thus we propose to change the result type to `ranges::in_in_out_result`.
The signatures are below:

```cpp
template <class In, class Out>
using rotate_copy_truncated_result = std::ranges::in_in_out_result<In, In, Out>;

template<@_execution-policy_@ Ep, random_access_iterator I, sized_sentinel_for<I> S,
        random_access_iterator O, sized_sentinel_for<O> OutS>
  requires indirectly_copyable<I, O>
  ranges::rotate_copy_truncated_result<I, O>
    ranges::rotate_copy(Ep&& exec, I first, I middle, S last, O result, OutS result_last);

template<@_execution-policy_@ Ep, @_sized-random-access-range_@ R, @_sized-random-access-range_@ OutR>
  requires indirectly_copyable<iterator_t<R>, iterator_t<OutR>>
  ranges::rotate_copy_truncated_result<borrowed_iterator_t<R>, borrowed_iterator_t<OutR>>
    ranges::rotate_copy(Ep&& exec, R&& r, iterator_t<R> middle, OutR&& result_r);
```

The design above addresses all the concerns in our opinion because:

- for `in_out_result` the name for input is `in` data member, while for `in_in_out_result` the names for input are `in1` and
  `in2` data members, so we achieve the goal for the the code to fail to compile when one uses the output and switches to the
  parallel overload even if users store the result as `auto` type.
- it takes into account future serial range algorithms with "range-as-the-output" where `rotate` copy returns
  - both `last`, calculated as iterator, and `middle` as the stop point, when the output size is sufficient
  - stop points for both "subranges" (from `middle` to `last` and from `first` to `middle`) when the output size is less
    than the input one.

Possible implementation of envisioned serial algorithm:

```cpp
struct rotate_copy_fn
{
    template<std::forward_iterator I, std::sentinel_for<I> S, std::forward_iterator O, std::sentinel_for<O> OutS>
    requires std::indirectly_copyable<I, O>
    constexpr std::ranges::rotate_copy_truncated_result<I, O>
        operator()(I first, I middle, S last, O result, OutS result_last) const
    {
        while (middle != last && result != result_last)
        {
            *result = *middle;
            ++middle;
            ++result;
        }

        while (first != middle && result != result_last)
        {
            *result = *first;
            ++first;
            ++result;
        }
        return {std::move(first), std::move(middle), std::move(result)};
    }

    template<std::ranges::forward_range R, std::ranges::forward_range OutR>
    requires std::indirectly_copyable<std::ranges::iterator_t<R>, std::ranges::iterator_t<OutR>>
    constexpr std::ranges::rotate_copy_truncated_result<std::ranges::borrowed_iterator_t<R>, std::ranges::borrowed_iterator_t<OutR>>
        operator()(R&& r, std::ranges::iterator_t<R> middle, OutR&& result_r) const
    {
        return (*this)(std::ranges::begin(r), std::move(middle),
                       std::ranges::end(r), std::ranges::begin(result_r), std::ranges::end(result_r));
    }
};

inline constexpr rotate_copy_fn rotate_copy {};
```

# `reverse_copy` recommended design # {#reverse_copy_design}

For `reverse_copy` the proposed algorithm in [@P3179R7] always returns past the last element (in reversed order) for input.
There is no "dual intent" for the variety of return values. It works perfectly fine in isolation, however two concerns
come in mind:

- different behavior at runtime compared to serial range counterpart, when output size is sufficient (serial one always
  returns `last`).
- we envision serial range algorithms with "range-as-the-output" where output size is allowed to be less than the input
  size. We calculated `last` as the iterator but don't return that. See [](#introduction) for more information.

Please find a concerning example below:

::: cmptable

> Serial vs Parallel `reverse_copy`

### iterator-as-the-output `reverse_copy`
```cpp
std::list v{1,2,3,4,5,6};
std::vector<int> out_v(6);

// view is not common_range
auto view = std::views::counted(v.begin(), 6);

static_assert(!std::ranges::common_range<decltype(view)>);

// iterator as the output
auto res = std::ranges::reverse_copy(view, out_v.begin());

// res.in is last

auto val = std::reduce(view.begin(), res.in);

// val is 21
```

### range-as-the-output `reverse_copy`
```cpp
std::list v{1,2,3,4,5,6};
std::vector<int> out_v(6);

// view is not common_range
auto view = std::views::counted(v.begin(), 6);

static_assert(!std::ranges::common_range<decltype(view)>);

// range as the output
auto res = std::ranges::reverse_copy(view, it, out_v);

// res.in is middle

auto val = std::reduce(view.begin(), res.in);

// val is 0
```
:::

Eventually, the story is very similar with `rotate_copy`. The solution is also more or less the same: we propose to
return `in_in_out_result`:

- `in1` represents a stop point
- `in2` is always `last`.

The signatures are below:

```cpp
template <class In, class Out>
using reverse_copy_truncated_result = std::ranges::in_in_out_result<In, In, Out>;

template<@_execution-policy_@ Ep, random_access_iterator I, sized_sentinel_for<I> S,
        random_access_iterator O, sized_sentinel_for<O> OutS>
  requires indirectly_copyable<I, O>
  ranges::reverse_copy_truncated_result<I, O>
    ranges::reverse_copy(Ep&& exec, I first, S last, O result, OutS result_last);

template<@_execution-policy_@ Ep, @_sized-random-access-range_@ R, @_sized-random-access-range_@ OutR>
  requires indirectly_copyable<iterator_t<R>, iterator_t<OutR>>
  ranges::reverse_copy_truncated_result<borrowed_iterator_t<R>, borrowed_iterator_t<OutR>>
    ranges::reverse_copy(Ep&& exec, R&& r, OutR&& result_r);
```

Possible implementation of envisioned serial algorithm:

```cpp
struct reverse_copy_fn
{
  template<std::bidirectional_iterator I, std::sentinel_for<I> S, std::forward_iterator O, std::sentinel_for<O> OutS>
    requires std::indirectly_copyable<I, O>
    constexpr std::ranges::reverse_copy_truncated_result<I, O>
        operator()(I first, S last, O result, OutS result_last) const
    {
        auto res = std::ranges::next(first, last);
        auto last_iter = res;

        while (last_iter != first && result != result_last)
        {
          *result = *--last_iter;
            ++result;
        }

        return {last_iter, res, result};
    }

    template<std::ranges::bidirectional_range R, std::ranges::forward_range OutR>
    requires std::indirectly_copyable<std::ranges::iterator_t<R>, std::ranges::iterator_t<OutR>>
    constexpr std::ranges::reverse_copy_truncated_result<std::ranges::borrowed_iterator_t<R>, std::ranges::borrowed_iterator_t<OutR>>
        operator()(R&& r, OutR&& result_r) const
    {
        return (*this)(std::ranges::begin(r), std::ranges::end(r),
                       std::ranges::begin(result_r), std::ranges::end(result_r));
    }
};

inline constexpr reverse_copy_fn reverse_copy {};
```

# Alternative design # {#alternative_design}

There are the following alternatives to the proposed design:

- Keep [@P3179R7] status quo. The concerns are:
  - The different behavior at runtime, when switching the code to parallel algorithms.
  - No calculated `last` for future serial range algorithms with "range-as-the-output".
- Return `last` for parallel `rotate_copy` when the output is sufficient.
  - `reverse_copy` remains the same, the concerns from the previous bullets apply.
  - `rotate_copy` becomes inconsistent with other parallel range algorithms with "range-as-the-output" because it "jumps"
    to last element instead of return past the last one.
  - The signature does not have `last` so it likely means a different return type for serial "rangified" `rotate_copy` in
    the future.

Authors' preference is to remove parallel `reverse_copy` and `rotate_copy` from [@P3179R7] at all and add them later if the
recommended design is not accepted
