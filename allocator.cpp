/*
 * SMART EXAM SEATING ARRANGEMENT & ANTI-CHEATING OPTIMIZER
 * -----------------------------------
 * Single-file C++17 version (all classes + main in one file for easy compiling).
 *
 * Solves three linked scheduling problems using classic DSA techniques:
 *   1. Timetable scheduling  -> Graph coloring (greedy Welsh-Powell + backtracking)
 *   2. Room allocation       -> Priority-queue based bin packing
 *   3. Seating arrangement   -> Backtracking CSP (no same-batch adjacency)
 *
 * Build:
 *   g++ -std=c++17 -Wall -Wextra -O2 allocator.cpp -o allocator
 *
 * Run (from the folder containing this file, so it can find data/ and output/):
 *   ./allocator          (or allocator.exe on Windows)
 *
 * Reads:  data/students.csv, data/subjects.csv, data/rooms.csv
 * Writes: output/report.txt  (human-readable report)
 *         output/report.json (structured export for the HTML visualizer)
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <queue>
#include <chrono>
#include <algorithm>
#include <numeric>
#if defined(_WIN32)
    #include <direct.h>
    #define MAKE_DIR(path) _mkdir(path)
#else
    #include <sys/stat.h>
    #define MAKE_DIR(path) mkdir(path, 0755)
#endif
using namespace std;

/* =========================================================================
   DATA MODELS
   ========================================================================= */

struct Student {
    string id;
    string name;
    string batch;
    vector<string> subjects; // subject codes this student is enrolled in
};

struct Room {
    string id;
    string name;
    int rows;
    int cols;
    int capacity() const { return rows * cols; }
};

struct Exam {
    string code;
    string name;
    set<string> studentIds; // populated from student enrollment data
    int slot = -1;          // assigned exam slot, -1 = unassigned
};

/* =========================================================================
   CSV READER
   ========================================================================= */

class CSVReader {
public:
    static vector<string> splitLine(const string& line, char delim = ',') {
        vector<string> tokens;
        stringstream ss(line);
        string token;
        while (getline(ss, token, delim)) tokens.push_back(token);
        return tokens;
    }

    static vector<string> splitSubjects(const string& field, char delim = ';') {
        return splitLine(field, delim);
    }

    static vector<Student> readStudents(const string& path) {
        ifstream file(path);
        if (!file.is_open()) throw runtime_error("Could not open student file: " + path);
        vector<Student> students;
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            if (line.empty()) continue;
            vector<string> tokens = splitLine(line);
            if (tokens.size() < 4) continue;
            Student s;
            s.id = tokens[0];
            s.name = tokens[1];
            s.batch = tokens[2];
            s.subjects = splitSubjects(tokens[3]);
            students.push_back(s);
        }
        return students;
    }

    static vector<Exam> readSubjects(const string& path) {
        ifstream file(path);
        if (!file.is_open()) throw runtime_error("Could not open subjects file: " + path);
        vector<Exam> exams;
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            if (line.empty()) continue;
            vector<string> tokens = splitLine(line);
            if (tokens.size() < 2) continue;
            Exam e;
            e.code = tokens[0];
            e.name = tokens[1];
            exams.push_back(e);
        }
        return exams;
    }

    static vector<Room> readRooms(const string& path) {
        ifstream file(path);
        if (!file.is_open()) throw runtime_error("Could not open rooms file: " + path);
        vector<Room> rooms;
        string line;
        getline(file, line); // skip header
        while (getline(file, line)) {
            if (line.empty()) continue;
            vector<string> tokens = splitLine(line);
            if (tokens.size() < 4) continue;
            Room r;
            r.id = tokens[0];
            r.name = tokens[1];
            r.rows = stoi(tokens[2]);
            r.cols = stoi(tokens[3]);
            rooms.push_back(r);
        }
        return rooms;
    }
};

/* =========================================================================
   TIMETABLE SCHEDULER — conflict graph + greedy Welsh-Powell + backtracking
   ========================================================================= */

