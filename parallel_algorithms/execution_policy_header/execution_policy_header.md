---
title: "A dedicated header for execution policies"
document: P4470R0
date: today
audience: LEWG, LWG
author:
  - name: Ruslan Arutyunyan (Intel)
    email: <ruslan.arutyunyan@intel.com>
toc: true
toc-depth: 2
---

# Abstract {- .unlisted}

This paper proposes a new standard library header, named `<exec_policy>`, that provides
the execution policy types, the execution policy objects, and the `is_execution_policy` type trait.

# Motivation {#motivation}

Execution policies were introduced in C++17 together with parallel algorithms ([@N4659], [execpol]{- .sref}). To use a
parallel algorithm, a user has to include `<execution>`, because it is the only header that declares
`std::execution::seq`, `std::execution::par`, `std::execution::par_unseq`, and `std::execution::unseq`.


```cpp
#include <algorithm>
#include <execution> // needed only for std::execution::par
#include <vector>

void f(std::vector<int>& v) {
    std::sort(std::execution::par, v.begin(), v.end());
}
```

When execution policies were added, `<execution>` contained nothing else, so the cost of including it was small.

However, with C++26 the whole Senders/Receivers (or `std::execution`, [@P2300R10]) and the follow-up
papers (`parallel_scheduler`, `task`, `async_scope`, etc.) landed into the same `<execution>` header.
The header now contains a lot of heavy template machinery: concepts, customization point objects, sender adaptors,
completion signatures metaprogramming, environments, queries, schedulers, and more.
None of this is needed to call `std::sort(std::execution::par, ...)`.

The result is that every translation unit that uses a parallel algorithm pays the compile-time cost of all of
`std::execution`.

The motivation of this proposal is to give the users the ability to access execution policies without the necessity
to include the whole `std::execution` if the only thing that they need is to use parallel algorithms.

# Proposal {#proposal}

- Add a new header `<exec_policy>` that declares:
    - `std::is_execution_policy` and `std::is_execution_policy_v` ([execpol.type]{- .sref});
    - `std::execution::sequenced_policy`, `std::execution::parallel_policy`,
      `std::execution::parallel_unsequenced_policy`, and `std::execution::unsequenced_policy`
      ([execpol.seq]{- .sref}, [execpol.par]{- .sref}, [execpol.parunseq]{- .sref}, [execpol.unseq]{- .sref});
    - `std::execution::seq`, `std::execution::par`, `std::execution::par_unseq`, and `std::execution::unseq`
      ([execpol.objects]{- .sref}).
- Keep the `<execution>` header providing all the names listed above.
- Add a feature-test macro.

