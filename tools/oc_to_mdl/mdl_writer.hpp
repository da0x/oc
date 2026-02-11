//
// Open Controls - MDL Writer (OC + metadata → MDL)
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#pragma once

#include "../liboc/oc_metadata.hpp"
#include "../liboc/oc_parser.hpp"
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <random>
#include <iomanip>
#include <algorithm>
#include <set>

namespace oc {

    class mdl_writer {
    public:
        // Write MDL file using metadata (verbatim round-trip)
        [[nodiscard]] auto write_with_metadata(const metadata::metadata& meta) -> std::string {
            std::ostringstream out;

            // Header
            out << "# MathWorks OPC Text Package\n";
            out << "Model {\n";
            out << "  Version  24.2\n";
            out << "  Description \"Simulink model saved in R2024b\"\n";
            out << "}\n";
            out << "__MWOPC_PACKAGE_BEGIN__ R2024b\n";

            // Write parts in original order if available, using raw content verbatim
            if (!meta.part_order.empty()) {
                for (const auto& path : meta.part_order) {
                    if (auto it = meta.raw_parts.find(path); it != meta.raw_parts.end()) {
                        write_part(out, path, it->second);
                    }
                }
            } else {
                // Fallback: write raw parts in sorted order
                for (const auto& [path, content] : meta.raw_parts) {
                    write_part(out, path, content);
                }
            }

            return out.str();
        }

        // Write MDL file with best-guess defaults (no metadata)
        [[nodiscard]] auto write_with_defaults(
            const std::vector<parser::oc_file>& oc_files,
            const std::string& model_name) -> std::string
        {
            std::ostringstream out;

            auto uuid = generate_uuid();
            auto library_name = model_name;
            // Remove _lib suffix if present
            if (library_name.size() > 4 && library_name.ends_with("_lib")) {
                library_name = library_name.substr(0, library_name.size() - 4);
            }

            // Header
            out << "# MathWorks OPC Text Package\n";
            out << "Model {\n";
            out << "  Version  24.2\n";
            out << "  Description \"Simulink model saved in R2024b\"\n";
            out << "}\n";
            out << "__MWOPC_PACKAGE_BEGIN__ R2024b\n";

            // Generate default parts
            write_part(out, "/[Content_Types].xml", generate_default_content_types());
            write_part(out, "/_rels/.rels", generate_default_rels());
            write_part(out, "/metadata/coreProperties.xml", generate_default_core_properties());
            write_part(out, "/metadata/mwcoreProperties.xml", generate_default_mw_core_properties());
            write_part(out, "/metadata/mwcorePropertiesExtension.xml",
                       generate_default_mw_core_extension(uuid));
            write_part(out, "/metadata/mwcorePropertiesReleaseInfo.xml",
                       generate_default_release_info());
            write_part(out, "/simulink/_rels/blockdiagram.xml.rels",
                       generate_default_blockdiagram_rels());
            write_part(out, "/simulink/_rels/configSetInfo.xml.rels",
                       generate_default_config_set_info_rels());
            write_part(out, "/simulink/bddefaults.xml", generate_default_bd_defaults());
            write_part(out, "/simulink/blockdiagram.xml",
                       generate_default_blockdiagram(uuid, model_name));
            write_part(out, "/simulink/configSet0.xml", generate_default_config_set());
            write_part(out, "/simulink/configSetInfo.xml", generate_default_config_set_info());
            write_part(out, "/simulink/modelDictionary.xml", generate_default_model_dictionary());

            // Count total elements to generate system IDs
            int total_elements = 0;
            for (const auto& file : oc_files) {
                for (const auto& ns : file.namespaces) {
                    total_elements += static_cast<int>(ns.elements.size());
                }
            }

            // Generate .rels for system_root (references all child systems)
            write_part(out, "/simulink/systems/_rels/system_root.xml.rels",
                       generate_default_system_rels(1, total_elements));

            // Generate system_root with subsystem blocks for each element
            auto root_xml = generate_default_root_system(oc_files);
            write_part(out, "/simulink/systems/system_root.xml", root_xml);

            // Generate subsystem XMLs for each element
            int sys_counter = 1;
            for (const auto& file : oc_files) {
                for (const auto& ns : file.namespaces) {
                    for (const auto& elem : ns.elements) {
                        auto sys_xml = generate_default_element_system(elem, sys_counter);
                        write_part(out, "/simulink/systems/system_" + std::to_string(sys_counter) + ".xml", sys_xml);
                        ++sys_counter;
                    }
                }
            }

            write_part(out, "/simulink/windowsInfo.xml", generate_default_windows_info());

            return out.str();
        }

