//
// Open Controls - C++ Code Generator from MDL
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include "oc_mdl.hpp"
#include <sstream>
#include <set>
#include <map>
#include <queue>
#include <algorithm>
#include <functional>
#include <cmath>
#include <iomanip>
#include <regex>

namespace oc::codegen {

    // ─────────────────────────────────────────────────────────────────────────────
    // Transfer Function Discretization (Tustin/Bilinear Transform)
    // ─────────────────────────────────────────────────────────────────────────────

    struct transfer_function {
        std::vector<double> num;  // numerator coefficients [b0, b1, ..., bn] for b0*s^n + b1*s^(n-1) + ...
        std::vector<double> den;  // denominator coefficients [a0, a1, ..., an]
        int order = 0;

        // Parse coefficient array from MATLAB format like "[0.3 0]" or "[0.02 1]"
        static auto parse_coefficients(std::string_view str) -> std::vector<double> {
            std::vector<double> coeffs;
            std::string s(str);

            // Remove brackets
            std::erase(s, '[');
            std::erase(s, ']');

            // Replace commas and semicolons with spaces
            for (char& c : s) {
                if (c == ',' || c == ';') c = ' ';
            }

            std::istringstream iss(s);
            double val;
            while (iss >> val) {
                coeffs.push_back(val);
            }

            return coeffs;
        }

        // Apply Tustin transform to get discrete-time coefficients
        // Returns pair of (num_d, den_d) discrete coefficients
        auto discretize(double dt) const -> std::pair<std::vector<double>, std::vector<double>> {
            // For a first-order system H(s) = (b0*s + b1) / (a0*s + a1)
            // Tustin: s = (2/dt) * (z-1)/(z+1)
            //
            // H(z) = (b0*(2/dt)*(z-1)/(z+1) + b1) / (a0*(2/dt)*(z-1)/(z+1) + a1)
            //      = (b0*(2/dt)*(z-1) + b1*(z+1)) / (a0*(2/dt)*(z-1) + a1*(z+1))
            //
            // Let k = 2/dt
            // Numerator:   (b0*k + b1)*z + (-b0*k + b1)
            // Denominator: (a0*k + a1)*z + (-a0*k + a1)

            double k = 2.0 / dt;

            if (order == 1) {
                double b0 = num.size() > 0 ? num[0] : 0.0;
                double b1 = num.size() > 1 ? num[1] : (num.size() == 1 ? num[0] : 1.0);
                double a0 = den.size() > 0 ? den[0] : 0.0;
                double a1 = den.size() > 1 ? den[1] : 1.0;

                // Handle case where num is just [1] meaning H(s) = 1/(a0*s + a1)
                if (num.size() == 1 && num[0] != 0) {
                    b0 = 0.0;
                    b1 = num[0];
                }

                std::vector<double> num_d = {b0 * k + b1, -b0 * k + b1};
                std::vector<double> den_d = {a0 * k + a1, -a0 * k + a1};

                return {num_d, den_d};
            }
            else if (order == 2) {
                // Second-order: H(s) = (b0*s^2 + b1*s + b2) / (a0*s^2 + a1*s + a2)
                double b0 = num.size() > 0 ? num[0] : 0.0;
                double b1 = num.size() > 1 ? num[1] : 0.0;
                double b2 = num.size() > 2 ? num[2] : (num.size() == 1 ? num[0] : 1.0);
                double a0 = den.size() > 0 ? den[0] : 0.0;
                double a1 = den.size() > 1 ? den[1] : 0.0;
                double a2 = den.size() > 2 ? den[2] : 1.0;

                if (num.size() == 1) { b0 = 0; b1 = 0; b2 = num[0]; }

                double k2 = k * k;

                // Tustin for s^2 -> (k*(z-1)/(z+1))^2 = k^2*(z^2 - 2z + 1)/(z^2 + 2z + 1)
                // Full expansion gives:
                // num: (b0*k2 + b1*k + b2)*z^2 + (2*b2 - 2*b0*k2)*z + (b0*k2 - b1*k + b2)
                // den: (a0*k2 + a1*k + a2)*z^2 + (2*a2 - 2*a0*k2)*z + (a0*k2 - a1*k + a2)

                std::vector<double> num_d = {
                    b0*k2 + b1*k + b2,
                    2*b2 - 2*b0*k2,
                    b0*k2 - b1*k + b2
                };
                std::vector<double> den_d = {
                    a0*k2 + a1*k + a2,
                    2*a2 - 2*a0*k2,
                    a0*k2 - a1*k + a2
                };

                return {num_d, den_d};
            }

            // Fallback: return original (shouldn't happen for supported orders)
            return {num, den};
        }
    };

