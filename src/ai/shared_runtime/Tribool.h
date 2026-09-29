// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace AISharedRuntime
{
	class tribool;

	namespace detail
	{
		struct indeterminate_t {};
	}

	/// Tests whether a tribool is indeterminate. Its name also serves as the
	/// third value: assign or pass `indeterminate` like `true` or `false`.
	constexpr bool indeterminate(tribool x, detail::indeterminate_t = detail::indeterminate_t());

	typedef bool (*indeterminate_keyword_t)(tribool, detail::indeterminate_t);

	/// Three-state boolean: true, false or indeterminate (unknown).
	///
	/// Replaces tribool with the same semantics, which the shared AI runtime
	/// depends on for its decisions:
	/// - `if (x)` and other bool contexts are true only when x is true;
	/// - `!`, `&&`, `||`, `==` and `!=` follow three-valued (Kleene) logic, so
	///   `!indeterminate` is indeterminate and comparing with an indeterminate
	///   value gives indeterminate;
	/// - a default-constructed tribool is false.
	/// Save streams store the AI_SHARED_RUNTIME_TRIBOOL_* codes from Runtime.h, never this
	/// class's representation.
	class tribool
	{
	public:
		constexpr tribool() : value(false_value) {}
		constexpr tribool(bool initial) : value(initial ? true_value : false_value) {}
		constexpr tribool(indeterminate_keyword_t) : value(indeterminate_value) {}

		constexpr explicit operator bool() const { return value == true_value; }

	private:
		enum Value { false_value, true_value, indeterminate_value };
		Value value;

		friend constexpr bool indeterminate(tribool x, detail::indeterminate_t);
	};

	constexpr bool indeterminate(tribool x, detail::indeterminate_t)
	{
		return x.value == tribool::indeterminate_value;
	}

	constexpr tribool operator!(tribool x)
	{
		return indeterminate(x) ? tribool(indeterminate) : tribool(!static_cast<bool>(x));
	}

	constexpr tribool operator&&(tribool x, tribool y)
	{
		return (static_cast<bool>(!x) || static_cast<bool>(!y)) ? tribool(false)
			: (static_cast<bool>(x) && static_cast<bool>(y)) ? tribool(true)
			: tribool(indeterminate);
	}
	constexpr tribool operator&&(tribool x, bool y) { return x && tribool(y); }
	constexpr tribool operator&&(bool x, tribool y) { return tribool(x) && y; }
	constexpr tribool operator&&(indeterminate_keyword_t, tribool x) { return tribool(indeterminate) && x; }
	constexpr tribool operator&&(tribool x, indeterminate_keyword_t) { return x && tribool(indeterminate); }

	constexpr tribool operator||(tribool x, tribool y)
	{
		return (static_cast<bool>(!x) && static_cast<bool>(!y)) ? tribool(false)
			: (static_cast<bool>(x) || static_cast<bool>(y)) ? tribool(true)
			: tribool(indeterminate);
	}
	constexpr tribool operator||(tribool x, bool y) { return x || tribool(y); }
	constexpr tribool operator||(bool x, tribool y) { return tribool(x) || y; }
	constexpr tribool operator||(indeterminate_keyword_t, tribool x) { return tribool(indeterminate) || x; }
	constexpr tribool operator||(tribool x, indeterminate_keyword_t) { return x || tribool(indeterminate); }

	constexpr tribool operator==(tribool x, tribool y)
	{
		return (indeterminate(x) || indeterminate(y)) ? tribool(indeterminate)
			: tribool(static_cast<bool>(x) == static_cast<bool>(y));
	}
	constexpr tribool operator==(tribool x, bool y) { return x == tribool(y); }
	constexpr tribool operator==(bool x, tribool y) { return tribool(x) == y; }
	constexpr tribool operator==(indeterminate_keyword_t, tribool x) { return tribool(indeterminate) == x; }
	constexpr tribool operator==(tribool x, indeterminate_keyword_t) { return x == tribool(indeterminate); }

	constexpr tribool operator!=(tribool x, tribool y)
	{
		return (indeterminate(x) || indeterminate(y)) ? tribool(indeterminate)
			: tribool(static_cast<bool>(x) != static_cast<bool>(y));
	}
	constexpr tribool operator!=(tribool x, bool y) { return x != tribool(y); }
	constexpr tribool operator!=(bool x, tribool y) { return tribool(x) != y; }
	constexpr tribool operator!=(indeterminate_keyword_t, tribool x) { return tribool(indeterminate) != x; }
	constexpr tribool operator!=(tribool x, indeterminate_keyword_t) { return x != tribool(indeterminate); }
}