    private:
        void write_part(std::ostringstream& out, const std::string& path, const std::string& content) {
            // Check if content looks like BASE64 (binary) data
            bool is_base64 = path.ends_with(".mxarray");
            out << "__MWOPC_PART_BEGIN__ " << path;
            if (is_base64) out << " BASE64";
            out << "\n" << content << "\n";
            // XML parts have a blank line separator; BASE64 parts don't
            if (!is_base64) out << "\n";
        }

        // ─── System XML Generation from metadata ────────────────────────────
        [[nodiscard]] auto generate_system_xml(const metadata::system_meta& sys) -> std::string {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<System>\n";

            // System properties
            if (!sys.location.empty()) {
                out << "  <P Name=\"Location\">[";
                for (std::size_t i = 0; i < sys.location.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << sys.location[i];
                }
                out << "]</P>\n";
            }
            if (!sys.open.empty()) {
                out << "  <P Name=\"Open\">" << sys.open << "</P>\n";
            }
            out << "  <P Name=\"ZoomFactor\">" << sys.zoom_factor << "</P>\n";
            if (!sys.report_name.empty()) {
                out << "  <P Name=\"ReportName\">" << sys.report_name << "</P>\n";
            }
            if (sys.sid_highwatermark > 0) {
                out << "  <P Name=\"SIDHighWatermark\">" << sys.sid_highwatermark << "</P>\n";
            }

            // Blocks
            for (const auto& blk : sys.blocks) {
                out << "  <Block BlockType=\"" << blk.type << "\" Name=\"" << xml_escape(blk.name) << "\" SID=\"" << blk.sid << "\">\n";

                // Port counts
                if (blk.port_in > 0 || blk.port_out > 0) {
                    out << "    <PortCounts";
                    if (blk.port_in > 0) out << " in=\"" << blk.port_in << "\"";
                    if (blk.port_out > 0) out << " out=\"" << blk.port_out << "\"";
                    out << "/>\n";
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
                out << "    <P Name=\"ZOrder\">" << blk.zorder << "</P>\n";

                // Other parameters
                for (const auto& [k, v] : blk.parameters) {
                    if (k == "Position" || k == "ZOrder") continue;
                    out << "    <P Name=\"" << k << "\">" << xml_escape(v) << "</P>\n";
                }

                // Mask
                if (!blk.mask_parameters.empty()) {
                    out << "    <Mask>\n";
                    if (!blk.mask_display_xml.empty()) {
                        out << "      " << blk.mask_display_xml << "\n";
                    } else {
                        out << "      <Display RunInitForIconRedraw=\"off\"/>\n";
                    }
                    for (const auto& mp : blk.mask_parameters) {
                        out << "      <MaskParameter Name=\"" << mp.name << "\" Type=\"" << mp.type << "\"";
                        if (!mp.show_tooltip.empty()) out << " ShowTooltip=\"" << mp.show_tooltip << "\"";
                        out << ">\n";
                        out << "        <Prompt>" << xml_escape(mp.prompt) << "</Prompt>\n";
                        out << "        <Value>" << xml_escape(mp.value) << "</Value>\n";
                        out << "      </MaskParameter>\n";
                    }
                    out << "    </Mask>\n";
                }

                // Port properties
                if (!blk.port_properties.empty()) {
                    out << "    <PortProperties>\n";
                    for (const auto& pp : blk.port_properties) {
                        out << "      <Port Type=\"" << pp.port_type << "\" Index=\"" << pp.index << "\">\n";
                        for (const auto& [k, v] : pp.properties) {
                            out << "        <P Name=\"" << k << "\">" << xml_escape(v) << "</P>\n";
                        }
                        out << "      </Port>\n";
                    }
                    out << "    </PortProperties>\n";
                }

                // Subsystem reference
                if (!blk.subsystem_ref.empty()) {
                    out << "    <System Ref=\"" << blk.subsystem_ref << "\"/>\n";
                }

                out << "  </Block>\n";
            }

            // Connections
            for (const auto& conn : sys.connections) {
                out << "  <Line>\n";
                if (!conn.name.empty()) {
                    out << "    <P Name=\"Name\">" << xml_escape(conn.name) << "</P>\n";
                }
                out << "    <P Name=\"ZOrder\">" << conn.zorder << "</P>\n";
                if (!conn.labels.empty()) {
                    out << "    <P Name=\"Labels\">" << conn.labels << "</P>\n";
                }
                out << "    <P Name=\"Src\">" << conn.source << "</P>\n";

                if (!conn.points.empty()) {
                    out << "    <P Name=\"Points\">[";
                    for (std::size_t i = 0; i < conn.points.size(); ++i) {
                        if (i > 0) out << ", ";
                        out << conn.points[i];
                    }
                    out << "]</P>\n";
                }

                if (!conn.destination.empty() && conn.branches.empty()) {
                    out << "    <P Name=\"Dst\">" << conn.destination << "</P>\n";
                }

                for (const auto& br : conn.branches) {
                    out << "    <Branch>\n";
                    out << "      <P Name=\"ZOrder\">" << br.zorder << "</P>\n";
                    if (!br.points.empty()) {
                        out << "      <P Name=\"Points\">[";
                        for (std::size_t i = 0; i < br.points.size(); ++i) {
                            if (i > 0) out << ", ";
                            out << br.points[i];
                        }
                        out << "]</P>\n";
                    }
                    out << "      <P Name=\"Dst\">" << br.destination << "</P>\n";
                    out << "    </Branch>\n";
                }

                out << "  </Line>\n";
            }

            out << "</System>";
            return out.str();
        }

        // ─── Default generators (for when metadata is missing) ──────────────

        [[nodiscard]] auto generate_default_content_types() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default ContentType="application/vnd.mathworks.matlab.mxarray+binary" Extension="mxarray"/>
  <Default ContentType="application/vnd.openxmlformats-package.relationships+xml" Extension="rels"/>
  <Default ContentType="application/vnd.mathworks.simulink.mdl+xml" Extension="xml"/>
  <Override ContentType="application/vnd.openxmlformats-package.core-properties+xml" PartName="/metadata/coreProperties.xml"/>
  <Override ContentType="application/vnd.mathworks.package.coreProperties+xml" PartName="/metadata/mwcoreProperties.xml"/>
  <Override ContentType="application/vnd.mathworks.package.corePropertiesExtension+xml" PartName="/metadata/mwcorePropertiesExtension.xml"/>
  <Override ContentType="application/vnd.mathworks.package.corePropertiesReleaseInfo+xml" PartName="/metadata/mwcorePropertiesReleaseInfo.xml"/>
  <Override ContentType="application/vnd.mathworks.simulink.configSet+xml" PartName="/simulink/configSet0.xml"/>
  <Override ContentType="application/vnd.mathworks.simulink.configSetInfo+xml" PartName="/simulink/configSetInfo.xml"/>
  <Override ContentType="application/vnd.mathworks.simulink.mf0+xml" PartName="/simulink/modelDictionary.xml"/>
  <Override ContentType="application/vnd.mathworks.simulink.blockDiagram+xml" PartName="/simulink/windowsInfo.xml"/>
</Types>)";
        }

