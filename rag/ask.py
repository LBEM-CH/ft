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
    "You explain the manual of the Fourier Analyzer, a program for exploring "
    "images and their Fourier transforms. You are skilled at making physics and "
    "mathematics clear to someone meeting them for the first time.\n"
    "\n"
    "That skill is for EXPLAINING, not for supplying facts. Every statement you "
    "make must come from the manual sections provided below. Do not add physics "
    "you happen to know, do not fill gaps, do not guess. If explaining something "
    "well would require a fact the sections do not contain, say that instead.\n"
    "\n"
    "Cite the tag of every section you draw on, inline, in the exact form it is "
    "given to you in square brackets. Cite as you "
    "use it, not in a list at the end.\n"
    "\n"
    "If the sections do not answer the question, say so plainly, then say what "
    "the manual does cover nearby. Saying the manual does not cover something is "
    "a correct and useful answer; inventing one is not.\n"
    "\n"
    "End with a line beginning 'Further reading:' naming the one or two sections "
    "worth opening in full, by tag.\n"
    "\n"
    "The worked examples that follow show FORMAT ONLY. Their content is not the "
    "manual. Never cite a tag that does not appear in the sections given to you "
    "for the current question, and never repeat a fact from an example unless "
    "the current sections also state it. If the current sections do not support "
    "an answer, refuse -- do not reach back to an example for one.\n"
    "\n"
    "Be concise and concrete. Use the exact tool and control names the manual "
    "uses."
)

# Two worked examples, shown as prior turns rather than described in the
# instructions above: a chat model copies the shape of previous assistant turns
# far more reliably than it follows a description of that shape.
#
# The content is deliberately abstract -- Widget Alpha, [sect-gamma] -- and none
# of these tags exist in the manual. An earlier version quoted real sections,
# and the model reproduced them: asked why a transform shows a bright cross, it
# cited [p1-taper-edges] three times although retrieval had never returned that
# section. The example had taught it a fact, and it recited the fact. Telling it
# in SYSTEM not to do that did not stop it. Examples that carry no usable facts
# do, and anything leaking out of them now names a tag that plainly does not
# exist, so it is visible rather than plausible.
#
# The second example is the one that earns its place. Refusal is what models are
# worst at and what matters most in a teaching tool -- a confident wrong answer
# about Fourier optics is read as true by exactly the students who cannot yet
# check it. It also shows the shape of a partial answer: what the manual lacks,
# then what it has.
FEWSHOT = [
    (
        "Manual sections:\n"
        "\n"
        "[sect-alpha] Widget Alpha\n"
        "Widget Alpha does the first thing. Setting One: what the first setting "
        "controls, in its own units.\n"
        "\n"
        "---\n"
        "\n"
        "[sect-beta] Widget Beta\n"
        "Widget Beta does a different, unrelated thing.\n"
        "\n"
        "---\n"
        "\n"
        "Question: how do I do the first thing?",

        "Use Widget Alpha, which does the first thing [sect-alpha].\n"
        "\n"
        "Setting One controls how much, in its own units [sect-alpha].\n"
        "\n"
        "Further reading: [sect-alpha]."
    ),
    (
        "Manual sections:\n"
        "\n"
        "[sect-gamma] Measuring Widget Gamma\n"
        "How to measure the value of Gamma from an image.\n"
        "\n"
        "---\n"
        "\n"
        "[sect-delta] What Gamma Is\n"
        "The theory of what Gamma means physically.\n"
        "\n"
        "---\n"
        "\n"
        "Question: how do I correct my image for Gamma?",

        "The manual does not cover correcting an image for Gamma. It covers "
        "measuring Gamma, not applying the result.\n"
        "\n"
        "What it does have nearby:\n"
        "- How to measure the value of Gamma from an image [sect-gamma].\n"
        "- The theory of what Gamma means physically [sect-delta].\n"
        "\n"
        "Further reading: [sect-delta] for the theory, then [sect-gamma] for the "
        "measurement."
    ),
]


def build_prompt(question, hits):
    blocks = []
    for c, score in hits:
        tag = c["anchor"] or c["page"]
        blocks.append(f"[{tag}] {c['title']}\n{c['text']}")
    return (
        "Manual sections:\n\n" + "\n\n---\n\n".join(blocks) +
        f"\n\n---\n\nQuestion: {question}"
    )


def build_messages(question, hits, fewshot=True):
    """
    The full chat exchange. Shared by the command line and by rag/serve.py so
    the application and the terminal cannot drift apart.

    The examples go in as real prior turns rather than as text inside SYSTEM,
    because a chat model copies the shape of previous assistant turns far more
    reliably than it follows a description of that shape.
    """
    msgs = [{"role": "system", "content": SYSTEM}]
    if fewshot:
        for user, assistant in FEWSHOT:
            msgs.append({"role": "user", "content": user})
            msgs.append({"role": "assistant", "content": assistant})
    msgs.append({"role": "user", "content": build_prompt(question, hits)})
    return msgs


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("question")
    ap.add_argument("-k", type=int, default=12, help="sections to retrieve")
    ap.add_argument("--floor", type=float, default=None,
                    help="minimum top-1 cosine; below it, refuse instead of "
                         "answering (calibrate with eval.py)")
    ap.add_argument("--model", default="mlx-community/Qwen3-8B-4bit",
                    help="any mlx-lm model on Hugging Face")
    ap.add_argument("--max-tokens", type=int, default=700)
    ap.add_argument("--no-fewshot", action="store_true",
                    help="drop the worked examples; useful for measuring what "
                         "they are worth, and for comparing token cost")
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
        build_messages(args.question, hits, fewshot=not args.no_fewshot),
        add_generation_prompt=True, tokenize=False)

    print(f"[{len(hits)} sections, ~{len(prompt.split())} words of context]\n")
    answer = generate(model, tokenizer, prompt=text,
                      max_tokens=args.max_tokens, verbose=False)
    print(answer.strip())

    # Numbered, with the tag kept visible, so a [tag] citation in the answer
    # can be found here by eye. The application rewrites the tags into numbered
    # links instead -- see linkCitations() in ftwindow_ai.cpp.
    print("\nSources:")
    for n, (c, score) in enumerate(hits, 1):
        print(f"  [{n}] {c['anchor'] or c['page']}  ({score:.3f})")
        print(f"      {c['url']}")


if __name__ == "__main__":
    main()
