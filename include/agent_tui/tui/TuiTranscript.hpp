#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace agent_tui {

enum class TuiTranscriptCellKind {
    System,
    User,
    AssistantStreaming,
    AssistantDone,
    Agent,
    ToolCall,
    ToolResult,
    ApprovalRequired,
    ApprovalDenied,
    ApprovalFeedback,
    Error,
};

struct TuiTranscriptCell {
    TuiTranscriptCellKind kind = TuiTranscriptCellKind::System;
    std::string title;
    std::string body;
};

class TuiTranscript {
public:
    void clear() {
        cells_.clear();
        active_assistant_ = npos();
        active_progress_ = npos();
    }

    void add_system(std::string body) { add(TuiTranscriptCellKind::System, {}, std::move(body)); }
    void add_user(std::string body) { add(TuiTranscriptCellKind::User, {}, std::move(body)); }
    void add_agent(std::string body) {
        finish_agent_progress();
        add(TuiTranscriptCellKind::Agent, {}, std::move(body));
    }
    void add_tool_call(std::string name, std::string summary) { add(TuiTranscriptCellKind::ToolCall, std::move(name), std::move(summary)); }
    void add_tool_result(std::string name, std::string summary) { add(TuiTranscriptCellKind::ToolResult, std::move(name), std::move(summary)); }
    void add_approval_required(std::string name, std::string summary) { add(TuiTranscriptCellKind::ApprovalRequired, std::move(name), std::move(summary)); }
    void add_approval_denied(std::string name, std::string summary) { add(TuiTranscriptCellKind::ApprovalDenied, std::move(name), std::move(summary)); }
    void add_approval_feedback(std::string name, std::string summary) { add(TuiTranscriptCellKind::ApprovalFeedback, std::move(name), std::move(summary)); }
    void add_error(std::string body) { add(TuiTranscriptCellKind::Error, {}, std::move(body)); }

    void set_or_update_agent_progress(std::string body) {
        if (active_progress_ < cells_.size()) {
            cells_[active_progress_].body = std::move(body);
            return;
        }
        cells_.push_back(TuiTranscriptCell{TuiTranscriptCellKind::Agent, {}, std::move(body)});
        active_progress_ = cells_.size() - 1;
    }

    void finish_agent_progress() {
        active_progress_ = npos();
    }

    void start_assistant_stream() {
        finish_agent_progress();
        if (active_assistant_ >= cells_.size()) {
            add(TuiTranscriptCellKind::AssistantStreaming, {}, {});
            active_assistant_ = cells_.size() - 1;
        }
    }

    void append_assistant_delta(const std::string& delta) {
        start_assistant_stream();
        cells_[active_assistant_].body += delta;
    }

    void finish_assistant_stream() {
        if (active_assistant_ < cells_.size()) {
            cells_[active_assistant_].kind = TuiTranscriptCellKind::AssistantDone;
        }
        active_assistant_ = npos();
    }

    void add_assistant_done(std::string body) {
        finish_agent_progress();
        add(TuiTranscriptCellKind::AssistantDone, {}, std::move(body));
        active_assistant_ = npos();
    }

    std::vector<std::string> render_lines(std::size_t width, std::size_t max_cells = 12) const {
        std::vector<std::string> lines;
        const std::size_t start = cells_.size() > max_cells ? cells_.size() - max_cells : 0;
        for (std::size_t i = start; i < cells_.size(); ++i) {
            auto rendered = render_cell_lines(i, width);
            lines.insert(lines.end(), rendered.begin(), rendered.end());
        }
        return lines;
    }

    std::vector<std::string> render_cell_lines(std::size_t index, std::size_t width) const {
        if (index >= cells_.size()) {
            return {};
        }

        const auto& cell = cells_[index];
        const auto text = cell_text(cell);
        const auto wrapped = wrap_text_for_terminal(text, width > 10 ? width - 10 : width);
        const auto label = std::string{cell_label(cell.kind)};

        std::vector<std::string> lines;
        if (wrapped.empty()) {
            lines.push_back("  " + label + " > ");
            return lines;
        }
        lines.push_back("  " + label + " > " + wrapped.front());
        for (std::size_t i = 1; i < wrapped.size(); ++i) {
            lines.push_back("       " + wrapped[i]);
        }
        return lines;
    }

    bool empty() const { return cells_.empty(); }
    std::size_t size() const { return cells_.size(); }

    static std::vector<std::string> wrap_text_for_terminal(const std::string& text, std::size_t width) {
        if (text.empty()) {
            return {};
        }
        if (width == 0) {
            return {text};
        }

        std::vector<std::string> lines;
        std::string current;
        std::size_t current_width = 0;

        for (std::size_t i = 0; i < text.size();) {
            const auto glyph = next_utf8_glyph(text, i);
            i += glyph.bytes;

            if (glyph.codepoint == '\r') {
                continue;
            }
            if (glyph.codepoint == '\n') {
                lines.push_back(current);
                current.clear();
                current_width = 0;
                continue;
            }

            const auto glyph_width = terminal_glyph_width(glyph.codepoint);
            if (!current.empty() && current_width + glyph_width > width) {
                lines.push_back(current);
                current.clear();
                current_width = 0;
            }

            current.append(text, glyph.offset, glyph.bytes);
            current_width += glyph_width;
        }

        if (!current.empty() || lines.empty()) {
            lines.push_back(current);
        }
        return lines;
    }

