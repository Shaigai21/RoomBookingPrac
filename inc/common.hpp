#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <unordered_set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

using BookingId = uint64_t;
using RoomId = uint64_t;
using UserId = uint64_t;

enum class ERole {
    Admin,
    Manager,
    User
};

struct TUser {
    UserId Id;
    std::string Name;
    ERole Role;
    int Priority = 0; // for preempt strategy
};

struct TResource {
    std::string Id; // e.g., "projector-1"
};

struct TRecurrence {
    enum class Type {
        None,
        Daily,
        Weekly
    } type = Type::None;
    std::optional<std::chrono::system_clock::time_point> Until;
};

struct TBooking {
    BookingId Id;
    RoomId RoomIdInternal;
    UserId UserIdInternal;
    std::chrono::system_clock::time_point Start;
    std::chrono::system_clock::time_point End;
    TRecurrence Recurrence;
    std::string Title;
    std::string Description;
    std::vector<UserId> Attendees;
    std::vector<TResource> Resources;
    int OwnerPriority = 0;
};

struct TCreateRequest {
    TBooking Booking;
    TUser Actor;
};

namespace NBooking {

    inline void ToJSON(json& j, TBooking const& b) {
        j = json{{"id", b.Id}, {"room_id", b.RoomIdInternal}, {"user_id", b.UserIdInternal}, {"start", std::chrono::duration_cast<std::chrono::seconds>(b.Start.time_since_epoch()).count()}, {"end", std::chrono::duration_cast<std::chrono::seconds>(b.End.time_since_epoch()).count()}, {"title", b.Title}, {"description", b.Description}};
    }

    inline void FromJsonInternal(json const& j, TBooking& b) {
        b.Id = j.at("id").get<BookingId>();
        b.RoomIdInternal = j.at("room_id").get<RoomId>();
        b.UserIdInternal = j.at("user_id").get<UserId>();
        auto s = j.at("start").get<long long>();
        auto e = j.at("end").get<long long>();
        b.Start = std::chrono::system_clock::time_point(std::chrono::seconds(s));
        b.End = std::chrono::system_clock::time_point(std::chrono::seconds(e));
        if (j.contains("title")) {
            b.Title = j.at("title").get<std::string>();
        }
        if (j.contains("description")) {
            b.Description = j.at("description").get<std::string>();
        }
    }

    inline bool IntervalsOverlap(const std::chrono::system_clock::time_point& a_start,
                                 const std::chrono::system_clock::time_point& a_end,
                                 const std::chrono::system_clock::time_point& b_start,
                                 const std::chrono::system_clock::time_point& b_end) {
        return !(a_end <= b_start || a_start >= b_end);
    }

    inline std::vector<TBooking> GenerateInstances(const TBooking& b,
                                                   const std::chrono::system_clock::time_point& from,
                                                   const std::chrono::system_clock::time_point& to) {
        std::vector<TBooking> out;
        auto dur = b.End - b.Start;
        if (b.Recurrence.type == TRecurrence::Type::None) {
            if (IntervalsOverlap(b.Start, b.End, from, to)) {
                out.push_back(b);
            }
            return out;
        }

        using namespace std::chrono;
        system_clock::time_point cur_start = b.Start;
        system_clock::time_point limit = to;
        if (b.Recurrence.Until && *b.Recurrence.Until < limit) {
            limit = *b.Recurrence.Until;
        }

        const size_t MAX_INSTANCES = 10000;
        size_t counter = 0;

        while (cur_start < limit && counter < MAX_INSTANCES) {
            auto cur_end = cur_start + dur;
            if (IntervalsOverlap(cur_start, cur_end, from, to)) {
                TBooking inst = b;
                inst.Start = cur_start;
                inst.End = cur_end;
                out.push_back(std::move(inst));
            }
            if (b.Recurrence.type == TRecurrence::Type::Daily) {
                cur_start += hours(24);
            } else if (b.Recurrence.type == TRecurrence::Type::Weekly) {
                cur_start += hours(24 * 7);
            } else {
                break;
            }
            ++counter;
        }
        return out;
    }

} // namespace NBooking