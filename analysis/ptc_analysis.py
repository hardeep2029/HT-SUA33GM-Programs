#!/usr/bin/env python3
"""
ptc_analysis.py
PTC fit and QE extraction for the HT-SUA33GM-T1V-C camera.

Usage:
    python3 ptc_analysis.py --ptc  <data/ptc_YYYYMMDD_HHMMSS>
    python3 ptc_analysis.py --qe   <data/qe_YYYYMMDD_HHMMSS>
    python3 ptc_analysis.py --both <data/ptc_...> <data/qe_...>

Outputs:
    - Printed calibration table
    - PTC plot saved as ptc_plot.png in the PTC data directory
    - QE linearity plot saved as qe_plot.png in the QE data directory

Dependencies:
    numpy, scipy, matplotlib
    Install with: pip install numpy scipy matplotlib
"""

import argparse
import os
import sys
import numpy as np
from scipy.stats import linregress
import matplotlib.pyplot as plt

# ------------------------------------------------------------------ #
# Sensor constants (must match camera_config.h)
# ------------------------------------------------------------------ #
CAM_WIDTH   = 640
CAM_HEIGHT  = 480
CAM_NPIX    = CAM_WIDTH * CAM_HEIGHT
ADU_MAX     = 255

ROI_X0, ROI_Y0 = 220, 140
ROI_W,  ROI_H  = 200, 200

H_JS     = 6.62607e-34   # Planck constant [J.s]
C_MS     = 2.99792e8     # speed of light  [m/s]
LAMBDA_M = 688e-9        # calibration wavelength [m]
A_PX     = 16e-12        # pixel area (4 um)^2 [m^2]


# ------------------------------------------------------------------ #
# Utility functions
# ------------------------------------------------------------------ #

def load_frame(path: str) -> np.ndarray:
    """Load a raw 8-bit binary frame as a (H, W) uint8 array."""
    data = np.fromfile(path, dtype=np.uint8)
    if data.size != CAM_NPIX:
        raise ValueError(
            f"Expected {CAM_NPIX} bytes, got {data.size} in {path}")
    return data.reshape(CAM_HEIGHT, CAM_WIDTH)


def roi_mean(frame: np.ndarray) -> float:
    """Mean pixel value over the central ROI."""
    return float(frame[ROI_Y0:ROI_Y0+ROI_H,
                       ROI_X0:ROI_X0+ROI_W].mean())


def read_metadata(directory: str) -> dict:
    """
    Parse metadata.txt in directory into a dict.
    Lines starting with '#' are skipped.
    Format: key = value
    """
    meta = {}
    path = os.path.join(directory, "metadata.txt")
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                key, _, value = line.partition('=')
                meta[key.strip()] = value.strip()
    return meta


def parse_exposures(meta: dict) -> np.ndarray:
    """Parse the comma-separated exposures_us field into a float array."""
    raw = meta.get("exposures_us", "")
    return np.array([float(x) for x in raw.split(",")])


# ------------------------------------------------------------------ #
# PTC analysis
# ------------------------------------------------------------------ #

