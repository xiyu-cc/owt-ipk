#include "libcore/temp_source.hpp"

#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "libcore/json.hpp"

extern "C" {
#include <libubox/blobmsg.h>
#include <libubus.h>
}

namespace fancontrol::core {
namespace {
constexpr int kMaxUbusArgsDepth = 32;

std::optional<int> read_int_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    long long value = 0;
    in >> value;
    if (in.fail()) {
        return std::nullopt;
    }

    if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

std::optional<int> parse_int_text(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t idx = 0;
        const long long v = std::stoll(std::string(text), &idx, 10);
        if (idx == 0 || idx != text.size()) {
            return std::nullopt;
        }
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(v);
    } catch (...) {
        return std::nullopt;
    }
}

std::string to_lower_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool text_contains_ascii_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const std::string hay = to_lower_ascii(haystack);
    const std::string ned = to_lower_ascii(needle);
    return hay.find(ned) != std::string::npos;
}

bool key_prefers_celsius(std::string_view key) {
    const std::string lower = to_lower_ascii(key);
    return lower.find("temp") != std::string::npos && lower.find("mc") == std::string::npos;
}

std::optional<std::string_view> extract_first_number_token(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        const bool is_sign = (c == '+' || c == '-');
        const bool starts_num = std::isdigit(static_cast<unsigned char>(c)) ||
                                (is_sign && i + 1 < text.size() &&
                                 std::isdigit(static_cast<unsigned char>(text[i + 1])));
        if (!starts_num) {
            ++i;
            continue;
        }

        const std::size_t start = i;
        if (is_sign) {
            ++i;
        }
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
        }
        return text.substr(start, i - start);
    }

    return std::nullopt;
}

