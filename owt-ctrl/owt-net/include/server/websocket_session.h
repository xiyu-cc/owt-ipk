#pragma once

#include "control/control_json_codec.h"
#include "log.h"
#include "service/command_store.h"
#include "service/control_hub.h"

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <deque>
#include <string>
#include <utility>

namespace server {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class websocket_session : public std::enable_shared_from_this<websocket_session> {
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  std::deque<std::string> write_queue_;
  std::string agent_id_;

public:
  explicit websocket_session(tcp::socket&& socket) : ws_(std::move(socket)) {}

  template <class Body, class Allocator>
  void do_accept(http::request<Body, http::basic_fields<Allocator>> req) {
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
      res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " owt-net");
    }));

    ws_.async_accept(
        req, beast::bind_front_handler(&websocket_session::on_accept, shared_from_this()));
  }

  void send_text(std::string text) {
    auto self = shared_from_this();
    net::post(ws_.get_executor(), [self, text = std::move(text)]() mutable {
      const bool idle = self->write_queue_.empty();
      self->write_queue_.push_back(std::move(text));
      if (idle) {
        self->do_write();
      }
    });
  }

  void send_control_message(const control::envelope& message) {
    send_text(control::encode_envelope_json(message));
  }

private:
  void on_accept(beast::error_code ec) {
    if (ec) {
      log::warn("websocket accept failed: {}", ec.message());
      return;
    }
    do_read();
  }

  void do_read() {
    ws_.async_read(
        buffer_, beast::bind_front_handler(&websocket_session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == websocket::error::closed) {
      cleanup_on_close();
      return;
    }
    if (ec) {
      log::warn("websocket read failed: {}", ec.message());
      cleanup_on_close();
      return;
    }

    const auto text = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    handle_text_message(text);
    do_read();
  }

  void do_write() {
    if (write_queue_.empty()) {
      return;
    }
    ws_.text(true);
    ws_.async_write(
        net::buffer(write_queue_.front()),
        beast::bind_front_handler(&websocket_session::on_write, shared_from_this()));
  }

  void on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
      log::warn("websocket write failed: {}", ec.message());
      cleanup_on_close();
      return;
    }

    if (!write_queue_.empty()) {
      write_queue_.pop_front();
    }
    if (!write_queue_.empty()) {
      do_write();
    }
  }

  void handle_text_message(const std::string& text) {
    control::envelope message;
    std::string error;
    if (!control::decode_envelope_json(text, message, error)) {
      log::warn("websocket decode message failed: {}", error);
      return;
    }

    switch (message.type) {
      case control::message_type::register_agent: {
        const auto* payload = std::get_if<control::register_payload>(&message.payload);
        if (payload == nullptr || payload->agent_id.empty()) {
          log::warn("websocket register payload missing agent_id");
          return;
        }
        if (!agent_id_.empty() && agent_id_ != payload->agent_id) {
          service::unregister_control_session(agent_id_, this);
        }
        agent_id_ = payload->agent_id;
        service::register_control_session(agent_id_, shared_from_this());

        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::register_ack;
        ack.protocol_version = message.protocol_version;
        ack.channel = control::channel_type::wss;
        ack.sent_at_ms = control::unix_time_ms_now();
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id_;
        ack.payload = control::register_ack_payload{true, "registered"};
        send_control_message(ack);
        break;
      }

      case control::message_type::heartbeat: {
        if (!message.agent_id.empty() && message.agent_id != agent_id_) {
          agent_id_ = message.agent_id;
          service::register_control_session(agent_id_, shared_from_this());
        }
        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::heartbeat_ack;
        ack.protocol_version = message.protocol_version;
        ack.channel = control::channel_type::wss;
        ack.sent_at_ms = control::unix_time_ms_now();
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id_;
        ack.payload = control::heartbeat_ack_payload{ack.sent_at_ms};
        send_control_message(ack);
        break;
      }

      case control::message_type::command_ack: {
        const auto* payload = std::get_if<control::command_ack_payload>(&message.payload);
        if (payload == nullptr || payload->command_id.empty()) {
          return;
        }
        std::string db_error;
        const auto now = control::unix_time_ms_now();
        bool should_update_ack = true;
        service::command_record existing;
        if (service::get_command(payload->command_id, existing, db_error) &&
            is_terminal_command_status(existing.status)) {
          should_update_ack = false;
        }
        if (should_update_ack &&
            !service::update_command_status(
                payload->command_id,
                control::to_string(payload->status),
                "wss",
                "",
                now,
                db_error)) {
          log::warn(
              "persist ack status failed: command_id={}, err={}", payload->command_id, db_error);
        }
        nlohmann::json detail = {
            {"event", "COMMAND_ACK"},
            {"agent_id", !agent_id_.empty() ? agent_id_ : message.agent_id},
            {"message", payload->message},
        };
        db_error.clear();
        if (!service::append_command_event(
                payload->command_id,
                "COMMAND_ACK_RECEIVED",
                control::to_string(payload->status),
                "wss",
                detail.dump(),
                now,
                db_error)) {
          log::warn("persist ack event failed: command_id={}, err={}", payload->command_id, db_error);
        }
        break;
      }

      case control::message_type::command_result: {
        const auto* payload = std::get_if<control::command_result_payload>(&message.payload);
        if (payload == nullptr || payload->command_id.empty()) {
          return;
        }
        std::string db_error;
        const auto now = control::unix_time_ms_now();
        if (!service::update_command_status(
                payload->command_id,
                control::to_string(payload->final_status),
                "wss",
                payload->result_json,
                now,
                db_error)) {
          log::warn(
              "persist result status failed: command_id={}, err={}", payload->command_id, db_error);
        }
        nlohmann::json detail = {
            {"event", "COMMAND_RESULT"},
            {"agent_id", !agent_id_.empty() ? agent_id_ : message.agent_id},
            {"exit_code", payload->exit_code},
        };
        db_error.clear();
        if (!service::append_command_event(
                payload->command_id,
                "COMMAND_RESULT_RECEIVED",
                control::to_string(payload->final_status),
                "wss",
                detail.dump(),
                now,
                db_error)) {
          log::warn(
              "persist result event failed: command_id={}, err={}", payload->command_id, db_error);
        }
        break;
      }

      default:
        break;
    }
  }

  void cleanup_on_close() {
    if (!agent_id_.empty()) {
      service::unregister_control_session(agent_id_, this);
      agent_id_.clear();
    }
  }

  static bool is_terminal_command_status(const std::string& status) {
    return status == "SUCCEEDED" || status == "FAILED" || status == "TIMED_OUT" ||
           status == "CANCELLED";
  }
};

} // namespace server
