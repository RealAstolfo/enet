#ifndef NETVAR_HPP
#define NETVAR_HPP

#include "common/exstd/constexpr_map.hpp"
#include "common/common.h"
#include <array>
#include <utility>

template<typename T>
struct netvar {
    T* data;
    size_t id;
    bool modified;
    bool is_local;
};

#endif