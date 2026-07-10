use anyhow::{Result, bail};

pub const BLOCK_BYTES: usize = 1024;
const PAYLOAD_BITS: usize = (BLOCK_BYTES - 8) * 8;

fn sign_extend(value: u32, bits: u8) -> i32 {
    ((value << (32 - bits)) as i32) >> (32 - bits)
}

fn signed_bits(value: i32) -> Result<u8> {
    for bits in 1..=24 {
        let limit = 1i32 << (bits - 1);
        if (-limit..limit).contains(&value) {
            return Ok(bits);
        }
    }
    bail!("24-bit delta overflow: {value}")
}

pub fn encode_block(samples: &[i32], width: u8) -> Result<[u8; BLOCK_BYTES]> {
    if !(1..=24).contains(&width) {
        bail!("codec width must be 1..24");
    }
    if samples.len() > PAYLOAD_BITS / width as usize {
        bail!("too many samples for block");
    }
    let mut deltas = Vec::with_capacity(samples.len());
    let mut previous = 0i32;
    let mut needed = 1;
    for &sample in samples {
        let delta = sample
            .checked_sub(previous)
            .ok_or_else(|| anyhow::anyhow!("delta overflow"))?;
        needed = needed.max(signed_bits(delta)?);
        deltas.push(delta);
        previous = sample;
    }
    if width < needed {
        bail!("width {width} cannot hold {needed}-bit deltas");
    }
    let mut words = [0u32; 254];
    let mut bitpos = 0usize;
    let mask = (1u32 << width) - 1;
    for delta in deltas {
        let value = delta as u32 & mask;
        let word = bitpos / 32;
        let offset = bitpos % 32;
        let remaining = 32 - offset;
        if width as usize <= remaining {
            words[word] |= value << (remaining - width as usize);
        } else {
            let low_bits = width as usize - remaining;
            words[word] |= value >> low_bits;
            words[word + 1] |= (value & ((1u32 << low_bits) - 1)) << (32 - low_bits);
        }
        bitpos += width as usize;
    }
    let mut out = [0u8; BLOCK_BYTES];
    out[0..4].copy_from_slice(&(width as u32).to_le_bytes());
    out[4..8].copy_from_slice(&(samples.len() as u32).to_le_bytes());
    for (index, word) in words.iter().enumerate() {
        out[8 + index * 4..12 + index * 4].copy_from_slice(&word.to_le_bytes());
    }
    Ok(out)
}

pub fn decode_block(block: &[u8]) -> Result<Vec<i32>> {
    if block.len() != BLOCK_BYTES {
        bail!("block must be 1024 bytes");
    }
    let header = u32::from_le_bytes(block[0..4].try_into().unwrap());
    let width = (header & 0xff) as u8;
    let count = u32::from_le_bytes(block[4..8].try_into().unwrap()) as usize;
    if !(1..=24).contains(&width) || count > PAYLOAD_BITS / width as usize {
        bail!("invalid block header");
    }
    let mut words = [0u32; 254];
    for (i, w) in words.iter_mut().enumerate() {
        *w = u32::from_le_bytes(block[8 + i * 4..12 + i * 4].try_into().unwrap());
    }
    let mut predictor = sign_extend(header >> 8, 24);
    let mut out = Vec::with_capacity(count);
    let mut bitpos = 0usize;
    let mask = (1u32 << width) - 1;
    for _ in 0..count {
        let word = bitpos / 32;
        let offset = bitpos % 32;
        let remaining = 32 - offset;
        let value = if width as usize <= remaining {
            (words[word] >> (remaining - width as usize)) & mask
        } else {
            let low_bits = width as usize - remaining;
            ((words[word] & ((1u32 << remaining) - 1)) << low_bits)
                | (words[word + 1] >> (32 - low_bits))
        };
        predictor = predictor
            .checked_add(sign_extend(value, width))
            .ok_or_else(|| anyhow::anyhow!("predictor overflow"))?;
        out.push(predictor);
        bitpos += width as usize;
    }
    Ok(out)
}

pub fn encode(samples: &[i32], width: u8) -> Result<Vec<u8>> {
    let capacity = PAYLOAD_BITS / width as usize;
    let mut out = Vec::new();
    for chunk in samples.chunks(capacity) {
        out.extend(encode_block(chunk, width)?);
    }
    Ok(out)
}
pub fn decode(data: &[u8]) -> Result<Vec<i32>> {
    if !data.len().is_multiple_of(BLOCK_BYTES) {
        bail!("data is not block aligned");
    }
    let mut out = Vec::new();
    for block in data.chunks(BLOCK_BYTES) {
        out.extend(decode_block(block)?);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_round_trip() {
        let x: Vec<i32> = (0..10_000)
            .map(|i| ((i as f64 * 0.03).sin() * 0.3 * (1u32 << 23) as f64).round() as i32)
            .collect();
        let packed = encode(&x, 24).unwrap();
        assert_eq!(decode(&packed).unwrap(), x);
    }
}
