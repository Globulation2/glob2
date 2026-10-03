// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptRuntime.h"
#include "QuickJSOwnership.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <map>
#include <bit>
#include <charconv>
#include <set>
#include <string_view>
#include <stdexcept>
namespace Script
{
namespace
{
struct Environment
{
#include "ScriptRuntimeV2.inc"
	JSRuntime *runtime = nullptr;
	JSContext *ctx = nullptr;
	JSValue recordPrototype = JS_NULL;
	Host *host = nullptr;
	std::uint64_t fuel = FuelLimit;
	std::size_t dataBytes = 0, nativeBytes = 0;
	bool hostFailed = false;
	std::set<void *> ancestors, frozen;
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
			clearManaged();
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
	void freeze(JSValueConst value, unsigned depth = 0, JSModuleDef *module = nullptr)
	{
		if (!JS_IsObject(value) || !frozen.insert(JS_VALUE_GET_PTR(value)).second)
			return;
		if (depth > 64)
			throw std::runtime_error("Source function/prototype nesting limit exceeded");
		if (module)
		{
			if (JS_IsFunction(ctx, value))
			{
				int persistent = JS_Glob2ModuleFunction(ctx, module, value);
				if (persistent < 0)
					fail();
				if (!persistent)
					throw std::runtime_error(
						"Source functions cannot hide private closure or class state");
			}
			else if (JS_IsProxy(value) || (JS_GetClassID(value) != 1 && !JS_IsArray(value)))
				throw std::runtime_error("Source function properties cannot retain opaque objects");
			else
			{
				JSValueOwner prototype(ctx, JS_GetPrototype(ctx, value));
				if (JS_IsException(prototype.get()))
					fail();
				if (!JS_IsNull(prototype.get()))
				{
					JSValueOwner global(ctx, JS_GetGlobalObject(ctx));
					JSValueOwner array(ctx, get(global.get(), "Array"));
					JSValueOwner arrayPrototype(ctx, get(array.get(), "prototype"));
					auto expected = JS_IsArray(value) ? arrayPrototype.get() : recordPrototype;
					if (JS_VALUE_GET_PTR(prototype.get()) != JS_VALUE_GET_PTR(expected))
						throw std::runtime_error(
							"Source function properties cannot retain class instances");
				}
			}
		}
		charge(1);
		JSEnumerationOwner enumeration(ctx);
		if (JS_GetOwnPropertyNames(ctx, &enumeration.keys, &enumeration.count, value,
								   JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
			fail();
		charge(enumeration.count);
		for (unsigned i = 0; i < enumeration.count; ++i)
		{
			JSPropertyOwner property(ctx);
			if (JS_GetOwnProperty(ctx, &property.descriptor, value, enumeration.keys[i].atom) < 0)
				fail();
			freeze(property.descriptor.value, depth + 1, module);
			freeze(property.descriptor.getter, depth + 1, module);
			freeze(property.descriptor.setter, depth + 1, module);
		}
		JSValueOwner global(ctx, JS_GetGlobalObject(ctx));
		JSValueOwner object(ctx, get(global.get(), "Object"));
		JSValueOwner function(ctx, get(object.get(), "freeze"));
		JSValue args[] = {value};
		JSValueOwner result(ctx, JS_Call(ctx, function.get(), JS_UNDEFINED, 1, args));
		if (JS_IsException(result.get()))
			fail();
	}
	void begin(Host *next)
	{
		if (std::fegetround() != FE_TONEAREST)
			throw HostFailure("Unsupported floating point rounding mode");
		// AI work can move between workers. Calls on one runtime are serial,
		// but QuickJS's physical-stack guard must use the calling thread.
		JS_UpdateStackTop(runtime);
		clearManaged();
		host = next;
		fuel = FuelLimit;
		dataBytes = nativeBytes = 0;
		ancestors.clear();
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
		if (JS_IsObject(exception.get()))
		{
			JSValueOwner stack(ctx, JS_GetPropertyStr(ctx, exception.get(), "stack"));
			if (JS_IsString(stack.get()))
			{
				JSStringOwner detail(ctx, stack.get());
				if (detail.get())
					message += "\n" + std::string(detail.get(),
												  std::min(detail.length, std::size_t(8192)));
			}
		}
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
		if (JS_IsObject(value) && ephemeral.contains(JS_VALUE_GET_PTR(value)))
			for (const auto &[key, entity] : managed)
				if (JS_VALUE_GET_PTR(entity.object) == JS_VALUE_GET_PTR(value))
					return entity.observed.get("ref");
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
									  "hints",    "interface", "buildingTypes", "wakeAgent"};
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
			auto result = e.hostQuery(names[magic], args);
			if (e.host->profile == 2 && magic == 4)
				return e.managedBuilding(result);
			if (e.host->profile == 2 && magic == 2)
			{
				JSValueOwner list(ctx, JS_NewArray(ctx));
				for (unsigned i = 0; i < result.items.size(); ++i)
					if (JS_SetPropertyUint32(ctx, list.get(), i,
											 e.managedBuilding(result.items[i])) < 0)
						e.fail();
				return list.release();
			}
			auto value = e.toJS(result);
			if (e.host->profile == 2 && JS_IsObject(value))
				e.readonly(value);
			return value;
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
									  "hints",    "interface", "buildingTypes", "wakeAgent"};
		for (int i = 0; i < 11; ++i)
			if (!host->commander || i < 7 || i == 10) set(i == 5 || i == 6 ? map.get() : game.get(), names[i],
				JS_NewCFunctionMagic(ctx, query, names[i], 0, JS_CFUNC_generic_magic, i));
		if (host->commander)
			set(context.get(), "wakeAgent", JS_NewCFunctionMagic(ctx, query, "wakeAgent", 1, JS_CFUNC_generic_magic, 11));
		set(map.get(), "width", JS_NewUint32(ctx, host->width));
		set(map.get(), "height", JS_NewUint32(ctx, host->height));
		set(game.get(), "map", map.release());
		set(context.get(), "game", game.release());
		if (host->profile == 2)
			services(context.get());
		return context.release();
	}
	JSValue compile(const std::string &source)
	{
		if (source.size() > SourceLimit || source.find('\0') != std::string::npos)
			throw std::runtime_error("Invalid or oversized JavaScript source");
		charge(source.size());
		// A plain script needs only a step(ctx) or main(ctx) declaration. Compile
		// as a module so top-level let/const/var bindings have script ownership.
		const std::string moduleSource = source + R"(
export function __glob2_step(ctx, legacy) {
  if (typeof step === 'function') return step.length > 1 ? step(ctx, legacy) : step(ctx);
  if (typeof main === 'function') return main.length > 1 ? main(ctx, legacy) : main(ctx);
  throw new TypeError('Script must declare step(ctx) or main(ctx)');
}
)";
		JSValueOwner code(ctx,
						  JS_Eval(ctx, moduleSource.data(), moduleSource.size(), "<glob2-script>",
								  JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
		if (JS_IsException(code.get()))
			fail();
		if (JS_ResolveModule(ctx, code.get()) < 0)
			fail();
		return code.release();
	}
};
// Data graphs are portable engine values. Aliases, cycles, undefined, signed
// zero and property attributes survive restoring into a newly evaluated module.
// Functions are stable source definitions, addressed by binding name.
class GlobalsCodec
{
	static constexpr std::uint64_t CanonicalNaNBits = UINT64_C(0x7ff8000000000000);
	Environment &e;
	JSValueConst definitions;
	std::map<void *, unsigned> objects;
	Value nodes = Value::array();
	std::map<void *, std::string> functions;

