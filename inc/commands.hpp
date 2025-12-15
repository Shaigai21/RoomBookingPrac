#pragma once
#include <memory>
#include <optional>
#include "models.hpp"

namespace booking {

    struct IRepository {
        virtual ~IRepository() = default;
        virtual BookingId createBooking(const Booking& b) = 0;
        virtual void updateBooking(const Booking& b) = 0;
        virtual void removeBooking(BookingId id) = 0;
        virtual std::optional<Booking> getBooking(BookingId id) = 0;
        virtual std::vector<Booking> listAll() = 0;
    };

    class Repository: public IRepository {
    public:
        explicit Repository(std::shared_ptr<IStorage> storage)
            : storage_(std::move(storage)) {
            reload();
        }

        BookingId createBooking(const Booking& b) override {
            std::lock_guard lk(mutex_);
            Booking nb = b;
            BookingId maxid = 0;
            for (auto const& ex : bookings_) {
                maxid = std::max(maxid, ex.first);
            }
            nb.id = maxid + 1;
            bookings_[nb.id] = nb;
            persist();
            nlohmann::json je = {{"op", "create"}, {"booking", bookingToJson(nb)}};
            storage_->appendJournal(je);
            return nb.id;
        }

        void updateBooking(const Booking& b) override {
            std::lock_guard lk(mutex_);
            bookings_[b.id] = b;
            persist();
            nlohmann::json je = {{"op", "update"}, {"booking", bookingToJson(b)}};
            storage_->appendJournal(je);
        }

        void removeBooking(BookingId id) override {
            std::lock_guard lk(mutex_);
            bookings_.erase(id);
            persist();
            nlohmann::json je = {{"op", "remove"}, {"id", id}};
            storage_->appendJournal(je);
        }

        std::optional<Booking> getBooking(BookingId id) override {
            std::lock_guard lk(mutex_);
            auto it = bookings_.find(id);
            if (it == bookings_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        std::vector<Booking> listAll() override {
            std::lock_guard lk(mutex_);
            std::vector<Booking> out;
            out.reserve(bookings_.size());
            for (auto const& kv : bookings_) {
                out.push_back(kv.second);
            }
            return out;
        }

    private:
        void reload() {
            std::lock_guard lk(mutex_);
            bookings_.clear();
            nlohmann::json snap = storage_->loadState();
            if (snap.is_object() && snap.contains("bookings") && snap["bookings"].is_array()) {
                for (auto const& jb : snap["bookings"]) {
                    Booking b;
                    from_json(jb, b);
                    bookings_[b.id] = b;
                }
            }
        }

        void persist() {
            nlohmann::json snap = nlohmann::json::object();
            snap["bookings"] = nlohmann::json::array();
            for (auto const& kv : bookings_) {
                snap["bookings"].push_back(bookingToJson(kv.second));
            }
            storage_->saveState(snap);
        }

        static nlohmann::json bookingToJson(const Booking& b) {
            nlohmann::json j;
            to_json(j, b);
            nlohmann::json r;
            r["type"] = static_cast<int>(b.recurrence.type);
            if (b.recurrence.until) {
                r["until"] = std::chrono::duration_cast<std::chrono::seconds>(b.recurrence.until->time_since_epoch()).count();
            }
            j["recurrence"] = r;
            j["attendees"] = nlohmann::json::array();
            for (auto a : b.attendees) {
                j["attendees"].push_back(a);
            }
            j["resources"] = nlohmann::json::array();
            for (auto const& res : b.resources) {
                j["resources"].push_back(res.id);
            }
            return j;
        }

        static void from_json(const nlohmann::json& j, Booking& b) {
            booking::from_json(j, b);
            if (j.contains("recurrence")) {
                auto const& r = j["recurrence"];
                if (r.contains("type")) {
                    b.recurrence.type = static_cast<Recurrence::Type>(r["type"].get<int>());
                }
                if (r.contains("until")) {
                    long long s = r["until"].get<long long>();
                    b.recurrence.until = std::chrono::system_clock::time_point(std::chrono::seconds(s));
                }
            }
            if (j.contains("attendees") && j["attendees"].is_array()) {
                b.attendees.clear();
                for (auto const& a : j["attendees"]) {
                    b.attendees.push_back(a.get<UserId>());
                }
            }
            if (j.contains("resources") && j["resources"].is_array()) {
                b.resources.clear();
                for (auto const& r : j["resources"]) {
                    b.resources.push_back(Resource{r.get<std::string>()});
                }
            }
        }

    private:
        std::shared_ptr<IStorage> storage_;
        std::mutex mutex_;
        std::unordered_map<BookingId, Booking> bookings_;
    };

    struct ICommand {
        virtual ~ICommand() = default;
        virtual void execute() = 0;
        virtual void undo() = 0;
        virtual std::string describe() const = 0;
    };

    class CreateBookingCommand: public ICommand {
    public:
        CreateBookingCommand(IRepository& repo, Booking booking)
            : repo_(repo)
            , booking_(std::move(booking)) {
        }

        void execute() override {
            if (!executed_) {
                booking_.id = repo_.createBooking(booking_);
                executed_ = true;
            } else {
                repo_.updateBooking(booking_);
            }
        }

        void undo() override {
            if (executed_) {
                repo_.removeBooking(booking_.id);
            }
        }

        std::string describe() const {
            return "Create booking id=" + std::to_string(booking_.id) + " title=\"" + booking_.title + "\"";
        }

        BookingId id() const {
            return booking_.id;
        }

    private:
        IRepository& repo_;
        Booking booking_;
        bool executed_ = false;
    };

    class UpdateBookingCommand: public ICommand {
    public:
        UpdateBookingCommand(IRepository& repo, Booking before, Booking after)
            : repo_(repo)
            , before_(std::move(before))
            , after_(std::move(after)) {
        }

        void execute() override {
            repo_.updateBooking(after_);
        }

        void undo() override {
            repo_.updateBooking(before_);
        }

        std::string describe() const {
            return "Update booking id=" + std::to_string(before_.id) + " title=\"" + before_.title + "\"";
        }

    private:
        IRepository& repo_;
        Booking before_;
        Booking after_;
    };

    class RemoveBookingCommand: public ICommand {
    public:
        RemoveBookingCommand(IRepository& repo, BookingId id)
            : repo_(repo)
            , id_(id) {
        }

        void execute() override {
            old_ = repo_.getBooking(id_);
            if (old_) {
                repo_.removeBooking(id_);
            }
        }

        void undo() override {
            if (old_) {
                repo_.createBooking(*old_);
            }
        }

        std::string describe() const {
            return "Cancel booking id=" + std::to_string(id_);
        }

    private:
        IRepository& repo_;
        BookingId id_;
        std::optional<Booking> old_;
    };

} // namespace booking