#include "service/ssh_executor.h"

#include <mutex>

#include <libssh2.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace {

std::string session_last_error(LIBSSH2_SESSION* session) {
  char* message = nullptr;
  int message_len = 0;
  libssh2_session_last_error(session, &message, &message_len, 0);
  if (message == nullptr || message_len <= 0) {
    return "unknown libssh2 error";
  }
  return std::string(message, static_cast<size_t>(message_len));
}

int connect_tcp(const std::string& host, int port, std::string& err) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  addrinfo* res = nullptr;
  const std::string port_str = std::to_string(port);
  const int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
  if (gai != 0) {
    err = std::string("getaddrinfo failed: ") + gai_strerror(gai);
    return -1;
  }

  int sock = -1;
  for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
    sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sock < 0) {
      continue;
    }
    if (connect(sock, it->ai_addr, it->ai_addrlen) == 0) {
      break;
    }
    close(sock);
    sock = -1;
  }
  freeaddrinfo(res);

  if (sock < 0) {
    err = "connect failed";
  }
  return sock;
}

} // namespace

namespace service {

ssh_result run_ssh_command(const ssh_request& req) {
  ssh_result result;

  if (req.host.empty() || req.user.empty() || req.password.empty() || req.command.empty()) {
    result.error = "host/user/password/command is required";
    return result;
  }
  if (req.port <= 0 || req.port > 65535) {
    result.error = "invalid ssh port";
    return result;
  }

  static std::once_flag init_once;
  static int init_rc = 0;
  std::call_once(init_once, []() { init_rc = libssh2_init(0); });
  if (init_rc != 0) {
    result.error = "libssh2_init failed";
    return result;
  }

  std::string connect_err;
  const int sock = connect_tcp(req.host, req.port, connect_err);
  if (sock < 0) {
    result.error = connect_err;
    return result;
  }

  LIBSSH2_SESSION* session = libssh2_session_init();
  if (session == nullptr) {
    close(sock);
    result.error = "libssh2_session_init failed";
    return result;
  }

  const int timeout = req.timeout_ms > 0 ? req.timeout_ms : 5000;
  libssh2_session_set_timeout(session, timeout);
  libssh2_session_set_blocking(session, 1);

  if (libssh2_session_handshake(session, sock) != 0) {
    result.error = std::string("ssh handshake failed: ") + session_last_error(session);
    libssh2_session_free(session);
    close(sock);
    return result;
  }

  if (libssh2_userauth_password(session, req.user.c_str(), req.password.c_str()) != 0) {
    result.error = std::string("ssh auth failed: ") + session_last_error(session);
    libssh2_session_disconnect(session, "auth failed");
    libssh2_session_free(session);
    close(sock);
    return result;
  }

  LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session);
  if (channel == nullptr) {
    result.error = std::string("open ssh channel failed: ") + session_last_error(session);
    libssh2_session_disconnect(session, "channel open failed");
    libssh2_session_free(session);
    close(sock);
    return result;
  }

  if (libssh2_channel_exec(channel, req.command.c_str()) != 0) {
    result.error = std::string("exec remote command failed: ") + session_last_error(session);
    libssh2_channel_free(channel);
    libssh2_session_disconnect(session, "exec failed");
    libssh2_session_free(session);
    close(sock);
    return result;
  }

  char buffer[4096];
  while (true) {
    const ssize_t n = libssh2_channel_read(channel, buffer, sizeof(buffer));
    if (n > 0) {
      result.output.append(buffer, static_cast<size_t>(n));
      continue;
    }
    if (n == LIBSSH2_ERROR_EAGAIN) {
      continue;
    }
    break;
  }

  while (true) {
    const ssize_t n = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
    if (n > 0) {
      result.output.append(buffer, static_cast<size_t>(n));
      continue;
    }
    if (n == LIBSSH2_ERROR_EAGAIN) {
      continue;
    }
    break;
  }

  result.exit_status = libssh2_channel_get_exit_status(channel);
  result.ok = (result.exit_status == 0);

  libssh2_channel_close(channel);
  libssh2_channel_free(channel);
  libssh2_session_disconnect(session, "normal shutdown");
  libssh2_session_free(session);
  close(sock);

  return result;
}

} // namespace service
