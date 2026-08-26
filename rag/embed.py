#!/usr/bin/env python3
"""
Build the vector index: chunks.json -> vectors.npy + meta.json

Run once after ingest.py, and again whenever the manual changes or the
embedding model is swapped. At 86 chunks this takes seconds.

The embedding model is a flag, so comparing candidates is a re-run rather than
a code change. Changing model changes the vector dimension, so meta.json
records which model built the index and retrieve.py refuses a mismatch.
"""
import argparse, json, time
from pathlib import Path

import numpy as np

from retrieve import DEFAULT_TASK, build_embed_text, corpus_hash, pick_device


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chunks", default="rag/chunks.json")
    ap.add_argument("--out-dir", default="rag")
    ap.add_argument("--model", default="Qwen/Qwen3-Embedding-0.6B",
                    help="any sentence-transformers model; must accept the "
                         "longest chunk without truncating (yours is ~2600 tokens)")
    ap.add_argument("--device", default=None, help="mps | cuda | cpu (default: auto)")
    ap.add_argument("--batch-size", type=int, default=8)
    ap.add_argument("--task", default=DEFAULT_TASK)
    args = ap.parse_args()

    chunks = json.loads(Path(args.chunks).read_text(encoding="utf-8"))
    texts = [build_embed_text(c) for c in chunks]
    device = pick_device(args.device)

    print(f"model   {args.model}")
    print(f"device  {device}")
    print(f"chunks  {len(texts)}")

    from sentence_transformers import SentenceTransformer
    model = SentenceTransformer(args.model, device=device)

    # Documents are embedded bare. Only the query carries the instruction
    # prefix -- see format_query() in retrieve.py.
    t0 = time.time()
    vectors = model.encode(texts, batch_size=args.batch_size,
                           normalize_embeddings=True,
                           show_progress_bar=True).astype(np.float32)
    dt = time.time() - t0

    out = Path(args.out_dir)
    np.save(out / "vectors.npy", vectors)
    (out / "meta.json").write_text(json.dumps({
        "model":          args.model,
        "device":         device,
        "task":           args.task,
        "dim":            int(vectors.shape[1]),
        "n_chunks":       int(vectors.shape[0]),
        "corpus_sha256":  corpus_hash(texts),
        "built_seconds":  round(dt, 1),
    }, indent=1), encoding="utf-8")

    print(f"dim     {vectors.shape[1]}")
    print(f"size    {vectors.nbytes / 1024:.0f} KB")
    print(f"time    {dt:.1f}s")
    print(f"wrote   {out/'vectors.npy'}  and  {out/'meta.json'}")


if __name__ == "__main__":
    main()
