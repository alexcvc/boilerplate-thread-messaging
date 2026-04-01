/*************************************************************************/ /**
 * @file
 * @brief   contains implementing winconfig logging to remote server
 * @details This implementation contains winconfig logging to remote server.
 * For more information see:
 * https://github.com/gabime/spdlog/wiki/4.-Sinks#implementing-your-own-sink
 * and
 * Documentation for WinConfig
 *
 * @author      Alexander Sacharov <a.sacharov@gmx.de>
 * @date        2021-07-28
 *****************************************************************************/

#pragma once

//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------
// clang format off
#include <syslog.h>
#include <unistd.h>

#include <array>
#include <map>
#include <sstream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "spdlog/common.h"
#include "spdlog/details/null_mutex.h"
#include "spdlog/details/synchronous_factory.h"
#include "spdlog/sinks/base_sink.h"
// clang format on

//----------------------------------------------------------------------------
// Public defines and macros
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Public typedefs, structs, enums, unions and variables
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Public Function Prototypes
//----------------------------------------------------------------------------

namespace spdlog {
namespace sinks {
  /**
  * Sink that write to rsyslog using udp.
  */
  template <typename Mutex>
  class rsyslog_sink final : public base_sink<Mutex> {
    std::map<int, int> m_mapSeveritySpd2Syslog = {
        {SPDLOG_LEVEL_CRITICAL, LOG_CRIT},
        {SPDLOG_LEVEL_ERROR, LOG_ERR},
        {SPDLOG_LEVEL_WARN, LOG_WARNING},
        {SPDLOG_LEVEL_INFO, LOG_INFO},
        {SPDLOG_LEVEL_DEBUG, LOG_DEBUG},
        {SPDLOG_LEVEL_TRACE, LOG_DEBUG},
        {SPDLOG_LEVEL_OFF, 0},
    };

    const int m_log_buffer_max_size{16777216};  ///< 16K
    struct sockaddr_in m_sockaddr;              ///< address
    int m_fd{-1};                               ///< socket descriptor
    int m_facility{0};                          ///< facility
    std::string m_ident;                        ///< identity
    std::string m_buffer;                       ///< buffer

   public:
    /**
    * constructor
    * @param ident - The string pointed to by ident is prepended to every message.
    * @param server_ip - remote syslog server IP
    * @param facility - facility codes specifies what type of program is logging the message.
    * This lets the configuration file specify that messages from different facilities will be handled differently.
    * https://man7.org/linux/man-pages/man3/syslog.3.html
    * @param port - remote port is 514 by default
    * @param log_buffer_max_size - buffer reserved in the message string. This increases performance when creating
     * log messages.
    * @param enable_formatting - default false. However the message format maybe changed.
    */
    rsyslog_sink(const std::string& ident, const std::string& server_ip, int facility, int log_buffer_max_size,
                 uint16_t port, bool enable_formatting)
        : m_log_buffer_max_size(log_buffer_max_size),
          m_facility(facility),
          m_ident(ident),
          m_enable_formatting(enable_formatting) {
      if (m_log_buffer_max_size > std::numeric_limits<int>::max()) {
        SPDLOG_THROW(spdlog_ex("too large maxLogSize"));
      }

      m_buffer.reserve(m_log_buffer_max_size);
      // socket
      memset(&m_sockaddr, 0, sizeof(m_sockaddr));
      m_sockaddr.sin_family = AF_INET;
      m_sockaddr.sin_port = htons(port);
      inet_pton(AF_INET, server_ip.c_str(), &m_sockaddr.sin_addr);

      // open  socket
      open_socket();
    }

    /**
    * destructor
    */
    ~rsyslog_sink() override {
      close(m_fd);
    }

    /**
    * delete copy constructor and assignment
    */
    rsyslog_sink(const rsyslog_sink&) = delete;
    rsyslog_sink& operator=(const rsyslog_sink&) = delete;

   protected:
    /**
    * sink message after severity filter
    * @param msg - message
    */
    void sink_it_(const details::log_msg& msg) override {
      if (msg.level != level::off) {
        string_view_t payload;
        memory_buf_t formatted;
        if (m_enable_formatting) {
          base_sink<Mutex>::formatter_->format(msg, formatted);
          payload = string_view_t(formatted.data(), formatted.size());
        } else {
          payload = msg.payload;
        }
        size_t length = payload.size();
        // limit to max int
        length = length > static_cast<size_t>(std::numeric_limits<int>::max())
                     ? static_cast<size_t>(std::numeric_limits<int>::max())
                     : length;
        std::stringstream ss;
        // <%u>%s:
        ss << "<" << m_facility + m_mapSeveritySpd2Syslog[msg.level] << ">" << m_ident << ": ";

        m_buffer += ss.str();
        length = length > m_log_buffer_max_size - m_buffer.size() ? m_log_buffer_max_size - m_buffer.size() : length;
        if (length > 0) {
          m_buffer.append(payload.data(), length);
        }
        if (write(m_fd, m_buffer.c_str(), m_buffer.size()) == -1)
          perror("write error");
        m_buffer.clear();
      }
    }

    void flush_() override {}

    bool m_enable_formatting{false};

   private:
    /**
     * open UDP socket
     */
    void open_socket() {
      int nb = 1;

      if ((m_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        SPDLOG_THROW(spdlog_ex("failed create socket"));
      } else if (ioctl(m_fd, FIONBIO, &nb) == -1) {
        SPDLOG_THROW(spdlog_ex("failed ioctl socket FIONBIO"));
      } else if (connect(m_fd, reinterpret_cast<struct sockaddr*>(&m_sockaddr), sizeof(m_sockaddr)) < 0) {
        SPDLOG_THROW(spdlog_ex("failed connect socket"));
      }
    }
  };

  using rsyslog_sink_mt = rsyslog_sink<std::mutex>;
  using rsyslog_sink_st = rsyslog_sink<details::null_mutex>;
}  // namespace sinks

// Create and register a syslog logger
template <typename Factory = synchronous_factory>
inline std::shared_ptr<logger> rsyslog_logger_mt(const std::string& logger_name, const std::string& ident,
                                                 const std::string& rsyslog_ip, uint facility,
                                                 int log_buffer_max_size = 1024 * 1024 * 16, uint16_t port = 514,
                                                 bool enable_formatting = true) {
  return Factory::template create<sinks::rsyslog_sink_mt>(logger_name, ident, rsyslog_ip, facility, log_buffer_max_size,
                                                          port, enable_formatting);
}

template <typename Factory = synchronous_factory>
inline std::shared_ptr<logger> rsyslog_logger_st(const std::string& logger_name, const std::string& ident,
                                                 const std::string& rsyslog_ip, uint facility,
                                                 int log_buffer_max_size = 1024 * 1024 * 16, uint16_t port = 514,
                                                 bool enable_formatting = true) {
  return Factory::template create<sinks::rsyslog_sink_st>(logger_name, ident, rsyslog_ip, facility, log_buffer_max_size,
                                                          port, enable_formatting);
}

}  // namespace spdlog
