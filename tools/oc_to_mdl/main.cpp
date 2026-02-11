//
// Open Controls - OC to MDL Converter
//
// Copyright (C) 2026 Daher Alfawares
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "../liboc/oc_parser.hpp"
#include "../liboc/oc_metadata.hpp"
#include "mdl_writer.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <print>

namespace fs = std::filesystem;

namespace {

    void print_usage(std::string_view program) {
        std::println("Usage: {} <input-dir> [-o output.mdl]", program);
        std::println("");
        std::println("Converts OC files back to Simulink MDL format.");
        std::println("Reads .oc files and optional .oc.metadata from the input directory.");
        std::println("");
        std::println("Options:");
        std::println("  -o <file>   Output MDL file path (default: <dir-name>.mdl)");
    }

    [[nodiscard]] auto read_file(const fs::path& path) -> std::string {
        std::ifstream in(path);
        if (!in) return {};
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

} // namespace

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_dir;
    std::string output_file;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (!arg.starts_with('-')) {
            input_dir = std::string(arg);
        }
    }

    if (input_dir.empty()) {
        std::println(stderr, "Error: No input directory specified");
        print_usage(argv[0]);
        return 1;
    }

    fs::path dir_path(input_dir);
    if (!fs::is_directory(dir_path)) {
        std::println(stderr, "Error: {} is not a directory", input_dir);
        return 1;
    }

    // Derive model name from directory name (remove -oc suffix)
    // Normalize path to remove trailing slashes
    auto normalized = fs::canonical(dir_path);
    auto dir_name = normalized.filename().string();
    auto model_name = dir_name;
    if (model_name.ends_with("-oc")) {
        model_name = model_name.substr(0, model_name.size() - 3);
    }

    if (output_file.empty()) {
        output_file = model_name + ".mdl";
    }

    std::println("Input directory: {}", input_dir);
    std::println("Model name: {}", model_name);

    // Step 1: Scan for .oc files
    std::vector<fs::path> oc_paths;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.path().extension() == ".oc") {
            oc_paths.push_back(entry.path());
        }
    }

    std::ranges::sort(oc_paths);

    if (oc_paths.empty()) {
        std::println(stderr, "Error: No .oc files found in {}", input_dir);
        return 1;
    }

    std::println("Found {} .oc file(s)", oc_paths.size());

    // Step 2: Parse each .oc file
    std::vector<oc::parser::oc_file> oc_files;
    bool parse_ok = true;

    for (const auto& path : oc_paths) {
        std::println("  Parsing: {}", path.filename().string());
        auto source = read_file(path);
        if (source.empty()) {
            std::println(stderr, "  Error: Could not read {}", path.string());
            parse_ok = false;
            continue;
        }

        auto result = oc::parser::parse_string(source);
        if (!result.success) {
            std::println(stderr, "  Syntax errors in {}:", path.filename().string());
            for (const auto& err : result.errors) {
                std::println(stderr, "    {}", err.to_string());
            }
            parse_ok = false;
            continue;
        }

        oc_files.push_back(std::move(result.file));
    }

    if (!parse_ok) {
        std::println(stderr, "Error: Aborting due to parse errors");
        return 1;
    }

    // Step 3: Look for matching .oc.metadata file
    std::optional<oc::metadata::metadata> meta;

    // Search for metadata file: could be <model_name>.oc.metadata in the input dir
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.path().extension() == ".metadata" &&
            entry.path().stem().extension() == ".oc") {
            std::println("Found metadata: {}", entry.path().filename().string());
            meta = oc::metadata::read_file(entry.path().string());
            if (!meta) {
                std::println(stderr, "Warning: Could not parse metadata file, using defaults");
            }
            break;
        }
    }

    // Step 4: Generate MDL
    oc::mdl_writer writer;
    std::string mdl_content;

    if (meta) {
        std::println("Reconstructing MDL from metadata (verbatim mode)...");
        mdl_content = writer.write_with_metadata(*meta);
    } else {
        std::println("No metadata found, generating MDL with best-guess defaults...");
        mdl_content = writer.write_with_defaults(oc_files, model_name);
    }

    // Step 5: Write output
    if (std::ofstream out(output_file); out) {
        out << mdl_content;
        std::println("Written: {} ({} bytes)", output_file, mdl_content.size());
    } else {
        std::println(stderr, "Error: Could not write {}", output_file);
        return 1;
    }

    return 0;
}
