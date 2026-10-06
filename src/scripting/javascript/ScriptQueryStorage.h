// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
namespace Script {
// Available native vector capacity only; excludes strings, container nodes,
// allocator metadata and the JavaScript runtime's inaccessible engine heap.
inline std::uint64_t retainedValueVectorBytes(const Value& value) {
    std::uint64_t bytes=value.items.capacity()*sizeof(Value)+value.fields.capacity()*sizeof(std::pair<std::string,Value>);
    for(const auto& item:value.items) bytes+=retainedValueVectorBytes(item);
    for(const auto& field:value.fields) bytes+=retainedValueVectorBytes(field.second);
    return bytes;
}
}
