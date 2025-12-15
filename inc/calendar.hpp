#pragma once
#include <vector>
#include "models.hpp"
#include <fstream>

static std::chrono::system_clock::time_point tp_from_sec(long long s) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{s}};
}

namespace NBooking {

    struct TCalendarEvent {
        RoomId RoomIdInternal;
        UserId UserIdInternal;
        std::chrono::system_clock::time_point Start;
        std::chrono::system_clock::time_point End;
        std::string Title;
        std::string Description;
    };

    class ICalendarAdapter {
    public:
        virtual ~ICalendarAdapter() = default;
        virtual std::vector<TCalendarEvent> Fetch(
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to) = 0;
    };

    class TJsonCalendarAdapter final: public ICalendarAdapter {
    public:
        explicit TJsonCalendarAdapter(std::string file)
            : file_(std::move(file)) {
        }

        std::vector<TCalendarEvent> Fetch(
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to) override {
            std::ifstream in(file_);
            if (!in) {
                throw std::runtime_error("Cannot open calendar file: " + file_);
            }

            nlohmann::json j;
            in >> j;

            std::vector<TCalendarEvent> out;

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

                TCalendarEvent ev;
                ev.RoomIdInternal = e.at("room_id").get<RoomId>();
                ev.UserIdInternal = e.at("user_id").get<UserId>();
                ev.Start = start;
                ev.End = end;
                ev.Title = e.value("title", "");
                ev.Description = e.value("description", "");

                out.push_back(std::move(ev));
            }

            return out;
        }

    private:
        std::string file_;
    };

} // namespace NBooking