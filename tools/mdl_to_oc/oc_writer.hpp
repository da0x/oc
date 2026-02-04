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
            auto [inports_list, outports_list, state_vars, config_vars, operation_code] =
                gen.generate_parts(sys, "");

            out << "namespace " << ns_name << " {\n\n";
            out << "element " << elem_name << " {\n";
            out << "    frequency: 1kHz;\n";

            // Input section
            if (!inports_list.empty()) {
                out << "\n    input {\n";
                for (const auto& [name, type] : inports_list) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // Output section
            if (!outports_list.empty()) {
                out << "\n    output {\n";
                for (const auto& [name, type] : outports_list) {
                    out << "        " << type << " " << name << ";\n";
                }
                out << "    }\n";
            }

            // State section
            if (!state_vars.empty()) {
                out << "\n    state {\n";
                for (const auto& [name, comment] : state_vars) {
                    out << "        float " << name << " = 0.0;";
                    if (!comment.empty()) out << "  // " << comment;
                    out << "\n";
                }
                out << "    }\n";
            }

            // Config section
            if (!config_vars.empty()) {
                out << "\n    config {\n";
                for (const auto& var : config_vars) {
                    out << "        float " << var << ";\n";
                }
                out << "        float dt = 0.001;  // sample time\n";
                out << "    }\n";
            }

            // Operation section - uses the shared code generator
            out << "\n    update {\n";
            out << operation_code;
            out << "    }\n";

            out << "}\n\n";
            out << "} // namespace " << ns_name << "\n";

            return out.str();
        }
    };

} // namespace oc
