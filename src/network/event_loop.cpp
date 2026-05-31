#include "network/event_loop.h"
#include "logging/logger.h"

namespace netcopy {
namespace network {

EventLoop::EventLoop(size_t num_threads)
    : num_threads_(num_threads), running_(false) {
    if (num_threads_ == 0) {
        num_threads_ = std::thread::hardware_concurrency();
        if (num_threads_ == 0) num_threads_ = 2; // Fallback
    }
}

EventLoop::~EventLoop() {
    stop();
}

void EventLoop::start() {
    if (running_) return;

    // Prevent io_context::run() from returning immediately when there's no work
    work_ = std::make_unique<asio::io_context::work>(io_context_);
    
    running_ = true;
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back([this]() {
            try {
                io_context_.run();
            } catch (const std::exception& e) {
                LOG_ERROR("EventLoop thread exception: " + std::string(e.what()));
            }
        });
    }
    LOG_DEBUG("EventLoop started with " + std::to_string(num_threads_) + " worker threads.");
}

void EventLoop::stop() {
    if (!running_) return;
    
    running_ = false;
    work_.reset(); // Allow io_context to run out of work
    io_context_.stop();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    LOG_DEBUG("EventLoop stopped.");
}

void EventLoop::post(std::function<void()> task) {
    asio::post(io_context_, std::move(task));
}

} // namespace network
} // namespace netcopy