class TimetableScheduler {
public:
    explicit TimetableScheduler(vector<Exam>& exams) : exams_(exams) {
        for (auto& e : exams_) {
            indexOf_[e.code] = (int)codes_.size();
            codes_.push_back(e.code);
        }
        adj_.assign(codes_.size(), vector<bool>(codes_.size(), false));
    }

    void buildConflictGraph() {
        int n = (int)exams_.size();
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const auto& a = exams_[i].studentIds;
                const auto& b = exams_[j].studentIds;
                bool shares = false;
                for (const auto& sid : a) {
                    if (b.count(sid)) { shares = true; break; }
                }
                if (shares) adj_[i][j] = adj_[j][i] = true;
            }
        }
    }

    // Greedy Welsh-Powell coloring: orders exams by degree (most constrained first).
    // Always succeeds; returns number of slots used. Sets exam.slot for each exam.
    int scheduleGreedy() {
        int n = (int)exams_.size();
        vector<int> order(n);
        iota(order.begin(), order.end(), 0);

        vector<int> degree(n, 0);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (adj_[i][j]) degree[i]++;

        sort(order.begin(), order.end(), [&](int a, int b) { return degree[a] > degree[b]; });

        vector<int> slotOf(n, -1);
        int maxSlotUsed = -1;

        for (int idx : order) {
            vector<bool> used(n, false);
            for (int j = 0; j < n; ++j)
                if (adj_[idx][j] && slotOf[j] != -1) used[slotOf[j]] = true;
            int slot = 0;
            while (slot < n && used[slot]) slot++;
            slotOf[idx] = slot;
            maxSlotUsed = max(maxSlotUsed, slot);
        }

        for (int i = 0; i < n; ++i) exams_[i].slot = slotOf[i];
        return maxSlotUsed + 1;
    }

    // Attempts to color the conflict graph using exactly maxSlots colors via
    // backtracking search. Returns true on success (and sets exam.slot),
    // false if no valid assignment exists with that many slots.
    bool scheduleBacktracking(int maxSlots) {
        int n = (int)exams_.size();
        vector<int> order(n);
        iota(order.begin(), order.end(), 0);

        vector<int> degree(n, 0);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (adj_[i][j]) degree[i]++;

        sort(order.begin(), order.end(), [&](int a, int b) { return degree[a] > degree[b]; });

        vector<int> assignment(n, -1);
        backtrackCount_ = 0;
        bool success = backtrackHelper(order, 0, maxSlots, assignment);
        if (success) for (int i = 0; i < n; ++i) exams_[i].slot = assignment[i];
        return success;
    }

    long long getBacktrackCount() const { return backtrackCount_; }

private:
    vector<Exam>& exams_;
    map<string, int> indexOf_;
    vector<string> codes_;
    vector<vector<bool>> adj_;
    long long backtrackCount_ = 0;

    bool backtrackHelper(const vector<int>& order, int pos, int maxSlots, vector<int>& assignment) {
        if (pos == (int)order.size()) return true;
        int node = order[pos];
        for (int color = 0; color < maxSlots; ++color) {
            backtrackCount_++;
            bool ok = true;
            for (int j = 0; j < (int)assignment.size(); ++j) {
                if (adj_[node][j] && assignment[j] == color) { ok = false; break; }
            }
            if (!ok) continue;
            assignment[node] = color;
            if (backtrackHelper(order, pos + 1, maxSlots, assignment)) return true;
            assignment[node] = -1; // backtrack
        }
        return false;
    }
};

/* =========================================================================
   SEATING ALLOCATOR — backtracking CSP (seats=nodes, adjacency=edges, batch=color)
   ========================================================================= */

class SeatingAllocator {
public:
    SeatingAllocator(int rows, int cols, map<string, int> batchCounts, bool diagonalConflict = true)
        : rows_(rows), cols_(cols), remaining_(std::move(batchCounts)), diagonal_(diagonalConflict) {
        seatGrid_.assign(rows_, vector<string>(cols_, ""));
        for (map<string, int>::iterator it = remaining_.begin(); it != remaining_.end(); ++it) {
            batchOrder_.push_back(it->first);
        }
    }

