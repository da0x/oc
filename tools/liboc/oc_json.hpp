//
// Open Controls - Minimal JSON Parser/Emitter
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
#include <variant>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <iomanip>
#include <cmath>

namespace oc::json {

    // ─────────────────────────────────────────────────────────────────────────────
    // JSON Value Type
    // ─────────────────────────────────────────────────────────────────────────────

    class value;
    using object = std::map<std::string, value>;
    using array = std::vector<value>;

    class value {
    public:
        using variant_type = std::variant<
            std::nullptr_t,
            bool,
            double,
            std::string,
            array,
            object
        >;

        value() : data_(nullptr) {}
        value(std::nullptr_t) : data_(nullptr) {}
        value(bool b) : data_(b) {}
        value(int n) : data_(static_cast<double>(n)) {}
        value(double n) : data_(n) {}
        value(const char* s) : data_(std::string(s)) {}
        value(std::string s) : data_(std::move(s)) {}
        value(array a) : data_(std::move(a)) {}
        value(object o) : data_(std::move(o)) {}

        [[nodiscard]] auto is_null() const -> bool { return std::holds_alternative<std::nullptr_t>(data_); }
        [[nodiscard]] auto is_bool() const -> bool { return std::holds_alternative<bool>(data_); }
        [[nodiscard]] auto is_number() const -> bool { return std::holds_alternative<double>(data_); }
        [[nodiscard]] auto is_string() const -> bool { return std::holds_alternative<std::string>(data_); }
        [[nodiscard]] auto is_array() const -> bool { return std::holds_alternative<array>(data_); }
        [[nodiscard]] auto is_object() const -> bool { return std::holds_alternative<object>(data_); }

        [[nodiscard]] auto as_bool() const -> bool { return std::get<bool>(data_); }
        [[nodiscard]] auto as_number() const -> double { return std::get<double>(data_); }
        [[nodiscard]] auto as_int() const -> int { return static_cast<int>(std::get<double>(data_)); }
        [[nodiscard]] auto as_string() const -> const std::string& { return std::get<std::string>(data_); }
        [[nodiscard]] auto as_array() const -> const array& { return std::get<array>(data_); }
        [[nodiscard]] auto as_array() -> array& { return std::get<array>(data_); }
        [[nodiscard]] auto as_object() const -> const object& { return std::get<object>(data_); }
        [[nodiscard]] auto as_object() -> object& { return std::get<object>(data_); }

        [[nodiscard]] auto operator[](const std::string& key) const -> const value& {
            static const value null_value;
            if (!is_object()) return null_value;
            auto& obj = as_object();
            auto it = obj.find(key);
            return it != obj.end() ? it->second : null_value;
        }

        [[nodiscard]] auto operator[](std::size_t index) const -> const value& {
            return as_array().at(index);
        }

        [[nodiscard]] auto get(const std::string& key, const value& default_val = value()) const -> const value& {
            if (!is_object()) return default_val;
            auto& obj = as_object();
            auto it = obj.find(key);
            return it != obj.end() ? it->second : default_val;
        }

        [[nodiscard]] auto contains(const std::string& key) const -> bool {
            if (!is_object()) return false;
            return as_object().count(key) > 0;
        }

        [[nodiscard]] auto size() const -> std::size_t {
            if (is_array()) return as_array().size();
            if (is_object()) return as_object().size();
            return 0;
        }

    private:
        variant_type data_;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // JSON Parser
    // ─────────────────────────────────────────────────────────────────────────────

    class parser {
        std::string_view input_;
        std::size_t pos_ = 0;

    public:
        [[nodiscard]] auto parse(std::string_view input) -> value {
            input_ = input;
            pos_ = 0;
            skip_ws();
            auto result = parse_value();
            return result;
        }

    private:
        [[nodiscard]] auto parse_value() -> value {
            skip_ws();
            if (pos_ >= input_.size()) return {};

            char c = input_[pos_];
            if (c == '"') return parse_string();
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            if (c == 't' || c == 'f') return parse_bool();
            if (c == 'n') return parse_null();
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();

            throw std::runtime_error("Unexpected character in JSON at position " + std::to_string(pos_));
        }

        [[nodiscard]] auto parse_string() -> value {
            return value(read_string());
        }

        [[nodiscard]] auto read_string() -> std::string {
            expect('"');
            std::string result;
            while (pos_ < input_.size() && input_[pos_] != '"') {
                if (input_[pos_] == '\\') {
                    ++pos_;
                    if (pos_ >= input_.size()) break;
                    switch (input_[pos_]) {
                        case '"': result += '"'; break;
                        case '\\': result += '\\'; break;
                        case '/': result += '/'; break;
                        case 'b': result += '\b'; break;
                        case 'f': result += '\f'; break;
                        case 'n': result += '\n'; break;
                        case 'r': result += '\r'; break;
                        case 't': result += '\t'; break;
                        case 'u': {
                            ++pos_;
                            // Read 4 hex digits
                            std::string hex;
                            for (int i = 0; i < 4 && pos_ < input_.size(); ++i) {
                                hex += input_[pos_++];
                            }
                            --pos_;  // will be incremented below
                            auto code = static_cast<char>(std::stoi(hex, nullptr, 16));
                            result += code;
                            break;
                        }
                        default: result += input_[pos_]; break;
                    }
                } else {
                    result += input_[pos_];
                }
                ++pos_;
            }
            expect('"');
            return result;
        }

        [[nodiscard]] auto parse_number() -> value {
            auto start = pos_;
            if (input_[pos_] == '-') ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (pos_ < input_.size() && input_[pos_] == '.') {
                ++pos_;
                while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
                while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            }
            auto num_str = std::string(input_.substr(start, pos_ - start));
            return value(std::stod(num_str));
        }

