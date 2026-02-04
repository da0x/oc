//
// MDL Structure Dumper - explores what's in an MDL file
//

#include "../libmdl/oc_mdl.hpp"
#include <iostream>
#include <print>
#include <set>

void dump_system(const oc::mdl::model& model, const oc::mdl::system& sys, int depth = 0) {
    std::string indent(depth * 2, ' ');

    std::println("{}System: {} ({})", indent, sys.name.empty() ? sys.id : sys.name, sys.id);

    // Group blocks by type
    std::map<std::string, std::vector<const oc::mdl::block*>> by_type;
    for (const auto& blk : sys.blocks) {
        by_type[blk.type].push_back(&blk);
    }

    std::println("{}  Blocks ({}):", indent, sys.blocks.size());
    for (const auto& [type, blocks] : by_type) {
        std::println("{}    {} x{}", indent, type, blocks.size());
        for (const auto* blk : blocks) {
            std::print("{}      - {}", indent, blk->name);

            // Show key parameters
            if (type == "Gain") {
                if (auto g = blk->param("Gain")) std::print(" [Gain={}]", *g);
            } else if (type == "Sum") {
                if (auto i = blk->param("Inputs")) std::print(" [Inputs={}]", *i);
            } else if (type == "Saturate") {
                if (auto u = blk->param("UpperLimit")) std::print(" [Upper={}]", *u);
                if (auto l = blk->param("LowerLimit")) std::print(" [Lower={}]", *l);
            } else if (type == "Constant") {
                if (auto v = blk->param("Value")) std::print(" [Value={}]", *v);
            } else if (type == "RelationalOperator") {
                if (auto o = blk->param("Operator")) std::print(" [Op={}]", *o);
            } else if (type == "Logic") {
                if (auto o = blk->param("Operator")) std::print(" [Op={}]", *o);
            } else if (type == "Switch") {
                if (auto c = blk->param("Criteria")) std::print(" [Criteria={}]", *c);
                if (auto t = blk->param("Threshold")) std::print(" [Threshold={}]", *t);
            } else if (type == "UnitDelay" || type == "DiscreteIntegrator") {
                if (auto ic = blk->param("InitialCondition")) std::print(" [IC={}]", *ic);
            } else if (type == "Product") {
                if (auto i = blk->param("Inputs")) std::print(" [Inputs={}]", *i);
            }

            std::println("");
        }
    }

    std::println("{}  Connections ({}):", indent, sys.connections.size());
    for (const auto& conn : sys.connections) {
        auto src = conn.source_endpoint();
        auto dst = conn.destination_endpoint();

        std::string src_name = "?", dst_name = "?";
        if (src) {
            if (auto* blk = sys.find_block_by_sid(src->block_sid)) {
                src_name = blk->name + ":" + std::to_string(src->port_index);
            }
        }
        if (dst) {
            if (auto* blk = sys.find_block_by_sid(dst->block_sid)) {
                dst_name = blk->name + ":" + std::to_string(dst->port_index);
            }
        }

        if (!conn.name.empty()) {
            std::println("{}    {} -> {} [{}]", indent, src_name, dst_name, conn.name);
        } else {
            std::println("{}    {} -> {}", indent, src_name, dst_name);
        }

        // Show branches
        for (const auto& br : conn.branches) {
            if (auto bdst = oc::mdl::endpoint::parse(br.destination)) {
                if (auto* blk = sys.find_block_by_sid(bdst->block_sid)) {
                    std::println("{}      -> {}:{}", indent, blk->name, bdst->port_index);
                }
            }
        }
    }

    // Recurse into subsystems
    for (const auto& blk : sys.blocks) {
        if (blk.is_subsystem() && !blk.subsystem_ref.empty()) {
            if (auto* subsys = model.get_system(blk.subsystem_ref)) {
                oc::mdl::system named = *subsys;
                named.name = blk.name;
                dump_system(model, named, depth + 1);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println("Usage: {} <file.mdl> [subsystem_name]", argv[0]);
        return 1;
    }

    oc::mdl::parser parser;
    if (!parser.load(argv[1])) {
        std::println(stderr, "Failed to load {}", argv[1]);
        return 1;
    }

    const auto& model = parser.get_model();
    const auto* root = model.root_system();
    if (!root) {
        std::println(stderr, "No root system");
        return 1;
    }

    std::string filter = argc > 2 ? argv[2] : "";

    // Collect all block types across all systems
    std::set<std::string> all_types;
    for (const auto& [id, sys] : model.systems) {
        for (const auto& blk : sys.blocks) {
            all_types.insert(blk.type);
        }
    }

    std::println("=== All Block Types in Model ===");
    for (const auto& t : all_types) {
        std::println("  {}", t);
    }
    std::println("");

    std::println("=== Top-level Subsystems ===");
    for (const auto& blk : root->subsystems()) {
        if (!filter.empty() && blk.name.find(filter) == std::string::npos) continue;

        if (auto* subsys = model.get_system(blk.subsystem_ref)) {
            oc::mdl::system named = *subsys;
            named.name = blk.name;
            dump_system(model, named, 0);
            std::println("");
        }
    }

    return 0;
}
