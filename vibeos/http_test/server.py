from pathlib import Path
from flask import Flask, abort, send_file, send_from_directory
import os

app = Flask(__name__)

BASE_DIR = Path(__file__).resolve().parent
BLOCKED_DOWNLOADS = {"server.key"}


@app.route("/")
def index():
    names = []
    for path in sorted(BASE_DIR.iterdir()):
        if path.is_file() and path.name not in BLOCKED_DOWNLOADS:
            names.append(path.name)
    return "OK\n" + "\n".join(names) + ("\n" if names else "")


@app.route("/download/<path:filename>")
@app.route("/<path:filename>")
def download_file(filename):
    path = (BASE_DIR / filename).resolve()
    if BASE_DIR not in path.parents and path != BASE_DIR:
        abort(404)
    if not path.is_file() or path.name in BLOCKED_DOWNLOADS:
        abort(404)
    if path.suffix == ".html":
        return send_file(path, mimetype="text/html")
    return send_from_directory(BASE_DIR, filename, as_attachment=True, download_name=path.name)


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "80"))
    app.run(host="0.0.0.0", port=port)
