#include "cachesim/types.hpp"

namespace cachesim {

std::string toString(ReplacementKind kind) {
    switch (kind) {
        case ReplacementKind::LRU: return "LRU";
        case ReplacementKind::FIFO: return "FIFO";
        case ReplacementKind::Random: return "Random";
    }
    return "?";
}

std::string toString(WritePolicy policy) {
    return policy == WritePolicy::WriteBack ? "write-back" : "write-through";
}

std::string toString(AccessType type) { return type == AccessType::Read ? "R" : "W"; }

}  // namespace cachesim
