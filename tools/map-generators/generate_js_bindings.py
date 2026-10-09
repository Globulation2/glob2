#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate native toolkit adapters using libclang 18 (development only).

Generated adapters are committed; shipping builds do not require clang's Python module.
Run with PYTHONPATH pointing at clang's bindings and GLOB2_SDL3_PREFIX at the build SDK.
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scons"))
from sources import INCLUDE_DIRECTORIES
from clang import cindex as c

CALLBACKS = {
    "Eligible": "bool(int)",
    "CropsEligible": "bool(int)",
    "StoneEligible": "bool(int)",
    "NoiseAt": "double(int)",
    "Key": "double(int)",
    "Priority": "double(int)",
    "SplitKey": "double(int)",
    "LevelAt": "int(int)",
    "ClumpsAllowed": "bool(int)",
    "Room": "bool(int)",
    "PatchAt": "double(int)",
    "SplitAt": "double(int)",
    "AmountsFor": "GroundAmounts(int)",
    "Propose": "bool()",
    "Cost": "double()",
    "Undo": "void()",
    "Remember": "void()",
    "Recall": "void()",
    "HomeMask": "std::vector<unsigned char>(int)",
    "Anchor": "MapGeneratorPoint(int)",
    "HeadingAt": "double(double,double)",
    "VAt": "double(int)",
    "StepCost": "long long(int,int,int,int)",
    "Open": "bool(int,int)",
    "Allowed": "bool(int,int)",
    "Stop": "bool(int)",
    "Roll": "double()",
    "Accept": "bool(const std::vector<StrokePoint>&,int)",
    "Skip": "bool(int)",
    "Water": "bool(int,int)",
    "Clear": "bool(int)",
    "Rate": "Objective(const River&,const std::vector<unsigned char>&)",
    "Reduce": "unsigned char(const std::vector<unsigned char>&)",
    "Place": "void(MapGeneratorPoint)",
}
VISITS = {
    "forEachTileInShape": "void(int,double,double)",
    "forEachTileInTeardrop": "void(int,double,double)",
    "ringWithGates": "void(int,int)",
    "forEachTileInPolygon": "void(int)",
    "stampRoundHome": "void(int)",
    "stampTeardropHome": "void(int,int)",
}
EQUIVALENTS = {
    "cachedDesign": "ctx.cachedDesign: invocation-local JavaScript design cache",
    "dealStarts": "ctx.shuffle: JavaScript arrays and native buffers",
    "designFailure": "call the script design function in validateRequest",
    "resolveDesignChoice": "ctx.resolveDesignChoice: feasible options from a script callback",
    "designMismatch": "validateWorld compares its script layout against finished-world queries",
}


def qualified(v):
    names = [v.spelling]
    p = v.semantic_parent
    while p and p.kind != c.CursorKind.TRANSLATION_UNIT:
        if p.spelling:
            names.append(p.spelling)
        p = p.semantic_parent
    return "::".join(reversed(names))


def typ(t):
    # Canonical spellings qualify nested records and preserve references.
    s = t.get_canonical().spelling
    s = s.replace("std::basic_string<char>", "std::string").replace(
        "std::basic_string_view<char>", "std::string_view"
    )
    if s.startswith("std::mersenne_twister_engine<"):
        s = "std::mt19937" + (" &" if t.kind == c.TypeKind.LVALUEREFERENCE else "")
    return s


SCALAR_KINDS = (
    c.TypeKind.INT,
    c.TypeKind.UINT,
    c.TypeKind.LONG,
    c.TypeKind.ULONG,
    c.TypeKind.LONGLONG,
    c.TypeKind.ULONGLONG,
    c.TypeKind.DOUBLE,
    c.TypeKind.FLOAT,
    c.TypeKind.SHORT,
    c.TypeKind.USHORT,
    c.TypeKind.BOOL,
)


def is_output(p):
    t = typ(p.type)
    return (
        (t.endswith("&") or t.endswith("*"))
        and not t.startswith("const ")
        and (
            t.strip(" &*") == "std::string"
            or p.type.get_canonical().get_pointee().kind in SCALAR_KINDS
        )
    )


def referenced_enums(native_type):
    """Include external enums used inside public signatures, not just declarations.

    TerrainType and MaterialId live outside the toolkit headers, but are still
    numeric script arguments. Follow containers/callbacks to preserve their types.
    """
    native_type = native_type.get_canonical()
    if native_type.kind == c.TypeKind.ENUM:
        return {typ(native_type)}
    if native_type.kind in (c.TypeKind.LVALUEREFERENCE, c.TypeKind.POINTER):
        return referenced_enums(native_type.get_pointee())
    result = set()
    for i in range(native_type.get_num_template_arguments()):
        result.update(referenced_enums(native_type.get_template_argument_type(i)))
    if native_type.kind == c.TypeKind.FUNCTIONPROTO:
        result.update(referenced_enums(native_type.get_result()))
        for argument in native_type.argument_types():
            result.update(referenced_enums(argument))
    return result


@dataclass(frozen=True)
class ParameterPlan:
    """One native parameter, shared by C++ and TypeScript emitters."""

    cursor: object
    cpp_type: str
    default: str | None
    implicit: str | None

    @property
    def mutable_vector(self):
        return (
            self.cpp_type.endswith("&")
            and not self.cpp_type.startswith("const ")
            and self.cpp_type.strip(" &").startswith("std::vector<")
        )


