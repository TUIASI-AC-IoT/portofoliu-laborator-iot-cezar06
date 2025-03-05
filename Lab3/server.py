import io
from flask import Flask, send_file, jsonify
import os.path
import re

app = Flask(__name__)


def get_current_version():
    try:
        with open("include/version.h", "r") as f:
            content = f.read()
            version_match = re.search(r'#define VERSION_SHORT "(.*?)"', content)
            if version_match:
                return version_match.group(1)
    except Exception as e:
        print(f"Error reading version: {e}")
    return "unknown"


@app.route("/firmware.bin")
def firm():
    with open(".pio\\build\\esp-wrover-kit\\firmware.bin", "rb") as bites:
        print(bites)
        return send_file(io.BytesIO(bites.read()), mimetype="application/octet-stream")


@app.route("/version")
def version():
    return jsonify({"version": get_current_version()})


@app.route("/")
def hello():
    return "Hello World!"


if __name__ == "__main__":
    app.run(host="0.0.0.0", ssl_context=("ca_cert.pem", "ca_key.pem"), debug=True)