    bool allocate() {
        backtrackCount_ = 0;
        return solve(0);
    }

    const vector<vector<string>>& getSeatGrid() const { return seatGrid_; }
    long long getBacktrackCount() const { return backtrackCount_; }

private:
    int rows_, cols_;
    vector<vector<string>> seatGrid_;
    map<string, int> remaining_;
    vector<string> batchOrder_;
    bool diagonal_;
    long long backtrackCount_ = 0;

    bool isSafe(int r, int c, const string& batch) const {
        static const int dr4[] = {-1, 1, 0, 0};
        static const int dc4[] = {0, 0, -1, 1};
        static const int dr8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        static const int dc8[] = {-1, 0, 1, -1, 1, -1, 0, 1};

        const int* dr = diagonal_ ? dr8 : dr4;
        const int* dc = diagonal_ ? dc8 : dc4;
        int dirs = diagonal_ ? 8 : 4;

        for (int k = 0; k < dirs; ++k) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nr >= rows_ || nc < 0 || nc >= cols_) continue;
            if (seatGrid_[nr][nc] == batch && !batch.empty()) return false;
        }
        return true;
    }

    bool solve(int seatIndex) {
        int totalSeats = rows_ * cols_;
        if (seatIndex == totalSeats) return true;

        int r = seatIndex / cols_;
        int c = seatIndex % cols_;

        for (const auto& batch : batchOrder_) {
            backtrackCount_++;
            if (remaining_[batch] <= 0) continue;
            if (!isSafe(r, c, batch)) continue;

            seatGrid_[r][c] = batch;
            remaining_[batch]--;
            if (solve(seatIndex + 1)) return true;
            remaining_[batch]++;
            seatGrid_[r][c] = "";
        }

        // Leaving the seat empty is also valid (if wasteful) - lets the search
        // succeed even when a batch runs short of students.
        if (solve(seatIndex + 1)) return true;
        return false;
    }
};

/* =========================================================================
   ROOM ALLOCATOR — priority-queue bin packing (largest exam / largest room first)
   ========================================================================= */

class RoomAllocator {
public:
    explicit RoomAllocator(const vector<Room>& rooms) : rooms_(rooms) {}

    // Returns: examCode -> list of (roomId, list of studentIds placed in that room)
    map<string, vector<pair<string, vector<string>>>> allocate(vector<Exam>& exams, int slot) {
        map<string, vector<pair<string, vector<string>>>> result;

        vector<Exam*> slotExams;
        for (auto& e : exams) if (e.slot == slot) slotExams.push_back(&e);
        sort(slotExams.begin(), slotExams.end(),
             [](Exam* a, Exam* b) { return a->studentIds.size() > b->studentIds.size(); });

        // Max-heap of (remainingCapacity, roomId), with lazy deletion via a map
        // tracking true remaining capacity - a classic decrease-key pattern.
        map<string, int> remainingCap;
        map<string, Room> roomById;
        for (const auto& r : rooms_) {
            remainingCap[r.id] = r.capacity();
            roomById[r.id] = r;
        }

        using HeapEntry = pair<int, string>;
        priority_queue<HeapEntry> heap;
        for (const auto& r : rooms_) heap.push({r.capacity(), r.id});

        for (Exam* examPtr : slotExams) {
            vector<string> studentsLeft(examPtr->studentIds.begin(), examPtr->studentIds.end());
            size_t cursor = 0;

            while (cursor < studentsLeft.size() && !heap.empty()) {
                int cap = heap.top().first;
                string roomId = heap.top().second;
                heap.pop();

                if (remainingCap[roomId] != cap) {
                    if (remainingCap[roomId] > 0) heap.push({remainingCap[roomId], roomId});
                    continue;
                }
                if (cap <= 0) continue;

                int take = min((int)(studentsLeft.size() - cursor), cap);
                vector<string> placed(studentsLeft.begin() + cursor, studentsLeft.begin() + cursor + take);
                cursor += take;
                remainingCap[roomId] -= take;

                result[examPtr->code].push_back({roomId, placed});

                if (remainingCap[roomId] > 0) heap.push({remainingCap[roomId], roomId});
            }
        }

        return result;
    }

private:
    vector<Room> rooms_;
};

