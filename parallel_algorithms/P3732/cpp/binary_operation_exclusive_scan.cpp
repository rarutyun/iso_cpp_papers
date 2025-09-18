#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <iostream>
#include <limits>
#include <numeric>
#include <ranges>
#include <vector>

// Some algorithms, like exclusive_scan, always need an initial value.
// Many algorithms -- e.g., {inclusive,exclusive}_scan, reduce, and
// transform_reduce -- need an identity value.  That means we need a way
// to distinguish the initial value from the identity value.
// We have a few ways to do that.
//
// 1. By position and order alone
//    - Initial value follows immediately after the input range
//      (because it's a property of the input)
//    - Identity value follows immediately after
//      the binary operation to which it applies
//      (e.g., for binary transform_reduce, (op1, id1), op2)
// 
// 2. By type (as well as position and order)
//    a. Attach identity value to binary operation as a single argument
//    b. Separate arguments for binary operation and identity value
//
// Design concerns:
//
// 1. What if there is no identity value?  Do we intend to support that?
//    C++17 parallel std::reduce already does.
//
// 2. Should we support specifying the identity value
//    via constant_wrapper or some other "compile-time value"?
//
// Design options:
//
// 1. binary_operation struct,
//    holding both binary operator and identity value
// 2. separate arguments for binary operator and identity value,
//    with op_identity struct holding identity value
// 3. separate arguments for binary operator and identity value,
//    with the identity value only identified by its position
//    (immediately following the binary operator it describes)

//
// Define at most one of the following.
//
// * BINARY_OPERATION_STRUCT is P3732's design.
//
// * OP_IDENTITY_STRUCT treats the identity as a separate parameter,
//   and wraps it in a struct.
//
// * POSITION_ONLY treats the identity as a separate parameter,
//   and passes it in directly (not wrapped in a struct).
//
#define BINARY_OPERATION_STRUCT 1
//#define OP_IDENTITY_STRUCT 1
//#define POSITION_ONLY 1

namespace p3732 {

// std::integral_constant doesn't accept non-integral types.
// std::constant_wrapper (C++26 feature) doesn't back-port nicely.
template<class T, T Value>
struct constant {
  static constexpr T value = Value;
  constexpr operator T() { return Value; }
  constexpr static T operator() () { return Value; }
};

// Tag type expressing that an identity value doesn't exist
// or isn't known for the given binary operator.
// Min and max on integers both have this problem
// (as integers lack representations of positive and negative infinity).
//
// Having this lets us implement ranges min and max algorithms
// using ranges::reduce.
struct no_identity_t {};
inline constexpr no_identity_t no_identity{};

// std::ranges::min is defined with both input types
// the same, so defining mixed operator< on
// constant<T, Value> won't help.
#if defined(BINARY_OPERATION_STRUCT)

template<class BinaryOp>
constexpr bool has_identity_value = false;

template<class BinaryOp>
struct binary_operation_base {
  template<class ... Args>
  constexpr auto operator() (Args&&... args) const 
    requires std::invocable<
      std::add_const_t<BinaryOp>,
      decltype(std::forward<Args>(args))...>
  {
    return std::as_const(op)(std::forward<Args>(args)...);
  }

  template<class ... Args>
  constexpr auto operator() (Args&&... args) 
    requires (! std::invocable<
      std::add_const_t<BinaryOp>,
      decltype(std::forward<Args>(args))...>)
  {
    return op(std::forward<Args>(args)...);
  }

