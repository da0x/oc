//
// Open Controls - Block Diagram Generator (OC source → Simulink block diagram)
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include "../liboc/oc_parser.hpp"
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <algorithm>

namespace oc {

    // ─── IR types for block diagram ─────────────────────────────────────────────

    struct ir_block {
        int sid = 0;
        std::string type;            // Simulink BlockType
        std::string name;            // Simulink Name
        int port_in = 0;
        int port_out = 0;
        std::map<std::string, std::string> parameters;
        std::string subsystem_ref;   // for SubSystem blocks
        std::vector<int> position;   // [x1, y1, x2, y2]
    };

    struct ir_connection {
        int src_sid = 0;
        int src_port = 0;
        int dst_sid = 0;
        int dst_port = 0;
    };

    struct ir_branch {
        int dst_sid = 0;
        int dst_port = 0;
    };

    struct ir_line {
        int src_sid = 0;
        int src_port = 0;
        std::vector<ir_branch> branches;  // if >1, use Branch elements
    };

    struct generated_system {
        std::string system_xml;
        std::vector<std::string> child_system_xmls;  // for component subsystems
        std::vector<std::string> child_system_ids;    // system_N ids
        int sid_highwatermark = 0;
    };

    // ─── Block Diagram Generator ────────────────────────────────────────────────

    class block_diagram_generator {
    public:

        // Generate a system XML for an element, using raw source to extract blocks
        [[nodiscard]] auto generate(
            const parser::oc_element& elem,
            const std::vector<parser::oc_component>& components,
            const std::string& raw_source,
            int& sys_counter) -> generated_system
        {
            generated_system result;

            // Extract the update body from raw source (with comments intact)
            auto body_lines = extract_update_body(raw_source, elem.name, "element");

            // Parse lines into blocks and track variable definitions
            std::vector<ir_block> blocks;
            std::vector<ir_connection> connections;
            std::map<std::string, std::pair<int, int>> var_map;  // var -> (sid, port)

            int sid = 1;

            // Phase 1: Create Inport blocks
            for (const auto& sec : elem.sections) {
                if (sec.kind != "input") continue;
                int port_num = 1;
                for (const auto& var : sec.variables) {
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = "Inport";
                    blk.name = var.name;
                    blk.port_out = 1;
                    if (port_num > 1) {
                        blk.parameters["Port"] = std::to_string(port_num);
                    }
                    blocks.push_back(blk);
                    // in.X -> this inport
                    var_map["in." + var.name] = {blk.sid, 1};
                    ++port_num;
                }
            }

            // Phase 2: Parse update body and create functional blocks
            parse_update_body(body_lines, elem, components, blocks, connections, var_map, sid, sys_counter, result, raw_source);

            // Phase 3: Create Outport blocks and connections
            int out_port_num = 1;
            auto output_assignments = extract_output_assignments(body_lines);
            for (const auto& sec : elem.sections) {
                if (sec.kind != "output") continue;
                for (const auto& var : sec.variables) {
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = "Outport";
                    blk.name = var.name;
                    blk.port_in = 1;
                    if (out_port_num > 1) {
                        blk.parameters["Port"] = std::to_string(out_port_num);
                    }
                    blocks.push_back(blk);

                    // Find source for this output
                    if (auto it = output_assignments.find(var.name); it != output_assignments.end()) {
                        auto src_var = it->second;
                        if (auto vit = var_map.find(src_var); vit != var_map.end()) {
                            connections.push_back({vit->second.first, vit->second.second, blk.sid, 1});
                        }
                    }
                    ++out_port_num;
                }
            }

            // Phase 4: Auto-layout
            auto_layout(blocks, connections);

            // Phase 5: Emit system XML
            int highwatermark = sid - 1;
            result.system_xml = emit_system_xml(blocks, connections, highwatermark);
            result.sid_highwatermark = highwatermark;

            return result;
        }

        // Generate for a component (same logic, different section source)
        [[nodiscard]] auto generate_component(
            const parser::oc_component& comp,
            const std::vector<parser::oc_component>& all_components,
            const std::string& raw_source,
            int& sys_counter) -> generated_system
        {
            generated_system result;

            auto body_lines = extract_update_body(raw_source, comp.name, "component");

            std::vector<ir_block> blocks;
            std::vector<ir_connection> connections;
            std::map<std::string, std::pair<int, int>> var_map;

            int sid = 1;

            // Create Inport blocks
            for (const auto& sec : comp.sections) {
                if (sec.kind != "input") continue;
                int port_num = 1;
                for (const auto& var : sec.variables) {
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = "Inport";
                    blk.name = var.name;
                    blk.port_out = 1;
                    if (port_num > 1) {
                        blk.parameters["Port"] = std::to_string(port_num);
                    }
                    blocks.push_back(blk);
                    var_map["in." + var.name] = {blk.sid, 1};
                    ++port_num;
                }
            }

            // Parse body - reuse element parser with a temporary oc_element
            parser::oc_element tmp_elem;
            tmp_elem.name = comp.name;
            tmp_elem.sections = comp.sections;
            tmp_elem.update = comp.update;

            parse_update_body(body_lines, tmp_elem, all_components, blocks, connections, var_map, sid, sys_counter, result, raw_source);

            // Create Outport blocks
            int out_port_num = 1;
            auto output_assignments = extract_output_assignments(body_lines);
            for (const auto& sec : comp.sections) {
                if (sec.kind != "output") continue;
                for (const auto& var : sec.variables) {
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = "Outport";
                    blk.name = var.name;
                    blk.port_in = 1;
                    if (out_port_num > 1) {
                        blk.parameters["Port"] = std::to_string(out_port_num);
                    }
                    blocks.push_back(blk);

                    if (auto it = output_assignments.find(var.name); it != output_assignments.end()) {
                        auto src_var = it->second;
                        if (auto vit = var_map.find(src_var); vit != var_map.end()) {
                            connections.push_back({vit->second.first, vit->second.second, blk.sid, 1});
                        }
                    }
                    ++out_port_num;
                }
            }

            auto_layout(blocks, connections);

            int highwatermark = sid - 1;
            result.system_xml = emit_system_xml(blocks, connections, highwatermark);
            result.sid_highwatermark = highwatermark;

            return result;
        }

    private:

        // ─── Phase 1: Extract update body from raw source ─────────────────────

        [[nodiscard]] auto extract_update_body(
            const std::string& raw_source,
            const std::string& entity_name,
            const std::string& entity_kind) -> std::vector<std::string>
        {
            std::vector<std::string> result;
            std::istringstream stream(raw_source);
            std::string line;

            // Find "element EntityName {" or "component EntityName {"
            bool found_entity = false;
            bool found_update = false;
            int brace_depth = 0;

            while (std::getline(stream, line)) {
                if (!found_entity) {
                    // Look for entity declaration
                    auto trimmed = trim(line);
                    if (trimmed.starts_with(entity_kind + " " + entity_name + " {") ||
                        trimmed.starts_with(entity_kind + " " + entity_name + "{") ||
                        trimmed == entity_kind + " " + entity_name) {
                        found_entity = true;
                        brace_depth = 0;
                        // Count braces on this line
                        for (char c : line) {
                            if (c == '{') ++brace_depth;
                            if (c == '}') --brace_depth;
                        }
                    }
                    continue;
                }

                if (!found_update) {
                    // Track brace depth to find update block
                    auto trimmed = trim(line);
                    for (char c : line) {
                        if (c == '{') ++brace_depth;
                        if (c == '}') --brace_depth;
                    }
                    if (trimmed.starts_with("update {") || trimmed.starts_with("update{") || trimmed == "update") {
                        found_update = true;
                        // Reset brace depth for update body tracking
                        brace_depth = 1;  // we're inside the update block
                    }
                    continue;
                }

                // Inside update body - track braces
                int delta = 0;
                for (char c : line) {
                    if (c == '{') ++delta;
                    if (c == '}') --delta;
                }
                brace_depth += delta;

                if (brace_depth <= 0) {
                    break;  // End of update block
                }

                result.push_back(line);
            }

            return result;
        }

