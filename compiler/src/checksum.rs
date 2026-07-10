use anyhow::{Result, bail};
use flate2::{Compression, write::ZlibEncoder};
use md5::{Digest, Md5};
use std::io::Write;

pub const CHECKSUM_MAGIC: &[u8] = b"mimic_checksum_list\0";

pub fn template_header(data: &[u8]) -> Result<Vec<u8>> {
    if !data.starts_with(CHECKSUM_MAGIC) {
        bail!("invalid checksum template");
    }
    let offset = CHECKSUM_MAGIC.len() + 4;
    if data.len() < offset + 4 {
        bail!("short checksum template");
    }
    let len = u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap()) as usize;
    if data.len() < offset + 4 + len {
        bail!("short checksum header");
    }
    Ok(data[offset + 4..offset + 4 + len].to_vec())
}

pub fn md5(data: &[u8]) -> [u8; 16] {
    Md5::digest(data).into()
}

pub fn encode(header: &[u8], entries: &[(&str, [u8; 16])]) -> Result<Vec<u8>> {
    let mut body = Vec::new();
    for (name, digest) in entries {
        body.extend(name.as_bytes());
        body.push(0);
        body.extend(digest);
    }
    let mut z = ZlibEncoder::new(Vec::new(), Compression::best());
    z.write_all(&body)?;
    let compressed = z.finish()?;
    let mut out = CHECKSUM_MAGIC.to_vec();
    out.extend(1u32.to_le_bytes());
    out.extend((header.len() as u32).to_le_bytes());
    out.extend(header);
    out.extend((entries.len() as u32).to_le_bytes());
    out.extend(compressed);
    Ok(out)
}
