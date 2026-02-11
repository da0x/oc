//
// Open Controls - OC Metadata Format Reader/Writer
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include "oc_json.hpp"
#include <string>
#include <vector>
#include <map>
#include <fstream>

namespace oc::metadata {

    // ─────────────────────────────────────────────────────────────────────────────
    // Metadata Structures
    // ─────────────────────────────────────────────────────────────────────────────

    struct model_info {
        std::string uuid;
        std::string library_type;
        std::string name;
    };

    struct port_property {
        std::string port_type;
        int index = 0;
        std::map<std::string, std::string> properties;
    };

    struct mask_param {
        std::string name;
        std::string type;
        std::string prompt;
        std::string value;
        std::string show_tooltip;
    };

    struct block_meta {
        std::string sid;
        std::string type;
        std::string name;
        std::vector<int> position;
        int zorder = 0;
        std::string background_color;
        std::string subsystem_ref;
        int port_in = 0;
        int port_out = 0;
        std::map<std::string, std::string> parameters;
        std::vector<mask_param> mask_parameters;
        std::vector<port_property> port_properties;
        std::string mask_display_xml;
    };

    struct branch_meta {
        int zorder = 0;
        std::string destination;
        std::vector<int> points;
    };

    struct connection_meta {
        std::string name;
        int zorder = 0;
        std::string source;
        std::string destination;
        std::vector<int> points;
        std::vector<branch_meta> branches;
        std::string labels;
    };

    struct system_meta {
        std::string id;
        std::vector<int> location;
        int zoom_factor = 100;
        int sid_highwatermark = 0;
        std::string open;
        std::string report_name;
        std::vector<block_meta> blocks;
        std::vector<connection_meta> connections;
    };

    struct metadata {
        int version = 1;
        model_info model;
        std::vector<std::string> part_order;  // preserves original OPC part ordering
        std::map<std::string, std::string> raw_parts;
        std::map<std::string, system_meta> systems;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Write metadata to JSON
    // ─────────────────────────────────────────────────────────────────────────────

