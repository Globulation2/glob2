// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeneratorPackage.h"
#include "GenerationContext.h"
#include "Game.h"
#include "Grid.h"
#include "scripting/javascript/QuickJSOwnership.h"
#include <cmath>
#include "Glob2Math.h"
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <typeindex>
#include <type_traits>
#include <tuple>
#include <span>
#include <set>
#include <deque>

namespace MapGeneration
{
struct Tessellation;
struct Symmetry;
struct RasterFit;
} // namespace MapGeneration
namespace MapGeneration::JavaScript
{
class Binding;
struct TypeMismatch : std::invalid_argument
{
	using std::invalid_argument::invalid_argument;
};
struct ScriptError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};
struct ResourceError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};
template <class T> struct Record;
template <class T> struct Vector : std::false_type
{
};
template <class T, class A> struct Vector<std::vector<T, A>> : std::true_type
{
	using Element = T;
};
template <class T> struct Array : std::false_type
{
};
template <class T, size_t N> struct Array<std::array<T, N>> : std::true_type
{
	using Element = T;
	static constexpr size_t size = N;
};
template <class T> struct Span : std::false_type
{
};
template <class T, size_t N> struct Span<std::span<T, N>> : std::true_type
{
};
template <class T, size_t N> struct SpanAlias
{
	std::span<T, N> data;
	std::shared_ptr<void> parent;
};
template <class T> struct Pair : std::false_type
{
};
template <class A, class B> struct Pair<std::pair<A, B>> : std::true_type
{
};
template <class T> struct Optional : std::false_type
{
};
template <class T> struct Optional<std::optional<T>> : std::true_type
{
	using Element = T;
};
template <class T> struct Dictionary : std::false_type
{
};
template <class K, class V, class C, class A>
struct Dictionary<std::map<K, V, C, A>> : std::true_type
{
	using Key = K;
	using Value = V;
};
template <class T> struct Function : std::false_type
{
};
template <class R, class... A> struct Function<std::function<R(A...)>> : std::true_type
{
	static std::function<R(A...)> read(Binding &, JSValueConst);
	static JSValue write(Binding &, std::function<R(A...)>);
};
struct NativeBox
{
	// An owner keeps storage alive; a lease separately limits borrowed dependencies.
	// Owning a class does not extend the lifetime of the context/RNG it refers to.
	std::type_index type{typeid(void)};
	std::shared_ptr<void> owner;
	void *pointer = nullptr;
	bool readonly = false;
	bool buffer = false;
	std::shared_ptr<bool> alive;
};
class Binding
{
  public:
	const Package &package;
	const GenerationRequest &request;
	Game *game;
	GenerationContext *generation;
	bool readonly, inspecting;
	JSRuntime *runtime = nullptr;
	JSContext *ctx = nullptr;
	static constexpr std::uint64_t InterpreterWork = 1000000000;
	static constexpr std::uint64_t NativeWork = 2000000000;
	std::uint64_t fuel = InterpreterWork;
	std::uint64_t nativeFuel = NativeWork;
	size_t nativeBytes = 0;
	bool exhausted = false;
	enum class Exhaustion
	{
		None,
		InterpreterWork,
		NativeWork,
		NativeMemory,
		CallbackDepth,
		Operations,
		CachedDesigns,
		HostFailure
	};
	// The first resource failure survives script catches and later interrupts.
	// Keep only a bounded cause: recording exhaustion must not allocate memory.
	Exhaustion exhaustion = Exhaustion::None;
	unsigned conversionDepth = 0, callbackDepth = 0;
	std::vector<std::shared_ptr<bool>> leases;
	std::shared_ptr<bool> generationLease;
	std::set<NativeBox *> boxes;
	std::deque<std::string> strings;
	std::map<std::string, JSValue> cachedDesigns;
	std::map<std::type_index, JSValue> prototypes;
	struct Operation
	{
		using Call = std::function<JSValue(JSValueConst, int, JSValueConst *)>;
		std::string name;
		// Registration may grow the vector while a call is running. Shared callable
		// storage keeps that call stable without copying its captured payload.
		std::shared_ptr<Call> call;
	};
	std::vector<Operation> operations;
	std::map<std::string, std::vector<unsigned>> overloads;
	static JSClassID nativeClass;
	static constexpr size_t MaximumOperations = 30000;
	Binding(const Package &, const GenerationRequest &, Game *, GenerationContext *, bool, bool);
	~Binding();
	template <class T> void preflight(std::string_view name, const T &value)
	{
		if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
		{
			if (name != "seed" && name != "rSeed" && std::abs(double(value)) > 1048576.0)
				throw TypeMismatch("Toolkit argument exceeds supported magnitude");
			if (name == "w" || name == "h" || name == "width" || name == "height")
				if (value < 1 || value > 1024)
					throw TypeMismatch("Toolkit dimensions must be 1..1024");
			if (name == "period" && (value < 1 || value > 1024))
				throw TypeMismatch("Noise period must be 1..1024");
			if (name == "octaves" && (value < 1 || value > 16))
				throw TypeMismatch("Noise octaves must be 1..16");
			if (name == "team" && (value < 0 || value >= request.nbTeams))
				throw TypeMismatch("Invalid toolkit colony index");
			if (name == "teams" && (value < 1 || value > Team::MAX_COUNT))
				throw TypeMismatch("Invalid toolkit colony count");
		}
	}
	template <class T> void preflightField(std::string_view name, const T &value)
	{
		// Record fields can use zero/-1 sentinels. Semantic dimensions are checked
		// on the completed record, rather than while filling a partial initializer.
		preflight(name == "seed" || name == "rSeed" ? name : "element", value);
	}
	void charge(uint64_t amount = 1);
	void chargeNative(uint64_t amount);
	void allocate(size_t amount);
	void exhaust(Exhaustion cause);
	const char *exhaustionMessage() const;
	[[noreturn]] void rejectResource(Exhaustion cause);
	[[noreturn]] void fail();
	void check(JSValueConst value)
	{
		if (JS_IsException(value))
			fail();
	}
	JSValue evaluate();
	JSValue context();
	void add(JSValueConst object, const std::string &name,
			 std::function<JSValue(JSValueConst, int, JSValueConst *)> operation);
	void overload(JSValueConst object, const std::string &name,
				  std::function<JSValue(JSValueConst, int, JSValueConst *)> operation);
	static JSValue dispatch(JSContext *, JSValueConst, int, JSValueConst *, int, JSValue *);
	static void finalize(JSRuntime *, JSValue);
	NativeBox *box(JSValueConst value) const;
	std::shared_ptr<bool> handleDependencyLease(JSValueConst value) const
	{
		auto b = box(value);
		return b ? b->alive : std::shared_ptr<bool>{};
	}
	std::shared_ptr<bool> contextDependencyLease() const { return generationLease; }
	// Callback arguments and classes borrowing them share this expiration boundary.
	class CallbackScope
	{
		Binding &e;
		Game *previousGame;
		GenerationContext *previousGeneration;
		bool previousReadonly;
		std::shared_ptr<bool> previousGenerationLease;
		std::shared_ptr<bool> lease;

