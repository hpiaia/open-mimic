# Open Mimic Compiler (Rust)

This crate compiles hardware-compatible Pearl Mimic Pro instrument libraries and
is designed to be embedded directly in a Tauri application.

The compiler supports:

- mono and stereo microphone groups defined by a known-working template;
- discrete close, overhead, and room channels;
- 1–16 velocity layers;
- 1–16 round-robin samples per layer;
- 48 kHz, 16-bit, interleaved PCM WAV input;
- the hardware-validated 1024-byte Mimic streaming codec;
- raw preload generation, `.bin` metadata, `.drd` audio, and `checksum.dat`;
- instrument/library names, type, MIDI note, mic names/volumes, and thumbnails.

## CLI

From the repository root:

```sh
cargo run --release --manifest-path compiler/Cargo.toml -- \
  examples/multimic-kick.json
```

The example produces `build/Open Mimic Compiler Example.lib`.

Paths in a manifest are resolved relative to the manifest file. A custom image
is optional; when omitted, the compiler retains the template's known-working
embedded image. Custom images are converted to 94×63 RGBA PNG.

The mic topology comes from the template because Mimic mic positions and stereo
flags affect channel routing. For the SCM kick template the WAV channel order is:

```text
0 Kick In (mono)
1 Kick Out (mono)
2 Overheads left
3 Overheads right
4 Room left
5 Room right
```

Every WAV assigned to one instrument must have that same channel count/order.

## Tauri integration

Add the crate as a path dependency in `src-tauri/Cargo.toml`:

```toml
[dependencies]
open-mimic-compiler = { path = "../../compiler" }
```

The library exposes both `compile_manifest_file` and the UI-friendly
`compile_manifest` function. A Tauri command can deserialize the manifest sent
by the frontend and call the library directly:

```rust,ignore
#[tauri::command]
async fn compile_instrument(
    manifest: open_mimic_compiler::Manifest,
    project_dir: std::path::PathBuf,
) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        open_mimic_compiler::compile_manifest(&manifest, &project_dir)
            .map(|result| result.library_dir.display().to_string())
            .map_err(|error| format!("{error:#}"))
    })
    .await
    .map_err(|error| error.to_string())?
}
```

Use a blocking worker because WAV encoding and checksum generation are CPU and
disk intensive. The frontend does not need to spawn or parse a CLI process.

## Validated behavior

The streaming codec and six-channel compilation path were validated on a real
Mimic Pro on 10 July 2026. A 1.5-second test instrument correctly played two
mono close signals, stereo overhead noise, and independent stereo room tones.

The compiler does not modify firmware. It produces importable instrument files.
