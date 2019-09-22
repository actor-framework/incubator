/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/detail/quicly_cb.hpp"

#include <fstream>
#include <iostream>
#include <quicly.h>

#include "caf/sec.hpp"

namespace caf {
namespace detail {

constexpr char ticket_file[] = "ticket.bin";

// -- callbacks ----------------------------------------------------------------

int on_stop_sending(quicly_stream_t*, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  std::cerr << "received STOP_SENDING: " << QUICLY_ERROR_GET_ERROR_CODE(err)
            << std::endl;
  return 0;
}

int save_ticket_cb(ptls_save_ticket_t*, ptls_t* tls, ptls_iovec_t src) {
  using namespace std;
  auto conn = static_cast<quicly_conn_t*>(*ptls_get_data_ptr(tls));
  ptls_buffer_t buf;
  char smallbuff[512];
  ofstream fticket(ticket_file, ios::app | ios::binary);
  int ret = 0;
  ptls_buffer_init(&buf, smallbuff, 0);

  /* build data (session ticket and transport parameters) */
  ptls_buffer_push_block(&buf, 2,
                         { ptls_buffer_pushv(&buf, src.base, src.len); });
  ptls_buffer_push_block(&buf, 2, {
    if (quicly_encode_transport_parameter_list(
          &buf, 1, quicly_get_peer_transport_parameters(conn), nullptr, nullptr)
        != 0)
      goto Exit;
  });

  /* write file */
  if (fticket.is_open()) {
    fticket.write(reinterpret_cast<char*>(buf.base), buf.off);
  } else {
    std::cerr << "failed to open file:" << ticket_file << ":" << strerror(errno)
              << std::endl;
    goto Exit;
  }

Exit:
  if (fticket.is_open())
    fticket.close();
  ptls_buffer_dispose(&buf);
  return 0;
}

// -- quicly send functions ----------------------------------------------------

variant<size_t, sec> send_quicly_datagram(net::udp_datagram_socket handle,
                                          quicly_datagram_t* datagram) {
  msghdr mess = {};
  iovec vec = {};
  mess.msg_name = &datagram->dest.sa;
  mess.msg_namelen = quicly_get_socklen(&datagram->dest.sa);
  vec.iov_base = datagram->data.base;
  vec.iov_len = datagram->data.len;
  mess.msg_iov = &vec;
  mess.msg_iovlen = 1;
  auto ret = sendmsg(handle.id, &mess, 0);
  return net::check_udp_datagram_socket_io_res(ret);
}

sec send_pending_datagrams(net::udp_datagram_socket handle,
                           detail::quicly_conn_ptr conn) {
  quicly_datagram_t* packets[16];
  size_t num_packets;
  int ret = 0;
  do {
    num_packets = sizeof(packets) / sizeof(packets[0]);
    if ((ret = quicly_send(conn.get(), packets, &num_packets)) == 0) {
      for (size_t i = 0; i < num_packets; ++i) {
        auto res = send_quicly_datagram(handle, packets[i]);
        if (auto err = get_if<sec>(&res)) {
          return *err; // how to make error from this sec?
        }
        auto pa = quicly_get_context(conn.get())->packet_allocator;
        pa->free_packet(pa, packets[i]);
      }
    }
  } while (ret == 0 && num_packets == sizeof(packets) / sizeof(packets[0]));
  return sec::none;
}

// -- ticket management --------------------------------------------------------

void load_ticket(ptls_handshake_properties_t* hs_properties,
                 quicly_transport_parameters_t* resumed_transport_params) {
  using namespace std;
  static uint8_t buf[65536];
  size_t len = 1;
  int ret = 0;
  ifstream fticket(ticket_file, ios::binary);

  if (!fticket.is_open())
    return;
  fticket.read(reinterpret_cast<char*>(buf), sizeof(buf));
  fticket.close();

  const uint8_t *src = buf, *end = buf + len;
  ptls_iovec_t ticket;
  ptls_decode_open_block(src, end, 2, {
    ticket = ptls_iovec_init(src, end - src);
    src = end;
  });
  ptls_decode_block(src, end, 2,
                    if ((ret = quicly_decode_transport_parameter_list(
                           resumed_transport_params, nullptr, nullptr, 1, src,
                           end))
                        != 0) goto Exit;
                    src = end;);
  hs_properties->client.session_ticket = ticket;

Exit:;
}

} // namespace detail
} // namespace caf
