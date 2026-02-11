//
// Open Controls - MDL to OC/YAML Converter
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "../libmdl/oc_mdl.hpp"
#include "../mdl_to_yaml/yaml_writer.hpp"
#include "oc_writer.hpp"
#include "metadata_writer.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <print>

namespace fs = std::filesystem;

namespace {

    void print_usage(std::string_view program) {
        std::println("Usage: {} <input.mdl>", program);
        std::println("");
        std::println("Converts a Simulink MDL file to both YAML and OC formats.");
        std::println("Output directories are created based on the model file name:");
        std::println("  - <model_name>-yaml/  for YAML schema files");
        std::println("  - <model_name>-oc/    for Open Controls files");
    }

    [[nodiscard]] auto to_lowercase(std::string_view str) -> std::string {
        std::string result;
        result.reserve(str.size());
        std::ranges::transform(str, std::back_inserter(result),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    [[nodiscard]] auto sanitize_filename(std::string_view name) -> std::string {
        std::string result;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                result += c;
            } else if (c == ' ') {
                result += '_';
            }
        }
        return result;
    }

} // namespace

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.starts_with('-')) {
            input_file = std::string(arg);
        }
    }

    if (input_file.empty()) {
        std::println(stderr, "Error: No input file specified");
        print_usage(argv[0]);
        return 1;
    }

    fs::path input_path(input_file);
    auto model_name = input_path.stem().string();
    auto yaml_dir = model_name + "-yaml";
    auto oc_dir = model_name + "-oc";

    std::println("Loading MDL file: {}", input_file);

    oc::mdl::parser parser;
    if (!parser.load(input_file)) {
        std::println(stderr, "Error: Failed to parse MDL file");
        return 1;
    }

    const auto& model = parser.get_model();

    std::println("Model UUID: {}", model.uuid);
    std::println("Library Type: {}", model.library_type);
    std::println("Systems: {}", model.systems.size());

    fs::create_directories(yaml_dir);
    fs::create_directories(oc_dir);

    const auto* root = model.root_system();
    if (!root) {
        std::println(stderr, "Error: No root system found");
        return 1;
    }

    auto library_name = to_lowercase(model_name);
    if (library_name.size() > 4 && library_name.ends_with("_lib")) {
        library_name = library_name.substr(0, library_name.size() - 4);
    }

    oc::yaml::converter yaml_converter;
    yaml_converter.set_model(&model);
    oc::yaml::writer yaml_writer;

    oc::oc_writer oc_converter;
    oc_converter.set_model(&model);

    int yaml_exported = 0;
    int oc_exported = 0;

    std::println("\nExporting...");

    for (const auto& blk : root->subsystems()) {
        if (blk.subsystem_ref.empty()) continue;

        const auto* subsys = model.get_system(blk.subsystem_ref);
        if (!subsys) {
            std::println(stderr, "  Warning: Could not find system {}", blk.subsystem_ref);
            continue;
        }

        oc::mdl::system named_sys = *subsys;
        named_sys.name = blk.name;

        auto base_filename = sanitize_filename(blk.name);

        // Export YAML
        auto schema = yaml_converter.convert(named_sys, library_name);
        auto yaml_content = yaml_writer.write(schema);

        auto yaml_filename = base_filename + "_schema.yaml";
        auto yaml_path = fs::path(yaml_dir) / yaml_filename;

        if (std::ofstream yaml_out(yaml_path); yaml_out) {
            yaml_out << yaml_content;
            ++yaml_exported;
        } else {
            std::println(stderr, "  Error: Could not write {}", yaml_path.string());
        }

        // Export OC
        auto oc_content = oc_converter.convert(named_sys, library_name);

        auto oc_filename = base_filename + ".oc";
        auto oc_path = fs::path(oc_dir) / oc_filename;

        if (std::ofstream oc_out(oc_path); oc_out) {
            oc_out << oc_content;
            ++oc_exported;
        } else {
            std::println(stderr, "  Error: Could not write {}", oc_path.string());
        }

        std::println("  {}", blk.name);
    }

    std::println("\nExported {} YAML schema(s) to {}/", yaml_exported, yaml_dir);
    std::println("Exported {} OC file(s) to {}/", oc_exported, oc_dir);

    // Export metadata
    oc::metadata_writer meta_writer;
    auto meta = meta_writer.build_metadata(model, parser.get_opc());

    auto metadata_path = fs::path(oc_dir) / (model_name + ".oc.metadata");
    if (oc::metadata::write_file(metadata_path.string(), meta)) {
        std::println("Exported metadata to {}", metadata_path.string());
    } else {
        std::println(stderr, "Error: Could not write metadata file");
    }

    return 0;
}
