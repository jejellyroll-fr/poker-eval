#!/usr/bin/env python3
"""
Aggregate qmc_vs_mc.csv (from examples/benchmark_qmc_vs_mc) with pandas.

Computes mean and std for equity_p1, equity_p2, tie, and time_sec
grouped by (policy, scenario, samples). Outputs CSV to stdout or file.

Usage:
  python scripts/aggregate_qmc_vs_mc.py qmc_vs_mc.csv --out agg.csv
  cat qmc_vs_mc.csv | python scripts/aggregate_qmc_vs_mc.py - --out agg.csv
"""

import sys
import argparse

try:
    import pandas as pd
except ImportError as e:
    sys.stderr.write("[ERROR] pandas is required (pip install pandas)\n")
    sys.exit(1)


def flatten_columns(df: pd.DataFrame) -> pd.DataFrame:
    df.columns = [
        ("_".join([c for c in map(str, col) if c and c != " "])).rstrip("_")
        if isinstance(col, tuple) else str(col) for col in df.columns
    ]
    return df


def aggregate(df: pd.DataFrame) -> pd.DataFrame:
    # Ensure correct dtypes
    for col in ["samples", "repeat", "batch_size"]:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    for col in ["equity_p1", "equity_p2", "tie", "time_sec"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Drop rows with NaNs in key metrics
    df = df.dropna(subset=["policy", "scenario", "samples", "equity_p1", "equity_p2", "tie", "time_sec"])

    grouped = (
        df.groupby(["policy", "scenario", "samples"], as_index=False)
          .agg(
              count=("repeat", "count"),
              equity_p1_mean=("equity_p1", "mean"), equity_p1_std=("equity_p1", "std"),
              equity_p2_mean=("equity_p2", "mean"), equity_p2_std=("equity_p2", "std"),
              tie_mean=("tie", "mean"), tie_std=("tie", "std"),
              time_sec_mean=("time_sec", "mean"), time_sec_std=("time_sec", "std"),
          )
          .sort_values(["scenario", "policy", "samples"]) 
    )

    # Fill NaN std (single-repeat groups) with 0
    for col in ["equity_p1_std", "equity_p2_std", "tie_std", "time_sec_std"]:
        if col in grouped.columns:
            grouped[col] = grouped[col].fillna(0.0)
    return grouped


def main():
    ap = argparse.ArgumentParser(description="Aggregate QMC vs MC benchmark CSV")
    ap.add_argument("input", help="Path to qmc_vs_mc.csv or '-' for stdin")
    ap.add_argument("--out", "-o", default="-", help="Output CSV path (default: stdout)")
    args = ap.parse_args()

    if args.input == "-":
        df = pd.read_csv(sys.stdin)
    else:
        df = pd.read_csv(args.input)

    agg = aggregate(df)

    if args.out == "-":
        agg.to_csv(sys.stdout, index=False)
    else:
        agg.to_csv(args.out, index=False)
        print(f"Wrote aggregated CSV to {args.out}")


if __name__ == "__main__":
    main()

