use anyhow::{Context, Result};
use open_mimic_compiler::compile_manifest_file;
use std::{env, path::PathBuf};

fn main() -> Result<()> {
    let manifest = env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .context("usage: open-mimic-compile <manifest.json>")?;
    let result = compile_manifest_file(&manifest)?;
    println!("built: {}", result.library_dir.display());
    println!(
        "samples: {}, layers: {}, channels: {}, audio: {} bytes",
        result.sample_count, result.layer_count, result.channel_count, result.drd_bytes
    );
    Ok(())
}