std::optional<int> parse_temperature_text_to_mc(std::string_view text, bool plain_number_is_celsius) {
    const auto token = extract_first_number_token(text);
    if (!token) {
        return std::nullopt;
    }

    double raw = 0.0;
    try {
        std::size_t idx = 0;
        raw = std::stod(std::string(*token), &idx);
        if (idx != token->size()) {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }

    const bool has_milli_unit = text_contains_ascii_ci(text, "mc") || text_contains_ascii_ci(text, "millic");
    const bool has_celsius_unit = !has_milli_unit &&
                                  (text_contains_ascii_ci(text, "c") || text_contains_ascii_ci(text, "deg"));
    const double scale = (has_milli_unit || (!has_celsius_unit && !plain_number_is_celsius)) ? 1.0 : 1000.0;
    const double scaled = raw * scale;

    if (scaled < static_cast<double>(std::numeric_limits<int>::min()) ||
        scaled > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    return static_cast<int>(scaled >= 0.0 ? (scaled + 0.5) : (scaled - 0.5));
}

std::optional<int> blob_attr_to_int(struct blob_attr *attr) {
    if (!attr) {
        return std::nullopt;
    }
    const int type = blobmsg_type(attr);
    switch (type) {
    case BLOBMSG_TYPE_INT8:
        return static_cast<int>(blobmsg_get_u8(attr));
    case BLOBMSG_TYPE_INT16:
        return static_cast<int>(blobmsg_get_u16(attr));
    case BLOBMSG_TYPE_INT32:
        return static_cast<int>(blobmsg_get_u32(attr));
    case BLOBMSG_TYPE_INT64: {
        const std::uint64_t raw = blobmsg_get_u64(attr);
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(raw);
    }
    case BLOBMSG_TYPE_STRING:
        return parse_int_text(blobmsg_get_string(attr));
    default:
        return std::nullopt;
    }
}

std::optional<int> blob_attr_to_temp_mc(struct blob_attr *attr, bool plain_number_is_celsius) {
    if (!attr) {
        return std::nullopt;
    }

    if (blobmsg_type(attr) == BLOBMSG_TYPE_STRING) {
        return parse_temperature_text_to_mc(blobmsg_get_string(attr), plain_number_is_celsius);
    }

    const auto v = blob_attr_to_int(attr);
    if (!v) {
        return std::nullopt;
    }
    if (!plain_number_is_celsius) {
        return v;
    }

    const long long scaled = static_cast<long long>(*v) * 1000LL;
    if (scaled < static_cast<long long>(std::numeric_limits<int>::min()) ||
        scaled > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(scaled);
}

struct UbusInvokeResult {
    std::string key;
    bool has_reply = false;
    std::optional<int> value;
    std::string error;
};

class UbusContextGuard {
public:
    UbusContextGuard() : ctx_(ubus_connect(nullptr)) {}

    ~UbusContextGuard() {
        if (ctx_) {
            ubus_free(ctx_);
        }
    }

    UbusContextGuard(const UbusContextGuard &) = delete;
    UbusContextGuard &operator=(const UbusContextGuard &) = delete;

    bool valid() const {
        return ctx_ != nullptr;
    }

    struct ubus_context &context() const {
        return *ctx_;
    }

private:
    struct ubus_context *ctx_ = nullptr;
};

class BlobBufGuard {
public:
    BlobBufGuard() {
        blob_buf_init(&buf_, 0);
    }

    ~BlobBufGuard() {
        blob_buf_free(&buf_);
    }

    BlobBufGuard(const BlobBufGuard &) = delete;
    BlobBufGuard &operator=(const BlobBufGuard &) = delete;

    struct blob_buf &get() {
        return buf_;
    }

    struct blob_attr *head() const {
        return buf_.head;
    }

private:
    struct blob_buf buf_ {};
};

bool add_json_value_to_blobmsg(struct blob_buf &buf,
                               std::optional<std::string_view> name,
                               const nlohmann::json &value,
                               std::string &error,
                               int depth);

bool add_json_object_to_blobmsg(struct blob_buf &buf,
                                const nlohmann::json &value,
                                std::string &error,
                                int depth) {
    if (depth > kMaxUbusArgsDepth) {
        error = "ubus args json nesting is too deep";
        return false;
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!add_json_value_to_blobmsg(buf, std::string_view(it.key()), it.value(), error, depth + 1)) {
            return false;
        }
    }
    return true;
}

bool add_json_value_to_blobmsg(struct blob_buf &buf,
                               std::optional<std::string_view> name,
                               const nlohmann::json &value,
                               std::string &error,
                               int depth) {
    if (depth > kMaxUbusArgsDepth) {
        error = "ubus args json nesting is too deep";
        return false;
    }

    if (value.is_object()) {
        void *cookie = blobmsg_open_table(&buf, name ? name->data() : nullptr);
        if (!cookie) {
            error = "failed to create ubus table field";
            return false;
        }
        if (!add_json_object_to_blobmsg(buf, value, error, depth + 1)) {
            blobmsg_close_table(&buf, cookie);
            return false;
        }
        blobmsg_close_table(&buf, cookie);
        return true;
    }

    if (value.is_array()) {
        void *cookie = blobmsg_open_array(&buf, name ? name->data() : nullptr);
        if (!cookie) {
            error = "failed to create ubus array field";
            return false;
        }
        for (const auto &elem : value) {
            if (!add_json_value_to_blobmsg(buf, std::nullopt, elem, error, depth + 1)) {
                blobmsg_close_array(&buf, cookie);
                return false;
            }
        }
        blobmsg_close_array(&buf, cookie);
        return true;
    }

    if (value.is_boolean()) {
        if (blobmsg_add_u8(&buf, name ? name->data() : nullptr, value.get<bool>() ? 1 : 0) != 0) {
            error = "failed to add ubus boolean field";
            return false;
        }
        return true;
    }

    if (value.is_number_unsigned()) {
        if (blobmsg_add_u64(&buf, name ? name->data() : nullptr, value.get<std::uint64_t>()) != 0) {
            error = "failed to add ubus unsigned integer field";
            return false;
        }
        return true;
    }

    if (value.is_number_integer()) {
        const long long v = value.get<long long>();
        if (v < 0) {
            error = "ubus args json does not support negative integer values";
            return false;
        }
        if (blobmsg_add_u64(&buf, name ? name->data() : nullptr, static_cast<std::uint64_t>(v)) != 0) {
            error = "failed to add ubus integer field";
            return false;
        }
        return true;
    }

    if (value.is_number_float()) {
        if (blobmsg_add_double(&buf, name ? name->data() : nullptr, value.get<double>()) != 0) {
            error = "failed to add ubus floating-point field";
            return false;
        }
        return true;
    }

    if (value.is_string()) {
        if (blobmsg_add_string(&buf, name ? name->data() : nullptr, value.get_ref<const std::string &>().c_str()) !=
            0) {
            error = "failed to add ubus string field";
            return false;
        }
        return true;
    }

    if (value.is_null()) {
        error = "ubus args json does not support null values";
        return false;
    }

    error = "ubus args json contains unsupported value type";
    return false;
}

bool parse_and_add_ubus_args(struct blob_buf &buf, const std::string &args_json, std::string &error) {
    try {
        const nlohmann::json parsed = nlohmann::json::parse(args_json);
        if (!parsed.is_object()) {
            error = "ubus args json must be an object";
            return false;
        }
        return add_json_object_to_blobmsg(buf, parsed, error, 0);
    } catch (const std::exception &e) {
        error = std::string("invalid ubus args json: ") + e.what();
        return false;
    }
}

void ubus_invoke_temp_callback(struct ubus_request *req, int /* type */, struct blob_attr *msg) {
    if (!req) {
        return;
    }
    if (!req->priv) {
        return;
    }
    auto &result = *static_cast<UbusInvokeResult *>(req->priv);

    result.has_reply = true;
    if (!msg) {
        result.error = "empty ubus reply";
        return;
    }

    struct blobmsg_policy policy[3] {};
    policy[0].name = result.key.c_str();
    policy[0].type = BLOBMSG_TYPE_UNSPEC;
    policy[1].name = "error";
    policy[1].type = BLOBMSG_TYPE_TABLE;
    policy[2].name = "temperature";
    policy[2].type = BLOBMSG_TYPE_UNSPEC;

    struct blob_attr *tb[3] {};
    blobmsg_parse(policy, 3, tb, blob_data(msg), blob_len(msg));

    if (tb[0]) {
        result.value = blob_attr_to_temp_mc(tb[0], key_prefers_celsius(result.key));
        if (!result.value) {
            result.error = "ubus key is not a temperature-compatible numeric value: " + result.key;
        }
        return;
    }

    if (result.key == "temp_mC" && tb[2]) {
        result.value = blob_attr_to_temp_mc(tb[2], true);
        if (!result.value) {
            result.error = "ubus fallback key is not a temperature-compatible numeric value: temperature";
        }
        return;
    }

    if (tb[1]) {
        struct blobmsg_policy err_policy {};
        err_policy.name = "message";
        err_policy.type = BLOBMSG_TYPE_STRING;
        struct blob_attr *err_tb[1] {};
        blobmsg_parse(&err_policy, 1, err_tb, blobmsg_data(tb[1]), blobmsg_data_len(tb[1]));
        if (err_tb[0]) {
            result.error = "ubus error: " + std::string(blobmsg_get_string(err_tb[0]));
            return;
        }
    }

    result.error = "ubus key not found: " + result.key;
}

} // namespace

SysfsTempSource::SysfsTempSource(std::string id, std::string path, std::chrono::seconds poll_interval)
    : id_(std::move(id)), path_(std::move(path)), poll_interval_(poll_interval) {
    if (poll_interval_ < std::chrono::seconds(1)) {
        poll_interval_ = std::chrono::seconds(1);
    }
}

const std::string &SysfsTempSource::id() const {
    return id_;
}

std::chrono::seconds SysfsTempSource::poll_interval() const {
    return poll_interval_;
}

void SysfsTempSource::store_sample(TempSample sample) {
    std::lock_guard<std::mutex> guard(sample_mutex_);
    last_sample_ = std::move(sample);
    if (last_sample_ && last_sample_->ok) {
        last_good_sample_ = last_sample_;
    }
    has_polled_ = true;
}

void SysfsTempSource::publish_failure(const std::string &error) {
    TempSample sample;
    sample.ok = false;
    sample.sample_ts = std::chrono::steady_clock::now();
    sample.error = error;
    store_sample(std::move(sample));
}

void SysfsTempSource::sample() {
    TempSample s;
    s.sample_ts = std::chrono::steady_clock::now();

    const auto v = read_int_file(path_);
    if (!v) {
        s.ok = false;
        s.error = "cannot read " + path_;
        store_sample(std::move(s));
        return;
    }

    s.ok = true;
    s.temp_mC = *v;
    store_sample(std::move(s));
}

SourceSnapshot SysfsTempSource::snapshot() const {
    std::lock_guard<std::mutex> guard(sample_mutex_);
    return SourceSnapshot{has_polled_, last_sample_, last_good_sample_};
}

UbusTempSource::UbusTempSource(std::string id,
                               std::string object,
                               std::string method,
                               std::string key,
                               std::string args_json,
                               std::chrono::seconds poll_interval)
    : id_(std::move(id)),
      object_(std::move(object)),
      method_(std::move(method)),
      key_(std::move(key)),
      args_json_(std::move(args_json)),
      poll_interval_(poll_interval) {
    if (poll_interval_ < std::chrono::seconds(1)) {
        poll_interval_ = std::chrono::seconds(1);
    }
    if (args_json_.empty()) {
        args_json_ = "{}";
    }
    ubus_timeout_ms_ = static_cast<int>(poll_interval_.count()) * 1000;
    if (ubus_timeout_ms_ < 1000) {
        ubus_timeout_ms_ = 1000;
    }
    if (ubus_timeout_ms_ > 10000) {
        ubus_timeout_ms_ = 10000;
    }
}

const std::string &UbusTempSource::id() const {
    return id_;
}

std::chrono::seconds UbusTempSource::poll_interval() const {
    return poll_interval_;
}

void UbusTempSource::store_sample(TempSample sample) {
    std::lock_guard<std::mutex> guard(sample_mutex_);
    last_sample_ = std::move(sample);
    if (last_sample_ && last_sample_->ok) {
        last_good_sample_ = last_sample_;
    }
    has_polled_ = true;
}

void UbusTempSource::publish_failure(const std::string &error) {
    TempSample sample;
    sample.ok = false;
    sample.sample_ts = std::chrono::steady_clock::now();
    sample.error = error;
    store_sample(std::move(sample));
}

void UbusTempSource::sample() {
    TempSample s;
    s.sample_ts = std::chrono::steady_clock::now();

    UbusContextGuard ctx;
    if (!ctx.valid()) {
        s.ok = false;
        s.error = "ubus connect failed";
        store_sample(std::move(s));
        return;
    }

    uint32_t object_id = 0;
    int rc = ubus_lookup_id(&ctx.context(), object_.c_str(), &object_id);
    if (rc != UBUS_STATUS_OK) {
        s.ok = false;
        s.error = "ubus object lookup failed for " + object_ + ": " + ubus_strerror(rc);
        store_sample(std::move(s));
        return;
    }

    BlobBufGuard request;
    if (!args_json_.empty() && args_json_ != "{}") {
        std::string args_error;
        if (!parse_and_add_ubus_args(request.get(), args_json_, args_error)) {
            s.ok = false;
            s.error = args_error;
            store_sample(std::move(s));
            return;
        }
    }

    UbusInvokeResult result;
    result.key = key_;
    rc = ubus_invoke(&ctx.context(),
                     object_id,
                     method_.c_str(),
                     request.head(),
                     ubus_invoke_temp_callback,
                     &result,
                     ubus_timeout_ms_);

    if (rc != UBUS_STATUS_OK) {
        s.ok = false;
        s.error = "ubus call failed for " + object_ + "." + method_ + ": " + ubus_strerror(rc);
        store_sample(std::move(s));
        return;
    }

    if (!result.has_reply || !result.value) {
        s.ok = false;
        s.error = result.error.empty() ? ("ubus key not found or invalid: " + key_) : result.error;
        store_sample(std::move(s));
        return;
    }

    s.ok = true;
    s.temp_mC = *result.value;
    store_sample(std::move(s));
}

SourceSnapshot UbusTempSource::snapshot() const {
    std::lock_guard<std::mutex> guard(sample_mutex_);
    return SourceSnapshot{has_polled_, last_sample_, last_good_sample_};
}

SourceManager::~SourceManager() {
    stop();
}

void SourceManager::add(std::unique_ptr<ITempSource> source) {
    auto rt = std::make_shared<SourceRuntime>();
    rt->source = std::move(source);
    runtimes_.push_back(std::move(rt));
}

void SourceManager::start(bool debug) {
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        if (running_) {
            return;
        }
        running_ = true;
    }

    try {
        for (auto &rt : runtimes_) {
            if (!rt || !rt->source) {
                continue;
            }
            rt->worker = std::thread([this, rt, debug]() {
                run_source_loop(*rt, debug);
            });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> guard(state_mutex_);
            running_ = false;
        }
        state_cv_.notify_all();
        for (auto &rt : runtimes_) {
            if (rt && rt->worker.joinable()) {
                rt->worker.join();
            }
        }
        throw;
    }
}

