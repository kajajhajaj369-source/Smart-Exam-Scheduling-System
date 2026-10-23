# Smart Exam Seating Arrangement & Anti-Cheating Optimizer

This C++17 project creates an exam timetable, distributes exam candidates across rooms, and assigns them to seats while separating students from the same batch. The accompanying HTML page visualizes the generated seating report; it does not run the scheduling algorithms itself.

## Files

| File | Purpose |
|---|---|
| `allocator.cpp` | Scheduling, room allocation, seating, and report generation |
| `data/students.csv`, `data/subjects.csv`, `data/rooms.csv` | Input data |
| `exam_seating_visualizer.html` | Browser-based viewer for `output/report.json` |
| `output/report.txt`, `output/report.json` | Generated reports |

## Build and run (Windows)

For normal use on Windows 11, double-click `Start_Exam_Seating.bat` in File Explorer. It checks the required tools, compiles the allocator only when needed, waits for the local server health check, and opens the app in your default browser. Leave the console window open while using the app; close it (or press Ctrl+C) to stop the server. If the app is already running, the launcher reuses it without opening another tab.

Prerequisites: Python 3.8 or newer and `g++` (for example, GCC from the MSYS2 UCRT64 MinGW-w64 toolchain), with their commands available on PATH. The launcher reports which tool is missing and how to install it.

From the project folder, compile and run:

```powershell
g++ -std=c++17 -Wall -Wextra -O2 allocator.cpp -o allocator.exe
.\allocator.exe
type output\report.txt
```

Open `exam_seating_visualizer.html` in a browser and use **Load your report.json** to select `output/report.json`. The viewer also includes sample data.

To upload input data and generate a new arrangement in the page, run `python server.py` and open `http://127.0.0.1:8000`. Select all three CSV files. Required headers are `id,name,batch,subjects` for students (separate subject codes with `;`), `code,name` for subjects, and `id,name,rows,cols` for rooms. Files are validated, limited to 1 MB each, processed temporarily, and deleted after generation. The uploaded files do not replace the sample datasets.

Set **Bench rows** and **Bench columns** before generating. These values set the actual seating grid for every room; each grid cell holds one student, matching the current allocator model. The configured grid is limited to 64 seats per room.

### Sample datasets

`data/sample/` contains small (12 students), medium (30), and large (60) student CSVs, plus their shared `subjects.csv` catalog and a normal multi-room `rooms.csv`. Upload a student file with the sample subject and room files to try a scale. The web uploader applies its selected rows and columns to every room; running the C++ allocator directly uses the varied dimensions in `rooms.csv`. To test insufficient capacity, upload `students_large.csv`, `subjects.csv`, and `rooms_limited_capacity.csv`; the report should show a capacity warning. The sample files use the same headers as the regular inputs and do not replace them.

## Discrete Mathematics in the implementation

- **Graph theory and coloring:** Each exam is a vertex. An edge connects two exams that share at least one student, so adjacent vertices cannot use the same timetable slot. The scheduler uses greedy Welsh–Powell coloring and checks whether one fewer slot can work using backtracking.
- **Sets and relations:** Student IDs enrolled in each exam are stored as sets. Shared membership defines the exam-conflict relation. The seating grid represents seat positions; adjacent positions are related by the four orthogonal and four diagonal neighbor directions.
- **Constraint satisfaction:** Backtracking assigns each seat a batch while forbidding equal batches in neighboring seats. This is an enforced seating rule using the `batch` field in `students.csv`.
- **Counting and the Pigeonhole Principle:** For each exam slot, the report compares the total number of candidates in its exams with the sum of room capacities (`rows × columns`). If demand exceeds capacity, at least one candidate cannot receive a seat in that slot. This check reports feasibility; it does not make insufficient capacity feasible.
- **Room allocation:** Within each slot, exams are processed largest first and rooms with the most remaining seats are selected using a priority queue.

## Limitations

The allocator models adjacent same-batch students as a hard constraint, including diagonal neighbors. It does not model individual classes beyond the supplied batch labels, previous seat assignments, or a repeat-seat penalty. If demand exceeds total room capacity for a slot, the report flags it, but the room allocator may leave candidates unassigned. The HTML page only displays the generated report.