        [[nodiscard]] auto generate_default_rels() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="blockDiagram" Target="simulink/blockdiagram.xml" Type="http://schemas.mathworks.com/simulink/2010/relationships/blockDiagram"/>
  <Relationship Id="blockDiagramDefaults" Target="simulink/bddefaults.xml" Type="http://schemas.mathworks.com/simulink/2017/relationships/blockDiagramDefaults"/>
  <Relationship Id="configSetInfo" Target="simulink/configSetInfo.xml" Type="http://schemas.mathworks.com/simulink/2014/relationships/configSetInfo"/>
  <Relationship Id="modelDictionary" Target="simulink/modelDictionary.xml" Type="http://schemas.mathworks.com/simulinkModel/2016/relationships/modelDictionary"/>
  <Relationship Id="rId1" Target="metadata/mwcoreProperties.xml" Type="http://schemas.mathworks.com/package/2012/relationships/coreProperties"/>
  <Relationship Id="rId2" Target="metadata/mwcorePropertiesExtension.xml" Type="http://schemas.mathworks.com/package/2014/relationships/corePropertiesExtension"/>
  <Relationship Id="rId3" Target="metadata/mwcorePropertiesReleaseInfo.xml" Type="http://schemas.mathworks.com/package/2019/relationships/corePropertiesReleaseInfo"/>
  <Relationship Id="rId4" Target="metadata/coreProperties.xml" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"/>
</Relationships>)";
        }

        [[nodiscard]] auto generate_default_core_properties() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <cp:category>library</cp:category>
  <dcterms:created xsi:type="dcterms:W3CDTF">2026-01-01T00:00:00Z</dcterms:created>
  <dc:creator>oc_to_mdl</dc:creator>
  <cp:lastModifiedBy>oc_to_mdl</cp:lastModifiedBy>
  <dcterms:modified xsi:type="dcterms:W3CDTF">2026-01-01T00:00:00Z</dcterms:modified>
  <cp:revision>1.0</cp:revision>
  <cp:version>R2024b</cp:version>
