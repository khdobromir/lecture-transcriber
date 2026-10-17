# Local portability changes

Pinned upstream: dd62982dfdefdfc9232ce8d76e122fb1c6d188ae (Apache-2.0).

* Replace Abseil StrCat/Hex with standard stream hex formatting.
* Replace the Abseil pixel-count map with std::unordered_map. Input order is retained in the separate pixels vector.
* Replace process-global srand/rand with a local, seeded mt19937; palette work cannot modify another worker's random state. Sampling uses the raw engine output rather than implementation-specific distributions.

* Include <optional> explicitly in temperature_cache.h for standalone compilation.

* Use standard C++ aggregate construction, the existing kPi constant and the unsigned Argb fallback type for MSVC.

* Qualify floating-point absolute values with std::abs to avoid integer-overload truncation on Clang.

All other C++ sources are unmodified. See UPSTREAM.json for archive SHA-256.