        // ─── Phase 2: Parse update body lines into blocks ─────────────────────

        void parse_update_body(
            const std::vector<std::string>& lines,
            const parser::oc_element& elem,
            const std::vector<parser::oc_component>& components,
            std::vector<ir_block>& blocks,
            std::vector<ir_connection>& connections,
            std::map<std::string, std::pair<int, int>>& var_map,
            int& sid,
            int& sys_counter,
            generated_system& result,
            const std::string& raw_source = {})
        {
            // ── Pre-scan: register state variables as forward-references ──
            // Integrator/UnitDelay outputs represent previous-timestep values
            // and are available before the block processes its input.
            // We reserve SIDs for them now so references resolve during parsing.

            struct prescan_state_var {
                std::string state_key;     // "state.X_state"
                std::string display_name;  // "X" (without _state suffix)
                bool is_integrator;        // true=Integrator, false=UnitDelay
                int reserved_sid;
            };
            std::vector<prescan_state_var> state_vars;

            // Also pre-scan TransferFcn scoped blocks for input variable and coefficients
            struct prescan_tf {
                std::string input_var;     // variable feeding the TransferFcn (from u_n = ...)
                std::string numerator;     // Simulink Numerator parameter
                std::string denominator;   // Simulink Denominator parameter
            };
            std::map<std::string, prescan_tf> tf_data;  // keyed by block name from comment

            {
                std::string scan_block_type;
                std::string scan_block_name;
                bool in_tf_scope = false;
                int tf_brace_depth = 0;
                std::string tf_name;
                std::string tf_u_n;
                double tf_b0_coeff = 0, tf_b1_coeff = 0;  // numerator k-multipliers
                double tf_a0_coeff = 0, tf_a1_coeff = 0;  // denominator k-multipliers

                for (const auto& line : lines) {
                    auto t = trim(line);
                    if (t.empty()) continue;

                    if (t.starts_with("//")) {
                        auto comment = trim(t.substr(2));
                        if (comment == "Outputs") break;
                        // Skip secondary TransferFcn comment (order line)
                        if (comment.starts_with("TransferFcn:") && scan_block_type == "TransferFcn") {
                            continue;
                        }
                        auto colon = comment.find(':');
                        if (colon != std::string::npos) {
                            scan_block_type = trim(comment.substr(0, colon));
                            scan_block_name = xml_decode(trim(comment.substr(colon + 1)));
                        }
                        continue;
                    }

                    // Track TransferFcn scoped block
                    if (in_tf_scope) {
                        for (char c : t) {
                            if (c == '{') ++tf_brace_depth;
                            if (c == '}') --tf_brace_depth;
                        }
                        // Extract u_n input: "float u_n = EXPR;"
                        if (t.starts_with("float u_n = ")) {
                            auto eq = t.find('=');
                            auto val = trim(t.substr(eq + 1));
                            if (val.back() == ';') val.pop_back();
                            tf_u_n = trim(val);
                        }
                        // Extract bilinear transform coefficients
                        // Pattern: float b0_d = COEFF * k + CONST;
                        auto extract_coeff = [&](const std::string& prefix) -> double {
                            if (!t.starts_with(prefix)) return 0.0;
                            auto eq = t.find('=');
                            if (eq == std::string::npos) return 0.0;
                            auto val = trim(t.substr(eq + 1));
                            if (val.back() == ';') val.pop_back();
                            // Pattern: "COEFF * k + CONST" -> extract COEFF
                            auto star_k = val.find(" * k");
                            if (star_k != std::string::npos) {
                                auto coeff_str = trim(val.substr(0, star_k));
                                try { return std::stod(coeff_str); } catch (...) { return 0.0; }
                            }
                            return 0.0;
                        };
                        if (t.starts_with("float b0_d")) tf_b0_coeff = extract_coeff("float b0_d = ");
                        if (t.starts_with("float b1_d")) tf_b1_coeff = extract_coeff("float b1_d = ");
                        if (t.starts_with("float a0_d")) tf_a0_coeff = extract_coeff("float a0_d = ");
                        if (t.starts_with("float a1_d")) tf_a1_coeff = extract_coeff("float a1_d = ");

                        if (tf_brace_depth <= 0) {
                            // End of scoped block - store results
                            prescan_tf ptf;
                            ptf.input_var = tf_u_n;
                            // Reconstruct continuous-time TF from bilinear coefficients:
                            // num_s = b0_coeff, den_s = a0_coeff
                            // Numerator = [num_s 1] if num_s != 0, else [1]
                            // Denominator = [den_s 1]
                            if (tf_b0_coeff != 0.0) {
                                ptf.numerator = "[" + format_coeff(tf_b0_coeff) + " 1]";
                            } else {
                                ptf.numerator = "[1]";
                            }
                            ptf.denominator = "[" + format_coeff(tf_a0_coeff) + " 1]";
                            tf_data[tf_name] = ptf;
                            in_tf_scope = false;
                            scan_block_type.clear();
                            scan_block_name.clear();
                        }
                        continue;
                    }

                    if (t == "{" && scan_block_type == "TransferFcn") {
                        in_tf_scope = true;
                        tf_brace_depth = 1;
                        tf_name = scan_block_name;
                        tf_u_n.clear();
                        tf_b0_coeff = tf_b1_coeff = tf_a0_coeff = tf_a1_coeff = 0.0;
                        continue;
                    }

                    // Pre-register Integrator state outputs
                    if (t.starts_with("state.") && t.find("+=") != std::string::npos &&
                        t.find("* cfg.dt") != std::string::npos && scan_block_type == "Integrator") {
                        auto dot = t.find('.');
                        auto plus = t.find("+=");
                        auto state_var = trim(t.substr(dot + 1, plus - dot - 1));
                        auto display = state_var;
                        if (display.ends_with("_state")) display = display.substr(0, display.size() - 6);
                        int rsid = sid++;
                        state_vars.push_back({"state." + state_var, display, true, rsid});
                        var_map["state." + state_var] = {rsid, 1};
                        scan_block_type.clear();
                    }

                    // Pre-register UnitDelay state outputs
                    if (t.starts_with("state.") && t.find("= ") != std::string::npos &&
                        t.find("+=") == std::string::npos && t.find("_tf_") == std::string::npos &&
                        scan_block_type == "UnitDelay") {
                        auto dot = t.find('.');
                        auto eq = t.find('=');
                        auto state_var = trim(t.substr(dot + 1, eq - dot - 1));
                        auto display = state_var;
                        if (display.ends_with("_state")) display = display.substr(0, display.size() - 6);
                        int rsid = sid++;
                        state_vars.push_back({"state." + state_var, display, false, rsid});
                        var_map["state." + state_var] = {rsid, 1};
                        scan_block_type.clear();
                    }
                }
            }

            // ── Main parse pass ──

            std::string pending_comment;
            std::string pending_block_type;
            std::string pending_block_name;

            for (std::size_t i = 0; i < lines.size(); ++i) {
                auto trimmed = trim(lines[i]);
                if (trimmed.empty()) continue;

                // Check for block comment: // BlockType: BlockName
                if (trimmed.starts_with("//")) {
                    auto comment = trim(trimmed.substr(2));

                    // Skip secondary TransferFcn comment (order line)
                    if (comment.starts_with("TransferFcn:") && !pending_block_type.empty() && pending_block_type == "TransferFcn") {
                        continue;
                    }

                    // Check for "Outputs" comment
                    if (comment == "Outputs") {
                        break;  // Stop processing blocks
                    }

                    auto colon = comment.find(':');
                    if (colon != std::string::npos) {
                        pending_block_type = trim(comment.substr(0, colon));
                        pending_block_name = xml_decode(trim(comment.substr(colon + 1)));

                        // Handle Demux: comment-only block (no code line)
                        if (pending_block_type == "Demux") {
                            ir_block blk;
                            blk.sid = sid++;
                            blk.type = "Demux";
                            blk.name = pending_block_name;
                            blk.port_in = 1;
                            blk.port_out = 2;
                            blk.parameters["Outputs"] = "2";
                            blocks.push_back(blk);
                            pending_block_type.clear();
                            pending_block_name.clear();
                        }
                    }
                    continue;
                }

                // Skip TransferFcn scoped block internals and braces
                if (trimmed == "{" || trimmed == "}") continue;
                if (trimmed.starts_with("float ")) continue;
                if (trimmed.starts_with("state.") && trimmed.find("_tf_") != std::string::npos) {
                    continue;  // Skip all TransferFcn state updates
                }

                // Parse: auto VarName = expression;
                if (trimmed.starts_with("auto ")) {
                    auto eq_pos = trimmed.find('=');
                    if (eq_pos == std::string::npos) continue;

                    auto var_name = trim(trimmed.substr(5, eq_pos - 5));
                    auto expr = trim(trimmed.substr(eq_pos + 1));
                    if (expr.back() == ';') expr.pop_back();
                    expr = trim(expr);

                    if (pending_block_type == "Component call") {
                        // This is a component output extraction: auto CompName_outN = CompName_out.field;
                        // Already handled by component call parser below
                        continue;
                    }

                    if (pending_block_type.empty()) continue;

                    // Special handling for TransferFcn: use pre-scanned data
                    if (pending_block_type == "TransferFcn") {
                        ir_block blk;
                        blk.sid = sid++;
                        blk.type = "TransferFcn";
                        blk.name = pending_block_name;
                        blk.port_in = 1;
                        blk.port_out = 1;

                        // Use pre-scanned input variable and coefficients
                        if (auto it = tf_data.find(pending_block_name); it != tf_data.end()) {
                            resolve_input(it->second.input_var, var_map, connections, blk.sid, 1);
                            blk.parameters["Numerator"] = it->second.numerator;
                            blk.parameters["Denominator"] = it->second.denominator;
                        } else {
                            // Fallback: try to resolve from expression
                            resolve_input(expr, var_map, connections, blk.sid, 1);
                        }

                        blocks.push_back(blk);
                        var_map[var_name] = {blk.sid, 1};

                        pending_block_type.clear();
                        pending_block_name.clear();
                        continue;
                    }

                    // Create block based on type
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = pending_block_type;
                    blk.name = pending_block_name;

                    create_block_from_type(blk, expr, var_map, blocks, connections, elem);

                    blocks.push_back(blk);
                    var_map[var_name] = {blk.sid, 1};

                    pending_block_type.clear();
                    pending_block_name.clear();
                    continue;
                }

                // Handle Integrator: state.X += input * cfg.dt;
                if (trimmed.starts_with("state.") && trimmed.find("+=") != std::string::npos &&
                    trimmed.find("* cfg.dt") != std::string::npos) {

                    if (pending_block_type == "Integrator") {
                        auto dot_pos = trimmed.find('.');
                        auto plus_pos = trimmed.find("+=");
                        auto state_var = trimmed.substr(dot_pos + 1, plus_pos - dot_pos - 1);
                        state_var = trim(state_var);

                        // Find the pre-reserved SID for this state variable
                        int blk_sid = 0;
                        for (auto& sv : state_vars) {
                            if (sv.state_key == "state." + state_var && sv.is_integrator) {
                                blk_sid = sv.reserved_sid;
                                break;
                            }
                        }
                        if (blk_sid == 0) blk_sid = sid++;  // fallback

                        ir_block blk;
                        blk.sid = blk_sid;
                        blk.type = "Integrator";
                        blk.name = pending_block_name;
                        blk.port_in = 1;
                        blk.port_out = 1;

                        // Extract input expression (between += and * cfg.dt)
                        auto input_expr = trimmed.substr(plus_pos + 2);
                        auto dt_pos = input_expr.find("* cfg.dt");
                        if (dt_pos != std::string::npos) {
                            input_expr = trim(input_expr.substr(0, dt_pos));
                        }

                        resolve_input(input_expr, var_map, connections, blk.sid, 1);
                        blocks.push_back(blk);

                        pending_block_type.clear();
                        pending_block_name.clear();
                    }
                    continue;
                }

                // Handle UnitDelay: state.X = input;  // update for next step
                if (trimmed.starts_with("state.") && trimmed.find("= ") != std::string::npos &&
                    trimmed.find("+=") == std::string::npos &&
                    trimmed.find("_tf_") == std::string::npos) {

                    if (pending_block_type == "UnitDelay") {
                        auto dot_pos = trimmed.find('.');
                        auto eq_pos = trimmed.find('=');
                        auto state_var = trimmed.substr(dot_pos + 1, eq_pos - dot_pos - 1);
                        state_var = trim(state_var);

                        auto expr = trimmed.substr(eq_pos + 1);
                        if (expr.back() == ';') expr.pop_back();
                        auto comment_pos = expr.find("//");
                        if (comment_pos != std::string::npos) {
                            expr = expr.substr(0, comment_pos);
                        }
                        expr = trim(expr);

                        // Find the pre-reserved SID
                        int blk_sid = 0;
                        for (auto& sv : state_vars) {
                            if (sv.state_key == "state." + state_var && !sv.is_integrator) {
                                blk_sid = sv.reserved_sid;
                                break;
                            }
                        }
                        if (blk_sid == 0) blk_sid = sid++;  // fallback

                        ir_block blk;
                        blk.sid = blk_sid;
                        blk.type = "UnitDelay";
                        blk.name = pending_block_name;
                        blk.port_in = 1;
                        blk.port_out = 1;

                        resolve_input(expr, var_map, connections, blk.sid, 1);

                        blocks.push_back(blk);
                        var_map["state." + state_var] = {blk.sid, 1};

                        pending_block_type.clear();
                        pending_block_name.clear();
                    }
                    continue;
                }

                // Handle Component call
                if (pending_block_type == "Component call") {
                    // Component call pattern spans multiple lines:
                    // CompType_input CompName_in{.field = val, ...};
                    // CompType_output CompName_out{};
                    // CompType_update(CompName_in, CompType_config{...}, state.CompName, CompName_out);
                    // auto CompName_outN = CompName_out.field;
                    // ... (more outputs)

                    auto comp_display_name = pending_block_name;
                    // Find the component type from the input struct line
                    // Pattern: TypeName_input VarName_in{...};
                    auto input_line = trimmed;
                    auto underscore_input = input_line.find("_input ");
                    if (underscore_input == std::string::npos) {
                        // Not the expected pattern, skip
                        pending_block_type.clear();
                        pending_block_name.clear();
                        continue;
                    }
                    auto comp_type = input_line.substr(0, underscore_input);

                    // Find the matching component definition
                    const parser::oc_component* comp_def = nullptr;
                    for (const auto& c : components) {
                        if (c.name == comp_type) {
                            comp_def = &c;
                            break;
                        }
                    }

                    // Count input and output ports from component definition
                    int in_count = 0, out_count = 0;
                    if (comp_def) {
                        for (const auto& sec : comp_def->sections) {
                            if (sec.kind == "input") in_count = static_cast<int>(sec.variables.size());
                            if (sec.kind == "output") out_count = static_cast<int>(sec.variables.size());
                        }
                    }

                    // Parse input assignments from the input struct
                    auto brace_start = input_line.find('{');
                    auto brace_end = input_line.rfind('}');
                    std::vector<std::string> input_values;
                    if (brace_start != std::string::npos && brace_end != std::string::npos) {
                        auto fields_str = input_line.substr(brace_start + 1, brace_end - brace_start - 1);
                        // Parse .field = value pairs
                        std::size_t pos = 0;
                        while (pos < fields_str.size()) {
                            auto dot = fields_str.find('.', pos);
                            if (dot == std::string::npos) break;
                            auto eq = fields_str.find('=', dot);
                            if (eq == std::string::npos) break;
                            auto comma = fields_str.find(',', eq);
                            if (comma == std::string::npos) comma = fields_str.size();
                            auto val = trim(fields_str.substr(eq + 1, comma - eq - 1));
                            input_values.push_back(val);
                            pos = comma + 1;
                        }
                    }

                    // Create SubSystem block
                    ir_block blk;
                    blk.sid = sid++;
                    blk.type = "SubSystem";
                    blk.name = comp_display_name;
                    blk.port_in = std::max(in_count, static_cast<int>(input_values.size()));
                    blk.port_out = std::max(out_count, 1);

                    // Generate child system for this component
                    if (comp_def) {
                        int child_sys_id = ++sys_counter;
                        blk.subsystem_ref = "system_" + std::to_string(child_sys_id);

                        auto child_result = generate_component_system(*comp_def, components, raw_source, sys_counter);
                        result.child_system_xmls.push_back(child_result.system_xml);
                        result.child_system_ids.push_back(std::to_string(child_sys_id));

                        // Recursively add grandchild systems
                        for (std::size_t ci = 0; ci < child_result.child_system_xmls.size(); ++ci) {
                            result.child_system_xmls.push_back(child_result.child_system_xmls[ci]);
                            result.child_system_ids.push_back(child_result.child_system_ids[ci]);
                        }
                    }

                    // Connect inputs
                    for (int p = 0; p < static_cast<int>(input_values.size()); ++p) {
                        resolve_input(input_values[p], var_map, connections, blk.sid, p + 1);
                    }

                    blocks.push_back(blk);

                    // Skip remaining component call lines (output struct, update call)
                    ++i;  // skip output struct line
                    if (i < lines.size()) ++i;  // skip update call line

                    // Parse output extractions: auto CompName_outN = CompName_out.field;
                    int out_port = 1;
                    while (i < lines.size()) {
                        auto next = trim(lines[i]);
                        // Check if this is an output extraction for this component
                        // Pattern: auto CompType_outN = CompType_out.field;
                        if (next.starts_with("auto ") && next.find(comp_type + "_out") != std::string::npos) {
                            auto eq = next.find('=');
                            if (eq != std::string::npos) {
                                auto out_var = trim(next.substr(5, eq - 5));
                                var_map[out_var] = {blk.sid, out_port};
                                ++out_port;
                            }
                            ++i;
                        } else {
                            --i;  // back up, this line belongs to next block
                            break;
                        }
                    }

                    pending_block_type.clear();
                    pending_block_name.clear();
                    continue;
                }
            }
        }