	  public:
		explicit CallbackScope(Binding &);
		~CallbackScope();
		template <class T> void bindHost(T &value)
		{
			if constexpr (std::is_same_v<std::remove_cv_t<T>, Game>)
			{
				e.game = const_cast<Game *>(&value);
				e.readonly = e.readonly || std::is_const_v<T>;
			}
			if constexpr (std::is_same_v<std::remove_cv_t<T>, GenerationContext>)
			{
				e.generation = const_cast<GenerationContext *>(&value);
				e.generationLease = lease;
			}
		}
	};
	template <class T> void validateNative(const T &result)
	{
		if constexpr (std::is_same_v<T, Torus>)
		{
			if (result.w <= 0 || result.h <= 0 || result.w > 1024 || result.h > 1024)
				throw TypeMismatch("Invalid torus dimensions");
		}
		else if constexpr (std::is_same_v<T, Axes>)
		{
			if (result.width <= 0 || result.height <= 0 || result.width > 1024 ||
				result.height > 1024)
				throw TypeMismatch("Invalid axes dimensions");
		}
		else if constexpr (std::is_same_v<T, Tessellation>)
		{
			validateNative(result.t);
			if (result.columns <= 0 || result.rows <= 0 || result.columns > 1024 ||
				result.rows > 1024)
				throw TypeMismatch("Invalid tessellation dimensions");
		}
		else if constexpr (std::is_same_v<T, Symmetry>)
		{
			if (result.width <= 0 || result.height <= 0 || result.width > 1024 ||
				result.height > 1024)
				throw TypeMismatch("Invalid symmetry dimensions");
		}
		else if constexpr (std::is_same_v<T, RasterFit>)
		{
			if (result.num <= 0 || result.den <= 0)
				throw TypeMismatch("Invalid raster scale");
		}
	}
	template <class T> T &native(JSValueConst value, bool mutableAccess = false)
	{
		auto b = box(value);
		if (!b || b->type != typeid(T))
			throw TypeMismatch("Expected native toolkit handle");
		if (!b->pointer || (b->alive && !*b->alive))
			throw TypeMismatch("Expired callback handle");
		if (mutableAccess && b->readonly)
			throw TypeMismatch("Read-only toolkit handle");
		auto &result = *static_cast<T *>(b->pointer);
		validateNative(result);
		return result;
	}
	template <class T>
	JSValue handle(T *pointer, std::shared_ptr<void> owner = {}, bool ro = false,
				   std::shared_ptr<bool> alive = {})
	{
		using U = std::remove_const_t<T>;
		if (!owner && !alive && !leases.empty())
			alive = leases.back();
		auto found = prototypes.find(typeid(U));
		if (found == prototypes.end())
		{
			auto prototype = JS_NewObject(ctx);
			check(prototype);
			prototypes.emplace(typeid(U), prototype);
			attach<U>(prototype);
			found = prototypes.find(typeid(U));
		}
		allocate(sizeof(NativeBox) + 64); // Box and the tracking-set node.
		Script::JSValueOwner value(ctx, JS_NewObjectProtoClass(ctx, found->second, nativeClass));
		check(value.get());
		auto b = std::make_unique<NativeBox>(NativeBox{
			typeid(U), std::move(owner), const_cast<U *>(pointer), ro || std::is_const_v<T>,
			Vector<U>::value || Array<U>::value || Span<U>::value, std::move(alive)});
		boxes.insert(b.get());
		JS_SetOpaque(value.get(), b.release());
		return value.release();
	}
	template <class T>
	JSValue dependentHandle(T *pointer, std::shared_ptr<void> owner,
							std::shared_ptr<bool> dependencyLease)
	{
		return handle(pointer, std::move(owner), false, std::move(dependencyLease));
	}
	template <class T> JSValue borrowed(T &value)
	{
		auto lease = leases.empty() ? std::shared_ptr<bool>{} : leases.back();
		return handle(&value, {}, std::is_const_v<T>, lease);
	}
	// Count owned payloads recursively before copying or retaining native records.
	template <class T> std::uint64_t footprint(const T &value)
	{
		charge();
		using U = std::remove_cvref_t<T>;
		std::uint64_t bytes = sizeof(U);
		if constexpr (Vector<U>::value || Array<U>::value || std::is_array_v<U>)
		{
			if constexpr (Vector<U>::value)
				bytes += std::uint64_t(value.capacity()) * sizeof(typename U::value_type);
			for (const auto &item : value)
				bytes += footprint(item) - sizeof(item);
		}
		else if constexpr (Dictionary<U>::value)
			for (const auto &[key, item] : value)
				bytes += 64 + footprint(key) + footprint(item);
		else if constexpr (std::is_same_v<U, std::string>)
			bytes += value.capacity();
		else if constexpr (Optional<U>::value)
		{
			if (value)
				bytes += footprint(*value) - sizeof(*value);
		}
		else if constexpr (Pair<U>::value)
			bytes += footprint(value.first) - sizeof(value.first) + footprint(value.second) -
					 sizeof(value.second);
		else if constexpr (Function<U>::value)
			// Toolkit-generated functions capture shared dependencies or a JS owner.
			// Reserve the bounded callable/control-block allocation on copies.
			bytes += value ? 128 : 0;
		else if constexpr (requires { Record<U>::footprint(*this, value); })
			return Record<U>::footprint(*this, value);
		return bytes;
	}
	template <class T> T copyNative(const T &value)
	{
		// References borrow; by-value arguments and clones reserve before copying.
		if constexpr (!std::is_trivially_copy_constructible_v<T>)
			allocate(footprint(value));
		return T(value);
	}
	template <class T> JSValue write(T &&value);
	template <class T> T read(JSValueConst value);
	template <class T, class F> T record(JSValueConst value, F fields)
	{
		if constexpr (std::is_default_constructible_v<T> && std::is_copy_constructible_v<T>)
		{
			if (!JS_IsObject(value))
				throw TypeMismatch("Expected toolkit record");
			T result{};
			fields(*this, result, value);
			validateNative(result);
			return result;
		}
		else
			throw TypeMismatch("Construct this toolkit class using its factory");
	}
	template <class T> void attach(JSValueConst value);
	template <class T> T defaultRecord()
	{
		if constexpr (std::is_default_constructible_v<T>)
			return T{};
		else
			throw TypeMismatch("This toolkit class requires constructor arguments");
	}
	template <class T>
	void field(JSValueConst value, const char *name, T NativeBox::*member) = delete;
	template <class T, class M> void field(JSValueConst value, const char *name, M T::*member)
	{
		if constexpr (std::is_array_v<M>)
		{
			fieldArray(value, name, member);
		}
		else
		{
			auto getter = addAccessor(
				[member](Binding &e, JSValueConst self, int, JSValueConst *)
				{
					auto &object = e.native<T>(self);
					if constexpr (std::is_arithmetic_v<M> || std::is_enum_v<M> ||
								  std::is_same_v<M, std::string> ||
								  std::is_same_v<M, std::string_view> ||
								  std::is_same_v<M, const char *> || Optional<M>::value ||
								  Pair<M>::value || Function<M>::value)
						return e.write(object.*member);
					else
					{
						auto b = e.box(self);
						return e.handle(&(object.*member), b->owner, b->readonly, b->alive);
					}
				});
			auto setter = addAccessor(
				[member, name](Binding &e, JSValueConst self, int n, JSValueConst *a)
				{
					if (n != 1)
						throw TypeMismatch("Expected field value");
					auto data = e.read<M>(a[0]);
					e.preflightField(name, data);
					e.native<T>(self, true).*member = std::move(data);
					return JS_UNDEFINED;
				});
			Script::JSAtomOwner atom(ctx, JS_NewAtom(ctx, name));
			if (JS_DefinePropertyGetSet(ctx, value, atom.get(), getter, setter,
										JS_PROP_ENUMERABLE) < 0)
				fail();
		}
	}

