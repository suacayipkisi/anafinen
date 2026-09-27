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

#include "ioService.hpp"

namespace anaf::IO {

  IoService::IoService() : m_worker([this](const std::stop_token stop) { run(stop); }) {}

  IoService::~IoService() {
    m_worker.request_stop();
    m_wakeUp.notify_all();
    // std::jthread joins in its destructor; queued jobs are drained as cancelled by run().
  }

  std::size_t IoService::queuedJobs() const {
    std::lock_guard lock(m_mutex);
    return m_jobs.size();
  }

  void IoService::submit(Job job) {
    {
      std::lock_guard lock(m_mutex);
      m_jobs.push_back(std::move(job));
    }
    m_wakeUp.notify_one();
  }

  void IoService::run(const std::stop_token stop) {
    while (true) {
      Job job;
      {
        std::unique_lock lock(m_mutex);
        m_wakeUp.wait(lock, stop, [this] { return !m_jobs.empty(); });
        if (m_jobs.empty()) return; // stop requested and nothing left to drain
        job = std::move(m_jobs.front());
        m_jobs.pop_front();
      }
      // After a stop request the remaining jobs still run, but see a cancelled context and
      // finish immediately, so every IoTask gets a result.
      job(stop);
    }
  }

  std::shared_ptr<IoTask<MeshModel>> IoService::importAsync(std::filesystem::path path, ReadOptions options) {
    const std::string description = "Import " + path.filename().string();
    return runAsync<MeshModel>(description, [path = std::move(path), options](const IoContext& context) {
      return readMesh(path, options, context);
    });
  }

  std::shared_ptr<IoTask<WriteReport>> IoService::exportAsync(std::filesystem::path path, std::shared_ptr<const MeshModel> model,
                                                              WriteOptions options) {
    const std::string description = "Export " + path.filename().string();
    return runAsync<WriteReport>(description, [path = std::move(path), model = std::move(model), options](const IoContext& context) {
      return writeMesh(path, *model, options, context);
    });
  }

} // namespace anaf::IO end
