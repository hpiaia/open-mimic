use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Manifest {
    pub template_bin: PathBuf,
    #[serde(default = "default_output")]
    pub output_directory: PathBuf,
    #[serde(default = "default_version")]
    pub library_version: String,
    #[serde(default = "default_width")]
    pub codec_width: u8,
    #[serde(default = "default_preload")]
    pub preload_samples: usize,
    #[serde(default = "default_volume")]
    pub sample_volume: f32,
    pub instrument: Instrument,
    #[serde(default)]
    pub mics: Vec<Mic>,
    pub velocity_layers: Vec<VelocityLayer>,
    pub image: Option<PathBuf>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Instrument {
    pub name: String,
    pub library_name: String,
    #[serde(rename = "type")]
    pub instrument_type: Option<String>,
    pub articulation_name: Option<String>,
    pub midi_note: Option<u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Mic {
    pub name: String,
    #[serde(default = "default_volume")]
    pub volume: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VelocityLayer {
    pub min_velocity: u32,
    #[serde(default = "default_volume")]
    pub min_volume: f32,
    #[serde(default = "default_volume")]
    pub max_volume: f32,
    pub samples: Vec<PathBuf>,
    pub min_round_robin_level: Option<Vec<f32>>,
    pub max_round_robin_level: Option<Vec<f32>>,
}

fn default_output() -> PathBuf {
    PathBuf::from("build")
}
fn default_version() -> String {
    "Open Mimic 1".into()
}
fn default_width() -> u8 {
    24
}
fn default_preload() -> usize {
    24_000
}
fn default_volume() -> f32 {
    1.0
}