	template <class T, class E, size_t N>
	void fieldArray(JSValueConst value, const char *name, E (T::*member)[N])
	{
		auto getter = addAccessor(
			[member](Binding &e, JSValueConst self, int, JSValueConst *)
			{
				auto &object = e.native<T>(self);
				auto b = e.box(self);
				auto alias = std::make_shared<SpanAlias<E, N>>(
					SpanAlias<E, N>{std::span<E, N>(object.*member), b->owner});
				e.allocate(sizeof(SpanAlias<E, N>));
				return e.handle(&alias->data, alias, b->readonly, b->alive);
			});
		auto setter = addAccessor(
			[member](Binding &e, JSValueConst self, int n, JSValueConst *a)
			{
				if (n != 1)
					throw TypeMismatch("Expected fixed array value");
				auto &object = e.native<T>(self, true);
				auto data = e.read<std::array<E, N>>(a[0]);
				std::copy(data.begin(), data.end(), object.*member);
				return JS_UNDEFINED;
			});
		Script::JSAtomOwner atom(ctx, JS_NewAtom(ctx, name));
		if (JS_DefinePropertyGetSet(ctx, value, atom.get(), getter, setter, JS_PROP_ENUMERABLE) < 0)
			fail();
	}

	JSValue addAccessor(std::function<JSValue(Binding &, JSValueConst, int, JSValueConst *)>);
	template <class T, class M>
	void readField(T &target, JSValueConst object, const char *name, M T::*member)
	{
		Script::JSValueOwner v(ctx, JS_GetPropertyStr(ctx, object, name));
		check(v.get());
		if (!JS_IsUndefined(v.get()))
		{
			if constexpr (std::is_array_v<M>)
			{
				auto data = read<std::array<std::remove_extent_t<M>, std::extent_v<M>>>(v.get());
				std::copy(data.begin(), data.end(), target.*member);
			}
			else
			{
				auto data = read<M>(v.get());
				preflightField(name, data);
				target.*member = std::move(data);
			}
		}
	}
	struct Depth
	{
		Binding &e;
		explicit Depth(Binding &e) : e(e)
		{
			if (++e.conversionDepth > 64)
			{
				--e.conversionDepth;
				throw TypeMismatch("Toolkit data nesting exceeds 64");
			}
		}
		~Depth() { --e.conversionDepth; }
	};

