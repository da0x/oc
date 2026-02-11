//
// Open Controls - OC Format Parser
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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace oc::parser {

    // ─────────────────────────────────────────────────────────────────────────────
    // AST Types
    // ─────────────────────────────────────────────────────────────────────────────

    struct oc_var_decl {
        std::string type;
        std::string name;
        std::string default_value;
        std::string comment;
    };

    struct oc_section {
        std::string kind;  // "input", "output", "state", "config"
        std::vector<oc_var_decl> variables;
    };

    struct oc_update_body {
        std::string raw_code;
    };

    struct oc_component {
        std::string name;
        std::vector<oc_section> sections;
        oc_update_body update;
    };

    struct oc_element {
        std::string name;
        std::string frequency;
        std::vector<oc_section> sections;
        oc_update_body update;
    };

    struct oc_namespace {
        std::string name;
        std::vector<oc_element> elements;
        std::vector<oc_component> components;
    };

    struct oc_file {
        std::vector<oc_namespace> namespaces;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Parse Error
    // ─────────────────────────────────────────────────────────────────────────────

    struct parse_error {
        int line = 0;
        int column = 0;
        std::string message;

        [[nodiscard]] auto to_string() const -> std::string {
            return std::to_string(line) + ":" + std::to_string(column) + ": " + message;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Token Types
    // ─────────────────────────────────────────────────────────────────────────────

    enum class token_type {
        // Keywords
        kw_namespace, kw_element, kw_component, kw_controller,
        kw_input, kw_output, kw_state, kw_config, kw_memory,
        kw_update, kw_operation, kw_frequency,
        // Types
        ty_float, ty_int, ty_auto,
        // Literals
        identifier, number, string_literal,
        // Punctuation
        lbrace, rbrace, lparen, rparen, semicolon, comma, colon,
        // Operators
        op_assign, op_dot, op_scope,
        // Special
        comment, eof
    };

    struct token {
        token_type type = token_type::eof;
        std::string text;
        int line = 0;
        int column = 0;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Lexer
    // ─────────────────────────────────────────────────────────────────────────────

    class lexer {
        std::string_view input_;
        std::size_t pos_ = 0;
        int line_ = 1;
        int col_ = 1;

    public:
        explicit lexer(std::string_view input) : input_(input) {}

        [[nodiscard]] auto tokenize() -> std::vector<token> {
            std::vector<token> tokens;
            while (pos_ < input_.size()) {
                skip_whitespace();
                if (pos_ >= input_.size()) break;

                auto tok = next_token();
                if (tok.type == token_type::comment) continue;  // skip comments
                tokens.push_back(std::move(tok));
            }
            tokens.push_back({token_type::eof, "", line_, col_});
            return tokens;
        }

    private:
        [[nodiscard]] auto next_token() -> token {
            int start_line = line_;
            int start_col = col_;
            char c = input_[pos_];

            // Single-line comment
            if (c == '/' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '/') {
                auto start = pos_;
                while (pos_ < input_.size() && input_[pos_] != '\n') advance();
                return {token_type::comment, std::string(input_.substr(start, pos_ - start)), start_line, start_col};
            }

            // String literal
            if (c == '"') {
                advance();
                auto start = pos_;
                while (pos_ < input_.size() && input_[pos_] != '"') {
                    if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) advance();
                    advance();
                }
                auto text = std::string(input_.substr(start, pos_ - start));
                if (pos_ < input_.size()) advance();  // skip closing quote
                return {token_type::string_literal, text, start_line, start_col};
            }

            // Punctuation
            if (c == '{') { advance(); return {token_type::lbrace, "{", start_line, start_col}; }
            if (c == '}') { advance(); return {token_type::rbrace, "}", start_line, start_col}; }
            if (c == '(') { advance(); return {token_type::lparen, "(", start_line, start_col}; }
            if (c == ')') { advance(); return {token_type::rparen, ")", start_line, start_col}; }
            if (c == ';') { advance(); return {token_type::semicolon, ";", start_line, start_col}; }
            if (c == ',') { advance(); return {token_type::comma, ",", start_line, start_col}; }
            if (c == '=') { advance(); return {token_type::op_assign, "=", start_line, start_col}; }
            if (c == '.') { advance(); return {token_type::op_dot, ".", start_line, start_col}; }

            if (c == ':') {
                if (pos_ + 1 < input_.size() && input_[pos_ + 1] == ':') {
                    advance(); advance();
                    return {token_type::op_scope, "::", start_line, start_col};
                }
                advance();
                return {token_type::colon, ":", start_line, start_col};
            }

            // Number
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && pos_ + 1 < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))) {
                auto start = pos_;
                if (c == '-') advance();
                while (pos_ < input_.size() && (std::isdigit(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '.')) advance();
                // Handle scientific notation
                if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
                    advance();
                    if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) advance();
                    while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) advance();
                }
                // Handle float suffix
                if (pos_ < input_.size() && (input_[pos_] == 'f' || input_[pos_] == 'F')) advance();
                return {token_type::number, std::string(input_.substr(start, pos_ - start)), start_line, start_col};
            }

            // Identifier or keyword
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                auto start = pos_;
                while (pos_ < input_.size() && (std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) advance();
                auto text = std::string(input_.substr(start, pos_ - start));

                auto type = classify_keyword(text);
                return {type, std::move(text), start_line, start_col};
            }

            // Unknown character — consume and return as identifier
            advance();
            return {token_type::identifier, std::string(1, c), start_line, start_col};
        }

        void skip_whitespace() {
            while (pos_ < input_.size()) {
                char c = input_[pos_];
                if (c == ' ' || c == '\t' || c == '\r') {
                    advance();
                } else if (c == '\n') {
                    advance();
                } else {
                    break;
                }
            }
        }

        void advance() {
            if (pos_ < input_.size()) {
                if (input_[pos_] == '\n') { ++line_; col_ = 1; }
                else { ++col_; }
                ++pos_;
            }
        }

        [[nodiscard]] static auto classify_keyword(const std::string& text) -> token_type {
            if (text == "namespace")  return token_type::kw_namespace;
            if (text == "element")    return token_type::kw_element;
            if (text == "component")  return token_type::kw_component;
            if (text == "controller") return token_type::kw_controller;
            if (text == "input")      return token_type::kw_input;
            if (text == "output")     return token_type::kw_output;
            if (text == "state")      return token_type::kw_state;
            if (text == "config")     return token_type::kw_config;
            if (text == "memory")     return token_type::kw_memory;
            if (text == "update")     return token_type::kw_update;
            if (text == "operation")  return token_type::kw_operation;
            if (text == "frequency")  return token_type::kw_frequency;
            if (text == "float")      return token_type::ty_float;
            if (text == "int")        return token_type::ty_int;
            if (text == "auto")       return token_type::ty_auto;
            return token_type::identifier;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Parser — Recursive Descent
    // ─────────────────────────────────────────────────────────────────────────────

    class oc_parser {
        std::vector<token> tokens_;
        std::size_t pos_ = 0;
        std::vector<parse_error> errors_;
        std::string_view source_;  // original source for raw code extraction

    public:
        [[nodiscard]] auto parse(std::string_view source) -> oc_file {
            source_ = source;
            lexer lex(source);
            tokens_ = lex.tokenize();
            pos_ = 0;
            errors_.clear();

            oc_file file;
            while (!at_end()) {
                if (check(token_type::kw_namespace)) {
                    file.namespaces.push_back(parse_namespace());
                } else {
                    error("Expected 'namespace' at top level");
                    advance();
                }
            }
            return file;
        }

        [[nodiscard]] auto has_errors() const -> bool { return !errors_.empty(); }
        [[nodiscard]] auto get_errors() const -> const std::vector<parse_error>& { return errors_; }

    private:
        // ─── Namespace ──────────────────────────────────────────────────────
        [[nodiscard]] auto parse_namespace() -> oc_namespace {
            oc_namespace ns;
            expect(token_type::kw_namespace);
            ns.name = expect_identifier();
            expect(token_type::lbrace);

            while (!check(token_type::rbrace) && !at_end()) {
                if (check(token_type::kw_element)) {
                    ns.elements.push_back(parse_element());
                } else if (check(token_type::kw_component)) {
                    ns.components.push_back(parse_component());
                } else if (check(token_type::kw_controller)) {
                    // Skip controller blocks for now — just brace-match
                    advance();
                    skip_identifier();
                    skip_brace_block();
                } else {
                    error("Expected 'element', 'component', or 'controller' inside namespace");
                    advance();
                }
            }
            expect(token_type::rbrace);
            return ns;
        }

        // ─── Element ────────────────────────────────────────────────────────
        [[nodiscard]] auto parse_element() -> oc_element {
            oc_element elem;
            expect(token_type::kw_element);
            elem.name = expect_identifier();
            expect(token_type::lbrace);

            while (!check(token_type::rbrace) && !at_end()) {
                if (check(token_type::kw_frequency)) {
                    elem.frequency = parse_frequency();
                } else if (check(token_type::kw_input) || check(token_type::kw_output) ||
                           check(token_type::kw_state) || check(token_type::kw_config) ||
                           check(token_type::kw_memory)) {
                    elem.sections.push_back(parse_section());
                } else if (check(token_type::kw_update) || check(token_type::kw_operation)) {
                    elem.update = parse_update();
                } else {
                    error("Unexpected token in element body");
                    advance();
                }
            }
            expect(token_type::rbrace);
            return elem;
        }

        // ─── Component ──────────────────────────────────────────────────────
        [[nodiscard]] auto parse_component() -> oc_component {
            oc_component comp;
            expect(token_type::kw_component);
            comp.name = expect_identifier();
            expect(token_type::lbrace);

            while (!check(token_type::rbrace) && !at_end()) {
                if (check(token_type::kw_input) || check(token_type::kw_output) ||
                    check(token_type::kw_state) || check(token_type::kw_config) ||
                    check(token_type::kw_memory)) {
                    comp.sections.push_back(parse_section());
                } else if (check(token_type::kw_update) || check(token_type::kw_operation)) {
                    comp.update = parse_update();
                } else {
                    error("Unexpected token in component body");
                    advance();
                }
            }
            expect(token_type::rbrace);
            return comp;
        }

        // ─── Frequency ──────────────────────────────────────────────────────
        [[nodiscard]] auto parse_frequency() -> std::string {
            expect(token_type::kw_frequency);
            // May have optional colon
            if (check(token_type::colon)) advance();

            // Collect everything until semicolon or newline-like break
            std::string freq;
            while (!check(token_type::semicolon) && !check(token_type::rbrace) &&
                   !check(token_type::kw_input) && !check(token_type::kw_output) &&
                   !check(token_type::kw_state) && !check(token_type::kw_config) &&
                   !check(token_type::kw_update) && !check(token_type::kw_operation) &&
                   !at_end()) {
                if (!freq.empty()) freq += " ";
                freq += current().text;
                advance();
            }
            if (check(token_type::semicolon)) advance();
            return freq;
        }

        // ─── Section (input/output/state/config/memory) ─────────────────────
        [[nodiscard]] auto parse_section() -> oc_section {
            oc_section sec;
            sec.kind = current().text;
            advance();

            // May use '{' ... '}' or ':' style
            if (check(token_type::lbrace)) {
                advance();
                while (!check(token_type::rbrace) && !at_end()) {
                    sec.variables.push_back(parse_var_decl());
                }
                expect(token_type::rbrace);
            } else if (check(token_type::colon)) {
                advance();
                // Colon-style: declarations until next section keyword or '}'
                while (!is_section_keyword() && !check(token_type::rbrace) &&
                       !check(token_type::kw_update) && !check(token_type::kw_operation) && !at_end()) {
                    sec.variables.push_back(parse_var_decl());
                }
            } else {
                // Brace block expected
                expect(token_type::lbrace);
            }
            return sec;
        }

        // ─── Variable Declaration ───────────────────────────────────────────
        [[nodiscard]] auto parse_var_decl() -> oc_var_decl {
            oc_var_decl var;

            // Type (could be a built-in type or a custom type name)
            if (is_type_token()) {
                var.type = current().text;
                advance();
            } else if (check(token_type::identifier)) {
                var.type = current().text;
                advance();
            } else {
                error("Expected type in variable declaration");
                advance();
                return var;
            }

            // Name
            if (check(token_type::identifier) || is_keyword_usable_as_name()) {
                var.name = current().text;
                advance();
            } else {
                // Type might be the name in "component_name component_name;" pattern
                // Backtrack: the "type" was actually the type, and name is next
                error("Expected variable name after type");
                return var;
            }

            // Optional default value
            if (check(token_type::op_assign)) {
                advance();
                // Collect expression until semicolon
                std::string expr;
                int paren_depth = 0;
                while (!at_end()) {
                    if (check(token_type::semicolon) && paren_depth == 0) break;
                    if (check(token_type::lparen)) paren_depth++;
                    if (check(token_type::rparen)) paren_depth--;
                    if (!expr.empty()) expr += " ";
                    expr += current().text;
                    advance();
                }
                var.default_value = expr;
            }

            if (check(token_type::semicolon)) advance();
            return var;
        }

        // ─── Update/Operation Body ──────────────────────────────────────────
        [[nodiscard]] auto parse_update() -> oc_update_body {
            oc_update_body body;
            advance();  // skip 'update' or 'operation'
            expect(token_type::lbrace);

            // Extract raw code by brace-matching
            int depth = 1;
            auto start_pos = pos_;

            while (!at_end() && depth > 0) {
                if (check(token_type::lbrace)) depth++;
                if (check(token_type::rbrace)) {
                    depth--;
                    if (depth == 0) break;
                }
                advance();
            }

            // Reconstruct code from tokens between start and current
            std::ostringstream code;
            for (auto i = start_pos; i < pos_; ++i) {
                // Reconstruct spacing heuristically
                if (i > start_pos) {
                    auto& prev = tokens_[i - 1];
                    auto& curr = tokens_[i];
                    if (curr.line > prev.line) {
                        for (int l = 0; l < curr.line - prev.line; ++l) code << '\n';
                        for (int s = 1; s < curr.column; ++s) code << ' ';
                    } else if (curr.column > prev.column + static_cast<int>(prev.text.size())) {
                        int spaces = curr.column - (prev.column + static_cast<int>(prev.text.size()));
                        for (int s = 0; s < spaces; ++s) code << ' ';
                    } else {
                        code << ' ';
                    }
                } else {
                    // Leading whitespace for first token
                    if (!tokens_.empty() && start_pos < tokens_.size()) {
                        auto& first = tokens_[start_pos];
                        if (first.line > tokens_[start_pos > 0 ? start_pos - 1 : 0].line) {
                            code << '\n';
                            for (int s = 1; s < first.column; ++s) code << ' ';
                        }
                    }
                }
                code << tokens_[i].text;
            }

            body.raw_code = code.str();
            expect(token_type::rbrace);
            return body;
        }

        // ─── Helpers ────────────────────────────────────────────────────────
        [[nodiscard]] auto current() const -> const token& { return tokens_[pos_]; }
        [[nodiscard]] auto at_end() const -> bool { return pos_ >= tokens_.size() || tokens_[pos_].type == token_type::eof; }

        [[nodiscard]] auto check(token_type type) const -> bool {
            return !at_end() && tokens_[pos_].type == type;
        }

        void advance() {
            if (!at_end()) ++pos_;
        }

        void expect(token_type type) {
            if (!check(type)) {
                error("Expected '" + token_name(type) + "', got '" + (at_end() ? "EOF" : current().text) + "'");
                return;
            }
            advance();
        }

        [[nodiscard]] auto expect_identifier() -> std::string {
            if (check(token_type::identifier)) {
                auto text = current().text;
                advance();
                return text;
            }
            // Allow keywords to be used as identifiers in name positions
            if (is_keyword_usable_as_name()) {
                auto text = current().text;
                advance();
                return text;
            }
            error("Expected identifier, got '" + (at_end() ? "EOF" : current().text) + "'");
            return "<error>";
        }

        void skip_identifier() {
            if (check(token_type::identifier) || is_keyword_usable_as_name()) advance();
        }

        void skip_brace_block() {
            if (!check(token_type::lbrace)) return;
            advance();
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (check(token_type::lbrace)) depth++;
                if (check(token_type::rbrace)) depth--;
                advance();
            }
        }

        [[nodiscard]] auto is_type_token() const -> bool {
            return check(token_type::ty_float) || check(token_type::ty_int) || check(token_type::ty_auto);
        }

        [[nodiscard]] auto is_keyword_usable_as_name() const -> bool {
            if (at_end()) return false;
            auto t = tokens_[pos_].type;
            // Allow certain keywords in name positions
            return t == token_type::kw_input || t == token_type::kw_output ||
                   t == token_type::kw_state || t == token_type::kw_config ||
                   t == token_type::kw_memory;
        }

        [[nodiscard]] auto is_section_keyword() const -> bool {
            return check(token_type::kw_input) || check(token_type::kw_output) ||
                   check(token_type::kw_state) || check(token_type::kw_config) ||
                   check(token_type::kw_memory) || check(token_type::kw_frequency);
        }

        void error(const std::string& msg) {
            parse_error err;
            if (!at_end()) {
                err.line = current().line;
                err.column = current().column;
            }
            err.message = msg;
            errors_.push_back(std::move(err));
        }

        [[nodiscard]] static auto token_name(token_type t) -> std::string {
            switch (t) {
                case token_type::kw_namespace: return "namespace";
                case token_type::kw_element: return "element";
                case token_type::kw_component: return "component";
                case token_type::kw_controller: return "controller";
                case token_type::kw_input: return "input";
                case token_type::kw_output: return "output";
                case token_type::kw_state: return "state";
                case token_type::kw_config: return "config";
                case token_type::kw_memory: return "memory";
                case token_type::kw_update: return "update";
                case token_type::kw_operation: return "operation";
                case token_type::kw_frequency: return "frequency";
                case token_type::ty_float: return "float";
                case token_type::ty_int: return "int";
                case token_type::ty_auto: return "auto";
                case token_type::identifier: return "identifier";
                case token_type::number: return "number";
                case token_type::string_literal: return "string";
                case token_type::lbrace: return "{";
                case token_type::rbrace: return "}";
                case token_type::lparen: return "(";
                case token_type::rparen: return ")";
                case token_type::semicolon: return ";";
                case token_type::comma: return ",";
                case token_type::colon: return ":";
                case token_type::op_assign: return "=";
                case token_type::op_dot: return ".";
                case token_type::op_scope: return "::";
                case token_type::comment: return "comment";
                case token_type::eof: return "EOF";
            }
            return "unknown";
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // Convenience: load and parse a file
    // ─────────────────────────────────────────────────────────────────────────────

    struct parse_result {
        oc_file file;
        std::vector<parse_error> errors;
        bool success = false;
    };

    [[nodiscard]] inline auto parse_string(std::string_view source) -> parse_result {
        oc_parser p;
        auto file = p.parse(source);
        return {std::move(file), p.get_errors(), !p.has_errors()};
    }

} // namespace oc::parser
