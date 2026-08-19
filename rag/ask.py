#!/usr/bin/env python3
"""
Answer a question from the manual: retrieve -> augment -> generate.

Retrieval (retrieve.py) picks the sections; this module pastes them into the
prompt tagged with their manual anchors, and asks a local model to answer from
those sections only. The anchors come back with the answer, so every claim can
be followed to the page it came from -- the same anchors the application
already deep-links to via openManualAnchor (ftwindow_mouse.cpp:789).

Generation runs in-process through mlx-lm on Apple Silicon. No server.
"""
import argparse, sys

from retrieve import Retriever

SYSTEM = (
    "You answer questions about the Fourier Analyzer, a program for exploring "
    "images and their Fourier transforms.\n"
    "Answer ONLY from the manual sections provided. Do not use outside knowledge "
    "and do not guess.\n"
    "Cite the [anchor] of every section you draw on, inline, as you use it.\n"
    "If the sections do not contain the answer, say plainly that the manual does "
    "not cover it. Saying so is a correct answer; inventing one is not.\n"
    "Be concise and concrete. Prefer the exact tool and control names the manual uses."
)


def build_prompt(question, hits):
    blocks = []
    for c, score in hits:
        tag = c["anchor"] or c["page"]
        blocks.append(f"[{tag}] {c['title']}\n{c['text']}")
    return (
        "Manual sections:\n\n" + "\n\n---\n\n".join(blocks) +
        f"\n\n---\n\nQuestion: {question}"
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("question")
    ap.add_argument("-k", type=int, default=8, help="sections to retrieve")
    ap.add_argument("--floor", type=float, default=None,
                    help="minimum top-1 cosine; below it, refuse instead of "
                         "answering (calibrate with eval.py)")
    ap.add_argument("--model", default="mlx-community/Qwen3-8B-4bit",
                    help="any mlx-lm model on Hugging Face")
    ap.add_argument("--max-tokens", type=int, default=700)
    ap.add_argument("--index-dir", default="rag")
    ap.add_argument("--device", default=None)
    ap.add_argument("--show-prompt", action="store_true",
                    help="print the assembled prompt and exit without generating")
    args = ap.parse_args()

    hits = Retriever(args.index_dir, args.device).retrieve(args.question, args.k, args.floor)
    if not hits:
        print("The manual does not appear to cover this question "
              f"(no section scored above {args.floor}).")
        return

    prompt = build_prompt(args.question, hits)
    if args.show_prompt:
        print(prompt)
        print(f"\n[{len(prompt.split())} words of context from {len(hits)} sections]")
        return

    try:
        from mlx_lm import load, generate
    except ImportError:
        sys.exit("mlx-lm is not installed.  pip install mlx-lm\n"
                 "(or use --show-prompt to inspect retrieval without a model)")

    model, tokenizer = load(args.model)
    text = tokenizer.apply_chat_template(
        [{"role": "system", "content": SYSTEM},
         {"role": "user", "content": prompt}],
        add_generation_prompt=True, tokenize=False)

    print(f"[{len(hits)} sections, ~{len(prompt.split())} words of context]\n")
    answer = generate(model, tokenizer, prompt=text,
                      max_tokens=args.max_tokens, verbose=False)
    print(answer.strip())

    print("\nSources:")
    for c, score in hits:
        print(f"  {score:.3f}  [{c['anchor'] or c['page']}]  {c['url']}")


if __name__ == "__main__":
    main()
