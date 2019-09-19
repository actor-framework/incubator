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

#pragma once

#include "caf/detail/quicly_cb.hpp"

#include <fstream>
#include <iostream>
#include <quicly.h>

namespace detail {

constexpr char ticket_file[] = "ticket.bin";

int on_stop_sending(quicly_stream_t*, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  std::cerr << "received STOP_SENDING: " << QUICLY_ERROR_GET_ERROR_CODE(err)
            << std::endl;
  return 0;
}

int on_receive_reset(quicly_stream_t*, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  std::cerr << "received RESET_STREAM: " << QUICLY_ERROR_GET_ERROR_CODE(err)
            << std::endl;
  return 0;
}

void on_closed_by_peer(quicly_closed_by_peer_t*, quicly_conn_t*, int err,
                       uint64_t frame_type, const char* reason, size_t) {
  if (QUICLY_ERROR_IS_QUIC_TRANSPORT(err)) {
    std::cerr << "transport close:code=0x" << std::hex
              << QUICLY_ERROR_GET_ERROR_CODE(err) << ";frame=" << frame_type
              << ";reason=" << reason << std::endl;
  } else if (QUICLY_ERROR_IS_QUIC_APPLICATION(err)) {
    std::cerr << "application close:code=0x" << std::hex
              << QUICLY_ERROR_GET_ERROR_CODE(err) << ";reason=" << reason
              << std::endl;
  } else if (err == QUICLY_ERROR_RECEIVED_STATELESS_RESET) {
    std::cerr << "stateless reset" << std::endl;
  } else {
    std::cerr << "unexpected close:code=" << err << std::endl;
  }
}

/*int send_one(int fd, quicly_datagram_t* p) {
  msghdr mess = {};
  iovec vec = {};
  memset(&mess, 0, sizeof(mess));
  mess.msg_name = &p->sa;
  mess.msg_namelen = p->salen;
  vec.iov_base = p->data.base;
  vec.iov_len = p->data.len;
  mess.msg_iov = &vec;
  mess.msg_iovlen = 1;
  auto ret = sendmsg(fd, &mess, 0);
  return static_cast<int>(ret);
}*/

int send_pending(int fd, quicly_conn_t* conn) {
  quicly_datagram_t* packets[16];
  size_t num_packets, i;
  int ret;

  do {
    num_packets = sizeof(packets) / sizeof(packets[0]);
    if ((ret = quicly_send(conn, packets, &num_packets)) == 0) {
      for (i = 0; i != num_packets; ++i) {
        if ((send_one(fd, packets[i])) == -1) {
          perror("sendmsg failed");
        } else {
        }
        ret = 0;
        quicly_packet_allocator_t* pa = quicly_get_context(conn)
                                          ->packet_allocator;
        pa->free_packet(pa, packets[i]);
      }
    } else {
    }
  } while (ret == 0 && num_packets == sizeof(packets) / sizeof(packets[0]));

  return ret;
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
