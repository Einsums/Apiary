//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "PythonOverloads.hpp"

#include "Diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace apiary {

std::vector<std::string> split_instantiation_args(std::string const &combo) {
    std::vector<std::string> parts;
    std::string              current;
    int                      angle = 0;
    for (char const c : combo) {
        if (c == '<') {
            ++angle;
        } else if (c == '>') {
            if (angle > 0) {
                --angle;
            }
        }
        if (c == ',' && angle == 0) {
            std::size_t b = 0;
            std::size_t e = current.size();
            while (b < e && current[b] == ' ') {
                ++b;
            }
            while (e > b && current[e - 1] == ' ') {
                --e;
            }
            parts.push_back(current.substr(b, e - b));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        std::size_t b = 0;
        std::size_t e = current.size();
        while (b < e && current[b] == ' ') {
            ++b;
        }
        while (e > b && current[e - 1] == ' ') {
            --e;
        }
        parts.push_back(current.substr(b, e - b));
    }
    return parts;
}

std::vector<std::string> dtype_aliases_for(std::string const &cpp_type) {
    std::string t;
    t.reserve(cpp_type.size());
    for (char const c : cpp_type) {
        if (c != ' ') {
            t += c;
        }
    }
    if (t == "float") {
        return {"float32", "float", "f4", "f", "single"};
    }
    if (t == "double") {
        return {"float64", "double", "f8", "d"};
    }
    if (t == "std::complex<float>" || t == "complex<float>") {
        return {"complex64", "c8", "F"};
    }
    if (t == "std::complex<double>" || t == "complex<double>") {
        return {"complex128", "complex", "c16", "D"};
    }
    return {};
}

std::string pick_default_dtype(std::vector<std::string> const &dtype_values_in_order) {
    for (auto const &v : dtype_values_in_order) {
        std::string compact;
        for (char const c : v) {
            if (c != ' ') {
                compact += c;
            }
        }
        if (compact == "double") {
            return "float64";
        }
    }
    if (dtype_values_in_order.empty()) {
        return "";
    }
    auto const aliases = dtype_aliases_for(dtype_values_in_order.front());
    return aliases.empty() ? std::string{} : aliases.front();
}

namespace {

// Whole-token identifier substitution. Mirrors the helper used during
// emission so resolved signatures we compute here match what the
// emitter would produce.
std::string substitute_idents(std::string const &type, std::map<std::string, std::string> const &bindings) {
    if (bindings.empty()) {
        return type;
    }
    auto is_ident_start = [](char c) { return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_'; };
    auto is_ident_cont  = [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; };

    std::string out;
    out.reserve(type.size());
    std::size_t i = 0;
    while (i < type.size()) {
        char const c = type[i];
        if (is_ident_start(c)) {
            std::size_t j = i + 1;
            while (j < type.size() && is_ident_cont(type[j])) {
                ++j;
            }
            std::string const tok = type.substr(i, j - i);
            auto const        it  = bindings.find(tok);
            if (it != bindings.end()) {
                out += it->second;
            } else {
                out += tok;
            }
            i = j;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// Build the resolved C++ argument-type signature ``"(t0,t1,...)"`` for
// one instantiation. Used only to test whether two instantiations of
// the same function share an argument signature (the dtype-dispatcher
// precondition).
std::string resolved_arg_signature(BoundFunction const &f, BoundInstantiation const &inst) {
    std::map<std::string, std::string> bindings;
    auto const                         split = split_instantiation_args(inst.type_args);
    for (std::size_t i = 0; i < f.template_param_names.size() && i < split.size(); ++i) {
        bindings[f.template_param_names[i]] = split[i];
    }
    std::string sig = "(";
    for (std::size_t i = 0; i < f.params.size(); ++i) {
        if (i != 0) {
            sig += ',';
        }
        sig += substitute_idents(f.params[i].type, bindings);
    }
    sig += ')';
    return sig;
}

} // namespace

void compute_python_overloads(BoundFunction &f) {
    f.python_overloads.clear();

    // Non-templated function: a single direct entry, no merging needed.
    if (!f.is_template) {
        PythonOverload entry;
        entry.kind    = PythonOverload::Kind::NonTemplate;
        entry.py_name = f.name;
        f.python_overloads.push_back(std::move(entry));
        return;
    }

    // Templated function with no @instantiate_as / @instantiate_bools:
    // no overloads. The emitter already prints a TODO comment for this
    // case; leaving python_overloads empty mirrors that.
    if (f.instantiations.empty()) {
        return;
    }

    // Group instantiations by py_name, preserving listing order.
    std::vector<std::string>                        py_names_in_order;
    std::map<std::string, std::vector<std::size_t>> by_name;
    for (std::size_t i = 0; i < f.instantiations.size(); ++i) {
        auto const &inst = f.instantiations[i];
        if (by_name.find(inst.py_name) == by_name.end()) {
            py_names_in_order.push_back(inst.py_name);
        }
        by_name[inst.py_name].push_back(i);
    }

    for (auto const &py_name : py_names_in_order) {
        auto const &group = by_name[py_name];

        // Bool fan-out path (APIARY_TEMPLATE_KWARGS). Sub-group
        // by the non-bool template-arg tail; each sub-group of 2^N
        // entries collapses into a TemplateKwargsDispatcher.
        if (!f.template_kwargs.empty()) {
            std::map<std::string, std::vector<std::size_t>> by_tail;
            std::vector<std::string>                        tails_in_order;
            std::size_t const                               n = f.template_kwargs.size();
            for (std::size_t const idx : group) {
                auto const  args = split_instantiation_args(f.instantiations[idx].type_args);
                std::string tail;
                for (std::size_t i = n; i < args.size(); ++i) {
                    if (i != n) {
                        tail += ", ";
                    }
                    tail += args[i];
                }
                if (by_tail.find(tail) == by_tail.end()) {
                    tails_in_order.push_back(tail);
                }
                by_tail[tail].push_back(idx);
            }
            for (auto const &tail : tails_in_order) {
                auto const       &sub      = by_tail[tail];
                std::size_t const expected = std::size_t{1} << n;
                if (sub.size() == expected) {
                    PythonOverload entry;
                    entry.kind                  = PythonOverload::Kind::TemplateKwargsDispatcher;
                    entry.py_name               = py_name;
                    entry.instantiation_indices = sub;
                    entry.kwarg_names           = f.template_kwargs;
                    f.python_overloads.push_back(std::move(entry));
                } else {
                    // Singleton or partial sub-group: emit each as a
                    // distinct overload. (Rare — happens only with an
                    // INSTANTIATE_AS partial specialization alongside
                    // the bool fan-out.)
                    for (std::size_t const idx : sub) {
                        PythonOverload entry;
                        entry.kind                  = PythonOverload::Kind::SingleInstantiation;
                        entry.py_name               = py_name;
                        entry.instantiation_indices = {idx};
                        f.python_overloads.push_back(std::move(entry));
                    }
                }
            }
            continue;
        }

        // Plain @instantiate_as path. Collapse into DtypeDispatcher iff
        // the group has > 1 entries, all share an argument signature,
        // and every entry's leading template arg is a recognized dtype.
        bool same_signature = group.size() > 1;
        if (same_signature) {
            std::string const ref_sig = resolved_arg_signature(f, f.instantiations[group.front()]);
            for (std::size_t k = 1; k < group.size(); ++k) {
                if (resolved_arg_signature(f, f.instantiations[group[k]]) != ref_sig) {
                    same_signature = false;
                    break;
                }
            }
        }
        bool all_dtype_known = same_signature;
        if (all_dtype_known) {
            for (std::size_t const idx : group) {
                auto const split = split_instantiation_args(f.instantiations[idx].type_args);
                if (split.empty() || dtype_aliases_for(split.front()).empty()) {
                    all_dtype_known = false;
                    break;
                }
            }
        }

        if (same_signature && all_dtype_known) {
            PythonOverload entry;
            entry.kind                  = PythonOverload::Kind::DtypeDispatcher;
            entry.py_name               = py_name;
            entry.instantiation_indices = group;
            entry.dtype_values.reserve(group.size());
            for (std::size_t const idx : group) {
                entry.dtype_values.push_back(split_instantiation_args(f.instantiations[idx].type_args).front());
            }
            entry.default_dtype = pick_default_dtype(entry.dtype_values);
            f.python_overloads.push_back(std::move(entry));
        } else if (group.size() == 1) {
            PythonOverload entry;
            entry.kind                  = PythonOverload::Kind::SingleInstantiation;
            entry.py_name               = py_name;
            entry.instantiation_indices = group;
            f.python_overloads.push_back(std::move(entry));
        } else {
            // Multi-entry group that doesn't merge into a dispatcher:
            // pybind11 uses overload resolution. We still record one
            // PythonOverload per instantiation so the .pyi emitter can
            // emit @overload decorators in declaration order.
            for (std::size_t const idx : group) {
                PythonOverload entry;
                entry.kind                  = PythonOverload::Kind::OverloadSet;
                entry.py_name               = py_name;
                entry.instantiation_indices = {idx};
                f.python_overloads.push_back(std::move(entry));
            }
        }
    }
}

void compute_python_overloads(Module &module_) {
    for (auto &f : module_.functions) {
        compute_python_overloads(f);
    }
}

namespace {

// True for a name Python could bind: an identifier, and not something a stub
// would fail to parse. Deliberately conservative - the names that actually
// occur and are invalid are C++ spellings like ``operator*``.
bool is_python_identifier(std::string const &s) {
    if (s.empty()) {
        return false;
    }
    if ((std::isalpha(static_cast<unsigned char>(s.front())) == 0) && s.front() != '_') {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; });
}

std::string const *first_arg(DirectiveList const &directives, std::string_view name) {
    for (Directive const &d : directives) {
        if (d.name == name && !d.args.empty()) {
            return &d.args.front();
        }
    }
    return nullptr;
}

// The name Python will actually see. A directive that renames the binding wins
// over the C++ spelling, which is why an ``operator+`` carrying
// APIARY_OPERATOR("__add__") is perfectly bindable and must not be dropped.
// This has to agree with what the emitters do, or the filter removes something
// they would have emitted correctly.
std::string effective_python_name(BoundEntityCommon const &e) {
    for (std::string_view const directive : {"rename", "operator", "getter", "setter"}) {
        if (std::string const *arg = first_arg(e.directives, directive); arg != nullptr) {
            return *arg;
        }
    }
    return e.name;
}

bool unbindable(BoundEntityCommon const &e, char const *kind) {
    std::string const name = effective_python_name(e);
    if (is_python_identifier(name)) {
        return false;
    }
    diag::report("invalid-python-name", e.location,
                 std::string{kind} + " '" + e.qualified_name + "' binds as '" + name +
                     "', which is not a Python identifier; nothing is bound for it. Give it an APIARY_RENAME, or "
                     "APIARY_OPERATOR if it is an operator on a class.");
    return true;
}

template <typename T>
void drop_if(std::vector<T> &items, char const *kind) {
    items.erase(std::remove_if(items.begin(), items.end(), [&](T const &item) { return unbindable(item, kind); }), items.end());
}

void filter_class(BoundClass &cls) {
    drop_if(cls.methods, "method");
    drop_if(cls.fields, "field");
    drop_if(cls.nested_enums, "enum");
    for (BoundClass &nested : cls.nested_classes) {
        filter_class(nested);
    }
    drop_if(cls.nested_classes, "class");
}

} // namespace

void drop_unbindable_names(Module &module_) {
    drop_if(module_.functions, "function");
    drop_if(module_.enums, "enum");
    for (BoundClass &cls : module_.classes) {
        filter_class(cls);
    }
    drop_if(module_.classes, "class");
}

namespace {

// Replace whole identifier tokens only, so substituting ``T`` does not maul
// ``Tensor``.
std::string replace_identifier(std::string const &text, std::string const &name, std::string const &value) {
    auto is_start = [](char c) { return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_'; };
    auto is_cont  = [](char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; };

    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (!is_start(text[i])) {
            out += text[i++];
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size() && is_cont(text[j])) {
            ++j;
        }
        std::string const token = text.substr(i, j - i);
        out += (token == name) ? value : token;
        i = j;
    }
    return out;
}

} // namespace

std::vector<ExpandedParam> expand_parameter_pack(std::vector<ExpandedParam> const &params,
                                                 std::vector<std::string> const   &template_param_names,
                                                 std::vector<std::string> const   &type_args) {
    std::vector<ExpandedParam> out;
    out.reserve(params.size());

    for (ExpandedParam const &p : params) {
        constexpr std::string_view k_ellipsis = "...";
        if (p.type.size() <= k_ellipsis.size() || p.type.compare(p.type.size() - k_ellipsis.size(), k_ellipsis.size(), k_ellipsis) != 0) {
            out.push_back(p);
            continue;
        }
        std::string const base = p.type.substr(0, p.type.size() - k_ellipsis.size());

        // Which template parameter is the pack? The last one whose name appears
        // in the parameter's type - `Ts...`, `Ts const &...`, `Wrapper<Ts>...`.
        std::size_t pack_index = std::string::npos;
        std::string pack_name;
        for (std::size_t i = 0; i < template_param_names.size(); ++i) {
            std::string const &name = template_param_names[i];
            if (!name.empty() && replace_identifier(base, name, "\x01") != base) {
                pack_index = i;
                pack_name  = name;
            }
        }
        if (pack_index == std::string::npos || pack_index >= type_args.size()) {
            // Nothing to expand against; leave it alone rather than invent an
            // arity. The caller's diagnostic still fires on the `...`.
            out.push_back(p);
            continue;
        }

        for (std::size_t k = pack_index; k < type_args.size(); ++k) {
            ExpandedParam expanded;
            expanded.type = replace_identifier(base, pack_name, type_args[k]);
            // pybind11 needs one py::arg per parameter and Python needs distinct
            // names, so index them from the pack's own start.
            expanded.name = p.name.empty() ? std::string{} : p.name + std::to_string(k - pack_index);
            out.push_back(std::move(expanded));
        }
    }
    return out;
}

} // namespace apiary
