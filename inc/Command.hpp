#pragma once
#include <memory>
#include <optional>
#include "common.hpp"
#include "Storage.hpp"

namespace NBooking {

    struct IRepository {
        virtual ~IRepository() = default;
        virtual BookingId CreateBooking(const TBooking& b) = 0;
        virtual void UpdateBooking(const TBooking& b) = 0;
        virtual void RemoveBooking(BookingId id) = 0;
        virtual std::optional<TBooking> GetBooking(BookingId id) = 0;
        virtual std::vector<TBooking> ListAll() = 0;
    };

    class TRepository: public IRepository {
    public:
        explicit TRepository(std::shared_ptr<IStorage> storage)
            : Storage(std::move(storage)) {
            Reload();
        }

        BookingId CreateBooking(const TBooking& b) override {
            std::lock_guard lk(Mutex_);
            TBooking nb = b;
            BookingId maxid = 0;
            for (auto const& ex : Bookings) {
                maxid = std::max(maxid, ex.first);
            }
            nb.Id = maxid + 1;
            Bookings[nb.Id] = nb;
            Persist();
            nlohmann::json je = {{"op", "create"}, {"booking", BookingToJson(nb)}};
            Storage->AppendJournal(je);
            return nb.Id;
        }

        void UpdateBooking(const TBooking& b) override {
            std::lock_guard lk(Mutex_);
            Bookings[b.Id] = b;
            Persist();
            nlohmann::json je = {{"op", "update"}, {"booking", BookingToJson(b)}};
            Storage->AppendJournal(je);
        }

        void RemoveBooking(BookingId id) override {
            std::lock_guard lk(Mutex_);
            Bookings.erase(id);
            Persist();
            nlohmann::json je = {{"op", "remove"}, {"id", id}};
            Storage->AppendJournal(je);
        }

        std::optional<TBooking> GetBooking(BookingId id) override {
            std::lock_guard lk(Mutex_);
            auto it = Bookings.find(id);
            if (it == Bookings.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        std::vector<TBooking> ListAll() override {
            std::lock_guard lk(Mutex_);
            std::vector<TBooking> out;
            out.reserve(Bookings.size());
            for (auto const& kv : Bookings) {
                out.push_back(kv.second);
            }
            return out;
        }

    private:
        void Reload() {
            std::lock_guard lk(Mutex_);
            Bookings.clear();
            nlohmann::json snap = Storage->LoadState();
            if (snap.is_object() && snap.contains("bookings") && snap["bookings"].is_array()) {
                for (auto const& jb : snap["bookings"]) {
                    TBooking b;
                    FromJSON(jb, b);
                    Bookings[b.Id] = b;
                }
            }
        }

        void Persist() {
            nlohmann::json snap = nlohmann::json::object();
            snap["bookings"] = nlohmann::json::array();
            for (auto const& kv : Bookings) {
                snap["bookings"].push_back(BookingToJson(kv.second));
            }
            Storage->SaveState(snap);
        }

        static nlohmann::json BookingToJson(const TBooking& b) {
            nlohmann::json j;
            ToJSON(j, b);
            nlohmann::json r;
            r["type"] = static_cast<int>(b.Recurrence.type);
            if (b.Recurrence.Until) {
                r["until"] = std::chrono::duration_cast<std::chrono::seconds>(b.Recurrence.Until->time_since_epoch()).count();
            }
            j["recurrence"] = r;
            j["attendees"] = nlohmann::json::array();
            for (auto a : b.Attendees) {
                j["attendees"].push_back(a);
            }
            j["resources"] = nlohmann::json::array();
            for (auto const& res : b.Resources) {
                j["resources"].push_back(res.Id);
            }
            return j;
        }

        static void FromJSON(const nlohmann::json& j, TBooking& b) {
            NBooking::FromJsonInternal(j, b);
            if (j.contains("recurrence")) {
                auto const& r = j["recurrence"];
                if (r.contains("type")) {
                    b.Recurrence.type = static_cast<TRecurrence::Type>(r["type"].get<int>());
                }
                if (r.contains("until")) {
                    long long s = r["until"].get<long long>();
                    b.Recurrence.Until = std::chrono::system_clock::time_point(std::chrono::seconds(s));
                }
            }
            if (j.contains("attendees") && j["attendees"].is_array()) {
                b.Attendees.clear();
                for (auto const& a : j["attendees"]) {
                    b.Attendees.push_back(a.get<UserId>());
                }
            }
            if (j.contains("resources") && j["resources"].is_array()) {
                b.Resources.clear();
                for (auto const& r : j["resources"]) {
                    b.Resources.push_back(TResource{r.get<std::string>()});
                }
            }
        }

    private:
        std::shared_ptr<IStorage> Storage;
        std::mutex Mutex_;
        std::unordered_map<BookingId, TBooking> Bookings;
    };

    struct ICommand {
        virtual ~ICommand() = default;
        virtual void Execute() = 0;
        virtual void Undo() = 0;
        virtual std::string Describe() const = 0;
    };

    class TCreateBookingCommand: public ICommand {
    public:
        TCreateBookingCommand(IRepository& repo, TBooking booking)
            : Repo(repo)
            , Booking(std::move(booking)) {
        }

        void Execute() override {
            if (!Executed) {
                Booking.Id = Repo.CreateBooking(Booking);
                Executed = true;
            } else {
                Repo.UpdateBooking(Booking);
            }
        }

        void Undo() override {
            if (Executed) {
                Repo.RemoveBooking(Booking.Id);
            }
        }

        std::string Describe() const {
            return "Create booking id=" + std::to_string(Booking.Id) + " title=\"" + Booking.Title + "\"";
        }

        BookingId id() const {
            return Booking.Id;
        }

    private:
        IRepository& Repo;
        TBooking Booking;
        bool Executed = false;
    };

    class TRemoveBookingCommand: public ICommand {
    public:
        TRemoveBookingCommand(IRepository& repo, BookingId id)
            : Repo(repo)
            , Id(id) {
        }

        void Execute() override {
            Old = Repo.GetBooking(Id);
            if (Old) {
                Repo.RemoveBooking(Id);
            }
        }

        void Undo() override {
            if (Old) {
                Repo.CreateBooking(*Old);
            }
        }

        std::string Describe() const {
            return "Cancel booking id=" + std::to_string(Id);
        }

    private:
        IRepository& Repo;
        BookingId Id;
        std::optional<TBooking> Old;
    };

} // namespace NBooking