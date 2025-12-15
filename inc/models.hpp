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

enum class Role {
    Admin,
    Manager,
    User
};

struct User {
    UserId id;
    std::string name;
    Role role;
    int priority = 0; // for preempt strategy
};

struct Resource {
    std::string id; // e.g., "projector-1"
};

struct Recurrence {
    enum class Type {
        None,
        Daily,
        Weekly
    } type = Type::None;
    std::optional<std::chrono::system_clock::time_point> until;
};

struct Booking {
    BookingId id;
    RoomId room_id;
    UserId user_id;
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
    Recurrence recurrence;
    std::string title;
    std::string description;
    std::vector<UserId> attendees;
    std::vector<Resource> resources;
    int owner_priority = 0;
};

struct CreateRequest {
    Booking booking;
    User actor;
};

struct ChangeRequest {
    BookingId id;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<std::chrono::system_clock::time_point> start;
    std::optional<std::chrono::system_clock::time_point> end;
    User actor;
};

namespace booking {

    inline void to_json(json& j, Booking const& b) {
        j = json{{"id", b.id}, {"room_id", b.room_id}, {"user_id", b.user_id}, {"start", std::chrono::duration_cast<std::chrono::seconds>(b.start.time_since_epoch()).count()}, {"end", std::chrono::duration_cast<std::chrono::seconds>(b.end.time_since_epoch()).count()}, {"title", b.title}, {"description", b.description}};
    }

    inline void from_json(json const& j, Booking& b) {
        b.id = j.at("id").get<BookingId>();
        b.room_id = j.at("room_id").get<RoomId>();
        b.user_id = j.at("user_id").get<UserId>();
        auto s = j.at("start").get<long long>();
        auto e = j.at("end").get<long long>();
        b.start = std::chrono::system_clock::time_point(std::chrono::seconds(s));
        b.end = std::chrono::system_clock::time_point(std::chrono::seconds(e));
        if (j.contains("title")) {
            b.title = j.at("title").get<std::string>();
        }
        if (j.contains("description")) {
            b.description = j.at("description").get<std::string>();
        }
    }

    inline bool intervalsOverlap(const std::chrono::system_clock::time_point& a_start,
                                 const std::chrono::system_clock::time_point& a_end,
                                 const std::chrono::system_clock::time_point& b_start,
                                 const std::chrono::system_clock::time_point& b_end) {
        return !(a_end <= b_start || a_start >= b_end);
    }

    inline std::vector<Booking> generateInstances(const Booking& b,
                                                  const std::chrono::system_clock::time_point& from,
                                                  const std::chrono::system_clock::time_point& to) {
        std::vector<Booking> out;
        auto dur = b.end - b.start;
        if (b.recurrence.type == Recurrence::Type::None) {
            if (intervalsOverlap(b.start, b.end, from, to)) {
                out.push_back(b);
            }
            return out;
        }

        using namespace std::chrono;
        system_clock::time_point cur_start = b.start;
        system_clock::time_point limit = to;
        if (b.recurrence.until && *b.recurrence.until < limit) {
            limit = *b.recurrence.until;
        }

        const size_t MAX_INSTANCES = 10000;
        size_t counter = 0;

        while (cur_start < limit && counter < MAX_INSTANCES) {
            auto cur_end = cur_start + dur;
            if (intervalsOverlap(cur_start, cur_end, from, to)) {
                Booking inst = b;
                inst.start = cur_start;
                inst.end = cur_end;
                out.push_back(std::move(inst));
            }
            if (b.recurrence.type == Recurrence::Type::Daily) {
                cur_start += hours(24);
            } else if (b.recurrence.type == Recurrence::Type::Weekly) {
                cur_start += hours(24 * 7);
            } else {
                break;
            }
            ++counter;
        }
        return out;
    }

} // namespace booking