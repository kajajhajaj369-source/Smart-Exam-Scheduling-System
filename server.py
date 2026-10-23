"""Small local upload server for the existing C++ exam allocator."""
import csv
import io
import json
import argparse
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from email import policy
from email.parser import BytesParser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
MAX_REQUEST = 3 * 1024 * 1024
REQUIRED = {
    "students": ["id", "name", "batch", "subjects"],
    "subjects": ["code", "name"],
    "rooms": ["id", "name", "rows", "cols"],
}


def validate_csv_files(files):
    parsed = {}
    for key, required in REQUIRED.items():
        if key not in files:
            raise ValueError(f"Select a {key}.csv file.")
        raw = files[key]
        try:
            text = raw.decode("utf-8-sig")
            reader = csv.reader(io.StringIO(text, newline=""), strict=True)
            rows = list(reader)
        except (UnicodeDecodeError, csv.Error):
            raise ValueError(f"{key}.csv must be a valid UTF-8 CSV file.")
        if not rows or not any(cell.strip() for row in rows for cell in row):
            raise ValueError(f"{key}.csv is empty.")
        header = [cell.strip() for cell in rows[0]]
        if header != required:
            raise ValueError(f"{key}.csv columns must be exactly: {', '.join(required)} (in this order).")
        indices = list(range(len(required)))
        data_rows = []
        for line_no, row in enumerate(rows[1:], start=2):
            if not row or not any(cell.strip() for cell in row):
                continue
            if len(row) != len(header):
                raise ValueError(f"{key}.csv row {line_no} has the wrong number of columns.")
            cells = [cell.strip() for cell in row]
            if any(not cells[i] for i in indices):
                raise ValueError(f"{key}.csv row {line_no} has a missing required value.")
            # The existing C++ reader splits on commas and does not support quoted commas.
            if any("," in cell for cell in cells):
                raise ValueError(f"{key}.csv row {line_no} contains a comma inside a value, which the current allocator cannot parse.")
            if any("\n" in cell or "\r" in cell for cell in cells):
                raise ValueError(f"{key}.csv row {line_no} contains a line break inside a value, which the current allocator cannot parse.")
            data_rows.append(dict(zip(header, cells)))
        if not data_rows:
            raise ValueError(f"{key}.csv has a header but no data rows.")
        parsed[key] = data_rows

    for key, field in (("students", "id"), ("subjects", "code"), ("rooms", "id")):
        values = [row[field] for row in parsed[key]]
        if len(values) != len(set(values)):
            raise ValueError(f"{key}.csv contains duplicate {field} values.")
    subject_codes = {row["code"] for row in parsed["subjects"]}
    for i, student in enumerate(parsed["students"], start=2):
        codes = [code.strip() for code in student["subjects"].split(";") if code.strip()]
        if not codes:
            raise ValueError(f"students.csv row {i} must list at least one subject code.")
        unknown = [code for code in codes if code not in subject_codes]
        if unknown:
            raise ValueError(f"students.csv row {i} refers to unknown subject code(s): {', '.join(unknown)}.")
    for i, room in enumerate(parsed["rooms"], start=2):
        try:
            if int(room["rows"]) < 1 or int(room["cols"]) < 1:
                raise ValueError
        except ValueError:
            raise ValueError(f"rooms.csv row {i} must have positive integer rows and cols.")
    # Store a normalized, simple CSV form understood by the existing C++ reader.
    for key, columns in REQUIRED.items():
        output = io.StringIO(newline="")
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(columns)
        writer.writerows([[row[column] for column in columns] for row in parsed[key]])
        files[key] = output.getvalue().encode("utf-8")


def ensure_allocator():
    source = ROOT / "allocator.cpp"
    binary = ROOT / "allocator.exe"
    if not binary.exists() or source.stat().st_mtime > binary.stat().st_mtime:
        result = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-O2", str(source), "-o", str(binary)],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode:
            raise RuntimeError("Could not build the allocator. Ensure g++ is installed.")
    return binary


