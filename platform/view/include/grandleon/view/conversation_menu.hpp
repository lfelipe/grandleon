// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/map_conversation.hpp>
#include <grandleon/view/console_text.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace grandleon::view {

// The board submits Talk only when confirm() returns a final answer ID.
class ConversationMenu final {
public:
    static constexpr std::size_t columns = 37;
    static constexpr std::size_t lines_per_page = 3;

    void start(core::MapConversation offer) {
        offer_ = std::move(offer);
        question_ = 0;
        selected_ = 0;
        offset_ = 0;
        reading_answer_ = false;
        present();
    }

    [[nodiscard]] bool active() const { return !offer_.questions.empty(); }
    [[nodiscard]] bool choices_visible() const { return !reading_answer_ && !more(); }
    [[nodiscard]] bool more() const { return text()[next_] != '\0'; }
    [[nodiscard]] std::size_t selected() const { return selected_; }
    [[nodiscard]] std::size_t answer_count() const { return question().answers.size(); }
    [[nodiscard]] std::uint16_t question_id() const { return question().id; }

    [[nodiscard]] const std::string& line(std::size_t index) const { return lines_[index]; }
    [[nodiscard]] const std::string& label(std::size_t index) const { return labels_[index]; }

    void move(int step) {
        if (!choices_visible()) return;
        const auto count = static_cast<int>(answer_count());
        selected_ = static_cast<std::size_t>((static_cast<int>(selected_) + step + count) % count);
    }

    std::uint16_t confirm() {
        if (more()) {
            offset_ = next_;
            present();
            return 0;
        }
        if (!reading_answer_ && label_is_shortened_[selected_]) {
            reading_answer_ = true;
            offset_ = 0;
            present();
            return 0;
        }
        const auto& chosen = question().answers[selected_];
        if (chosen.next == 0) return chosen.result;
        const auto next_question = std::find_if(offer_.questions.begin(), offer_.questions.end(),
            [&](const auto& candidate) { return candidate.id == chosen.next; });
        question_ = static_cast<std::size_t>(next_question - offer_.questions.begin());
        offset_ = 0;
        selected_ = 0;
        reading_answer_ = false;
        present();
        return 0;
    }

private:
    [[nodiscard]] const core::ConversationQuestion& question() const {
        return offer_.questions[question_];
    }

    [[nodiscard]] const char* text() const {
        return reading_answer_ ? question().answers[selected_].text.c_str() : question().text.c_str();
    }

    static bool separator(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    static const char* wrapped_line(const char* start, std::string& result, std::size_t width) {
        const char* cursor = start;
        const char* last_space = nullptr;
        for (std::size_t col = 0; col < width && *cursor && *cursor != '\n'; ++col) {
            const char* glyph = cursor;
            if (separator(grandleon_text_next(&cursor))) last_space = glyph;
        }
        const char* end = cursor;
        if (*cursor && !separator(*cursor) && last_space && last_space != start)
            end = last_space;
        result.assign(start, end);
        // Consume the break and spaces before the next line.
        cursor = end;
        while (separator(*cursor)) ++cursor;
        return cursor;
    }

    void present() {
        for (std::size_t index = 0; index < answer_count(); ++index) {
            const char* answer = question().answers[index].text.c_str();
            const char* remaining = wrapped_line(answer, labels_[index], columns - 3);
            label_is_shortened_[index] = *remaining != '\0';
            if (label_is_shortened_[index]) labels_[index] += "...";
        }
        const char* cursor = text() + offset_;
        for (auto& line : lines_) cursor = wrapped_line(cursor, line, columns);
        next_ = static_cast<std::size_t>(cursor - text());
    }

    core::MapConversation offer_;
    std::size_t question_{};
    std::size_t selected_{};
    std::size_t offset_{};
    std::size_t next_{};
    bool reading_answer_{};
    std::array<std::string, lines_per_page> lines_;
    std::array<std::string, 4> labels_;
    std::array<bool, 4> label_is_shortened_{};
};

}  // namespace grandleon::view
