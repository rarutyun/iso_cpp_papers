---
title: "Rename `join` and `nest` in `async_scope` proposal"
document: P3333R0
date: today
audience: LEWG, LWG
author:
  - name: Ruslan Arutyunyan
    email: <ruslan.arutyunyan@intel.com>
  - name: Ville Voutilainen
    email: <ville.voutilainen@gmail.com>
toc: true
toc-depth: 2
---

# Abstract {- .unlisted}

This paper proposes renaming `join` and `nest` methods in `async_scope` proposal [P3149R9]

# Motivation {#motivation}

[@P3149R9] provides a one of the important peaces peaces for [exec]{- .sref} ([@P2300R10] proposal) to complete a picture.
The facilities there allow to group the work by associating it with asynchronous scopes and then wait at some point for all
associated work to be completed. It a bridge from unstructured concurrency to structured one with saying that individual
pieces of work (possibly on different execution contexts) become the one, combined work. The facilities also support

- associating the work and run it (eager execution)
- associating the work but getting back a sender to run it later (lazy execution)

Despite all the advantages, some names might be arguably misleading because they are either to not follow the existing
practices or might have a different meaning in the same or very close domains: concurrency and parallelism.

[@P3149R9] has a section that ruminates about naming. But this discussion never happened in LEWG and/or reflector.

[@P3685R0] started making improvements by proposing `async_scope_token` -> `scope_token` renaming. We want to make it even
better with this proposal by renaming `join` and `nest`. There are other candidates to be renamed as well, however the
existing names are also fine and authors cannot come up with better alternative, so those are out of scope for this paper.

# Rename `join` member functions {#rename_join}

The name `join` is problematic because we already have `join` in the standard for threads but in a different way. [@P3149R9]
tells about similarity between `std::thread::join` and `execution::counting_scope::join`, however there is a fundamental
difference: `join` for `std::thread` is a blocking call. It blocks the current thread and waits till its completion.
`counting_scope::join`, on the other hand, is not blocking. It returns a sender that signals when all the work associated
with a scope is completed. Consider the following comparison table:

::: cmptable

> `thread::join` vs `counting_scope::join`

### `thread::join`
```cpp






// create the thread doing some work
std::thread t1([]{ /* some work */ });




// block a current thread and waits for t1 completion
t1.join();





```

### `counting_scope::join`
```cpp
namespace ex = std::execution;

my_thread_pool pool;
ex::counting_scope scope;

// create sender to run within async scope
auto snd = ex::transfer_just(pool.get_scheduler()) |
               ex::then([] { /* some work */ });

// associate snd and launch the work
ex::spawn(snd, scope.get_token());

// !!! Has no effect. Returns sender that is discarded
scope.join();

// Since join itself does not block, users should call
// sync_wait explicitly with the returned sender
this_thread::sync_wait(scope.join());
```
:::

As you can see in the example above, `counting_scope::join` does not block as some might expect. It returns a sender that
can signal that the all associated work with the scope is completed. Somebody might argue that this is a quality of
implementation: implementers can put `[[nodiscard]]` attribute on the function and just see the warning. While this is true
it does not eliminate the fact that the name itself it not helpful to recognize such a situation.

In [@libunifex] there is a v1 of `async_scope` (the latest one is v2) that call this function as `complete`. While this name is
better in our opinion it does not give any clue that the function itself is not blocking.

Together with authors or [@P3149R9] and some other people we considered a bunch of names. For example, `complete`,
`cleanup`, `drain`, `completes`, `completes_when`, `done`, etc.

It seems like people converge to have `when_` prefix, similar to `when_all` and given that we mentioned a term *completion*
several times in this paper, so we suggest to rename `join` function to `when_completed` because in our opinion it's much
clear and gives users better understanding of what's going on.

The last line of the code in the column with async scope looks like this with out proposal:

```cpp
this_thread::sync_wait(scope.when_completed());
```

# Rename `nest` {#rename_nest}

There is a term that is called *nested parallelism*. It basically means that people spawn more work from a parallel context.

One of the APIs that creates a nested parallelism (or nested work) in [@P2300R10] is `let_value`. It, basically, runs the
returned sender from the passed function and waits for its completion to send the computed values further. This
returned-by-the-passed-function-sender is a nested work. [@P3149R9] itself contains the example with nested parallelism when
shows handling a tree with `spawn_future`.

However, `nest` name itself doesn't have imply nested parallelism at all. That's why it's confusing. When users call `nest`
they basically want to `associate` the piece of work with the scope, or `attach` the piece of work to the scope. The
execution of the work is deferred. It might be also called like that by historical reasons: previously [@P3149R9]
proposal had `nest` as one of the required operations on scopes by `async_scope` concept. The current state is that the
`async_scope` concept was replaced by lower level one named `async_scope_token` (hopefully, just `scope_token` after
applying [@P3685R0]) with requiring `try_associate` and `deassociate` methods (plus `wrap`). Also v1 `async_scope` in
[@libunifex] had the `attach` name instead of `nest`.

In our desire to avoid confusion and also taking into account the previous experience we can recommend two names instead of
`nest`. It's either `associate` or `attach`. Given that [@P3149R9] talks about association a lot, this is the preferred
option, unless authors of [@P3149R9] the similarity with `try_associate` is rather confusing than helpful. If this is the
case then `attach` is also a good choice.

# Proposal {#proposal}

- Rename `join` to `when_completed`
- Rename `nest` to `associate`

---
references:
  - id: P3685R0
    citation-label: P3685R0
    title: Rename `async_scope_token`
    author:
      - family: Leahy
        given: Robert
    URL: https://isocpp.org/files/papers/P3685R0.pdf
  - id: libunifex
    citation-label: libunifex
    title: libunifex
    author:
      - family:
        given:
    URL: https://github.com/facebookexperimental/libunifex

---