</cp:coreProperties>)";
        }

        [[nodiscard]] auto generate_default_mw_core_properties() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<mwcoreProperties xmlns="http://schemas.mathworks.com/package/2012/coreProperties">
  <contentType>application/vnd.mathworks.simulink.model</contentType>
  <contentTypeFriendlyName>Simulink Model</contentTypeFriendlyName>
  <matlabRelease>R2024b</matlabRelease>
</mwcoreProperties>)";
        }

        [[nodiscard]] auto generate_default_mw_core_extension(const std::string& uuid) -> std::string {
            return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n"
                   "<mwcoreProperties xmlns=\"http://schemas.mathworks.com/package/2014/corePropertiesExtension\">\n"
                   "  <uuid>" + uuid + "</uuid>\n"
                   "</mwcoreProperties>";
        }

        [[nodiscard]] auto generate_default_release_info() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8"?>
<MathWorks_version_info>
  <version>24.2.0.2863752</version>
  <release>R2024b</release>
  <description>Update 5</description>
  <date>Jan 31 2025</date>
  <checksum>2052451712</checksum>
</MathWorks_version_info>)";
        }

        [[nodiscard]] auto generate_default_blockdiagram_rels() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="system_root" Target="systems/system_root.xml" Type="http://schemas.mathworks.com/simulink/2010/relationships/system"/>
  <Relationship Id="windowsInfo" Target="windowsInfo.xml" Type="http://schemas.mathworks.com/simulinkModel/2019/relationships/windowsInfo"/>
