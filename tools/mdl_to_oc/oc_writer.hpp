//
// Open Controls - OC Format Writer
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include "../libmdl/oc_mdl.hpp"
#include "../libmdl/oc_codegen.hpp"
#include <sstream>
#include <set>
#include <tuple>

namespace oc {

    class oc_writer {
        const mdl::model* model_ = nullptr;

    public:
        void set_model(const mdl::model* m) { model_ = m; }

        [[nodiscard]] auto convert(const mdl::system& sys, std::string_view ns_name = "imported") -> std::string {
            std::ostringstream out;

            auto elem_name = codegen::sanitize_name(sys.name.empty() ? sys.id : sys.name);

            // Use the shared code generator to collect all variables and generate code
            codegen::generator gen;
            gen.set_model(model_);

            // Get generated info from codegen
            auto parts = gen.generate_parts(sys, "");

            out << "namespace " << ns_name << " {\n\n";

            // Emit all functions depth-first (children before parents, before element)
            for (const auto& func : parts.functions) {
                write_function(out, func);
            }

            out << "element " << elem_name << " {\n";
            out << "    frequency: 1kHz;\n";

            // Input section
            if (!parts.inports.empty()) {
                out << "\n    input {\n";
                for (const auto& [name, type] : parts.inports) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // Output section
            if (!parts.outports.empty()) {
                out << "\n    output {\n";
                for (const auto& [name, type] : parts.outports) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // State section
            if (!parts.state_vars.empty()) {
                out << "\n    state {\n";
                for (const auto& [name, comment] : parts.state_vars) {
                    bool is_func_state = (comment == "function state");
                    if (is_func_state) {
                        out << "        " << name << " " << name << ";";
                    } else {
                        out << "        float " << name << " = 0.0;";
                    }
                    if (!comment.empty()) out << "  // " << comment;
                    out << "\n";
                }
                out << "    }\n";
            }

            // Config section
            bool needs_config = !parts.config_vars.empty() || !parts.functions.empty();
            if (needs_config) {
                out << "\n    config {\n";
                for (const auto& var : parts.config_vars) {
                    out << "        float " << var << ";\n";
                }
                out << "        float dt = 0.001;  // sample time\n";
                out << "    }\n";
            }

            // Operation section - uses the shared code generator
            out << "\n    update {\n";
            out << parts.operation_code;
            out << "    }\n";

            out << "}\n\n";
            out << "} // namespace " << ns_name << "\n";

            return out.str();
        }

    private:
        // Recursively write function blocks (depth-first: children before parents)
        void write_function(std::ostringstream& out, const codegen::generated_function& func) {
            // Write child functions first
            for (const auto& child : func.child_functions) {
                write_function(out, child);
            }

            out << "function " << func.name << " {\n";

            // Input section
            if (!func.inports.empty()) {
                out << "    input {\n";
                for (const auto& [name, type] : func.inports) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // Output section
            if (!func.outports.empty()) {
                out << "    output {\n";
                for (const auto& [name, type] : func.outports) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // State section
            if (!func.state_vars.empty()) {
                out << "    state {\n";
                for (const auto& [name, comment] : func.state_vars) {
                    bool is_func_state = (comment == "function state");
                    if (is_func_state) {
                        out << "        " << name << " " << name << ";";
                    } else {
                        out << "        float " << name << " = 0.0;";
                    }
                    if (!comment.empty()) out << "  // " << comment;
                    out << "\n";
                }
                out << "    }\n";
            }

            // Config section (always present for functions - includes dt)
            out << "    config {\n";
            for (const auto& var : func.config_vars) {
                out << "        float " << var << ";\n";
            }
            out << "        float dt = 0.001;  // sample time\n";
            out << "    }\n";

            // Update section
            out << "    update {\n";
            out << func.operation_code;
            out << "    }\n";

            out << "}\n\n";
        }
    };

} // namespace oc