        [[nodiscard]] auto parse_bool() -> value {
            if (input_.substr(pos_, 4) == "true") { pos_ += 4; return value(true); }
            if (input_.substr(pos_, 5) == "false") { pos_ += 5; return value(false); }
            throw std::runtime_error("Invalid boolean at position " + std::to_string(pos_));
        }

        [[nodiscard]] auto parse_null() -> value {
            if (input_.substr(pos_, 4) == "null") { pos_ += 4; return value(nullptr); }
            throw std::runtime_error("Invalid null at position " + std::to_string(pos_));
        }

        [[nodiscard]] auto parse_object() -> value {
            expect('{');
            skip_ws();
            object obj;
            if (pos_ < input_.size() && input_[pos_] == '}') { ++pos_; return value(std::move(obj)); }

            while (true) {
                skip_ws();
                auto key = read_string();
                skip_ws();
                expect(':');
                skip_ws();
                obj[key] = parse_value();
                skip_ws();
                if (pos_ < input_.size() && input_[pos_] == ',') { ++pos_; continue; }
                break;
            }
            skip_ws();
            expect('}');
            return value(std::move(obj));
        }

        [[nodiscard]] auto parse_array() -> value {
            expect('[');
            skip_ws();
            array arr;
            if (pos_ < input_.size() && input_[pos_] == ']') { ++pos_; return value(std::move(arr)); }

            while (true) {
                skip_ws();
                arr.push_back(parse_value());
                skip_ws();
                if (pos_ < input_.size() && input_[pos_] == ',') { ++pos_; continue; }
                break;
            }
            skip_ws();
            expect(']');
            return value(std::move(arr));
        }

        void skip_ws() {
            while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }

        void expect(char c) {
            if (pos_ >= input_.size() || input_[pos_] != c) {
                throw std::runtime_error(std::string("Expected '") + c + "' at position " + std::to_string(pos_));
            }
            ++pos_;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // JSON Emitter
    // ─────────────────────────────────────────────────────────────────────────────

    class emitter {
    public:
        [[nodiscard]] auto emit(const value& v, int indent = 0) const -> std::string {
            std::ostringstream out;
            write_value(out, v, indent, 0);
            out << '\n';
            return out.str();
        }

    private:
        void write_value(std::ostringstream& out, const value& v, int indent, int depth) const {
            if (v.is_null()) { out << "null"; return; }
            if (v.is_bool()) { out << (v.as_bool() ? "true" : "false"); return; }
            if (v.is_number()) { write_number(out, v.as_number()); return; }
            if (v.is_string()) { write_string(out, v.as_string()); return; }
            if (v.is_array()) { write_array(out, v.as_array(), indent, depth); return; }
            if (v.is_object()) { write_object(out, v.as_object(), indent, depth); return; }
        }

        void write_number(std::ostringstream& out, double n) const {
            if (n == std::floor(n) && std::abs(n) < 1e15) {
                out << static_cast<long long>(n);
            } else {
                out << std::setprecision(17) << n;
            }
        }

        void write_string(std::ostringstream& out, const std::string& s) const {
            out << '"';
            for (char c : s) {
                switch (c) {
                    case '"': out << "\\\""; break;
                    case '\\': out << "\\\\"; break;
                    case '\b': out << "\\b"; break;
                    case '\f': out << "\\f"; break;
                    case '\n': out << "\\n"; break;
                    case '\r': out << "\\r"; break;
                    case '\t': out << "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                        } else {
                            out << c;
                        }
                }
            }
            out << '"';
        }

        void write_array(std::ostringstream& out, const array& arr, int indent, int depth) const {
            if (arr.empty()) { out << "[]"; return; }

            // Use compact format for small arrays of numbers
            bool all_simple = arr.size() <= 8;
            for (const auto& v : arr) {
                if (!v.is_number() && !v.is_bool() && !v.is_null()) { all_simple = false; break; }
            }

            if (all_simple) {
                out << "[";
                for (std::size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) out << ", ";
                    write_value(out, arr[i], indent, depth + 1);
                }
                out << "]";
                return;
            }

            out << "[\n";
            for (std::size_t i = 0; i < arr.size(); ++i) {
                write_indent(out, indent, depth + 1);
                write_value(out, arr[i], indent, depth + 1);
                if (i + 1 < arr.size()) out << ",";
                out << "\n";
            }
            write_indent(out, indent, depth);
            out << "]";
        }

        void write_object(std::ostringstream& out, const object& obj, int indent, int depth) const {
            if (obj.empty()) { out << "{}"; return; }

            out << "{\n";
            auto it = obj.begin();
            while (it != obj.end()) {
                write_indent(out, indent, depth + 1);
                write_string(out, it->first);
                out << ": ";
                write_value(out, it->second, indent, depth + 1);
                ++it;
                if (it != obj.end()) out << ",";
                out << "\n";
            }
            write_indent(out, indent, depth);
            out << "}";
        }

        void write_indent(std::ostringstream& out, int indent, int depth) const {
            for (int i = 0; i < indent * depth; ++i) out << ' ';
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Convenience functions
    // ─────────────────────────────────────────────────────────────────────────────

    [[nodiscard]] inline auto parse(std::string_view input) -> value {
        parser p;
        return p.parse(input);
    }

    [[nodiscard]] inline auto stringify(const value& v, int indent = 2) -> std::string {
        emitter e;
        return e.emit(v, indent);
    }

} // namespace oc::json
