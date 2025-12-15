#pragma once
#include "models.hpp"
#include <optional>
#include <vector>

struct TConflictResolutionResult {
    bool ok;
    std::optional<std::string> Message;
    // For auto-bump: suggested new start time
    std::optional<std::chrono::system_clock::time_point> SuggestedStart;
    std::vector<BookingId> ToPreempt;
};

struct IConflictStrategy {
    virtual ~IConflictStrategy() = default;
    virtual TConflictResolutionResult Resolve(const TBooking& candidate, const std::vector<TBooking>& existing, const TUser& actor) = 0;
};

// RejectStrategy: отказывает на первом конфликте
struct TRejectStrategy: public IConflictStrategy {
    TConflictResolutionResult Resolve(const TBooking& candidate, const std::vector<TBooking>& existing, const TUser&) override {
        for (auto const& e : existing) {
            if (!(candidate.End <= e.Start || candidate.Start >= e.End)) {
                return {false, std::string("Conflict with booking id ") + std::to_string(e.Id), std::nullopt, {}};
            }
        }
        return {true, std::nullopt, std::nullopt, {}};
    }
};

struct TAutoBumpStrategy: public IConflictStrategy {
    TConflictResolutionResult Resolve(
        const TBooking& b,
        const std::vector<TBooking>& existing,
        const TUser&) override {
        auto start = b.Start;
        auto dur = b.End - b.Start;

        bool moved = true;
        while (moved) {
            moved = false;
            for (auto const& e : existing) {
                if (!(start + dur <= e.Start || start >= e.End)) {
                    start = e.End;
                    moved = true;
                }
            }
        }

        if (start != b.Start) {
            return {true, "Auto-bumped", start, {}};
        }
        return {true, std::nullopt, std::nullopt, {}};
    }
};

// PreemptStrategy: если actor.priority > existing.user.priority -> удаление
struct TPreemptStrategy: public IConflictStrategy {
    TConflictResolutionResult Resolve(
        const TBooking& candidate,
        const std::vector<TBooking>& existing,
        const TUser& actor) override {
        std::vector<BookingId> to_preempt;

        for (auto const& e : existing) {
            if (candidate.End <= e.Start || candidate.Start >= e.End) {
                continue;
            }

            if (actor.Priority > e.OwnerPriority) {
                to_preempt.push_back(e.Id);
            } else {
                return {false, "Higher priority booking exists", std::nullopt, {}};
            }
        }

        return {true, "Preempt allowed", std::nullopt, to_preempt};
    }
};

// QuorumStrategy: Разрешаем бронирование, если число attendees >= quorum_size.
struct TQuorumStrategy: public IConflictStrategy {
    explicit TQuorumStrategy(size_t quorum_size)
        : Quorum(quorum_size) {
    }
    TConflictResolutionResult Resolve(const TBooking& candidate, const std::vector<TBooking>& existing, const TUser&) override {
        for (auto const& e : existing) {
            if (!(candidate.End <= e.Start || candidate.Start >= e.End)) {
                if (candidate.Attendees.size() >= Quorum) {
                    return {true, std::string("Allowed by quorum (") + std::to_string(Quorum) + ")", std::nullopt, {}};
                }

                return {false, std::string("Conflict and quorum not satisfied (need ") + std::to_string(Quorum) + ")", std::nullopt, {}};
            }
        }
        return {true, std::nullopt, std::nullopt, {}};
    }

private:
    size_t Quorum;
};