  [[no_unique_address]] BinaryOp op;
};

// Identity can be, say, constant_wrapper of the value,
// not the actual value.  This works because the accumulator
// type is deduced from the operator result.
template<class BinaryOp, class Identity>
struct binary_operation :
  public binary_operation_base<BinaryOp>
{
  [[no_unique_address]] Identity id;
};

// Value-initialize Identity by default,
// if the type supports that.
//
// Identity=no_identity_t means that the binary operator
// does not have an identity, or the user does not know
// an identity value.  It still gets "stored" in the struct
// so that the struct can remain an aggregate.  Otherwise,
// it would need a one-parameter constructor for that case.
template<class BinaryOp, class Identity>
requires requires { Identity{}; }
struct binary_operation<BinaryOp, Identity> :
  public binary_operation_base<BinaryOp>
{
  [[no_unique_address]] Identity id{};
};

// As with std::plus, Identity=void means "the algorithm (in this case,
// exclusive_scan) needs to deduce the identity type and value."
template<class BinaryOp>
struct binary_operation<BinaryOp, void> :
  public binary_operation_base<BinaryOp>
{
  [[no_unique_address]] BinaryOp op;
};

template<class BinaryOp, class Identity>
binary_operation(BinaryOp, Identity) ->
  binary_operation<BinaryOp, Identity>;

template<class BinaryOp>
binary_operation(BinaryOp) ->
  binary_operation<BinaryOp, no_identity_t>;

struct test_binary_operation_nonconst {
  float operator() (float x, float y) { // deliberately nonconst
    return x + y;
  }
};

struct test_binary_operation_const {
  float operator() (float x, float y) const {
    return x + y;
  }
};

inline constexpr void test_binary_operation() {
  {
    test_binary_operation_nonconst op;
    [[maybe_unused]] binary_operation bop{op, 0.0f};
    static_assert(std::is_same_v<decltype(bop(1.0f, 2.0f)), float>);
  }
  {
    test_binary_operation_const op;
    [[maybe_unused]] binary_operation bop{op, 0.0f};
    static_assert(std::is_same_v<decltype(bop(1.0f, 2.0f)), float>);
  }
}

template<class BinaryOp, class Identity>
constexpr bool has_identity_value<
  binary_operation<BinaryOp, Identity>> = true;

template<class BinaryOp>
constexpr bool has_identity_value<
  binary_operation<BinaryOp, no_identity_t>> = false;

template<std::default_initializable InputRangeValueType,
         class BinaryOp>
constexpr auto
identity_value(const binary_operation<BinaryOp, void>&) {
  return InputRangeValueType{};
}

template<class InputRangeValueType,
         class BinaryOp, class Identity>
  requires(
    not std::is_same_v<Identity, no_identity_t>
  )
constexpr auto
identity_value(const binary_operation<BinaryOp, Identity>& bop) {
  return bop.id;
}

#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)

// Identity can be, say, constant_wrapper of the value, not the actual value.
// This works because the accumulator type is deduced from the operator result.
//
// Default template argument permits using `op_identity{}`
// as an argument of `exclusive_scan`.  It's up to `exclusive_scan` to figure out
// what Identity=void means, but users can guess that it's like
// std::plus<void> (meaning the identity value type is deduced).
template<class Identity=void>
struct op_identity;

template<class Identity>
struct op_identity {
  [[no_unique_address]] Identity id;
};

template<std::default_initializable Identity>
struct op_identity<Identity> {
  [[no_unique_address]] Identity id{};
};

template<>
struct op_identity<void> {};

template<>
struct op_identity<no_identity_t> {};

// Abbreviation so users don't have to type "identity" twice.
// Otherwise, they would have to write op_identity{no_identity}
// or op_identity<no_identity_t>{}.
inline constexpr op_identity<no_identity_t> no_op_identity{};

template<class Identity>
op_identity(Identity) -> op_identity<Identity>;

template<class InputRangeValueType, class Identity>
  requires(! std::is_same_v<Identity, no_identity_t>)
constexpr auto identity_value(op_identity<Identity> op_id) {
  return op_id.id;
}

template<std::default_initializable InputRangeValueType>
constexpr auto identity_value(op_identity<void>) {
  return InputRangeValueType{};
}

#endif // OP_IDENTITY_STRUCT

#if defined(BINARY_OPERATION_STRUCT)

template<
  std::ranges::forward_range InRange,
  std::ranges::forward_range OutRange,
  class InitialValue,
  class BinaryOp = std::plus<>
>
requires(
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    std::ranges::range_value_t<InRange>,
    std::ranges::range_value_t<InRange>
  > &&
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    InitialValue,
    std::ranges::range_value_t<InRange>
  >
)
std::ranges::in_out_result<
  std::ranges::iterator_t<InRange>,
  std::ranges::iterator_t<OutRange>
>
exclusive_scan(InRange&& in, OutRange&& out,
  InitialValue initial_value,
  BinaryOp bop)
{
  using in_value_type = std::ranges::range_value_t<InRange>;
  // FIXME this probably won't work for expression templates.
  using result_type = std::remove_cvref_t<
    decltype(bop(
      initial_value,
      std::declval<in_value_type>()))>;

  auto in_beg = std::ranges::begin(in);
  auto in_end = std::ranges::end(in);
  auto out_beg = std::ranges::begin(out);
  auto out_end = std::ranges::end(out);

  if constexpr (has_identity_value<BinaryOp>) {
    // Only parallel algorithms need the identity value.
    // For testing, though, we exercise getting and
    // using it.
    if (in_beg != in_end) {
      [[maybe_unused]] auto id =
        identity_value<in_value_type>(bop);
      assert(bop.op(id, *in_beg) == *in_beg);
      assert(bop.op(*in_beg, id) == *in_beg);
    }
  }

  auto in_iter = in_beg;
  auto out_iter = out_beg;
  result_type total = initial_value;
  for (;
       in_iter != in_end && out_iter != out_end;
       ++in_iter, ++out_iter) {
    *out_iter = total;
    total = bop(total, *in_iter);
  }
  return {in_iter, out_iter};
}

#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)

template<
  std::ranges::forward_range InRange,
  std::ranges::forward_range OutRange,
  class InitialValue,
  class BinaryOp = std::plus<>,
  class IdentityValue = std::ranges::range_value_t<InRange>
>
requires(
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    std::ranges::range_value_t<InRange>,
    std::ranges::range_value_t<InRange>
  > &&
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    InitialValue,
    std::ranges::range_value_t<InRange>
  >
)
std::ranges::in_out_result<
  std::ranges::iterator_t<InRange>,
  std::ranges::iterator_t<OutRange>
>
exclusive_scan(InRange&& in, OutRange&& out,
  InitialValue initial_value,
  BinaryOp op,
  op_identity<IdentityValue> op_id)
  requires(std::is_same_v<IdentityValue, no_identity_t> ||
    std::is_invocable_r_v<
      std::ranges::range_value_t<OutRange>,
      BinaryOp,
      decltype(
        identity_value<std::ranges::range_value_t<InRange>>(op_id)
      ),
      std::ranges::range_value_t<InRange>
    >
  )
{
  using in_value_type = std::ranges::range_value_t<InRange>;
  // FIXME this probably won't work for expression templates.
  using result_type = std::remove_cvref_t<
    decltype(op(initial_value, std::declval<in_value_type>()))>;

  auto in_beg = std::ranges::begin(in);
  auto in_end = std::ranges::end(in);
  auto out_beg = std::ranges::begin(out);
  auto out_end = std::ranges::end(out);

  if constexpr (! std::is_same_v<IdentityValue, no_identity_t>) {
    // Only parallel algorithms need the identity value.
    // For testing, though, we exercise getting and using it.
    if (in_beg != in_end) {
      [[maybe_unused]] auto id =
        identity_value<in_value_type>(op_id);
      assert(op(id, *in_beg) == *in_beg);
      assert(op(*in_beg, id) == *in_beg);
    }
  }

  auto in_iter = in_beg;
  auto out_iter = out_beg;
  result_type total = initial_value;
  for (;
       in_iter != in_end && out_iter != out_end;
       ++in_iter, ++out_iter) {
    *out_iter = total;
    total = op(total, *in_iter);
  }
  return {in_iter, out_iter};
}

#endif // OP_IDENTITY_STRUCT

#if defined(POSITION_ONLY)

template<
  std::ranges::forward_range InRange,
  std::ranges::forward_range OutRange,
  class InitialValue,
  class BinaryOp = std::plus<>,
  class IdentityValue = std::ranges::range_value_t<InRange>
>
requires(
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    std::ranges::range_value_t<InRange>,
    std::ranges::range_value_t<InRange>
  > &&
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    InitialValue,
    std::ranges::range_value_t<InRange>
  >
)
std::ranges::in_out_result<
  std::ranges::iterator_t<InRange>,
  std::ranges::iterator_t<OutRange>
>
exclusive_scan(InRange&& in, OutRange&& out,
  InitialValue initial_value,
  BinaryOp op,
  IdentityValue id)
  requires(std::is_same_v<std::remove_cvref_t<IdentityValue>, no_identity_t> ||
    std::is_invocable_r_v<
      std::ranges::range_value_t<OutRange>,
      BinaryOp,
      IdentityValue,
      std::ranges::range_value_t<InRange>
    >
  )
{
  using in_value_type = std::ranges::range_value_t<InRange>;
  // FIXME this probably won't work for expression templates.
  using result_type = std::remove_cvref_t<
    decltype(op(initial_value, std::declval<in_value_type>()))>;

  auto in_beg = std::ranges::begin(in);
  auto in_end = std::ranges::end(in);
  auto out_beg = std::ranges::begin(out);
  auto out_end = std::ranges::end(out);

  if constexpr (! std::is_same_v<IdentityValue, no_identity_t>) {
    // Only parallel algorithms need the identity value.
    // For testing, though, we exercise getting and using it.
    if (in_beg != in_end) {
      assert(op(id, *in_beg) == *in_beg);
      assert(op(*in_beg, id) == *in_beg);
    }
  }

  auto in_iter = in_beg;
  auto out_iter = out_beg;
  result_type total = initial_value;
  for (;
       in_iter != in_end && out_iter != out_end;
       ++in_iter, ++out_iter) {
    *out_iter = total;
    total = op(total, *in_iter);
  }
  return {in_iter, out_iter};
}

#endif // POSITION_ONLY

#if ! defined(BINARY_OPERATION_STRUCT)

// User only specifies the binary operation,
// not an identity value.
// Algorithm determines default identity as
// value-initialized value type of the input range.
//
// If you want no_identity_t, you have to spell it
// explicitly by calling one of the overheads
// that takes an identity value.
template<
  std::ranges::forward_range InRange,
  std::ranges::forward_range OutRange,
  class InitialValue,
  class BinaryOp
>
requires(
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    std::ranges::range_value_t<InRange>,
    std::ranges::range_value_t<InRange>
  > &&
  std::is_invocable_r_v<
    std::ranges::range_value_t<OutRange>,
    BinaryOp,
    InitialValue,
    std::ranges::range_value_t<InRange>
  >
)
std::ranges::in_out_result<
  std::ranges::iterator_t<InRange>,
  std::ranges::iterator_t<OutRange>
>
exclusive_scan(InRange&& in, OutRange&& out,
  InitialValue initial_value, BinaryOp op)
  requires(std::is_invocable_r_v<
      std::ranges::range_value_t<OutRange>,
      BinaryOp,
#if defined(POSITION_ONLY)
      std::ranges::range_value_t<InRange>,
#else
      decltype(        
        identity_value<std::ranges::range_value_t<InRange>>(
#  if defined(OP_IDENTITY_STRUCT)
          op_identity{}
#  endif
        )
      ),
#endif
      std::ranges::range_value_t<InRange>
    >
  )
{
#if defined(OP_IDENTITY_STRUCT)
  return ::p3732::exclusive_scan(std::forward<InRange>(in),
    std::forward<OutRange>(out),
    initial_value,
    op, op_identity{});
#elif defined(POSITION_ONLY)
  return ::p3732::exclusive_scan(std::forward<InRange>(in),
    std::forward<OutRange>(out),
    initial_value,
    op, std::ranges::range_value_t<InRange>{});
#endif // BINARY_OPERATION_STRUCT
}

#endif // ! BINARY_OPERATION_STRUCT

} // namespace p3732

