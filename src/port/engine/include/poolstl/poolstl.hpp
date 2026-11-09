// The engine asks for parallel algorithms through std::execution. MSVC provides
// that itself, so nothing else is needed on Windows.
#include <algorithm>
#include <execution>
