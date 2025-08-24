# MiniShrinkler

A simplified version of the Shrinkler compressor, implemented in a single C file with hash-based match finding for excellent compression performance.

## Features

- **Compatibility**: Produces bitstreams compatible with existing Shrinkler decompressors
- **No dynamic allocation**: All memory is passed via pre-allocated buffers
- **Compact code**: Single C file (< 1000 lines)
- **Decent compression**: Hash-based match finding plus basic range encoder
