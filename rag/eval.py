#!/usr/bin/env python3
"""
Measure retrieval on its own, with no language model involved.

Two numbers matter:

  recall@k   Is the section that should answer the question among the top k?
             If it is not, no model can answer correctly -- so this is the
             first thing to fix, and the only thing worth tuning the embedding
             model against.

  separation Top-1 score for questions the manual covers, versus questions it
             does not. The gap between the two is what a refusal floor is set
             from. If they overlap, no floor can separate them and refusal has
             to be handled in the prompt instead.

Question file: JSON list of {"question": str, "anchor": str or null}
An anchor of null marks a question the manual deliberately does not answer.
"""
import argparse, json, statistics, sys
from pathlib import Path

import numpy as np

from retrieve import Retriever


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--questions", default="rag/questions.json")
    ap.add_argument("--index-dir", default="rag")
    ap.add_argument("--device", default=None)
    ap.add_argument("--ks", default="1,3,5,8")
    ap.add_argument("--show-misses", action="store_true")
    args = ap.parse_args()

    qpath = Path(args.questions)
    if not qpath.exists():
        sys.exit(f"{qpath} not found - copy rag/questions.example.json and edit it")
    questions = json.loads(qpath.read_text(encoding="utf-8"))
    ks = [int(x) for x in args.ks.split(",")]
    kmax = max(ks)

    r = Retriever(args.index_dir, args.device)
    print(f"model   {r.meta['model']}  (dim {r.meta['dim']})")
    print(f"chunks  {r.meta['n_chunks']}")
    print(f"asking  {len(questions)} questions\n")

    covered_top, uncovered_top, misses = [], [], []
    hits_at = {k: 0 for k in ks}
    n_covered = 0

    for q in questions:
        s = r.scores(q["question"])
        order = np.argsort(-s)[:kmax]
        ranked = [r.chunks[int(i)] for i in order]
        top1 = float(s[order[0]])

        if not q.get("anchor"):
            uncovered_top.append(top1)
            continue

        n_covered += 1
        covered_top.append(top1)
        rank = next((i for i, c in enumerate(ranked) if c["anchor"] == q["anchor"]), None)
        for k in ks:
            if rank is not None and rank < k:
                hits_at[k] += 1
        if rank is None or rank >= min(ks[-1], kmax):
            misses.append((q["question"], q["anchor"], rank,
                           [c["anchor"] for c in ranked[:3]]))

    print("recall")
    for k in ks:
        pct = 100.0 * hits_at[k] / n_covered if n_covered else 0.0
        print(f"  @{k:<3d} {hits_at[k]:3d}/{n_covered:<3d}  {pct:5.1f}%")

    if covered_top and uncovered_top:
        cmin, cmed = min(covered_top), statistics.median(covered_top)
        umax, umed = max(uncovered_top), statistics.median(uncovered_top)
        print("\nscore separation (top-1 cosine)")
        print(f"  covered    min {cmin:.3f}   median {cmed:.3f}")
        print(f"  uncovered  max {umax:.3f}   median {umed:.3f}")
        if cmin > umax:
            print(f"  -> clean gap; a floor anywhere in ({umax:.3f}, {cmin:.3f}) "
                  f"separates them.  suggested --floor {(umax + cmin) / 2:.3f}")
        else:
            print("  -> distributions overlap; no floor separates them cleanly.")
            print("     rely on the prompt's refusal instruction, or add "
                  "more/better-targeted chunks.")
    elif not uncovered_top:
        print("\nno uncovered questions in the set - add some (\"anchor\": null)")
        print("or the refusal floor cannot be calibrated")

    if misses and args.show_misses:
        print(f"\nmisses ({len(misses)})")
        for question, want, rank, got in misses:
            where = "not in top-k" if rank is None else f"rank {rank + 1}"
            print(f"  {question[:64]}")
            print(f"    want [{want}]  {where}   top3: {got}")


if __name__ == "__main__":
    main()
