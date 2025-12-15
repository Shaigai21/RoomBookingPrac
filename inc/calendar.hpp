#pragma once
#include <vector>
#include "models.hpp"
#include <fstream>

static std::chrono::system_clock::time_point tp_from_sec(long long s) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{s}};
}

namespace booking {

    struct CalendarEvent {
        RoomId room_id;
        UserId user_id;
        std::chrono::system_clock::time_point start;
        std::chrono::system_clock::time_point end;
        std::string title;
        std::string description;
    };

    class ICalendarAdapter {
    public:
        virtual ~ICalendarAdapter() = default;
        virtual std::vector<CalendarEvent> fetch(
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to) = 0;
    };

    class JsonCalendarAdapter final: public ICalendarAdapter {
    public:
        explicit JsonCalendarAdapter(std::string file)
            : file_(std::move(file)) {
        }

        std::vector<CalendarEvent> fetch(
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to) override {
            std::ifstream in(file_);
            if (!in) {
                throw std::runtime_error("Cannot open calendar file: " + file_);
            }

            nlohmann::json j;
            in >> j;

            std::vector<CalendarEvent> out;

            if (!j.is_array()) {
                return out;
            }

            for (auto const& e : j) {
                long long s = e.at("start").get<long long>();
                long long en = e.at("end").get<long long>();

                auto start = tp_from_sec(s);
                auto end = tp_from_sec(en);

                if (end <= from || start >= to) {
                    continue;
                }

                CalendarEvent ev;
                ev.room_id = e.at("room_id").get<RoomId>();
                ev.user_id = e.at("user_id").get<UserId>();
                ev.start = start;
                ev.end = end;
                ev.title = e.value("title", "");
                ev.description = e.value("description", "");

                out.push_back(std::move(ev));
            }

            return out;
        }

    private:
        std::string file_;
    };

} // namespace booking