  private:
	void initializeRuntime();
	void initializeNativeClass();
	void initializeGlobals();
	void initializeModules();
	JSValue requestValue();
	JSValue toolkitValue();
	void installContextRandom(JSValueConst);
	void installContextTelemetry(JSValueConst);
	void installContextDesign(JSValueConst);
	void installContextWorld(JSValueConst);
	void installContextBuffers(JSValueConst);
	void setProperty(JSValueConst object, const char *name, JSValue value);
	unsigned registerOperation(std::string name,
							   std::function<JSValue(JSValueConst, int, JSValueConst *)>);
};
// Default record adapter: opaque classes must be constructed through their factory.
template <class T> struct Record
{
	static T read(Binding &, JSValueConst) { throw TypeMismatch("Expected toolkit object handle"); }
	static void attach(Binding &, JSValueConst) {}
};
template <class T> T Binding::read(JSValueConst value)
{
	Depth depth(*this);
	charge();
	if constexpr (std::is_same_v<T, bool>)
	{
		if (!JS_IsBool(value))
			throw TypeMismatch("Expected boolean");
		return JS_ToBool(ctx, value);
	}
	else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
	{
		if (!JS_IsNumber(value))
			throw TypeMismatch("Expected number");
		double n;
		if (JS_ToFloat64(ctx, &n, value) < 0)
			fail();
		if (!std::isfinite(n))
			throw TypeMismatch("Toolkit numbers must be finite");
		if constexpr (std::is_enum_v<T>)
		{
			using U = std::underlying_type_t<T>;
			if (glob2_math_trunc(n) != n || n < double(std::numeric_limits<U>::lowest()) ||
				n > double(std::numeric_limits<U>::max()))
				throw TypeMismatch("Enum outside range");
		}
		else if constexpr (std::is_integral_v<T>)
		{
			if (glob2_math_trunc(n) != n || n < double(std::numeric_limits<T>::lowest()) ||
				n > double(std::numeric_limits<T>::max()) || std::abs(n) > 9007199254740991.0)
				throw TypeMismatch("Integer outside exact range");
		}
		else if (std::abs(n) > double(std::numeric_limits<T>::max()))
			throw TypeMismatch("Number outside range");
		return static_cast<T>(n);
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		if (!JS_IsString(value))
			throw TypeMismatch("Expected string");
		Script::JSStringOwner s(ctx, value);
		if (!s.get())
			fail();
		charge(s.length);
		if (s.length > 1048576 ||
			std::string_view(s.get(), s.length).find('\0') != std::string_view::npos)
			throw TypeMismatch("Invalid toolkit string");
		allocate(s.length);
		return {s.get(), s.length};
	}
	else if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, std::string_view>)
	{
		strings.push_back(read<std::string>(value));
		if constexpr (std::is_same_v<T, const char *>)
			return strings.back().c_str();
		else
			return std::string_view(strings.back());
	}
	else if constexpr (Function<T>::value)
		return Function<T>::read(*this, value);
	else if constexpr (Optional<T>::value)
	{
		if (JS_IsNull(value) || JS_IsUndefined(value))
			return {};
		return read<typename Optional<T>::Element>(value);
	}
	else if constexpr (Pair<T>::value)
	{
		if (!JS_IsArray(value))
			throw TypeMismatch("Expected pair array");
		Script::JSValueOwner a(ctx, JS_GetPropertyUint32(ctx, value, 0)),
			b(ctx, JS_GetPropertyUint32(ctx, value, 1));
		return {read<typename T::first_type>(a.get()), read<typename T::second_type>(b.get())};
	}
	else if constexpr (Vector<T>::value || Array<T>::value || Span<T>::value)
	{
		if (auto b = box(value); b && b->type == typeid(T))
		{
			const auto &source = native<T>(value);
			allocate(footprint(source));
			return source;
		}
		const auto nativeBuffer = box(value);
		if (!JS_IsArray(value) && !(nativeBuffer && nativeBuffer->buffer))
			throw TypeMismatch("Expected buffer handle or array");
		Script::JSValueOwner length(ctx, JS_GetPropertyStr(ctx, value, "length"));
		auto n = read<unsigned>(length.get());
		if (n > 1048576)
			throw TypeMismatch("Toolkit array exceeds limit");
		T result{};
		if constexpr (Vector<T>::value)
		{
			allocate(uint64_t(n) * 32);
			result.reserve(n);
		}
		else if (n != Array<T>::size)
			throw TypeMismatch("Wrong fixed array length");
		for (unsigned i = 0; i < n; ++i)
		{
			JSValue item = JS_UNDEFINED;
			if (nativeBuffer)
			{
				Script::JSValueOwner fn(ctx, JS_GetPropertyStr(ctx, value, "get"));
				check(fn.get());
				auto index = JS_NewUint32(ctx, i);
				item = JS_Call(ctx, fn.get(), value, 1, &index);
				JS_FreeValue(ctx, index);
			}
			else
				item = JS_GetPropertyUint32(ctx, value, i);
			Script::JSValueOwner v(ctx, item);
			check(v.get());
			using U = typename T::value_type;
			if constexpr (Vector<T>::value)
			{
				auto element = read<U>(v.get());
				preflight("element", element);
				result.push_back(std::move(element));
			}
			else
			{
				auto element = read<U>(v.get());
				preflight("element", element);
				result[i] = std::move(element);
			}
		}
		return result;
	}
	else
	{
		if (auto b = box(value); b && b->type == typeid(T))
		{
			const auto &source = native<T>(value);
			allocate(footprint(source));
			return source;
		}
		return Record<T>::read(*this, value);
	}
}
template <class T> JSValue Binding::write(T &&value)
{
	using U = std::remove_cvref_t<T>;
	charge();
	if constexpr (std::is_same_v<U, bool>)
		return JS_NewBool(ctx, value);
	else if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U>)
		return JS_NewFloat64(ctx, static_cast<double>(value));
	else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, std::string_view>)
		return JS_NewStringLen(ctx, value.data(), value.size());
	else if constexpr (std::is_same_v<U, const char *> || std::is_same_v<U, char *>)
		return value ? JS_NewString(ctx, value) : JS_NULL;
	else if constexpr (Function<U>::value)
	{
		if (value)
			allocate(footprint(value));
		return Function<U>::write(*this, value);
	}
	else if constexpr (Optional<U>::value)
		return value ? write(*value) : JS_NULL;
	else if constexpr (Pair<U>::value)
	{
		Script::JSValueOwner a(ctx, JS_NewArray(ctx));
		check(a.get());
		if (JS_SetPropertyUint32(ctx, a.get(), 0, write(value.first)) < 0 ||
			JS_SetPropertyUint32(ctx, a.get(), 1, write(value.second)) < 0)
			fail();
		return a.release();
	}
	else if constexpr (std::is_pointer_v<U>)
		return value ? handle(value, {}, std::is_const_v<std::remove_pointer_t<U>>) : JS_NULL;
	else
	{
		allocate(256 + footprint(value));
		auto pointer = std::make_shared<U>(std::forward<T>(value));
		return handle(pointer.get(), pointer);
	}
}
template <class T> void Binding::attach(JSValueConst value)
{
	if constexpr (Vector<T>::value || Array<T>::value || Span<T>::value)
	{
		auto getLength = addAccessor([](Binding &e, JSValueConst self, int, JSValueConst *)
									 { return e.write(e.native<T>(self).size()); });
		Script::JSAtomOwner length(ctx, JS_NewAtom(ctx, "length"));
		JS_DefinePropertyGetSet(ctx, value, length.get(), getLength, JS_UNDEFINED,
								JS_PROP_ENUMERABLE);
		add(value, "get",
			[this](JSValueConst self, int n, JSValueConst *a)
			{
				if (n != 1)
					throw TypeMismatch("get(index)");
				auto &v = native<T>(self);
				auto i = read<unsigned>(a[0]);
				if (i >= v.size())
					throw TypeMismatch("Buffer index outside range");
				if constexpr (std::is_same_v<typename T::value_type, bool>)
					return write(bool(v[i])); // vector<bool> returns a proxy, not a bool reference.
				else
					return write(copyNative(v[i]));
			});
		add(value, "set",
			[this](JSValueConst self, int n, JSValueConst *a)
			{
				if (n != 2)
					throw TypeMismatch("set(index, value)");
				auto &v = native<T>(self, true);
				auto i = read<unsigned>(a[0]);
				if (i >= v.size())
					throw TypeMismatch("Buffer index outside range");
				v[i] = read<typename T::value_type>(a[1]);
				return JS_UNDEFINED;
			});
		add(value, "fill",
			[this](JSValueConst self, int n, JSValueConst *a)
			{
				if (n != 1)
					throw TypeMismatch("fill(value)");
				auto &v = native<T>(self, true);
				auto x = read<typename T::value_type>(a[0]);
				charge(v.size());
				// read() reserves one converted value; fill copies its dynamic payload
				// into every destination, including nested buffers and record fields.
				allocate(uint64_t(v.size()) * (footprint(x) - sizeof(x)));
				std::fill(v.begin(), v.end(), x);
				return JS_UNDEFINED;
			});
		add(value, "clone",
			[this](JSValueConst self, int, JSValueConst *)
			{
				if constexpr (Span<T>::value)
				{
					auto &v = native<T>(self);
					allocate(sizeof(std::vector<typename T::value_type>) +
							 uint64_t(v.size()) * sizeof(typename T::value_type));
					for (const auto &item : v)
						allocate(footprint(item) - sizeof(item));
					return write(std::vector<typename T::value_type>(v.begin(), v.end()));
				}
				else
					return write(copyNative(native<T>(self)));
			});
		add(value, "toArray",
			[this](JSValueConst self, int, JSValueConst *)
			{
				auto &v = native<T>(self);
				Script::JSValueOwner a(ctx, JS_NewArray(ctx));
				check(a.get());
				charge(v.size());
				for (unsigned i = 0; i < v.size(); ++i)
				{
					JSValue item;
					if constexpr (std::is_same_v<typename T::value_type, bool>)
						item = write(bool(v[i]));
					else
						item = write(v[i]);
					if (JS_SetPropertyUint32(ctx, a.get(), i, item) < 0)
						fail();
				}
				return a.release();
			});
	}
	else if constexpr (Dictionary<T>::value)
	{
		add(value, "get",
			[this](JSValueConst self, int n, JSValueConst *a)
			{
				if (n != 1)
					throw TypeMismatch("get(key)");
				auto &map = native<T>(self);
				auto it = map.find(read<typename Dictionary<T>::Key>(a[0]));
				return it == map.end() ? JS_UNDEFINED : write(it->second);
			});
		add(value, "keys",
			[this](JSValueConst self, int n, JSValueConst *)
			{
				if (n)
					throw TypeMismatch("keys()");
				auto &source = native<T>(self);
				using Key = typename Dictionary<T>::Key;
				allocate(uint64_t(source.size()) * sizeof(Key));
				for (const auto &[key, value] : source)
					allocate(footprint(key) - sizeof(key));
				std::vector<typename Dictionary<T>::Key> keys;
				keys.reserve(source.size());
				for (const auto &[k, v] : source)
					keys.push_back(k);
				return write(std::move(keys));
			});
	}
	else if constexpr (std::is_same_v<T, std::mt19937>)
	{
		add(value, "next",
			[this](JSValueConst self, int n, JSValueConst *)
			{
				if (n)
					throw TypeMismatch("next()");
				return write(native<T>(self, true)());
			});
	}
	else
		Record<T>::attach(*this, value);
}
template <class T> class Argument
{
	using U = std::remove_cvref_t<T>;
	Binding &binding;
	std::optional<U> storage;
	U *pointer = nullptr;

  public:
	Argument(Binding &e, JSValueConst v) : binding(e)
	{
		if (auto b = e.box(v); b && b->type == typeid(U))
			pointer = &e.native<U>(v, std::is_lvalue_reference_v<T> &&
										  !std::is_const_v<std::remove_reference_t<T>>);
		else
		{
			if constexpr (std::is_copy_constructible_v<U>)
			{
				if constexpr (std::is_lvalue_reference_v<T> &&
							  !std::is_const_v<std::remove_reference_t<T>> && Vector<U>::value)
					throw TypeMismatch("Mutable vectors require a native buffer handle");
				storage.emplace(e.read<U>(v));
			}
			else
				throw TypeMismatch("Expected constructed toolkit handle");
		}
	}
	decltype(auto) get()
	{
		// Resolve self-owned storage after moves, including tuple construction.
		auto &value = storage ? *storage : *pointer;
		if constexpr (std::is_lvalue_reference_v<T>)
			return static_cast<T>(value);
		else
			return binding.copyNative(value);
	}
};
template <class T> class Argument<T *>
{
	using U = std::remove_const_t<T>;
	std::optional<U> storage;
	T *pointer = nullptr;

  public:
	Argument(Binding &e, JSValueConst v)
	{
		if constexpr (std::is_same_v<std::remove_const_t<T>, Building> ||
					  std::is_same_v<std::remove_const_t<T>, BuildingType>)
			if (JS_IsNull(v) || JS_IsUndefined(v))
				throw TypeMismatch("Required engine object handle is missing");
		if (!JS_IsNull(v) && !JS_IsUndefined(v))
		{
			if constexpr (Function<U>::value || std::is_arithmetic_v<U>)
			{
				storage.emplace(e.read<U>(v));
				e.preflight("pointer", *storage);
			}
			else
				pointer = &e.native<U>(v, !std::is_const_v<T>);
		}
	}
	T *get() { return storage ? &*storage : pointer; }
};
template <class T, size_t N> class Argument<T[N]>
{
	std::array<T, N> value;

  public:
	Argument(Binding &e, JSValueConst v) : value(e.read<std::array<T, N>>(v)) {}
	T *get() { return value.data(); }
};
template <> class Argument<const char *>
{
	std::string value;

  public:
	Argument(Binding &e, JSValueConst v) : value(e.read<std::string>(v)) {}
	const char *get() { return value.c_str(); }
};
template <class R, class... A>
JSValue Function<std::function<R(A...)>>::write(Binding &e, std::function<R(A...)> fn)
{
	if (!fn)
		return JS_NULL;
	return e.addAccessor(
		[fn = std::move(fn)](Binding &e, JSValueConst, int n, JSValueConst *a) -> JSValue
		{
			if (n != sizeof...(A))
				throw TypeMismatch("Wrong callback argument count");
			return [&]<size_t... I>(std::index_sequence<I...>) -> JSValue
			{
				std::tuple<Argument<A>...> args{Argument<A>(e, a[I])...};
				if constexpr (std::is_void_v<R>)
				{
					fn(std::get<I>(args).get()...);
					return JS_UNDEFINED;
				}
				else
					return e.write(fn(std::get<I>(args).get()...));
			}(std::index_sequence_for<A...>{});
		});
}
template <class R, class... A>
std::function<R(A...)> Function<std::function<R(A...)>>::read(Binding &e, JSValueConst value)
{
	if (JS_IsNull(value) || JS_IsUndefined(value))
		return {};
	if (!JS_IsFunction(e.ctx, value))
		throw TypeMismatch("Expected synchronous callback");
	e.allocate(sizeof(Script::JSValueOwner) + 64);
	auto fn = std::make_shared<Script::JSValueOwner>(e.ctx, JS_DupValue(e.ctx, value));
	return [&e, fn](A... args) -> R
	{
		Binding::CallbackScope callback(e);
		(callback.bindHost(args), ...);
		e.charge();
		auto convert = [&e]<class T>(T &&x)
		{
			if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>> ||
						  std::is_enum_v<std::remove_cvref_t<T>> ||
						  std::is_same_v<std::remove_cvref_t<T>, std::string>)
				return e.write(x);
			else
				return e.borrowed(x);
		};
		std::array<JSValue, sizeof...(A)> values;
		values.fill(JS_UNDEFINED);
		struct Values
		{
			Binding &e;
			decltype(values) &v;
			~Values()
			{
				for (auto x : v)
					JS_FreeValue(e.ctx, x);
			}
		} cleanup{e, values};
		size_t converted = 0;
		((values[converted++] = convert(args)), ...);
		Script::JSValueOwner result(
			e.ctx, JS_Call(e.ctx, fn->get(), JS_UNDEFINED, values.size(), values.data()));
		e.check(result.get());
		if (JS_IsPromise(result.get()))
			throw ScriptError("Toolkit callbacks must be synchronous");
		if constexpr (!std::is_void_v<R>)
			return e.read<R>(result.get());
	};
}
void registerToolkit(Binding &, JSValueConst);
} // namespace MapGeneration::JavaScript
