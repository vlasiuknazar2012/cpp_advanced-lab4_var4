#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <random>
#include <functional>

const int M = 3;
const int TOTAL_OPS = 1000000;
const int RUNS = 5;

class DataStruct {
    struct alignas(64) Field {
        int value = 0;
        mutable std::shared_mutex mtx;
    };
    Field fields[M];

public:
    void set(int index, int value) {
        if (index < 0 || index >= M) return;
        std::unique_lock<std::shared_mutex> lock(fields[index].mtx);
        fields[index].value = value;
    }

    int get(int index) const {
        if (index < 0 || index >= M) return 0;
        std::shared_lock<std::shared_mutex> lock(fields[index].mtx);
        return fields[index].value;
    }

    operator std::string() const {
        std::shared_lock<std::shared_mutex> l0(fields[0].mtx);
        std::shared_lock<std::shared_mutex> l1(fields[1].mtx);
        std::shared_lock<std::shared_mutex> l2(fields[2].mtx);
        return std::to_string(fields[0].value) + " " +
            std::to_string(fields[1].value) + " " +
            std::to_string(fields[2].value);
    }
};

enum class CmdType { READ, WRITE, STRING };

struct Action {
    CmdType cmd = CmdType::STRING;
    int field = 0;
    int value = 0;
};

void generateFile(const std::string& filename, int count, const int weights[7]) {
    std::ofstream out(filename);
    std::mt19937 gen(42);
    std::discrete_distribution<> dist(weights, weights + 7);
    for (int i = 0; i < count; ++i) {
        int op = dist(gen);
        if (op == 6) out << "string\n";
        else if (op % 2 == 0) out << "read " << op / 2 << "\n";
        else out << "write " << op / 2 << " " << gen() % 100 << "\n";
    }
}

Action* loadActions(const std::string& filename, int count) {
    std::ifstream in(filename);
    if (!in) {
        std::cout << "Cannot open " << filename << "\n";
        return nullptr;
    }
    Action* actions = new Action[count];
    std::string cmd;
    int i = 0;
    while (i < count && in >> cmd) {
        if (cmd == "string") {
            actions[i].cmd = CmdType::STRING;
        }
        else if (cmd == "read") {
            actions[i].cmd = CmdType::READ;
            in >> actions[i].field;
        }
        else if (cmd == "write") {
            actions[i].cmd = CmdType::WRITE;
            in >> actions[i].field >> actions[i].value;
        }
        i++;
    }
    return actions;
}

void worker(DataStruct& ds, const Action* actions, int start, int end, long long& result) {
    long long local = 0;
    for (int i = start; i < end; ++i) {
        if (actions[i].cmd == CmdType::READ) {
            local += ds.get(actions[i].field);
        }
        else if (actions[i].cmd == CmdType::WRITE) {
            ds.set(actions[i].field, actions[i].value);
        }
        else {
            std::string s = ds;
            local += s.size();
        }
    }
    result = local;
}

double runTest(const Action* actions, int total_ops, int threads_count) {
    DataStruct ds;
    std::thread* threads = new std::thread[threads_count];
    long long* results = new long long[threads_count]();
    int ops_per_thread = total_ops / threads_count;

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < threads_count; ++i) {
        int s = i * ops_per_thread;
        int e = (i == threads_count - 1) ? total_ops : s + ops_per_thread;
        threads[i] = std::thread(worker, std::ref(ds), actions, s, e, std::ref(results[i]));
    }
    for (int i = 0; i < threads_count; ++i) {
        threads[i].join();
    }

    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> ms = end_time - start_time;

    delete[] threads;
    delete[] results;
    return ms.count();
}

int main() {
    int w_a[7] = { 15, 1, 50, 3, 5, 1, 25 };
    int w_b[7] = { 1, 1, 1, 1, 1, 1, 1 };
    int w_c[7] = { 1, 90, 1, 3, 1, 3, 1 };

    std::cout << "Generating files..." << std::endl;
    generateFile("file_a.txt", TOTAL_OPS, w_a);
    generateFile("file_b.txt", TOTAL_OPS, w_b);
    generateFile("file_c.txt", TOTAL_OPS, w_c);

    std::cout << "Loading files..." << std::endl;
    const char* names[3] = { "A (variant 4)", "B (equal)", "C (write-heavy)" };
    const char* files[3] = { "file_a.txt", "file_b.txt", "file_c.txt" };
    Action* acts[3] = { nullptr, nullptr, nullptr };
    for (int f = 0; f < 3; ++f) {
        acts[f] = loadActions(files[f], TOTAL_OPS);
        if (!acts[f]) {
            for (int k = 0; k < f; ++k) delete[] acts[k];
            return 1;
        }
    }

    std::cout << "Measuring (" << RUNS << " runs each, please wait)..." << std::endl;
    double table[3][3];
    for (int f = 0; f < 3; ++f) {
        for (int t = 1; t <= 3; ++t) {
            runTest(acts[f], TOTAL_OPS, t);
            double sum = 0;
            for (int r = 0; r < RUNS; ++r) {
                sum += runTest(acts[f], TOTAL_OPS, t);
            }
            table[f][t - 1] = sum / RUNS;
            std::cout << "File " << names[f] << ", " << t << " thread(s): "
                << std::fixed << std::setprecision(2) << table[f][t - 1] << " ms" << std::endl;
        }
    }

    std::cout << "\nAverage time over " << RUNS << " runs (ms), "
        << TOTAL_OPS << " operations:\n";
    std::cout << std::left << std::setw(18) << "File"
        << std::setw(14) << "1 thread" << std::setw(14) << "2 threads"
        << std::setw(14) << "3 threads" << "\n";
    for (int f = 0; f < 3; ++f) {
        std::cout << std::left << std::setw(18) << names[f];
        for (int t = 0; t < 3; ++t)
            std::cout << std::setw(14) << std::fixed << std::setprecision(2) << table[f][t];
        std::cout << "\n";
    }

    for (int f = 0; f < 3; ++f) delete[] acts[f];
    return 0;
}