#pragma once

namespace es
{

// set 'false' if disable print
const bool print_msg = true;

// id for connection
const unsigned int session_id = 1234;

const unsigned int max_tcp_clients = 4;

}

#include "internal/common.hpp"

#include "internal/tcp.hpp"
#include "internal/udp.hpp"

#include "internal/tcp.inl"
#include "internal/udp.inl"
