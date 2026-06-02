//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#pragma once

#include <set>
#include <string>

#include "IR.hpp"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace apiary {

/// @brief Walks a translation unit and builds a Module IR populated only with
/// declarations carrying at least one APIARY_* annotation. Other
/// declarations are ignored entirely.
///
/// @note Class scope is tracked via a stack so that methods, fields, and nested
/// types attach to the right BoundClass. Templates are detected and the
/// is_template flag is set, but full instantiation handling is deferred to
/// Phase 4.
class Visitor : public clang::RecursiveASTVisitor<Visitor> {
  public:
    /// @brief Construct a Visitor bound to the given AST context.
    /// @param ctx The Clang AST context to walk.
    explicit Visitor(clang::ASTContext &ctx) : _context(ctx) {}

    /// @brief Set the list of source-include header paths (as passed via
    /// ``--source-include`` on the codegen command line).
    ///
    /// When non-empty, the visitor only binds declarations whose source
    /// location resolves to a file ending in one of these relative paths —
    /// transitive includes from other modules' headers are skipped to avoid
    /// duplicate bindings (the owning module's codegen run handles them).
    /// @param headers The relative header paths to filter declarations by.
    void set_module_header_filter(std::vector<std::string> const &headers) { _module_headers = headers; }

    /// @brief Enable "docs mode": instead of binding only APIARY_*-annotated
    /// declarations, capture the full *public, documented* surface of the
    /// module headers for C++ API documentation (Option 2 — replacing
    /// Doxygen+Breathe with our own libclang extraction).
    ///
    /// The filter is applied by ``passes_docs_filter``: in a module header, not
    /// in a ``detail``/``impl``/anonymous namespace, not ``@internal``, and
    /// (for class members) public access. Documents only entities carrying
    /// a doc comment.
    /// @param on Whether to enable docs mode.
    void set_docs_mode(bool on) { _docs_mode = on; }

    /// @brief Enable "report undocumented" mode (docs mode only).
    ///
    /// When on, every declaration that ``passes_docs_filter`` rejects *solely*
    /// because it lacks a doc comment — i.e. a public, in-module-header,
    /// non-``detail``/``impl``/anonymous, non-``@internal`` entity that SHOULD
    /// be documented — is printed to stderr as
    /// ``file:line:col: undocumented <kind> <name>``. What gets emitted to the
    /// JSON is unchanged; this only surfaces a punch-list of missing Doxygen
    /// blocks. Each entity is reported once per process (deduplicated by
    /// location+name).
    /// @param on Whether to enable report-undocumented mode.
    void set_report_undocumented(bool on) { _report_undocumented = on; }

    /// @brief Move the built Module IR out of the visitor.
    /// @return The accumulated Module IR.
    Module take() && { return std::move(_module); }

    /// @brief Number of errors encountered during traversal.
    /// @return The error count.
    [[nodiscard]] int error_count() const { return _error_count; }

    /// @brief Number of distinct undocumented public entities seen this run
    /// (only meaningful when ``set_report_undocumented(true)``).
    /// @return The count of distinct undocumented public entities.
    [[nodiscard]] int undocumented_count() const { return static_cast<int>(_undocumented_seen.size()); }