        // Generate a child system for a component call
        [[nodiscard]] auto generate_component_system(
            const parser::oc_component& comp,
            const std::vector<parser::oc_component>& all_components,
            const std::string& raw_source,
            int& sys_counter) -> generated_system
        {
            return generate_component(comp, all_components, raw_source, sys_counter);
        }

        // ─── Block creation from expression ───────────────────────────────────

        void create_block_from_type(
            ir_block& blk,
            const std::string& expr,
            std::map<std::string, std::pair<int, int>>& var_map,
            std::vector<ir_block>& /*blocks*/,
            std::vector<ir_connection>& connections,
            const parser::oc_element& elem)
        {
            if (blk.type == "Gain") {
                create_gain(blk, expr, var_map, connections);
            } else if (blk.type == "Sum") {
                create_sum(blk, expr, var_map, connections);
            } else if (blk.type == "Product") {
                create_product(blk, expr, var_map, connections);
            } else if (blk.type == "Constant") {
                create_constant(blk, expr, var_map, elem);
            } else if (blk.type == "Saturate") {
                create_saturate(blk, expr, var_map, connections);
            } else if (blk.type == "MinMax") {
                create_minmax(blk, expr, var_map, connections);
            } else if (blk.type == "Switch") {
                create_switch(blk, expr, var_map, connections);
            } else if (blk.type == "RelationalOperator") {
                create_relational(blk, expr, var_map, connections);
            } else if (blk.type == "Logic") {
                create_logic(blk, expr, var_map, connections);
            } else if (blk.type == "Abs") {
                create_abs(blk, expr, var_map, connections);
            } else if (blk.type == "Trigonometry") {
                create_trig(blk, expr, var_map, connections);
            } else if (blk.type == "Math") {
                create_math(blk, expr, var_map, connections);
            } else if (blk.type == "TransferFcn") {
                create_transferfcn(blk, expr, var_map, connections);
            } else if (blk.type == "Reference") {
                create_reference(blk, expr, var_map, connections);
            } else {
                // Generic: 1 in, 1 out, try to connect input
                blk.port_in = 1;
                blk.port_out = 1;
                resolve_input(expr, var_map, connections, blk.sid, 1);
            }
        }

