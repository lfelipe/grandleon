// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/core/content_identity.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace grandleon::core {

struct ConversationAnswer final {
    std::string text;
    std::uint16_t next{};
    std::uint16_t result{};
};

struct ConversationQuestion final {
    std::uint16_t id{};
    std::string text;
    std::vector<ConversationAnswer> answers;
};

struct MapConversation final {
    std::uint64_t result_flag{};
    std::string key;
    std::vector<ConversationQuestion> questions;
};

[[nodiscard]] inline bool valid_map_conversation(const MapConversation& offer) {
    if (offer.key.empty() || offer.key.size() > 96 || offer.result_flag == 0 ||
        stable_content_id_v1(offer.key) != offer.result_flag || offer.questions.empty() ||
        offer.questions.size() > 32) return false;
    std::set<std::uint16_t> seen_questions;
    std::set<std::uint16_t> seen_answers;
    for (auto question = offer.questions.rbegin(); question != offer.questions.rend();
         ++question) {
        if (question->id == 0 || question->text.empty() ||
            question->text.size() > 1024 || question->answers.size() < 2 ||
            question->answers.size() > 4) return false;
        for (const auto& answer : question->answers) {
            if (answer.text.empty() || answer.text.size() > 240 ||
                answer.result == 0 || answer.result > 32767 ||
                !seen_answers.insert(answer.result).second ||
                (answer.next != 0 && seen_questions.count(answer.next) == 0)) {
                return false;
            }
        }
        if (!seen_questions.insert(question->id).second) return false;
    }
    std::set<std::uint16_t> reachable{offer.questions.front().id};
    for (const auto& question : offer.questions) {
        if (reachable.count(question.id) == 0) return false;
        for (const auto& answer : question.answers) {
            if (answer.next != 0) reachable.insert(answer.next);
        }
    }
    return true;
}

}  // namespace grandleon::core
