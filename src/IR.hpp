//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

// IR — internal representation of declarations the codegen tool will bind.
//
// The Visitor (Visitor.hpp) populates this IR from the Clang AST; the
// Emitter (Phase 3) consumes it and produces pybind11 C++. Keeping the IR
// independent of clang::* types means the emitter doesn't need to drag in
// libtooling headers, and unit tests against the IR can construct fixtures
// directly without spinning up a ClangTool.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apiary {

/// @brief Source position (file, line, column) of a captured declaration.
struct SourceLocation {
    /// Source file path.
    std::string file;
    /// 1-based line number.
    unsigned    line   = 0;
    /// 1-based column number.
    unsigned    column = 0;
};

/// @brief One parsed annotation directive.
///
/// The AnnotationParser splits the raw
/// "apiary:<name>[:<arg>[:<arg>...]]" payload into this form so the
/// emitter can switch on `name` cleanly.
struct Directive {
    /// Directive name (the ``<name>`` token).
    std::string              name;
    /// Directive arguments (the ``<arg>`` tokens).
    std::vector<std::string> args;
};

/// @brief A list of parsed annotation directives.
using DirectiveList = std::vector<Directive>;

/// @brief Common metadata every bound declaration carries.
///
/// Inherited by the concrete bound-* structs below so the emitter can write
/// generic code against this base when convenient.
struct BoundEntityCommon {
    /// Unqualified name.
    std::string    name;
    /// Fully qualified name, e.g. ``::ns::Class::method``.
    std::string    qualified_name;
    /// Annotation directives attached to this entity.
    DirectiveList  directives;
    /// Raw doxygen text, or empty.
    std::string    doc;
    /// Source location of the declaration.
    SourceLocation location;
    /// Resolved Python submodule path (from a per-entity or inherited
    /// APIARY_MODULE directive on an enclosing namespace). Empty
    /// when the entity belongs to the top-level module. The .pyi
    /// emitter uses this to decide which `<module>.pyi` file an
    /// entity belongs to (e.g. einsums.linalg, einsums.graph).
    std::optional<std::string> submodule;
};

/// @brief A single function parameter.
///
/// Default-value text is captured verbatim from the AST so the emitter can
/// re-emit it as a `py::arg("x") = ...`.
///
/// @note `py_type` and `default_value_py` carry the Python-stub forms — a
/// best-effort translation populated alongside the C++ form. The .pyi
/// emitter consumes these; the C++ emitter ignores them.
struct BoundParam {
    /// Parameter name.
    std::string name;
    /// C++ type as written.
    std::string type;
    /// Canonical (typedef-expanded) C++ type, populated alongside
    /// ``type``. Same purpose as ``BoundFunction::return_type_canonical``
    /// — lets the .pyi emitter substitute and resolve through typedef
    /// aliases like ``RuntimeTensor<T>`` ↔ ``GeneralRuntimeTensor<T,
    /// std::allocator<T>>``.
    std::string                type_canonical;
    /// Python-stub form of the type.
    std::string                py_type;
    /// Default-value text as written, or empty.
    std::optional<std::string> default_value;
    /// Python-stub form of the default value.
    std::optional<std::string> default_value_py;
};

/// @brief A bound public data member (field) of a class.
struct BoundField : BoundEntityCommon {
    /// C++ type of the field.
    std::string type;
    /// Python-stub form of the type.
    std::string py_type;
    /// True for static data members.
    bool        is_static = false;
};

/// @brief A bound class method, constructor, or destructor.
struct BoundMethod : BoundEntityCommon {
    /// C++ return type as written.
    std::string return_type;
    /// Canonical (typedef-expanded) form of the return type. Same role
    /// as ``BoundFunction::return_type_canonical``: the .pyi emitter
    /// substitutes member-template bindings on this and re-resolves so
    /// per-directive overloads emit concrete return types.
    std::string              return_type_canonical;
    /// Python-stub form of the return type.
    std::string              return_py_type;
    /// Parameters of the method.
    std::vector<BoundParam>  params;
    /// True for const-qualified methods.
    bool                     is_const    = false;
    /// True for static methods.
    bool                     is_static   = false;
    /// True for virtual methods.
    bool                     is_virtual  = false;
    /// True for member function templates.
    bool                     is_template = false;
    /// Its template parameters (docs mode).
    std::vector<std::string> template_param_names;
    /// True for pure-virtual methods.
    bool                     is_pure_virtual = false;
    /// True if this method is a constructor.
    bool                     is_constructor  = false;
    /// True if this method is a destructor.
    bool                     is_destructor   = false;
    /// True if this method is an operator overload.
    bool                     is_operator     = false;
    /// True if this method is deleted (`= delete`).
    bool                     is_deleted      = false;

    /// @brief Whether the last parameter is a variadic pack (set by APIARY_VARIADIC_FROM).
    ///
    /// Set by APIARY_VARIADIC_FROM: the last parameter is a pack
    /// expansion whose arity comes from the named template parameter, and
    /// each expanded slot has type ``variadic_element_type``. When emitting
    /// a per-instantiation binding, the pack slot is replaced with N copies
    /// of ``(variadic_element_type, dim_<i>)``.
    bool        has_variadic_pack = false;
    /// Name of the template parameter that supplies the pack arity.
    std::string variadic_from_param;
    /// Element type of each expanded pack slot.
    std::string variadic_element_type;
};