	Value token(JSValueConst value, unsigned depth)
	{
		e.charge(1);
		if (depth > DepthLimit)
			throw std::runtime_error("Global data nesting limit exceeded");
		if (JS_IsUndefined(value))
		{
			Value tagged = Value::array();
			tagged.items.emplace_back("undefined");
			return tagged;
		}
		if (!JS_IsObject(value))
		{
			if (JS_IsNumber(value))
			{
				double number;
				if (JS_ToFloat64(e.ctx, &number, value) < 0)
					e.fail();
				if (!std::isfinite(number))
				{
					// NaN signs/payloads are not observable in this profile, but hardware
					// arithmetic and QuickJS's 32-bit NaN boxing produce different bits.
					// Snapshots participate in saves and simulation checksums, so use one
					// quiet NaN representation while retaining the sign of infinities.
					const auto bits =
						std::isnan(number) ? CanonicalNaNBits : std::bit_cast<std::uint64_t>(number);
					std::string text(16, '0');
					for (unsigned i = 0; i < 16; ++i)
						text[i] = "0123456789abcdef"[(bits >> (4 * (15 - i))) & 15];
					Value tagged = Value::array();
					tagged.items = {Value("number"), Value(text)};
					return tagged;
				}
			}
			return e.fromJS(value);
		}
		void *identity = JS_VALUE_GET_PTR(value);
		if (JS_IsFunction(e.ctx, value))
		{
			auto function = functions.find(identity);
			if (function == functions.end())
				throw std::runtime_error("Global functions must be unchanged source definitions; "
										 "captured local closures are not saveable");
			Value reference = Value::array();
			reference.items = {Value("function"), Value(function->second)};
			return reference;
		}
		if (e.ephemeral.contains(identity))
			throw std::runtime_error("Managed entities cannot be saved; store building.ref and "
									 "reacquire it in step(ctx)");
		if (e.frozen.contains(identity))
			throw std::runtime_error("Persistent globals cannot retain built-ins or function-owned "
									 "objects; use independent top-level data");
		const bool array = JS_IsArray(value);
		if (JS_IsProxy(value) || (JS_GetClassID(value) != 1 && !array))
			throw std::runtime_error("Persistent globals require plain objects and arrays");
		JSValueOwner prototype(e.ctx, JS_GetPrototype(e.ctx, value));
		if (JS_IsException(prototype.get()))
			e.fail();
		if (array && !JS_IsNull(prototype.get()))
		{
			JSValueOwner global(e.ctx, JS_GetGlobalObject(e.ctx));
			JSValueOwner constructor(e.ctx, e.get(global.get(), "Array"));
			JSValueOwner ordinary(e.ctx, e.get(constructor.get(), "prototype"));
			if (JS_VALUE_GET_PTR(prototype.get()) != JS_VALUE_GET_PTR(ordinary.get()))
				throw std::runtime_error("Persistent arrays cannot have custom prototypes");
		}
		if (!array && !JS_IsNull(prototype.get()) &&
			JS_VALUE_GET_PTR(prototype.get()) != JS_VALUE_GET_PTR(e.recordPrototype))
			throw std::runtime_error("Persistent global objects cannot have custom prototypes");
		Value reference = Value::array();
		auto existing = objects.find(identity);
		if (existing != objects.end())
		{
			reference.items = {Value("ref"), Value(existing->second)};
			return reference;
		}
		e.chargeNative(NativeValueCost + 4 * NativeFieldCost);
		unsigned id = nodes.items.size();
		objects.emplace(identity, id);
		nodes.items.push_back(Value());
		reference.items = {Value("ref"), Value(id)};
		Value node =
			Value::object().set("array", array).set("nullPrototype", JS_IsNull(prototype.get()));
		Value properties = Value::array();
		JSEnumerationOwner enumeration(e.ctx);
		if (JS_GetOwnPropertyNames(e.ctx, &enumeration.keys, &enumeration.count, value,
								   JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) < 0)
			e.fail();
		e.charge(enumeration.count);
		for (unsigned i = 0; i < enumeration.count; ++i)
		{
			JSPropertyOwner property(e.ctx);
			e.property(property, value, enumeration.keys[i].atom);
			e.chargeNative(NativeFieldCost + 3 * NativeValueCost);
			Value entry = Value::array();
			entry.items = {Value(e.key(enumeration.keys[i].atom)),
						   token(property.descriptor.value, depth + 1),
						   Value(property.descriptor.flags & JS_PROP_C_W_E)};
			properties.items.push_back(std::move(entry));
		}
		node.set("properties", std::move(properties));
		// Extensibility is part of observable state, too.
		int extensible = JS_IsExtensible(e.ctx, value);
		if (extensible < 0)
			e.fail();
		node.set("extensible", extensible != 0);
		nodes.items[id] = std::move(node);
		return reference;
	}