    static const char* cell_label(TuiTranscriptCellKind kind) {
        switch (kind) {
            case TuiTranscriptCellKind::System:
                return "system";
            case TuiTranscriptCellKind::User:
                return "user";
            case TuiTranscriptCellKind::AssistantStreaming:
                return "assistant streaming";
            case TuiTranscriptCellKind::AssistantDone:
                return "assistant done";
            case TuiTranscriptCellKind::Agent:
                return "agent";
            case TuiTranscriptCellKind::ToolCall:
                return "tool call";
            case TuiTranscriptCellKind::ToolResult:
                return "tool result";
            case TuiTranscriptCellKind::ApprovalRequired:
                return "approval required";
            case TuiTranscriptCellKind::ApprovalDenied:
                return "approval denied";
            case TuiTranscriptCellKind::ApprovalFeedback:
                return "approval feedback";
            case TuiTranscriptCellKind::Error:
                return "error";
        }
        return "log";
    }

private:
    struct Utf8Glyph {
        std::size_t offset = 0;
        std::size_t bytes = 1;
        std::uint32_t codepoint = 0;
    };

    static constexpr std::size_t npos() { return static_cast<std::size_t>(-1); }

    void add(TuiTranscriptCellKind kind, std::string title, std::string body) {
        finish_agent_progress();
        cells_.push_back(TuiTranscriptCell{kind, std::move(title), std::move(body)});
    }

    static std::string cell_text(const TuiTranscriptCell& cell) {
        if (cell.title.empty()) {
            return cell.body;
        }
        if (cell.body.empty()) {
            return cell.title;
        }
        return cell.title + " " + cell.body;
    }

    static Utf8Glyph next_utf8_glyph(const std::string& text, std::size_t offset) {
        const auto first = static_cast<unsigned char>(text[offset]);
        if ((first & 0x80U) == 0) {
            return Utf8Glyph{offset, 1, first};
        }

        auto continuation = [&](std::size_t index) {
            return index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U;
        };

        if ((first & 0xE0U) == 0xC0U && continuation(offset + 1)) {
            const auto b1 = static_cast<unsigned char>(text[offset + 1]);
            const auto cp = ((first & 0x1FU) << 6U) | (b1 & 0x3FU);
            return Utf8Glyph{offset, 2, cp};
        }
        if ((first & 0xF0U) == 0xE0U && continuation(offset + 1) && continuation(offset + 2)) {
            const auto b1 = static_cast<unsigned char>(text[offset + 1]);
            const auto b2 = static_cast<unsigned char>(text[offset + 2]);
            const auto cp = ((first & 0x0FU) << 12U) | ((b1 & 0x3FU) << 6U) | (b2 & 0x3FU);
            return Utf8Glyph{offset, 3, cp};
        }
        if ((first & 0xF8U) == 0xF0U && continuation(offset + 1) && continuation(offset + 2) && continuation(offset + 3)) {
            const auto b1 = static_cast<unsigned char>(text[offset + 1]);
            const auto b2 = static_cast<unsigned char>(text[offset + 2]);
            const auto b3 = static_cast<unsigned char>(text[offset + 3]);
            const auto cp = ((first & 0x07U) << 18U) | ((b1 & 0x3FU) << 12U) | ((b2 & 0x3FU) << 6U) | (b3 & 0x3FU);
            return Utf8Glyph{offset, 4, cp};
        }
        return Utf8Glyph{offset, 1, first};
    }

    static std::size_t terminal_glyph_width(std::uint32_t cp) {
        if (cp == '\t') {
            return 4;
        }
        if (cp == 0 || cp < 32 || (cp >= 0x7FU && cp < 0xA0U)) {
            return 0;
        }
        if ((cp >= 0x0300U && cp <= 0x036FU) ||
            (cp >= 0x1AB0U && cp <= 0x1AFFU) ||
            (cp >= 0x1DC0U && cp <= 0x1DFFU) ||
            (cp >= 0x20D0U && cp <= 0x20FFU) ||
            (cp >= 0xFE20U && cp <= 0xFE2FU)) {
            return 0;
        }
        if ((cp >= 0x1100U && cp <= 0x115FU) ||
            (cp >= 0x2329U && cp <= 0x232AU) ||
            (cp >= 0x2E80U && cp <= 0xA4CFU) ||
            (cp >= 0xAC00U && cp <= 0xD7A3U) ||
            (cp >= 0xF900U && cp <= 0xFAFFU) ||
            (cp >= 0xFE10U && cp <= 0xFE19U) ||
            (cp >= 0xFE30U && cp <= 0xFE6FU) ||
            (cp >= 0xFF00U && cp <= 0xFF60U) ||
            (cp >= 0xFFE0U && cp <= 0xFFE6U)) {
            return 2;
        }
        return 1;
    }

    std::vector<TuiTranscriptCell> cells_;
    std::size_t active_assistant_ = npos();
    std::size_t active_progress_ = npos();
};

}  // namespace agent_tui
