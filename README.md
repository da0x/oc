# Open Controls

Tools for extracting algorithm-relevant information from Simulink `.mdl` files into diff-friendly formats for version control workflows.

## The Problem

Simulink's `.mdl` format is hostile to git:
- XML-based with irrelevant metadata (positions, timestamps, UUIDs)
- Large files that are hard to diff and merge
- Simulation settings mixed with algorithm definitions

## The Solution

Extract only the algorithm-relevant information into formats designed for version control:

| Format | Purpose |
|--------|---------|
| **OC** | C++-like syntax with operations, state, and config |
| **YAML** | Interface schema (inputs, outputs, parameters) |
| **C++** | Standalone header files ready to compile |

## Building

```bash
make        # Build all tools to ./bin/
make clean  # Remove build artifacts
```

Requires C++23 compatible compiler (GCC 13+, Clang 17+).

## Tools

### mdl_to_oc

Convert MDL subsystems to OC format with full operation code:

```bash
./bin/mdl_to_oc model.mdl
```

Outputs to `model-oc/` directory.

### mdl_to_yaml

Generate YAML interface schemas:

```bash
./bin/mdl_to_yaml model.mdl
```

Outputs to `model-yaml/` directory.

### mdl_to_cpp

Generate standalone C++ header files:

```bash
./bin/mdl_to_cpp model.mdl
```

Outputs to `model-cpp/` directory.

### mdl_dump

Debug tool for inspecting MDL structure:

```bash
./bin/mdl_dump model.mdl
```

## Features

- **Transfer Function Discretization**: Tustin/bilinear transform for 1st and 2nd order systems
- **Subsystem Inlining**: Recursive inlining with proper variable prefixing
- **Topological Sorting**: Correct execution order via Kahn's algorithm
- **State Handling**: Integrators, UnitDelay, Memory blocks break feedback cycles
- **Config Extraction**: Workspace variables become configurable parameters

## Example Output

**OC Format** (`dc_voltage_regulator.oc`):
```
namespace controls_module {

element dc_voltage_regulator {
    frequency: 1kHz;

    input {
        float v_ref;
        float v_cap;
    }

    output {
        float P_request;
    }

    state {
        float Integrator_state = 0.0;
    }

    config {
        float kpFast;
        float dt = 0.001;
    }

    update {
        auto error = in.v_ref - in.v_cap;
        auto P_fast = error * cfg.kpFast;
        state.Integrator_state += error * cfg.dt;
        out.P_request = P_fast + state.Integrator_state;
    }
}

} // namespace controls_module
```

## License

GNU General Public License v3.0

Copyright (C) 2026 Daher Alfawares
