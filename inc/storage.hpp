#pragma once
#include "models.hpp"
#include <vector>
#include <mutex>
#include <optional>

struct IStorage {
    virtual ~IStorage() = default;
    virtual void SaveState(const nlohmann::json& snapshot) = 0;
    virtual nlohmann::json LoadState() = 0;
    virtual void AppendJournal(const nlohmann::json& entry) = 0;
    virtual std::vector<nlohmann::json> LoadJournal() = 0;
};

class TMemoryStorage: public IStorage {
public:
    TMemoryStorage() = default;

    void SaveState(const nlohmann::json& snapshot) override {
        std::scoped_lock lk(Mutex_);
        Snapshot = snapshot;
    }

    nlohmann::json LoadState() override {
        std::scoped_lock lk(Mutex_);
        return Snapshot;
    }

    void AppendJournal(const nlohmann::json& entry) override {
        std::scoped_lock lk(Mutex_);
        Journal.push_back(entry);
    }

    std::vector<nlohmann::json> LoadJournal() override {
        std::scoped_lock lk(Mutex_);
        return Journal;
    }

private:
    nlohmann::json Snapshot = nlohmann::json::object();
    std::vector<nlohmann::json> Journal;
    std::mutex Mutex_;
};