        // ─── Individual block type creators ───────────────────────────────────

        void create_gain(ir_block& blk, const std::string& expr,
                        std::map<std::string, std::pair<int, int>>& var_map,
                        std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            // Pattern: input * factor  OR  factor * input  OR  input / factor
            auto mul_pos = expr.find(" * ");
            auto div_pos = expr.find(" / ");

            if (mul_pos != std::string::npos) {
                auto left = trim(expr.substr(0, mul_pos));
                auto right = trim(expr.substr(mul_pos + 3));

                if (is_variable(left, var_map)) {
                    resolve_input(left, var_map, connections, blk.sid, 1);
                    blk.parameters["Gain"] = right;
                } else if (is_variable(right, var_map)) {
                    resolve_input(right, var_map, connections, blk.sid, 1);
                    blk.parameters["Gain"] = left;
                } else {
                    // Try left as input, right as gain
                    resolve_input(left, var_map, connections, blk.sid, 1);
                    blk.parameters["Gain"] = right;
                }
            } else if (div_pos != std::string::npos) {
                auto left = trim(expr.substr(0, div_pos));
                auto right = trim(expr.substr(div_pos + 3));
                resolve_input(left, var_map, connections, blk.sid, 1);
                blk.parameters["Gain"] = "1/" + right;
            } else {
                // Might be: input * (complex expression)
                // Try last * position
                resolve_input(expr, var_map, connections, blk.sid, 1);
                blk.parameters["Gain"] = "1";
            }
        }

