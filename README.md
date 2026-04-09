# ht-sua33-calibration

Radiometric calibration suite for the **HT-SUA33GM-T1V-C** industrial camera
(HuaTeng Vision, SmartSens CMOS global shutter, 640 × 480 px, 4 µm pixels).

Developed for atom interferometry imaging with strontium at the
**Northwestern University Kovachy Group**.

---

## What this does

Implements the Photon Transfer Curve (PTC) method to measure:

| Parameter | Symbol | Typical value |
|---|---|---|
| System gain | K | ~41 e⁻/ADU |
| Read noise | σ_read | ~10 e⁻ |
| Full-well capacity | FWC | ~10 000 e⁻ |
| Dynamic range | DR | ~60 dB |
| Peak SNR | SNR | ~40 dB |
| Quantum efficiency at 688 nm | QE_eff | ~35–40 % |

Theory and derivations are documented in `docs/camera_calibration_theory.tex`.

---

## Hardware requirements

- HT-SUA33GM-T1V-C camera connected via USB 3.0
- HuaTeng Vision Linux SDK v2.1.0.49 or later (see Installation)
- Calibration laser at **688 nm**, expanded to uniform beam
- Calibrated power sensor with known active area
- ND filters for beam attenuation

---

## Software requirements

- Linux x86_64 (Ubuntu 22.04 or later recommended)
- GCC 9+
- Python 3.8+ with `numpy`, `scipy`, `matplotlib`

---

## SDK Installation

The HuaTeng Vision Linux SDK must be installed before building.

1. Download the SDK from [www.huatengvision.com](https://www.huatengvision.com)
   or locate the unpacked tarball (e.g. `linuxSDK_V2.1.0.49`).

2. Copy the shared library to the system library path:
   ```bash
   sudo cp linuxSDK_V2.1.0.49/lib/x64/libMVSDK.so /usr/lib/
   sudo ldconfig
   ```

3. Copy the headers to the system include path:
   ```bash
   sudo cp linuxSDK_V2.1.0.49/include/*.h /usr/include/
   ```

4. Verify:
   ```bash
   ls /usr/lib/libMVSDK.so      # should exist
   ls /usr/include/CameraApi.h  # should exist
   ```

---

## Build

```bash
git clone https://github.com/hardeep2029/ht-sua33-calibration.git
cd ht-sua33-calibration
make
```

This produces three executables in the project root:
```
ptc_verify     — camera sanity check
ptc_acquire    — PTC flat-pair sweep
qe_acquire     — QE exposure sweep
```

---

## Usage

### Step 0 — Verify the camera

Run first, every session, before touching the laser:

```bash
./ptc_verify
```

With the lens cap on, then remove it when prompted. Confirms the camera
is alive, ISP is off, and the dark level is sensible.

---

### Step 1 — PTC acquisition

Set up the expanded 688 nm laser beam. Install ND filters as needed.

```bash
./ptc_acquire
```

The program will:
1. Acquire 10 dark frames (beam blocked)
2. Run a preview loop so you can dial in ND filters
3. Run 20 log-spaced flat pairs from 10 µs to 20 ms
4. Save all frames and a `metadata.txt` to `data/ptc_YYYYMMDD_HHMMSS/`

Options:
```
-n <steps>   Number of flat-pair steps (default: 20)
-d <frames>  Number of dark frames     (default: 10)
-t <us>      Minimum exposure [µs]     (default: 10.0)
-T <us>      Maximum exposure [µs]     (default: 20000.0)
```

---

### Step 2 — PTC analysis

```bash
python3 analysis/ptc_analysis.py --ptc data/ptc_YYYYMMDD_HHMMSS
```

Prints K, σ_read, FWC, DR, and SNR. Saves `ptc_plot.png` in the data directory.
Note the value of **K** for the QE step.

---

### Step 3 — QE acquisition

Remove the camera. Place the power sensor at the sensor plane. Record
`P_sensor` (W) and note `A_sensor` (m²) from the sensor datasheet.
Reinstall the camera.

```bash
./qe_acquire -K <K_from_step_2>
```

The program prompts for `P_sensor` and `A_sensor`, then runs a 30-step
exposure sweep. A live QE estimate is printed at each step.

---

### Step 4 — QE analysis

```bash
python3 analysis/ptc_analysis.py --qe data/qe_YYYYMMDD_HHMMSS
```

Or run both analyses together:

```bash
python3 analysis/ptc_analysis.py \
    --both data/ptc_YYYYMMDD_HHMMSS data/qe_YYYYMMDD_HHMMSS
```

---

## Repository layout

```
ht-sua33-calibration/
├── Makefile
├── LICENSE
├── README.md
├── .gitignore
├── include/
│   └── camera_config.h      — all tunable constants
├── src/
│   ├── camera_utils.h / .c  — camera init, config, capture
│   ├── file_utils.h  / .c   — directory creation, frame saving
│   ├── ptc_verify.c         — executable 1
│   ├── ptc_acquire.c        — executable 2
│   └── qe_acquire.c         — executable 3
└── analysis/
    └── ptc_analysis.py      — PTC fit and QE extraction
```

Raw data (`data/`) is excluded from version control via `.gitignore`.

---

## Notes on the etalon effect

The AR glass window on the sensor acts as a low-finesse etalon when
illuminated with a coherent laser, producing interference fringes across
the image. To eliminate these for the uniformity-sensitive PTC measurement,
place a **ground glass diffuser** (e.g. Thorlabs DG05-600) a few mm in
front of the sensor. The power sensor measurement must be taken on the
camera side of the diffuser.

---

## References

- Janesick, J. R. (2001). *Scientific Charge-Coupled Devices*. SPIE Press.
- Janesick, J. R. (2007). *Photon Transfer: DN → λ*. SPIE Press.
- EMVA Standard 1288, Release 4.0 (2021). European Machine Vision Association.
- Theuwissen, A. — Harvest Imaging Blog: "How to Measure Full Well Capacity"
  https://harvestimaging.com/blog/?p=1238

---

## License

MIT — see [LICENSE](LICENSE).
