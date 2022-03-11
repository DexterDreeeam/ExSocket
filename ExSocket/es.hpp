#pragma once

namespace es
{

// set 'false' if disable print
const bool print_msg = true;

// id for connection
const unsigned int session_id = 1234;

// packet max len
const unsigned int tcp_packet_max_len = 1024 * 1024 * 64;

// tcp disconnect if no data for period (milliseconds), 0 means no limit
const unsigned int tcp_data_timeout = 1000 * 10;

}

#include "internal/common.hpp"

#include "internal/tcp.hpp"
#include "internal/udp.hpp"

#include "internal/tcp.inl"
#include "internal/udp.inl"
