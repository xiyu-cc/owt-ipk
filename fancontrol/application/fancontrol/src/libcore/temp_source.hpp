#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fancontrol::core {

struct TempSample {
    bool ok = false;
    int temp_mC = 0;
    std::chrono::steady_clock::time_point sample_ts;
    std::string error;
};

class ITempSource {
public:
    virtual ~ITempSource() = default;
    virtual const std::string &id() const = 0;
    virtual std::chrono::seconds poll_interval() const = 0;
    virtual TempSample sample() = 0;
};

class SysfsTempSource : public ITempSource {
public:
    SysfsTempSource(std::string id, std::string path, std::chrono::seconds poll_interval);

    const std::string &id() const override;
    std::chrono::seconds poll_interval() const override;
    TempSample sample() override;

private:
    std::string id_;
    std::string path_;
    std::chrono::seconds poll_interval_;
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
    TempSample sample() override;

private:
    std::string id_;
    std::string object_;
    std::string method_;
    std::string key_;
    std::string args_json_;
    std::chrono::seconds poll_interval_;
};

struct SourceRuntime {
    std::unique_ptr<ITempSource> source;
    std::optional<TempSample> last_sample;
    std::chrono::steady_clock::time_point last_poll;
    bool has_polled = false;
};

class SourceManager {
public:
    void add(std::unique_ptr<ITempSource> source);
    void poll(bool debug);
    const std::vector<SourceRuntime> &runtimes() const;

private:
    std::vector<SourceRuntime> runtimes_;
};

} // namespace fancontrol::core
