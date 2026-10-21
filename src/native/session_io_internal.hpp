#pragma once
#include <imagejxx/session.hpp>
namespace imagejxx::detail {
std::vector<unsigned char> read_session_bytes(const std::string &,
                                              const SessionLimits &,
                                              const ExecutionControl &);
}
