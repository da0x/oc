//
// Open Controls - YAML Schema Writer
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
#include <array>

namespace oc::yaml {

    struct signal_def {
        std::string name;
        std::string description;
        std::string type = "float";
        int array_size = 0;
        std::string default_value;
        std::string units;
    };

    struct function_schema {
        std::string name;
        std::vector<signal_def> inputs;
        std::vector<signal_def> outputs;
        std::vector<signal_def> state;
        std::vector<signal_def> config;
    };

    struct element_schema {
        std::string name;
        std::string description;
        std::string parent_library;

        std::vector<signal_def> inputs;
        std::vector<signal_def> config;
        std::vector<signal_def> outputs;
        std::vector<signal_def> state;
        std::vector<function_schema> functions;
    };

    class writer {
    public:
        [[nodiscard]] auto write(const element_schema& schema) const -> std::string {
            std::ostringstream out;

            out << "---\n";
            out << "metadata:\n";
            out << "    name: " << schema.name << "\n";
            out << "    type: A\n";
            out << "    revision: 0\n";
            out << "    format_version: 0.0\n";
            out << "    description: '" << escape_yaml(schema.description) << "'\n";
            out << "    parent_library: '" << schema.parent_library << "'\n";
            out << "    category: 'element'\n";
            out << "\n";

            if (!schema.inputs.empty()) {
                out << "IN:\n";
                out << "    use: inputs_group\n";
                out << "    signals:\n";
                write_signals(out, schema.inputs, 8);
                out << "\n";
            }

            if (!schema.config.empty()) {
                out << "CONFIG:\n";
                out << "    use: config_group\n";
                out << "    description: 'Configuration parameters'\n";
                out << "    signals:\n";
                write_signals(out, schema.config, 8);
                out << "\n";
            }

            if (!schema.outputs.empty()) {
                out << "OUT:\n";
                out << "    use: outputs_group\n";
                out << "    signals:\n";
                write_signals(out, schema.outputs, 8);
                out << "\n";
            }

            if (!schema.state.empty()) {
                out << "STATE:\n";
                out << "    use: state_group\n";
                out << "    signals:\n";
                write_signals(out, schema.state, 8);
                out << "\n";
            }

            if (!schema.functions.empty()) {
                out << "FUNCTIONS:\n";
                for (const auto& func : schema.functions) {
                    write_function(out, func, 4);
                }
                out << "\n";
            }

            return out.str();
        }

    private:
        static void write_signals(std::ostringstream& out, const std::vector<signal_def>& signals, int indent) {
            std::string indent_str(static_cast<std::size_t>(indent), ' ');

            for (const auto& sig : signals) {
                out << indent_str << sig.name << ":\n";
                out << indent_str << "    description: '" << escape_yaml(sig.description) << "'\n";
                out << indent_str << "    type: " << sig.type << "\n";
                if (sig.array_size > 0) {
                    out << indent_str << "    array: " << sig.array_size << "\n";
                }
                if (!sig.default_value.empty()) {
                    out << indent_str << "    default: " << sig.default_value << "\n";
                }
                if (!sig.units.empty()) {
                    out << indent_str << "    units: '" << sig.units << "'\n";
                }
            }
        }

        static void write_function(std::ostringstream& out, const function_schema& func, int indent) {
            std::string indent_str(static_cast<std::size_t>(indent), ' ');

            out << indent_str << func.name << ":\n";

            if (!func.inputs.empty()) {
                out << indent_str << "    IN:\n";
                for (const auto& sig : func.inputs) {
                    out << indent_str << "        " << sig.name << ": { type: " << sig.type;
                    if (!sig.default_value.empty()) {
                        out << ", default: " << sig.default_value;
                    }
                    out << " }\n";
                }
            }

            if (!func.outputs.empty()) {
                out << indent_str << "    OUT:\n";
                for (const auto& sig : func.outputs) {
                    out << indent_str << "        " << sig.name << ": { type: " << sig.type;
                    if (!sig.default_value.empty()) {
                        out << ", default: " << sig.default_value;
                    }
                    out << " }\n";
                }
            }

            if (!func.state.empty()) {
                out << indent_str << "    STATE:\n";
                for (const auto& sig : func.state) {
                    out << indent_str << "        " << sig.name << ": { type: " << sig.type;
                    if (!sig.default_value.empty()) {
                        out << ", default: " << sig.default_value;
                    }
                    out << " }\n";
                }
            }

            if (!func.config.empty()) {
                out << indent_str << "    CONFIG:\n";
                for (const auto& sig : func.config) {
                    out << indent_str << "        " << sig.name << ": { type: " << sig.type;
                    if (!sig.default_value.empty()) {
                        out << ", default: " << sig.default_value;
                    }
                    out << " }\n";
                }
            }
        }

