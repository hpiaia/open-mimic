use crate::{checksum, codec, manifest::Manifest, storage::MimicStorage};
use anyhow::{Context, Result, bail};
use image::{ImageFormat, imageops::FilterType};
use indexmap::IndexMap;
use std::{
    fs,
    io::Cursor,
    path::{Path, PathBuf},
};

const POOL: &str = "INST0pool0";
const EMPTY_SAMPLE: u32 = u32::MAX;

#[derive(Debug, Clone)]
pub struct CompileResult {
    pub library_dir: PathBuf,
    pub bin_path: PathBuf,
    pub drd_path: PathBuf,
    pub sample_count: usize,
    pub layer_count: usize,
    pub channel_count: u16,
    pub drd_bytes: u64,
}

fn resolve(base: &Path, path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_owned()
    } else {
        base.join(path)
    }
}
fn required_u32(map: &IndexMap<String, u32>, key: &str) -> Result<u32> {
    map.get(key)
        .copied()
        .with_context(|| format!("template missing {key}"))
}
fn valid_name(value: &str) -> Result<()> {
    if value.is_empty() || value.contains(['/', '\\', '\0']) {
        bail!("invalid Mimic name: {value:?}");
    }
    Ok(())
}

fn read_wav(path: &Path, channels: u16) -> Result<Vec<i32>> {
    let mut reader =
        hound::WavReader::open(path).with_context(|| format!("open WAV {}", path.display()))?;
    let spec = reader.spec();
    if spec.channels != channels
        || spec.sample_rate != 48_000
        || spec.bits_per_sample != 16
        || spec.sample_format != hound::SampleFormat::Int
    {
        bail!(
            "{}: expected {}ch 48kHz PCM16 WAV, got {}ch {}Hz {:?} {}-bit",
            path.display(),
            channels,
            spec.channels,
            spec.sample_rate,
            spec.sample_format,
            spec.bits_per_sample
        );
    }
    reader
        .samples::<i16>()
        .map(|v| Ok((v? as i32) << 8))
        .collect()
}

fn normalize_png(path: &Path) -> Result<Vec<u8>> {
    let bytes = fs::read(path)?;
    let image = image::load_from_memory(&bytes)?
        .resize_exact(94, 63, FilterType::Lanczos3)
        .to_rgba8();
    let mut cursor = Cursor::new(Vec::new());
    image.write_to(&mut cursor, ImageFormat::Png)?;
    Ok(cursor.into_inner())
}

fn set_rms_entries(
    storage: &mut MimicStorage,
    original: &MimicStorage,
    count: usize,
    old_count: usize,
) {
    for index in 0..count {
        let source = index % old_count;
        for suffix in ["rmsl"] {
            if let Some(v) = original
                .u32_values
                .get(&format!("{POOL}rmsenv{source}{suffix}"))
            {
                storage
                    .u32_values
                    .insert(format!("{POOL}rmsenv{index}{suffix}"), *v);
            }
        }
        for suffix in ["rmse"] {
            if let Some(v) = original
                .u32_arrays
                .get(&format!("{POOL}rmsenv{source}{suffix}"))
            {
                storage
                    .u32_arrays
                    .insert(format!("{POOL}rmsenv{index}{suffix}"), v.clone());
            }
        }
        for suffix in ["mrms"] {
            if let Some(v) = original
                .f32_values
                .get(&format!("{POOL}rmsenv{source}{suffix}"))
            {
                storage
                    .f32_values
                    .insert(format!("{POOL}rmsenv{index}{suffix}"), *v);
            }
        }
    }
}

pub fn compile_manifest_file(path: &Path) -> Result<CompileResult> {
    let text = fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
    let manifest: Manifest = serde_json::from_str(&text)?;
    compile_manifest(&manifest, path.parent().unwrap_or(Path::new(".")))
}

