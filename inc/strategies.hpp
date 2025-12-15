#pragma once
#include "models.hpp"
#include <optional>
#include <vector>

struct ConflictResolutionResult {
    bool ok;
    std::optional<std::string> message;
    // For auto-bump: suggested new start time
    std::optional<std::chrono::system_clock::time_point> suggested_start;
    std::vector<BookingId> to_preempt;
};

struct IConflictStrategy {
    virtual ~IConflictStrategy() = default;
    virtual ConflictResolutionResult resolve(const Booking& candidate, const std::vector<Booking>& existing, const User& actor) = 0;
};

// RejectStrategy: отказывает на первом конфликте
struct RejectStrategy: public IConflictStrategy {
    ConflictResolutionResult resolve(const Booking& candidate, const std::vector<Booking>& existing, const User&) override {
        for (auto const& e : existing) {
            if (!(candidate.end <= e.start || candidate.start >= e.end)) {
                return {false, std::string("Conflict with booking id ") + std::to_string(e.id), std::nullopt, {}};
            }
        }
        return {true, std::nullopt, std::nullopt, {}};
    }
};

struct AutoBumpStrategy: public IConflictStrategy {
    ConflictResolutionResult resolve(
        const Booking& b,
        const std::vector<Booking>& existing,
        const User&) override {
        auto start = b.start;
        auto dur = b.end - b.start;

        bool moved = true;
        while (moved) {
            moved = false;
            for (auto const& e : existing) {
                if (!(start + dur <= e.start || start >= e.end)) {
                    start = e.end;
                    moved = true;
                }
            }
        }

        if (start != b.start) {
            return {true, "Auto-bumped", start, {}};
        }
        return {true, std::nullopt, std::nullopt, {}};
    }
};

// PreemptStrategy: если actor.priority > existing.user.priority -> удаление
struct PreemptStrategy: public IConflictStrategy {
    ConflictResolutionResult resolve(
        const Booking& candidate,
        const std::vector<Booking>& existing,
        const User& actor) override {
        std::vector<BookingId> to_preempt;

        for (auto const& e : existing) {
            if (candidate.end <= e.start || candidate.start >= e.end) {
                continue;
            }

            if (actor.priority > e.owner_priority) {
                to_preempt.push_back(e.id);
            } else {
                return {false, "Higher priority booking exists", std::nullopt, {}};
            }
        }

        return {true, "Preempt allowed", std::nullopt, to_preempt};
    }
};

// QuorumStrategy: Разрешаем бронирование, если число attendees >= quorum_size.
struct QuorumStrategy: public IConflictStrategy {
    explicit QuorumStrategy(size_t quorum_size)
        : quorum_(quorum_size) {
    }
    ConflictResolutionResult resolve(const Booking& candidate, const std::vector<Booking>& existing, const User&) override {
        for (auto const& e : existing) {
            if (!(candidate.end <= e.start || candidate.start >= e.end)) {
                if (candidate.attendees.size() >= quorum_) {
                    return {true, std::string("Allowed by quorum (") + std::to_string(quorum_) + ")", std::nullopt, {}};
                }

                return {false, std::string("Conflict and quorum not satisfied (need ") + std::to_string(quorum_) + ")", std::nullopt, {}};
            }
        }
        return {true, std::nullopt, std::nullopt, {}};
    }

private:
    size_t quorum_;
};