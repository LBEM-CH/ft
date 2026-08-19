# rag — question answering over the ft manual

Retrieval-augmented generation for the Help function: a question in, a direct
answer out, with links back into the manual for every section used.

Today the Help dialog returns a list of occurrences and leaves the user to open
each one and work out the answer. This replaces that with one answer, grounded
in the manual, citing its sources.

Nothing outside this directory is touched. The existing "Find in manual"
literal search is unaffected and keeps working with no model present.

## Pipeline

```
ft-manual/*.html
   │  ingest.py     parse <details> sections            → chunks.json
   │  embed.py      one vector per chunk                → vectors.npy, meta.json
   │  retrieve.py   question → k best chunks              (this is the retrieval)
   │  ask.py        chunks → prompt → local model       → answer + citations
   │  eval.py       measures retrieve.py on its own
```

`retrieve.py` is separate from `ask.py` on purpose: retrieval can be measured
without a language model, and when an answer is wrong you need to know which of
the two failed.

## Setup

```bash
uv venv rag/.venv                 # Python 3.14.4, the default here
source rag/.venv/bin/activate
uv pip install numpy sentence-transformers mlx-lm
```

Verified on this machine, Python 3.14.4 / arm64:

| package | version | |
|---|---|---|
| torch | 2.13.0 | `mps` available, matmul runs on Metal |
| mlx / mlx-metal | 0.32.1 | default device `gpu` |
| mlx-lm | 0.31.3 | |
| sentence-transformers | 6.0.0 | |
| transformers | 5.15.1 | |
| numpy | 2.5.2 | |

`sentence-transformers` pulls in torch, which reaches the GPU through the
`mps` device. `mlx-lm` runs generation in-process — no server.

## Run

```bash
python3 rag/ingest.py                       # → 86 chunks
python3 rag/embed.py                        # → vectors.npy (seconds)
python3 rag/retrieve.py "how do I do CTF correction?"
python3 rag/ask.py     "how do I do CTF correction?"
```

Inspect what the model is given, without loading a model at all:

```bash
python3 rag/ask.py "how do I do CTF correction?" --show-prompt
```

## Measure

```bash
cp rag/questions.example.json rag/questions.json   # then edit / extend it
python3 rag/eval.py --show-misses
```

Reports recall at k = 1, 3, 5, 8, and the gap between top-1 scores for
questions the manual covers and questions it does not. That gap is where the
`--floor` value comes from — calibrate it, do not guess it.

Extend `questions.json` with questions students actually ask. It is the only
instrument that tells you whether a change helped.

## Swapping models

Embedding — changes the vector dimension, so re-embed:

```bash
python3 rag/embed.py --model BAAI/bge-m3
python3 rag/eval.py                        # compare recall
```

`retrieve.py` refuses to run if `chunks.json` changed since `embed.py` last
ran, because a stale index returns plausible nonsense rather than an error.

Generation — no re-embedding needed:

```bash
python3 rag/ask.py "..." --model mlx-community/Qwen2.5-32B-Instruct-4bit
```

Verify exact repository names on Hugging Face before relying on the defaults.

## Design notes

**Chunking follows the manual's own structure.** Sections are `<details>`
elements with `id` anchors, which the application already deep-links to
(`openManualAnchor`, ftwindow_mouse.cpp:789). Chunking on the same boundaries
means every chunk knows its address, so citations are free. Sections over 600
words are split at their nested `<details>`, never mid-thought; the parent
heading rides along so a fragment still says what it is about.

**No vector database.** The index is 86 vectors. Scoring is an exact dot
product against every one — microseconds. An index structure would be slower to
build than the search it replaces, and could only lose recall. This stays true
to roughly 100,000 chunks.

**Retrieve generously.** Eight of 86 chunks is ~9% of the corpus. At that ratio
query rewriting, re-ranking and agent loops solve problems you do not have —
they exist to pick 5 out of 500,000. Add them only if `eval.py` shows a failure
they would fix.

**Refusal is a feature.** A cosine ranking always returns something. For a
teaching tool, answering confidently from irrelevant sections is the failure
mode that does real harm, so there are two guards: the score floor, and the
prompt's instruction to say when the manual does not cover something.

## Files

| file | role |
|---|---|
| `ingest.py` | HTML → `chunks.json` |
| `embed.py` | `chunks.json` → `vectors.npy` + `meta.json` |
| `retrieve.py` | question → k chunks; also importable |
| `ask.py` | retrieve → prompt → mlx-lm → answer |
| `eval.py` | recall@k and score separation |
| `questions.example.json` | 35 seed questions, 5 deliberately uncovered |
