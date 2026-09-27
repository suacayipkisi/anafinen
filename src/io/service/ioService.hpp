// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Asynchronous front end over meshIo.hpp. One worker thread executes jobs in submission order,
// so the caller (GUI frame loop) never blocks. Each job returns an IoTask that can be polled
// every frame, cancelled, and queried for progress.

#include "../meshIo.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace anaf::IO {

  template <typename T>
  class IoTask {
  public:
    using Result = std::expected<T, IoError>;

    explicit IoTask(std::string description)
      : m_description(std::move(description)), m_future(m_promise.get_future().share()) {}

    bool ready() const { return m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    const Result& wait() const { return m_future.get(); }
    // Result once ready, nullptr before that. Never blocks.
    const Result* tryResult() const { return ready() ? &m_future.get() : nullptr; }

    float progress() const { return m_progress.load(std::memory_order_relaxed); }
    std::string stage() const {
      std::lock_guard lock(m_stageMutex);
      return m_stage;
    }
    const std::string& description() const { return m_description; }

    // Cooperative: the job stops at its next checkpoint (between stages / chunks).
    void cancel() { m_cancel.store(true, std::memory_order_relaxed); }
    bool cancelRequested() const { return m_cancel.load(std::memory_order_relaxed); }

  private:
    friend class IoService;

    IoContext context(std::stop_token serviceStop) {
      IoContext context;
      context.isCancelled = [this, serviceStop] { return cancelRequested() || serviceStop.stop_requested(); };
      context.onProgress = [this](const float fraction, const std::string_view stage) {
        m_progress.store(fraction, std::memory_order_relaxed);
        std::lock_guard lock(m_stageMutex);
        m_stage.assign(stage);
      };
      return context;
    }

    void finish(Result result) { m_promise.set_value(std::move(result)); }

    std::string m_description;
    std::promise<Result> m_promise;
    std::shared_future<Result> m_future;
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_cancel{false};
    mutable std::mutex m_stageMutex;
    std::string m_stage{"queued"};
  };

  class IoService {
  public:
    IoService();
    ~IoService(); // cancels queued jobs and joins the worker

    IoService(const IoService&) = delete;
    IoService& operator=(const IoService&) = delete;

    // Runs any job on the I/O thread, e.g. "read, then convert for a solver" so that no
    // post-processing step runs on the caller's (GUI) thread either.
    template <typename T>
    std::shared_ptr<IoTask<T>> runAsync(std::string description, std::function<std::expected<T, IoError>(const IoContext&)> job) {
      auto task = std::make_shared<IoTask<T>>(std::move(description));
      submit([task, job = std::move(job)](const std::stop_token stop) {
        const IoContext context = task->context(stop);
        if (context.cancelled()) {
          task->finish(std::unexpected(IoError{IoError::Code::Cancelled, "cancelled before start"}));
          return;
        }
        try {
          task->finish(job(context));
        } catch (const std::exception& error) {
          task->finish(std::unexpected(IoError{IoError::Code::BackendError, error.what()}));
        }
      });
      return task;
    }

    std::shared_ptr<IoTask<MeshModel>> importAsync(std::filesystem::path path, ReadOptions options = {});
    // The model is shared, not copied: callers publish an immutable snapshot.
    std::shared_ptr<IoTask<WriteReport>> exportAsync(std::filesystem::path path, std::shared_ptr<const MeshModel> model,
                                                     WriteOptions options = {});

    std::size_t queuedJobs() const;

  private:
    using Job = std::function<void(std::stop_token)>;
    void submit(Job job);
    void run(std::stop_token stop);

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wakeUp;
    std::deque<Job> m_jobs;
    std::jthread m_worker; // declared last: joins before the queue is destroyed
  };

} // namespace anaf::IO end
