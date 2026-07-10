use anyhow::{Context, Result, bail};
use flate2::{Compression, read::ZlibDecoder, write::ZlibEncoder};
use indexmap::IndexMap;
use std::io::{Cursor, Read, Write};

pub const STORAGE_MAGIC: &[u8] = b"mimic_storage\0";

#[derive(Debug, Clone, Default)]
pub struct MimicStorage {
    pub version: u32,
    pub u32_values: IndexMap<String, u32>,
    pub u32_arrays: IndexMap<String, Vec<u32>>,
    pub f32_values: IndexMap<String, f32>,
    pub f32_arrays: IndexMap<String, Vec<f32>>,
    pub bytes: IndexMap<String, u8>,
    pub byte_arrays: IndexMap<String, Vec<u8>>,
    pub strings: IndexMap<String, String>,
    pub blob: Vec<u8>,
    pub tail: Vec<u8>,
}

struct Reader<'a> {
    data: &'a [u8],
    offset: usize,
}
impl<'a> Reader<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, offset: 0 }
    }
    fn take(&mut self, count: usize) -> Result<&'a [u8]> {
        let end = self.offset.checked_add(count).context("offset overflow")?;
        if end > self.data.len() {
            bail!("short payload at 0x{:x}", self.offset);
        }
        let result = &self.data[self.offset..end];
        self.offset = end;
        Ok(result)
    }
    fn u8(&mut self) -> Result<u8> {
        Ok(self.take(1)?[0])
    }
    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn f32(&mut self) -> Result<f32> {
        Ok(f32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn key(&mut self) -> Result<String> {
        let len = self.u32()? as usize;
        if len == 0 {
            bail!("zero-length key");
        }
        let raw = self.take(len)?;
        if raw[len - 1] != 0 {
            bail!("key is not NUL terminated");
        }
        Ok(String::from_utf8_lossy(&raw[..len - 1]).into_owned())
    }
}

impl MimicStorage {
    pub fn decode(data: &[u8]) -> Result<Self> {
        if !data.starts_with(STORAGE_MAGIC) {
            bail!("not a mimic_storage container");
        }
        let mut outer = Reader::new(&data[STORAGE_MAGIC.len()..]);
        let version = outer.u32()?;
        let expected = outer.u32()? as usize;
        let mut decoder = ZlibDecoder::new(Cursor::new(&data[STORAGE_MAGIC.len() + 8..]));
        let mut payload = Vec::new();
        decoder
            .read_to_end(&mut payload)
            .context("zlib decode failed")?;
        if payload.len() != expected {
            bail!("payload size {}, expected {}", payload.len(), expected);
        }
        let mut r = Reader::new(&payload);
        let mut result = Self {
            version,
            ..Self::default()
        };
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let value = r.u32()?;
            result.u32_values.insert(key, value);
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let count = r.u32()? as usize;
            let mut value = Vec::with_capacity(count);
            for _ in 0..count {
                value.push(r.u32()?);
            }
            result.u32_arrays.insert(key, value);
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let value = r.f32()?;
            result.f32_values.insert(key, value);
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let count = r.u32()? as usize;
            let mut value = Vec::with_capacity(count);
            for _ in 0..count {
                value.push(r.f32()?);
            }
            result.f32_arrays.insert(key, value);
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let value = r.u8()?;
            result.bytes.insert(key, value);
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let count = r.u32()? as usize;
            result.byte_arrays.insert(key, r.take(count)?.to_vec());
        }
        for _ in 0..r.u32()? {
            let key = r.key()?;
            let count = r.u32()? as usize;
            let raw = r.take(count)?;
            let end = raw.iter().position(|v| *v == 0).unwrap_or(raw.len());
            result
                .strings
                .insert(key, String::from_utf8_lossy(&raw[..end]).into_owned());
        }
        if r.offset + 4 <= payload.len() {
            let count = r.u32()? as usize;
            result.blob = r.take(count)?.to_vec();
        }
        result.tail = payload[r.offset..].to_vec();
        Ok(result)
    }

    pub fn encode(&self) -> Result<Vec<u8>> {
        let mut p = Vec::new();
        put_u32(&mut p, self.u32_values.len());
        for (k, v) in &self.u32_values {
            put_key(&mut p, k);
            p.extend(v.to_le_bytes());
        }
        put_u32(&mut p, self.u32_arrays.len());
        for (k, values) in &self.u32_arrays {
            put_key(&mut p, k);
            put_u32(&mut p, values.len());
            for v in values {
                p.extend(v.to_le_bytes());
            }
        }
        put_u32(&mut p, self.f32_values.len());
        for (k, v) in &self.f32_values {
            put_key(&mut p, k);
            p.extend(v.to_le_bytes());
        }
        put_u32(&mut p, self.f32_arrays.len());
        for (k, values) in &self.f32_arrays {
            put_key(&mut p, k);
            put_u32(&mut p, values.len());
            for v in values {
                p.extend(v.to_le_bytes());
            }
        }
        put_u32(&mut p, self.bytes.len());
        for (k, v) in &self.bytes {
            put_key(&mut p, k);
            p.push(*v);
        }
        put_u32(&mut p, self.byte_arrays.len());
        for (k, v) in &self.byte_arrays {
            put_key(&mut p, k);
            put_u32(&mut p, v.len());
            p.extend(v);
        }
        put_u32(&mut p, self.strings.len());
        for (k, v) in &self.strings {
            put_key(&mut p, k);
            let mut raw = v.as_bytes().to_vec();
            raw.push(0);
            put_u32(&mut p, raw.len());
            p.extend(raw);
        }
        put_u32(&mut p, self.blob.len());
        p.extend(&self.blob);
        p.extend(&self.tail);
        let mut z = ZlibEncoder::new(Vec::new(), Compression::best());
        z.write_all(&p)?;
        let compressed = z.finish()?;
        let mut out = STORAGE_MAGIC.to_vec();
        out.extend(self.version.to_le_bytes());
        out.extend((p.len() as u32).to_le_bytes());
        out.extend(compressed);
        Ok(out)
    }
}

fn put_u32(out: &mut Vec<u8>, value: usize) {
    out.extend((value as u32).to_le_bytes());
}
fn put_key(out: &mut Vec<u8>, key: &str) {
    put_u32(out, key.len() + 1);
    out.extend(key.as_bytes());
    out.push(0);
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn round_trip() {
        let mut s = MimicStorage {
            version: 1,
            blob: vec![1, 2, 3],
            ..Default::default()
        };
        s.u32_values.insert("answer".into(), 42);
        s.strings.insert("name".into(), "kick".into());
        let decoded = MimicStorage::decode(&s.encode().unwrap()).unwrap();
        assert_eq!(decoded.u32_values["answer"], 42);
        assert_eq!(decoded.strings["name"], "kick");
        assert_eq!(decoded.blob, vec![1, 2, 3]);
    }
}
