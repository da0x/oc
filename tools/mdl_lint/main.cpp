//
// Open Controls - MDL Model Linter
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "../libmdl/oc_mdl.hpp"
#include <iostream>
#include <print>
#include <vector>
#include <string>
#include <set>
#include <filesystem>

namespace fs = std::filesystem;

// ANSI color codes
namespace color {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto red    = "\033[31m";
    constexpr auto green  = "\033[32m";
    constexpr auto yellow = "\033[33m";
    constexpr auto blue   = "\033[34m";
    constexpr auto cyan   = "\033[36m";
    constexpr auto dim    = "\033[2m";
}

struct lint_result {
    bool passed = true;
    std::string rule;
    std::string message;
    std::string context;
};

struct lint_report {
    std::string model_name;
    std::string model_type;  // "library" or "app"
    std::vector<lint_result> results;
    int passed = 0;
    int failed = 0;

    void add_pass(const std::string& rule, const std::string& message, const std::string& context = "") {
        results.push_back({true, rule, message, context});
        passed++;
    }

    void add_fail(const std::string& rule, const std::string& message, const std::string& context = "") {
        results.push_back({false, rule, message, context});
        failed++;
    }

    [[nodiscard]] auto all_passed() const -> bool { return failed == 0; }
};

// Check if block references an external library (via SourceBlock parameter)
auto get_source_library(const oc::mdl::block& blk) -> std::string {
    // Reference blocks have a SourceBlock parameter like "library_name/block_name"
    if (auto src = blk.param("SourceBlock")) {
        auto pos = src->find('/');
        if (pos != std::string::npos) {
            return src->substr(0, pos);
        }
    }
    return "";
}

// Detect model type based on content
auto detect_model_type(const oc::mdl::model& model) -> std::string {
    if (model.library_type == "BlockLibrary") {
        return "library";
    }
    return "app";
}

// ─────────────────────────────────────────────────────────────────────────────
// Library Rules
// ─────────────────────────────────────────────────────────────────────────────

void check_library_naming(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "LIB-001";

    const auto* root = model.root_system();
    if (!root) return;

    // Check each top-level subsystem (element) in the library
    for (const auto& blk : root->blocks) {
        if (!blk.is_subsystem()) continue;

        // Check if name follows a meaningful pattern (not just generic names)
        bool has_meaningful_name = !blk.name.empty() && blk.name.length() > 2;

        if (has_meaningful_name) {
            report.add_pass(rule, "Element has descriptive name", blk.name);
        } else {
            report.add_fail(rule, "Element has non-descriptive name", blk.name);
        }
    }
}

void check_library_no_external_links(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "LIB-002";

    // Built-in Simulink libraries are allowed
    std::set<std::string> allowed_libs = {
        "simulink", "simulink_extras", "simscape", "stateflow"
    };

    // Check each system (element) for external library references
    for (const auto& [id, sys] : model.systems) {
        if (id == "system_root") continue;  // Skip root

        bool has_external_link = false;
        std::string linked_lib;

        for (const auto& blk : sys.blocks) {
            auto source_lib = get_source_library(blk);
            if (!source_lib.empty() && source_lib != model.name && !allowed_libs.contains(source_lib)) {
                has_external_link = true;
                linked_lib = source_lib;
                break;
            }
        }

        auto name = sys.name.empty() ? id : sys.name;
        if (!has_external_link) {
            report.add_pass(rule, "No external element links", name);
        } else {
            report.add_fail(rule, "Links to external library: " + linked_lib, name);
        }
    }
}

void check_library_masked(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "LIB-003";

    const auto* root = model.root_system();
    if (!root) return;

    // Check each top-level subsystem block for mask parameters
    for (const auto& blk : root->blocks) {
        if (!blk.is_subsystem()) continue;

        bool is_masked = !blk.mask_parameters.empty();

        if (is_masked) {
            auto param_count = blk.mask_parameters.size();
            report.add_pass(rule, "Element is masked (" + std::to_string(param_count) + " params)", blk.name);
        } else {
            report.add_fail(rule, "Element is not masked (no configuration parameters)", blk.name);
        }
    }
}

