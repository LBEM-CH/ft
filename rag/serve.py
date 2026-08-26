#!/usr/bin/env python3
"""
Persistent worker behind the application's AI Help mode.

The application starts this once (QProcess) and keeps it alive, because loading
an 8B model takes 10-20 seconds and doing that per question would be unusable.
Both models stay resident; each question is only retrieval plus generation.

Protocol: one JSON object per line, both directions. Line-delimited rather than
framed, so it is readable in a terminal and debuggable by hand:

    $ python3 rag/serve.py
    {"type":"status","stage":"index","detail":"83 chunks"}
    {"type":"status","stage":"embed-model","detail":"Qwen/Qwen3-Embedding-0.6B"}
    {"type":"status","stage":"llm","detail":"mlx-community/Qwen3-8B-4bit"}
    {"type":"ready"}
    {"question": "how do I do CTF correction?"}          <- typed in, or sent by Qt
    {"type":"token","text":"..."}                         <- streamed while generating
    {"type":"answer","think":"...","answer":"...","sources":[...],"elapsed":12.4}

Reasoning models (Qwen3) wrap their working in <think>...</think>. That block
is split off here rather than in C++, so the application receives the reasoning
and the answer as separate fields and can fold one away.
"""
import argparse, json, re, sys, time

from retrieve import Retriever
from ask import SYSTEM, build_prompt, build_messages

THINK_RE = re.compile(r"<think>(.*?)</think>", re.S)


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def split_think(text):
    """Separate the reasoning block from the answer."""
    m = THINK_RE.search(text)
    if m:
        return m.group(1).strip(), THINK_RE.sub("", text, count=1).strip()
    # An unterminated <think> means generation stopped mid-reasoning.
    if "<think>" in text:
        return text.split("<think>", 1)[1].strip(), ""
    return "", text.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index-dir", default="rag")
    ap.add_argument("--model", default="mlx-community/Qwen3-8B-4bit")
    ap.add_argument("--device", default=None)
    ap.add_argument("-k", type=int, default=12)
    ap.add_argument("--floor", type=float, default=None)
    ap.add_argument("--max-tokens", type=int, default=900)
    ap.add_argument("--temp", type=float, default=0.2)
    ap.add_argument("--no-fewshot", action="store_true",
                    help="drop the worked examples (see rag/ask.py FEWSHOT)")
    args = ap.parse_args()

    try:
        emit({"type": "status", "stage": "index", "detail": "loading"})
        r = Retriever(args.index_dir, args.device)
        emit({"type": "status", "stage": "index",
              "detail": f"{r.meta['n_chunks']} chunks"})

        # Touch the encoder now so the first question is not slowed by it.
        emit({"type": "status", "stage": "embed-model", "detail": r.meta["model"]})
        r.scores("warm up")

        emit({"type": "status", "stage": "llm", "detail": args.model})
        from mlx_lm import load, stream_generate
        model, tokenizer = load(args.model)
    except (Exception, SystemExit) as e:
        # SystemExit is included because it is not an Exception: Retriever
        # refuses a stale index via sys.exit(message), and letting that fly
        # would end the worker without the fatal JSON the application waits for.
        if isinstance(e, SystemExit):
            msg = str(e.code) if e.code is not None else "SystemExit"
        else:
            msg = f"{type(e).__name__}: {e}"
        emit({"type": "error", "message": msg, "fatal": True})
        return 1

    # Sampler construction moved between mlx-lm versions; low temperature suits
    # grounded answering, but running without it is better than not running.
    gen_kwargs = {}
    try:
        from mlx_lm.sample_utils import make_sampler
        gen_kwargs["sampler"] = make_sampler(temp=args.temp)
    except Exception:
        pass

    emit({"type": "ready"})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            emit({"type": "error", "message": f"bad request JSON: {e}"})
            continue
        if req.get("command") == "quit":
            break

        question = (req.get("question") or "").strip()
        if not question:
            emit({"type": "error", "message": "empty question"})
            continue

        try:
            t0 = time.time()
            k = int(req.get("k", args.k))
            # Coerced like k: a client bridging JSON through UI code may well
            # send the number as a string, and it would otherwise surface as a
            # TypeError deep in the score comparison.
            floor = req.get("floor", args.floor)
            if floor is not None:
                floor = float(floor)
            hits = r.retrieve(question, k, floor)

            if not hits:
                emit({"type": "answer", "think": "",
                      "answer": "The manual does not appear to cover this question.",
                      "sources": [], "elapsed": round(time.time() - t0, 1)})
                continue

            emit({"type": "retrieved", "sources": [
                {"tag": c["anchor"] or c["page"], "anchor": c["anchor"],
                 "title": c["title"], "url": c["url"], "score": round(s, 4)}
                for c, s in hits]})

            prompt = build_prompt(question, hits)
            try:
                text = tokenizer.apply_chat_template(
                    build_messages(question, hits,
                                   fewshot=bool(req.get("fewshot", not args.no_fewshot))),
                    add_generation_prompt=True, tokenize=False)
            except Exception:
                text = SYSTEM + "\n\n" + prompt

            out = []
            for piece in stream_generate(model, tokenizer, prompt=text,
                                         max_tokens=int(req.get("max_tokens",
                                                                args.max_tokens)),
                                         **gen_kwargs):
                chunk = getattr(piece, "text", piece)
                out.append(chunk)
                emit({"type": "token", "text": chunk})

            think, answer = split_think("".join(out))
            emit({"type": "answer", "think": think, "answer": answer,
                  "sources": [{"tag": c["anchor"] or c["page"],
                               "anchor": c["anchor"], "title": c["title"],
                               "url": c["url"], "score": round(s, 4)}
                              for c, s in hits],
                  "elapsed": round(time.time() - t0, 1)})
        except Exception as e:
            emit({"type": "error", "message": f"{type(e).__name__}: {e}"})

    return 0


if __name__ == "__main__":
    sys.exit(main())
