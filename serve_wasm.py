#!/usr/bin/env python3
"""Local dev server for the multithreaded WASM build.

The threaded build relies on SharedArrayBuffer, which browsers only expose to
"cross-origin isolated" pages. That requires two response headers that the
stock `python3 -m http.server` does not send:

    Cross-Origin-Opener-Policy:   same-origin
    Cross-Origin-Embedder-Policy: require-corp

Run from the WASM build directory:

    cd build_wasm && python3 ../serve_wasm.py 8080

then open http://localhost:8080/ft.html. Verify isolation in the JS console:

    crossOriginIsolated            // -> true
    typeof SharedArrayBuffer       // -> "object"
"""
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler


class CrossOriginIsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # ft.wasm / ft.js are overwritten in place on each build, so force
        # revalidation to avoid serving a stale pair (mirrors ft-apache.conf).
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def guess_type(self, path):
        if path.endswith(".wasm"):
            return "application/wasm"
        return super().guess_type(path)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    httpd = HTTPServer(("0.0.0.0", port), CrossOriginIsolatedHandler)
    print(f"Serving cross-origin-isolated on http://localhost:{port}/ft.html")
    print("(Ctrl+C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        httpd.server_close()


if __name__ == "__main__":
    main()