        void create_sum(ir_block& blk, const std::string& expr,
                       std::map<std::string, std::pair<int, int>>& var_map,
                       std::vector<ir_connection>& connections)
        {
            blk.port_out = 1;

            // Parse sum expression: a + b - c + d ...
            // Build sign string and operand list
            std::string signs;
            std::vector<std::string> operands;

            // Tokenize: split by + and - while tracking signs
            std::string current;
            bool first = true;
            bool negate_next = false;

            // Handle leading negative: "- X + Y"
            auto trimmed_expr = trim(expr);
            std::size_t pos = 0;

            // First pass: split by + and -
            while (pos <= trimmed_expr.size()) {
                char c = pos < trimmed_expr.size() ? trimmed_expr[pos] : '\0';

                if (c == '+' || c == '-' || c == '\0') {
                    auto operand = trim(current);
                    if (!operand.empty()) {
                        signs += negate_next ? "-" : "+";
                        operands.push_back(operand);
                    } else if (first && c == '-') {
                        negate_next = true;
                        current.clear();
                        ++pos;
                        first = false;
                        continue;
                    }
                    negate_next = (c == '-');
                    current.clear();
                    first = false;
                } else {
                    current += c;
                }
                ++pos;
            }

            blk.port_in = static_cast<int>(operands.size());
            blk.parameters["Inputs"] = "|" + signs;

            for (int p = 0; p < static_cast<int>(operands.size()); ++p) {
                resolve_input(operands[p], var_map, connections, blk.sid, p + 1);
            }
        }

        void create_product(ir_block& blk, const std::string& expr,
                           std::map<std::string, std::pair<int, int>>& var_map,
                           std::vector<ir_connection>& connections)
        {
            blk.port_out = 1;

            // Check for division: a / b
            auto div_pos = expr.find(" / ");
            if (div_pos != std::string::npos) {
                auto left = trim(expr.substr(0, div_pos));
                auto right = trim(expr.substr(div_pos + 3));
                blk.port_in = 2;
                blk.parameters["Inputs"] = "*/";
                resolve_input(left, var_map, connections, blk.sid, 1);
                resolve_input(right, var_map, connections, blk.sid, 2);
                return;
            }

            // Multiplication: a * b (* c ...)
            std::vector<std::string> operands;
            std::size_t pos = 0;
            std::string current;
            while (pos <= expr.size()) {
                char c = pos < expr.size() ? expr[pos] : '\0';
                if ((c == '*' || c == '\0') &&
                    (c == '\0' || (pos + 1 < expr.size() && expr[pos + 1] == ' '))) {
                    auto op = trim(current);
                    if (!op.empty()) operands.push_back(op);
                    current.clear();
                    if (c == '*') ++pos;  // skip the space after *
                } else {
                    current += c;
                }
                ++pos;
            }

            if (operands.size() < 2) {
                // Fallback: treat as passthrough
                blk.port_in = 1;
                blk.parameters["Inputs"] = "1";
                resolve_input(expr, var_map, connections, blk.sid, 1);
                return;
            }

            blk.port_in = static_cast<int>(operands.size());
            std::string inputs_str;
            for (int k = 0; k < blk.port_in; ++k) inputs_str += "*";
            blk.parameters["Inputs"] = inputs_str;

            for (int p = 0; p < static_cast<int>(operands.size()); ++p) {
                resolve_input(operands[p], var_map, connections, blk.sid, p + 1);
            }
        }

        void create_constant(ir_block& blk, const std::string& expr,
                            std::map<std::string, std::pair<int, int>>& /*var_map*/,
                            const parser::oc_element& /*elem*/)
        {
            blk.port_in = 0;
            blk.port_out = 1;

            // If expression is cfg.X, use X as the value
            auto value = expr;
            if (value.starts_with("cfg.")) {
                value = value.substr(4);
            }
            blk.parameters["Value"] = value;
        }

        void create_saturate(ir_block& blk, const std::string& expr,
                            std::map<std::string, std::pair<int, int>>& var_map,
                            std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            // Pattern: std::clamp(input, lower, upper)
            auto paren_start = expr.find('(');
            auto paren_end = expr.rfind(')');
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                auto args = expr.substr(paren_start + 1, paren_end - paren_start - 1);
                auto parts = split_args(args);
                if (parts.size() >= 3) {
                    resolve_input(parts[0], var_map, connections, blk.sid, 1);
                    blk.parameters["LowerLimit"] = clean_value(parts[1]);
                    blk.parameters["UpperLimit"] = clean_value(parts[2]);
                }
            }
        }