    [[nodiscard]] inline auto to_json(const metadata& meta) -> json::value {
        json::object root;
        root["version"] = meta.version;

        // Model info
        json::object model_obj;
        model_obj["uuid"] = meta.model.uuid;
        model_obj["library_type"] = meta.model.library_type;
        model_obj["name"] = meta.model.name;
        root["model"] = json::value(std::move(model_obj));

        // Part order
        if (!meta.part_order.empty()) {
            json::array order_arr;
            for (const auto& p : meta.part_order) order_arr.push_back(json::value(p));
            root["part_order"] = json::value(std::move(order_arr));
        }

        // Raw parts
        json::object raw_obj;
        for (const auto& [path, content] : meta.raw_parts) {
            raw_obj[path] = content;
        }
        root["raw_parts"] = json::value(std::move(raw_obj));

        // Systems
        json::object systems_obj;
        for (const auto& [sys_id, sys] : meta.systems) {
            json::object sys_obj;

            // System properties
            {
                json::array loc;
                for (int v : sys.location) loc.push_back(json::value(v));
                sys_obj["location"] = json::value(std::move(loc));
            }
            sys_obj["zoom_factor"] = sys.zoom_factor;
            sys_obj["sid_highwatermark"] = sys.sid_highwatermark;
            if (!sys.open.empty()) sys_obj["open"] = sys.open;
            if (!sys.report_name.empty()) sys_obj["report_name"] = sys.report_name;

            // Blocks
            json::array blocks_arr;
            for (const auto& blk : sys.blocks) {
                json::object blk_obj;
                blk_obj["sid"] = blk.sid;
                blk_obj["type"] = blk.type;
                blk_obj["name"] = blk.name;

                {
                    json::array pos;
                    for (int v : blk.position) pos.push_back(json::value(v));
                    blk_obj["position"] = json::value(std::move(pos));
                }

                blk_obj["zorder"] = blk.zorder;
                if (!blk.background_color.empty()) blk_obj["background_color"] = blk.background_color;
                if (!blk.subsystem_ref.empty()) blk_obj["subsystem_ref"] = blk.subsystem_ref;
                if (blk.port_in > 0) blk_obj["port_in"] = blk.port_in;
                if (blk.port_out > 0) blk_obj["port_out"] = blk.port_out;

                // Parameters
                if (!blk.parameters.empty()) {
                    json::object params;
                    for (const auto& [k, v] : blk.parameters) params[k] = v;
                    blk_obj["parameters"] = json::value(std::move(params));
                }

                // Mask parameters
                if (!blk.mask_parameters.empty()) {
                    json::array mask_arr;
                    for (const auto& mp : blk.mask_parameters) {
                        json::object mp_obj;
                        mp_obj["name"] = mp.name;
                        mp_obj["type"] = mp.type;
                        mp_obj["prompt"] = mp.prompt;
                        mp_obj["value"] = mp.value;
                        if (!mp.show_tooltip.empty()) mp_obj["show_tooltip"] = mp.show_tooltip;
                        mask_arr.push_back(json::value(std::move(mp_obj)));
                    }
                    blk_obj["mask"] = json::value(std::move(mask_arr));
                }
                if (!blk.mask_display_xml.empty()) {
                    blk_obj["mask_display_xml"] = blk.mask_display_xml;
                }

                // Port properties
                if (!blk.port_properties.empty()) {
                    json::array pp_arr;
                    for (const auto& pp : blk.port_properties) {
                        json::object pp_obj;
                        pp_obj["port_type"] = pp.port_type;
                        pp_obj["index"] = pp.index;
                        json::object props;
                        for (const auto& [k, v] : pp.properties) props[k] = v;
                        pp_obj["properties"] = json::value(std::move(props));
                        pp_arr.push_back(json::value(std::move(pp_obj)));
                    }
                    blk_obj["port_properties"] = json::value(std::move(pp_arr));
                }

                blocks_arr.push_back(json::value(std::move(blk_obj)));
            }
            sys_obj["blocks"] = json::value(std::move(blocks_arr));

            // Connections
            json::array conns_arr;
            for (const auto& conn : sys.connections) {
                json::object conn_obj;
                if (!conn.name.empty()) conn_obj["name"] = conn.name;
                conn_obj["zorder"] = conn.zorder;
                conn_obj["src"] = conn.source;
                if (!conn.destination.empty()) conn_obj["dst"] = conn.destination;
                if (!conn.labels.empty()) conn_obj["labels"] = conn.labels;

                if (!conn.points.empty()) {
                    json::array pts;
                    for (int v : conn.points) pts.push_back(json::value(v));
                    conn_obj["points"] = json::value(std::move(pts));
                }

                if (!conn.branches.empty()) {
                    json::array br_arr;
                    for (const auto& br : conn.branches) {
                        json::object br_obj;
                        br_obj["zorder"] = br.zorder;
                        br_obj["dst"] = br.destination;
                        if (!br.points.empty()) {
                            json::array pts;
                            for (int v : br.points) pts.push_back(json::value(v));
                            br_obj["points"] = json::value(std::move(pts));
                        }
                        br_arr.push_back(json::value(std::move(br_obj)));
                    }
                    conn_obj["branches"] = json::value(std::move(br_arr));
                }

                conns_arr.push_back(json::value(std::move(conn_obj)));
            }
            sys_obj["connections"] = json::value(std::move(conns_arr));

            systems_obj[sys_id] = json::value(std::move(sys_obj));
        }
        root["systems"] = json::value(std::move(systems_obj));

        return json::value(std::move(root));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Read metadata from JSON
    // ─────────────────────────────────────────────────────────────────────────────

    namespace detail {
        [[nodiscard]] inline auto parse_int_array(const json::value& v) -> std::vector<int> {
            std::vector<int> result;
            if (v.is_array()) {
                for (const auto& item : v.as_array()) {
                    result.push_back(item.as_int());
                }
            }
            return result;
        }

        [[nodiscard]] inline auto get_string(const json::value& v, const std::string& key) -> std::string {
            if (v.contains(key) && v[key].is_string()) return v[key].as_string();
            return {};
        }

        [[nodiscard]] inline auto get_int(const json::value& v, const std::string& key, int def = 0) -> int {
            if (v.contains(key) && v[key].is_number()) return v[key].as_int();
            return def;
        }
    }

    [[nodiscard]] inline auto from_json(const json::value& root) -> metadata {
        metadata meta;
        meta.version = detail::get_int(root, "version", 1);

        // Model info
        if (root.contains("model")) {
            const auto& m = root["model"];
            meta.model.uuid = detail::get_string(m, "uuid");
            meta.model.library_type = detail::get_string(m, "library_type");
            meta.model.name = detail::get_string(m, "name");
        }

        // Part order
        if (root.contains("part_order") && root["part_order"].is_array()) {
            for (const auto& item : root["part_order"].as_array()) {
                if (item.is_string()) meta.part_order.push_back(item.as_string());
            }
        }

        // Raw parts
        if (root.contains("raw_parts") && root["raw_parts"].is_object()) {
            for (const auto& [path, content] : root["raw_parts"].as_object()) {
                if (content.is_string()) meta.raw_parts[path] = content.as_string();
            }
        }

        // Systems
        if (root.contains("systems") && root["systems"].is_object()) {
            for (const auto& [sys_id, sys_val] : root["systems"].as_object()) {
                system_meta sys;
                sys.id = sys_id;
                sys.location = detail::parse_int_array(sys_val["location"]);
                sys.zoom_factor = detail::get_int(sys_val, "zoom_factor", 100);
                sys.sid_highwatermark = detail::get_int(sys_val, "sid_highwatermark");
                sys.open = detail::get_string(sys_val, "open");
                sys.report_name = detail::get_string(sys_val, "report_name");

                // Blocks
                if (sys_val.contains("blocks") && sys_val["blocks"].is_array()) {
                    for (const auto& blk_val : sys_val["blocks"].as_array()) {
                        block_meta blk;
                        blk.sid = detail::get_string(blk_val, "sid");
                        blk.type = detail::get_string(blk_val, "type");
                        blk.name = detail::get_string(blk_val, "name");
                        blk.position = detail::parse_int_array(blk_val["position"]);
                        blk.zorder = detail::get_int(blk_val, "zorder");
                        blk.background_color = detail::get_string(blk_val, "background_color");
                        blk.subsystem_ref = detail::get_string(blk_val, "subsystem_ref");
                        blk.port_in = detail::get_int(blk_val, "port_in");
                        blk.port_out = detail::get_int(blk_val, "port_out");
                        blk.mask_display_xml = detail::get_string(blk_val, "mask_display_xml");

                        // Parameters
                        if (blk_val.contains("parameters") && blk_val["parameters"].is_object()) {
                            for (const auto& [k, v] : blk_val["parameters"].as_object()) {
                                if (v.is_string()) blk.parameters[k] = v.as_string();
                            }
                        }

                        // Mask parameters
                        if (blk_val.contains("mask") && blk_val["mask"].is_array()) {
                            for (const auto& mp_val : blk_val["mask"].as_array()) {
                                mask_param mp;
                                mp.name = detail::get_string(mp_val, "name");
                                mp.type = detail::get_string(mp_val, "type");
                                mp.prompt = detail::get_string(mp_val, "prompt");
                                mp.value = detail::get_string(mp_val, "value");
                                mp.show_tooltip = detail::get_string(mp_val, "show_tooltip");
                                blk.mask_parameters.push_back(std::move(mp));
                            }
                        }

                        // Port properties
                        if (blk_val.contains("port_properties") && blk_val["port_properties"].is_array()) {
                            for (const auto& pp_val : blk_val["port_properties"].as_array()) {
                                port_property pp;
                                pp.port_type = detail::get_string(pp_val, "port_type");
                                pp.index = detail::get_int(pp_val, "index");
                                if (pp_val.contains("properties") && pp_val["properties"].is_object()) {
                                    for (const auto& [k, v] : pp_val["properties"].as_object()) {
                                        if (v.is_string()) pp.properties[k] = v.as_string();
                                    }
                                }
                                blk.port_properties.push_back(std::move(pp));
                            }
                        }

                        sys.blocks.push_back(std::move(blk));
                    }
                }

                // Connections
                if (sys_val.contains("connections") && sys_val["connections"].is_array()) {
                    for (const auto& conn_val : sys_val["connections"].as_array()) {
                        connection_meta conn;
                        conn.name = detail::get_string(conn_val, "name");
                        conn.zorder = detail::get_int(conn_val, "zorder");
                        conn.source = detail::get_string(conn_val, "src");
                        conn.destination = detail::get_string(conn_val, "dst");
                        conn.labels = detail::get_string(conn_val, "labels");
                        conn.points = detail::parse_int_array(conn_val["points"]);

                        // Branches
                        if (conn_val.contains("branches") && conn_val["branches"].is_array()) {
                            for (const auto& br_val : conn_val["branches"].as_array()) {
                                branch_meta br;
                                br.zorder = detail::get_int(br_val, "zorder");
                                br.destination = detail::get_string(br_val, "dst");
                                br.points = detail::parse_int_array(br_val["points"]);
                                conn.branches.push_back(std::move(br));
                            }
                        }

                        sys.connections.push_back(std::move(conn));
                    }
                }

                meta.systems[sys_id] = std::move(sys);
            }
        }

        return meta;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // File I/O
    // ─────────────────────────────────────────────────────────────────────────────

    [[nodiscard]] inline auto write_file(const std::string& path, const metadata& meta) -> bool {
        auto json_val = to_json(meta);
        auto json_str = json::stringify(json_val, 2);

        std::ofstream out(path);
        if (!out) return false;
        out << json_str;
        return out.good();
    }

    [[nodiscard]] inline auto read_file(const std::string& path) -> std::optional<metadata> {
        std::ifstream in(path);
        if (!in) return std::nullopt;

        std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        try {
            auto json_val = json::parse(content);
            return from_json(json_val);
        } catch (...) {
            return std::nullopt;
        }
    }

} // namespace oc::metadata
