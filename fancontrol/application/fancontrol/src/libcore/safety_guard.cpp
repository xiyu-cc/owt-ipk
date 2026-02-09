#include "libcore/safety_guard.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>

#include "libcore/demand_policy.hpp"
#include "libcore/json.hpp"

namespace fancontrol::core {

TargetDecision compute_target_decision(const BoardConfig &cfg,
                                       const SourceManager &mgr,
                                       const std::unordered_map<std::string, BoardSourceConfig> &by_id,
                                       std::unordered_map<std::string, bool> &active_state,
                                       std::vector<SourceTelemetry> &telemetry,
                                       bool debug) {
    const auto now = std::chrono::steady_clock::now();

    TargetDecision decision;
    decision.target_pwm = min_cooling_pwm(cfg);
    telemetry.clear();
    telemetry.reserve(mgr.runtimes().size());

    for (const auto &rt : mgr.runtimes()) {
        if (!rt || !rt->source) {
            continue;
        }

        SourceTelemetry item;
        item.id = rt->source->id();
        const auto cfg_it = by_id.find(item.id);
        if (cfg_it == by_id.end()) {
            item.error = "source id missing in config";
            telemetry.push_back(std::move(item));
            continue;
        }
        const auto &src = cfg_it->second;
        item.ttl_sec = src.ttl_sec;

        const SourceSnapshot snap = rt->source->snapshot();
        item.has_polled = snap.has_polled;

        if (snap.last_sample) {
            item.ok = snap.last_sample->ok;
            item.error = snap.last_sample->error;
            if (snap.last_sample->ok) {
                item.temp_mC = snap.last_sample->temp_mC;
            }
        } else if (item.has_polled) {
            item.error = "no sample";
        }

        bool source_timeout = false;
        if (snap.last_good_sample) {
            item.age_sec = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(now - snap.last_good_sample->sample_ts).count());
            item.stale = (item.age_sec > src.ttl_sec);
            source_timeout = item.stale;

            if (!snap.last_sample || !snap.last_sample->ok) {
                item.using_last_good = true;
                item.temp_mC = snap.last_good_sample->temp_mC;
            }
        } else if (snap.last_sample) {
            item.age_sec = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(now - snap.last_sample->sample_ts).count());
            source_timeout = item.has_polled && (item.age_sec > src.ttl_sec);
        } else {
            source_timeout = item.has_polled;
        }

        if (source_timeout) {
            decision.any_timeout = true;
            if (debug) {
                std::cerr << "source[" << item.id << "] timeout age=" << item.age_sec
                          << "s ttl=" << src.ttl_sec << "s\n";
            }
            telemetry.push_back(std::move(item));
            continue;
        }

        if (!snap.last_good_sample) {
            telemetry.push_back(std::move(item));
            continue;
        }

        decision.any_valid = true;
        bool &active = active_state[item.id];
        item.active = active;
        bool source_critical = false;
        item.demand_pwm = demand_from_source(cfg, src, snap.last_good_sample->temp_mC, active, source_critical);
        item.active = active;
        item.critical = source_critical;
        decision.critical = decision.critical || source_critical;
        decision.target_pwm = stronger_cooling_pwm(decision.target_pwm, item.demand_pwm, cfg);

        if (debug) {
            std::cerr << "source[" << item.id << "] temp=" << item.temp_mC
                      << " demand=" << item.demand_pwm << " active=" << (item.active ? 1 : 0)
                      << " using_last_good=" << (item.using_last_good ? 1 : 0) << "\n";
        }
        telemetry.push_back(std::move(item));
    }

    if (decision.critical) {
        decision.target_pwm = max_cooling_pwm(cfg);
    }
    if (!decision.any_valid) {
        decision.target_pwm = max_cooling_pwm(cfg);
    }
    if (decision.any_timeout) {
        decision.target_pwm = stronger_cooling_pwm(decision.target_pwm, clamp_pwm(cfg, cfg.failsafe_pwm), cfg);
    }

    decision.target_pwm = clamp_pwm(cfg, decision.target_pwm);
    return decision;
}

std::string build_runtime_status_json(const BoardConfig &cfg,
                                      const TargetDecision &decision,
                                      int current_pwm,
                                      int target_pwm,
                                      int applied_pwm,
                                      const std::vector<SourceTelemetry> &telemetry) {
    const std::time_t now = std::time(nullptr);
    nlohmann::json root = {
        {"ok", 1},
        {"timestamp", static_cast<long long>(now)},
        {"policy", cfg.policy},
        {"pwm",
         {
             {"current", current_pwm},
             {"target", target_pwm},
             {"applied", applied_pwm},
         }},
        {"safety",
         {
             {"any_valid", decision.any_valid ? 1 : 0},
             {"any_timeout", decision.any_timeout ? 1 : 0},
             {"critical", decision.critical ? 1 : 0},
         }},
        {"sources", nlohmann::json::array()},
    };

    for (const auto &s : telemetry) {
        root["sources"].push_back({
            {"id", s.id},
            {"has_polled", s.has_polled ? 1 : 0},
            {"ok", s.ok ? 1 : 0},
            {"stale", s.stale ? 1 : 0},
            {"using_last_good", s.using_last_good ? 1 : 0},
            {"active", s.active ? 1 : 0},
            {"critical", s.critical ? 1 : 0},
            {"temp_mC", s.temp_mC},
            {"age_s", s.age_sec},
            {"ttl_s", s.ttl_sec},
            {"demand_pwm", s.demand_pwm},
            {"error", s.error},
        });
    }

    return root.dump();
}

bool write_runtime_status_file(const std::string &path, const std::string &payload) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) {
            return false;
        }
        out << payload << '\n';
        if (!out.good()) {
            return false;
        }
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

} // namespace fancontrol::core