namespace test {

// Binary operator is the usual arithmetic plus;
// identity value is the usual zero.
void exclusive_scan_plus_and_zero() {
  constexpr int initial_value = 2;
  constexpr int flag = -100000;
  std::vector in{-3, 5, -7, 11, -13, 17};
  // Input sequence: 2, -3,  5, -7, 11, -13, 17
  // Exclusive scan:     2, -1,  4, -3,   8, -5
  std::vector expected_out{2, -1, 4, -3, 8, -5};
  std::vector out(in.size(), flag);

  std::exclusive_scan(in.begin(), in.end(), out.begin(),
    initial_value, std::plus{});
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  p3732::exclusive_scan(in, out, initial_value, std::plus{});
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // This assumes that we let users omit the identity.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

#if defined(BINARY_OPERATION_STRUCT)
  // User specifies identity value explicitly.
  //
  // We generally would want users to rely on CTAD
  // for binary_operation, since there's no way to 
  // get the type of an inline lambda and pass the
  // lambda to a function at the same time
  // (as two inline lambdas have different types).
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; },
      0
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User specifies identity value as a compile-time constant.
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; },
      p3732::constant<int, 0>{}
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User does not specify identity value at all;
  // use the default identity value.
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; }
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User wants the algorithm not to assume
  // that an identity value exists.
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; },
      p3732::no_identity
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)
  // User specifies both template argument
  // (type of identity value) and identity value explicitly.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity<int>{0}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User relies on CTAD
  // and specifies identity value explicitly.
  //
  // If op_identity has a default template argument,
  // then it needs a deduction guide to make this work.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity{0}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User relies on CTAD _and_ a default identity value.
  //
  // This only works if op_identity has a default template argument.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity{}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User omits "op_identity" type and relies on
  // curly-brace initialization with an identity value.
  // 
  // Should we even permit this?
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    {0}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User omits "op_identity" type and relies on
  // the default identity value.
  //
  // This works even if op_identity lacks both
  // a default template argument and a deduction guide.
  //
  // Should we even permit this?
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    {}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User specifies identity value as a compile-time constant
  // and relies on op_identity CTAD.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity{p3732::constant<int, 0>{}}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User specifies type of identity explicitly
  // (as a compile-time constant) but relies on default value.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity<p3732::constant<int, 0>>{}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User wants the algorithm not to assume
  // that an identity value exists.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::no_op_identity
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // OP_IDENTITY_STRUCT

