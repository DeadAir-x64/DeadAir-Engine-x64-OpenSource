#pragma once
#include <unordered_set>
#include "xr_allocator.h"

// [DA_PORT] Парный к xr_unordered_map. Заведён, когда понадобилось множество живых серверных
// сущностей (xrServer_Object_Base.cpp): в проекте были только xr_set и xr_unordered_map, а нужен
// был именно поиск за постоянное время без второго значения.
template <typename K, class Hasher = std::hash<K>, class Traits = std::equal_to<K>,
          typename allocator = xr_allocator<K>>
using xr_unordered_set = std::unordered_set<K, Hasher, Traits, allocator>;