        void create_minmax(ir_block& blk, const std::string& expr,
                          std::map<std::string, std::pair<int, int>>& var_map,
                          std::vector<ir_connection>& connections)
        {
            blk.port_out = 1;

            // Pattern: std::min(a, b) or std::max(a, b)
            std::string func;
            if (expr.find("std::min") != std::string::npos) {
                func = "min";
            } else if (expr.find("std::max") != std::string::npos) {
                func = "max";
            }

            blk.parameters["Function"] = func;

            auto paren_start = expr.find('(');
            auto paren_end = expr.rfind(')');
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                auto args = expr.substr(paren_start + 1, paren_end - paren_start - 1);
                auto parts = split_args(args);
                blk.port_in = static_cast<int>(parts.size());
                for (int p = 0; p < static_cast<int>(parts.size()); ++p) {
                    resolve_input(parts[p], var_map, connections, blk.sid, p + 1);
                }
            } else {
                blk.port_in = 2;
            }
        }

        void create_switch(ir_block& blk, const std::string& expr,
                          std::map<std::string, std::pair<int, int>>& var_map,
                          std::vector<ir_connection>& connections)
        {
            blk.port_in = 3;
            blk.port_out = 1;

            // Pattern: (cond > threshold) ? true_val : false_val
            auto q_pos = expr.find('?');
            auto colon_pos = expr.find(':', q_pos);

            if (q_pos != std::string::npos && colon_pos != std::string::npos) {
                auto condition = trim(expr.substr(0, q_pos));
                auto true_val = trim(expr.substr(q_pos + 1, colon_pos - q_pos - 1));
                auto false_val = trim(expr.substr(colon_pos + 1));

                // Remove outer parens from condition
                if (condition.front() == '(' && condition.back() == ')') {
                    condition = condition.substr(1, condition.size() - 2);
                }

                // Extract threshold from condition (e.g., "var > 0.0f")
                auto gt_pos = condition.find(" > ");
                if (gt_pos != std::string::npos) {
                    auto cond_input = trim(condition.substr(0, gt_pos));
                    auto threshold = trim(condition.substr(gt_pos + 3));
                    blk.parameters["Criteria"] = "u2 > Threshold";
                    blk.parameters["Threshold"] = clean_value(threshold);

                    // Port 1: true_val, Port 2: condition, Port 3: false_val
                    resolve_input(true_val, var_map, connections, blk.sid, 1);
                    resolve_input(cond_input, var_map, connections, blk.sid, 2);
                    resolve_input(false_val, var_map, connections, blk.sid, 3);
                }
            }
        }

        void create_relational(ir_block& blk, const std::string& expr,
                              std::map<std::string, std::pair<int, int>>& var_map,
                              std::vector<ir_connection>& connections)
        {
            blk.port_in = 2;
            blk.port_out = 1;

            // Pattern: (a > b) ? 1.0f : 0.0f
            auto q_pos = expr.find('?');
            if (q_pos != std::string::npos) {
                auto condition = trim(expr.substr(0, q_pos));
                if (condition.front() == '(' && condition.back() == ')') {
                    condition = condition.substr(1, condition.size() - 2);
                }

                // Find relational operator
                std::string op;
                std::string left, right;
                for (const auto& rel : {" >= ", " <= ", " > ", " < ", " == ", " != "}) {
                    auto rp = condition.find(rel);
                    if (rp != std::string::npos) {
                        left = trim(condition.substr(0, rp));
                        right = trim(condition.substr(rp + std::string(rel).size()));
                        op = trim(std::string(rel));
                        break;
                    }
                }

                if (op == ">") blk.parameters["Operator"] = ">";
                else if (op == "<") blk.parameters["Operator"] = "<";
                else if (op == ">=") blk.parameters["Operator"] = ">=";
                else if (op == "<=") blk.parameters["Operator"] = "<=";
                else if (op == "==") blk.parameters["Operator"] = "==";
                else if (op == "!=") blk.parameters["Operator"] = "~=";

                resolve_input(left, var_map, connections, blk.sid, 1);
                resolve_input(right, var_map, connections, blk.sid, 2);
            }
        }

        void create_logic(ir_block& blk, const std::string& expr,
                         std::map<std::string, std::pair<int, int>>& var_map,
                         std::vector<ir_connection>& connections)
        {
            blk.port_out = 1;

            // Pattern: ((a != 0.0f) && (b != 0.0f)) ? 1.0f : 0.0f  (AND)
            // Pattern: ((a != 0.0f) || (b != 0.0f)) ? 1.0f : 0.0f  (OR)
            // Pattern: (a == 0.0f) ? 1.0f : 0.0f                     (NOT)

            auto q_pos = expr.find('?');
            if (q_pos == std::string::npos) {
                blk.port_in = 1;
                return;
            }

            auto condition = trim(expr.substr(0, q_pos));

            // Determine operator
            if (condition.find("== 0.0f") != std::string::npos && condition.find("&&") == std::string::npos && condition.find("||") == std::string::npos) {
                // NOT
                blk.parameters["Operator"] = "NOT";
                blk.port_in = 1;

                // Extract input: (X == 0.0f)
                auto pstart = condition.find('(');
                auto eq_pos = condition.find(" == ");
                if (pstart != std::string::npos && eq_pos != std::string::npos) {
                    auto input = trim(condition.substr(pstart + 1, eq_pos - pstart - 1));
                    resolve_input(input, var_map, connections, blk.sid, 1);
                }
            } else {
                // AND or OR
                bool is_and = condition.find("&&") != std::string::npos;
                blk.parameters["Operator"] = is_and ? "AND" : "OR";

                // Extract operands: ((a != 0.0f) && (b != 0.0f))
                std::vector<std::string> operands;
                std::string delim = is_and ? "&&" : "||";

                // Remove outer parens
                auto inner = condition;
                if (inner.front() == '(' && inner.back() == ')') {
                    inner = inner.substr(1, inner.size() - 2);
                }

                // Split by && or ||
                std::size_t pos = 0;
                while (pos < inner.size()) {
                    auto dp = inner.find(delim, pos);
                    if (dp == std::string::npos) dp = inner.size();
                    auto part = trim(inner.substr(pos, dp - pos));
                    // Extract variable from (X != 0.0f)
                    auto pstart = part.find('(');
                    auto ne_pos = part.find(" != ");
                    if (pstart != std::string::npos && ne_pos != std::string::npos) {
                        auto var = trim(part.substr(pstart + 1, ne_pos - pstart - 1));
                        operands.push_back(var);
                    }
                    pos = dp + delim.size();
                }

                blk.port_in = static_cast<int>(operands.size());
                blk.parameters["Ports"] = "[" + std::to_string(operands.size()) + ", 1]";

                for (int p = 0; p < static_cast<int>(operands.size()); ++p) {
                    resolve_input(operands[p], var_map, connections, blk.sid, p + 1);
                }
            }
        }

        void create_abs(ir_block& blk, const std::string& expr,
                       std::map<std::string, std::pair<int, int>>& var_map,
                       std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            // Pattern: std::abs(input)
            auto paren_start = expr.find('(');
            auto paren_end = expr.rfind(')');
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                auto input = trim(expr.substr(paren_start + 1, paren_end - paren_start - 1));
                resolve_input(input, var_map, connections, blk.sid, 1);
            }
        }

        void create_trig(ir_block& blk, const std::string& expr,
                        std::map<std::string, std::pair<int, int>>& var_map,
                        std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            if (expr.find("std::cos") != std::string::npos) {
                blk.parameters["Operator"] = "cos";
            } else if (expr.find("std::sin") != std::string::npos) {
                blk.parameters["Operator"] = "sin";
            } else if (expr.find("std::tan") != std::string::npos) {
                blk.parameters["Operator"] = "tan";
            } else if (expr.find("std::atan2") != std::string::npos) {
                blk.parameters["Operator"] = "atan2";
                blk.port_in = 2;
            }

            auto paren_start = expr.find('(');
            auto paren_end = expr.rfind(')');
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                auto args_str = expr.substr(paren_start + 1, paren_end - paren_start - 1);
                auto parts = split_args(args_str);
                for (int p = 0; p < static_cast<int>(parts.size()) && p < blk.port_in; ++p) {
                    resolve_input(parts[p], var_map, connections, blk.sid, p + 1);
                }
            }
        }

        void create_math(ir_block& blk, const std::string& expr,
                        std::map<std::string, std::pair<int, int>>& var_map,
                        std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            if (expr.find("std::sqrt") != std::string::npos) {
                blk.parameters["Operator"] = "sqrt";
            } else if (expr.find("std::exp") != std::string::npos) {
                blk.parameters["Operator"] = "exp";
            } else if (expr.find("std::log") != std::string::npos) {
                blk.parameters["Operator"] = "log";
            } else if (expr.find(" * ") != std::string::npos && blk.name.find("Square") != std::string::npos) {
                blk.parameters["Operator"] = "square";
                // Pattern: X * X (squaring)
                auto mul = expr.find(" * ");
                auto left = trim(expr.substr(0, mul));
                resolve_input(left, var_map, connections, blk.sid, 1);
                return;
            } else if (expr.find("// TODO: Math/conj") != std::string::npos || blk.name.find("Conj") != std::string::npos) {
                blk.parameters["Operator"] = "conj";
            }

            auto paren_start = expr.find('(');
            auto paren_end = expr.rfind(')');
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                auto input = trim(expr.substr(paren_start + 1, paren_end - paren_start - 1));
                resolve_input(input, var_map, connections, blk.sid, 1);
            } else {
                // Fallback: try to resolve the whole expr (e.g., for conj pass-through)
                // Strip TODO comments
                auto clean = expr;
                auto todo = clean.find("// TODO:");
                if (todo != std::string::npos) {
                    clean = trim(clean.substr(0, todo));
                }
                if (!clean.empty()) {
                    resolve_input(clean, var_map, connections, blk.sid, 1);
                }
            }
        }

        void create_transferfcn(ir_block& blk, const std::string& expr,
                               std::map<std::string, std::pair<int, int>>& var_map,
                               std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            // The expr is: state.X_tf_x0  (after the scoped block)
            // The actual input was connected inside the scoped block
            // We'll try to resolve it from the variable map
            resolve_input(expr, var_map, connections, blk.sid, 1);
        }

        void create_reference(ir_block& blk, const std::string& expr,
                             std::map<std::string, std::pair<int, int>>& var_map,
                             std::vector<ir_connection>& connections)
        {
            blk.port_in = 1;
            blk.port_out = 1;

            // Extract input from: value_expr // TODO: Reference
            auto clean = expr;
            auto todo = clean.find("// TODO:");
            if (todo != std::string::npos) {
                clean = trim(clean.substr(0, todo));
            }

            // Try to extract source block name from block name for Reference type
            blk.parameters["SourceType"] = "Compare To Constant";

            if (!clean.empty() && clean != "0.0f /* missing input 1 */") {
                resolve_input(clean, var_map, connections, blk.sid, 1);
            }
        }

        // ─── Variable resolution ──────────────────────────────────────────────

        void resolve_input(const std::string& expr,
                          std::map<std::string, std::pair<int, int>>& var_map,
                          std::vector<ir_connection>& connections,
                          int dst_sid, int dst_port)
        {
            auto clean = trim(expr);

            // Strip trailing comments
            auto todo = clean.find("// TODO:");
            if (todo != std::string::npos) {
                clean = trim(clean.substr(0, todo));
            }

            // Skip literal values / missing inputs
            if (clean.empty()) return;
            if (clean.find("/* missing input") != std::string::npos) return;
            if (clean == "0.0f" || clean == "0" || clean == "1.0f" || clean == "1") return;
            if (clean.find("std::numeric_limits") != std::string::npos) return;

            // Direct variable lookup
            if (auto it = var_map.find(clean); it != var_map.end()) {
                connections.push_back({it->second.first, it->second.second, dst_sid, dst_port});
                return;
            }

            // Try with state. prefix
            if (!clean.starts_with("state.")) {
                auto state_key = "state." + clean + "_state";
                if (auto it = var_map.find(state_key); it != var_map.end()) {
                    connections.push_back({it->second.first, it->second.second, dst_sid, dst_port});
                    return;
                }
            }

            // Try stripping "state." to get the block output
            if (clean.starts_with("state.")) {
                if (auto it = var_map.find(clean); it != var_map.end()) {
                    connections.push_back({it->second.first, it->second.second, dst_sid, dst_port});
                    return;
                }
                // Try with just the var name after "state."
                auto var_after = clean.substr(6);
                if (auto it = var_map.find(var_after); it != var_map.end()) {
                    connections.push_back({it->second.first, it->second.second, dst_sid, dst_port});
                    return;
                }
            }

            // Try in. prefix
            if (clean.starts_with("in.")) {
                if (auto it = var_map.find(clean); it != var_map.end()) {
                    connections.push_back({it->second.first, it->second.second, dst_sid, dst_port});
                    return;
                }
            }

            // cfg.X references don't produce connections (they're parameter values)
            if (clean.starts_with("cfg.")) return;

            // Unable to resolve - skip (no connection)
        }

        [[nodiscard]] auto is_variable(const std::string& name,
                                       const std::map<std::string, std::pair<int, int>>& var_map) -> bool
        {
            if (var_map.count(name)) return true;
            if (var_map.count("in." + name)) return true;
            if (var_map.count("state." + name + "_state")) return true;
            // Check if it looks like a variable (starts with letter, not a number/expression)
            if (!name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
                // But not if it looks like a constant expression
                if (name.find('*') == std::string::npos && name.find('+') == std::string::npos &&
                    name.find('(') == std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        // ─── Output assignment extraction ─────────────────────────────────────

        [[nodiscard]] auto extract_output_assignments(const std::vector<std::string>& lines)
            -> std::map<std::string, std::string>
        {
            std::map<std::string, std::string> result;
            bool in_outputs = false;

            for (const auto& line : lines) {
                auto trimmed = trim(line);

                if (trimmed == "// Outputs") {
                    in_outputs = true;
                    continue;
                }

                if (in_outputs && trimmed.starts_with("out.")) {
                    // out.X = Y;
                    auto dot_pos = trimmed.find('.');
                    auto eq_pos = trimmed.find('=');
                    if (dot_pos != std::string::npos && eq_pos != std::string::npos) {
                        auto out_name = trim(trimmed.substr(dot_pos + 1, eq_pos - dot_pos - 1));
                        auto src = trim(trimmed.substr(eq_pos + 1));
                        if (src.back() == ';') src.pop_back();
                        src = trim(src);
                        result[out_name] = src;
                    }
                }
            }

            return result;
        }

        // ─── Auto-layout ─────────────────────────────────────────────────────

        void auto_layout(std::vector<ir_block>& blocks,
                        const std::vector<ir_connection>& connections)
        {
            if (blocks.empty()) return;

            // Build dependency graph for topological ordering
            std::map<int, int> sid_to_idx;
            for (std::size_t i = 0; i < blocks.size(); ++i) {
                sid_to_idx[blocks[i].sid] = static_cast<int>(i);
            }

            // Compute column for each block based on dependencies
            std::map<int, int> block_column;  // sid -> column

            // Inports are column 0, Outports are last column
            for (auto& blk : blocks) {
                if (blk.type == "Inport") {
                    block_column[blk.sid] = 0;
                }
            }

            // Assign columns via forward pass
            bool changed = true;
            int max_iters = static_cast<int>(blocks.size()) + 1;
            while (changed && max_iters-- > 0) {
                changed = false;
                for (const auto& conn : connections) {
                    if (block_column.count(conn.src_sid) && sid_to_idx.count(conn.dst_sid)) {
                        int new_col = block_column[conn.src_sid] + 1;
                        if (!block_column.count(conn.dst_sid) || block_column[conn.dst_sid] < new_col) {
                            block_column[conn.dst_sid] = new_col;
                            changed = true;
                        }
                    }
                }
            }

            // Assign column 1 to blocks with no column yet (disconnected)
            int max_col = 1;
            for (auto& blk : blocks) {
                if (!block_column.count(blk.sid) && blk.type != "Outport") {
                    block_column[blk.sid] = 1;
                }
                if (block_column.count(blk.sid)) {
                    max_col = std::max(max_col, block_column[blk.sid]);
                }
            }

            // Outports go to last column + 1
            for (auto& blk : blocks) {
                if (blk.type == "Outport") {
                    block_column[blk.sid] = max_col + 1;
                }
            }

            // Group blocks by column
            std::map<int, std::vector<int>> column_blocks;  // col -> [block indices]
            for (std::size_t i = 0; i < blocks.size(); ++i) {
                int col = block_column.count(blocks[i].sid) ? block_column[blocks[i].sid] : 1;
                column_blocks[col].push_back(static_cast<int>(i));
            }

            // Layout parameters
            const int left_margin = 50;
            const int top_margin = 30;
            const int col_width = 160;
            const int row_height = 60;

            for (auto& [col, indices] : column_blocks) {
                int x = left_margin + col * col_width;
                for (int r = 0; r < static_cast<int>(indices.size()); ++r) {
                    auto& blk = blocks[indices[r]];
                    int y = top_margin + r * row_height;

                    // Block size based on type
                    int w, h;
                    if (blk.type == "Inport" || blk.type == "Outport") {
                        w = 30; h = 14;
                    } else if (blk.type == "SubSystem") {
                        w = 120; h = 80;
                    } else if (blk.type == "Sum") {
                        w = 36; h = 36;
                    } else if (blk.type == "Gain") {
                        w = 40; h = 36;
                    } else {
                        w = 50; h = 36;
                    }

                    blk.position = {x, y, x + w, y + h};
                }
            }
        }

        // ─── System XML emission ──────────────────────────────────────────────

        [[nodiscard]] auto emit_system_xml(
            const std::vector<ir_block>& blocks,
            const std::vector<ir_connection>& connections,
            int highwatermark) -> std::string
        {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<System>\n";
            out << "  <P Name=\"Location\">[-1, -8, 1921, 1033]</P>\n";
            out << "  <P Name=\"ZoomFactor\">100</P>\n";
            out << "  <P Name=\"SIDHighWatermark\">" << highwatermark << "</P>\n";

            // Emit blocks
            for (const auto& blk : blocks) {
                out << "  <Block BlockType=\"" << blk.type
                    << "\" Name=\"" << xml_escape(blk.name)
                    << "\" SID=\"" << blk.sid << "\">\n";

                // Port counts (only if non-default)
                if (blk.port_in > 0 || blk.port_out > 0) {
                    bool needs_portcounts = false;
                    if (blk.type == "SubSystem" || blk.port_in > 1 || blk.port_out > 1) {
                        needs_portcounts = true;
                    }
                    if (needs_portcounts) {
                        out << "    <PortCounts";
                        if (blk.port_in > 0) out << " in=\"" << blk.port_in << "\"";
                        if (blk.port_out > 0) out << " out=\"" << blk.port_out << "\"";
                        out << "/>\n";
                    }
                }

                // Position
                if (!blk.position.empty()) {
                    out << "    <P Name=\"Position\">[";
                    for (std::size_t i = 0; i < blk.position.size(); ++i) {
                        if (i > 0) out << ", ";
                        out << blk.position[i];
                    }
                    out << "]</P>\n";
                }

                // ZOrder
                out << "    <P Name=\"ZOrder\">" << blk.sid << "</P>\n";

                // Parameters
                for (const auto& [k, v] : blk.parameters) {
                    out << "    <P Name=\"" << k << "\">" << xml_escape(v) << "</P>\n";
                }

                // Subsystem reference
                if (!blk.subsystem_ref.empty()) {
                    out << "    <System Ref=\"" << blk.subsystem_ref << "\"/>\n";
                }

                out << "  </Block>\n";
            }

            // Group connections by source to create Lines with Branches
            std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> line_groups;
            for (const auto& conn : connections) {
                line_groups[{conn.src_sid, conn.src_port}].push_back({conn.dst_sid, conn.dst_port});
            }

            int zorder = 1;
            for (const auto& [src, dests] : line_groups) {
                out << "  <Line>\n";
                out << "    <P Name=\"ZOrder\">" << zorder++ << "</P>\n";
                out << "    <P Name=\"Src\">" << src.first << "#out:" << src.second << "</P>\n";

                if (dests.size() == 1) {
                    out << "    <P Name=\"Dst\">" << dests[0].first << "#in:" << dests[0].second << "</P>\n";
                } else {
                    for (const auto& [dst_sid, dst_port] : dests) {
                        out << "    <Branch>\n";
                        out << "      <P Name=\"ZOrder\">" << zorder++ << "</P>\n";
                        out << "      <P Name=\"Dst\">" << dst_sid << "#in:" << dst_port << "</P>\n";
                        out << "    </Branch>\n";
                    }
                }

                out << "  </Line>\n";
            }

            out << "</System>";
            return out.str();
        }

        // ─── Utility ──────────────────────────────────────────────────────────

        [[nodiscard]] static auto xml_escape(const std::string& s) -> std::string {
            std::string result;
            result.reserve(s.size());
            for (char c : s) {
                switch (c) {
                    case '&': result += "&amp;"; break;
                    case '<': result += "&lt;"; break;
                    case '>': result += "&gt;"; break;
                    case '"': result += "&quot;"; break;
                    case '\'': result += "&apos;"; break;
                    case '\n': result += "&#xA;"; break;
                    default: result += c;
                }
            }
            return result;
        }

        // Decode XML entity references to actual characters
        [[nodiscard]] static auto xml_decode(const std::string& s) -> std::string {
            std::string result;
            result.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '&' && i + 1 < s.size()) {
                    // Look for &#xA; (newline)
                    if (s.substr(i, 5) == "&#xA;") {
                        result += '\n';
                        i += 4;
                        continue;
                    }
                    // Look for &amp;
                    if (s.substr(i, 5) == "&amp;") {
                        result += '&';
                        i += 4;
                        continue;
                    }
                    // Look for &lt;
                    if (s.substr(i, 4) == "&lt;") {
                        result += '<';
                        i += 3;
                        continue;
                    }
                    // Look for &gt;
                    if (s.substr(i, 4) == "&gt;") {
                        result += '>';
                        i += 3;
                        continue;
                    }
                }
                result += s[i];
            }
            return result;
        }

        [[nodiscard]] static auto trim(const std::string& s) -> std::string {
            auto start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return {};
            auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        [[nodiscard]] static auto split_args(const std::string& s) -> std::vector<std::string> {
            std::vector<std::string> result;
            int depth = 0;
            std::string current;
            for (char c : s) {
                if (c == '(' || c == '[') ++depth;
                if (c == ')' || c == ']') --depth;
                if (c == ',' && depth == 0) {
                    auto t = trim(current);
                    if (!t.empty()) result.push_back(t);
                    current.clear();
                } else {
                    current += c;
                }
            }
            auto t = trim(current);
            if (!t.empty()) result.push_back(t);
            return result;
        }

        [[nodiscard]] static auto format_coeff(double v) -> std::string {
            // Format a coefficient, removing unnecessary trailing zeros
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }

        [[nodiscard]] static auto clean_value(const std::string& s) -> std::string {
            auto v = trim(s);
            // Remove 'f' suffix from float literals
            if (v.ends_with("f") && v.size() > 1) {
                // Check if it's a float literal
                bool is_float = true;
                for (std::size_t i = 0; i < v.size() - 1; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(v[i])) && v[i] != '.' && v[i] != '-' && v[i] != 'e' && v[i] != 'E') {
                        is_float = false;
                        break;
                    }
                }
                if (is_float) v.pop_back();
            }
            // Remove cfg. prefix
            if (v.starts_with("cfg.")) {
                v = v.substr(4);
            }
            return v;
        }
    };

} // namespace oc
