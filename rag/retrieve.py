#!/usr/bin/env python3
"""
Retrieval: question -> the k manual chunks most likely to answer it.

This is the "R" in retrieval-augmented generation, kept in its own module so it
can be measured (eval.py) without involving a language model at all.

The whole index is 83 vectors. Scoring is an exact dot product against every
one of them -- roughly 90k multiply-adds, microseconds. Nothing is approximated
and no chunk is skipped, so there is no reason for an index structure or a
vector database at this corpus size.

Both the stored vectors and the query vector are L2-normalised, so their dot
product IS the cosine similarity.
"""
import argparse, hashlib, json, sys
from pathlib import Path

import numpy as np

# Prepended to the query, not to the documents. Instruction-aware embedding
# models (Qwen3-Embedding, gte-Qwen2, e5-mistral) expect this asymmetry;
# formatting both sides the same way silently costs accuracy.
DEFAULT_TASK = ("Given a question about the Fourier Analyzer software and about "
                "Fourier-space image processing, retrieve the manual sections "
                "that answer it")


def build_embed_text(chunk):
    """
    The exact string that represents a chunk in vector space.

    Shared by embed.py and query time so the two can never drift. The heading
    path rides along with the body: a fragment split out of a long section is
    otherwise anonymous prose, and "CTF SIM" means little without "Panel 2".
    """
    parts = [chunk["page_title"]] + list(chunk.get("trail", [])) + [chunk["title"]]
    heading, seen = [], set()
    for p in parts:
        if p and p not in seen:
            seen.add(p)
            heading.append(p)
    return " > ".join(heading) + "\n" + chunk["text"]


def corpus_hash(texts):
    h = hashlib.sha256()
    for t in texts:
        h.update(t.encode("utf-8"))
        h.update(b"\0")
    return h.hexdigest()


def format_query(question, task=DEFAULT_TASK):
    return f"Instruct: {task}\nQuery: {question}"


def pick_device(requested=None):
    if requested:
        return requested
    try:
        import torch
        if torch.backends.mps.is_available():
            return "mps"
        if torch.cuda.is_available():
            return "cuda"
    except Exception:
        pass
    return "cpu"


class Retriever:
    """Loads the index; loads the embedding model only when first queried."""

    def __init__(self, index_dir="rag", device=None):
        d = Path(index_dir)
        self.chunks = json.loads((d / "chunks.json").read_text(encoding="utf-8"))
        self.meta = json.loads((d / "meta.json").read_text(encoding="utf-8"))
        self.vectors = np.load(d / "vectors.npy")

        # A stale index paired with a fresh model returns plausible nonsense
        # rather than an error, so refuse instead of guessing.
        live = corpus_hash([build_embed_text(c) for c in self.chunks])
        if live != self.meta["corpus_sha256"]:
            sys.exit("index is stale: chunks.json changed since embed.py ran.\n"
                     "  re-run:  python3 rag/embed.py")
        if self.vectors.shape[0] != len(self.chunks):
            sys.exit(f"index is inconsistent: {self.vectors.shape[0]} vectors "
                     f"for {len(self.chunks)} chunks")

        self.device = pick_device(device)
        self._model = None

    @property
    def model(self):
        if self._model is None:
            from sentence_transformers import SentenceTransformer
            self._model = SentenceTransformer(self.meta["model"], device=self.device)
        return self._model

    def scores(self, question):
        v = self.model.encode([format_query(question, self.meta["task"])],
                              normalize_embeddings=True)[0]
        return self.vectors @ v            # (N,) cosine similarity

    def retrieve(self, question, k=12, floor=None):
        """
        The k best-matching chunks, best first, as (chunk, score) pairs.

        `floor` guards the case a cosine ranking cannot express: a question the
        manual does not cover still produces a ranking, just a poor one. Below
        the floor nothing is returned, and the caller should say the manual does
        not cover it rather than answer from the least-irrelevant sections.
        Calibrate the value with eval.py; do not guess it.
        """
        # A k of 0 would slice to nothing and read as "the manual does not
        # cover this"; a negative one would slice to nearly the whole corpus.
        # Neither is ever meant, so clamp rather than trust the caller.
        k = max(1, int(k))
        s = self.scores(question)
        order = np.argsort(-s)[:k]
        if floor is not None and (len(order) == 0 or s[order[0]] < floor):
            return []
        return [(self.chunks[int(i)], float(s[int(i)])) for i in order]


def main():
    ap = argparse.ArgumentParser(description="Retrieve manual chunks for a question.")
    ap.add_argument("question")
    ap.add_argument("-k", type=int, default=12)
    ap.add_argument("--floor", type=float, default=None)
    ap.add_argument("--index-dir", default="rag")
    ap.add_argument("--device", default=None)
    ap.add_argument("--full", action="store_true", help="print full chunk text")
    args = ap.parse_args()

    hits = Retriever(args.index_dir, args.device).retrieve(args.question, args.k, args.floor)
    if not hits:
        print("no chunk scored above the floor - treat as not covered by the manual")
        return
    for rank, (c, score) in enumerate(hits, 1):
        print(f"{rank:2d}. {score:.4f}  [{c['anchor'] or c['page']}]  {c['title'][:64]}")
        print(f"     {c['url']}")
        print(f"     {c['text'] if args.full else c['text'][:160] + ' ...'}")
        print()


if __name__ == "__main__":
    main()
