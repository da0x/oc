//
// Open Controls - MDL Parser Library
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ranges>

namespace oc::mdl {

    // ─────────────────────────────────────────────────────────────────────────────
    // XML Parser - sufficient for MDL files
    // ─────────────────────────────────────────────────────────────────────────────

    namespace xml {

        struct attribute {
            std::string name;
            std::string value;
        };

        struct element {
            std::string tag;
            std::vector<attribute> attributes;
            std::string text;
            std::vector<element> children;

            [[nodiscard]] auto attr(std::string_view name) const -> std::string {
                for (const auto& a : attributes) {
                    if (a.name == name) return a.value;
                }
                return {};
            }

            [[nodiscard]] auto child(std::string_view tag_name) const -> const element* {
                for (const auto& c : children) {
                    if (c.tag == tag_name) return &c;
                }
                return nullptr;
            }

            [[nodiscard]] auto children_by_tag(std::string_view tag_name) const -> std::vector<const element*> {
                std::vector<const element*> result;
                for (const auto& c : children) {
                    if (c.tag == tag_name) result.push_back(&c);
                }
                return result;
            }

            [[nodiscard]] auto child_text(std::string_view tag_name) const -> std::string {
                if (auto c = child(tag_name)) return c->text;
                return {};
            }
        };

        class parser {
            std::string_view input_;
            std::size_t pos_ = 0;

        public:
            [[nodiscard]] auto parse(std::string_view xml_content) -> element {
                input_ = xml_content;
                pos_ = 0;
                skip_whitespace();
                skip_declaration();
                skip_whitespace();
                return parse_element();
            }

        private:
            void skip_whitespace() {
                while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
                    ++pos_;
                }
            }

            void skip_declaration() {
                if (pos_ + 1 < input_.size() && input_[pos_] == '<' && input_[pos_ + 1] == '?') {
                    while (pos_ < input_.size()) {
                        if (input_[pos_] == '?' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
                            pos_ += 2;
                            return;
                        }
                        ++pos_;
                    }
                }
            }

            void skip_comment() {
                if (pos_ + 3 < input_.size() && input_.substr(pos_, 4) == "<!--") {
                    pos_ += 4;
                    while (pos_ + 2 < input_.size()) {
                        if (input_.substr(pos_, 3) == "-->") {
                            pos_ += 3;
                            return;
                        }
                        ++pos_;
                    }
                }
            }

            [[nodiscard]] auto parse_element() -> element {
                element elem;
                skip_whitespace();

                while (pos_ + 3 < input_.size() && input_.substr(pos_, 4) == "<!--") {
                    skip_comment();
                    skip_whitespace();
                }

                if (pos_ >= input_.size() || input_[pos_] != '<') return elem;
                ++pos_;

                auto tag_start = pos_;
                while (pos_ < input_.size() &&
                       !std::isspace(static_cast<unsigned char>(input_[pos_])) &&
                       input_[pos_] != '>' && input_[pos_] != '/') {
                    ++pos_;
                }
                elem.tag = std::string(input_.substr(tag_start, pos_ - tag_start));

                while (pos_ < input_.size()) {
                    skip_whitespace();
                    if (input_[pos_] == '/' || input_[pos_] == '>') break;

                    attribute attr;
                    auto name_start = pos_;
                    while (pos_ < input_.size() && input_[pos_] != '=' &&
                           !std::isspace(static_cast<unsigned char>(input_[pos_]))) {
                        ++pos_;
                    }
                    attr.name = std::string(input_.substr(name_start, pos_ - name_start));

                    skip_whitespace();
                    if (pos_ < input_.size() && input_[pos_] == '=') {
                        ++pos_;
                        skip_whitespace();
                        if (pos_ < input_.size() && input_[pos_] == '"') {
                            ++pos_;
                            auto value_start = pos_;
                            while (pos_ < input_.size() && input_[pos_] != '"') ++pos_;
                            attr.value = std::string(input_.substr(value_start, pos_ - value_start));
                            if (pos_ < input_.size()) ++pos_;
                        }
                    }
                    elem.attributes.push_back(std::move(attr));
                }

                if (pos_ < input_.size() && input_[pos_] == '/') {
                    ++pos_;
                    if (pos_ < input_.size() && input_[pos_] == '>') ++pos_;
                    return elem;
                }

                if (pos_ < input_.size() && input_[pos_] == '>') ++pos_;

                while (pos_ < input_.size()) {
                    skip_whitespace();

                    if (pos_ + 1 < input_.size() && input_[pos_] == '<' && input_[pos_ + 1] == '/') {
                        pos_ += 2;
                        while (pos_ < input_.size() && input_[pos_] != '>') ++pos_;
                        if (pos_ < input_.size()) ++pos_;
                        break;
                    }

                    if (pos_ + 3 < input_.size() && input_.substr(pos_, 4) == "<!--") {
                        skip_comment();
                        continue;
                    }

                    if (pos_ < input_.size() && input_[pos_] == '<') {
                        elem.children.push_back(parse_element());
                    } else {
                        auto text_start = pos_;
                        while (pos_ < input_.size() && input_[pos_] != '<') ++pos_;
                        auto text_content = std::string(input_.substr(text_start, pos_ - text_start));

                        if (auto first = text_content.find_first_not_of(" \t\n\r"); first != std::string::npos) {
                            auto last = text_content.find_last_not_of(" \t\n\r");
                            elem.text = text_content.substr(first, last - first + 1);
                        }
                    }
                }

                return elem;
            }
        };

