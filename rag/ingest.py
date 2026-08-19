#!/usr/bin/env python3
"""
Turn the manual pages in ft-manual/ into retrieval chunks.

The manual is already divided into self-contained topics as <details>
elements carrying id attributes, and the application deep-links to those ids
(see openManualAnchor in ftwindow_mouse.cpp). Chunking on the same boundaries
means every chunk knows its own address in the manual, so an answer can cite
its sources as clickable links for free.

Long sections are split at their nested <details> boundaries rather than at an
arbitrary character count, so a chunk is never half a thought. The parent
heading is carried onto each piece so the fragment still says what it is about.

Output: chunks.json, a list of
    {id, page, page_title, anchor, title, trail, url, text, words}
"""
import argparse, json, re, sys
from html.parser import HTMLParser
from pathlib import Path

# Where the manual is published. Matches kManualBase in ftwindow_mouse.cpp so
# citations point at the same pages the application's own links do.
BASE_URL = "https://lbem-status.epfl.ch/ft-manual/"

# Page titles copied from the kManualPages table in ftwindow_mouse.cpp, so the
# two search routes name the pages identically.
PAGE_TITLES = {
    "manual.html":           "Main manual",
    "manual_panel1.html":    "Panel 1 - real-space tools",
    "manual_panel2.html":    "Panel 2 - Fourier tools",
    "manual_exercises.html": "Exercises",
}

# Elements whose content the browser does not render as prose. Without this the
# 21 inline data:image blocks in manual.html would enter the chunks as base64.
# Mirrors the strip list in manualTextBlocks(), ftwindow_mouse.cpp:849-853.
SKIP_TAGS = {"script", "style", "svg", "head"}


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


class Node:
    """One <details> element, plus its nested ones."""
    __slots__ = ("anchor", "title_parts", "text_parts", "children")

    def __init__(self, anchor):
        self.anchor = anchor
        self.title_parts = []   # text inside this element's own <summary>
        self.text_parts = []    # text directly inside it, excluding children
        self.children = []


class DetailsParser(HTMLParser):
    """
    Builds the <details> tree. A hand-rolled parser rather than a regex because
    the sections nest, and a non-greedy regex closes the outer element on the
    inner element's end tag.
    """

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node(None)          # page-level text, outside any <details>
        self.stack = [self.root]
        self.skip_depth = 0
        self.in_summary = False

    def handle_starttag(self, tag, attrs):
        if self.skip_depth:
            if tag in SKIP_TAGS:
                self.skip_depth += 1
            return
        if tag in SKIP_TAGS:
            self.skip_depth = 1
            return
        if tag == "details":
            node = Node(dict(attrs).get("id"))
            self.stack[-1].children.append(node)
            self.stack.append(node)
            self.in_summary = False
        elif tag == "summary":
            self.in_summary = True

    def handle_endtag(self, tag):
        if self.skip_depth:
            if tag in SKIP_TAGS:
                self.skip_depth -= 1
            return
        if tag == "details":
            if len(self.stack) > 1:
                self.stack.pop()
            self.in_summary = False
        elif tag == "summary":
            self.in_summary = False

    def handle_data(self, data):
        if self.skip_depth or not data.strip():
            return
        node = self.stack[-1]
        (node.title_parts if self.in_summary else node.text_parts).append(data)


def title_of(n):
    return norm(" ".join(n.title_parts))


def own_text(n):
    return norm(" ".join(n.text_parts))


def full_text(n):
    """This node's text plus every descendant's, headings included."""
    parts = [own_text(n)]
    for c in n.children:
        parts.append(title_of(c))
        parts.append(full_text(c))
    return norm(" ".join(p for p in parts if p))


def emit(node, page, inherited_anchor, trail, out, max_words, min_words):
    """
    Append one chunk for `node`, or several if it is long enough to be worth
    splitting at its nested sections.
    """
    anchor = node.anchor or inherited_anchor
    title = title_of(node) or (trail[-1] if trail else PAGE_TITLES.get(page, page))
    text = full_text(node)
    words = len(text.split())

    # Short enough to stand as one chunk, or nothing to split it at.
    if words <= max_words or not node.children:
        if words >= min_words:
            add(out, page, anchor, title, trail, text)
        return

    # Too long: the parent's own prose becomes a chunk, each nested section
    # becomes its own, and the parent's heading rides along in the trail so the
    # fragment is still identifiable.
    parent_own = own_text(node)
    if len(parent_own.split()) >= min_words:
        add(out, page, anchor, title, trail, parent_own)
    for c in node.children:
        emit(c, page, anchor, trail + [title], out, max_words, min_words)


def add(out, page, anchor, title, trail, text):
    out.append({
        "id":         f"{page}#{anchor or 'page'}::{len(out)}",
        "page":       page,
        "page_title": PAGE_TITLES.get(page, page),
        "anchor":     anchor,
        "title":      title,
        "trail":      list(trail),
        "url":        BASE_URL + page + (f"#{anchor}" if anchor else ""),
        "text":       text,
        "words":      len(text.split()),
    })


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manual-dir", default="ft-manual",
                    help="directory holding the manual*.html pages")
    ap.add_argument("--out", default="rag/chunks.json")
    ap.add_argument("--max-words", type=int, default=600,
                    help="sections longer than this are split at nested <details>")
    ap.add_argument("--min-words", type=int, default=20,
                    help="fragments shorter than this are dropped as headings-only")
    args = ap.parse_args()

    manual_dir = Path(args.manual_dir)
    pages = sorted(manual_dir.glob("manual*.html"))
    if not pages:
        sys.exit(f"no manual*.html found in {manual_dir}")

    chunks, skipped_preamble = [], 0
    for path in pages:
        parser = DetailsParser()
        parser.feed(path.read_text(encoding="utf-8", errors="replace"))
        parser.close()
        page = path.name

        # Prose that sits outside every <details> would otherwise never be
        # retrievable, so it becomes a chunk of its own when substantial.
        preamble = own_text(parser.root)
        if len(preamble.split()) >= args.min_words:
            add(chunks, page, None, PAGE_TITLES.get(page, page) + " - introduction",
                [], preamble)
        else:
            skipped_preamble += len(preamble.split())

        for top in parser.root.children:
            emit(top, page, None, [], chunks, args.max_words, args.min_words)

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(chunks, indent=1, ensure_ascii=False),
                              encoding="utf-8")

    ws = sorted(c["words"] for c in chunks)
    print(f"pages           {len(pages)}")
    print(f"chunks          {len(chunks)}")
    print(f"words total     {sum(ws)}")
    print(f"words  median   {ws[len(ws) // 2]}")
    print(f"       90th pct {ws[int(len(ws) * 0.9)]}")
    print(f"       max      {ws[-1]}")
    print(f"no anchor       {sum(1 for c in chunks if not c['anchor'])}"
          "   (these can only link to the page, not to a section)")
    if skipped_preamble:
        print(f"dropped         {skipped_preamble} words of short page preamble")
    print(f"wrote           {args.out}")


if __name__ == "__main__":
    main()
