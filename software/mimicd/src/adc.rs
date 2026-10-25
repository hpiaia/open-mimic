use crate::format::Layout;
use std::ffi::c_void;
use std::fs::{File, OpenOptions};
use std::io;
use std::os::fd::AsRawFd;
use std::ptr::NonNull;

const IOCTL_MAP_SIZE: usize = 0x4101;
const IOCTL_DMA_SIZE: usize = 0x4102;
const IOCTL_PORTION_SIZE: usize = 0x4103;
const PROT_READ: i32 = 1;
const MAP_PRIVATE: i32 = 2;

extern "C" {
    fn ioctl(fd: i32, request: usize, ...) -> i32;
    fn mmap(
        address: *mut c_void,
        length: usize,
        protection: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> *mut c_void;
    fn munmap(address: *mut c_void, length: usize) -> i32;
}

pub struct Adc {
    _file: File,
    mapping: NonNull<u8>,
    layout: Layout,
}

impl Adc {
    pub fn open(path: &str) -> io::Result<Self> {
        let file = OpenOptions::new().read(true).open(path)?;
        let fd = file.as_raw_fd();
        let query = |request| -> io::Result<u32> {
            // Pearl returns each size as the ioctl result, not via an output
            // pointer. This matches CADC::adcOpen and the live hardware probe.
            let value = unsafe { ioctl(fd, request) };
            if value < 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(value as u32)
        };
        let layout = Layout {
            map_bytes: query(IOCTL_MAP_SIZE)?,
            dma_bytes: query(IOCTL_DMA_SIZE)?,
            portion_bytes: query(IOCTL_PORTION_SIZE)?,
        }
        .validate()?;
        let mapped = unsafe {
            mmap(
                std::ptr::null_mut(),
                layout.map_bytes as usize,
                PROT_READ,
                MAP_PRIVATE,
                fd,
                0,
            )
        };
        if mapped as isize == -1 {
            return Err(io::Error::last_os_error());
        }
        let mapping = NonNull::new(mapped.cast::<u8>()).expect("mmap returned null");
        Ok(Self {
            _file: file,
            mapping,
            layout,
        })
    }

    pub fn layout(&self) -> Layout {
        self.layout
    }

    pub fn snapshot(&self, destination: &mut [u8]) -> io::Result<(u32, u32)> {
        if destination.len() != self.layout.dma_bytes as usize {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "wrong DMA buffer size",
            ));
        }
        // The cursor and ring can change during the copy. This is a diagnostic
        // snapshot, not an atomic or lossless stream of ADC frames.
        let cursor_ptr = unsafe {
            self.mapping
                .as_ptr()
                .add(self.layout.dma_bytes as usize)
                .cast::<u32>()
        };
        let before = unsafe { std::ptr::read_volatile(cursor_ptr) };
        for (index, byte) in destination.iter_mut().enumerate() {
            *byte = unsafe { std::ptr::read_volatile(self.mapping.as_ptr().add(index)) };
        }
        let after = unsafe { std::ptr::read_volatile(cursor_ptr) };
        Ok((before, after))
    }
}

impl Drop for Adc {
    fn drop(&mut self) {
        unsafe { munmap(self.mapping.as_ptr().cast(), self.layout.map_bytes as usize) };
    }
}