def template_substitutions(v):
    """Resolve supported callable templates once; unlisted state templates stay native."""
    substitutions = {}
    if v.kind != c.CursorKind.FUNCTION_TEMPLATE:
        return substitutions, None
    for t in v.get_children():
        if t.kind != c.CursorKind.TEMPLATE_TYPE_PARAMETER:
            continue
        if t.spelling == "State":
            return (
                None,
                "state overload is equivalent to anneal with explicit callbacks",
            )
        if t.spelling == "Cost" and v.spelling == "growTerritories":
            signature = "long long(int)"
        elif t.spelling == "Visit":
            signature = VISITS.get(v.spelling, "void(int)")
        elif t.spelling in CALLBACKS:
            signature = CALLBACKS[t.spelling]
        else:
            return None, "unmapped template parameter " + t.spelling
        substitutions[t.spelling] = "std::function<" + signature + ">"
    return substitutions, None


def parameters(v):
    """Classify aliases, defaults and injected parameters without rendering code."""
    params = (
        list(v.get_arguments())
        if v.kind != c.CursorKind.FUNCTION_TEMPLATE
        else [x for x in v.get_children() if x.kind == c.CursorKind.PARM_DECL]
    )
    if any("MapGenerationDescriptor" in typ(p.type) for p in params):
        return None, "Legacy serialized descriptors use the frozen context request"
    substitutions, error = template_substitutions(v)
    if error:
        return None, error
    implicit = {
        "Game": "(*e.game)",
        "Map": "e.game->map",
        "GenerationContext": "(*e.generation)",
        "GenerationRequest": "e.request",
        "GenerationTelemetry": "e.generation->telemetry",
    }
    plans = []
    for p in params:
        spelling = p.type.spelling
        dependent = any(
            re.search(r"\b" + name + r"\b", spelling) for name in substitutions
        )
        cpp_type = spelling if dependent else typ(p.type)
        for name, replacement in substitutions.items():
            cpp_type = re.sub(r"\b" + name + r"\b", replacement, cpp_type)
        tokens = list(p.get_tokens())
        default = next(
            (
                " ".join(x.spelling for x in tokens[k + 1 :])
                for k, tok in enumerate(tokens)
                if tok.spelling == "="
            ),
            None,
        )
        if default and qualified(v).startswith("Fertility::"):
            default = default.replace("Path ::", "Fertility::Path ::")
        plans.append(
            ParameterPlan(
                p,
                cpp_type,
                default,
                implicit.get(cpp_type.replace("const ", "").strip(" &")),
            )
        )
    return plans, None


# These two classes retain native references instead of copying constructor inputs.
# Their owned handles must retain the lifetime of the host/reference they borrow.
CONSTRUCTOR_LEASES = {
    "HeightMap": "e.handleDependencyLease(a[2])",
    "MapGeneration::Brief": "e.contextDependencyLease()",
}