/// @brief A single enumerator within a bound enum.
struct BoundEnumerator {
    /// Enumerator name.
    std::string  name;
    /// Enumerator integer value.
    std::int64_t value = 0;
    /// Raw doxygen text, or empty.
    std::string  doc;
};

/// @brief A bound enumeration.
struct BoundEnum : BoundEntityCommon {
    /// True for scoped enums (`enum class`).
    bool                         is_scoped = false;
    /// C++ underlying type as written.
    std::string                  underlying_type;
    /// Python-stub form of the underlying type.
    std::string                  underlying_py_type;
    /// The enum's enumerators.
    std::vector<BoundEnumerator> enumerators;
};

/// @brief One concrete template instantiation requested by an @instantiate or
/// @instantiate_as directive.
///
/// The emitter produces both an explicit
/// ``template class Foo<args>;`` declaration and a per-instantiation
/// ``py::class_<Foo<args>>(m, py_name)`` binding block from this.
struct BoundInstantiation {
    /// Python identifier (sanitized or user-supplied).
    std::string py_name;
    /// Type arguments ready to paste between < and >, e.g. "float, 2".
    std::string type_args;
};

/// @brief One logical Python-facing entry on a free function or method.
///
/// Computed by a post-IR pass (PythonOverloads.hpp) that groups raw
/// BoundInstantiation entries according to the merge rules pybind11
/// expects:
///
///   * NonTemplate          — function has no templates; one m.def, one stub
///   * SingleInstantiation  — one instantiation with this py_name; one m.def
///   * DtypeDispatcher      — N instantiations sharing arg signature, only
///                            return type varies; collapse into one m.def
///                            taking ``dtype="..."`` kwarg
///   * TemplateKwargsDispatcher
///                          — 2^N instantiations from APIARY_INSTANTIATE_BOOLS
///                            collapse into one m.def with N bool kwargs
///   * OverloadSet          — multiple instantiations with the same py_name
///                            that DON'T merge into a dispatcher; pybind11
///                            picks at runtime via overload resolution
///
/// @note Both the C++ emitter and the .pyi emitter consume this view so the
/// merge rules live in one place.
struct PythonOverload {
    /// @brief How the instantiations merge into a Python-facing entry.
    enum class Kind : std::uint8_t {
        /// Function has no templates; one m.def, one stub.
        NonTemplate,
        /// One instantiation with this py_name; one m.def.
        SingleInstantiation,
        /// N instantiations sharing arg signature, only return type varies; one m.def with ``dtype`` kwarg.
        DtypeDispatcher,
        /// 2^N instantiations from APIARY_INSTANTIATE_BOOLS; one m.def with N bool kwargs.
        TemplateKwargsDispatcher,
        /// Multiple instantiations with the same py_name resolved at runtime by pybind11.
        OverloadSet,
    };

    /// Which merge rule applies to this entry.
    Kind        kind = Kind::NonTemplate;
    /// Python name of this entry.
    std::string py_name;

    /// @brief Indices into the owning entity's `instantiations` vector for this entry.
    ///
    /// Empty for NonTemplate (which uses the function itself directly).
    std::vector<std::size_t> instantiation_indices;

    /// @brief For DtypeDispatcher: the C++ scalar type each instantiation contributes.
    ///
    /// Parallel to instantiation_indices.
    std::vector<std::string> dtype_values;
    /// For DtypeDispatcher: the picked default dtype string (e.g. "float64").
    std::string              default_dtype;

    /// For TemplateKwargsDispatcher: parallel to f.template_kwargs.
    std::vector<std::string> kwarg_names;
};

/// @brief A property aggregated from @getter / @setter directives on class methods.
///
/// Computed by a post-IR pass (Properties.hpp) that walks
/// BoundClass.methods. The .pyi emitter consumes this directly; the
/// pybind11 C++ emitter still derives the same merge inline (kept
/// separate to avoid churning the working emit code).
struct BoundProperty {
    /// Python attribute name from @getter("name").
    std::string py_name;
    /// Getter's C++ return type, ref/cv stripped.
    std::string type;
    /// Python form (translated from type).
    std::string py_type;
    /// Doxygen text from getter.
    std::string doc;
    /// True when a matching @setter exists.
    bool        has_setter = false;
    /// @brief Index into BoundClass.methods for the getter.
    std::size_t getter_index = 0;
    /// @brief Index into BoundClass.methods for the setter.
    /// @note Valid only when has_setter is true.
    std::size_t setter_index = 0;
};