</Relationships>)";
        }

        [[nodiscard]] auto generate_default_config_set_info_rels() -> std::string {
            return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="configSet0" Target="configSet0.xml" Type="http://schemas.mathworks.com/simulink/2014/relationships/configSet"/>
</Relationships>)";
        }

        [[nodiscard]] auto generate_default_bd_defaults() -> std::string {
            return R"(<?xml version="1.0" encoding="utf-8"?>
<BlockDiagramDefaults>
  <MaskDefaults SelfModifiable="off">
    <Display IconFrame="on" IconOpaque="opaque" RunInitForIconRedraw="analyze" IconRotate="none" PortRotate="default" IconUnits="autoscale"/>
    <MaskParameter Evaluate="on" Tunable="on" NeverSave="off" Internal="off" ReadOnly="off" Enabled="on" Visible="on" ToolTip="on"/>
    <DialogControl>
      <ControlOptions Visible="on" Enabled="on" Row="new" HorizontalStretch="on" PromptLocation="top" Orientation="horizontal" Scale="linear" TextType="Plain Text" Expand="off" ShowFilter="on" ShowParameterName="on" WordWrap="on" AlignPrompts="off"/>
    </DialogControl>
  </MaskDefaults>
</BlockDiagramDefaults>)";
        }

        [[nodiscard]] auto generate_default_blockdiagram(const std::string& uuid, [[maybe_unused]] const std::string& model_name) -> std::string {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<ModelInformation Version=\"1.0\">\n";
            out << "  <Library>\n";
            out << "    <P Name=\"ModelUUID\">" << uuid << "</P>\n";
            out << "    <P Name=\"LibraryType\">BlockLibrary</P>\n";
            out << "    <System Ref=\"system_root\"/>\n";
            out << "  </Library>\n";
            out << "</ModelInformation>";
            return out.str();
        }

        [[nodiscard]] auto generate_default_config_set() -> std::string {
            return R"(<?xml version="1.0" encoding="utf-8"?>
<ConfigSet>
  <Object Version="24.1.0" ClassName="Simulink.ConfigSet">
    <P Name="DisabledProps" Class="double">[]</P>
    <P Name="Description"/>
    <Array PropName="Components" Type="Handle" Dimension="1*1">
      <Object ObjectID="2" Version="24.1.0" ClassName="Simulink.SolverCC">
        <P Name="DisabledProps" Class="double">[]</P>
        <P Name="Description"/>
        <P Name="Components" Class="double">[]</P>
        <P Name="SolverName">VariableStepAuto</P>
      </Object>
    </Array>
  </Object>
</ConfigSet>)";
        }

        [[nodiscard]] auto generate_default_config_set_info() -> std::string {
            return R"(<?xml version="1.0" encoding="utf-8"?>
<ConfigSetInfo>
  <ConfigSet Ref="configSet0" Active="true"/>
</ConfigSetInfo>)";
        }

        [[nodiscard]] auto generate_default_model_dictionary() -> std::string {
            return R"(<?xml version="1.0" encoding="utf-8"?>
<ModelDictionary/>)";
        }

        [[nodiscard]] auto generate_default_windows_info() -> std::string {
            return R"(<?xml version="1.0" encoding="utf-8"?>
<WindowsInfo>
  <Object PropName="BdWindowsInfo" ObjectID="1" ClassName="Simulink.BDWindowsInfo">
    <Object PropName="WindowsInfo" ObjectID="2" ClassName="Simulink.WindowInfo">
      <P Name="IsActive" Class="logical">1</P>
      <P Name="Location" Class="double">[0.0, 0.0, 1920.0, 1080.0]</P>
    </Object>
  </Object>
</WindowsInfo>)";
        }

        // ─── Default system generators ──────────────────────────────────────

        [[nodiscard]] auto generate_default_root_system(
            const std::vector<parser::oc_file>& oc_files) -> std::string
        {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<System>\n";
            out << "  <P Name=\"Location\">[-1, -8, 1921, 1153]</P>\n";
            out << "  <P Name=\"ZoomFactor\">100</P>\n";

            // Count total elements for SIDHighWatermark
            int total_sids = 0;
            for (const auto& file : oc_files) {
                for (const auto& ns : file.namespaces) {
                    total_sids += static_cast<int>(ns.elements.size());
                }
            }
            out << "  <P Name=\"SIDHighWatermark\">" << total_sids << "</P>\n";

            int sid = 1;
            int x = 100;
            int y = 100;

            for (const auto& file : oc_files) {
                for (const auto& ns : file.namespaces) {
                    for (const auto& elem : ns.elements) {
                        // Count inports/outports from element sections
                        int in_count = 0;
                        int out_count = 0;
                        for (const auto& sec : elem.sections) {
                            if (sec.kind == "input") in_count = static_cast<int>(sec.variables.size());
                            if (sec.kind == "output") out_count = static_cast<int>(sec.variables.size());
                        }

                        out << "  <Block BlockType=\"SubSystem\" Name=\"" << xml_escape(elem.name) << "\" SID=\"" << sid << "\">\n";
                        if (in_count > 0 || out_count > 0) {
                            out << "    <PortCounts";
                            if (in_count > 0) out << " in=\"" << in_count << "\"";
                            if (out_count > 0) out << " out=\"" << out_count << "\"";
                            out << "/>\n";
                        }
                        out << "    <P Name=\"Position\">[" << x << ", " << y << ", " << (x + 120) << ", " << (y + 80) << "]</P>\n";
                        out << "    <P Name=\"ZOrder\">" << sid << "</P>\n";
                        out << "    <System Ref=\"system_" << sid << "\"/>\n";
                        out << "  </Block>\n";

                        y += 120;
                        if (y > 800) { y = 100; x += 200; }
                        ++sid;
                    }
                }
            }

            out << "</System>";
            return out.str();
        }

        [[nodiscard]] auto generate_default_element_system(
            const parser::oc_element& elem,
            [[maybe_unused]] int sys_id) -> std::string
        {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<System>\n";
            out << "  <P Name=\"Location\">[-1, -8, 1921, 1033]</P>\n";
            out << "  <P Name=\"ZoomFactor\">100</P>\n";

            int sid = 1;
            int x = 100;
            int y = 50;

            // Generate Inport blocks
            for (const auto& sec : elem.sections) {
                if (sec.kind != "input") continue;
                int port_num = 1;
                for (const auto& var : sec.variables) {
                    out << "  <Block BlockType=\"Inport\" Name=\"" << xml_escape(var.name) << "\" SID=\"" << sid << "\">\n";
                    out << "    <P Name=\"Position\">[" << x << ", " << y << ", " << (x + 30) << ", " << (y + 14) << "]</P>\n";
                    out << "    <P Name=\"ZOrder\">" << sid << "</P>\n";
                    if (port_num > 1) {
                        out << "    <P Name=\"Port\">" << port_num << "</P>\n";
                    }
                    out << "  </Block>\n";
                    y += 50;
                    ++sid;
                    ++port_num;
                }
            }

            // Generate Outport blocks
            x = 600;
            y = 50;
            for (const auto& sec : elem.sections) {
                if (sec.kind != "output") continue;
                int port_num = 1;
                for (const auto& var : sec.variables) {
                    out << "  <Block BlockType=\"Outport\" Name=\"" << xml_escape(var.name) << "\" SID=\"" << sid << "\">\n";
                    out << "    <P Name=\"Position\">[" << x << ", " << y << ", " << (x + 30) << ", " << (y + 14) << "]</P>\n";
                    out << "    <P Name=\"ZOrder\">" << sid << "</P>\n";
                    if (port_num > 1) {
                        out << "    <P Name=\"Port\">" << port_num << "</P>\n";
                    }
                    out << "  </Block>\n";
                    y += 50;
                    ++sid;
                    ++port_num;
                }
            }

            out << "</System>";
            return out.str();
        }

        // ─── System .rels generators ────────────────────────────────────────

        [[nodiscard]] auto generate_default_system_rels(int start_id, int count) -> std::string {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n";
            out << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
            for (int i = 0; i < count; ++i) {
                int id = start_id + i;
                out << "  <Relationship Id=\"system_" << id
                    << "\" Target=\"system_" << id
                    << ".xml\" Type=\"http://schemas.mathworks.com/simulink/2010/relationships/system\"/>\n";
            }
            out << "</Relationships>";
            return out.str();
        }

        // ─── Utility ────────────────────────────────────────────────────────

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
                    default: result += c;
                }
            }
            return result;
        }

        [[nodiscard]] static auto generate_uuid() -> std::string {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, 15);

            auto hex_char = [&]() -> char {
                int v = dist(gen);
                return v < 10 ? static_cast<char>('0' + v) : static_cast<char>('a' + v - 10);
            };

            std::string uuid;
            for (int i = 0; i < 32; ++i) {
                if (i == 8 || i == 12 || i == 16 || i == 20) uuid += '-';
                uuid += hex_char();
            }
            return uuid;
        }
    };

} // namespace oc
