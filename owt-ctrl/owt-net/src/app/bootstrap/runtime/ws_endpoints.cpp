#include "app/bootstrap/runtime/ws_endpoints.h"

#include "app/bootstrap/runtime/runtime_composition.h"

#include <drogon/WebSocketController.h>

#include <mutex>

namespace app::bootstrap::runtime {

namespace {

class RuntimeRouter {
public:
  static RuntimeRouter& instance() {
    static RuntimeRouter s;
    return s;
  }

  void bind(RuntimeComposition* state) {
    std::lock_guard<std::mutex> lk(mutex_);
    state_ = state;
  }

  RuntimeComposition* get() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
  }

private:
  mutable std::mutex mutex_;
  RuntimeComposition* state_ = nullptr;
};

class UiWsController final : public drogon::WebSocketController<UiWsController> {
public:
  void handleNewMessage(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_ui_message(conn, std::move(message), type);
  }

  void handleNewConnection(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_ui_open(req, conn);
  }

  void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      return;
    }
    state->on_ui_close(conn);
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/v5/ui");
  WS_PATH_LIST_END
};

class AgentWsController final : public drogon::WebSocketController<AgentWsController> {
public:
  void handleNewMessage(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_agent_message(conn, std::move(message), type);
  }

  void handleNewConnection(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "runtime unavailable");
      return;
    }
    state->on_agent_open(req, conn);
  }

  void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
    auto* state = RuntimeRouter::instance().get();
    if (state == nullptr) {
      return;
    }
    state->on_agent_close(conn);
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/v5/agent");
  WS_PATH_LIST_END
};

} // namespace

void bind_runtime_composition(RuntimeComposition* runtime) {
  RuntimeRouter::instance().bind(runtime);
}

RuntimeComposition* current_runtime_composition() {
  return RuntimeRouter::instance().get();
}

} // namespace app::bootstrap::runtime
