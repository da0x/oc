//
// Open Controls - Metadata Writer (MDL → .oc.metadata)
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
#include "../liboc/oc_metadata.hpp"
#include <string>
#include <set>

namespace oc {

    class metadata_writer {
    public:
        [[nodiscard]] auto build_metadata(
            const mdl::model& model,
            const mdl::opc_extractor& opc) -> metadata::metadata
        {
            metadata::metadata meta;
            meta.version = 1;

            // Model info
            meta.model.uuid = model.uuid;
            meta.model.library_type = model.library_type;
            meta.model.name = model.name;

            // Capture original part ordering and ALL raw parts (including system XMLs)
            for (const auto& path : opc.list_parts()) {
                meta.part_order.push_back(path);
                if (auto* content = opc.get_part(path)) {
                    meta.raw_parts[path] = *content;
                }
            }

            // Capture per-system metadata
            for (const auto& [sys_id, sys] : model.systems) {
                meta.systems[sys_id] = build_system_meta(sys);
            }

            return meta;
        }

    private:
        [[nodiscard]] auto collect_system_paths(const mdl::opc_extractor& opc) -> std::set<std::string> {
            std::set<std::string> paths;
            for (const auto& path : opc.list_systems()) {
                paths.insert(path);
            }
            return paths;
        }

        [[nodiscard]] auto build_system_meta(const mdl::system& sys) -> metadata::system_meta {
            metadata::system_meta sm;
            sm.id = sys.id;
            sm.location = sys.location;
            sm.zoom_factor = sys.zoom_factor;
            sm.sid_highwatermark = sys.sid_highwatermark;
            sm.open = sys.open;
            sm.report_name = sys.report_name;

            // Blocks
            for (const auto& blk : sys.blocks) {
                metadata::block_meta bm;
                bm.sid = blk.sid;
                bm.type = blk.type;
                bm.name = blk.name;
                bm.position = blk.position;
                bm.zorder = blk.zorder;
                bm.subsystem_ref = blk.subsystem_ref;
                bm.port_in = blk.port_in;
                bm.port_out = blk.port_out;

                // Capture all parameters
                for (const auto& [k, v] : blk.parameters) {
                    // Skip position and zorder (already captured separately)
                    if (k == "Position" || k == "ZOrder") continue;
                    bm.parameters[k] = v;
                }

                // Background color is in parameters
                if (auto it = blk.parameters.find("BackgroundColor"); it != blk.parameters.end()) {
                    bm.background_color = it->second;
                }

                // Mask parameters
                for (const auto& mp : blk.mask_parameters) {
                    metadata::mask_param mm;
                    mm.name = mp.name;
                    mm.type = mp.type;
                    mm.prompt = mp.prompt;
                    mm.value = mp.value;
                    bm.mask_parameters.push_back(std::move(mm));
                }

                // Port properties
                for (const auto& pi : blk.input_ports) {
                    metadata::port_property pp;
                    pp.port_type = "in";
                    pp.index = pi.index;
                    if (!pi.name.empty()) pp.properties["Name"] = pi.name;
                    if (!pi.propagated_signals.empty()) pp.properties["PropagatedSignals"] = pi.propagated_signals;
                    bm.port_properties.push_back(std::move(pp));
                }
                for (const auto& pi : blk.output_ports) {
                    metadata::port_property pp;
                    pp.port_type = "out";
                    pp.index = pi.index;
                    if (!pi.name.empty()) pp.properties["Name"] = pi.name;
                    if (!pi.propagated_signals.empty()) pp.properties["PropagatedSignals"] = pi.propagated_signals;
                    bm.port_properties.push_back(std::move(pp));
                }

                sm.blocks.push_back(std::move(bm));
            }

            // Connections
            for (const auto& conn : sys.connections) {
                metadata::connection_meta cm;
                cm.name = conn.name;
                cm.zorder = conn.zorder;
                cm.source = conn.source;
                cm.destination = conn.destination;
                cm.points = conn.points;
                cm.labels = conn.labels;

                for (const auto& br : conn.branches) {
                    metadata::branch_meta bm;
                    bm.zorder = br.zorder;
                    bm.destination = br.destination;
                    bm.points = br.points;
                    cm.branches.push_back(std::move(bm));
                }

                sm.connections.push_back(std::move(cm));
            }

            return sm;
        }
    };

} // namespace oc
