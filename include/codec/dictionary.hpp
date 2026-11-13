#pragma once

#include <array>
#include <string_view>

namespace bardzip {

using Dictionary = std::array<std::string_view, 16>;

extern const Dictionary adjectives;
extern const Dictionary nouns;
extern const Dictionary verbs;
extern const Dictionary objects;

}