def fertility_preconditions(v):
    """External simulation helpers require explicit checks at the script boundary."""
    if qualified(v.semantic_parent) != "Fertility::Field":
        return []
    if v.spelling in ("rebuild", "rebuildWeighted"):
        return [
            "prepareFertilityRebuild(e,arg0.get(),arg1.get(),arg2.get(),arg3.get(),arg4.get());"
        ]
    if v.spelling in ("gate", "multiplyLocal"):
        return ["prepareFertilityMask(e,e.native<Fertility::Field>(self),arg0.get());"]
    if v.spelling == "at":
        return ["prepareFertilityRead(e.native<Fertility::Field>(self));"]
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--check",
        action="store_true",
        help="Fail if committed adapters or declarations need regeneration",
    )
    ap.add_argument("--libclang", default="/usr/lib/x86_64-linux-gnu/libclang-18.so.1")
    args = ap.parse_args()
    c.Config.set_library_file(args.libclang)
    stale = []

    def emit(path, contents):
        if args.check:
            if not path.exists() or path.read_text() != contents:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.write_text(contents)

    headers = sorted(
        p
        for p in (ROOT / "src/map/generator/shared").rglob("*.h")
        if p.name != "PerlinNoiseTest.h"
    )
    source = (
        '#include "map/FertilityField.h"\n#include "GeneratorControls.h"\n'
        + "\n".join('#include "' + str(p) + '"' for p in headers)
    )
    flags = [
        "-I" + str(ROOT / "third_party/quickjs-ng"),
        "-std=c++20",
        "-I" + str(ROOT / "build/linux/client/release/include"),
    ] + ["-I" + str(ROOT / p) for p in INCLUDE_DIRECTORIES]
    if os.getenv("GLOB2_SDL3_PREFIX"):
        flags += ["-I" + os.environ["GLOB2_SDL3_PREFIX"] + "/include"]
    tu = c.Index.create().parse(
        "toolkit.cpp", args=flags, unsaved_files=[("toolkit.cpp", source)]
    )
    errors = [str(d) for d in tu.diagnostics if d.severity >= c.Diagnostic.Error]
    if errors:
        sys.exit("\n".join(errors))
    seen = set()
    records = []
    functions = []
    enums = []
    constants = []

    def walk(n):
        for v in n.get_children():
            if not v.location.file or not (
                "/map/generator/shared/" in str(v.location.file)
                or str(v.location.file).endswith(
                    ("/map/FertilityField.h", "/map/generator/core/GeneratorControls.h")
                )
            ):
                continue
            if (
                str(v.location.file).endswith("/map/generator/core/GeneratorControls.h")
                and v.kind not in (c.CursorKind.NAMESPACE, c.CursorKind.ENUM_DECL)
                and v.spelling not in ("GeneratorControl", "ControlGroup")
            ):
                continue
            if (
                str(v.location.file).endswith("/map/FertilityField.h")
                and v.kind not in (c.CursorKind.NAMESPACE,)
                and v.spelling not in ("Field", "Path")
            ):
                continue
            if (
                v.kind in [c.CursorKind.STRUCT_DECL, c.CursorKind.CLASS_DECL]
                and v.is_definition()
            ):
                if qualified(v) not in seen:
                    records.append(v)
                    seen.add(qualified(v))
            elif v.kind in [c.CursorKind.FUNCTION_DECL, c.CursorKind.FUNCTION_TEMPLATE]:
                key = (qualified(v), v.type.spelling)
                if key not in seen:
                    functions.append(v)
                    seen.add(key)
            elif v.kind == c.CursorKind.ENUM_DECL and v.spelling:
                enums.append(v)
            elif (
                v.kind == c.CursorKind.VAR_DECL
                and v.semantic_parent.kind == c.CursorKind.NAMESPACE
            ):
                constants.append(v)
            if v.kind in (
                c.CursorKind.NAMESPACE,
                c.CursorKind.STRUCT_DECL,
                c.CursorKind.CLASS_DECL,
                c.CursorKind.ENUM_DECL,
            ):
                walk(v)

    walk(tu.cursor)
    out = ROOT / "src/map/generator/javascript"
    out.mkdir(exist_ok=True)
    includes = '#include "map/FertilityField.h"\n' + "\n".join(
        '#include "' + p.relative_to(ROOT / "src").as_posix() + '"' for p in headers
    )
    includes += (
        '\n#include "map/gradient/fertility/Fertility.h"'
        if (ROOT / "src/map/gradient/fertility/Fertility.h").exists()
        else ""
    )
    declarations = []
    definitions = []
    groups = {}
    coverage = []
    record_groups = {}
    record_fields = {}
    record_methods = {}
    record_constructors = {}
    record_static_methods = {}

    def group(v):
        family = Path(str(v.location.file)).stem
        if "/legacy/" in str(v.location.file):
            family = "Legacy" + family
        return family

    def wrapper(v, call=None, ret=None, constructor=None):
        plans, error = parameters(v)
        if error:
            return None, error
        lines = []
        required = 0
        count = 0
        allargs = []
        params = [plan.cursor for plan in plans]
        for i, plan in enumerate(plans):
            p = plan.cursor
            t = plan.cpp_type
            raw = t.replace("const ", "").strip(" &")
            if plan.implicit:
                if raw in ("Game", "Map"):
                    lines.append(
                        "if(!e.game || e.inspecting"
                        + (" || e.readonly" if not t.startswith("const ") else "")
                        + ') throw TypeMismatch("World access unavailable in this callback");'
                    )
                elif raw in ("GenerationContext", "GenerationTelemetry"):
                    lines.append(
                        'if(!e.generation || e.inspecting) throw TypeMismatch("Generation context unavailable");'
                    )
                allargs.append(plan.implicit)
                continue
            default = plan.default
            bare = t.strip(" &").removeprefix("const ")
            if default is None:
                required = count + 1
            if default is not None:
                # A temporary keeps reference defaults alive through the native call.
                lines.append(f"{bare} default{i} = {default};")
                default_js = (
                    f"e.write(default{i})"
                    if default not in ("nullptr", "NULL")
                    else "JS_NULL"
                )
                supplied = f"(n>{count} && !JS_IsUndefined(a[{count}]))"
                lines.append(
                    f"Script::JSValueOwner fallback{i}(e.ctx, {supplied}?JS_UNDEFINED:{default_js});"
                )
                value = f"{supplied}?a[{count}]:fallback{i}.get()"
            else:
                value = f"a[{count}]"
            lines.append(f"Argument<{t}> arg{i}(e,{value});")
            record_type = t.replace("const ", "").strip(" &*")
            if record_type == "Fertility::Field":
                lines.append(
                    f"if(arg{i}.get()) prepareFertilityRead(*arg{i}.get());"
                    if t.endswith("*")
                    else f"prepareFertilityRead(arg{i}.get());"
                )
            if record_type == "GeneratorControl":
                lines.append(f"prepareGeneratorControl(e,arg{i}.get());")
            if p.type.get_canonical().kind in (
                c.TypeKind.INT,
                c.TypeKind.UINT,
                c.TypeKind.LONG,
                c.TypeKind.ULONG,
                c.TypeKind.LONGLONG,
                c.TypeKind.ULONGLONG,
                c.TypeKind.FLOAT,
                c.TypeKind.DOUBLE,
                c.TypeKind.SHORT,
                c.TypeKind.USHORT,
            ):
                lines.append(
                    (f"if(n>{count}) " if default is not None else "")
                    + f'e.preflight("{p.spelling}",arg{i}.get());'
                )
            if p.spelling == "spacing" and v.spelling in (
                "spreadPoints",
                "nearestSiteLabels",
                "relaxPoints",
            ):
                lines.append(
                    f'if(arg{i}.get()<=0)throw TypeMismatch("Site spacing must be positive");'
                )
            allargs.append(f"arg{i}.get()")
            count += 1
        expr = (call or qualified(v)) + "(" + ", ".join(allargs) + ")"
        result = ret or typ(v.result_type)
        outputs = [(i, p) for i, p in enumerate(params) if is_output(p)]
        if qualified(v.semantic_parent) == "GeneratorControl":
            if v.kind == c.CursorKind.CXX_METHOD and not v.is_static_method():
                lines.append(
                    "prepareGeneratorControl(e,e.native<GeneratorControl>(self));"
                )
                if v.spelling == "displayValue":
                    lines.append(
                        "prepareGeneratorDisplay(e.native<GeneratorControl>(self),arg0.get());"
                    )
        lines.extend(fertility_preconditions(v))
        if constructor:
            lines.append(f"e.allocate(256+sizeof({constructor}));")
            lines.append("auto pointer=" + expr + ";")
            lines.append(f"e.allocate(e.footprint(*pointer)-sizeof({constructor}));")
            lease = CONSTRUCTOR_LEASES.get(constructor)
            lines.append(
                "return "
                + (
                    f"e.dependentHandle(pointer.get(),pointer,{lease})"
                    if lease
                    else "e.handle(pointer.get(),pointer)"
                )
                + ";"
            )
        elif outputs:
            lines.append(
                (expr + ";" if result == "void" else "auto nativeResult=" + expr + ";")
            )
            lines.append(
                "Script::JSValueOwner result(e.ctx,JS_NewObject(e.ctx));e.check(result.get());"
            )
            if result != "void":
                lines.append(
                    'if(JS_SetPropertyStr(e.ctx,result.get(),"result",e.write(nativeResult))<0)e.fail();'
                )
            for i, p in outputs:
                value = (
                    f"arg{i}.get()?e.write(*arg{i}.get()):JS_NULL"
                    if typ(p.type).endswith("*")
                    else f"e.write(arg{i}.get())"
                )
                lines.append(
                    f'if(JS_SetPropertyStr(e.ctx,result.get(),"{p.spelling}",{value})<0)e.fail();'
                )
            lines.append("return result.release();")
        elif qualified(v) == "MapGeneration::cellGraph" and len(params) == 1:
            lines.append(
                "e.allocate(256+e.footprint(arg0.get())); auto tiling=std::make_shared<MapGeneration::Tessellation>(arg0.get()); auto graph=MapGeneration::cellGraph(*tiling); graph.distance2=[tiling](int a,int b){return tiling->distance2(a,b);}; return e.write(std::move(graph));"
            )
        elif qualified(v) == "MapGeneration::cellGraph" and len(params) == 3:
            lines.append(
                "return e.write(ownedSitesCellGraph(e,arg0.get(),arg1.get(),arg2.get()));"
            )
        elif result == "void":
            lines.append(expr + "; return JS_UNDEFINED;")
        elif result.endswith("&"):
            lines.append(
                "auto &result="
                + expr
                + "; auto b=e.box(self); return e.handle(&result,b?b->owner:std::shared_ptr<void>{},b?b->readonly:false,b?b->alive:std::shared_ptr<bool>{});"
            )
        elif v.spelling == "fairnessModelTerms":
            lines.append(
                "return e.write(std::vector<FairnessModelTerm>(fairnessModelTerms(),fairnessModelTerms()+FAIRNESS_MODEL_FEATURE_COUNT));"
            )
        else:
            lines.append("return e.write(" + expr + ");")
        lines.insert(
            0,
            f'if(n<{required} || n>{count}) throw TypeMismatch("Wrong argument count");',
        )
        return (
            "[](Binding &e, JSValueConst self, int n, JSValueConst *a)->JSValue {\n"
            + "\n".join("  " + line for line in lines)
            + "\n}",
            None,
        )

    for r in records:
        q = qualified(r)
        family = group(r)
        groups.setdefault(family, [])
        if r.spelling in ("ListComparator", "PerlinNoiseTest"):
            continue
        fields = [
            v
            for v in r.get_children()
            if v.kind == c.CursorKind.FIELD_DECL
            and v.access_specifier
            in (c.AccessSpecifier.PUBLIC, c.AccessSpecifier.INVALID)
        ]
        methods = [
            v
            for v in r.get_children()
            if v.kind == c.CursorKind.CXX_METHOD
            and v.access_specifier == c.AccessSpecifier.PUBLIC
            and (
                not v.spelling.startswith("operator")
                or v.spelling in ("operator()", "operator==")
            )
            and not v.is_static_method()
        ]
        constructors = [
            v
            for v in r.get_children()
            if v.kind == c.CursorKind.CONSTRUCTOR
            and v.access_specifier == c.AccessSpecifier.PUBLIC
            and not v.is_copy_constructor()
            and not v.is_move_constructor()
            and not v.is_deleted_method()
        ]
        public_fields = [
            f
            for f in fields
            if f.type.kind not in (c.TypeKind.LVALUEREFERENCE, c.TypeKind.POINTER)
            or f.type.spelling == "const char *"
        ]
        record_fields[q] = public_fields
        record_methods[q] = []
        record_constructors[q] = []
        record_static_methods[q] = []
        declarations.append(
            f"template<> struct Record<{q}> {{ static {q} read(Binding &,JSValueConst); static void attach(Binding &,JSValueConst); static std::uint64_t footprint(Binding &,const {q}&); }};"
        )
        first_definition = len(definitions)
        reads = " ".join(
            f'e.readField(value,v,"{f.spelling}",&{q}::{f.spelling});'
            for f in public_fields
        )
        if r.spelling in ("Blob", "WedgeField", "MapGeneratorPoint"):
            inputs = " ".join(
                f'Script::JSValueOwner f{i}(e.ctx,JS_GetPropertyStr(e.ctx,v,"{f.spelling}"));e.check(f{i}.get());'
                for i, f in enumerate(public_fields)
            )
            values = ", ".join(
                f"e.read<{typ(f.type)}>(f{i}.get())"
                for i, f in enumerate(public_fields)
            )
            definitions.append(
                f"inline {q} Record<{q}>::read(Binding &e,JSValueConst v) {{ {inputs} return {q}{{{values}}}; }}"
            )
        else:
            definitions.append(
                f"inline {q} Record<{q}>::read(Binding &e,JSValueConst v) {{ return e.record<{q}>(v,[](Binding &e,{q} &value,JSValueConst v){{ {reads} }}); }}"
            )
        payload = (
            " + ".join(
                f"(e.footprint(v.{f.spelling})-sizeof(v.{f.spelling}))"
                for f in public_fields
            )
            or "0"
        )
        if q == "Fertility::Field":
            payload += " + (e.footprint(v.values())-sizeof(v.values()))"
        definitions.append(
            f"inline std::uint64_t Record<{q}>::footprint(Binding &e,const {q}&v) {{ return sizeof(v)+{payload}; }}"
        )
        attach = " ".join(
            f'e.field(v,"{f.spelling}",&{q}::{f.spelling});' for f in public_fields
        )
        for m in methods:
            if q == "GeneratorControl" and m.spelling == "set":
                coverage.append(
                    {
                        "header": family,
                        "symbol": qualified(m),
                        "equivalent": "Request controls are frozen; use manifest defaults or construct a derived script design",
                    }
                )
                continue
            if m.spelling in ("begin", "end"):
                coverage.append(
                    {
                        "header": family,
                        "symbol": qualified(m),
                        "equivalent": "Objective.terms() returns the same ordered terms",
                    }
                )
                continue
            body, error = wrapper(
                m,
                call=f"e.native<{q}>(self,{str(not m.is_const_method()).lower()}).{m.spelling}",
            )
            if error:
                coverage.append(
                    {
                        "header": family,
                        "symbol": qualified(m),
                        "signature": m.type.spelling,
                        "equivalent": error,
                    }
                )
                continue
            method_name = {"operator()": "at", "operator==": "equals"}.get(
                m.spelling, m.spelling
            )
            record_methods[q].append((method_name, m))
            coverage.append(
                {
                    "header": family,
                    "symbol": qualified(m),
                    "signature": m.type.spelling,
                    "kind": "method",
                    "binding": r.spelling + "." + method_name,
                }
            )
            attach += f' e.overload(v,"{method_name}",[&e](JSValueConst self,int n,JSValueConst*a) {{ return ({body})(e,self,n,a); }});'
        if r.spelling == "Objective":
            attach += ' e.add(v,"terms",[&e](JSValueConst self,int n,JSValueConst*){if(n)throw TypeMismatch("terms()");const auto&o=e.native<MapGeneration::Objective>(self);return e.write(std::vector<MapGeneration::Objective::Term>(o.begin(),o.end()));});'
        definitions.append(
            f"inline void Record<{q}>::attach(Binding &e,JSValueConst v) {{ {attach} }}"
        )
        # Constructors: generate custom wrapper (normal return owning handle supports immovable objects).
        for ctor in constructors:
            body, error = wrapper(ctor, call=f"std::make_shared<{q}>", constructor=q)
            if not error:
                record_constructors[q].append(ctor)
                coverage.append(
                    {
                        "header": family,
                        "symbol": qualified(ctor),
                        "signature": ctor.type.spelling,
                        "kind": "constructor",
                        "binding": r.spelling,
                    }
                )
                groups.setdefault(family, []).append((r.spelling, body))
        if not constructors and q != "GeneratorControl":
            # Aggregate records can be created from JavaScript configuration objects.
            body = f'[](Binding &e,JSValueConst,int n,JSValueConst*a)->JSValue {{ if(n>1)throw TypeMismatch("Expected optional record"); return e.write(n?e.read<{q}>(a[0]):e.defaultRecord<{q}>()); }}'
            groups.setdefault(family, []).append((r.spelling, body))
        record_groups.setdefault(family, []).extend(definitions[first_definition:])
        coverage.append(
            {"header": family, "symbol": q, "kind": "record", "binding": r.spelling}
        )
        for m in r.get_children():
            if q == "GeneratorControl":
                continue
            if (
                m.kind == c.CursorKind.CXX_METHOD
                and m.access_specifier == c.AccessSpecifier.PUBLIC
                and m.is_static_method()
            ):
                body, error = wrapper(m)
                if not error:
                    record_static_methods[q].append(m)
                    coverage.append(
                        {
                            "header": family,
                            "symbol": qualified(m),
                            "signature": m.type.spelling,
                            "kind": "static-method",
                            "binding": r.spelling + "_" + m.spelling,
                        }
                    )
                    groups.setdefault(family, []).append(
                        (r.spelling + "_" + m.spelling, body)
                    )
    for v in functions:
        family = group(v)
        item = {"header": family, "symbol": qualified(v), "signature": v.type.spelling}
        if v.spelling in EQUIVALENTS:
            item["equivalent"] = EQUIVALENTS[v.spelling]
        else:
            body, error = wrapper(v)
            if error:
                item["equivalent"] = error
            else:
                item["binding"] = v.spelling
                groups.setdefault(family, []).append((v.spelling, body))
        coverage.append(item)
    preamble = (
        '// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated by tools/map-generators/generate_js_bindings.py.\n#pragma once\n#include "ToolkitBinding.h"\n#include "ToolkitFertilityContracts.h"\n#include "ToolkitControlContracts.h"\n#include "ToolkitGraphContracts.h"\n'
        + includes
        + "\nnamespace MapGeneration::JavaScript {\nusing namespace ::MapGeneration;\n"
    )
    emit(out / "ToolkitRecords.h", preamble + "\n".join(declarations) + "\n}\n")
    names = sorted(groups)
    for batch in range(8):
        families = names[batch::8]
        record_definitions = "\n".join(
            d.replace("inline ", "", 1)
            for family in families
            for d in record_groups.get(family, [])
        )
        code = (
            '// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated by tools/map-generators/generate_js_bindings.py.\n#include "ToolkitRecords.h"\nnamespace MapGeneration::JavaScript {\n'
            + record_definitions
            + "\nvoid registerToolkit"
            + str(batch)
            + "(Binding &e,JSValueConst root) {\n"
        )
        for family in families:
            code += " { Script::JSValueOwner objectOwner(e.ctx,JS_NewObject(e.ctx));auto object=objectOwner.get();e.check(object);\n"
            for name, body in groups[family]:
                code += f'e.overload(object,"{name}",[&e](JSValueConst self,int n,JSValueConst*a){{return ({body})(e,self,n,a);}});\n'
            for constant in constants:
                if group(constant) == family and constant.type.get_canonical().kind in (
                    c.TypeKind.INT,
                    c.TypeKind.UINT,
                    c.TypeKind.DOUBLE,
                ):
                    coverage.append(
                        {
                            "header": family,
                            "symbol": qualified(constant),
                            "kind": "constant",
                            "binding": constant.spelling,
                        }
                    )
                    code += f'if(JS_SetPropertyStr(e.ctx,object,"{constant.spelling}",e.write({qualified(constant)}))<0)e.fail();\n'
            for enum in enums:
                if group(enum) == family:
                    q = qualified(enum)
                    code += " { Script::JSValueOwner valuesOwner(e.ctx,JS_NewObject(e.ctx));auto values=valuesOwner.get();e.check(values);\n"
                    for entry in enum.get_children():
                        if entry.kind == c.CursorKind.ENUM_CONSTANT_DECL:
                            code += f'if(JS_SetPropertyStr(e.ctx,values,"{entry.spelling}",e.write({q}::{entry.spelling}))<0)e.fail();\n'
                    code += f'if(JS_SetPropertyStr(e.ctx,object,"{enum.spelling}",valuesOwner.release())<0)e.fail(); }}\n'
            code += f'if(JS_SetPropertyStr(e.ctx,root,"{family}",objectOwner.release())<0)e.fail(); }}\n'
        code += "}\n}\n"
        emit(out / f"ToolkitBindings{batch}.cpp", code)
    emit(out / "ToolkitCoverage.json", json.dumps(coverage, indent=2) + "\n")

    # Authoring types are generated from the same parsed declarations as the adapters.
    record_names = {qualified(r): group(r) + "." + r.spelling for r in records}
    enum_names = {qualified(v) for v in enums}
    for record in records:
        for field in record_fields.get(qualified(record), []):
            enum_names.update(referenced_enums(field.type))
    for function in functions:
        enum_names.update(referenced_enums(function.type))

    def split_types(value):
        depth = 0
        parts = []
        start = 0
        for i, ch in enumerate(value):
            if ch in "<([":
                depth += 1
            elif ch in ">)]":
                depth -= 1
            elif ch == "," and not depth:
                parts.append(value[start:i].strip())
                start = i + 1
        parts.append(value[start:].strip())
        return parts

    # Only mutable vectors require exact native scalar storage. Const/value
    # vector arguments intentionally accept differing handles and convert elements.
    scalar_storage = {
        "unsigned char": "u8",
        "signed char": "i8",
        "char": "char",
        "unsigned short": "u16",
        "short": "i16",
        "unsigned int": "u32",
        "int": "i32",
        "unsigned long": "ulong",
        "long": "long",
        "unsigned long long": "u64",
        "long long": "i64",
        "float": "f32",
        "double": "f64",
        "bool": "bool",
    }

    def ts_type(value, input=False, mutable=False):
        readonly_reference = value.startswith("const ") and value.endswith("&")
        value = value.strip().removeprefix("const ").rstrip(" &")
        if re.search(r"\[\d+\]$", value):
            return (
                "Buffer<"
                + ts_type(value[: value.rfind("[")].strip())
                + ">"
                + (
                    " | " + ts_type(value[: value.rfind("[")].strip()) + "[]"
                    if input
                    else ""
                )
            )
        if value == "void":
            return "void"
        if value == "bool":
            return "boolean"
        if value in ("std::string", "std::string_view", "char *", "const char *"):
            return "string"
        if value == "std::mt19937":
            return "RandomStream"
        if value in record_names:
            return (
                "Partial<" + record_names[value] + ">" if input else record_names[value]
            )
        simple = [q for q in record_names if q.split("::")[-1] == value]
        if len(simple) == 1:
            return (
                "Partial<" + record_names[simple[0]] + ">"
                if input
                else record_names[simple[0]]
            )
        if value in enum_names:
            return "number"
        simple_enums = [q for q in enum_names if q.split("::")[-1] == value]
        if len(simple_enums) == 1:
            return "number"
        if value.startswith("std::map<"):
            key, item, *_ = split_types(value[9:-1])
            return "NativeMap<" + ts_type(key) + ", " + ts_type(item) + ">"
        if value.startswith("std::vector<"):
            native_element = split_types(value[12:-1])[0]
            element = ts_type(native_element)
            buffer = "Buffer" if mutable or not readonly_reference else "ReadonlyBuffer"
            storage = scalar_storage.get(native_element)
            tag = ', "' + storage + '"' if storage and (mutable or not input) else ""
            return (
                buffer
                + "<"
                + element
                + tag
                + ">"
                + (" | " + element + "[]" if input and not mutable else "")
            )
        if value.startswith("std::array<"):
            return (
                "Buffer<"
                + ts_type(split_types(value[11:-1])[0])
                + ">"
                + (
                    " | " + ts_type(split_types(value[11:-1])[0]) + "[]"
                    if input
                    else ""
                )
            )
        if value.startswith("std::optional<"):
            return ts_type(value[14:-1]) + " | null"
        if value.startswith("std::pair<"):
            return "[" + ", ".join(ts_type(v) for v in split_types(value[10:-1])) + "]"
        if value.startswith("std::function<"):
            signature = value[14:-1]
            at = signature.find("(")
            return (
                "(("
                + ", ".join(
                    "arg" + str(i) + ": " + ts_type(v)
                    for i, v in enumerate(split_types(signature[at + 1 : -1]))
                    if v
                )
                + ") => "
                + ts_type(signature[:at])
                + ") | null"
            )
        if value.endswith("*"):
            return ts_type(value[:-1]) + " | null"
        if any(
            v in value
            for v in ("int", "long", "short", "float", "double", "size_t", "char")
        ):
            return "number"
        return "unknown"

    def ts_result(v):
        plans, error = parameters(v)
        if error:
            raise ValueError(error)
        outputs = [plan.cursor for plan in plans if is_output(plan.cursor)]
        if not outputs:
            return ts_type(typ(v.result_type))
        fields = (
            ["result: " + ts_type(typ(v.result_type))]
            if typ(v.result_type) != "void"
            else []
        ) + [p.spelling + ": " + ts_type(typ(p.type)) for p in outputs]
        return "{ " + "; ".join(fields) + " }"

    def ts_parameters(v):
        plans, error = parameters(v)
        if error:
            raise ValueError(error)
        return ", ".join(
            (plan.cursor.spelling or "arg" + str(i))
            + ("?" if plan.default is not None else "")
            + ": "
            + ts_type(plan.cpp_type, True, plan.mutable_vector)
            for i, plan in enumerate(plans)
            if not plan.implicit
        )

    types = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "// Generated by tools/map-generators/generate_js_bindings.py.",
        "declare const bufferStorage: unique symbol;",
        "export interface ReadonlyBuffer<T,Storage=unknown> { readonly [bufferStorage]?: Storage; readonly length: number; get(index: number): T; clone(): Buffer<T,Storage>; toArray(): T[]; }",
        "export interface Buffer<T,Storage=unknown> extends ReadonlyBuffer<T,Storage> { set(index: number, value: T): void; fill(value: T): void; }",
        'export type ByteBuffer = Buffer<number,"u8">; export type IntegerBuffer = Buffer<number,"i32">; export type FieldBuffer = Buffer<number,"f64">;',
        "export interface NativeMap<K,V> { get(key: K): V | undefined; keys(): Buffer<K>; }",
        "export interface RandomStream { next(): number; }",
    ]
    for family in names:
        types.append("export namespace " + family + " {")
        for r in records:
            if group(r) != family or r.spelling == "ListComparator":
                continue
            types.append("export interface " + r.spelling + " {")
            q = qualified(r)
            for f in record_fields.get(q, []):
                types.append(f.spelling + ": " + ts_type(typ(f.type)) + ";")
            for name, m in record_methods.get(q, []):
                types.append(name + "(" + ts_parameters(m) + "): " + ts_result(m) + ";")
            if q == "MapGeneration::Objective":
                types.append("terms(): Buffer<Solve.Term>;")
            types.append("}")
            ctors = record_constructors.get(q, [])
            for ctor in ctors:
                types.append(
                    "export function "
                    + r.spelling
                    + "("
                    + ts_parameters(ctor)
                    + "): "
                    + r.spelling
                    + ";"
                )
            if not ctors and q != "GeneratorControl":
                types.append(
                    "export function "
                    + r.spelling
                    + "(value?: Partial<"
                    + r.spelling
                    + ">): "
                    + r.spelling
                    + ";"
                )
            for m in record_static_methods.get(q, []):
                types.append(
                    "export function "
                    + r.spelling
                    + "_"
                    + m.spelling
                    + "("
                    + ts_parameters(m)
                    + "): "
                    + ts_result(m)
                    + ";"
                )
        for f in functions:
            if group(f) != family or f.spelling in EQUIVALENTS:
                continue
            body, error = wrapper(f)
            if not error:
                types.append(
                    "export function "
                    + f.spelling
                    + "("
                    + ts_parameters(f)
                    + "): "
                    + (
                        "Buffer<FairnessModel.FairnessModelTerm>"
                        if f.spelling == "fairnessModelTerms"
                        else ts_result(f)
                    )
                    + ";"
                )
        for constant in constants:
            if group(constant) == family and constant.type.get_canonical().kind in (
                c.TypeKind.INT,
                c.TypeKind.UINT,
                c.TypeKind.DOUBLE,
            ):
                types.append("export const " + constant.spelling + ": number;")
        for enum in enums:
            if group(enum) == family:
                types.append(
                    "export const "
                    + enum.spelling
                    + ": { "
                    + "; ".join(
                        "readonly " + v.spelling + ": number"
                        for v in enum.get_children()
                        if v.kind == c.CursorKind.ENUM_CONSTANT_DECL
                    )
                    + " };"
                )
        types.append("}")
    types += [
        "export namespace Blueprints {",
        "export interface FortDesign { layout: number; x(u: number,v: number): number; y(u: number,v: number): number; }",
        "export interface FortPlan { settleU: number; settleV: number; orchardU: number; orchardV: number; orchardDu: number; orchardDv: number; }",
        "export interface FortLayout { t: Grid.Torus; terrain: ByteBuffer; homeOf: IntegerBuffer; plot: IntegerBuffer; uplands: IntegerBuffer; wall: ByteBuffer; roads: ByteBuffer; buffer: ByteBuffer; towns: ByteBuffer; homes: Buffer<Geometry.ShapePoint>; villages: Buffer<Geometry.ShapePoint>; design: FortDesign; failure: string; }",
        "export interface SolvedLayout { t: Grid.Torus; lat: { w: number; h: number; tiles: number }; terrain: ByteBuffer; kind: ByteBuffer; pinned: ByteBuffer; cellOf: IntegerBuffer; homeOf: IntegerBuffer; home: IntegerBuffer; failure: string; }",
        "export function fortsDesign(): FortLayout; export function fortsPlan(layout: number,radius: number): FortPlan; export function validateForts(): string;",
        "export function evenGroundDesign(): SolvedLayout; export function evenGroundRequestFailure(): string;",
        "}",
    ]
    types += (
        ["export interface Toolkit {", "Blueprints: typeof Blueprints;"]
        + [family + ": typeof " + family + ";" for family in names]
        + [
            "}",
            "export interface GeneratorContext { readonly request: Readonly<{ width: number; height: number; teams: number; workers: number; seed: number; options: Readonly<Record<string,number>> }>; readonly torus: Grid.Torus; readonly toolkit: Toolkit; stream(name: string): RandomStream; bounded(name: string, bound: number): number; mask(length?: number, value?: number): ByteBuffer; integers(length?: number, value?: number): IntegerBuffer; field(length?: number, value?: number): FieldBuffer; stage(name: string): void; measure(key: string, value: number, subject?: number): void; choice(key: string, value: string, subject?: number): void; fallback(key: string, value: string, subject?: number): void; buildingType(name?: string, level?: number, construction?: boolean): unknown; addTeams(): void; shuffle<T>(values: T[], stream: string): void; cachedDesign<T>(key: string, builder: () => T): T; resolveDesignChoice(stream: string, values: number[], feasible: (value: number) => boolean): number; resourceType(key: string): number; setResource(tile: number, type: number, brushSize: number): void; buildable(tile: number): boolean; }",
            "export type GeneratorCallback = (context: GeneratorContext) => void | string;",
        ]
    )
    emit(ROOT / "data/generators/toolkit.d.ts", "\n".join(types) + "\n")
    print(
        f"{len(records)} records, {len(functions)} functions, {sum(len(x) for x in groups.values())} adapters"
    )
    print(
        "Equivalents:",
        [(v["symbol"], v.get("equivalent")) for v in coverage if "equivalent" in v],
    )
    if stale:
        sys.exit("Generated toolkit files are stale:\n" + "\n".join(stale))


if __name__ == "__main__":
    main()