  public:
	GlobalsCodec(Environment &environment, JSValueConst initial)
		: e(environment), definitions(initial)
	{
		JSEnumerationOwner enumeration(e.ctx);
		if (JS_GetOwnPropertyNames(e.ctx, &enumeration.keys, &enumeration.count, initial,
								   JS_GPN_STRING_MASK) < 0)
			e.fail();
		for (unsigned i = 0; i < enumeration.count; ++i)
		{
			JSValueOwner value(e.ctx, JS_GetProperty(e.ctx, initial, enumeration.keys[i].atom));
			if (JS_IsException(value.get()))
				e.fail();
			if (JS_IsFunction(e.ctx, value.get()))
				functions.emplace(JS_VALUE_GET_PTR(value.get()), e.key(enumeration.keys[i].atom));
		}
	}
	Value capture(JSValueConst root)
	{
		auto reference = token(root, 0);
		return Value::object().set("root", std::move(reference)).set("nodes", std::move(nodes));
	}
	JSValue restore(const Value &snapshot)
	{
		const auto &saved = snapshot.get("nodes");
		if (saved.kind != Value::Array || saved.items.size() > StateLimit)
			throw std::runtime_error("Invalid global snapshot nodes");
		std::vector<JSValueOwner> values;
		values.reserve(saved.items.size());
		for (const auto &node : saved.items)
		{
			e.charge(1);
			if (node.kind != Value::Object || node.get("array").kind != Value::Boolean ||
				node.get("nullPrototype").kind != Value::Boolean ||
				node.get("extensible").kind != Value::Boolean)
				throw std::runtime_error("Invalid global snapshot object");
			values.emplace_back(e.ctx,
								node.get("array").number
									? JS_NewArray(e.ctx)
									: JS_NewObjectProto(e.ctx, node.get("nullPrototype").number
																   ? JS_NULL
																   : e.recordPrototype));
			if (JS_IsException(values.back().get()))
				e.fail();
			if (node.get("array").number && node.get("nullPrototype").number &&
				JS_SetPrototype(e.ctx, values.back().get(), JS_NULL) < 0)
				e.fail();
		}
		auto decode = [&](const Value &value) -> JSValue
		{
			e.charge(1);
			if (value.kind != Value::Array)
				return e.toJS(value);
			if (value.items.size() == 1 && value.items[0].text == "undefined")
				return JS_UNDEFINED;
			if (value.items.size() != 2 || value.items[0].kind != Value::String)
				throw std::runtime_error("Invalid global snapshot reference");
			if (value.items[0].text == "ref")
			{
				const auto &id = value.items[1];
				if (id.kind != Value::Number || id.number < 0 || id.number >= values.size() ||
					static_cast<unsigned>(id.number) != id.number)
					throw std::runtime_error("Invalid global snapshot object reference");
				return JS_DupValue(e.ctx, values[unsigned(id.number)].get());
			}
			if (value.items[0].text == "function" && value.items[1].kind == Value::String)
			{
				const auto &name = value.items[1].text;
				JSAtomOwner atom(e.ctx, JS_NewAtomLen(e.ctx, name.data(), name.size()));
				if (atom.get() == JS_ATOM_NULL)
					e.fail();
				auto function = JS_GetProperty(e.ctx, definitions, atom.get());
				if (JS_IsException(function))
					e.fail();
				if (!JS_IsFunction(e.ctx, function))
				{
					JS_FreeValue(e.ctx, function);
					throw std::runtime_error("Invalid global snapshot function reference");
				}
				return function;
			}
			if (value.items[0].text == "number" && value.items[1].kind == Value::String)
			{
				const auto &text = value.items[1].text;
				std::uint64_t bits;
				auto parsed = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
				if (text.size() != 16 || parsed.ec != std::errc() ||
					parsed.ptr != text.data() + text.size())
					throw std::runtime_error("Invalid global snapshot number bits");
				auto number = std::bit_cast<double>(bits);
				if (std::isfinite(number))
					throw std::runtime_error("Invalid global snapshot non-finite number");
				// Older unpublished snapshots may contain architecture-specific NaNs.
				if (std::isnan(number))
					number = std::bit_cast<double>(CanonicalNaNBits);
				return JS_NewFloat64(e.ctx, number);
			}
			throw std::runtime_error("Invalid global snapshot tag");
		};
		for (unsigned i = 0; i < saved.items.size(); ++i)
		{
			const auto &properties = saved.items[i].get("properties");
			if (properties.kind != Value::Array)
				throw std::runtime_error("Invalid global snapshot properties");
			std::set<std::string> names;
			for (const auto &entry : properties.items)
			{
				e.chargeNative(NativeFieldCost);
				if (entry.kind != Value::Array || entry.items.size() != 3 ||
					entry.items[0].kind != Value::String || entry.items[2].kind != Value::Number ||
					entry.items[2].number < 0 || entry.items[2].number > JS_PROP_C_W_E ||
					static_cast<int>(entry.items[2].number) != entry.items[2].number ||
					!names.insert(entry.items[0].text).second)
					throw std::runtime_error("Invalid global snapshot property");
				JSAtomOwner atom(e.ctx, JS_NewAtomLen(e.ctx, entry.items[0].text.data(),
													  entry.items[0].text.size()));
				if (atom.get() == JS_ATOM_NULL ||
					JS_DefinePropertyValue(e.ctx, values[i].get(), atom.get(),
										   decode(entry.items[1]), int(entry.items[2].number)) < 0)
					e.fail();
			}
			if (!saved.items[i].get("extensible").number &&
				JS_PreventExtensions(e.ctx, values[i].get()) < 0)
				e.fail();
		}
		return decode(snapshot.get("root"));
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

	struct Live
	{
		Environment environment{nullptr};
		JSModuleDef *module = nullptr;
		std::unique_ptr<JSValueOwner> exports, definitions;
		std::string source, candidate;
		explicit Live(const std::string &text) : source(text)
		{
			auto &e = environment;
			JSValueOwner global(e.ctx, JS_GetGlobalObject(e.ctx));
			e.freeze(global.get());
			JSValueOwner code(e.ctx, e.compile(text));
			module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(code.get()));
			evaluate(e, code);
			exports = std::make_unique<JSValueOwner>(e.ctx, JS_GetModuleNamespace(e.ctx, module));
			JSValueOwner initial(e.ctx, JS_Glob2ModuleBindings(e.ctx, module));
			definitions = std::make_unique<JSValueOwner>(e.ctx, JS_NewObjectProto(e.ctx, JS_NULL));
			if (JS_IsException(exports->get()) || JS_IsException(initial.get()) ||
				JS_IsException(definitions->get()))
				e.fail();
			JSEnumerationOwner enumeration(e.ctx);
			if (JS_GetOwnPropertyNames(e.ctx, &enumeration.keys, &enumeration.count, initial.get(),
									   JS_GPN_STRING_MASK) < 0)
				e.fail();
			for (unsigned i = 0; i < enumeration.count; ++i)
			{
				JSValueOwner value(e.ctx,
								   JS_GetProperty(e.ctx, initial.get(), enumeration.keys[i].atom));
				if (JS_IsException(value.get()))
					e.fail();
				if (JS_IsFunction(e.ctx, value.get()))
				{
					int persistent = JS_Glob2ModuleFunction(e.ctx, module, value.get());
					if (persistent < 0)
						e.fail();
					if (!persistent)
						throw std::runtime_error("Global functions cannot capture private local "
												 "state; use top-level variables");
					e.freeze(value.get(), 0, module);
					if (JS_DefinePropertyValue(e.ctx, definitions->get(), enumeration.keys[i].atom,
											   value.release(), JS_PROP_C_W_E) < 0)
						e.fail();
				}
			}
		}
	};
	std::unique_ptr<Live> live;

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
	void discard() noexcept override { live.reset(); }
	Metadata inspect(const std::string &source)
	{
		Live temporary(source);
		auto &e = temporary.environment;
		JSValueOwner bindings(e.ctx, JS_Glob2ModuleBindings(e.ctx, temporary.module));
		bool callback = false, localCallback = false;
		for (const char *name : {"step", "main"})
		{
			JSValueOwner exported(e.ctx, e.get(temporary.exports->get(), name));
			JSValueOwner local(e.ctx, e.get(bindings.get(), name));
			callback |= JS_IsFunction(e.ctx, exported.get()) || JS_IsFunction(e.ctx, local.get());
			localCallback |= JS_IsFunction(e.ctx, local.get());
		}
		if (!callback)
			throw std::runtime_error("AI must define step(ctx) or main(ctx)");
		Metadata metadata;
		JSValueOwner exported(e.ctx, e.get(temporary.exports->get(), "metadata"));
		JSValueOwner local(e.ctx, e.get(bindings.get(), "metadata"));
		JSValue function = JS_IsUndefined(exported.get()) ? local.get() : exported.get();
		if (!JS_IsUndefined(function))
		{
			if (!JS_IsFunction(e.ctx, function))
				throw std::runtime_error("metadata must be a function");
			JSValueOwner value(e.ctx, JS_Call(e.ctx, function, JS_UNDEFINED, 0, nullptr));
			if (JS_IsException(value.get()))
				e.fail();
			const auto data = e.fromJS(value.get());
			metadata.apiVersion = data.integer("apiVersion", 1, 2);
			metadata.name = data.string("name");
			if (metadata.name.empty() || metadata.name.size() > 128)
				throw std::runtime_error("AI name must contain 1..128 UTF-8 bytes");
			for (auto field : {std::pair{"description", &metadata.description},
							   {"version", &metadata.version},
							   {"author", &metadata.author}})
				if (data.get(field.first).kind != Value::Null)
				{
					*field.second = data.string(field.first);
					if (field.second->size() > 4096)
						throw std::runtime_error("AI metadata field exceeds limit");
				}
		}
		if (metadata.apiVersion == 1 && !localCallback)
			throw std::runtime_error("Renamed callback exports require API profile 2 metadata");
		// metadata is deliberately separate from the gameplay runtime. Validate
		// fresh source globals as well, rather than metadata's mutated bindings.
		Live startup(source);
		GlobalsCodec codec(startup.environment, startup.definitions->get());
		JSValueOwner initial(startup.environment.ctx,
							 JS_Glob2ModuleBindings(startup.environment.ctx, startup.module));
		auto saved = codec.capture(initial.get());
		saved.encode();
		JSValueOwner restored(startup.environment.ctx, codec.restore(saved));
		return metadata;
	}
	Value inspectGlobals() override
	{
		if (!live)
			return Value::object();
		auto &e = live->environment;
		e.begin(nullptr);
		JSValueOwner bindings(e.ctx, JS_Glob2ModuleBindings(e.ctx, live->module));
		JSValueOwner data(e.ctx, JS_NewObjectProto(e.ctx, JS_NULL));
		if (JS_IsException(bindings.get()) || JS_IsException(data.get()))
			e.fail();
		JSEnumerationOwner enumeration(e.ctx);
		if (JS_GetOwnPropertyNames(e.ctx, &enumeration.keys, &enumeration.count, bindings.get(),
								   JS_GPN_STRING_MASK) < 0)
			e.fail();
		for (unsigned i = 0; i < enumeration.count; ++i)
		{
			JSValueOwner value(e.ctx,
							   JS_GetProperty(e.ctx, bindings.get(), enumeration.keys[i].atom));
			if (JS_IsException(value.get()))
				e.fail();
			if (!JS_IsFunction(e.ctx, value.get()) && !JS_IsUndefined(value.get()))
				if (JS_DefinePropertyValue(e.ctx, data.get(), enumeration.keys[i].atom,
										   value.release(), JS_PROP_C_W_E) < 0)
					e.fail();
		}
		return e.fromJS(data.get());
	}
	Result invoke(const std::string &source, const Value &state, bool initialize,
				  Host &host) override
	{
		try
		{
			const auto encoded = state.encode();
			const bool rebuild = !live || live->source != source || live->candidate != encoded;
			if (rebuild)
				live = std::make_unique<Live>(source);
			auto &e = live->environment;
			e.begin(nullptr);
			auto *ctx = e.ctx;
			GlobalsCodec codec(e, live->definitions->get());
			if (rebuild && state.get("__glob2_globals").kind != Value::Null)
			{
				JSValueOwner restored(ctx, codec.restore(state.get("__glob2_globals")));
				if (!JS_IsObject(restored.get()) || JS_GetClassID(restored.get()) != 1)
					throw std::runtime_error("Invalid global snapshot bindings");
				JSValueOwner current(ctx, JS_Glob2ModuleBindings(ctx, live->module));
				if (JS_IsException(current.get()))
					e.fail();
				JSEnumerationOwner expected(ctx), actual(ctx);
				if (JS_GetOwnPropertyNames(ctx, &expected.keys, &expected.count, current.get(),
										   JS_GPN_STRING_MASK) < 0 ||
					JS_GetOwnPropertyNames(ctx, &actual.keys, &actual.count, restored.get(),
										   JS_GPN_STRING_MASK) < 0)
					e.fail();
				e.charge(expected.count);
				if (expected.count != actual.count)
					throw std::runtime_error("Global snapshot bindings differ from script source");
				for (unsigned i = 0; i < expected.count; ++i)
				{
					JSPropertyOwner property(ctx);
					int present = JS_GetOwnProperty(ctx, &property.descriptor, restored.get(),
													expected.keys[i].atom);
					if (present < 0)
						e.fail();
					if (!present)
						throw std::runtime_error("Global snapshot binding is missing");
				}
				if (JS_Glob2RestoreModuleBindings(ctx, live->module, restored.get()) < 0)
					e.fail();
			}
			e.begin(&host);
			// Retain the old draft argument for embedded draft scripts. New scripts
			// use ordinary bindings. Do not copy the automatic snapshot here.
			Value legacy = Value::object();
			for (const auto &field : state.fields)
				if (field.first != "__glob2_globals")
					legacy.fields.push_back(field);
			JSValueOwner context(ctx, e.context());
			JSValueOwner scriptState(ctx, e.toJS(legacy));
			if (initialize)
			{
				auto result =
					call(e, live->exports->get(), "init", context.get(), scriptState.get(), true);
				if (!JS_IsUndefined(result.get()) && !JS_IsNull(result.get()))
					throw std::runtime_error("init must not return effects");
			}
			const char *entry = "__glob2_step";
			if (host.profile == 2)
				for (const char *name : {"step", "main"})
				{
					JSValueOwner exported(ctx, e.get(live->exports->get(), name));
					if (JS_IsFunction(ctx, exported.get()))
					{
						entry = name;
						break;
					}
				}
			auto effects =
				call(e, live->exports->get(), entry, context.get(), scriptState.get(), false);
			if (e.hostFailed || JS_Glob2HostFailure(e.runtime))
				throw HostFailure("JavaScript native resource limit exhausted");
			if (!e.fuel || (host.profile == 2 && !e.nativeFuel))
				throw std::runtime_error("JavaScript work budget exhausted");
			Result result{e.fromJS(scriptState.get()),
						  JS_IsUndefined(effects.get()) ? Value() : e.fromJS(effects.get())};
			JSValueOwner bindings(ctx, JS_Glob2ModuleBindings(ctx, live->module));
			if (JS_IsException(bindings.get()))
				e.fail();
			result.state.set("__glob2_globals", codec.capture(bindings.get()));
			live->candidate = result.state.encode();
			result.effects.encode();
			if (host.profile == 2)
			{
				result.commands = e.commands;
				result.telemetry = e.diagnosticValues;
				result.telemetry.set("runtime.queryWork",
									 Value::object()
										 .set("value", double(16000000 - e.nativeFuel))
										 .set("updated", host.tick));
				result.telemetry.set(
					"runtime.placementFailures",
					Value::object().set("value", e.placementFailures).set("updated", host.tick));
				result.commands.encode();
				result.telemetry.encode();
			}
			e.host = nullptr;
			return result;
		}
		catch (const std::bad_alloc &)
		{
			discard();
			throw HostFailure("Native allocation failed in persistent JavaScript runtime");
		}
		catch (...)
		{
			discard();
			throw;
		}
	}
};
} // namespace
std::unique_ptr<Runtime> makeRuntime()
{
	return std::make_unique<QuickRuntime>();
}
Metadata inspectAI(const std::string &source)
{
	return QuickRuntime().inspect(source);
}
} // namespace Script
