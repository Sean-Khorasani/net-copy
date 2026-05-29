#pragma once

#include <asio.hpp>
#include <memory>
#include <thread>
#include <vector>
#include <functional>

namespace netcopy {
namespace network {

class EventLoop {
public:
    // Create an event loop with a specified number of worker threads.
    // If num_threads is 0, it defaults to the number of hardware concurrency.
    explicit EventLoop(size_t num_threads = 0);
    ~EventLoop();

    // Start the event loop processing (spawns worker threads)
    void start();

    // Stop the event loop and wait for all workers to finish
    void stop();

    // Post a task to be executed by the event loop
    void post(std::function<void()> task);

    // Get the underlying asio::io_context
    asio::io_context& get_io_context() { return io_context_; }

private:
    asio::io_context io_context_;
    std::unique_ptr<asio::io_context::work> work_;
    std::vector<std::thread> workers_;
    size_t num_threads_;
    bool running_;
};

} // namespace network
} // namespace netcopy
