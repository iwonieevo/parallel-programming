import matplotlib.pyplot as plt
import pandas as pd
from pathlib import Path


def plot_sa_progress(csv_path: Path, output_path: Path) -> None:
    df = pd.read_csv(csv_path)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.set_title("SA Convergence")
    ax.set_xlabel("Step")
    ax.set_ylabel("Best Cost")
    ax.plot(df["step"], df["best_cost"], linewidth=0.8)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def plot_best_tour(csv_path: Path, output_path: Path) -> None:
    df = pd.read_csv(csv_path)

    fig, ax = plt.subplots(figsize=(16, 16))
    ax.set_title("Best Tour")
    ax.set_aspect("equal")
    ax.plot(df["x"], df["y"], linewidth=0.5, color="steelblue", zorder=1)
    ax.scatter(df["x"], df["y"], s=3, color="red", zorder=2)
    ax.scatter(df["x"].iloc[0], df["y"].iloc[0], s=6, color="lime", zorder=3, label="Start")
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    results_path = Path(__file__).resolve().parent / "results"

    if not results_path.exists():
        raise FileNotFoundError(f"Directory not found: {results_path}")
    if not results_path.is_dir():
        raise NotADirectoryError(f"Not a directory: {results_path}")

    for run_dir in sorted(results_path.iterdir()):
        if not run_dir.is_dir():
            continue

        print(f"Processing {run_dir.name}...")

        progress_csv = run_dir / "sa_progress.csv"
        tour_csv     = run_dir / "best_tour.csv"

        if progress_csv.exists():
            plot_sa_progress(progress_csv, run_dir / "sa_progress.png")
            print(f"  saved sa_progress.png")
        else:
            print(f"  sa_progress.csv not found, skipping")

        if tour_csv.exists():
            plot_best_tour(tour_csv, run_dir / "best_tour.png")
            print(f"  saved best_tour.png")
        else:
            print(f"  best_tour.csv not found, skipping")