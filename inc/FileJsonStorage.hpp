#pragma once
#include "storage.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <mutex>
#include <string>

namespace NBooking {

    class TFileJsonStorage: public IStorage {
    public:
        explicit TFileJsonStorage(std::filesystem::path snapshot_path, std::filesystem::path journal_path);
        void SaveState(const nlohmann::json& snapshot) override;
        nlohmann::json LoadState() override;
        void AppendJournal(const nlohmann::json& entry) override;
        std::vector<nlohmann::json> LoadJournal() override;

    private:
        std::filesystem::path SnapshotPath;
        std::filesystem::path JournalPath;
        std::mutex Mutex_;
        void AtomicWrite(const std::filesystem::path& path, const nlohmann::json& j);
    };
} // namespace NBooking
