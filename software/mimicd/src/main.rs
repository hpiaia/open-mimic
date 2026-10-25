#[cfg(target_os = "linux")]
mod adc;
#[cfg(any(target_os = "linux", test))]
mod format;

#[cfg(target_os = "linux")]
fn run() -> std::io::Result<()> {
    use std::fs::OpenOptions;
    use std::io::{self, BufWriter, Write};
    use std::time::{Duration, Instant};

    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 && args.len() != 4 {
        eprintln!("Usage: mimicd probe | mimicd capture OUTPUT.omadc SECONDS");
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid arguments",
        ));
    }
    if !(args[1] == "probe" && args.len() == 2 || args[1] == "capture" && args.len() == 4) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid command",
        ));
    }
    let seconds = if args[1] == "capture" {
        let seconds: u64 = args[3].parse().map_err(|_| {
            io::Error::new(io::ErrorKind::InvalidInput, "SECONDS must be an integer")
        })?;
        if !(1..=60).contains(&seconds) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "SECONDS must be 1..60",
            ));
        }
        seconds
    } else {
        0
    };
    let devices = [
        adc::Adc::open("/dev/ads7953.0")?,
        adc::Adc::open("/dev/ads7953.1")?,
    ];
    let layouts = [devices[0].layout(), devices[1].layout()];
    for (index, layout) in layouts.iter().enumerate() {
        eprintln!(
            "ADC{index}: map={} DMA={} portion={}",
            layout.map_bytes, layout.dma_bytes, layout.portion_bytes
        );
    }
    if args[1] == "probe" && args.len() == 2 {
        return Ok(());
    }
    let file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&args[2])?;
    let mut output = BufWriter::new(file);
    const POLL_US: u32 = 2_000;
    format::write_header(&mut output, layouts, POLL_US)?;
    let mut buffers = [
        vec![0; layouts[0].dma_bytes as usize],
        vec![0; layouts[1].dma_bytes as usize],
    ];
    let mut captured = 0u64;
    let mut cursor_moved = 0u64;
    let started = Instant::now();
    while started.elapsed() < Duration::from_secs(seconds) {
        let first = devices[0].snapshot(&mut buffers[0])?;
        let second = devices[1].snapshot(&mut buffers[1])?;
        if first.0 != first.1 || second.0 != second.1 {
            cursor_moved += 1;
        }
        format::write_record(
            &mut output,
            started.elapsed().as_nanos() as u64,
            [first, second],
            [&buffers[0], &buffers[1]],
        )?;
        captured += 1;
        std::thread::sleep(Duration::from_micros(POLL_US as u64));
    }
    output.flush()?;
    eprintln!("Captured {captured} snapshots; cursor moved during {cursor_moved}");
    Ok(())
}

#[cfg(not(target_os = "linux"))]
fn run() -> std::io::Result<()> {
    Err(std::io::Error::new(
        std::io::ErrorKind::Unsupported,
        "ADC capture requires Linux",
    ))
}

fn main() {
    if let Err(error) = run() {
        eprintln!("mimicd: {error}");
        std::process::exit(1);
    }
}