    // Override the *Traverse* hooks for class-like records so we can push
    // and pop the scope stack around the recursive descent into members.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Traverse a class-like record, pushing/popping the scope stack
    /// around the recursive descent into its members.
    /// @param decl The record declaration being traversed.
    /// @return True to continue traversal.
    bool TraverseCXXRecordDecl(clang::CXXRecordDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Traverse a class template, pushing/popping the scope stack
    /// around the recursive descent into its members.
    /// @param decl The class template declaration being traversed.
    /// @return True to continue traversal.
    bool TraverseClassTemplateDecl(clang::ClassTemplateDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Traverse a namespace, pushing/popping the inherited submodule
    /// directive stack.
    ///
    /// Entities inside a ``namespace APIARY_MODULE("foo") bar { ... }`` block
    /// inherit ``module:foo`` unless they declare their own override.
    /// @param decl The namespace declaration being traversed.
    /// @return True to continue traversal.
    bool TraverseNamespaceDecl(clang::NamespaceDecl *decl);

    // *Visit* hooks fire on the way down and produce IR records.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit a C++ method declaration and produce an IR record.
    /// @param decl The method declaration being visited.
    /// @return True to continue traversal.
    bool VisitCXXMethodDecl(clang::CXXMethodDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit a free function declaration and produce an IR record.
    /// @param decl The function declaration being visited.
    /// @return True to continue traversal.
    bool VisitFunctionDecl(clang::FunctionDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit a field declaration and produce an IR record.
    /// @param decl The field declaration being visited.
    /// @return True to continue traversal.
    bool VisitFieldDecl(clang::FieldDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit an enum declaration and produce an IR record.
    /// @param decl The enum declaration being visited.
    /// @return True to continue traversal.
    bool VisitEnumDecl(clang::EnumDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit a typedef/using-alias declaration (docs mode only).
    /// @param decl The typedef-name declaration being visited.
    /// @return True to continue traversal.
    bool VisitTypedefNameDecl(clang::TypedefNameDecl *decl);
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    /// @brief Visit a C++20 concept declaration (docs mode only).
    /// @param decl The concept declaration being visited.
    /// @return True to continue traversal.
    bool VisitConceptDecl(clang::ConceptDecl *decl);

  private:
    clang::ASTContext        &_context;
    Module                    _module;
    std::vector<BoundClass *> _class_stack; // current containing class chain (top = innermost)
    int                       _error_count = 0;

    [[nodiscard]] BoundClass *current_class() const { return _class_stack.empty() ? nullptr : _class_stack.back(); }

    // Returns false if the decl carries no apiary annotation, in
    // which case visitors should leave it alone.
    [[nodiscard]] bool has_any_pybind_annotation(clang::Decl const *decl) const;

    // Docs-mode gate for top-level entities (free functions, classes,
    // typedefs, enums): in a module header, not internal, has a doc comment.
    [[nodiscard]] bool passes_docs_filter(clang::NamedDecl const *decl) const;

    // Docs-mode gate for class members: public access + in a module header +
    // not @internal. No per-member doc requirement — a documented class's
    // public members are documented even when individually undocumented.
    [[nodiscard]] bool passes_member_filter(clang::NamedDecl const *decl) const;

    bool _docs_mode = false;

    // ``--report-undocumented``: print public-but-undocumented entities to
    // stderr from ``passes_docs_filter``. ``_undocumented_seen`` deduplicates
    // (a class template can hit the gate via both its template and inner
    // record decl) and backs ``undocumented_count()``. ``mutable`` because
    // the reporting happens inside the ``const`` filter.
    bool                          _report_undocumented = false;
    mutable std::set<std::string> _undocumented_seen;

    // Returns true if the decl's source location is in one of the module's
    // own headers (per ``--source-include`` flags). When the filter is
    // empty all decls pass — preserves behaviour for callers that don't
    // set a filter.
    [[nodiscard]] bool decl_in_module_headers(clang::Decl const *decl) const;

    std::vector<std::string> _module_headers;

    // Stack of inherited submodule paths from enclosing
    // APIARY_MODULE-annotated namespaces. The innermost value is at
    // the back; empty when no enclosing namespace carries a directive.
    // ``fill_common`` injects the back of this stack as a synthetic
    // ``module`` directive on entities that don't have one of their own.
    std::vector<std::string> _module_stack;

    // Common metadata population — qualified name, doc, location, parsed
    // directives. Used by every BoundEntityCommon-derived struct.
    void fill_common(BoundEntityCommon &entity, clang::NamedDecl const *decl);
};

} // namespace apiary
