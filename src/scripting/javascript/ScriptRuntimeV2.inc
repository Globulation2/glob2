// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside Environment: all engine interaction still crosses Host.query.
struct Managed
{
	Value observed, desired;
	JSValue object = JS_UNDEFINED;
	std::map<std::string, std::size_t> operations;
};
std::map<std::string, Managed> managed;
std::set<void *> ephemeral;
std::vector<JSValue> fieldHandles;
Value commands = Value::array(), diagnosticValues = Value::object();
std::uint64_t nativeFuel = 16000000;
unsigned placementFailures = 0;
void clearManaged()
{
	for (auto &[key, value] : managed)
		JS_FreeValue(ctx, value.object);
	managed.clear();
	ephemeral.clear();
	for (auto value : fieldHandles)
		JS_FreeValue(ctx, value);
	fieldHandles.clear();
	commands = Value::array();
	diagnosticValues = Value::object();
	nativeFuel = 16000000;
	placementFailures = 0;
}
void readonly(JSValueConst value)
{
	// Built-in/source identity tracking must not retain addresses of temporary
	// observations: QuickJS may reuse those addresses in a later callback.
	auto permanent = std::move(frozen);
	frozen.clear();
	try
	{
		freeze(value);
	}
	catch (...)
	{
		frozen = std::move(permanent);
		throw;
	}
	frozen = std::move(permanent);
}
Value hostQuery(const std::string &name, const std::vector<Value> &args)
{
	if (!host || !host->query)
		throw std::runtime_error("Game API unavailable outside step(ctx)");
	const bool spatial = name.rfind("spatial.", 0) == 0;
	return host->query(name, args,
					   [this, spatial](std::size_t work, std::size_t bytes)
					   {
						   if (spatial)
						   {
							   if (work > nativeFuel)
							   {
								   nativeFuel = 0;
								   throw std::runtime_error("Native spatial work budget exhausted");
							   }
							   nativeFuel -= work;
						   }
						   else
							   charge(work);
						   chargeNative(bytes);
					   });
}
unsigned appendCommand(Value command)
{
	if (commands.items.size() >= 256 || host->nextAction > 0x7fffffffu - 256)
		throw std::runtime_error("AI action limit exceeded");
	unsigned id = host->nextAction + unsigned(commands.items.size());
	command.set("actionId", id);
	commands.items.push_back(std::move(command));
	return id;
}
static JSValue managedProperty(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv,
							   int setter, JSValue *data)
{
	auto &e = *static_cast<Environment *>(JS_GetContextOpaque(ctx));
	try
	{
		auto key = e.string(data[0]), property = e.string(data[1]);
		auto it = e.managed.find(key);
		if (it == e.managed.end() || !e.host)
			throw std::runtime_error(
				"Entity belongs to an expired callback; store its ref instead");
		auto &m = it->second;
		if (!setter)
		{
			auto value = e.toJS(m.desired.get(property));
			if (JS_IsObject(value))
				e.readonly(value);
			return value;
		}
		if (argc != 1)
			throw std::runtime_error("Property assignment requires a value");
		auto value = e.fromJS(argv[0]);
		auto desired = m.desired;
		desired.set(property, value);
		std::string operation = property;
		if (property == "x" || property == "y")
			operation = "moveFlag";
		if (property == "receiveMask" || property == "sendMask")
			operation = "exchange";
		Value command =
			Value::object().set("type", operation).set("building", m.observed.get("ref"));
		if (operation == "moveFlag")
			command.set("x", desired.get("x")).set("y", desired.get("y"));
		else if (operation == "exchange")
			command.set("receiveMask", desired.get("receiveMask"))
				.set("sendMask", desired.get("sendMask"));
		else
			command.set(property == "production"          ? "ratios"
						: property == "minimumLevel"      ? "level"
						: property == "clearingResources" ? "resources"
														  : property,
						value);
		// Validate immediately for useful, catchable assignment diagnostics.
		e.hostQuery("validateAction", {command});
		auto existing = m.operations.find(operation);
		if (existing == m.operations.end())
		{
			const auto index = e.commands.items.size();
			e.appendCommand(command);
			m.operations.emplace(operation, index);
		}
		else
		{
			command.set("actionId", e.commands.items[existing->second].get("actionId"));
			e.commands.items[existing->second] = std::move(command);
		}
		m.desired = std::move(desired);
		return JS_UNDEFINED;
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
		return JS_ThrowTypeError(ctx, "entity property: %s", ex.what());
	}
}
JSValue managedBuilding(Value data)
{
	if (data.kind == Value::Null)
		return JS_NULL;
	if (data.get("team").number != host->team)
	{
		auto value = toJS(data);
		readonly(value);
		return value;
	}
	const std::string key = std::to_string(unsigned(data.get("id").number)) + ":" +
							std::to_string(unsigned(data.get("generation").number));
	if (auto it = managed.find(key); it != managed.end())
		return JS_DupValue(ctx, it->second.object);
	Value ref = Value::object().set("id", data.get("id")).set("generation", data.get("generation"));
	data.set("ref", ref);
	Managed m;
	m.observed = data;
	m.desired = data;
	const auto pending = hostQuery("desired", {ref});
	for (const auto &[field, value] : pending.fields)
		m.desired.set(field, value);
	JSValueOwner object(ctx, JS_NewObjectProto(ctx, JS_NULL));
	for (const auto &[field, value] : data.fields)
	{
		const bool flag = data.get("virtual").number != 0;
		const bool writable = field == "workers" || field == "priority" ||
							  (field == "production" && data.get("shortType").number == 0) ||
							  ((field == "receiveMask" || field == "sendMask") &&
							   data.get("shortType").number == 12) ||
							  (flag && (field == "x" || field == "y" || field == "range" ||
										field == "minimumLevel")) ||
							  (field == "clearingResources" && data.get("shortType").number == 10);
		JSAtomOwner atom(ctx, JS_NewAtom(ctx, field.c_str()));
		if (writable)
		{
			JSValueOwner k(ctx, JS_NewString(ctx, key.c_str())),
				p(ctx, JS_NewString(ctx, field.c_str()));
			JSValue args[] = {k.get(), p.get()};
			JSValue getter = JS_NewCFunctionData(ctx, managedProperty, 0, 0, 2, args);
			JSValue setter = JS_NewCFunctionData(ctx, managedProperty, 1, 1, 2, args);
			if (JS_DefinePropertyGetSet(ctx, object.get(), atom.get(), getter, setter,
										JS_PROP_ENUMERABLE) < 0)
				fail();
		}
		else
		{
			JSValue v = toJS(value);
			if (JS_IsObject(v))
				readonly(v);
			if (JS_DefinePropertyValue(ctx, object.get(), atom.get(), v, JS_PROP_ENUMERABLE) < 0)
				fail();
		}
	}
	JSValue observed = toJS(data);
	readonly(observed);
	if (JS_DefinePropertyValueStr(ctx, object.get(), "observed", observed, JS_PROP_ENUMERABLE) <
			0 ||
		JS_PreventExtensions(ctx, object.get()) < 0)
		fail();
	m.object = JS_DupValue(ctx, object.get());
	ephemeral.insert(JS_VALUE_GET_PTR(object.get()));
	managed.emplace(key, std::move(m));
	return object.release();
}
static JSValue service(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int magic)
{
	static const char *names[] = {"create",
								  "build",
								  "upgrade",
								  "repair",
								  "delete",
								  "zone",
								  "status",
								  "cancel",
								  "distance",
								  "displacement",
								  "footprintDistance",
								  "overlap",
								  "distanceField",
								  "fieldValue",
								  "summary",
								  "hotspots",
								  "placement",
								  "components",
								  "passable",
								  "telemetry"};
	auto &e = *static_cast<Environment *>(JS_GetContextOpaque(ctx));
	try
	{
		e.charge(1);
		std::vector<Value> args;
		for (int i = 0; i < argc; ++i)
			args.push_back(e.fromJS(argv[i]));
		const std::string name = names[magic];
		if (magic == 19)
		{
			if (args.size() < 2 || args.size() > 3 || args[0].kind != Value::String ||
				args[0].text.empty() || args[0].text.size() > 128)
				throw std::runtime_error("telemetry.set requires a name and scalar value");
			const auto &v = args[1];
			if (v.kind != Value::Number && v.kind != Value::Boolean && v.kind != Value::String)
				throw std::runtime_error("Telemetry requires a number, boolean or string");
			if (v.text.size() > 512 || (e.diagnosticValues.fields.size() >= 128 &&
										e.diagnosticValues.get(args[0].text).kind == Value::Null))
				throw std::runtime_error("Telemetry exceeds limit");
			if (args[0].text.rfind("runtime.", 0) == 0)
				throw std::runtime_error("runtime telemetry names are reserved");
			Value sample = Value::object().set("value", v).set("updated", e.host->tick);
			if (args.size() == 3)
			{
				const auto &d = args[2];
				if (d.kind != Value::String && d.kind != Value::Object)
					throw std::runtime_error(
						"Telemetry description requires text or an options record");
				if (d.text.size() > 512 || d.get("unit").text.size() > 64 ||
					d.get("description").text.size() > 512)
					throw std::runtime_error("Telemetry description exceeds limit");
				sample.set("description", d);
			}
			e.diagnosticValues.set(args[0].text, sample);
			return JS_UNDEFINED;
		}
		if (magic >= 8)
		{
			if (magic == 16)
				args.push_back(e.commands);
			auto value = e.toJS(e.hostQuery("spatial." + name, args));
			if (magic == 12 && JS_IsObject(value))
			{
				e.ephemeral.insert(JS_VALUE_GET_PTR(value));
				e.fieldHandles.push_back(JS_DupValue(ctx, value));
				e.readonly(value);
			}
			return value;
		}
		if (magic == 6)
			return e.toJS(e.hostQuery("actionStatus", args));
		if (args.size() != 1)
			throw std::runtime_error(name + " requires one argument");
		Value command = args[0];
		if (magic == 7)
			command = Value::object().set("type", "cancel").set("target", args[0]);
		else if (magic == 1)
		{
			auto placement = e.hostQuery("spatial.placement", {args[0], e.commands});
			if (!placement.get("found").number)
			{
				++e.placementFailures;
				return JS_NULL;
			}
			command = placement.get("order");
		}
		else if (magic == 2 || magic == 3)
		{
			command.set("type", "construction").set("intent", name);
		}
		else if (magic != 5)
			command.set("type", name);
		e.hostQuery("validateAction", {command});
		return JS_NewUint32(ctx, e.appendCommand(std::move(command)));
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
		return JS_ThrowTypeError(ctx, "%s: %s", names[magic], ex.what());
	}
}
void services(JSValueConst context)
{
	JSValueOwner actions(ctx, JS_NewObjectProto(ctx, JS_NULL)),
		spatial(ctx, JS_NewObjectProto(ctx, JS_NULL)),
		telemetry(ctx, JS_NewObjectProto(ctx, JS_NULL));
	const char *names[] = {"create",
						   "build",
						   "upgrade",
						   "repair",
						   "delete",
						   "zone",
						   "status",
						   "cancel",
						   "distance",
						   "displacement",
						   "footprintDistance",
						   "overlap",
						   "distanceField",
						   "fieldValue",
						   "summary",
						   "hotspots",
						   "placement",
						   "components",
						   "passable",
						   "set"};
	for (int i = 0; i < 20; ++i)
		set(i < 8    ? actions.get()
			: i < 19 ? spatial.get()
					 : telemetry.get(),
			names[i], JS_NewCFunctionMagic(ctx, service, names[i], 1, JS_CFUNC_generic_magic, i));
	set(context, "actions", actions.release());
	set(context, "spatial", spatial.release());
	set(context, "telemetry", telemetry.release());
}
