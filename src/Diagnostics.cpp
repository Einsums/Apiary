//----------------------------------------------------------------------------------------------
// Copyright (c) The Einsums Developers. All rights reserved.
// Licensed under the MIT License. See LICENSE.txt in the project root for license information.
//----------------------------------------------------------------------------------------------

#include "Diagnostics.hpp"

#include <algorithm>
#include <set>

#include "llvm/Support/raw_ostream.h"

namespace apiary::diag {

namespace {

struct Check {
    std::string_view id;
    Severity         severity;
    std::string_view help; // one line, printed by --list-diagnostics
};

// The registry. Defaults are deliberately conservative: every check here
// describes output that is already wrong, but making them errors on day one
// would turn a tool that quietly emitted broken code into one that refuses to
// run on inputs it has always accepted. Warning first, then ratchet with
// --diagnostic=<id>=error once the underlying defect is fixed.
//
// skipped-entity is Ignored rather than Note because it fires on every
// templated function without an instantiation directive - a normal, intended
// state - and would bury the checks that matter. It is here so a project
// auditing its coverage can turn it on.
//
// Only ids that something actually reports belong here. A registered check
// that never fires reads as coverage and is not, which is the same kind of
// silence this module exists to remove. (An "unmapped-type" check was drafted
// and dropped for that reason: TypeTranslator deliberately does NOT degrade to
// Any - it hands back the C++ name so pyright flags it - so the check as
// conceived described behavior that does not exist.)
std::vector<Check> &registry() {
    static std::vector<Check> checks = {
        {"unrepresentable-overload", Severity::Warning,
         "an overload the emitted static_cast cannot name (e.g. a ref-qualified method)"},
        {"unrepresentable-pack", Severity::Warning, "a parameter pack re-emitted verbatim into a cast or a stub"},
        {"invalid-python-name", Severity::Warning, "a binding name that is not a Python identifier"},
        {"skipped-entity", Severity::Ignored, "an annotated entity the emitter chose not to bind"},
    };
    return checks;
}

Check *find(std::string_view id) {
    auto &checks = registry();
    auto  it     = std::find_if(checks.begin(), checks.end(), [&](Check const &c) { return c.id == id; });
    return it == checks.end() ? nullptr : &*it;
}

int g_errors   = 0;
int g_warnings = 0;

char const *severity_name(Severity s) {
    switch (s) {
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Ignored:
        break;
    }
    return "ignored";
}

} // namespace

bool parse_severity(std::string_view name, Severity &out) {
    if (name == "ignored") {
        out = Severity::Ignored;
    } else if (name == "note") {
        out = Severity::Note;
    } else if (name == "warning") {
        out = Severity::Warning;
    } else if (name == "error") {
        out = Severity::Error;
    } else {
        return false;
    }
    return true;
}

std::vector<std::string_view> known_checks() {
    std::vector<std::string_view> out;
    out.reserve(registry().size());
    for (Check const &c : registry()) {
        out.push_back(c.id);
    }
    return out;
}

void describe_checks() {
    std::size_t width = 0;
    for (Check const &c : registry()) {
        width = std::max(width, c.id.size());
    }
    for (Check const &c : registry()) {
        llvm::outs() << c.id;
        for (std::size_t i = c.id.size(); i < width + 2; ++i) {
            llvm::outs() << ' ';
        }
        llvm::outs() << severity_name(c.severity);
        for (std::size_t i = std::string_view{severity_name(c.severity)}.size(); i < 9; ++i) {
            llvm::outs() << ' ';
        }
        llvm::outs() << c.help << "\n";
    }
}

bool set_severity(std::string_view check, Severity severity) {
    Check *c = find(check);
    if (c == nullptr) {
        return false;
    }
    c->severity = severity;
    return true;
}

bool set_severity_from_spec(std::string_view spec, std::string &error) {
    std::size_t const eq = spec.find('=');
    if (eq == std::string_view::npos) {
        error = "expected <check>=<severity>, got '" + std::string{spec} + "'";
        return false;
    }
    std::string_view const id   = spec.substr(0, eq);
    std::string_view const name = spec.substr(eq + 1);
    Severity               sev{};
    if (!parse_severity(name, sev)) {
        error = "unknown severity '" + std::string{name} + "'; expected ignored, note, warning or error";
        return false;
    }
    if (!set_severity(id, sev)) {
        std::string ids;
        for (std::string_view const known : known_checks()) {
            if (!ids.empty()) {
                ids += ", ";
            }
            ids += std::string{known};
        }
        error = "unknown check '" + std::string{id} + "'; known checks: " + ids;
        return false;
    }
    return true;
}

void promote_warnings_to_errors() {
    for (Check &c : registry()) {
        if (c.severity == Severity::Warning) {
            c.severity = Severity::Error;
        }
    }
}

void report(std::string_view check, SourceLocation const &loc, std::string const &message) {
    Check const *c = find(check);
    if (c == nullptr) {
        // A typo'd id would otherwise vanish, which is the failure mode this
        // whole module exists to remove. Say so loudly and count it.
        llvm::errs() << "apiary: internal error: unknown diagnostic check '" << check << "'\n";
        ++g_errors;
        return;
    }
    if (c->severity == Severity::Ignored) {
        return;
    }
    // An entity with no recorded location still gets reported; "<unknown>" is
    // worse than a file name and better than silence.
    std::string const file = loc.file.empty() ? std::string{"<unknown>"} : loc.file;

    // Report each distinct problem once. The emitters walk the module more than
    // once - a counting pass before the emitting one, and name lookups happen
    // per use - so a single defect otherwise arrives several times. An identical
    // (check, location, message) IS the same defect; the counts have to agree
    // with what was printed, so dedupe before counting, not after.
    static std::set<std::string> seen;
    std::string const            key = std::string{check} + "|" + file + ":" + std::to_string(loc.line) + ":" +
                                       std::to_string(loc.column) + "|" + message;
    if (!seen.insert(key).second) {
        return;
    }
    llvm::errs() << file << ":" << loc.line << ":" << loc.column << ": " << severity_name(c->severity) << ": apiary: " << message << " ["
                 << check << "]\n";
    if (c->severity == Severity::Error) {
        ++g_errors;
    } else if (c->severity == Severity::Warning) {
        ++g_warnings;
    }
}

int errors() {
    return g_errors;
}

int warnings() {
    return g_warnings;
}

void print_summary() {
    if (g_errors == 0 && g_warnings == 0) {
        return;
    }
    llvm::errs() << "apiary: " << g_errors << " diagnostic error(s), " << g_warnings << " warning(s)\n";
}

} // namespace apiary::diag
