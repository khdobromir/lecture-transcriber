# Performance measurements

[User limitations](../user/guide.md) · [Testing](../testing/README.md)

There is no current cross-hardware speed or WER guarantee. A short real-ASR smoke
establishes integration, not accuracy. Report exact source/binary/tool/model/input
hashes, CPU/instruction dispatch, available cores, RAM/swap, OS/compiler/build
type, power/governor, duration, VAD and threads/chunks/jobs.

The [2026-10-03 lecture record](../archive/lecture-validation-2026-10-03.md)
is one historical 5,610.283-second Russian recording on a Ryzen 5 7430U/16 GiB
Omarchy laptop. It explicitly identifies development commit
`206cdb51769ad69c96ba4baf4cff988823011705` and binary version **0.2.1**, preceding
v0.3.0. It is not a v0.4.0 benchmark. Filesystem cache was not cold; unrelated
desktop processes and system-level I/O/temperature/swap affected observations.
Do not attribute all system resource changes to Transcribe.

## Repeatable local comparison

Linux [benchmark.cpp](../../tests/benchmark.cpp) builds as `benchmark` with tests.
Use authorized local speech at least ten minutes long and an isolated output/data
directory with installed tools/models:

```sh
out/build/linux-cli-release/benchmark --input /path/to/lecture.wav \
  --binary out/build/linux-cli-release/transcribe --app-home /path/to/isolated/data \
  --model medium --out /path/to/new/benchmark-parent
```

The harness warms five seconds, then runs three repetitions for each fixed
variant: one chunk/job with 4/6/8/12 threads; ten chunks with 2×3 or 3×2 workers×threads.
It records wall time (preparation/split/model load/merge included), median of three,
and process-tree peak sampled RSS/VmSwap every 10 ms. Summed RSS can double-count
shared pages; short spikes may be missed. Raw CSV, report and outputs are retained
under a unique run directory. `--allow-short` permits technical experiments only.
These fixed variants are measurements to compare, not automatic settings advice.

Keep input/model/cache conditions stable, report warm/cold behavior, and inspect
all chunk boundaries against the audio. The harness does not score accuracy;
its proposed split recommendation needs over 15% time improvement and no observed
boundary degradation. A proper WER study additionally needs a licensed reference
transcript and a stated normalization/scoring method. Private media/transcripts
and full diagnostic logs must not be published with results.
