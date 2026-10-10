// A trusted-directory test backend with a missing dependency. The dependency
// must not be resolved from the caller's cwd or PATH during backend scoring.
extern "C" __declspec(dllimport) int ggml_backend_score();
extern "C" __declspec(dllexport) int transcribe_dependency_probe() { return ggml_backend_score(); }