    // Format a double as a float literal with proper syntax
    inline auto format_float(double val) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << val << "f";
        return oss.str();
    }

    // Parse a TransferFcn block and return order
    inline auto parse_transfer_function(const mdl::block& blk) -> transfer_function {
        transfer_function tf;

        auto num_str = blk.param("Numerator").value_or("[1]");
        auto den_str = blk.param("Denominator").value_or("[1]");

        tf.num = transfer_function::parse_coefficients(num_str);
        tf.den = transfer_function::parse_coefficients(den_str);

        // Order is determined by denominator (assuming proper transfer function)
        tf.order = static_cast<int>(tf.den.size()) - 1;
        if (tf.order < 1) tf.order = 1;

        return tf;
    }

    // Helper function to sanitize names (public for reuse)
    [[nodiscard]] inline auto sanitize_name(std::string_view name) -> std::string {
        std::string result;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                result += c;
            } else if (c == ' ' || c == '-') {
                result += '_';
            }
        }
        if (!result.empty() && std::isdigit(static_cast<unsigned char>(result[0]))) {
            result = "_" + result;
        }
        return result;
    }

    // Struct to hold generated parts for reuse between different output formats
    struct generated_parts {
        std::vector<std::pair<std::string, std::string>> inports;      // {name, type}
        std::vector<std::pair<std::string, std::string>> outports;     // {name, type}
        std::vector<std::pair<std::string, std::string>> state_vars;   // {name, comment}
        std::set<std::string> config_vars;
        std::string operation_code;
    };

    class generator {
        const mdl::model* model_ = nullptr;
        std::string indent_ = "        ";
        int max_inline_depth_ = 10;

        // Accumulated state variables from all inlined subsystems
        std::vector<std::pair<std::string, std::string>> all_state_vars_;  // {state_var_name, comment}

        // Accumulated config variables
        std::set<std::string> all_config_vars_;

    public:
        void set_model(const mdl::model* m) { model_ = m; }

        // Generate structured parts that can be used by different output formats (OC, C++, etc.)
        [[nodiscard]] auto generate_parts(const mdl::system& sys, std::string_view prefix = "") -> generated_parts {
            // Reset accumulators
            all_state_vars_.clear();
            all_config_vars_.clear();

            // First pass: collect all config and state variables (including from subsystems)
            collect_all_variables(sys, std::string(prefix), 0);

            // Get inputs and outputs
            auto inports = sys.inports();
            std::ranges::sort(inports, [](const auto& a, const auto& b) {
                int pa = 1, pb = 1;
                if (auto v = a.param("Port")) pa = std::stoi(*v);
                if (auto v = b.param("Port")) pb = std::stoi(*v);
                return pa < pb;
            });

            auto outports = sys.outports();
            std::ranges::sort(outports, [](const auto& a, const auto& b) {
                int pa = 1, pb = 1;
                if (auto v = a.param("Port")) pa = std::stoi(*v);
                if (auto v = b.param("Port")) pb = std::stoi(*v);
                return pa < pb;
            });

            // Build inports list
            std::vector<std::pair<std::string, std::string>> inports_list;
            for (const auto& inp : inports) {
                inports_list.emplace_back(sanitize_name(inp.name), "float");
            }

            // Build outports list
            std::vector<std::pair<std::string, std::string>> outports_list;
            for (const auto& outp : outports) {
                outports_list.emplace_back(sanitize_name(outp.name), "float");
            }

            // Generate operation code
            std::ostringstream code;
            std::map<std::string, std::string> signal_map;

            // Map inports to input struct
            for (const auto& inp : inports) {
                auto key = inp.sid + "#out:1";
                signal_map[key] = "in." + sanitize_name(inp.name);
            }

            generate_system_code(sys, std::string(prefix), signal_map, code, 0);

            // Generate output assignments
            code << "\n" << indent_ << "// Outputs\n";
            for (const auto& outp : outports) {
                // Find what connects to this outport
                for (const auto& conn : sys.connections) {
                    auto check_dst = [&](const std::string& dst_str) {
                        if (auto dst = mdl::endpoint::parse(dst_str)) {
                            if (dst->block_sid == outp.sid) {
                                if (auto src = mdl::endpoint::parse(conn.source)) {
                                    auto src_key = src->block_sid + "#out:" + std::to_string(src->port_index);
                                    if (signal_map.count(src_key)) {
                                        code << indent_ << "out." << sanitize_name(outp.name)
                                             << " = " << signal_map[src_key] << ";\n";
                                    }
                                }
                            }
                        }
                    };

                    check_dst(conn.destination);
                    for (const auto& br : conn.branches) {
                        check_dst(br.destination);
                    }
                }
            }

            return generated_parts{
                .inports = std::move(inports_list),
                .outports = std::move(outports_list),
                .state_vars = all_state_vars_,
                .config_vars = all_config_vars_,
                .operation_code = code.str()
            };
        }

        [[nodiscard]] auto generate(const mdl::system& sys, std::string_view ns_name = "generated") -> std::string {
            // Use generate_parts() for code reuse
            auto parts = generate_parts(sys, "");

            auto elem_name = sanitize_name(sys.name.empty() ? sys.id : sys.name);

            // Build output
            std::ostringstream out;
            out << "namespace " << ns_name << " {\n\n";

            // Input struct
            out << "    struct " << elem_name << "_input {\n";
            for (const auto& [name, type] : parts.inports) {
                out << "        " << type << " " << name << " = 0.0f;\n";
            }
            out << "    };\n\n";

            // Output struct
            out << "    struct " << elem_name << "_output {\n";
            for (const auto& [name, type] : parts.outports) {
                out << "        " << type << " " << name << " = 0.0f;\n";
            }
            out << "    };\n\n";

            // State struct
            if (!parts.state_vars.empty()) {
                out << "    struct " << elem_name << "_state {\n";
                for (const auto& [var, comment] : parts.state_vars) {
                    out << "        float " << var << " = 0.0f;";
                    if (!comment.empty()) out << "  // " << comment;
                    out << "\n";
                }
                out << "    };\n\n";
            }

            // Config struct
            if (!parts.config_vars.empty()) {
                out << "    struct " << elem_name << "_config {\n";
                for (const auto& var : parts.config_vars) {
                    out << "        float " << var << " = 0.0f;\n";
                }
                out << "        float dt = 0.001f;  // sample time\n";
                out << "    };\n\n";
            }

            // Update function
            out << "    inline auto " << elem_name << "_update(\n";
            out << "        const " << elem_name << "_input& in,\n";
            if (!parts.config_vars.empty()) {
                out << "        const " << elem_name << "_config& cfg,\n";
            }
            if (!parts.state_vars.empty()) {
                out << "        " << elem_name << "_state& state,\n";
            }
            out << "        " << elem_name << "_output& out) -> void\n";
            out << "    {\n";

            out << parts.operation_code;

            out << "    }\n\n";
            out << "} // namespace " << ns_name << "\n";

            return out.str();
        }

    private:
        // Collect all state and config variables recursively
        void collect_all_variables(const mdl::system& sys, const std::string& prefix, int depth) {
            if (depth > max_inline_depth_) return;

            for (const auto& blk : sys.blocks) {
                auto var_prefix = prefix.empty() ? sanitize_name(blk.name) : prefix + "_" + sanitize_name(blk.name);

                // State blocks
                if (blk.type == "UnitDelay" || blk.type == "Integrator" ||
                    blk.type == "DiscreteIntegrator" || blk.type == "Memory") {
                    auto state_var = var_prefix + "_state";
                    auto comment = blk.type + " in " + (prefix.empty() ? "root" : prefix);
                    all_state_vars_.emplace_back(state_var, comment);
                }

                // TransferFcn needs state variables based on order
                if (blk.type == "TransferFcn") {
                    auto tf = parse_transfer_function(blk);
                    for (int i = 0; i < tf.order; ++i) {
                        auto state_var = var_prefix + "_tf_x" + std::to_string(i);
                        auto comment = "TransferFcn state " + std::to_string(i) + " in " + (prefix.empty() ? "root" : prefix);
                        all_state_vars_.emplace_back(state_var, comment);
                        // Also need previous input state
                        state_var = var_prefix + "_tf_u" + std::to_string(i);
                        comment = "TransferFcn input history " + std::to_string(i);
                        all_state_vars_.emplace_back(state_var, comment);
                    }
                }

                // Config from block parameters
                collect_config_from_block(blk);

                // Recurse into subsystems
                if (blk.is_subsystem() && !blk.subsystem_ref.empty() && model_) {
                    if (auto* subsys = model_->get_system(blk.subsystem_ref)) {
                        collect_all_variables(*subsys, var_prefix, depth + 1);
                    }
                }
            }
        }

        void collect_config_from_block(const mdl::block& blk) {
            static constexpr std::array param_names = {
                "Gain", "UpperLimit", "LowerLimit", "Value", "InitialCondition",
                "Threshold", "Numerator", "Denominator"
            };

            for (const auto& pname : param_names) {
                if (auto v = blk.param(pname)) {
                    extract_config_vars(*v, all_config_vars_);
                }
            }

            for (const auto& mp : blk.mask_parameters) {
                extract_config_vars(mp.value, all_config_vars_);
            }
        }

        // Generate code for a system, populating signal_map with output variable names
        void generate_system_code(
            const mdl::system& sys,
            const std::string& prefix,
            std::map<std::string, std::string>& signal_map,
            std::ostringstream& code,
            int depth)
        {
            if (depth > max_inline_depth_) {
                code << indent_ << "// Max inline depth reached\n";
                return;
            }

            // Build state block set for this system
            std::set<std::string> state_sids;
            std::map<std::string, std::string> state_var_map;  // SID -> state variable name

            for (const auto& blk : sys.blocks) {
                if (blk.type == "UnitDelay" || blk.type == "Integrator" ||
                    blk.type == "DiscreteIntegrator" || blk.type == "Memory") {
                    state_sids.insert(blk.sid);
                    auto var_prefix = prefix.empty() ? sanitize_name(blk.name) : prefix + "_" + sanitize_name(blk.name);
                    state_var_map[blk.sid] = "state." + var_prefix + "_state";
                }
            }

            // Assign output variable names for all blocks
            for (const auto& blk : sys.blocks) {
                if (blk.is_inport() || blk.is_outport()) continue;

                auto var_prefix = prefix.empty() ? sanitize_name(blk.name) : prefix + "_" + sanitize_name(blk.name);

                int num_outputs = blk.port_out;

                // For subsystems, pre-map outputs to the names that will be created during inlining
                if (blk.is_subsystem()) {
                    for (int i = 1; i <= num_outputs; ++i) {
                        auto key = blk.sid + "#out:" + std::to_string(i);
                        signal_map[key] = var_prefix + "_out" + std::to_string(i);
                    }
                    continue;
                }

                for (int i = 1; i <= num_outputs; ++i) {
                    auto key = blk.sid + "#out:" + std::to_string(i);

                    // State blocks output from state variable
                    if (state_sids.contains(blk.sid)) {
                        signal_map[key] = state_var_map[blk.sid];
                    } else {
                        auto var = var_prefix;
                        if (num_outputs > 1) var += "_" + std::to_string(i);
                        signal_map[key] = var;
                    }
                }
            }

            // Build dependencies and input mappings
            std::map<std::string, std::set<std::string>> dependencies;
            std::map<std::string, std::vector<std::string>> block_inputs;

            for (const auto& blk : sys.blocks) {
                if (blk.is_inport()) continue;
                dependencies[blk.sid] = {};
                block_inputs[blk.sid] = {};
            }

            for (const auto& conn : sys.connections) {
                auto src = mdl::endpoint::parse(conn.source);
                if (!src) continue;

                auto src_key = src->block_sid + "#out:" + std::to_string(src->port_index);
                auto src_var = signal_map.count(src_key) ? signal_map[src_key] : "0.0f /* unknown */";

                auto add_connection = [&](const std::string& dst_str) {
                    auto dst = mdl::endpoint::parse(dst_str);
                    if (!dst) return;

                    auto& inputs = block_inputs[dst->block_sid];
                    if (inputs.size() < static_cast<size_t>(dst->port_index)) {
                        inputs.resize(dst->port_index);
                    }
                    inputs[dst->port_index - 1] = src_var;

                    if (dependencies.count(dst->block_sid)) {
                        auto* src_blk = sys.find_block_by_sid(src->block_sid);
                        bool is_inport = src_blk && src_blk->is_inport();
                        bool is_state = state_sids.contains(src->block_sid);

                        if (!is_inport && !is_state) {
                            dependencies[dst->block_sid].insert(src->block_sid);
                        }
                    }
                };

                if (!conn.destination.empty()) {
                    add_connection(conn.destination);
                }
                for (const auto& branch : conn.branches) {
                    add_connection(branch.destination);
                }
            }

            // Topological sort
            std::map<std::string, int> in_degree;
            for (const auto& [sid, deps] : dependencies) {
                if (!in_degree.count(sid)) in_degree[sid] = 0;
                for (const auto& dep : deps) {
                    if (auto* blk = sys.find_block_by_sid(dep); blk && !blk->is_inport()) {
                        in_degree[sid]++;
                    }
                }
            }

            std::queue<std::string> ready;
            for (const auto& [sid, deg] : in_degree) {
                if (deg == 0) ready.push(sid);
            }

            std::vector<std::string> sorted_sids;
            while (!ready.empty()) {
                auto sid = ready.front();
                ready.pop();
                sorted_sids.push_back(sid);

                for (auto& [other_sid, deps] : dependencies) {
                    if (deps.contains(sid)) {
                        deps.erase(sid);
                        in_degree[other_sid]--;
                        if (in_degree[other_sid] == 0) {
                            ready.push(other_sid);
                        }
                    }
                }
            }

            // Generate code for each block
            for (const auto& sid : sorted_sids) {
                auto* blk = sys.find_block_by_sid(sid);
                if (!blk || blk->is_inport() || blk->is_outport()) continue;

                auto& inputs = block_inputs[sid];
                auto var_prefix = prefix.empty() ? sanitize_name(blk->name) : prefix + "_" + sanitize_name(blk->name);
                auto out_var = signal_map[sid + "#out:1"];
                auto state_var = state_var_map.count(sid) ? state_var_map[sid] : "";

                generate_block_code(*blk, inputs, out_var, var_prefix, state_var, signal_map, code, depth);
            }
        }

        void generate_block_code(
            const mdl::block& blk,
            const std::vector<std::string>& inputs,
            const std::string& out_var,
            const std::string& var_prefix,
            const std::string& state_var,
            std::map<std::string, std::string>& signal_map,
            std::ostringstream& code,
            int depth)
        {
            auto get_input = [&](int idx) -> std::string {
                if (idx < static_cast<int>(inputs.size()) && !inputs[idx].empty()) {
                    return inputs[idx];
                }
                return "0.0f /* missing input " + std::to_string(idx + 1) + " */";
            };

            auto get_param = [&](const std::string& name, const std::string& def = "0.0f") -> std::string {
                if (auto v = blk.param(name)) {
                    return format_param_value(*v);
                }
                return def;
            };

            // Handle SubSystem by inlining
            if (blk.type == "SubSystem") {
                if (!blk.subsystem_ref.empty() && model_) {
                    if (auto* subsys = model_->get_system(blk.subsystem_ref)) {
                        generate_subsystem_inline(*subsys, blk, inputs, var_prefix, signal_map, code, depth);
                        return;
                    }
                }
                code << indent_ << "// SubSystem: " << blk.name << " (not found)\n";
                code << indent_ << "auto " << out_var << " = " << get_input(0) << ";\n";
                return;
            }

            code << indent_ << "// " << blk.type << ": " << blk.name << "\n";

            if (blk.type == "Gain") {
                auto gain = get_param("Gain", "1.0f");
                code << indent_ << "auto " << out_var << " = " << get_input(0) << " * " << gain << ";\n";
            }
            else if (blk.type == "Sum") {
                auto inputs_spec = blk.param("Inputs").value_or("++");
                code << indent_ << "auto " << out_var << " = ";

                bool first = true;
                int input_idx = 0;
                for (char c : inputs_spec) {
                    if (c == '|') continue;
                    if (c == '+' || c == '-') {
                        if (!first) code << " ";
                        if (c == '-') code << "- ";
                        else if (!first) code << "+ ";
                        code << get_input(input_idx++);
                        first = false;
                    }
                }
                code << ";\n";
            }
            else if (blk.type == "Product") {
                auto inputs_spec = blk.param("Inputs").value_or("**");
                code << indent_ << "auto " << out_var << " = ";

                int idx = 0;
                bool first = true;
                for (char c : inputs_spec) {
                    if (c == '*' || c == '/') {
                        if (!first) code << (c == '*' ? " * " : " / ");
                        code << get_input(idx++);
                        first = false;
                    }
                }
                if (idx == 0) {
                    code << get_input(0) << " * " << get_input(1);
                }
                code << ";\n";
            }
            else if (blk.type == "Saturate") {
                auto upper = get_param("UpperLimit", "1.0f");
                auto lower = get_param("LowerLimit", "-1.0f");
                code << indent_ << "auto " << out_var << " = std::clamp(" << get_input(0)
                     << ", " << lower << ", " << upper << ");\n";
            }
            else if (blk.type == "MinMax") {
                auto func = blk.param("Function").value_or("min");
                std::string fn = (func == "max" || func == "Max") ? "std::max" : "std::min";
                code << indent_ << "auto " << out_var << " = " << fn << "("
                     << get_input(0) << ", " << get_input(1) << ");\n";
            }
            else if (blk.type == "Abs") {
                code << indent_ << "auto " << out_var << " = std::abs(" << get_input(0) << ");\n";
            }
            else if (blk.type == "Constant") {
                auto value = get_param("Value", "0.0f");
                code << indent_ << "auto " << out_var << " = " << value << ";\n";
            }
            else if (blk.type == "UnitDelay" || blk.type == "Memory") {
                // Output already comes from state variable, just update state
                code << indent_ << state_var << " = " << get_input(0) << ";  // update for next step\n";
            }
            else if (blk.type == "Integrator" || blk.type == "DiscreteIntegrator") {
                code << indent_ << state_var << " += " << get_input(0) << " * cfg.dt;\n";
            }
            else if (blk.type == "RelationalOperator") {
                auto op = blk.param("Operator").value_or("==");
                std::string cpp_op = op;
                if (op == "~=") cpp_op = "!=";
                code << indent_ << "auto " << out_var << " = (" << get_input(0)
                     << " " << cpp_op << " " << get_input(1) << ") ? 1.0f : 0.0f;\n";
            }
            else if (blk.type == "Logic") {
                auto op = blk.param("Operator").value_or("AND");
                if (op == "NOT") {
                    code << indent_ << "auto " << out_var << " = (" << get_input(0)
                         << " == 0.0f) ? 1.0f : 0.0f;\n";
                } else {
                    std::string cpp_op = "&&";
                    if (op == "OR") cpp_op = "||";
                    else if (op == "XOR") cpp_op = "!=";
                    code << indent_ << "auto " << out_var << " = ((" << get_input(0)
                         << " != 0.0f) " << cpp_op << " (" << get_input(1)
                         << " != 0.0f)) ? 1.0f : 0.0f;\n";
                }
            }
            else if (blk.type == "Switch") {
                auto threshold = get_param("Threshold", "0.0f");
                auto criteria = blk.param("Criteria").value_or("u2 >= Threshold");
                std::string cond;
                if (criteria.find(">=") != std::string::npos) {
                    cond = get_input(1) + " >= " + threshold;
                } else if (criteria.find(">") != std::string::npos) {
                    cond = get_input(1) + " > " + threshold;
                } else if (criteria.find("!=") != std::string::npos || criteria.find("~=") != std::string::npos) {
                    cond = get_input(1) + " != " + threshold;
                } else {
                    cond = get_input(1) + " != 0.0f";
                }
                code << indent_ << "auto " << out_var << " = (" << cond << ") ? "
                     << get_input(0) << " : " << get_input(2) << ";\n";
            }
            else if (blk.type == "Trigonometry") {
                auto func = blk.param("Operator").value_or("sin");
                code << indent_ << "auto " << out_var << " = std::" << func << "("
                     << get_input(0) << ");\n";
            }
            else if (blk.type == "Math") {
                auto func = blk.param("Operator").value_or("sqrt");
                if (func == "sqrt" || func == "exp" || func == "log" || func == "log10") {
                    code << indent_ << "auto " << out_var << " = std::" << func << "("
                         << get_input(0) << ");\n";
                } else if (func == "square") {
                    code << indent_ << "auto " << out_var << " = " << get_input(0) << " * "
                         << get_input(0) << ";\n";
                } else if (func == "pow") {
                    code << indent_ << "auto " << out_var << " = std::pow("
                         << get_input(0) << ", " << get_input(1) << ");\n";
                } else {
                    code << indent_ << "auto " << out_var << " = " << get_input(0)
                         << "; // TODO: Math/" << func << "\n";
                }
            }
            else if (blk.type == "TransferFcn") {
                // Tustin discretization
                auto tf = parse_transfer_function(blk);
                auto state_prefix = "state." + var_prefix + "_tf_";

                code << indent_ << "// TransferFcn: " << blk.name << " (order " << tf.order << ")\n";
                code << indent_ << "{\n";

                if (tf.order == 1) {
                    // First-order: y[n] = (b0_d*u[n] + b1_d*u[n-1] - a1_d*y[n-1]) / a0_d
                    // Using Direct Form I with Tustin coefficients
                    auto [num_d, den_d] = tf.discretize(0.001);  // Default dt, will use cfg.dt at runtime

                    // We need to compute coefficients at runtime since dt is in cfg
                    // For H(s) = (b0*s + b1)/(a0*s + a1), Tustin gives:
                    // Let k = 2/dt
                    // num_d = [b0*k + b1, -b0*k + b1]
                    // den_d = [a0*k + a1, -a0*k + a1]

                    double b0 = tf.num.size() > 1 ? tf.num[0] : 0.0;
                    double b1 = tf.num.size() > 1 ? tf.num[1] : (tf.num.size() == 1 ? tf.num[0] : 1.0);
                    double a0 = tf.den.size() > 0 ? tf.den[0] : 0.0;
                    double a1 = tf.den.size() > 1 ? tf.den[1] : 1.0;

                    if (tf.num.size() == 1) { b0 = 0.0; b1 = tf.num[0]; }

                    code << indent_ << "    float k = 2.0f / cfg.dt;\n";
                    code << indent_ << "    float b0_d = " << format_float(b0) << " * k + " << format_float(b1) << ";\n";
                    code << indent_ << "    float b1_d = -" << format_float(b0) << " * k + " << format_float(b1) << ";\n";
                    code << indent_ << "    float a0_d = " << format_float(a0) << " * k + " << format_float(a1) << ";\n";
                    code << indent_ << "    float a1_d = -" << format_float(a0) << " * k + " << format_float(a1) << ";\n";
                    code << indent_ << "    float u_n = " << get_input(0) << ";\n";
                    code << indent_ << "    float y_n = (b0_d * u_n + b1_d * " << state_prefix << "u0"
                         << " - a1_d * " << state_prefix << "x0) / a0_d;\n";
                    code << indent_ << "    " << state_prefix << "u0 = u_n;\n";
                    code << indent_ << "    " << state_prefix << "x0 = y_n;\n";
                    code << indent_ << "}\n";
                    code << indent_ << "auto " << out_var << " = " << state_prefix << "x0;\n";
                }
                else if (tf.order == 2) {
                    // Second-order system
                    double b0 = tf.num.size() > 2 ? tf.num[0] : 0.0;
                    double b1 = tf.num.size() > 2 ? tf.num[1] : (tf.num.size() > 1 ? tf.num[0] : 0.0);
                    double b2 = tf.num.size() > 2 ? tf.num[2] : (tf.num.size() > 1 ? tf.num[1] : tf.num[0]);
                    double a0 = tf.den[0];
                    double a1 = tf.den.size() > 1 ? tf.den[1] : 0.0;
                    double a2 = tf.den.size() > 2 ? tf.den[2] : 1.0;

                    if (tf.num.size() == 1) { b0 = 0; b1 = 0; b2 = tf.num[0]; }

                    code << indent_ << "    float k = 2.0f / cfg.dt;\n";
                    code << indent_ << "    float k2 = k * k;\n";
                    code << indent_ << "    float b0_d = " << format_float(b0) << "*k2 + " << format_float(b1) << "*k + " << format_float(b2) << ";\n";
                    code << indent_ << "    float b1_d = 2.0f*" << format_float(b2) << " - 2.0f*" << format_float(b0) << "*k2;\n";
                    code << indent_ << "    float b2_d = " << format_float(b0) << "*k2 - " << format_float(b1) << "*k + " << format_float(b2) << ";\n";
                    code << indent_ << "    float a0_d = " << format_float(a0) << "*k2 + " << format_float(a1) << "*k + " << format_float(a2) << ";\n";
                    code << indent_ << "    float a1_d = 2.0f*" << format_float(a2) << " - 2.0f*" << format_float(a0) << "*k2;\n";
                    code << indent_ << "    float a2_d = " << format_float(a0) << "*k2 - " << format_float(a1) << "*k + " << format_float(a2) << ";\n";
                    code << indent_ << "    float u_n = " << get_input(0) << ";\n";
                    code << indent_ << "    float y_n = (b0_d*u_n + b1_d*" << state_prefix << "u0 + b2_d*"
                         << state_prefix << "u1 - a1_d*" << state_prefix << "x0 - a2_d*" << state_prefix << "x1) / a0_d;\n";
                    code << indent_ << "    " << state_prefix << "u1 = " << state_prefix << "u0;\n";
                    code << indent_ << "    " << state_prefix << "u0 = u_n;\n";
                    code << indent_ << "    " << state_prefix << "x1 = " << state_prefix << "x0;\n";
                    code << indent_ << "    " << state_prefix << "x0 = y_n;\n";
                    code << indent_ << "}\n";
                    code << indent_ << "auto " << out_var << " = " << state_prefix << "x0;\n";
                }
                else {
                    // Higher order - fallback to passthrough with warning
                    code << indent_ << "    // Order " << tf.order << " transfer function not yet supported\n";
                    code << indent_ << "}\n";
                    code << indent_ << "auto " << out_var << " = " << get_input(0) << ";\n";
                }
            }
            else if (blk.type == "Derivative") {
                code << indent_ << "auto " << out_var << " = " << get_input(0)
                     << "; // TODO: Derivative needs previous value\n";
            }
            else if (blk.type == "Demux") {
                // Demux just passes through - outputs are indexed separately
                int num_outputs = blk.port_out;
                for (int i = 1; i <= num_outputs; ++i) {
                    auto key = blk.sid + "#out:" + std::to_string(i);
                    signal_map[key] = get_input(0) + " /* demux " + std::to_string(i) + " */";
                }
            }
            else if (blk.type == "Mux") {
                // Mux combines inputs - for now treat as first input
                code << indent_ << "auto " << out_var << " = " << get_input(0) << "; // Mux\n";
            }
            else {
                code << indent_ << "auto " << out_var << " = " << get_input(0)
                     << "; // TODO: " << blk.type << "\n";
            }
        }

        void generate_subsystem_inline(
            const mdl::system& subsys,
            const mdl::block& blk,
            const std::vector<std::string>& inputs,
            const std::string& var_prefix,
            std::map<std::string, std::string>& parent_signal_map,
            std::ostringstream& code,
            int depth)
        {
            code << indent_ << "// ─── Subsystem: " << blk.name << " ───\n";

            // Create a local signal map for the subsystem, starting with parent's map
            // This allows subsystem to see signals from parent scope
            std::map<std::string, std::string> sub_signal_map = parent_signal_map;

            // Map subsystem inports to the inputs passed to this block
            auto inports = subsys.inports();
            std::ranges::sort(inports, [](const auto& a, const auto& b) {
                int pa = 1, pb = 1;
                if (auto v = a.param("Port")) pa = std::stoi(*v);
                if (auto v = b.param("Port")) pb = std::stoi(*v);
                return pa < pb;
            });

            for (std::size_t i = 0; i < inports.size(); ++i) {
                auto key = inports[i].sid + "#out:1";
                if (i < inputs.size() && !inputs[i].empty()) {
                    sub_signal_map[key] = inputs[i];
                } else {
                    sub_signal_map[key] = "0.0f /* missing subsystem input */";
                }
            }

            // Generate code for the subsystem
            generate_system_code(subsys, var_prefix, sub_signal_map, code, depth + 1);

            // Map subsystem outports to the parent signal map
            auto outports = subsys.outports();
            std::ranges::sort(outports, [](const auto& a, const auto& b) {
                int pa = 1, pb = 1;
                if (auto v = a.param("Port")) pa = std::stoi(*v);
                if (auto v = b.param("Port")) pb = std::stoi(*v);
                return pa < pb;
            });

            for (std::size_t i = 0; i < outports.size(); ++i) {
                // Find what connects to this outport in the subsystem
                std::string outport_value = "0.0f /* unmapped outport */";
                for (const auto& conn : subsys.connections) {
                    auto check_dst = [&](const std::string& dst_str) {
                        if (auto dst = mdl::endpoint::parse(dst_str)) {
                            if (dst->block_sid == outports[i].sid) {
                                if (auto src = mdl::endpoint::parse(conn.source)) {
                                    auto src_key = src->block_sid + "#out:" + std::to_string(src->port_index);
                                    if (sub_signal_map.count(src_key)) {
                                        outport_value = sub_signal_map[src_key];
                                    }
                                }
                            }
                        }
                    };
                    check_dst(conn.destination);
                    for (const auto& br : conn.branches) {
                        check_dst(br.destination);
                    }
                }

                // Map to parent's signal map
                auto parent_key = blk.sid + "#out:" + std::to_string(i + 1);
                parent_signal_map[parent_key] = outport_value;

                // Also create an alias variable for readability
                auto alias_var = var_prefix + "_out" + std::to_string(i + 1);
                code << indent_ << "auto " << alias_var << " = " << outport_value << ";\n";
                parent_signal_map[parent_key] = alias_var;
            }

            code << indent_ << "// ─── End: " << blk.name << " ───\n";
        }

        static void extract_config_vars(std::string_view expr, std::set<std::string>& vars) {
            static const std::set<std::string> builtins = {
                "sqrt", "exp", "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan",
                "sinh", "cosh", "tanh", "abs", "floor", "ceil", "round", "mod", "sign",
                "max", "min", "pi", "inf", "nan", "eps", "true", "false"
            };

            std::string current;
            for (char c : expr) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    current += c;
                } else {
                    if (!current.empty() &&
                        std::isalpha(static_cast<unsigned char>(current[0])) &&
                        !builtins.contains(current)) {
                        vars.insert(current);
                    }
                    current.clear();
                }
            }
            if (!current.empty() &&
                std::isalpha(static_cast<unsigned char>(current[0])) &&
                !builtins.contains(current)) {
                vars.insert(current);
            }
        }

        // Format parameter value, replacing MATLAB constants with C++ equivalents
        [[nodiscard]] static auto format_param_value(std::string_view value) -> std::string {
            if (value.empty()) return "0.0f";

            std::string result(value);

            // Replace MATLAB constants with C++ equivalents
            auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
                size_t pos = 0;
                while ((pos = str.find(from, pos)) != std::string::npos) {
                    // Check it's a whole word (not part of another identifier)
                    bool word_start = (pos == 0 || !std::isalnum(static_cast<unsigned char>(str[pos - 1])));
                    bool word_end = (pos + from.size() >= str.size() ||
                                     !std::isalnum(static_cast<unsigned char>(str[pos + from.size()])));
                    if (word_start && word_end) {
                        str.replace(pos, from.size(), to);
                        pos += to.size();
                    } else {
                        pos += from.size();
                    }
                }
            };

            replace_all(result, "pi", "3.14159265358979f");
            replace_all(result, "inf", "std::numeric_limits<float>::infinity()");
            replace_all(result, "eps", "std::numeric_limits<float>::epsilon()");

            // Check if it's a pure identifier (workspace variable)
            bool is_identifier = !result.empty() && std::isalpha(static_cast<unsigned char>(result[0]));
            for (char c : result) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                    is_identifier = false;
                    break;
                }
            }

            if (is_identifier) {
                return "cfg." + result;
            }

            return result;
        }

    };

} // namespace oc::codegen
