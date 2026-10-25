use std::io::{self, Write};

pub const MAGIC: &[u8; 8] = b"OMADC002";
pub const MAX_DMA_BYTES: usize = 1024 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Layout {
    pub map_bytes: u32,
    pub dma_bytes: u32,
    pub portion_bytes: u32,
}

impl Layout {
    pub fn validate(self) -> io::Result<Self> {
        let map = self.map_bytes as usize;
        let dma = self.dma_bytes as usize;
        let portion = self.portion_bytes as usize;
        if dma == 0
            || dma > MAX_DMA_BYTES
            || dma & 31 != 0
            || map < dma + 4
            || map > MAX_DMA_BYTES * 2
            || portion == 0
            || portion > dma
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("unexpected ADC layout: {self:?}"),
            ));
        }
        Ok(self)
    }
}

pub fn write_header(mut output: impl Write, layouts: [Layout; 2], poll_us: u32) -> io::Result<()> {
    output.write_all(MAGIC)?;
    output.write_all(&poll_us.to_le_bytes())?;
    for layout in layouts {
        output.write_all(&layout.map_bytes.to_le_bytes())?;
        output.write_all(&layout.dma_bytes.to_le_bytes())?;
        output.write_all(&layout.portion_bytes.to_le_bytes())?;
    }
    Ok(())
}

pub fn write_record(
    mut output: impl Write,
    elapsed_ns: u64,
    cursors: [(u32, u32); 2],
    buffers: [&[u8]; 2],
) -> io::Result<()> {
    output.write_all(&elapsed_ns.to_le_bytes())?;
    for (before, after) in cursors {
        output.write_all(&before.to_le_bytes())?;
        output.write_all(&after.to_le_bytes())?;
    }
    for buffer in buffers {
        output.write_all(buffer)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_observed_layout_and_rejects_bad_mapping() {
        let observed = Layout {
            map_bytes: 8192,
            dma_bytes: 4096,
            portion_bytes: 32,
        };
        assert!(observed.validate().is_ok());
        assert!(Layout {
            map_bytes: 4096,
            ..observed
        }
        .validate()
        .is_err());
        assert!(Layout {
            dma_bytes: 0,
            ..observed
        }
        .validate()
        .is_err());
    }

    #[test]
    fn writes_documented_little_endian_format() {
        let layout = Layout {
            map_bytes: 8192,
            dma_bytes: 4096,
            portion_bytes: 32,
        };
        let mut bytes = Vec::new();
        write_header(&mut bytes, [layout; 2], 2000).unwrap();
        write_record(&mut bytes, 42, [(7, 8), (9, 10)], [&[1, 2], &[3, 4]]).unwrap();
        assert_eq!(&bytes[..8], MAGIC);
        assert_eq!(u32::from_le_bytes(bytes[8..12].try_into().unwrap()), 2000);
        assert_eq!(u64::from_le_bytes(bytes[36..44].try_into().unwrap()), 42);
        assert_eq!(&bytes[60..], &[1, 2, 3, 4]);
    }
}