def run_ptc(ptc_dir: str) -> dict:
    """
    Load PTC data, fit the shot-noise regime, and return a dict of
    calibration parameters.
    """
    print(f"\n{'='*50}")
    print(f" PTC Analysis: {ptc_dir}")
    print(f"{'='*50}\n")

    meta        = read_metadata(ptc_dir)
    n_dark      = int(meta.get("n_dark",   10))
    n_steps     = int(meta.get("n_steps",  20))
    exposures   = parse_exposures(meta)

    # --- Load dark frames and compute bias map ---
    dark_stack = np.zeros((n_dark, CAM_HEIGHT, CAM_WIDTH), dtype=float)
    for i in range(n_dark):
        path = os.path.join(ptc_dir, f"dark_{i:02d}.bin")
        dark_stack[i] = load_frame(path).astype(float)
    dark_map = dark_stack.mean(axis=0)
    dark_roi_mean = float(dark_map[ROI_Y0:ROI_Y0+ROI_H,
                                   ROI_X0:ROI_X0+ROI_W].mean())
    print(f"  Dark bias (ROI mean): {dark_roi_mean:.2f} ADU")

    # --- Compute (mean, variance) for each flat pair ---
    means     = np.zeros(n_steps)
    variances = np.zeros(n_steps)

    for k in range(n_steps):
        path_A = os.path.join(ptc_dir, f"flat_{k:02d}_A.bin")
        path_B = os.path.join(ptc_dir, f"flat_{k:02d}_B.bin")

        if not os.path.exists(path_A) or not os.path.exists(path_B):
            print(f"  [WARN] Missing flat pair at step {k}, skipping.")
            means[k] = np.nan
            variances[k] = np.nan
            continue

        A = load_frame(path_A).astype(float) - dark_map
        B = load_frame(path_B).astype(float) - dark_map

        roi_A = A[ROI_Y0:ROI_Y0+ROI_H, ROI_X0:ROI_X0+ROI_W]
        roi_B = B[ROI_Y0:ROI_Y0+ROI_H, ROI_X0:ROI_X0+ROI_W]

        # Mean signal: average of both frames
        means[k] = 0.5 * (roi_A.mean() + roi_B.mean())

        # Variance: half the variance of the difference (cancels FPN)
        diff = roi_A - roi_B
        variances[k] = 0.5 * diff.var()

    # Remove NaN entries
    valid = np.isfinite(means) & np.isfinite(variances)
    means     = means[valid]
    variances = variances[valid]

    # --- Identify the shot-noise (linear) regime ---
    # Exclude read-noise plateau (low signal) and saturation rolloff
    mask = (means > 0.05 * ADU_MAX) & (means < 0.80 * ADU_MAX)

    if mask.sum() < 3:
        print("  [ERROR] Fewer than 3 points in shot-noise regime. "
              "Check exposure range.")
        sys.exit(1)

    slope, intercept, r, p, se = linregress(means[mask], variances[mask])

    # --- Extract calibration parameters ---
    K          = 1.0 / slope                  # gain [e-/ADU]
    sigma_read = K * np.sqrt(max(intercept, 0.0))  # read noise [e-]

    # ADU_sat: mean signal at variance peak
    adc_sat    = means[np.argmax(variances)]
    FWC        = adc_sat * K                  # full-well capacity [e-]
    DR         = FWC / sigma_read
    DR_dB      = 20.0 * np.log10(DR)
    SNR_peak   = np.sqrt(FWC)
    SNR_dB     = 20.0 * np.log10(SNR_peak)

    # --- Print results ---
    print(f"\n  {'Parameter':<30}  {'Value':>15}  {'Datasheet':>12}")
    print(f"  {'-'*30}  {'-'*15}  {'-'*12}")
    print(f"  {'System gain K':<30}  {K:>12.2f} e-/ADU  {'~41':>12}")
    print(f"  {'Read noise sigma_read':<30}  {sigma_read:>12.1f} e-  {'~10':>12}")
    print(f"  {'ADU_sat':<30}  {adc_sat:>13.1f}  {'~245':>12}")
    print(f"  {'FWC':<30}  {FWC:>10.0f} e-  {'~10000':>12}")
    print(f"  {'Dynamic range':<30}  {DR_dB:>11.1f} dB  {'60 dB':>12}")
    print(f"  {'Peak SNR':<30}  {SNR_dB:>11.1f} dB  {'40 dB':>12}")
    print(f"  {'PTC fit R^2':<30}  {r**2:>15.4f}  {'':>12}")

    # --- Plot ---
    fig, ax = plt.subplots(figsize=(7, 5))

    ax.loglog(means, variances, 'o', color='steelblue',
              markersize=5, label='Measured data')

    # Fitted line over the linear regime
    mu_fit = np.linspace(means[mask].min(), means[mask].max(), 200)
    ax.loglog(mu_fit, slope * mu_fit + intercept, '-',
              color='darkorange', linewidth=2,
              label=f'Fit (K = {K:.2f} e⁻/ADU)')

    ax.axvline(adc_sat, color='crimson', linestyle='--',
               linewidth=1.2, label=f'ADU_sat = {adc_sat:.0f}')

    ax.set_xlabel('Mean signal $\\bar{\\mu}$ (ADU)')
    ax.set_ylabel('Variance $\\hat{\\sigma}^2$ (ADU²)')
    ax.set_title('Photon Transfer Curve — HT-SUA33GM-T1V-C')
    ax.legend(framealpha=0.9)
    ax.grid(True, which='both', alpha=0.3)
    plt.tight_layout()

    plot_path = os.path.join(ptc_dir, "ptc_plot.png")
    plt.savefig(plot_path, dpi=150)
    print(f"\n  PTC plot saved: {plot_path}")
    plt.close()

    return {
        "K":          K,
        "sigma_read": sigma_read,
        "ADU_sat":    adc_sat,
        "FWC":        FWC,
        "DR_dB":      DR_dB,
        "SNR_dB":     SNR_dB,
    }