With this proposal, the example from [](#motivation) becomes:

```cpp
#include <algorithm>
#include <exec_policy> // just execution policies, no std::execution
#include <vector>

void f(std::vector<int>& v) {
    std::sort(std::execution::par, v.begin(), v.end());
}
```

As stated, including `<execution>` instead remains valid and has the same meaning as before.

# Design considerations {#design}

## Header name {#header_name}

The name `<exec_policy>` is a working name. Possible alternatives:

- `<execution_policy>`: matches the "execution policy" term used by the standard and the `is_execution_policy` trait.
- `<execution_policies>`: plural, as the header provides several policies (compare with `<concepts>`, `<ranges>`,
  `<numbers>`).
- `<execpol>`: matches the stable name of the corresponding subclause. Too cryptic for users.

We can always debate the name in LEWG, although this is not what the group prefers
to spend time on.

## Should `<algorithm>` provide execution policies instead? {#include_from_algorithms}

An alternative approach would be to require the `<algorithm>` header to make execution policies available,
so that no extra include is needed at all.

However, in my opinion, this option is not that viable. We also have `<numeric>` and `<memory>` headers with parallel
algorithms, which would require us to provide execution policies when any of those headers is included. While, technically,
we could do that, it is still not a good idea, for the following reason.

If users only need serial algorithms (meaning that only the `<algorithm>` header is included and not `<execution>`), some
implementations (for example libstdc++) do not make parallel algorithm overloads available at all, meaning that the
compilation time is reduced. If `<algorithm>` (and the other headers) provided execution policies, the implementation
would have to include both parallel and non-parallel algorithms, which would make compilation time for existing users of serial
algorithms worse.

## Interaction with libraries implementing parallel algorithms in `<execution>` {#impl_details}

As mentioned above, libstdc++ pulls the parallel algorithms implementation in when `<execution>` is included. With this
proposal, such an implementation would need to do the same when `<exec_policy>` is included, which is straightforward:
the new header would include the parallel algorithm implementation details, but not the `std::execution` senders/receivers
framework.

## What about the `execution-policy` exposition-only concept? {#concept}

[@P3179R9] introduced the exposition-only `@_execution-policy_@` concept ([algorithms.parallel.defns]{- .sref}) that is
used to constrain parallel range algorithms. It is exposition-only, so it does not need to be declared in any header. It is
defined in terms of `is_execution_policy_v`, which is provided by the new header.

## Implementation-defined execution policies {#impl_defined_policies}

[execpol.general]{- .sref} allows implementations to provide additional execution policies. This paper requires
implementation-defined policies to be available via `<exec_policy>`, as well.

## Feature-test macro {#ftm}

Users can detect the new header with `__has_include(<exec_policy>)`. Nevertheless, for consistency with other
library additions, we propose a new feature-test macro `__cpp_lib_execution_policy_header`, defined in `<version>`,
`<exec_policy>`, and `<execution>`.

# Wording {#wording}

[The wording is relative to the C++ working draft.]{.ednote}

## Modify [headers]{- .sref}

Add `<exec_policy>` to the table "C++ library headers" (in alphabetical order, after `<execution>`).

## Modify [algorithms.general]{- .sref}

In the table "Algorithms library summary", change the header for [algorithms.parallel]{- .sref}:

|                       |                     |                                              |
|-----------------------|---------------------|----------------------------------------------|
| [algorithms.parallel] | Parallel algorithms | [`<execution>`]{.rm} [`<exec_policy>`]{.add} |

## Add a new subclause [execpol.syn] before [execpol.type]{- .sref}

::: add
**Header `<exec_policy>` synopsis [execpol.syn]**

```cpp
namespace std {
  // [execpol.type], execution policy type trait
  template<class T> struct is_execution_policy;
  template<class T> constexpr bool is_execution_policy_v = is_execution_policy<T>::value;
}

namespace std::execution {
  // [execpol.seq], sequenced execution policy
  class sequenced_policy;

  // [execpol.par], parallel execution policy
  class parallel_policy;

  // [execpol.parunseq], parallel and unsequenced execution policy
  class parallel_unsequenced_policy;

  // [execpol.unseq], unsequenced execution policy
  class unsequenced_policy;

  // [execpol.objects], execution policy objects
  inline constexpr sequenced_policy            seq{ @_unspecified_@ };
  inline constexpr parallel_policy             par{ @_unspecified_@ };
  inline constexpr parallel_unsequenced_policy par_unseq{ @_unspecified_@ };
  inline constexpr unsequenced_policy          unseq{ @_unspecified_@ };
}
```
:::

## Modify [execution.syn]{- .sref}

```diff
+#include <exec_policy> @_// see [execpol.syn]_@
+
-namespace std {
-  @_// [execpol.type], execution policy type trait_@
-  template<class T> struct is_execution_policy;
-  template<class T> constexpr bool is_execution_policy_v = is_execution_policy<T>::value;
-}
-
-namespace std::execution {
-  @_// [execpol.seq], sequenced execution policy_@
-  class sequenced_policy;
-
-  @_// [execpol.par], parallel execution policy_@
-  class parallel_policy;
-
-  @_// [execpol.parunseq], parallel and unsequenced execution policy_@
-  class parallel_unsequenced_policy;
-
-  @_// [execpol.unseq], unsequenced execution policy_@
-  class unsequenced_policy;
-
-  @_// [execpol.objects], execution policy objects_@
-  inline constexpr sequenced_policy            seq{ @_unspecified_@ };
-  inline constexpr parallel_policy             par{ @_unspecified_@ };
-  inline constexpr parallel_unsequenced_policy par_unseq{ @_unspecified_@ };
-  inline constexpr unsequenced_policy          unseq{ @_unspecified_@ };
-}
-
 namespace std {
   @_// [execution.queryable], queryable objects_@
   template<class T>
     concept queryable = destructible<T>;
   ...
```

## Modify [version.syn]{- .sref}

```diff
 #define __cpp_lib_execution                         @_see below_@ // also in <execution>
+#define __cpp_lib_execution_policy_header           20XXXXL // also in <execution>, <exec_policy>
```