/// @brief A bound class or struct, with its members and nested entities.
struct BoundClass : BoundEntityCommon {
    /// True for class templates.
    bool is_template = false;
    /// True for annotated classes seen in headers OUTSIDE the current
    /// module's source filter. Captured purely for cross-module name
    /// resolution in the .pyi emitter — the C++ emitter ignores them
    /// (their bindings live in the owning module's TU).
    bool                            is_external = false;
    /// Template parameter names, e.g. ["T", "rank"] for ``template <typename T, size_t rank>``.
    std::vector<std::string>        template_param_names;
    /// Base class names.
    std::vector<std::string>        bases;
    /// Bound constructors.
    std::vector<BoundMethod>        ctors;
    /// Bound methods.
    std::vector<BoundMethod>        methods;
    /// Bound public fields.
    std::vector<BoundField>         fields;
    /// Nested enums.
    std::vector<BoundEnum>          nested_enums;
    /// Nested classes.
    std::vector<BoundClass>         nested_classes;
    /// Requested template instantiations.
    std::vector<BoundInstantiation> instantiations;
    /// @getter/@setter pairs collapsed into properties. Computed by the
    /// post-IR pass in Properties.hpp; empty until the pass has run.
    std::vector<BoundProperty> properties;
};

/// @brief A bound free function.
struct BoundFunction : BoundEntityCommon {
    /// C++ return type as written.
    std::string return_type;
    /// Canonical (typedef-expanded) form of the return type. For
    /// per-instantiation .pyi emission we substitute and look up the
    /// canonical form so a function returning the typedef alias
    /// ``RuntimeTensor<T>`` resolves to the same Python class as the
    /// underlying ``GeneralRuntimeTensor<T, std::allocator<T>>``.
    std::string             return_type_canonical;
    /// Python-stub form of the return type.
    std::string             return_py_type;
    /// Parameters of the function.
    std::vector<BoundParam> params;
    /// True for function templates.
    bool                    is_template = false;
    /// Names of the function template's template parameters (e.g.
    /// ``["AType", "BType", "CType"]`` for
    /// ``template <BasicTensorConcept AType, …>``). Used by the emitter
    /// to substitute concrete types from per-instantiation type_args
    /// into return/parameter types when emitting a static_cast<>
    /// to disambiguate overloads.
    std::vector<std::string> template_param_names;
    /// Python kwarg names for the leading bool template parameters,
    /// from ``APIARY_TEMPLATE_KWARGS``. Empty for functions
    /// without that directive. The emitter generates a runtime
    /// dispatcher with these as keyword-only arguments when non-empty.
    std::vector<std::string> template_kwargs;
    /// One per ``APIARY_INSTANTIATE_AS`` directive on a templated
    /// free function. Empty for non-templated functions and for templated
    /// functions without explicit instantiation directives (those skip
    /// emission with a TODO comment).
    std::vector<BoundInstantiation> instantiations;
    /// Logical Python-facing entries for this function, computed by a
    /// post-IR pass (see PythonOverloads.hpp). The C++ emitter dispatches
    /// on each entry's `kind` to choose dispatcher style; the .pyi
    /// emitter renders one `def` (with `@overload` decorators if needed)
    /// per entry. Empty when the pass hasn't run yet.
    std::vector<PythonOverload> python_overloads;
};

/// @brief A typedef / using-alias.
///
/// Captured in docs mode so the C++ reference can
/// emit a ``cpp:type`` declaration — which makes references to the alias
/// (e.g. ``einsums::blas::int_t`` in a function signature) resolve instead of
/// dangling. `underlying_type` is the aliased type as written.
struct BoundTypedef : BoundEntityCommon {
    /// The aliased type as written.
    std::string              underlying_type;
    /// True for alias templates.
    bool                     is_template = false;
    /// Template parameter names for alias templates.
    std::vector<std::string> template_param_names;
};

/// @brief A C++20 concept.
///
/// Captured in docs mode for ``cpp:concept`` declarations.
struct BoundConcept : BoundEntityCommon {
    /// Template parameter names of the concept.
    std::vector<std::string> template_param_names;
};

/// @brief A documented preprocessor macro.
///
/// Macros are not AST declarations, so these
/// are gathered by a raw-text scan of the module header for a doc comment
/// immediately preceding a ``#define`` (docs mode only). Rendered as
/// ``c:macro`` in the C++ reference.
struct BoundMacro : BoundEntityCommon {
    /// True for function-like macros.
    bool                     is_function_like = false;
    /// Parameter names for function-like macros.
    std::vector<std::string> params;
};

/// @brief The full IR for one bound module.
struct Module {
    /// Bound classes and structs.
    std::vector<BoundClass>    classes;
    /// Bound free functions.
    std::vector<BoundFunction> functions;
    /// Bound enums.
    std::vector<BoundEnum>     enums;
    /// Bound typedefs / aliases (docs mode only).
    std::vector<BoundTypedef>  typedefs;
    /// Bound concepts (docs mode only).
    std::vector<BoundConcept>  concepts;
    /// Bound macros (docs mode only).
    std::vector<BoundMacro>    macros;
};

/// @brief Produce a deterministic textual dump of the IR.
///
/// For golden-output testing and diagnostics. Phase 3's emitter produces the
/// actual pybind11 C++; this function exists so Phase 2 has something testable.
///
/// @param module_ The IR module to dump.
/// @return The textual dump.
std::string dump(Module const &module_);

} // namespace apiary