        [[nodiscard]] static auto escape_yaml(std::string_view str) -> std::string {
            std::string result;
            for (char c : str) {
                if (c == '\'') {
                    result += "''";
                } else {
                    result += c;
                }
            }
            return result;
        }
    };

    class converter {
        const mdl::model* model_ = nullptr;

    public:
        void set_model(const mdl::model* m) { model_ = m; }

        [[nodiscard]] auto convert(const mdl::system& sys, std::string_view library_name = "imported") -> element_schema {
            element_schema schema;
            schema.name = sanitize_name(sys.name.empty() ? sys.id : sys.name);
            schema.parent_library = std::string(library_name);
            schema.description = "Imported from Simulink subsystem " + sys.id;

            // Extract inputs
            auto inports = sys.inports();
            std::ranges::sort(inports, [](const mdl::block& a, const mdl::block& b) {
                int port_a = 1, port_b = 1;
                if (auto pa = a.param("Port")) port_a = std::stoi(*pa);
                if (auto pb = b.param("Port")) port_b = std::stoi(*pb);
                return port_a < port_b;
            });

            for (const auto& inp : inports) {
                signal_def sig;
                sig.name = sanitize_name(inp.name);
                sig.description = "Input port " + inp.name;
                sig.type = "float";
                sig.default_value = "0.0f";

                if (auto bracket = inp.name.find('['); bracket != std::string::npos) {
                    if (auto close = inp.name.find(']', bracket); close != std::string::npos) {
                        sig.array_size = std::stoi(inp.name.substr(bracket + 1, close - bracket - 1));
                        sig.name = sanitize_name(inp.name.substr(0, bracket));
                    }
                }

                schema.inputs.push_back(std::move(sig));
            }

            // Extract outputs
            for (const auto& outp : sys.outports()) {
                signal_def sig;
                sig.name = sanitize_name(outp.name);
                sig.description = "Output port " + outp.name;
                sig.type = "float";
                sig.default_value = "0.0f";

                if (auto bracket = outp.name.find('['); bracket != std::string::npos) {
                    if (auto close = outp.name.find(']', bracket); close != std::string::npos) {
                        sig.array_size = std::stoi(outp.name.substr(bracket + 1, close - bracket - 1));
                        sig.name = sanitize_name(outp.name.substr(0, bracket));
                    }
                }

                schema.outputs.push_back(std::move(sig));
            }

            // Extract config and state
            std::set<std::string> seen_params;
            extract_config_recursive(sys, schema.config, schema.state, seen_params, 0);

            // Generate functions using codegen
            if (model_) {
                codegen::generator gen;
                gen.set_model(model_);
                auto parts = gen.generate_parts(sys, "");

                for (const auto& func : parts.functions) {
                    collect_functions_flat(func, schema.functions);
                }
            }

            return schema;
        }

    private:
        // Flatten the function hierarchy into a list of function schemas
        static void collect_functions_flat(const codegen::generated_function& func,
                                           std::vector<function_schema>& out) {
            // Collect children first (depth-first)
            for (const auto& child : func.child_functions) {
                collect_functions_flat(child, out);
            }

            function_schema fs;
            fs.name = func.name;

            for (const auto& [name, type] : func.inports) {
                signal_def sig;
                sig.name = name;
                sig.type = type;
                sig.default_value = "0.0f";
                fs.inputs.push_back(std::move(sig));
            }

            for (const auto& [name, type] : func.outports) {
                signal_def sig;
                sig.name = name;
                sig.type = type;
                sig.default_value = "0.0f";
                fs.outputs.push_back(std::move(sig));
            }

            for (const auto& [name, comment] : func.state_vars) {
                signal_def sig;
                sig.name = name;
                sig.description = comment;
                sig.type = (comment == "function state") ? name + "_state" : "float";
                sig.default_value = (comment == "function state") ? "" : "0.0f";
                fs.state.push_back(std::move(sig));
            }

            for (const auto& var : func.config_vars) {
                signal_def sig;
                sig.name = var;
                sig.type = "float";
                sig.default_value = "0.0f";
                fs.config.push_back(std::move(sig));
            }

            // Always add dt to config
            signal_def dt_sig;
            dt_sig.name = "dt";
            dt_sig.type = "float";
            dt_sig.default_value = "0.001";
            fs.config.push_back(std::move(dt_sig));

            out.push_back(std::move(fs));
        }