void check_library_helper_subsystems(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "LIB-004";

    // For each element system, check its internal subsystems
    for (const auto& [id, sys] : model.systems) {
        if (id == "system_root") continue;

        auto name = sys.name.empty() ? id : sys.name;
        int helper_count = 0;
        bool has_element_subsystem = false;
        std::string problem_subsystem;

        for (const auto& blk : sys.blocks) {
            if (blk.is_subsystem()) {
                helper_count++;

                // Check if this internal subsystem looks like a full element
                // (has many mask params suggesting it's an element, not a helper)
                if (blk.mask_parameters.size() > 3) {
                    has_element_subsystem = true;
                    problem_subsystem = blk.name;
                }
            }
        }

        if (!has_element_subsystem) {
            if (helper_count > 0) {
                report.add_pass(rule, "Has " + std::to_string(helper_count) + " helper subsystem(s)", name);
            } else {
                report.add_pass(rule, "No subsystems (flat structure)", name);
            }
        } else {
            report.add_fail(rule, "Contains element-like subsystem: " + problem_subsystem, name);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// App Rules
// ─────────────────────────────────────────────────────────────────────────────

void check_app_library_links(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "APP-001";

    const auto* root = model.root_system();
    if (!root) {
        report.add_fail(rule, "No root system found", "");
        return;
    }

    std::set<std::string> libraries_used;

    for (const auto& blk : root->blocks) {
        auto source_lib = get_source_library(blk);
        if (!source_lib.empty()) {
            libraries_used.insert(source_lib);
        }
    }

    if (!libraries_used.empty()) {
        std::string libs;
        for (const auto& lib : libraries_used) {
            if (!libs.empty()) libs += ", ";
            libs += lib;
        }
        report.add_pass(rule, "Uses element libraries: " + libs, "");
    } else {
        report.add_fail(rule, "No library links found - app should use element libraries", "");
    }
}

void check_app_links_enforced(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "APP-002";

    const auto* root = model.root_system();
    if (!root) return;

    for (const auto& blk : root->blocks) {
        auto source_lib = get_source_library(blk);
        if (!source_lib.empty()) {
            // Check for link status (LinkStatus parameter)
            auto link_status = blk.param("LinkStatus");
            bool is_broken = link_status && (*link_status == "inactive" || *link_status == "none");

            if (!is_broken) {
                report.add_pass(rule, "Link is active", blk.name + " -> " + source_lib);
            } else {
                report.add_fail(rule, "Link is broken/disabled", blk.name + " -> " + source_lib);
            }
        }
    }
}

void check_app_no_loose_logic(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "APP-003";

    const auto* root = model.root_system();
    if (!root) return;

    std::set<std::string> allowed_types = {
        "Inport", "Outport", "SubSystem", "From", "Goto", "Terminator", "Ground", "Reference"
    };

    bool found_loose = false;

    for (const auto& blk : root->blocks) {
        // Skip if it's a library reference
        if (!get_source_library(blk).empty()) continue;

        // Skip allowed structural blocks
        if (allowed_types.contains(blk.type)) continue;

        // This is loose logic
        report.add_fail(rule, "Loose logic block found: " + blk.type, blk.name);
        found_loose = true;
    }

    if (!found_loose) {
        report.add_pass(rule, "No loose logic blocks at top level", "");
    }
}

void check_app_connections(const oc::mdl::model& model, lint_report& report) {
    const std::string rule = "APP-004";

    const auto* root = model.root_system();
    if (!root) return;

    int connection_count = static_cast<int>(root->connections.size());

    if (connection_count > 0) {
        report.add_pass(rule, "Has " + std::to_string(connection_count) + " connection(s)", "");
    } else {
        report.add_fail(rule, "No connections found between elements", "");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

void print_report(const lint_report& report) {
    std::println("");
    std::println("{}{}══════════════════════════════════════════════════════════════{}",
                 color::bold, color::cyan, color::reset);
    std::println("{}{}  MDL Lint Report: {}{}",
                 color::bold, color::cyan, report.model_name, color::reset);
    std::println("{}{}══════════════════════════════════════════════════════════════{}",
                 color::bold, color::cyan, color::reset);
    std::println("");
    std::println("  {}Model Type:{} {}", color::dim, color::reset, report.model_type);
    std::println("");

    for (const auto& result : report.results) {
        if (result.passed) {
            std::print("  {}✓{} ", color::green, color::reset);
        } else {
            std::print("  {}✗{} ", color::red, color::reset);
        }

        std::print("{}[{}]{} ", color::dim, result.rule, color::reset);
        std::print("{}", result.message);

        if (!result.context.empty()) {
            std::print(" {}({}){}", color::dim, result.context, color::reset);
        }
        std::println("");
    }

    std::println("");
    std::println("{}──────────────────────────────────────────────────────────────{}", color::dim, color::reset);

    if (report.all_passed()) {
        std::println("  {}{}✓ All {} tests passed{}",
                     color::bold, color::green, report.passed, color::reset);
    } else {
        std::println("  {}Passed:{} {}{}  {}Failed:{} {}{}",
                     color::dim, color::reset,
                     color::green, report.passed,
                     color::dim, color::reset,
                     color::red, report.failed);
    }
    std::println("");
}

auto lint_model(const fs::path& path) -> lint_report {
    lint_report report;
    report.model_name = path.filename().string();

    // Load model
    oc::mdl::parser parser;
    if (!parser.load(path.string())) {
        report.add_fail("LOAD", "Failed to load model file", path.string());
        return report;
    }

    const auto& model = parser.get_model();
    report.model_type = detect_model_type(model);

    if (report.model_type == "library") {
        check_library_naming(model, report);
        check_library_no_external_links(model, report);
        check_library_masked(model, report);
        check_library_helper_subsystems(model, report);
    } else {
        check_app_library_links(model, report);
        check_app_links_enforced(model, report);
        check_app_no_loose_logic(model, report);
        check_app_connections(model, report);
    }

    return report;
}

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        std::println("Usage: mdl_lint <model.mdl> [model2.mdl ...]");
        std::println("");
        std::println("Validates MDL models against Open Controls structural rules.");
        std::println("");
        std::println("Library Rules:");
        std::println("  LIB-001  Element names should represent their type");
        std::println("  LIB-002  Elements should not link to other elements");
        std::println("  LIB-003  Elements should be masked with configuration parameters");
        std::println("  LIB-004  Internal subsystems should be helpers, not elements");
        std::println("");
        std::println("App Rules:");
        std::println("  APP-001  App should link elements from libraries");
        std::println("  APP-002  Library links should be enforced (not disabled/broken)");
        std::println("  APP-003  App should only contain elements and connections");
        std::println("  APP-004  App should have connections between elements");
        return 1;
    }

    int total_passed = 0;
    int total_failed = 0;

    for (int i = 1; i < argc; ++i) {
        auto report = lint_model(argv[i]);
        print_report(report);
        total_passed += report.passed;
        total_failed += report.failed;
    }

    if (argc > 2) {
        std::println("{}{}══════════════════════════════════════════════════════════════{}",
                     color::bold, color::blue, color::reset);
        std::println("{}{}  Summary: {} passed, {} failed{}",
                     color::bold, color::blue, total_passed, total_failed, color::reset);
        std::println("{}{}══════════════════════════════════════════════════════════════{}",
                     color::bold, color::blue, color::reset);
    }

    return total_failed > 0 ? 1 : 0;
}