/// Reusable entry point intended to be called directly from a Tauri command.
pub fn compile_manifest(manifest: &Manifest, base: &Path) -> Result<CompileResult> {
    valid_name(&manifest.instrument.name)?;
    valid_name(&manifest.instrument.library_name)?;
    if !(1..=24).contains(&manifest.codec_width) {
        bail!("codec_width must be 1..24");
    }
    if manifest.velocity_layers.is_empty() || manifest.velocity_layers.len() > 16 {
        bail!("velocity_layers must contain 1..16 layers");
    }
    let template_path = resolve(base, &manifest.template_bin);
    let original = MimicStorage::decode(&fs::read(&template_path)?)?;
    let mut storage = original.clone();
    let mic_count = required_u32(&storage.u32_values, "INST0micnt")? as usize;
    if !manifest.mics.is_empty() && manifest.mics.len() != mic_count {
        bail!(
            "manifest has {} mics; template requires {mic_count}",
            manifest.mics.len()
        );
    }
    let mut channels = 0u16;
    for i in 0..mic_count {
        channels += if storage
            .bytes
            .get(&format!("INST0micinf{i}isst"))
            .copied()
            .unwrap_or(0)
            != 0
        {
            2
        } else {
            1
        };
    }
    let mut previous = None;
    let mut samples = Vec::new();
    let mut layer_indices = Vec::new();
    for layer in &manifest.velocity_layers {
        if layer.min_velocity > 127 || previous.is_some_and(|v| layer.min_velocity <= v) {
            bail!("layer velocities must be unique ascending values <=127");
        }
        previous = Some(layer.min_velocity);
        if layer.samples.is_empty() || layer.samples.len() > 16 {
            bail!("each layer needs 1..16 samples");
        }
        let mut indices = Vec::new();
        for wav in &layer.samples {
            let values = read_wav(&resolve(base, wav), channels)?;
            if values.len() % channels as usize != 0 {
                bail!("WAV does not contain complete frames");
            }
            indices.push(samples.len() as u32);
            samples.push(values);
        }
        layer_indices.push(indices);
    }
    if manifest.velocity_layers[0].min_velocity != 0 {
        bail!("first layer must start at velocity 0");
    }
    let mut encoded = Vec::new();
    for values in &samples {
        let packed = codec::encode(values, manifest.codec_width)?;
        if codec::decode(&packed)? != *values {
            bail!("internal codec round-trip failed");
        }
        encoded.push(packed);
    }
    let preloads: Vec<Vec<u8>> = samples
        .iter()
        .map(|values| {
            let mut out = vec![0u8; 64 * 4];
            for &v in values.iter().take(manifest.preload_samples) {
                out.extend((v as f32 / (1u32 << 23) as f32).to_le_bytes());
            }
            out
        })
        .collect();
    let mut cursor = 0u32;
    let mut dedo = Vec::new();
    for p in &preloads {
        dedo.push(cursor);
        cursor = cursor
            .checked_add(p.len() as u32)
            .context("DRD exceeds 4 GiB")?;
    }
    let mut dofs = Vec::new();
    for p in &encoded {
        dofs.push(cursor);
        cursor = cursor
            .checked_add(p.len() as u32)
            .context("DRD exceeds 4 GiB")?;
    }
    let old_count = required_u32(&storage.u32_values, &format!("{POOL}psz"))? as usize;
    storage
        .u32_values
        .insert(format!("{POOL}psz"), samples.len() as u32);
    storage.u32_arrays.insert(format!("{POOL}dedo"), dedo);
    storage.u32_arrays.insert(
        format!("{POOL}desc"),
        preloads.iter().map(|v| (v.len() / 4) as u32).collect(),
    );
    storage.u32_arrays.insert(format!("{POOL}dofs"), dofs);
    storage.u32_arrays.insert(
        format!("{POOL}dlen"),
        encoded.iter().map(|v| v.len() as u32).collect(),
    );
    storage.u32_arrays.insert(
        format!("{POOL}fcnt"),
        samples.iter().map(|v| v.len() as u32).collect(),
    );
    storage
        .u32_arrays
        .insert(format!("{POOL}nchn"), vec![channels as u32; samples.len()]);
    storage
        .u32_arrays
        .insert(format!("{POOL}nobu"), vec![64; samples.len()]);
    storage.f32_arrays.insert(
        format!("{POOL}svol"),
        vec![manifest.sample_volume; samples.len()],
    );
    set_rms_entries(&mut storage, &original, samples.len(), old_count);
    storage
        .u32_values
        .retain(|k, _| !k.starts_with("INST0artic0veloLay"));
    storage
        .u32_arrays
        .retain(|k, _| !k.starts_with("INST0artic0veloLay"));
    storage
        .f32_values
        .retain(|k, _| !k.starts_with("INST0artic0veloLay"));
    storage
        .f32_arrays
        .retain(|k, _| !k.starts_with("INST0artic0veloLay"));
    storage.u32_values.insert(
        "INST0artic0numlay".into(),
        manifest.velocity_layers.len() as u32,
    );
    for (i, (layer, indices)) in manifest
        .velocity_layers
        .iter()
        .zip(&layer_indices)
        .enumerate()
    {
        for (label, values) in [
            ("min_round_robin_level", &layer.min_round_robin_level),
            ("max_round_robin_level", &layer.max_round_robin_level),
        ] {
            if values.as_ref().is_some_and(|values| values.len() != 8) {
                bail!("velocity layer {i}: {label} must contain exactly 8 values");
            }
        }
        let p = format!("INST0artic0veloLay{i}");
        storage
            .u32_values
            .insert(format!("{p}minvel"), layer.min_velocity);
        storage
            .u32_values
            .insert(format!("{p}nums"), indices.len() as u32);
        let mut smp = indices.clone();
        smp.resize(16, EMPTY_SAMPLE);
        storage.u32_arrays.insert(format!("{p}smpidx"), smp);
        storage
            .f32_values
            .insert(format!("{p}minvol"), layer.min_volume);
        storage
            .f32_values
            .insert(format!("{p}maxvol"), layer.max_volume);
        storage.f32_arrays.insert(
            format!("{p}minrtl"),
            layer
                .min_round_robin_level
                .clone()
                .unwrap_or_else(|| vec![1.0; 8]),
        );
        storage.f32_arrays.insert(
            format!("{p}maxrtl"),
            layer
                .max_round_robin_level
                .clone()
                .unwrap_or_else(|| vec![1.0; 8]),
        );
    }
    let inst = &manifest.instrument;
    storage
        .strings
        .insert("INST0insnam".into(), inst.name.clone());
    storage
        .strings
        .insert("INST0insdat".into(), inst.name.clone());
    storage
        .strings
        .insert("INST0libnam".into(), inst.library_name.clone());
    storage.strings.insert("INST0insimj".into(), String::new());
    if let Some(v) = &inst.instrument_type {
        storage.strings.insert("INST0instyp".into(), v.clone());
    }
    if let Some(v) = &inst.articulation_name {
        storage.strings.insert("INST0artic0artn".into(), v.clone());
    }
    if let Some(v) = inst.midi_note {
        if v > 127 {
            bail!("midi_note must be <= 127");
        }
        storage.u32_values.insert("INST0artic0noteon".into(), v);
    }
    for (i, mic) in manifest.mics.iter().enumerate() {
        storage
            .strings
            .insert(format!("INST0micinf{i}micn"), mic.name.clone());
        storage
            .f32_values
            .insert(format!("INST0micinf{i}micv"), mic.volume);
    }
    if let Some(path) = &manifest.image {
        storage.blob = normalize_png(&resolve(base, path))?;
    }
    let output_root = resolve(base, &manifest.output_directory);
    let lib_dir = output_root.join(format!("{}.lib", inst.library_name));
    if lib_dir.exists() {
        fs::remove_dir_all(&lib_dir)?;
    }
    let instruments = lib_dir.join("instruments");
    fs::create_dir_all(&instruments)?;
    let bin_path = instruments.join(format!("{}.bin", inst.name));
    let drd_path = instruments.join(format!("{}.drd", inst.name));
    let bin = storage.encode()?;
    let mut drd = Vec::with_capacity(cursor as usize);
    for p in &preloads {
        drd.extend(p);
    }
    for p in &encoded {
        drd.extend(p);
    }
    fs::write(&bin_path, &bin)?;
    fs::write(&drd_path, &drd)?;
    fs::write(
        lib_dir.join("libver.mimicinfo"),
        manifest.library_version.as_bytes(),
    )?;
    let template_checksum = fs::read(
        template_path
            .parent()
            .and_then(Path::parent)
            .context("template must be inside <library>/instruments")?
            .join("checksum.dat"),
    )?;
    let header = checksum::template_header(&template_checksum)?;
    let bin_name = bin_path.file_name().unwrap().to_string_lossy();
    let drd_name = drd_path.file_name().unwrap().to_string_lossy();
    let checks = checksum::encode(
        &header,
        &[
            (&bin_name, checksum::md5(&bin)),
            (&drd_name, checksum::md5(&drd)),
        ],
    )?;
    fs::write(lib_dir.join("checksum.dat"), checks)?;
    let rebuilt = MimicStorage::decode(&bin)?;
    let offsets = &rebuilt.u32_arrays[&format!("{POOL}dofs")];
    let lengths = &rebuilt.u32_arrays[&format!("{POOL}dlen")];
    let end = offsets
        .iter()
        .zip(lengths)
        .map(|(o, n)| o + n)
        .max()
        .unwrap_or(0);
    if end as usize != drd.len() {
        bail!("generated DRD span mismatch");
    }
    Ok(CompileResult {
        library_dir: lib_dir,
        bin_path,
        drd_path,
        sample_count: samples.len(),
        layer_count: manifest.velocity_layers.len(),
        channel_count: channels,
        drd_bytes: drd.len() as u64,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::manifest::{Instrument, Mic, VelocityLayer};

    #[test]
    fn compiles_six_channel_library_when_template_is_available() {
        let template = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../assets/Tama SCM Steve Mackrill.lib/instruments/Kick Tma 22 SCL.bin");
        if !template.exists() {
            return;
        }
        let temp = tempfile::tempdir().unwrap();
        let wav = temp.path().join("six.wav");
        let spec = hound::WavSpec {
            channels: 6,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: hound::SampleFormat::Int,
        };
        let mut writer = hound::WavWriter::create(&wav, spec).unwrap();
        for frame in 0..1000 {
            for channel in 0..6 {
                writer
                    .write_sample::<i16>(((frame * (channel + 1)) % 2000 - 1000) as i16)
                    .unwrap();
            }
        }
        writer.finalize().unwrap();
        let manifest = Manifest {
            template_bin: template.canonicalize().unwrap(),
            output_directory: temp.path().to_owned(),
            library_version: "test".into(),
            codec_width: 24,
            preload_samples: 500,
            sample_volume: 1.0,
            instrument: Instrument {
                name: "Rust Test Kick".into(),
                library_name: "Rust Test Library".into(),
                instrument_type: Some("Kick".into()),
                articulation_name: Some("Kick".into()),
                midi_note: Some(36),
            },
            mics: vec![
                Mic {
                    name: "In".into(),
                    volume: 1.0,
                },
                Mic {
                    name: "Out".into(),
                    volume: 1.0,
                },
                Mic {
                    name: "OH".into(),
                    volume: 1.0,
                },
                Mic {
                    name: "Room".into(),
                    volume: 1.0,
                },
            ],
            velocity_layers: vec![VelocityLayer {
                min_velocity: 0,
                min_volume: 1.0,
                max_volume: 1.0,
                samples: vec![wav],
                min_round_robin_level: None,
                max_round_robin_level: None,
            }],
            image: None,
        };
        let result = compile_manifest(&manifest, Path::new(".")).unwrap();
        let storage = MimicStorage::decode(&fs::read(result.bin_path).unwrap()).unwrap();
        assert_eq!(storage.u32_values[&format!("{POOL}psz")], 1);
        assert_eq!(storage.u32_arrays[&format!("{POOL}nchn")], vec![6]);
        let drd = fs::read(result.drd_path).unwrap();
        let offset = storage.u32_arrays[&format!("{POOL}dofs")][0] as usize;
        let length = storage.u32_arrays[&format!("{POOL}dlen")][0] as usize;
        assert_eq!(
            codec::decode(&drd[offset..offset + length]).unwrap().len(),
            6000
        );
    }
}