void SourceManager::stop() {
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    state_cv_.notify_all();

    for (auto &rt : runtimes_) {
        if (rt && rt->worker.joinable()) {
            rt->worker.join();
        }
    }
}

void SourceManager::run_source_loop(SourceRuntime &rt, bool debug) {
    if (!rt.source) {
        return;
    }
    const auto interval = rt.source->poll_interval();
    auto next_deadline = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> guard(state_mutex_);
            if (!running_) {
                break;
            }
        }

        try {
            rt.source->sample();
        } catch (const std::exception &e) {
            rt.source->publish_failure(std::string("sampling exception: ") + e.what());
        } catch (...) {
            rt.source->publish_failure("sampling exception: unknown");
        }

        if (debug) {
            const SourceSnapshot snap = rt.source->snapshot();
            if (snap.last_sample) {
                if (snap.last_sample->ok) {
                    std::cerr << "source[" << rt.source->id() << "]=" << snap.last_sample->temp_mC << "mC\n";
                } else {
                    std::cerr << "source[" << rt.source->id() << "] error: " << snap.last_sample->error << "\n";
                }
            }
        }

        next_deadline += interval;
        const auto now = std::chrono::steady_clock::now();
        if (next_deadline <= now) {
            const auto lag = now - next_deadline;
            const auto missed = (lag / interval) + 1;
            next_deadline += interval * missed;
        }

        std::unique_lock<std::mutex> lock(state_mutex_);
        if (!running_) {
            break;
        }
        state_cv_.wait_until(lock, next_deadline, [this]() {
            return !running_;
        });
        if (!running_) {
            break;
        }
    }
}

const std::vector<std::shared_ptr<SourceRuntime>> &SourceManager::runtimes() const {
    return runtimes_;
}

} // namespace fancontrol::core