#if defined(POSITION_ONLY)
  // User specifies identity value explicitly.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    0
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User specifies identity value as a compile-time constant.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::constant<int, 0>{}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // User wants the algorithm not to assume
  // that an identity value exists.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::no_identity
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // POSITION_ONLY
}

// The "min tropical semiring" a.k.a. "min-plus semiring"
// defines binary addition as minimum, and binary multiplication as addition.
// Additive identity is +Inf and multiplicative identity is zero.
class min_plus_semiring {
public:
  constexpr min_plus_semiring() = default;
  explicit constexpr min_plus_semiring(double value) : value_(value) {}

  constexpr double value() const { return value_; }

  friend constexpr min_plus_semiring
  operator+(min_plus_semiring x, min_plus_semiring y) {
    return min_plus_semiring{std::fmin(x.value_, y.value_)};
  }

  friend constexpr min_plus_semiring
  operator*(min_plus_semiring x, min_plus_semiring y) {
    // that's right, it's plus and not times
    return min_plus_semiring{x.value_ + y.value_};
  }

  friend constexpr bool
  operator==(min_plus_semiring, min_plus_semiring) = default;

private:
  static constexpr double additive_identity =
    std::numeric_limits<double>::infinity();

  double value_ = additive_identity;
};

// This is like exclusive_scan_plus_and_zero
// in that it relies on plus and the default identity value,
// except that it uses a custom number type.
void exclusive_scan_min_plus_semiring() {
  constexpr auto initial_value = min_plus_semiring{2.0};
  constexpr auto flag = min_plus_semiring{-100000.0};
  std::vector in{
    min_plus_semiring{ -3.0},
    min_plus_semiring{  5.0},
    min_plus_semiring{ -7.0},
    min_plus_semiring{ 11.0},
    min_plus_semiring{-13.0},
    min_plus_semiring{ 17.0}
  };
  // Input sequence:      2, -3,  5, -7, 11, -13,  17
  // Exclusive plus scan: 2, -3, -3, -7, -7, -13
  std::vector expected_out{
    min_plus_semiring{  2.0},
    min_plus_semiring{ -3.0},
    min_plus_semiring{ -3.0},
    min_plus_semiring{ -7.0},
    min_plus_semiring{ -7.0},
    min_plus_semiring{-13.0}
  };
  std::vector out(in.size(), flag);

  std::exclusive_scan(in.begin(), in.end(), out.begin(),
    initial_value, std::plus{});
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  p3732::exclusive_scan(in, out, initial_value, std::plus{});
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // This assumes that we let users omit the identity.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

#if defined(BINARY_OPERATION_STRUCT)
  // Specify identity value
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; },
      min_plus_semiring()
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // Use default identity value
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; }
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)
  // Identity argument is explicitly wrapped.
  // All of these cases need to work in this design.

  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity<min_plus_semiring>{min_plus_semiring()}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // If op_identity has a default template argument,
  // then it needs a deduction guide to make this work.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity{min_plus_semiring()}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // op_identity needs a default template argument to make this work.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::op_identity{}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // Identity argument is implicit but requires curly braces.
  // Should this be permitted?

  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    {min_plus_semiring()}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  // This works even if op_identity lacks both
  // a default template argument and a deduction guide.
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    {}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

