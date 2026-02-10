#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fancontrol::core {

struct TempSample {
    bool ok = false;
    int temp_mC = 0;
    std::chrono::steady_clock::time_point sample_ts;
    std::string error;
};

struct SourceSnapshot {
    bool has_polled = false;
    std::optional<TempSample> last_sample;
    std::optional<TempSample> last_good_sample;
};

class ITempSource {
public:
    virtual ~ITempSource() = default;
    virtual const std::string &id() const = 0;
    virtual std::chrono::seconds poll_interval() const = 0;
    virtual void sample() = 0;
    virtual void publish_failure(const std::string &error) = 0;
    virtual SourceSnapshot snapshot() const = 0;
};

class SysfsTempSource : public ITempSource {
public:
    SysfsTempSource(std::string id, std::string path, std::chrono::seconds poll_interval);

    const std::string &id() const override;
    std::chrono::seconds poll_interval() const override;
    void sample() override;
    void publish_failure(const std::string &error) override;
    SourceSnapshot snapshot() const override;

private:
    void store_sample(TempSample sample);

    std::string id_;
    std::string path_;
    std::chrono::seconds poll_interval_;
    mutable std::mutex sample_mutex_;
    std::optional<TempSample> last_sample_;
    std::optional<TempSample> last_good_sample_;
    bool has_polled_ = false;
};

class UbusTempSource : public ITempSource {
public:
    UbusTempSource(std::string id,
                   std::string object,
                   std::string method,
                   std::string key,
                   std::string args_json,
                   std::chrono::seconds poll_interval);

    const std::string &id() const override;
    std::chrono::seconds poll_interval() const override;
    void sample() override;
    void publish_failure(const std::string &error) override;
    SourceSnapshot snapshot() const override;

private:
    void store_sample(TempSample sample);

    std::string id_;
    std::string object_;
    std::string method_;
    std::string key_;
    std::string args_json_;
    std::chrono::seconds poll_interval_;
    int ubus_timeout_ms_ = 2000;
    mutable std::mutex sample_mutex_;
    std::optional<TempSample> last_sample_;
    std::optional<TempSample> last_good_sample_;
    bool has_polled_ = false;
};

struct SourceRuntime {
    std::unique_ptr<ITempSource> source;
    std::thread worker;
};

class SourceManager {
public:
    ~SourceManager();
    void add(std::unique_ptr<ITempSource> source);
    void start();
    void stop();
    const std::vector<std::shared_ptr<SourceRuntime>> &runtimes() const;

private:
    void run_source_loop(SourceRuntime &rt);

    std::vector<std::shared_ptr<SourceRuntime>> runtimes_;
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    bool running_ = false;
};

} // namespace fancontrol::core