        [[nodiscard]] inline auto decode_entities(std::string_view input) -> std::string {
            std::string result;
            result.reserve(input.size());

            for (std::size_t i = 0; i < input.size(); ++i) {
                if (input[i] == '&') {
                    if (input.substr(i, 4) == "&lt;") { result += '<'; i += 3; }
                    else if (input.substr(i, 4) == "&gt;") { result += '>'; i += 3; }
                    else if (input.substr(i, 5) == "&amp;") { result += '&'; i += 4; }
                    else if (input.substr(i, 6) == "&quot;") { result += '"'; i += 5; }
                    else if (input.substr(i, 6) == "&apos;") { result += '\''; i += 5; }
                    else result += input[i];
                } else {
                    result += input[i];
                }
            }
            return result;
        }

    } // namespace xml

    // ─────────────────────────────────────────────────────────────────────────────
    // Block types
    // ─────────────────────────────────────────────────────────────────────────────

    struct port_info {
        int index = 0;
        std::string name;
        std::string port_type;
        std::string propagated_signals;
    };

    struct mask_parameter {
        std::string name;
        std::string type;
        std::string prompt;
        std::string value;
    };

    struct block {
        std::string type;
        std::string name;
        std::string sid;
        std::vector<int> position;
        int zorder = 0;
        int port_in = 1;
        int port_out = 1;

        std::map<std::string, std::string> parameters;
        std::vector<mask_parameter> mask_parameters;
        std::vector<port_info> input_ports;
        std::vector<port_info> output_ports;

        std::string subsystem_ref;

        [[nodiscard]] constexpr auto is_inport() const noexcept -> bool { return type == "Inport"; }
        [[nodiscard]] constexpr auto is_outport() const noexcept -> bool { return type == "Outport"; }
        [[nodiscard]] constexpr auto is_subsystem() const noexcept -> bool { return type == "SubSystem"; }

        [[nodiscard]] auto param(std::string_view key) const -> std::optional<std::string> {
            if (auto it = parameters.find(std::string(key)); it != parameters.end()) {
                return it->second;
            }
            return std::nullopt;
        }

        [[nodiscard]] auto mask_param(std::string_view key) const -> std::optional<std::string> {
            for (const auto& mp : mask_parameters) {
                if (mp.name == key) return mp.value;
            }
            return std::nullopt;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Connection types
    // ─────────────────────────────────────────────────────────────────────────────

    struct endpoint {
        std::string block_sid;
        std::string port_type;
        int port_index = 1;

        [[nodiscard]] static auto parse(std::string_view spec) -> std::optional<endpoint> {
            auto hash_pos = spec.find('#');
            if (hash_pos == std::string_view::npos) return std::nullopt;

            auto colon_pos = spec.find(':', hash_pos);
            if (colon_pos == std::string_view::npos) return std::nullopt;

            return endpoint{
                .block_sid = std::string(spec.substr(0, hash_pos)),
                .port_type = std::string(spec.substr(hash_pos + 1, colon_pos - hash_pos - 1)),
                .port_index = std::stoi(std::string(spec.substr(colon_pos + 1)))
            };
        }
    };

    struct branch {
        int zorder = 0;
        std::string destination;
        std::vector<int> points;
    };

    struct connection {
        std::string name;
        int zorder = 0;
        std::string source;
        std::string destination;
        std::vector<int> points;
        std::vector<branch> branches;
        std::string labels;

        [[nodiscard]] auto source_endpoint() const { return endpoint::parse(source); }
        [[nodiscard]] auto destination_endpoint() const { return endpoint::parse(destination); }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // System and Model
    // ─────────────────────────────────────────────────────────────────────────────

    struct system {
        std::string id;
        std::string name;
        std::vector<int> location;
        int zoom_factor = 100;
        int sid_highwatermark = 0;
        std::string open;
        std::string report_name;

        std::vector<block> blocks;
        std::vector<connection> connections;
        std::vector<std::string> child_system_refs;

        [[nodiscard]] auto inports() const -> std::vector<block> {
            std::vector<block> result;
            for (const auto& b : blocks) {
                if (b.is_inport()) result.push_back(b);
            }
            return result;
        }

        [[nodiscard]] auto outports() const -> std::vector<block> {
            std::vector<block> result;
            for (const auto& b : blocks) {
                if (b.is_outport()) result.push_back(b);
            }
            return result;
        }

        [[nodiscard]] auto subsystems() const -> std::vector<block> {
            std::vector<block> result;
            for (const auto& b : blocks) {
                if (b.is_subsystem()) result.push_back(b);
            }
            return result;
        }

        [[nodiscard]] auto find_block_by_sid(std::string_view sid) const -> const block* {
            for (const auto& b : blocks) {
                if (b.sid == sid) return &b;
            }
            return nullptr;
        }

        [[nodiscard]] auto find_block_by_name(std::string_view name) const -> const block* {
            for (const auto& b : blocks) {
                if (b.name == name) return &b;
            }
            return nullptr;
        }
    };

    struct model {
        std::string uuid;
        std::string name;
        std::string version;
        std::string library_type;
        std::map<std::string, system> systems;

        [[nodiscard]] auto root_system() const -> const system* {
            if (auto it = systems.find("system_root"); it != systems.end()) {
                return &it->second;
            }
            return nullptr;
        }

        [[nodiscard]] auto get_system(std::string_view id) const -> const system* {
            if (auto it = systems.find(std::string(id)); it != systems.end()) {
                return &it->second;
            }
            return nullptr;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // OPC Extractor - extracts parts from MDL container
    // ─────────────────────────────────────────────────────────────────────────────

    class opc_extractor {
        std::map<std::string, std::string> parts_;

    public:
        [[nodiscard]] auto load(const std::string& mdl_path) -> bool {
            std::ifstream file(mdl_path);
            if (!file) return false;

            std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

            constexpr std::string_view marker = "__MWOPC_PART_BEGIN__ ";
            std::size_t pos = 0;

            while ((pos = content.find(marker, pos)) != std::string::npos) {
                pos += marker.size();

                auto path_end = content.find('\n', pos);
                if (path_end == std::string::npos) break;

                auto part_line = content.substr(pos, path_end - pos);
                auto space_pos = part_line.find(' ');
                auto part_path = (space_pos != std::string::npos) ? part_line.substr(0, space_pos) : part_line;

                while (!part_path.empty() && (part_path.back() == '\r' || part_path.back() == ' ')) {
                    part_path.pop_back();
                }

                pos = path_end + 1;

                auto next_marker = content.find("__MWOPC_PART_BEGIN__", pos);
                auto part_content = (next_marker != std::string::npos)
                    ? content.substr(pos, next_marker - pos)
                    : content.substr(pos);

                while (!part_content.empty() &&
                       (part_content.back() == '\n' || part_content.back() == '\r' || part_content.back() == ' ')) {
                    part_content.pop_back();
                }

                parts_[part_path] = std::move(part_content);
            }

            return !parts_.empty();
        }

        [[nodiscard]] auto get_part(std::string_view path) const -> const std::string* {
            if (auto it = parts_.find(std::string(path)); it != parts_.end()) {
                return &it->second;
            }
            return nullptr;
        }

        [[nodiscard]] auto list_parts() const -> std::vector<std::string> {
            std::vector<std::string> result;
            result.reserve(parts_.size());
            for (const auto& [path, _] : parts_) {
                result.push_back(path);
            }
            return result;
        }

        [[nodiscard]] auto list_systems() const -> std::vector<std::string> {
            std::vector<std::string> result;
            for (const auto& [path, _] : parts_) {
                if (path.find("/simulink/systems/system_") != std::string::npos &&
                    path.find(".xml.rels") == std::string::npos &&
                    path.ends_with(".xml")) {
                    result.push_back(path);
                }
            }
            return result;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // System Parser
    // ─────────────────────────────────────────────────────────────────────────────

    class system_parser {
    public:
        [[nodiscard]] auto parse(std::string_view system_id, std::string_view xml_content) -> system {
            system sys;
            sys.id = std::string(system_id);

            xml::parser parser;
            auto root = parser.parse(xml_content);

            for (const auto* p : root.children_by_tag("P")) {
                auto pname = p->attr("Name");
                if (pname == "Location") {
                    sys.location = parse_int_array(p->text);
                } else if (pname == "ZoomFactor") {
                    sys.zoom_factor = std::stoi(p->text);
                } else if (pname == "SIDHighWatermark") {
                    sys.sid_highwatermark = std::stoi(p->text);
                } else if (pname == "Open") {
                    sys.open = p->text;
                } else if (pname == "ReportName") {
                    sys.report_name = p->text;
                }
            }

            for (const auto* block_elem : root.children_by_tag("Block")) {
                sys.blocks.push_back(parse_block(*block_elem));
            }

            for (const auto* line_elem : root.children_by_tag("Line")) {
                sys.connections.push_back(parse_connection(*line_elem));
            }

            return sys;
        }

    private:
        [[nodiscard]] auto parse_block(const xml::element& elem) -> block {
            block b;
            b.type = elem.attr("BlockType");
            b.name = elem.attr("Name");
            b.sid = elem.attr("SID");

            if (auto port_counts = elem.child("PortCounts")) {
                if (auto in_str = port_counts->attr("in"); !in_str.empty()) {
                    b.port_in = std::stoi(in_str);
                }
                if (auto out_str = port_counts->attr("out"); !out_str.empty()) {
                    b.port_out = std::stoi(out_str);
                }
            }

            for (const auto* p : elem.children_by_tag("P")) {
                auto name = p->attr("Name");
                auto value = xml::decode_entities(p->text);
                b.parameters[name] = value;

                if (name == "Position") {
                    b.position = parse_int_array(value);
                } else if (name == "ZOrder") {
                    b.zorder = std::stoi(value);
                }
            }

            if (auto sys_ref = elem.child("System")) {
                b.subsystem_ref = sys_ref->attr("Ref");
            }

            if (auto mask = elem.child("Mask")) {
                for (const auto* mp : mask->children_by_tag("MaskParameter")) {
                    mask_parameter param;
                    param.name = mp->attr("Name");
                    param.type = mp->attr("Type");

                    if (auto prompt = mp->child("Prompt")) {
                        param.prompt = prompt->text;
                    }
                    if (auto value = mp->child("Value")) {
                        param.value = xml::decode_entities(value->text);
                    }

                    b.mask_parameters.push_back(std::move(param));
                }
            }

            if (auto port_props = elem.child("PortProperties")) {
                for (const auto* port : port_props->children_by_tag("Port")) {
                    port_info pi;
                    pi.port_type = port->attr("Type");
                    if (auto idx_str = port->attr("Index"); !idx_str.empty()) {
                        pi.index = std::stoi(idx_str);
                    }

                    for (const auto* p : port->children_by_tag("P")) {
                        if (p->attr("Name") == "Name") {
                            pi.name = p->text;
                        } else if (p->attr("Name") == "PropagatedSignals") {
                            pi.propagated_signals = p->text;
                        }
                    }

                    if (pi.port_type == "in") {
                        b.input_ports.push_back(std::move(pi));
                    } else if (pi.port_type == "out") {
                        b.output_ports.push_back(std::move(pi));
                    }
                }
            }

            return b;
        }

        [[nodiscard]] auto parse_connection(const xml::element& elem) -> connection {
            connection conn;

            for (const auto* p : elem.children_by_tag("P")) {
                auto name = p->attr("Name");
                if (name == "Name") {
                    conn.name = p->text;
                } else if (name == "ZOrder") {
                    conn.zorder = std::stoi(p->text);
                } else if (name == "Src") {
                    conn.source = p->text;
                } else if (name == "Dst") {
                    conn.destination = p->text;
                } else if (name == "Points") {
                    conn.points = parse_int_array(p->text);
                } else if (name == "Labels") {
                    conn.labels = p->text;
                }
            }

            for (const auto* branch_elem : elem.children_by_tag("Branch")) {
                branch br;
                for (const auto* p : branch_elem->children_by_tag("P")) {
                    auto name = p->attr("Name");
                    if (name == "ZOrder") {
                        br.zorder = std::stoi(p->text);
                    } else if (name == "Dst") {
                        br.destination = p->text;
                    } else if (name == "Points") {
                        br.points = parse_int_array(p->text);
                    }
                }
                conn.branches.push_back(std::move(br));
            }

            return conn;
        }

        [[nodiscard]] static auto parse_int_array(std::string_view str) -> std::vector<int> {
            std::vector<int> result;
            std::string cleaned;

            for (char c : str) {
                if (c == '[' || c == ']') continue;
                if (c == ',' || c == ';') c = ' ';
                cleaned += c;
            }

            std::istringstream iss(cleaned);
            int val;
            while (iss >> val) {
                result.push_back(val);
            }
            return result;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Main MDL Parser
    // ─────────────────────────────────────────────────────────────────────────────

    class parser {
        opc_extractor opc_;
        model model_;

    public:
        [[nodiscard]] auto load(const std::string& mdl_path) -> bool {
            if (!opc_.load(mdl_path)) {
                return false;
            }

            if (auto blockdiagram = opc_.get_part("/simulink/blockdiagram.xml")) {
                parse_blockdiagram(*blockdiagram);
            }

            system_parser sys_parser;
            for (const auto& sys_path : opc_.list_systems()) {
                if (auto content = opc_.get_part(sys_path)) {
                    auto sys_id = sys_path;
                    if (auto last_slash = sys_id.rfind('/'); last_slash != std::string::npos) {
                        sys_id = sys_id.substr(last_slash + 1);
                    }
                    if (auto dot = sys_id.rfind('.'); dot != std::string::npos) {
                        sys_id = sys_id.substr(0, dot);
                    }

                    auto sys = sys_parser.parse(sys_id, *content);
                    model_.systems[sys_id] = std::move(sys);
                }
            }

            return true;
        }

        [[nodiscard]] auto get_model() const -> const model& { return model_; }
        [[nodiscard]] auto get_opc() const -> const opc_extractor& { return opc_; }

    private:
        void parse_blockdiagram(std::string_view xml_content) {
            xml::parser p;
            auto root = p.parse(xml_content);

            auto library = root.child("Library");
            auto model_elem = library ? library : root.child("Model");

            if (model_elem) {
                for (const auto* prop : model_elem->children_by_tag("P")) {
                    auto name = prop->attr("Name");
                    if (name == "ModelUUID") {
                        model_.uuid = prop->text;
                    } else if (name == "LibraryType") {
                        model_.library_type = prop->text;
                    }
                }
            }
        }
    };

} // namespace oc::mdl