/* =========================================================================
   JSON HELPERS  (minimal - only used for our own controlled string content)
   ========================================================================= */

static string escapeJson(const string& s) {
    string result;
    for (char c : s) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c == '\n') { result += "\\n"; }
        else result += c;
    }
    return result;
}
static string jstr(const string& s) { return "\"" + escapeJson(s) + "\""; }

static void printSeparator(ostream& out) {
    out << "------------------------------------------------------------\n";
}

/* =========================================================================
   MAIN
   ========================================================================= */

int main(int argc, char* argv[]) {
    using Clock = chrono::high_resolution_clock;

    int layoutRows = 0;
    int layoutCols = 0;
    if (argc == 3 || argc == 6) {
        try {
            layoutRows = stoi(argv[1]);
            layoutCols = stoi(argv[2]);
        } catch (...) {
            cerr << "ERROR: bench rows and columns must be positive integers.\n";
            return 1;
        }
        if (layoutRows < 1 || layoutCols < 1 || layoutRows > 16 || layoutCols > 16 ||
            layoutRows * layoutCols > 64) {
            cerr << "ERROR: layout must be positive and contain no more than 64 seats per room.\n";
            return 1;
        }
    } else if (argc != 1) {
        cerr << "Usage: allocator [bench_rows bench_columns [students.csv subjects.csv rooms.csv]]\n";
        return 1;
    }

    // ---------- 1. Load data ----------
    vector<Student> students;
    vector<Exam> exams;
    vector<Room> rooms;
    const string studentsPath = argc == 6 ? argv[3] : "data/students.csv";
    const string subjectsPath = argc == 6 ? argv[4] : "data/subjects.csv";
    const string roomsPath = argc == 6 ? argv[5] : "data/rooms.csv";
    try {
        students = CSVReader::readStudents(studentsPath);
        exams = CSVReader::readSubjects(subjectsPath);
        rooms = CSVReader::readRooms(roomsPath);
    } catch (const exception& e) {
        cerr << "ERROR: " << e.what() << "\n";
        cerr << "Make sure you are running this program from the folder that "
                "contains both allocator.cpp/exe AND the data/ folder.\n";
        return 1;
    }
    if (layoutRows > 0) {
        for (auto& room : rooms) {
            room.rows = layoutRows;
            room.cols = layoutCols;
        }
    }

    map<string, Student> studentById;
    for (auto& s : students) studentById[s.id] = s;

    map<string, int> examIndex;
    for (int i = 0; i < (int)exams.size(); ++i) examIndex[exams[i].code] = i;
    for (auto& s : students) {
        for (auto& subCode : s.subjects) {
            if (examIndex.count(subCode)) exams[examIndex[subCode]].studentIds.insert(s.id);
        }
    }

    // Make sure the output folder exists - ofstream cannot create missing
    // directories on its own, only files inside directories that already exist.
    // MAKE_DIR returns -1 if the folder already exists, which is fine - we
    // only care that it exists by the time we try to open files in it.
    MAKE_DIR("output");

    ofstream out("output/report.txt");
    ofstream jout("output/report.json");

    if (!out.is_open() || !jout.is_open()) {
        cerr << "ERROR: could not open output files. Make sure you are running "
                "this program from the folder that contains allocator.cpp/exe.\n";
        return 1;
    }

    out << "SMART EXAM SEATING ARRANGEMENT & ANTI-CHEATING OPTIMIZER\n";
    printSeparator(out);
    out << "Loaded " << students.size() << " students, " << exams.size()
        << " subjects, " << rooms.size() << " rooms.\n\n";

    // ---------- 2. Timetable scheduling (graph coloring) ----------
    TimetableScheduler scheduler(exams);
    scheduler.buildConflictGraph();

    auto t1 = Clock::now();
    int slotsUsedGreedy = scheduler.scheduleGreedy();
    auto t2 = Clock::now();
    double greedyMs = chrono::duration<double, milli>(t2 - t1).count();

    map<string, int> greedySlot;
    for (auto& e : exams) greedySlot[e.code] = e.slot;

    bool fewerSlotsPossible = false;
    long long backtrackCount = 0;
    double backtrackMs = 0;
    if (slotsUsedGreedy > 1) {
        auto t3 = Clock::now();
        fewerSlotsPossible = scheduler.scheduleBacktracking(slotsUsedGreedy - 1);
        auto t4 = Clock::now();
        backtrackMs = chrono::duration<double, milli>(t4 - t3).count();
        backtrackCount = scheduler.getBacktrackCount();
    }

    // Restore the greedy (guaranteed valid) assignment as the final timetable.
    for (auto& e : exams) e.slot = greedySlot[e.code];

    out << "TIMETABLE (Graph Coloring)\n";
    printSeparator(out);
    out << "Slots required by greedy Welsh-Powell coloring: " << slotsUsedGreedy
        << " (" << greedyMs << " ms)\n";
    out << "Backtracking check with " << (slotsUsedGreedy - 1) << " slots: "
        << (fewerSlotsPossible ? "POSSIBLE" : "NOT POSSIBLE")
        << " (" << backtrackCount << " states explored, " << backtrackMs << " ms)\n\n";

    map<int, vector<string>> slotToExams;
    for (auto& e : exams) slotToExams[e.slot].push_back(e.code + " (" + e.name + ")");
    for (map<int, vector<string>>::iterator sit = slotToExams.begin(); sit != slotToExams.end(); ++sit) {
        int slot = sit->first;
        const vector<string>& list = sit->second;
        out << "  Slot " << slot + 1 << ": ";
        for (size_t i = 0; i < list.size(); ++i) out << list[i] << (i + 1 < list.size() ? ", " : "");
        out << "\n";
    }
    out << "\n";

    // ---------- JSON: header + stats + timetable ----------
    jout << "{\n";
    jout << "  \"stats\": {"
         << "\"students\": " << students.size() << ", "
         << "\"subjects\": " << exams.size() << ", "
         << "\"rooms\": " << rooms.size() << ", "
         << "\"slots\": " << slotsUsedGreedy << "},\n";

    jout << "  \"timetable\": {"
         << "\"greedySlots\": " << slotsUsedGreedy << ", "
         << "\"greedyMs\": " << greedyMs << ", "
         << "\"backtrackSlotsTried\": " << (slotsUsedGreedy - 1) << ", "
         << "\"backtrackPossible\": " << (fewerSlotsPossible ? "true" : "false") << ", "
         << "\"backtrackStates\": " << backtrackCount << ", "
         << "\"backtrackMs\": " << backtrackMs << "},\n";

    jout << "  \"slots\": [\n";
    {
        bool firstSlot = true;
        map<int, vector<Exam*>> slotExamPtrs;
        for (auto& e : exams) slotExamPtrs[e.slot].push_back(&e);
        for (auto& kv : slotExamPtrs) {
            if (!firstSlot) jout << ",\n";
            firstSlot = false;
            jout << "    {\"slot\": " << kv.first << ", \"exams\": [";
            for (size_t i = 0; i < kv.second.size(); ++i) {
                if (i) jout << ", ";
                jout << "{\"code\": " << jstr(kv.second[i]->code)
                     << ", \"name\": " << jstr(kv.second[i]->name) << "}";
            }
            jout << "]}";
        }
    }
    jout << "\n  ],\n";

    // ---------- 3. Room allocation (priority queue) + seating (backtracking) ----------
    RoomAllocator roomAllocator(rooms);
    map<string, Room> roomById;
    for (auto& r : rooms) roomById[r.id] = r;

    jout << "  \"allocations\": [\n";
    bool firstAlloc = true;

    for (map<int, vector<string>>::iterator sit = slotToExams.begin(); sit != slotToExams.end(); ++sit) {
        int slot = sit->first;
        out << "SLOT " << slot + 1 << " - ROOM & SEATING ALLOCATION\n";
        printSeparator(out);

        long long slotDemand = 0;
        for (const auto& exam : exams)
            if (exam.slot == slot) slotDemand += static_cast<long long>(exam.studentIds.size());
        long long slotCapacity = 0;
        for (const auto& room : rooms) slotCapacity += room.capacity();
        out << "Capacity check: " << slotDemand << " candidates, " << slotCapacity
            << " available seats - "
            << (slotDemand <= slotCapacity ? "SUFFICIENT" : "INSUFFICIENT (some candidates may remain unassigned)")
            << "\n\n";

        map<string, vector<pair<string, vector<string>>>> allocation = roomAllocator.allocate(exams, slot);

        for (map<string, vector<pair<string, vector<string>>>>::iterator eit = allocation.begin();
             eit != allocation.end(); ++eit) {
            const string& examCode = eit->first;
            const vector<pair<string, vector<string>>>& roomAssignments = eit->second;
            out << "Exam " << examCode << ":\n";

            string examName = exams[examIndex[examCode]].name;

            for (size_t ai = 0; ai < roomAssignments.size(); ++ai) {
                const string& roomId = roomAssignments[ai].first;
                const vector<string>& studentIds = roomAssignments[ai].second;
                Room r = roomById[roomId];
                out << "  Room " << roomId << " (" << r.name << ", "
                    << studentIds.size() << "/" << r.capacity() << " seats used)\n";

                map<string, int> batchCounts;
                for (auto& sid : studentIds) batchCounts[studentById[sid].batch]++;

                SeatingAllocator seater(r.rows, r.cols, batchCounts, true);
                bool ok = seater.allocate();
                out << "    Seating (batch-conflict-free): "
                    << (ok ? "SUCCESS" : "FAILED - could not avoid all adjacencies")
                    << " (" << seater.getBacktrackCount() << " states explored)\n";

                auto grid = seater.getSeatGrid();

                if (ok) {
                    out << "    Seat map (batch labels):\n";
                    for (int rr = 0; rr < r.rows; ++rr) {
                        out << "      ";
                        for (int cc = 0; cc < r.cols; ++cc) {
                            string label = grid[rr][cc].empty() ? "----" : grid[rr][cc];
                            out << label << "\t";
                        }
                        out << "\n";
                    }
                }

                // ---- JSON entry for this room ----
                if (!firstAlloc) jout << ",\n";
                firstAlloc = false;

                map<string, vector<string>> namesByBatch;
                for (auto& sid : studentIds) namesByBatch[studentById[sid].batch].push_back(studentById[sid].name);
                map<string, int> cursor;

                jout << "    {\"slot\": " << slot
                     << ", \"examCode\": " << jstr(examCode)
                     << ", \"examName\": " << jstr(examName)
                     << ", \"roomId\": " << jstr(roomId)
                     << ", \"roomName\": " << jstr(r.name)
                     << ", \"rows\": " << r.rows
                     << ", \"cols\": " << r.cols
                     << ", \"success\": " << (ok ? "true" : "false")
                     << ", \"states\": " << seater.getBacktrackCount()
                     << ", \"seats\": [";

                for (int rr = 0; rr < r.rows; ++rr) {
                    if (rr) jout << ", ";
                    jout << "[";
                    for (int cc = 0; cc < r.cols; ++cc) {
                        if (cc) jout << ", ";
                        const string& batch = grid[rr][cc];
                        if (batch.empty()) {
                            jout << "null";
                        } else {
                            int& idx = cursor[batch];
                            string name = (idx < (int)namesByBatch[batch].size()) ? namesByBatch[batch][idx] : batch;
                            idx++;
                            jout << "{\"batch\": " << jstr(batch) << ", \"name\": " << jstr(name) << "}";
                        }
                    }
                    jout << "]";
                }
                jout << "]}";
            }
            out << "\n";
        }
    }
    jout << "\n  ]\n";
    jout << "}\n";

    out.close();
    jout.close();
    cout << "Done. Report written to output/report.txt and output/report.json\n";
    return 0;
}
