// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptRuntime.h"
#include "QuickJSOwnership.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <set>
#include <string_view>
#include <stdexcept>
namespace Script
{
namespace
{
struct Environment
{
	JSRuntime *runtime = nullptr;
	JSContext *ctx = nullptr;
	JSValue recordPrototype = JS_NULL;
	Host *host = nullptr;
	std::uint64_t fuel = FuelLimit;
	std::size_t dataBytes = 0, nativeBytes = 0;
	bool hostFailed = false;
	std::set<void *> ancestors;
	explicit Environment(Host *h) : host(h)
	{
		try
		{
			initialize();
		}
		catch (...)
		{
			cleanup();
			throw;
		}
	}
	Environment(const Environment &) = delete;
	Environment &operator=(const Environment &) = delete;
	~Environment() { cleanup(); }
	void cleanup() noexcept
	{
		if (ctx)
		{
			JS_FreeValue(ctx, recordPrototype);
			JS_FreeContext(ctx);
			ctx = nullptr;
		}
		if (runtime)
		{
			JS_FreeRuntime(runtime);
			runtime = nullptr;
		}
	}
	void initialize()
	{
		if (std::fegetround() != FE_TONEAREST)
			throw HostFailure("Unsupported floating point rounding mode");
		runtime = JS_NewRuntime();
		if (!runtime)
			throw HostFailure("Cannot create JavaScript runtime");
		JS_SetMemoryLimit(runtime, 32 * 1024 * 1024);
		JS_SetMaxStackSize(runtime, 256 * 1024);
		JS_SetInterruptHandler(
			runtime,
			[](JSRuntime *, void *opaque) -> int
			{
				auto &environment = *static_cast<Environment *>(opaque);
				if (!environment.fuel)
					return 1;
				--environment.fuel;
				return 0;
			},
			this);
		ctx = JS_NewContextRaw(runtime);
		if (!ctx)
			throw HostFailure("Cannot create JavaScript context");
		JS_SetContextOpaque(ctx, this);
		if (JS_AddIntrinsicBaseObjects(ctx) < 0 || JS_AddIntrinsicEval(ctx) < 0 ||
			JS_AddIntrinsicPromise(ctx) < 0 || JS_AddIntrinsicJSON(ctx) < 0 ||
			JS_AddIntrinsicMapSet(ctx) < 0)
			fail();
		JSValueOwner global(ctx, JS_GetGlobalObject(ctx));
		for (const char *name : {"BigInt", "eval", "Function", "Promise"})
			remove(global.get(), name);
		JSValueOwner array(ctx, get(global.get(), "Array"));
		remove(array.get(), "fromAsync");
		JSValueOwner object(ctx, get(global.get(), "Object"));
		recordPrototype = get(object.get(), "prototype");
		JSValueOwner math(ctx, get(global.get(), "Math"));
		set(math.get(), "random", JS_NewCFunction(ctx, random, "random", 0));
	}
	JSValue get(JSValueConst object, const char *name)
	{
		auto value = JS_GetPropertyStr(ctx, object, name);
		if (JS_IsException(value))
			fail();
		return value;
	}
	void set(JSValueConst object, const char *name, JSValue value)
	{
		// SetPropertyStr consumes value even on failure.
		if (JS_SetPropertyStr(ctx, object, name, value) < 0)
			fail();
	}
	void remove(JSValueConst object, const char *name)
	{
		JSAtomOwner atom(ctx, JS_NewAtom(ctx, name));
		if (atom.get() == JS_ATOM_NULL || JS_DeleteProperty(ctx, object, atom.get(), 0) < 0)
			fail();
	}
	void charge(std::size_t n)
	{
		if (n > fuel)
		{
			fuel = 0;
			throw std::runtime_error("JavaScript work budget exhausted");
		}
		fuel -= n;
	}
	void chargeNative(std::size_t n)
	{
		if (n > NativeDataLimit - nativeBytes)
			throw std::runtime_error("Native script data exceeds limit");
		nativeBytes += n;
	}
	[[noreturn]] void fail()
	{
		if (hostFailed || JS_Glob2HostFailure(runtime))
			throw HostFailure("JavaScript native resource limit exhausted");
		JSValueOwner exception(ctx, JS_GetException(ctx));
		JSStringOwner text(ctx, exception.get());
		if (JS_Glob2HostFailure(runtime))
			throw HostFailure("Cannot format JavaScript diagnostic");
		std::string message =
			text.get() ? std::string(text.get(), std::min(text.length, std::size_t(16384)))
					   : "JavaScript execution failed";
		if (!fuel)
			message = "JavaScript work budget exhausted";
		throw std::runtime_error(message);
	}
	static JSValue random(JSContext *ctx, JSValueConst, int, JSValueConst *)
	{
		auto &e = *static_cast<Environment *>(JS_GetContextOpaque(ctx));
		if (!e.host || !e.host->random)
			return JS_ThrowTypeError(ctx, "Randomness unavailable during source validation");
		try
		{
			e.charge(1);
			return JS_NewFloat64(ctx, e.host->random() / 4294967296.0);
		}
		catch (const std::bad_alloc &)
		{
			e.hostFailed = true;
			return JS_ThrowOutOfMemory(ctx);
		}
		catch (const HostFailure &ex)
		{
			e.hostFailed = true;
			return JS_ThrowInternalError(ctx, "%s", ex.what());
		}
		catch (const std::exception &ex)
		{
			return JS_ThrowInternalError(ctx, "%s", ex.what());
		}
	}
	JSValue toJS(const Value &v, unsigned depth = 0)
	{
		charge(1);
		if (depth > DepthLimit)
			throw std::runtime_error("Observation nesting limit exceeded");
		switch (v.kind)
		{
		case Value::Null:
			return JS_NULL;
		case Value::Boolean:
			return JS_NewBool(ctx, v.number != 0);
		case Value::Number:
			return JS_NewFloat64(ctx, v.number);
		case Value::String:
			charge(v.text.size());
			return JS_NewStringLen(ctx, v.text.data(), v.text.size());
		case Value::Array:
		{
			JSValueOwner array(ctx, JS_NewArray(ctx));
			if (JS_IsException(array.get()))
				fail();
			for (unsigned i = 0; i < v.items.size(); ++i)
				if (JS_SetPropertyUint32(ctx, array.get(), i, toJS(v.items[i], depth + 1)) < 0)
					fail();
			return array.release();
		}
		case Value::Object:
		{
			JSValueOwner object(ctx, JS_NewObjectProto(ctx, JS_NULL));
			if (JS_IsException(object.get()))
				fail();
			for (const auto &field : v.fields)
			{
				JSValueOwner value(ctx, toJS(field.second, depth + 1));
				JSAtomOwner atom(ctx, JS_NewAtomLen(ctx, field.first.data(), field.first.size()));
				if (atom.get() == JS_ATOM_NULL)
					fail();
				if (JS_DefinePropertyValue(ctx, object.get(), atom.get(), value.release(),
										   JS_PROP_C_W_E) < 0)
					fail();
			}
			return object.release();
		}
		}
		return JS_NULL;
	}
	std::string string(JSValueConst value)
	{
		JSStringOwner text(ctx, value);
		if (!text.get())
			fail();
		if (text.length > (NativeDataLimit - nativeBytes) / 2)
			throw std::runtime_error("Native script data exceeds limit");
		chargeNative(2 * text.length);
		charge(text.length);
		return std::string(text.get(), text.length);
	}
	std::string key(JSAtom atom)
	{
		JSValueOwner value(ctx, JS_AtomToValue(ctx, atom));
		if (JS_IsException(value.get()))
			fail();
		if (JS_IsSymbol(value.get()))
			throw std::runtime_error("Symbol keys are not script data");
		return string(value.get());
	}
	void property(JSPropertyOwner &property, JSValueConst object, JSAtom atom)
	{
		if (JS_GetOwnProperty(ctx, &property.descriptor, object, atom) < 0)
			fail();
		if (property.descriptor.flags & JS_PROP_GETSET)
			throw std::runtime_error("Accessors are not script data");
	}
	static uint32_t arrayIndex(const std::string &key, uint32_t length)
	{
		if (key.empty() || key.size() > 10 || (key.size() > 1 && key.front() == '0'))
			throw std::runtime_error("Invalid array key");
		std::uint64_t index = 0;
		for (char digit : key)
		{
			if (digit < '0' || digit > '9')
				throw std::runtime_error("Invalid array key");
			index = index * 10 + unsigned(digit - '0');
		}
		if (index >= length)
			throw std::runtime_error("Invalid array key");
		return uint32_t(index);
	}
	Value fromJS(JSValueConst value, unsigned depth = 0)
	{
		charge(1);
		chargeNative(NativeValueCost);
		if (depth > DepthLimit)
			throw std::runtime_error("Script data nesting limit exceeded");
		if (JS_IsNull(value))
			return {};
		if (JS_IsBool(value))
			return Value(bool(JS_ToBool(ctx, value)));
		if (JS_IsNumber(value))
		{
			double number;
			if (JS_ToFloat64(ctx, &number, value) < 0)
				fail();
			if (!std::isfinite(number))
				throw std::runtime_error("Script data requires finite numbers");
			return Value(number);
		}
		if (JS_IsString(value))
		{
			auto text = string(value);
			dataBytes += text.size();
			if (dataBytes > StateLimit)
				throw std::runtime_error("Script data exceeds limit");
			return Value(text);
		}
		const bool array = JS_IsArray(value);
		if (!JS_IsObject(value) || JS_IsProxy(value) || (JS_GetClassID(value) != 1 && !array))
			throw std::runtime_error("Script data requires plain records and arrays");
		if (!array)
		{
			JSValueOwner prototype(ctx, JS_GetPrototype(ctx, value));
			if (JS_IsException(prototype.get()))
				fail();
			const bool plain =
				JS_IsNull(prototype.get()) ||
				(JS_IsObject(prototype.get()) &&
				 JS_VALUE_GET_PTR(prototype.get()) == JS_VALUE_GET_PTR(recordPrototype));
			if (!plain)
				throw std::runtime_error("Class instances are not script data");
		}
		void *identity = JS_VALUE_GET_PTR(value);
		if (!ancestors.insert(identity).second)
			throw std::runtime_error("Cyclic script data");
		struct AncestorGuard
		{
			std::set<void *> &ancestors;
			void *identity;
			~AncestorGuard() { ancestors.erase(identity); }
		} guard{ancestors, identity};
		JSEnumerationOwner enumeration(ctx);
		if (JS_GetOwnPropertyNames(ctx, &enumeration.keys, &enumeration.count, value,
								   JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
			fail();
		charge(enumeration.count);
		Value output = array ? Value::array() : Value::object();
		uint32_t length = 0;
		if (array)
		{
			JSValueOwner size(ctx, JS_GetPropertyStr(ctx, value, "length"));
			if (JS_IsException(size.get()) || JS_ToUint32(ctx, &length, size.get()) < 0)
				fail();
			if (length > StateLimit)
				throw std::runtime_error("Array length exceeds limit");
			if (enumeration.count != length + 1)
				throw std::runtime_error("Sparse or named arrays are not script data");
			charge(length);
			chargeNative(std::size_t(length) * NativeValueCost);
			output.items.resize(length);
		}
		uint32_t entries = 0;
		for (uint32_t i = 0; i < enumeration.count; ++i)
		{
			JSPropertyOwner descriptor(ctx);
			property(descriptor, value, enumeration.keys[i].atom);
			auto name = key(enumeration.keys[i].atom);
			if (array && name == "length")
				continue;
			chargeNative(NativeFieldCost);
			dataBytes += name.size() + 16;
			if (dataBytes > StateLimit)
				throw std::runtime_error("Script data exceeds limit");
			if (array)
			{
				const auto index = arrayIndex(name, length);
				output.items[index] = fromJS(descriptor.descriptor.value, depth + 1);
				++entries;
			}
			else
				output.fields.emplace_back(std::move(name),
										   fromJS(descriptor.descriptor.value, depth + 1));
		}
		if (array && entries != length)
			throw std::runtime_error("Sparse arrays are not script data");
		return output;
	}
	static JSValue query(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int magic)
	{
		static const char *names[] = {"teams",    "units",     "buildings",    "unit",
									  "building", "tile",      "region",       "objectives",
									  "hints",    "interface", "buildingTypes"};
		auto &e = *static_cast<Environment *>(JS_GetContextOpaque(ctx));
		try
		{
			e.charge(1);
			std::vector<Value> args;
			for (int i = 0; i < argc; ++i)
				args.push_back(e.fromJS(argv[i]));
			if (magic == 6)
			{
				if (args.size() != 4)
					throw std::runtime_error("region requires x, y, width, height");
				auto dimensions = Value::object().set("w", args[2]).set("h", args[3]);
				auto w = dimensions.integer("w", 0, 256), h = dimensions.integer("h", 0, 256);
				e.charge(std::size_t(w) * h * 128);
			}
			if (magic == 1 || magic == 2)
				e.charge(32 * 1024);
			return e.toJS(e.host->query(names[magic], args,
										[&e](std::size_t work, std::size_t bytes)
										{
											e.charge(work);
											e.chargeNative(bytes);
										}));
		}
		catch (const std::bad_alloc &)
		{
			e.hostFailed = true;
			return JS_ThrowOutOfMemory(ctx);
		}
		catch (const HostFailure &ex)
		{
			e.hostFailed = true;
			return JS_ThrowInternalError(ctx, "%s", ex.what());
		}
		catch (const std::exception &ex)
		{
			return JS_ThrowTypeError(ctx, "%s", ex.what());
		}
	}
	JSValue context()
	{
		JSValueOwner context(ctx, JS_NewObjectProto(ctx, JS_NULL));
		JSValueOwner game(ctx, JS_NewObjectProto(ctx, JS_NULL));
		JSValueOwner map(ctx, JS_NewObjectProto(ctx, JS_NULL));
		if (JS_IsException(context.get()) || JS_IsException(game.get()) ||
			JS_IsException(map.get()))
			fail();
		set(context.get(), "tick", JS_NewUint32(ctx, host->tick));
		set(context.get(), "myTeam", JS_NewInt32(ctx, host->team));
		set(context.get(), "random", JS_NewCFunction(ctx, random, "random", 0));
		static const char *names[] = {"teams",    "units",     "buildings",    "unit",
									  "building", "tile",      "region",       "objectives",
									  "hints",    "interface", "buildingTypes"};
		for (int i = 0; i < 11; ++i)
			set(i == 5 || i == 6 ? map.get() : game.get(), names[i],
				JS_NewCFunctionMagic(ctx, query, names[i], 0, JS_CFUNC_generic_magic, i));
		set(map.get(), "width", JS_NewUint32(ctx, host->width));
		set(map.get(), "height", JS_NewUint32(ctx, host->height));
		set(game.get(), "map", map.release());
		set(context.get(), "game", game.release());
		return context.release();
	}
	JSValue compile(const std::string &source)
	{
		if (source.size() > SourceLimit || source.find('\0') != std::string::npos)
			throw std::runtime_error("Invalid or oversized JavaScript source");
		charge(source.size());
		JSValueOwner code(ctx, JS_Eval(ctx, source.data(), source.size(), "<glob2-script>",
									   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
		if (JS_IsException(code.get()))
			fail();
		if (JS_ResolveModule(ctx, code.get()) < 0)
			fail();
		return code.release();
	}
};
class QuickRuntime : public Runtime
{
	static void evaluate(Environment &environment, JSValueOwner &code)
	{
		auto *context = environment.ctx;
		JSValueOwner evaluation(context, JS_EvalFunction(context, code.release()));
		if (JS_IsException(evaluation.get()))
			environment.fail();
		if (JS_IsPromise(evaluation.get()) &&
			JS_PromiseState(context, evaluation.get()) != JS_PROMISE_FULFILLED)
		{
			JS_Throw(context, JS_PromiseResult(context, evaluation.get()));
			environment.fail();
		}
	}
	static JSValueOwner call(Environment &environment, JSValueConst module, const char *name,
							 JSValueConst context, JSValueConst state, bool optional)
	{
		auto *ctx = environment.ctx;
		JSValueOwner function(ctx, JS_GetPropertyStr(ctx, module, name));
		if (JS_IsException(function.get()))
			environment.fail();
		if (optional && JS_IsUndefined(function.get()))
			return JSValueOwner(ctx, JS_UNDEFINED);
		if (!JS_IsFunction(ctx, function.get()))
			throw std::runtime_error(std::string("Script must export ") + name + "(ctx, state)");
		JSValue args[] = {context, state};
		JSValueOwner result(ctx, JS_Call(ctx, function.get(), JS_UNDEFINED, 2, args));
		if (JS_IsException(result.get()))
			environment.fail();
		return result;
	}

  public:
	void validate(const std::string &source) override
	{
		try
		{
			Environment environment(nullptr);
			JSValueOwner code(environment.ctx, environment.compile(source));
		}
		catch (const std::bad_alloc &)
		{
			throw HostFailure("Native allocation failed while validating JavaScript");
		}
	}

	Result invoke(const std::string &source, const Value &state, bool initialize,
				  Host &host) override
	{
		Environment environment(&host);
		auto *ctx = environment.ctx;
		JSValueOwner code(ctx, environment.compile(source));
		auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(code.get()));
		evaluate(environment, code);
		JSValueOwner exports(ctx, JS_GetModuleNamespace(ctx, module));
		if (JS_IsException(exports.get()))
			environment.fail();
		JSValueOwner context(ctx, environment.context());
		JSValueOwner scriptState(ctx, environment.toJS(state));
		if (JS_IsException(scriptState.get()))
			environment.fail();
		if (initialize)
		{
			auto result =
				call(environment, exports.get(), "init", context.get(), scriptState.get(), true);
			if (!JS_IsUndefined(result.get()) && !JS_IsNull(result.get()))
				throw std::runtime_error("init must not return effects");
		}
		auto effects =
			call(environment, exports.get(), "step", context.get(), scriptState.get(), false);
		if (environment.hostFailed || JS_Glob2HostFailure(environment.runtime))
			throw HostFailure("JavaScript native resource limit exhausted");
		if (!environment.fuel)
			throw std::runtime_error("JavaScript work budget exhausted");
		Result result{environment.fromJS(scriptState.get()),
					  JS_IsUndefined(effects.get()) ? Value() : environment.fromJS(effects.get())};
		result.state.encode();
		result.effects.encode();
		return result;
	}
};
} // namespace
std::unique_ptr<Runtime> makeRuntime()
{
	return std::make_unique<QuickRuntime>();
}
} // namespace Script
