# Open Controls - MDL to OC Converter
# Copyright (C) 2026 Daher Alfawares

CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -O2
TARGET := mdl_to_oc

SRCS := main.cpp
HDRS := oc_writer.hpp ../libmdl/oc_mdl.hpp ../mdl_to_yaml/yaml_writer.hpp

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