class Handler(BaseHTTPRequestHandler):
    def send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            body = b'{"status":"ok","service":"exam-seating"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path not in ("/", "/exam_seating_visualizer.html"):
            self.send_error(404)
            return
        body = (ROOT / "exam_seating_visualizer.html").read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/generate":
            self.send_error(404)
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if size <= 0 or size > MAX_REQUEST:
                raise ValueError("Upload is empty or exceeds the 3 MB total limit.")
            content_type = self.headers.get("Content-Type", "")
            if not content_type.lower().startswith("multipart/form-data;"):
                raise ValueError("Upload the three input files using the CSV controls.")
            mime = BytesParser(policy=policy.default).parsebytes(
                (f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n").encode() + self.rfile.read(size)
            )
            files = {}
            for part in mime.iter_parts():
                key = part.get_param("name", header="content-disposition")
                filename = part.get_filename()
                if key not in REQUIRED or not filename:
                    continue
                if not filename.lower().endswith(".csv"):
                    raise ValueError("Only .csv input files are accepted.")
                if key in files:
                    raise ValueError(f"Choose only one file for {key}.")
                data = part.get_payload(decode=True) or b""
                if len(data) > 1024 * 1024:
                    raise ValueError("Each CSV file must be 1 MB or smaller.")
                files[key] = data
            validate_csv_files(files)
            rows = int(self.headers.get("X-Bench-Rows", "0"))
            cols = int(self.headers.get("X-Bench-Columns", "0"))
            if rows < 1 or cols < 1 or rows > 16 or cols > 16 or rows * cols > 64:
                raise ValueError("Bench rows and columns must be positive; each room layout is limited to 64 seats (maximum 16 per side).")
            binary = ensure_allocator()
            temp_paths = []
            try:
                for key in ("students", "subjects", "rooms"):
                    with tempfile.NamedTemporaryFile(
                        mode="wb", prefix=".upload-", suffix=".csv",
                        dir=ROOT / "output", delete=False,
                    ) as temp_file:
                        temp_file.write(files[key])
                    temp_paths.append(Path(temp_file.name))
                result = subprocess.run(
                    [str(binary), str(rows), str(cols), *(str(path) for path in temp_paths)], cwd=ROOT,
                    capture_output=True, text=True, timeout=30,
                )
                if result.returncode:
                    raise RuntimeError("The allocator could not process these inputs. Check the CSV values and try again.")
                report = json.loads((ROOT / "output" / "report.json").read_text(encoding="utf-8"))
            finally:
                for path in temp_paths:
                    try:
                        path.unlink(missing_ok=True)
                    except OSError:
                        pass
            self.send_json(200, {"report": report, "message": "Schedule and seating generated successfully."})
        except (ValueError, json.JSONDecodeError) as exc:
            self.send_json(400, {"error": str(exc)})
        except subprocess.TimeoutExpired:
            self.send_json(400, {"error": "Allocation took too long. Try a smaller bench layout or a smaller dataset."})
        except Exception as exc:
            message = str(exc) if isinstance(exc, RuntimeError) else "The upload could not be processed. Check the files and try again."
            self.send_json(500, {"error": message})


def main():
    parser = argparse.ArgumentParser(description="Local server for the exam seating visualizer")
    parser.add_argument("--open-browser", action="store_true", help="open the default browser after /health responds")
    args = parser.parse_args()
    url = "http://127.0.0.1:8000/"
    try:
        server = HTTPServer(("127.0.0.1", 8000), Handler)
    except OSError as exc:
        try:
            with urllib.request.urlopen("http://127.0.0.1:8000/health", timeout=2) as response:
                if response.status == 200 and b'"service":"exam-seating"' in response.read():
                    print("The exam seating app is already running at " + url)
                    return 0
        except (OSError, urllib.error.URLError):
            pass
        print("ERROR: Could not start the server on 127.0.0.1:8000. The port may be in use by another application.")
        print("Close the other application using port 8000, then run this launcher again.")
        return 1

    print("Exam seating server is ready at " + url)
    print("Close this console window or press Ctrl+C to stop the server.", flush=True)
    if args.open_browser:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        ready = False
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen("http://127.0.0.1:8000/health", timeout=1) as response:
                    if response.status == 200 and b'"status":"ok"' in response.read():
                        ready = True
                        break
            except (OSError, urllib.error.URLError):
                time.sleep(0.2)
        if not ready:
            print("ERROR: The server did not become responsive within 15 seconds.")
            server.shutdown()
            worker.join()
            server.server_close()
            return 1
        try:
            if not webbrowser.open(url, new=0):
                print("The server is ready, but Windows could not open a browser. Open " + url)
        except webbrowser.Error as exc:
            print("The server is ready, but Windows could not open a browser: " + str(exc))
        try:
            worker.join()
        except KeyboardInterrupt:
            server.shutdown()
            worker.join()
    else:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