# ------------------------------------------------------------------ #
# QE analysis
# ------------------------------------------------------------------ #

def run_qe(qe_dir: str, K_override: float = None) -> None:
    """
    Load QE sweep data and compute QE_eff at 688 nm.
    K_override: if provided, use this gain instead of the metadata value.
    """
    print(f"\n{'='*50}")
    print(f" QE Analysis: {qe_dir}")
    print(f"{'='*50}\n")

    meta      = read_metadata(qe_dir)
    n_dark    = int(meta.get("n_dark",  10))
    n_steps   = int(meta.get("n_steps", 30))
    exposures = parse_exposures(meta)

    # Physical parameters from metadata
    P_sensor  = float(meta["P_sensor_W"])
    A_sensor  = float(meta["A_sensor_m2"])
    K = float(meta.get("K_gain_e_per_ADU", -1))
    if K_override is not None:
        K = K_override
    if K <= 0:
        print("  [ERROR] K not found in metadata and not provided "
              "via --K flag. Run PTC analysis first.")
        sys.exit(1)

    irradiance    = P_sensor / A_sensor
    photon_energy = H_JS * C_MS / LAMBDA_M

    print(f"  P_sensor      = {P_sensor:.4e} W")
    print(f"  A_sensor      = {A_sensor:.4e} m^2")
    print(f"  Irradiance    = {irradiance:.4e} W/m^2")
    print(f"  K (gain)      = {K:.3f} e-/ADU")
    print(f"  Photon energy = {photon_energy:.4e} J  "
          f"(lambda = {LAMBDA_M*1e9:.0f} nm)")

    # --- Load dark map ---
    dark_stack = np.zeros((n_dark, CAM_HEIGHT, CAM_WIDTH), dtype=float)
    for i in range(n_dark):
        path = os.path.join(qe_dir, f"dark_{i:02d}.bin")
        dark_stack[i] = load_frame(path).astype(float)
    dark_map = dark_stack.mean(axis=0)

    # --- Load sweep frames and compute QE at each step ---
    mus   = np.zeros(n_steps)
    qes   = np.full(n_steps, np.nan)
    t_arr = exposures * 1e-6    # convert us to s

    for k in range(n_steps):
        path = os.path.join(qe_dir, f"flat_{k:02d}.bin")
        if not os.path.exists(path):
            print(f"  [WARN] Missing flat at step {k}, skipping.")
            continue

        frame = load_frame(path).astype(float) - dark_map
        roi   = frame[ROI_Y0:ROI_Y0+ROI_H, ROI_X0:ROI_X0+ROI_W]
        mu    = float(roi.mean())
        mus[k] = mu

        # QE_eff = (mu * A_sensor * K * h * c)
        #          / (P_sensor * lambda * A_px * t)
        if mu > 5.0 and t_arr[k] > 0.0:
            num = mu * A_sensor * K * H_JS * C_MS
            den = P_sensor * LAMBDA_M * A_PX * t_arr[k]
            qes[k] = num / den

    # Use only the linear regime (before saturation) for final estimate
    linear = (mus > 5.0) & (mus < 0.80 * ADU_MAX) & np.isfinite(qes)

    if linear.sum() == 0:
        print("  [ERROR] No valid data points in linear regime.")
        sys.exit(1)

    qe_mean   = float(np.nanmean(qes[linear]))
    qe_std    = float(np.nanstd(qes[linear]))

    print(f"\n  QE_eff({LAMBDA_M*1e9:.0f} nm) = "
          f"{qe_mean*100:.1f} ± {qe_std*100:.1f} %  "
          f"(n = {linear.sum()} points)")
    print(f"  Datasheet estimate: ~35-40 % at 688 nm")

    # --- Linearity check: mu vs t in the linear regime ---
    t_lin  = t_arr[linear]
    mu_lin = mus[linear]
    slope_lin, intercept_lin, r_lin, _, _ = linregress(t_lin, mu_lin)
    print(f"\n  Linearity check (mu vs t):  R^2 = {r_lin**2:.5f}")
    if r_lin**2 > 0.999:
        print("  [PASS] Response is linear.")
    else:
        print("  [WARN] R^2 below 0.999 — check for nonlinearity "
              "or beam instability.")

    # --- Plot ---
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    # Left: mu vs exposure time
    ax1.plot(t_arr[linear]*1e3, mu_lin, 'o', color='steelblue',
             markersize=5, label='Data (linear regime)')
    t_plot = np.linspace(0, t_lin.max(), 200)
    ax1.plot(t_plot*1e3, slope_lin*t_plot + intercept_lin, '-',
             color='darkorange', linewidth=2,
             label=f'Fit  R²={r_lin**2:.4f}')
    ax1.set_xlabel('Exposure time (ms)')
    ax1.set_ylabel('Mean signal $\\bar{\\mu}$ (ADU)')
    ax1.set_title('Linearity check')
    ax1.legend(framealpha=0.9)
    ax1.grid(alpha=0.3)

    # Right: QE vs exposure
    qe_plot = qes[linear] * 100.0
    ax2.plot(t_arr[linear]*1e3, qe_plot, 'o', color='steelblue',
             markersize=5)
    ax2.axhline(qe_mean*100, color='darkorange', linewidth=2,
                label=f'Mean = {qe_mean*100:.1f}%')
    ax2.fill_between(
        [t_arr[linear].min()*1e3, t_arr[linear].max()*1e3],
        [(qe_mean - qe_std)*100, (qe_mean - qe_std)*100],
        [(qe_mean + qe_std)*100, (qe_mean + qe_std)*100],
        alpha=0.2, color='darkorange', label=f'±1σ = {qe_std*100:.1f}%')
    ax2.set_xlabel('Exposure time (ms)')
    ax2.set_ylabel('QE$_{eff}$ (%)')
    ax2.set_title(f'QE$_{{eff}}$ at {LAMBDA_M*1e9:.0f} nm')
    ax2.legend(framealpha=0.9)
    ax2.grid(alpha=0.3)

    plt.suptitle('HT-SUA33GM-T1V-C — QE Analysis', fontsize=13)
    plt.tight_layout()

    plot_path = os.path.join(qe_dir, "qe_plot.png")
    plt.savefig(plot_path, dpi=150)
    print(f"\n  QE plot saved: {plot_path}")
    plt.close()


# ------------------------------------------------------------------ #
# Entry point
# ------------------------------------------------------------------ #

def main():
    parser = argparse.ArgumentParser(
        description="PTC fit and QE extraction for HT-SUA33GM-T1V-C.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)

    parser.add_argument("--ptc",  metavar="DIR",
                        help="PTC data directory")
    parser.add_argument("--qe",   metavar="DIR",
                        help="QE data directory")
    parser.add_argument("--both", metavar="DIR", nargs=2,
                        help="Run both: --both <ptc_dir> <qe_dir>")
    parser.add_argument("--K",    metavar="GAIN", type=float,
                        default=None,
                        help="Override gain K [e-/ADU] for QE analysis")

    args = parser.parse_args()

    if args.both:
        results = run_ptc(args.both[0])
        run_qe(args.both[1], K_override=results["K"])
    elif args.ptc:
        run_ptc(args.ptc)
    elif args.qe:
        run_qe(args.qe, K_override=args.K)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
