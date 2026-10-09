// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "ToolkitRecords.h"
#include "Grid.h"
#include "GenerationWork.h"
#include "GenerationResult.h"
#include "ExampleBlueprints.h"
#include <cfenv>
#include <filesystem>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace MapGeneration::JavaScript
{
JSClassID Binding::nativeClass = 0;
Binding::Binding(const Package &p, const GenerationRequest &r, Game *g, GenerationContext *c,
				 bool ro, bool inspect)
	: package(p), request(r), game(g), generation(c), readonly(ro), inspecting(inspect)
{
	if (std::fegetround() != FE_TONEAREST)
		throw ScriptError("Unsupported floating-point rounding mode");
	runtime = JS_NewRuntime();
	if (!runtime)
		throw ResourceError("Cannot create generation runtime");
	try
	{
		initializeRuntime();
		initializeNativeClass();
		initializeGlobals();
		initializeModules();
	}
	catch (...)
	{
		if (ctx)
			JS_FreeContext(ctx);
		JS_FreeRuntime(runtime);
		ctx = nullptr;
		runtime = nullptr;
		throw;
	}
}
void Binding::initializeRuntime()
{
	JS_Glob2GeneratorImports(runtime, true);
	JS_SetMemoryLimit(runtime, 128 * 1024 * 1024);
	JS_SetMaxStackSize(runtime, 512 * 1024);
	JS_SetInterruptHandler(
		runtime,
		[](JSRuntime *, void *opaque) -> int
		{
			auto &e = *static_cast<Binding *>(opaque);
			if (!e.fuel || e.exhausted)
			{
				e.exhaust(Exhaustion::InterpreterWork);
				return 1;
			}
			--e.fuel;
			return 0;
		},
		this);
	ctx = JS_NewContextRaw(runtime);
	if (!ctx)
		throw ResourceError("Cannot create generation context");
	JS_SetContextOpaque(ctx, this);
	if (JS_AddIntrinsicBaseObjects(ctx) < 0 || JS_AddIntrinsicEval(ctx) < 0 ||
		JS_AddIntrinsicPromise(ctx) < 0 || JS_AddIntrinsicJSON(ctx) < 0 ||
		JS_AddIntrinsicMapSet(ctx) < 0)
		fail();
}
void Binding::initializeNativeClass()
{
	{
		static std::mutex mutex;
		std::lock_guard lock(mutex);
		if (!nativeClass)
			JS_NewClassID(runtime, &nativeClass);
	}
	JSClassDef def{};
	def.class_name = "GeneratorToolkitObject";
	def.finalizer = finalize;
	if (JS_NewClass(runtime, nativeClass, &def) < 0)
		fail();
	JS_SetRuntimeOpaque(runtime, this);
}
void Binding::initializeGlobals()
{
	Script::JSValueOwner global(ctx, JS_GetGlobalObject(ctx));
	for (const char *name : {"BigInt", "eval", "Function", "Promise"})
	{
		Script::JSAtomOwner atom(ctx, JS_NewAtom(ctx, name));
		JS_DeleteProperty(ctx, global.get(), atom.get(), 0);
	}
	Script::JSValueOwner math(ctx, JS_GetPropertyStr(ctx, global.get(), "Math"));
	add(math.get(), "random",
		[this](JSValueConst, int, JSValueConst *)
		{
			if (inspecting || !generation)
				throw TypeMismatch("Randomness is unavailable during inspection");
			return write(generation->stream("javascript-default")() / 4294967296.0);
		});
}
void Binding::initializeModules()
{
	JS_SetModuleLoaderFunc(
		runtime,
		[](JSContext *ctx, const char *base, const char *name, void *) -> char *
		{
			try
			{
				std::string path;
				if (std::strncmp(name, "./", 2) == 0 || std::strncmp(name, "../", 3) == 0)
					path = (std::filesystem::path(base).parent_path() / name)
							   .lexically_normal()
							   .generic_string();
				else
					throw TypeMismatch("Generator imports must be relative package modules");
				if (path.empty() || path.front() == '/' || path.starts_with("../") ||
					path.find('\\') != std::string::npos)
					throw TypeMismatch("Generator import leaves package");
				auto &e = *static_cast<Binding *>(JS_GetContextOpaque(ctx));
				if (!e.package.modules.contains(path))
					throw TypeMismatch("Missing generator module: " + path);
				return js_strdup(ctx, path.c_str());
			}
			catch (const std::exception &e)
			{
				JS_ThrowReferenceError(ctx, "%s", e.what());
				return nullptr;
			}
		},
		[](JSContext *ctx, const char *name, void *) -> JSModuleDef *
		{
			auto &e = *static_cast<Binding *>(JS_GetContextOpaque(ctx));
			auto it = e.package.modules.find(name);
			if (it == e.package.modules.end())
			{
				JS_ThrowReferenceError(ctx, "Unknown package module");
				return nullptr;
			}
			Script::JSValueOwner v(ctx, JS_Eval(ctx, it->second.data(), it->second.size(), name,
												JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
			if (JS_IsException(v.get()))
				return nullptr;
			return static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(v.get()));
		},
		this);
}
void Binding::tracePrototype(const char *stage, const std::type_info &type,
                             JSValueConst value, bool inserted)
{
	const auto *enabled = std::getenv("GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS");
	if (!enabled || std::strcmp(enabled, "1") != 0)
		return;
	std::cerr << "GENERATOR_PROTOTYPE " << package.id << " " << stage
	          << " type=" << type.name() << " object=" << JS_VALUE_GET_PTR(value)
	          << " inserted=" << inserted << " cache=" << prototypes.size() << "\n";
}
Binding::~Binding()
{
	// Native callbacks may own JS values; release these while their context is alive.
	std::vector<std::shared_ptr<void>> owners;
	for (auto *b : boxes)
	{
		owners.push_back(std::move(b->owner));
		b->pointer = nullptr;
	}
	owners.clear();
	for (auto &[key, value] : cachedDesigns)
		JS_FreeValue(ctx, value);
	cachedDesigns.clear();
	for (auto &[type, value] : prototypes)
	{
		const auto *enabled = std::getenv("GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS");
		if (enabled && std::strcmp(enabled, "1") == 0)
			std::cerr << "GENERATOR_PROTOTYPE " << package.id << " release type="
			          << type.name() << " object=" << JS_VALUE_GET_PTR(value) << "\n";
		JS_FreeValue(ctx, value);
	}
	prototypes.clear();
	operations.clear();
	if (ctx)
		JS_FreeContext(ctx);
	if (runtime)
		JS_FreeRuntime(runtime);
}
void Binding::finalize(JSRuntime *rt, JSValue value)
{
	auto *b = static_cast<NativeBox *>(JS_GetOpaque(value, nativeClass));
	if (!b)
		return;
	auto *e = static_cast<Binding *>(JS_GetRuntimeOpaque(rt));
	if (e)
		e->boxes.erase(b);
	delete b;
}
NativeBox *Binding::box(JSValueConst value) const
{
	return static_cast<NativeBox *>(JS_GetOpaque(value, nativeClass));
}
void Binding::exhaust(Exhaustion cause)
{
	if (exhaustion == Exhaustion::None)
		exhaustion = cause;
	exhausted = true;
}
const char *Binding::exhaustionMessage() const
{
	switch (exhaustion)
	{
	case Exhaustion::InterpreterWork:
		return "Generator execution budget exhausted";
	case Exhaustion::NativeWork:
		return "Generator native work budget exhausted";
	case Exhaustion::NativeMemory:
		return "Generator native memory budget exhausted";
	case Exhaustion::CallbackDepth:
		return "Native callback nesting exceeds 64";
	case Exhaustion::Operations:
		return "Too many toolkit operation handles";
	case Exhaustion::CachedDesigns:
		return "Too many cached designs";
	case Exhaustion::None:
	case Exhaustion::HostFailure:
		return "Generator resource budget exhausted";
	}
	return "Generator resource budget exhausted";
}
[[noreturn]] void Binding::rejectResource(Exhaustion cause)
{
	exhaust(cause);
	throw ResourceError(exhaustionMessage());
}
void Binding::charge(uint64_t n)
{
	if (exhausted || n > fuel)
	{
		if (!exhausted)
			fuel = 0;
		rejectResource(Exhaustion::InterpreterWork);
	}
	fuel -= n;
}
void Binding::chargeNative(uint64_t n)
{
	if (exhausted || n > nativeFuel)
	{
		if (!exhausted)
			nativeFuel = 0;
		rejectResource(Exhaustion::NativeWork);
	}
	nativeFuel -= n;
}
void Binding::allocate(size_t n)
{
	constexpr size_t limit = 128 * 1024 * 1024;
	// This is an invocation allocation budget, not an estimate of live RSS.
	// Keep it monotonic and give QuickJS only the remaining shared allowance.
	if (exhausted || n > limit - nativeBytes)
	{
		rejectResource(Exhaustion::NativeMemory);
	}
	nativeBytes += n;
	JS_SetMemoryLimit(runtime, limit - nativeBytes);
}
Binding::CallbackScope::CallbackScope(Binding &binding)
	: e(binding), previousGame(e.game), previousGeneration(e.generation),
	  previousReadonly(e.readonly), previousGenerationLease(e.generationLease)
{
	if (e.callbackDepth >= 64)
		e.rejectResource(Exhaustion::CallbackDepth);
	e.allocate(64);
	lease = std::make_shared<bool>(true);
	e.leases.push_back(lease);
	++e.callbackDepth;
}
Binding::CallbackScope::~CallbackScope()
{
	*lease = false;
	e.leases.pop_back();
	--e.callbackDepth;
	e.game = previousGame;
	e.generation = previousGeneration;
	e.readonly = previousReadonly;
	e.generationLease = std::move(previousGenerationLease);
}
[[noreturn]] void Binding::fail()
{
	Script::JSValueOwner error(ctx, JS_GetException(ctx));
	// QuickJS may replace a caught native error with "interrupted", or run out
	// of memory while constructing it. Preserve our original bounded cause.
	if (exhausted || !fuel || JS_Glob2HostFailure(runtime))
		rejectResource(!fuel ? Exhaustion::InterpreterWork : Exhaustion::HostFailure);
	Script::JSStringOwner text(ctx, error.get());
	std::string message = text.get() ? text.get() : "JavaScript exception";
	Script::JSValueOwner stack(ctx, JS_GetPropertyStr(ctx, error.get(), "stack"));
	if (JS_IsString(stack.get()))
	{
		Script::JSStringOwner s(ctx, stack.get());
		if (s.get())
			message += '\n' + std::string(s.get());
	}
	if (JS_Glob2HostFailure(runtime))
		rejectResource(Exhaustion::HostFailure);
	throw ScriptError(message);
}
JSValue Binding::dispatch(JSContext *ctx, JSValueConst self, int n, JSValueConst *a, int index,
						  JSValue *)
{
	auto &e = *static_cast<Binding *>(JS_GetContextOpaque(ctx));
	try
	{
		e.charge();
		auto operation = e.operations.at(index).call;
		return (*operation)(self, n, a);
	}
	catch (const ResourceError &)
	{
		e.exhaust(Exhaustion::HostFailure);
		return JS_ThrowInternalError(ctx, "%s", e.exhaustionMessage());
	}
	catch (const std::exception &error)
	{
		return JS_ThrowTypeError(ctx, "%s: %s", e.operations.at(index).name.c_str(), error.what());
	}
}
void Binding::add(JSValueConst object, const std::string &name,
				  std::function<JSValue(JSValueConst, int, JSValueConst *)> operation)
{
	auto index = registerOperation(name, std::move(operation));
	auto fn = JS_NewCFunctionData(ctx, dispatch, 0, index, 0, nullptr);
	check(fn);
	if (JS_SetPropertyStr(ctx, object, name.c_str(), fn) < 0)
		fail();
}
unsigned
Binding::registerOperation(std::string name,
						   std::function<JSValue(JSValueConst, int, JSValueConst *)> operation)
{
	if (operations.size() >= MaximumOperations)
		rejectResource(Exhaustion::Operations);
	// Every registration path retains its callable until runtime teardown.
	allocate(sizeof(Operation) + name.capacity() + 128);
	const auto index = unsigned(operations.size());
	operations.push_back(
		{std::move(name), std::make_shared<Operation::Call>(std::move(operation))});
	return index;
}
JSValue
Binding::addAccessor(std::function<JSValue(Binding &, JSValueConst, int, JSValueConst *)> operation)
{
	auto index = registerOperation(
		"field", [this, operation = std::move(operation)](JSValueConst self, int n, JSValueConst *a)
		{ return operation(*this, self, n, a); });
	auto result = JS_NewCFunctionData(ctx, dispatch, 0, index, 0, nullptr);
	check(result);
	return result;
}
void Binding::overload(JSValueConst object, const std::string &name,
					   std::function<JSValue(JSValueConst, int, JSValueConst *)> operation)
{
	auto key = name + "@" + std::to_string(reinterpret_cast<uintptr_t>(JS_VALUE_GET_PTR(object)));
	auto index = registerOperation(name, std::move(operation));
	auto &list = overloads[key];
	list.push_back(index);
	if (list.size() == 1)
		add(object, name,
			[this, key, name](JSValueConst self, int n, JSValueConst *a)
			{
				std::string reason;
				// Retain dispatch data: callbacks may register new operations during this call.
				auto candidates = overloads.at(key);
				for (auto i : candidates)
				{
					auto call = operations.at(i).call;
					try
					{
						return (*call)(self, n, a);
					}
					catch (const TypeMismatch &error)
					{
						reason = error.what();
					}
				}
				throw TypeMismatch("No matching overload for " + name + ": " + reason);
				return JS_UNDEFINED;
			});
}
JSValue Binding::evaluate()
{
	const auto &s = package.modules.at(package.entry);
	Script::JSValueOwner code(ctx, JS_Eval(ctx, s.data(), s.size(), package.entry.c_str(),
										   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
	check(code.get());
	auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(code.get()));
	if (JS_ResolveModule(ctx, code.get()) < 0)
		fail();
	Script::JSValueOwner evaluated(ctx, JS_EvalFunction(ctx, code.release()));
	check(evaluated.get());
	if (JS_IsPromise(evaluated.get()) &&
		JS_PromiseState(ctx, evaluated.get()) != JS_PROMISE_FULFILLED)
		throw ScriptError("Generator modules must initialize synchronously");
	auto ns = JS_GetModuleNamespace(ctx, module);
	check(ns);
	return ns;
}
void Binding::setProperty(JSValueConst object, const char *name, JSValue value)
{
	if (JS_SetPropertyStr(ctx, object, name, value) < 0)
		fail();
}
JSValue Binding::requestValue()
{
	Script::JSValueOwner result(ctx, JS_NewObject(ctx)), options(ctx, JS_NewObject(ctx));
	check(result.get());
	check(options.get());
	for (const auto &[key, value] : request.options)
		if (JS_DefinePropertyValueStr(ctx, options.get(), key.c_str(), write(value),
									  JS_PROP_C_W_E) < 0)
			fail();
	if (JS_FreezeObject(ctx, options.get()) < 0)
		fail();
	setProperty(result.get(), "width", write(1 << request.wDec));
	setProperty(result.get(), "height", write(1 << request.hDec));
	setProperty(result.get(), "teams", write(request.nbTeams));
	setProperty(result.get(), "workers", write(request.nbWorkers));
	setProperty(result.get(), "seed", write(request.seed));
	setProperty(result.get(), "options", options.release());
	if (JS_FreezeObject(ctx, result.get()) < 0)
		fail();
	return result.release();
}
JSValue Binding::toolkitValue()
{
	Script::JSValueOwner toolkit(ctx, JS_NewObject(ctx)), blueprints(ctx, JS_NewObject(ctx));
	check(toolkit.get());
	check(blueprints.get());
	registerToolkit(*this, toolkit.get());
	registerFortsBlueprint(*this, blueprints.get());
	registerEvenGroundBlueprint(*this, blueprints.get());
	setProperty(toolkit.get(), "Blueprints", blueprints.release());
	return toolkit.release();
}
JSValue Binding::context()
{
	Script::JSValueOwner result(ctx, JS_NewObject(ctx));
	check(result.get());
	if (JS_DefinePropertyValueStr(ctx, result.get(), "request", requestValue(),
								  JS_PROP_ENUMERABLE) < 0)
		fail();
	setProperty(result.get(), "torus", write(Torus(1 << request.wDec, 1 << request.hDec)));
	setProperty(result.get(), "toolkit", toolkitValue());
	installContextRandom(result.get());
	installContextTelemetry(result.get());
	installContextDesign(result.get());
	installContextWorld(result.get());
	installContextBuffers(result.get());
	return result.release();
}
void Binding::installContextRandom(JSValueConst result)
{
	add(result, "stream",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 1 || inspecting || !generation)
				throw TypeMismatch("stream(name) is unavailable here");
			auto name = read<std::string>(a[0]);
			return handle(&generation->stream(name));
		});
	add(result, "bounded",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 2 || inspecting || !generation)
				throw TypeMismatch("bounded(name, bound)");
			return write(generation->bounded(read<std::string>(a[0]), read<uint32_t>(a[1])));
		});
	add(result, "shuffle",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 2 || !generation || inspecting || !JS_IsArray(a[0]))
				throw TypeMismatch("shuffle(array, streamName)");
			auto stream = read<std::string>(a[1]);
			Script::JSValueOwner length(ctx, JS_GetPropertyStr(ctx, a[0], "length"));
			auto count = read<unsigned>(length.get());
			if (count > 1048576)
				throw TypeMismatch("Shuffle exceeds limit");
			for (unsigned i = count; i > 1; --i)
			{
				charge();
				auto j = generation->bounded(stream, i);
				Script::JSValueOwner x(ctx, JS_GetPropertyUint32(ctx, a[0], i - 1)),
					y(ctx, JS_GetPropertyUint32(ctx, a[0], j));
				check(x.get());
				check(y.get());
				if (JS_SetPropertyUint32(ctx, a[0], i - 1, y.release()) < 0 ||
					JS_SetPropertyUint32(ctx, a[0], j, x.release()) < 0)
					fail();
			}
			return JS_UNDEFINED;
		});
}
void Binding::installContextTelemetry(JSValueConst result)
{
	add(result, "stage",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 1 || !generation)
				throw TypeMismatch("stage(name)");
			generation->stage = read<std::string>(a[0]);
			return JS_UNDEFINED;
		});
	add(result, "measure",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n < 2 || n > 3 || !generation)
				throw TypeMismatch("measure(key, number, subject?)");
			generation->telemetry.measure(read<std::string>(a[0]), read<double>(a[1]),
										  n == 3 ? read<int>(a[2]) : -1);
			return JS_UNDEFINED;
		});
	for (const char *kind : {"choice", "fallback"})
		add(result, kind,
			[this, kind](JSValueConst, int n, JSValueConst *a)
			{
				if (n < 2 || n > 3 || !generation)
					throw TypeMismatch("telemetry(key, text, subject?)");
				auto key = read<std::string>(a[0]), text = read<std::string>(a[1]);
				auto subject = n == 3 ? read<int>(a[2]) : -1;
				if (std::string_view(kind) == "choice")
					generation->telemetry.choice(key, text, subject);
				else
					generation->telemetry.fallback(key, text, subject);
				return JS_UNDEFINED;
			});
}
void Binding::installContextDesign(JSValueConst result)
{
	add(result, "cachedDesign",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 2 || !JS_IsFunction(ctx, a[1]))
				throw TypeMismatch("cachedDesign(key, builder)");
			auto key = read<std::string>(a[0]);
			auto it = cachedDesigns.find(key);
			if (it != cachedDesigns.end())
				return JS_DupValue(ctx, it->second);
			if (cachedDesigns.size() >= 128)
				rejectResource(Exhaustion::CachedDesigns);
			Script::JSValueOwner value(ctx, JS_Call(ctx, a[1], JS_UNDEFINED, 0, nullptr));
			check(value.get());
			if (JS_IsPromise(value.get()))
				throw ScriptError("Design builders must be synchronous");
			cachedDesigns.emplace(key, JS_DupValue(ctx, value.get()));
			return value.release();
		});
	add(result, "resolveDesignChoice",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 3 || !JS_IsFunction(ctx, a[2]))
				throw TypeMismatch("resolveDesignChoice(streamName, values, feasible)");
			auto name = read<std::string>(a[0]);
			auto choices = read<std::vector<int>>(a[1]);
			std::vector<int> feasible;
			auto predicate = read<std::function<bool(int)>>(a[2]);
			for (auto v : choices)
				if (predicate(v))
					feasible.push_back(v);
			return write(GenerationContext::choiceFromSeed(request.seed, name, feasible));
		});
}
void Binding::installContextWorld(JSValueConst result)
{
	add(result, "buildingType",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (!game || inspecting || n > 3)
				throw TypeMismatch("buildingType(name?, level?, construction?) requires a world");
			int id = n ? game->buildingsTypes.getTypeNum(read<std::string>(a[0]),
														 n > 1 ? read<int>(a[1]) : 0,
														 n > 2 ? read<bool>(a[2]) : false)
					   : game->buildingsTypes.getStartingBuildingTypeNum();
			auto *type = id >= 0 ? game->buildingsTypes.get(id) : nullptr;
			if (!type)
				throw TypeMismatch("Unknown building type");
			return handle(type);
		});
	add(result, "addTeams",
		[this](JSValueConst, int n, JSValueConst *)
		{
			if (n || !game || readonly || inspecting)
				throw TypeMismatch("addTeams() requires generation");
			if (game->mapHeader.getNumberOfTeams())
				throw TypeMismatch("Teams already placed");
			for (int i = 0; i < (package.hasStartingColonies ? request.nbTeams : 1); ++i)
				game->addTeam();
			return JS_UNDEFINED;
		});

	add(result, "resourceType",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 1 || !game || inspecting)
				throw TypeMismatch("resourceType(key) requires a world");
			auto id = game->map.resourceRegistry().find(read<std::string>(a[0]));
			if (!id)
				throw TypeMismatch("Unknown resource key");
			return write(resourceIndex(*id));
		});
	add(result, "setResource",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 3 || !game || readonly || inspecting)
				throw TypeMismatch("setResource(tile, type, size)");
			auto tile = read<unsigned>(a[0]);
			auto type = read<int>(a[1]), size = read<int>(a[2]);
			if (tile >= unsigned(game->map.getW() * game->map.getH()) || type < 0 ||
				!game->map.resourceRegistry().valid(unsigned(type)) || size < 0 ||
				size >= std::min(game->map.getW(), game->map.getH()))
				throw TypeMismatch("Invalid resource placement");
			game->map.setResourceByIndex(powerOfTwoRemainder(tile, game->map.getW()), tile / game->map.getW(), type,
										 size);
			return JS_UNDEFINED;
		});
	add(result, "buildable",
		[this](JSValueConst, int n, JSValueConst *a)
		{
			if (n != 1 || !game || inspecting)
				throw TypeMismatch("buildable(tile)");
			auto i = read<unsigned>(a[0]);
			if (i >= unsigned(game->map.getW() * game->map.getH()))
				throw TypeMismatch("Invalid tile");
			return write(game->map.terrainPropertiesAt(powerOfTwoRemainder(i, game->map.getW()), i / game->map.getW())
							 .buildable);
		});
}
void Binding::installContextBuffers(JSValueConst result)
{
	auto buffer = [this](const char *name, auto initial)
	{
		using T = decltype(initial);
		return [this, name, initial](JSValueConst, int n, JSValueConst *a)
		{
			if (n > 2)
				throw TypeMismatch(std::string(name) + "(length?, value?)");
			auto count =
				n ? read<unsigned>(a[0]) : unsigned((1 << request.wDec) * (1 << request.hDec));
			if (count > 1048576)
				throw TypeMismatch("Buffer exceeds limit");
			allocate(uint64_t(count) * 32);
			auto v = n > 1 ? read<T>(a[1]) : initial;
			return write(std::vector<T>(count, v));
		};
	};
	add(result, "mask", buffer("mask", static_cast<unsigned char>(0)));
	add(result, "integers", buffer("integers", int(0)));
	add(result, "field", buffer("field", double(0)));
}