#endif // OP_IDENTITY_STRUCT
}

// "Manual" means instead of writing a type that implements
// the min-plus semiring, we use plain double and supply the
// binary operator and identity value by hand.
//
// This use case REQUIRES an identity value,
// at least in the parallel case.
void exclusive_scan_min_plus_semiring_manual() {
  constexpr auto initial_value = double{2.0};
  constexpr auto flag = double{-100000.0};
  std::vector in{
    double{ -3.0},
    double{  5.0},
    double{ -7.0},
    double{ 11.0},
    double{-13.0},
    double{ 17.0}
  };
  // Input sequence:      2, -3,  5, -7, 11, -13,  17
  // Exclusive plus scan: 2, -3, -3, -7, -7, -13
  std::vector expected_out{
    double{  2.0},
    double{ -3.0},
    double{ -3.0},
    double{ -7.0},
    double{ -7.0},
    double{-13.0}
  };
  std::vector out(in.size(), flag);

  //
  // User specifies identity value explicitly.
  //

#if defined(BINARY_OPERATION_STRUCT)
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      std::ranges::min,
      double(std::numeric_limits<double>::infinity())
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)
  // If op_identity has a default template argument,
  // then it needs a deduction guide to make this work.
  p3732::exclusive_scan(in, out, initial_value,
    std::ranges::min,
    p3732::op_identity{
      std::numeric_limits<double>::infinity()
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

  p3732::exclusive_scan(in, out, initial_value,
    std::ranges::min,
    p3732::op_identity<double>{
      std::numeric_limits<double>::infinity()
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif

#if defined(POSITION_ONLY)
  p3732::exclusive_scan(in, out, initial_value,
    std::ranges::min,
    std::numeric_limits<double>::infinity()
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // POSITION_ONLY

  //
  // User specifies identity as a compile-time value.
  //
  // This doesn't work with std::ranges::min
  // if we deduce the template argument T,
  // even if the compile-time value type
  // has overloaded mixed operator<,
  // because std::ranges::min assumes that
  // the two values to compare have the same type.
  // Users also aren't allowed to say
  // std::ranges::min<double>,
  // so they have to use a lambda instead.
  //

#if defined(BINARY_OPERATION_STRUCT)
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      //std::ranges::min,
      [] (auto x, auto y) { return std::fmin(x, y); },
      p3732::constant<
        double,
        std::numeric_limits<double>::infinity()
      >{}
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)
  p3732::exclusive_scan(in, out, initial_value,
    //std::ranges::min,
    [] (auto x, auto y) { return std::fmin(x, y); },
    p3732::op_identity{
      p3732::constant<
        double,
        std::numeric_limits<double>::infinity()
      >{}
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // OP_IDENTITY_STRUCT

#if defined(POSITION_ONLY)
  p3732::exclusive_scan(in, out, initial_value,
    //std::ranges::min,
    [] (auto x, auto y) { return std::fmin(x, y); },
    p3732::constant<
      double,
      std::numeric_limits<double>::infinity()
    >{}
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // POSITION_ONLY

  //
  // Use cases that we might not want to permit
  //

#if defined(OP_IDENTITY_STRUCT)
  // Identity argument is implicit but requires curly braces.
  //
  // Should this be permitted?
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return std::fmin(x, y); },
    {
      std::numeric_limits<double>::infinity()
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // OP_IDENTITY_STRUCT
}

// Custom number type with no identity value
// (that cannot be value-initialized).
//
// The integer variant of the min-plus semiring
// still defines binary addition as minimum
// and binary multiplication as addition.
// However, the additive identity (+Inf)
// can't be represented as an integer.
// A user-defined type like this could add extra state
// (e.g., a bool is_finite_ = false), so that a
// value-initialized object would represent +Inf.
// However, that would make it hard for the type
// to interact with existing libraries that expect
// sizeof(integer_min_plus_semiring) == sizeof(int).
// It would also complicate the implementation.
// For example, the (int value_, bool is_finite_=false)
// implementation would not have a unique bit representation
// of +Inf.
class integer_min_plus_semiring {
public:
  explicit constexpr integer_min_plus_semiring(int value) : value_(value) {}

  constexpr int value() const { return value_; }

  friend constexpr integer_min_plus_semiring
  operator+(integer_min_plus_semiring x, integer_min_plus_semiring y) {
    return integer_min_plus_semiring{x.value_ <= y.value_ ? x.value_ : y.value_};
  }

  friend constexpr integer_min_plus_semiring
  operator*(integer_min_plus_semiring x, integer_min_plus_semiring y) {
    // that's right, it's plus and not times
    return integer_min_plus_semiring{x.value_ + y.value_};
  }

  friend constexpr bool
  operator==(integer_min_plus_semiring, integer_min_plus_semiring) = default;

private:
  int value_;
};

// integer_min_plus_semiring doesn't have an identity value.
void exclusive_scan_integer_min_plus_semiring() {
  constexpr auto initial_value = integer_min_plus_semiring{2};
  constexpr auto flag = integer_min_plus_semiring{-100000};
  std::vector in{
    integer_min_plus_semiring{ -3},
    integer_min_plus_semiring{  5},
    integer_min_plus_semiring{ -7},
    integer_min_plus_semiring{ 11},
    integer_min_plus_semiring{-13},
    integer_min_plus_semiring{ 17}
  };
  // Input sequence:      2, -3,  5, -7, 11, -13,  17
  // Exclusive plus scan: 2, -3, -3, -7, -7, -13
  std::vector expected_out{
    integer_min_plus_semiring{  2},
    integer_min_plus_semiring{ -3},
    integer_min_plus_semiring{ -3},
    integer_min_plus_semiring{ -7},
    integer_min_plus_semiring{ -7},
    integer_min_plus_semiring{-13}
  };
  std::vector out(in.size(), flag);

  std::exclusive_scan(in.begin(), in.end(), out.begin(),
    initial_value, std::plus{});
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);

#if defined(BINARY_OPERATION_STRUCT)
  p3732::exclusive_scan(in, out, initial_value,
    p3732::binary_operation{
      [](auto x, auto y) { return x + y; },
      p3732::no_identity
    }
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // BINARY_OPERATION_STRUCT

#if defined(OP_IDENTITY_STRUCT)
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::no_op_identity
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // OP_IDENTITY_STRUCT

#if defined(POSITION_ONLY)
  p3732::exclusive_scan(in, out, initial_value,
    [](auto x, auto y) { return x + y; },
    p3732::no_identity
  );
  assert(expected_out == out);
  std::fill(out.begin(), out.end(), flag);
#endif // POSITION_ONLY
}

} // namespace test

int main()
{
  test::exclusive_scan_plus_and_zero();
  test::exclusive_scan_min_plus_semiring();
  test::exclusive_scan_min_plus_semiring_manual();
  test::exclusive_scan_integer_min_plus_semiring();
  return 0;
}
