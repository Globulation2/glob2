// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "quickjs.h"
#include <utility>

namespace Script
{
// QuickJS consumes values passed to setters, EvalFunction and Throw. Call release()
// at those boundaries; borrowed JSValueConst arguments never belong to a handle.
class JSValueOwner
{
	JSContext *context;
	JSValue value;

  public:
	JSValueOwner(JSContext *context, JSValue value) : context(context), value(value) {}
	~JSValueOwner() { JS_FreeValue(context, value); }
	JSValueOwner(const JSValueOwner &) = delete;
	JSValueOwner &operator=(const JSValueOwner &) = delete;
	JSValueOwner(JSValueOwner &&other) noexcept : context(other.context), value(other.release()) {}
	JSValue get() const { return value; }
	JSValue release() { return std::exchange(value, JS_UNDEFINED); }
};

class JSAtomOwner
{
	JSContext *context;
	JSAtom atom;

  public:
	JSAtomOwner(JSContext *context, JSAtom atom) : context(context), atom(atom) {}
	~JSAtomOwner() { JS_FreeAtom(context, atom); }
	JSAtomOwner(const JSAtomOwner &) = delete;
	JSAtomOwner &operator=(const JSAtomOwner &) = delete;
	JSAtom get() const { return atom; }
};

class JSStringOwner
{
  public:
	size_t length = 0;

  private:
	JSContext *context;
	const char *text;

  public:
	JSStringOwner(JSContext *context, JSValueConst value)
		: context(context), text(JS_ToCStringLen(context, &length, value))
	{
	}
	~JSStringOwner()
	{
		if (text)
			JS_FreeCString(context, text);
	}
	JSStringOwner(const JSStringOwner &) = delete;
	JSStringOwner &operator=(const JSStringOwner &) = delete;
	const char *get() const { return text; }
};

class JSPropertyOwner
{
	JSContext *context;

  public:
	JSPropertyDescriptor descriptor{};
	explicit JSPropertyOwner(JSContext *context) : context(context) {}
	~JSPropertyOwner()
	{
		JS_FreeValue(context, descriptor.value);
		JS_FreeValue(context, descriptor.getter);
		JS_FreeValue(context, descriptor.setter);
	}
	JSPropertyOwner(const JSPropertyOwner &) = delete;
	JSPropertyOwner &operator=(const JSPropertyOwner &) = delete;
};

class JSEnumerationOwner
{
	JSContext *context;

  public:
	JSPropertyEnum *keys = nullptr;
	uint32_t count = 0;
	explicit JSEnumerationOwner(JSContext *context) : context(context) {}
	~JSEnumerationOwner()
	{
		if (keys)
			JS_FreePropertyEnum(context, keys, count);
	}
	JSEnumerationOwner(const JSEnumerationOwner &) = delete;
	JSEnumerationOwner &operator=(const JSEnumerationOwner &) = delete;
};
} // namespace Script