        void extract_config_recursive(const mdl::system& sys,
                                       std::vector<signal_def>& config,
                                       std::vector<signal_def>& state,
                                       std::set<std::string>& seen_params,
                                       int depth) const {
            if (depth > 10) return;

            for (const auto& blk : sys.blocks) {
                for (const auto& mp : blk.mask_parameters) {
                    if (seen_params.contains(mp.name)) continue;
                    seen_params.insert(mp.name);

                    signal_def sig;
                    sig.name = mp.name;
                    sig.description = mp.prompt.empty() ? mp.name : mp.prompt;
                    sig.type = "float";
                    sig.default_value = mp.value.empty() ? "0.0f" : mp.value;
                    config.push_back(std::move(sig));
                }

                extract_block_params(blk, config, seen_params);

                if (blk.type == "UnitDelay" || blk.type == "Integrator" ||
                    blk.type == "DiscreteIntegrator" || blk.type == "Memory") {
                    auto state_name = sanitize_name(blk.name) + "_state";
                    if (!seen_params.contains(state_name)) {
                        seen_params.insert(state_name);
                        signal_def sig;
                        sig.name = state_name;
                        sig.description = "State for " + blk.name;
                        sig.type = "float";
                        sig.default_value = "0.0f";
                        state.push_back(std::move(sig));
                    }
                }

                if (blk.is_subsystem() && !blk.subsystem_ref.empty() && model_) {
                    if (auto subsys = model_->get_system(blk.subsystem_ref)) {
                        extract_config_recursive(*subsys, config, state, seen_params, depth + 1);
                    }
                }
            }
        }

        [[nodiscard]] static auto sanitize_name(std::string_view name) -> std::string {
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

        [[nodiscard]] static auto is_matlab_builtin(std::string_view name) -> bool {
            static const std::set<std::string> builtins = {
                "sqrt", "exp", "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
                "sinh", "cosh", "tanh", "abs", "floor", "ceil", "round", "mod", "rem", "sign",
                "max", "min", "sum", "prod", "mean", "std", "var",
                "real", "imag", "conj", "angle", "complex",
                "pi", "inf", "nan", "eps", "i", "j", "true", "false",
                "zeros", "ones", "eye", "rand", "randn",
                "length", "size", "numel", "reshape", "transpose",
                "on", "off", "auto"
            };
            return builtins.contains(std::string(name));
        }

        [[nodiscard]] static auto extract_workspace_vars(std::string_view expr) -> std::vector<std::string> {
            std::vector<std::string> vars;
            std::string current;

            for (char c : expr) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    current += c;
                } else {
                    if (!current.empty() &&
                        std::isalpha(static_cast<unsigned char>(current[0])) &&
                        !is_matlab_builtin(current)) {
                        vars.push_back(current);
                    }
                    current.clear();
                }
            }

            if (!current.empty() &&
                std::isalpha(static_cast<unsigned char>(current[0])) &&
                !is_matlab_builtin(current)) {
                vars.push_back(current);
            }

            return vars;
        }

        void extract_block_params(const mdl::block& blk,
                                   std::vector<signal_def>& config,
                                   std::set<std::string>& seen_params) const {
            static constexpr std::array param_names = {
                "Gain", "UpperLimit", "LowerLimit", "Value", "InitialCondition",
                "SampleTime", "Threshold", "OnSwitchValue", "OffSwitchValue"
            };

            for (const auto& pname : param_names) {
                if (auto val = blk.param(pname); val && !val->empty()) {
                    for (const auto& var : extract_workspace_vars(*val)) {
                        if (seen_params.contains(var)) continue;
                        seen_params.insert(var);

                        signal_def sig;
                        sig.name = var;
                        sig.description = "Workspace variable used in " + blk.name + "." + std::string(pname);
                        sig.type = "float";
                        sig.default_value = "0.0f";
                        config.push_back(std::move(sig));
                    }
                }
            }

            for (const auto& mp : blk.mask_parameters) {
                for (const auto& var : extract_workspace_vars(mp.value)) {
                    if (seen_params.contains(var)) continue;
                    seen_params.insert(var);

                    signal_def sig;
                    sig.name = var;
                    sig.description = "Workspace variable used in " + blk.name + "." + mp.name;
                    sig.type = "float";
                    sig.default_value = "0.0f";
                    config.push_back(std::move(sig));
                }
            }
        }
    };

} // namespace oc::yaml
