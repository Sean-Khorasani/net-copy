#pragma once

#include <vector>
#include <string>
#include <memory>
#include "file/file_manager.h"

namespace netcopy {
namespace file {

class SynchronizationManager {
public:
    // Constructor and initialization
    SynchronizationManager();
    ~SynchronizationManager();
    
    // Main synchronization functions
    bool start_synchronization(const std::string& local_path, const std::string& remote_path);
    bool stop_synchronization(const std::string& sync_id);
    bool resume_synchronization(const std::string& sync_id);
    
    // Configuration methods
    void set_conflict_resolution_policy(FileManager::ConflictResolution policy);
    void set_sync_interval(uint32_t interval_seconds);
    
    // Status reporting
    struct SyncStatus {
        std::string sync_id;
        std::string local_path;
        std::string remote_path;
        uint64_t files_transferred;
        uint64_t total_files;
        bool is_active;
        std::string status_message;
    };
    
    std::vector<SyncStatus> get_active_syncs() const;
    SyncStatus get_sync_status(const std::string& sync_id) const;
    
private:
    // Internal helper functions
    std::vector<FileManager::FileInfo> calculate_differences(
        const std::string& local_path, 
        const std::string& remote_path);
    
    void process_file_transfer(const FileManager::FileInfo& file_info);
    void handle_conflict_resolution(const FileManager::FileInfo& file_info);
    
    // Synchronization state tracking
    struct SyncState {
        std::string sync_id;
        std::string local_path;
        std::string remote_path;
        uint64_t last_sync_time;
        bool is_active;
        FileManager::ConflictResolution conflict_policy;
        std::vector<FileManager::FileInfo> pending_files;
    };
    
    // Internal state
    std::vector<SyncState> active_syncs_;
    uint32_t sync_interval_;
    
    // Helper methods for file comparison and transfer
    std::vector<FileManager::FileInfo> get_remote_files(const std::string& remote_path);
    bool send_file_transfer_request(const FileManager::FileInfo& file_info);
    
    // Error handling and logging
    void log_sync_error(const std::string& error_msg);
    void log_sync_status(const std::string& status_msg);
};

} // namespace file
} // namespace netcopy