std::string invoke(const Package &p, const char *callback, const GenerationRequest &r, Game *g,
				   GenerationContext *c, bool ro)
{
	try
	{
		Binding e(p, r, g, c, ro, false);
		struct BudgetReport
		{
			Binding &e;
			GenerationContext *c;
			~BudgetReport()
			{
				if (c && c->telemetry.enabled())
				{
					c->telemetry.measure("javascript.interpreter_operations",
										 Binding::InterpreterWork - e.fuel);
					c->telemetry.measure("javascript.native_checkpoints",
										 Binding::NativeWork - e.nativeFuel);
				}
			}
		} budgetReport{e, c};
		GenerationWork work{
			&e, [](void *host, std::uint64_t n) { static_cast<Binding *>(host)->chargeNative(n); },
			[](void *host, std::uint64_t bytes) { static_cast<Binding *>(host)->allocate(bytes); }};
		GenerationWorkScope scope(work);
		Script::JSValueOwner exports(e.ctx, e.evaluate());
		Script::JSValueOwner fn(e.ctx, JS_GetPropertyStr(e.ctx, exports.get(), callback));
		e.check(fn.get());
		if (JS_IsUndefined(fn.get()) && std::string_view(callback) != "generate")
			return {};
		if (!JS_IsFunction(e.ctx, fn.get()))
			throw ScriptError(std::string("Missing generator callback: ") + callback);
		Script::JSValueOwner context(e.ctx, e.context());
		JSValue arg = context.get();
		Script::JSValueOwner result(e.ctx, JS_Call(e.ctx, fn.get(), JS_UNDEFINED, 1, &arg));
		e.check(result.get());
		if (e.exhausted || JS_Glob2HostFailure(e.runtime))
			e.rejectResource(Binding::Exhaustion::HostFailure);
		if (JS_IsUndefined(result.get()) || JS_IsNull(result.get()))
			return {};
		if (JS_IsString(result.get()))
			return e.read<std::string>(result.get());
		throw ScriptError(
			"Generator callbacks return undefined on success or a diagnostic string on failure");
	}
	catch (const ResourceError &error)
	{
		throw ScriptGenerationFailure(GenerationError::BudgetExceeded,
									  p.id + " [" + callback + "]: " + error.what());
	}
	catch (const std::exception &error)
	{
		throw ScriptGenerationFailure(GenerationError::ScriptFailed,
									  p.id + " [" + callback + "]: " + error.what());
	}
}
void inspect(const Package &p)
{
	GenerationRequest request;
	GenerationContext context(request);
	Binding e(p, request, nullptr, &context, true, true);
	Script::JSValueOwner exports(e.ctx, e.evaluate());
	for (const char *name : {"generate", "validateRequest", "validateWorld"})
	{
		Script::JSValueOwner fn(e.ctx, JS_GetPropertyStr(e.ctx, exports.get(), name));
		e.check(fn.get());
		if (JS_IsUndefined(fn.get()) && std::string_view(name) != "generate")
			continue;
		if (!JS_IsFunction(e.ctx, fn.get()))
			throw ScriptError(std::string("Expected generator function: ") + name);
	}
}
} // namespace MapGeneration::JavaScript

namespace MapGeneration::JavaScript
{
void registerToolkit0(Binding &, JSValueConst);
void registerToolkit1(Binding &, JSValueConst);
void registerToolkit2(Binding &, JSValueConst);
void registerToolkit3(Binding &, JSValueConst);
void registerToolkit4(Binding &, JSValueConst);
void registerToolkit5(Binding &, JSValueConst);
void registerToolkit6(Binding &, JSValueConst);
void registerToolkit7(Binding &, JSValueConst);
void registerToolkit(Binding &e, JSValueConst value)
{
	registerToolkit0(e, value);
	registerToolkit1(e, value);
	registerToolkit2(e, value);
	registerToolkit3(e, value);
	registerToolkit4(e, value);
	registerToolkit5(e, value);
	registerToolkit6(e, value);
	registerToolkit7(e, value);
}
} // namespace MapGeneration::JavaScript
