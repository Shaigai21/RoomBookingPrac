#include <BookingManager.hpp>

#include <iostream>
#include <sstream>
#include <chrono>
#include <memory>

using namespace NBooking;

int main() {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);
    // auto strategy = std::make_shared<TRejectStrategy>();
    auto strategy = std::make_shared<TPreemptStrategy>();
    // auto strategy = std::make_shared<TQuorumStrategy>(3);

    TBookingManager mgr(repo, storage, strategy);

    std::cout << "Simple Booking CLI. Commands:\n"
              << "  login <id> <name> <role:Admin|Manager|User>  -- authenticate as user\n"
              << "  create <room> <hours> <title (no-spaces)> <description (no-spaces)>\n"
              << "  list <room>\n"
              << "  cancel <id>\n"
              << "  undo\n"
              << "  redo\n"
              << "  exit\n";

    TUser current{0, "guest", ERole::User, 0};

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) {
            continue;
        }
        if (cmd == "exit") {
            break;
        }

        try {
            if (cmd == "login") {
                int id;
                std::string name;
                std::string role;
                iss >> id >> name >> role;
                ERole r = ERole::User;
                if (role == "Admin") {
                    r = ERole::Admin;
                } else if (role == "Manager") {
                    r = ERole::Manager;
                }
                current = TUser{static_cast<UserId>(id), name, r, (r == ERole::Admin ? 100 : (r == ERole::Manager ? 50 : 10))};
                std::cout << "Logged in as " << name << " role=" << role << "\n";
                continue;
            }

            if (cmd == "create") {
                RoomId rid;
                int hours;
                std::string title;
                std::string desc;
                iss >> rid >> hours >> title >> desc;
                if (!iss) {
                    std::cout << "Usage: create <room> <hours> <title> <description>\n";
                    continue;
                }
                TBooking b;
                b.RoomIdInternal = rid;
                b.UserIdInternal = current.Id;
                b.Start = std::chrono::system_clock::now();
                b.End = b.Start + std::chrono::hours(hours);
                b.Title = title;
                b.Description = desc;
                b.Attendees = {};
                auto id = mgr.CreateBooking(b, current);
                if (id) {
                    std::cout << "Created booking with id=" << *id << "\n";
                } else {
                    std::cout << "Create failed (conflict or access denied)\n";
                }
                continue;
            }

            if (cmd == "list") {
                RoomId rid;
                iss >> rid;
                if (!iss) {
                    rid = 1;
                }
                auto now = std::chrono::system_clock::now();
                auto items = mgr.ListBookings(rid, now - std::chrono::hours(24), now + std::chrono::hours(24));
                for (auto& b : items) {
                    auto start_s = std::chrono::duration_cast<std::chrono::seconds>(b.Start.time_since_epoch()).count();
                    auto end_s = std::chrono::duration_cast<std::chrono::seconds>(b.End.time_since_epoch()).count();
                    std::cout << "id=" << b.Id << " title=\"" << b.Title << "\" start=" << start_s << " end=" << end_s << " owner=" << b.UserIdInternal << "\n";
                }
                continue;
            }

            if (cmd == "cancel") {
                BookingId id;
                iss >> id;
                if (!iss) {
                    std::cout << "Usage: cancel <id>\n";
                    continue;
                }
                bool ok = mgr.CancelBooking(id, current);
                std::cout << (ok ? "Cancelled" : "Not found") << " id=" << id << "\n";
                continue;
            }

            if (cmd == "undo") {
                auto res = mgr.Undo();
                if (res) {
                    std::cout << *res << "\n";
                } else {
                    std::cout << "Nothing to undo\n";
                }
                continue;
            }

            if (cmd == "redo") {
                auto res = mgr.Redo();
                if (res) {
                    std::cout << *res << "\n";
                } else {
                    std::cout << "Nothing to redo\n";
                }
                continue;
            }

            std::cout << "Unknown command\n";
        } catch (const std::exception& ex) {
            std::cout << "Error: " << ex.what() << "\n";
        }
    }

    return 0;
}
