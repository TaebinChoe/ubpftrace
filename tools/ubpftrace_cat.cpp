#include "hpc/ubpf_shm_buffer.hpp"
#include "hpc/ubpf_container_writer.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <queue>
#include <sstream>
#include <filesystem>
#include <lz4.h>

using namespace bpftime::hpc;

namespace {

// Fast CRC32 check
uint32_t compute_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

std::string escape_json_string(const std::string &input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

struct DecodedEvent {
    uint64_t timestamp_ns;
    uint16_t rank;
    uint16_t event_type;
    uint32_t node_id;
    std::string payload;
};

struct DecodedChunk {
    ubpf_chunk_header header;
    bool crc_valid;
    uint64_t file_offset;
};

struct DecodedFile {
    std::string filepath;
    uint64_t file_size{0};
    ubpf_file_header header;
    bool has_trailer{false};
    ubpf_file_trailer trailer;
    std::vector<DecodedChunk> chunks;
    std::vector<DecodedEvent> events;
    uint64_t total_uncompressed_bytes{0};
    uint64_t total_compressed_bytes{0};
    uint64_t total_dropped_events{0};
};

bool parse_ubpf_file(const std::string &filepath, DecodedFile &df) {
    df.filepath = filepath;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filepath << std::endl;
        return false;
    }

    df.file_size = static_cast<uint64_t>(file.tellg());
    if (df.file_size < sizeof(ubpf_file_header)) {
        std::cerr << "Error: File size too small for header: " << filepath << std::endl;
        return false;
    }

    // 1. Read static file header (128 bytes)
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(&df.header), sizeof(df.header));
    if (df.header.magic != UBPF_FILE_MAGIC) {
        std::cerr << "Error: Invalid file magic 0x" << std::hex << df.header.magic
                  << " (expected 0x" << UBPF_FILE_MAGIC << ") in " << filepath << std::dec << std::endl;
        return false;
    }

    // 2. Check for trailer at file end
    if (df.file_size >= sizeof(ubpf_file_header) + sizeof(ubpf_file_trailer)) {
        file.seekg(df.file_size - sizeof(ubpf_file_trailer), std::ios::beg);
        ubpf_file_trailer trailer_tmp;
        file.read(reinterpret_cast<char *>(&trailer_tmp), sizeof(trailer_tmp));
        if (trailer_tmp.trailer_magic == UBPF_TRAILER_MAGIC) {
            df.has_trailer = true;
            df.trailer = trailer_tmp;
        }
    }

    // 3. Sequentially read and decompress chunks
    file.seekg(sizeof(ubpf_file_header), std::ios::beg);
    uint64_t end_offset = df.has_trailer ? df.trailer.index_table_offset : df.file_size;

    while (static_cast<uint64_t>(file.tellg()) + sizeof(ubpf_chunk_header) <= end_offset) {
        uint64_t chunk_pos = static_cast<uint64_t>(file.tellg());
        ubpf_chunk_header chdr;
        file.read(reinterpret_cast<char *>(&chdr), sizeof(chdr));

        if (chdr.chunk_magic != UBPF_CHUNK_MAGIC) {
            // Reached padding, index table, or unexpected end
            break;
        }

        std::vector<uint8_t> comp_buf(chdr.compressed_size);
        file.read(reinterpret_cast<char *>(comp_buf.data()), chdr.compressed_size);

        uint32_t calc_crc = compute_crc32(comp_buf.data(), chdr.compressed_size);
        bool crc_ok = (calc_crc == chdr.crc32);

        DecodedChunk dchunk;
        dchunk.header = chdr;
        dchunk.crc_valid = crc_ok;
        dchunk.file_offset = chunk_pos;
        df.chunks.push_back(dchunk);

        df.total_compressed_bytes += chdr.compressed_size;
        df.total_uncompressed_bytes += chdr.uncompressed_size;
        df.total_dropped_events += chdr.dropped_events_count;

        // Decompress payload
        std::vector<uint8_t> uncomp_buf(chdr.uncompressed_size);
        int decomp_len = LZ4_decompress_safe(
            reinterpret_cast<const char *>(comp_buf.data()),
            reinterpret_cast<char *>(uncomp_buf.data()),
            static_cast<int>(chdr.compressed_size),
            static_cast<int>(chdr.uncompressed_size)
        );

        if (decomp_len > 0) {
            size_t offset = 0;
            while (offset + sizeof(ubpf_event_record_header) <= static_cast<size_t>(decomp_len)) {
                auto *rec_hdr = reinterpret_cast<const ubpf_event_record_header *>(uncomp_buf.data() + offset);
                if (rec_hdr->record_len == 0 || offset + rec_hdr->record_len > static_cast<size_t>(decomp_len)) {
                    break;
                }

                DecodedEvent ev;
                ev.timestamp_ns = rec_hdr->timestamp_ns;
                ev.rank = rec_hdr->rank;
                ev.event_type = rec_hdr->event_type;
                ev.node_id = df.header.node_id;

                if (rec_hdr->payload_len > 0) {
                    const char *payload_ptr = reinterpret_cast<const char *>(uncomp_buf.data() + offset + sizeof(ubpf_event_record_header));
                    // Check if null-terminated or string payload
                    size_t str_len = rec_hdr->payload_len;
                    if (str_len > 0 && payload_ptr[str_len - 1] == '\0') {
                        str_len--;
                    }
                    ev.payload = std::string(payload_ptr, str_len);
                }

                df.events.push_back(ev);
                offset += rec_hdr->record_len;
            }
        }
    }

    return true;
}

void print_file_info(const DecodedFile &df) {
    std::cout << "============================================================" << std::endl;
    std::cout << "  UBPFTRACE CONTAINER METADATA: " << df.filepath << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "  Job ID            : " << df.header.job_id << std::endl;
    std::cout << "  Node ID           : " << df.header.node_id << std::endl;
    std::cout << "  Hostname          : " << df.header.hostname << std::endl;
    std::cout << "  File Size         : " << df.file_size << " bytes ("
              << std::fixed << std::setprecision(2) << (df.file_size / 1024.0 / 1024.0) << " MB)" << std::endl;
    std::cout << "  Codec             : " << (df.header.codec_id == CODEC_LZ4 ? "LZ4" : "NONE") << std::endl;
    std::cout << "  Base Monotonic Ts : " << df.header.timestamp_base_ns << " ns" << std::endl;
    std::cout << "  Base Wallclock Ts : " << df.header.wallclock_base_ns << " ns" << std::endl;
    std::cout << "  Total Chunks      : " << df.chunks.size() << std::endl;
    std::cout << "  Total Records     : " << df.events.size() << std::endl;
    std::cout << "  Total Dropped     : " << df.total_dropped_events << std::endl;
    
    double ratio = df.total_uncompressed_bytes > 0
                       ? (100.0 * (1.0 - static_cast<double>(df.total_compressed_bytes) / df.total_uncompressed_bytes))
                       : 0.0;
    std::cout << "  Uncompressed Size : " << df.total_uncompressed_bytes << " bytes" << std::endl;
    std::cout << "  Compressed Size   : " << df.total_compressed_bytes << " bytes (Savings: "
              << std::fixed << std::setprecision(1) << ratio << "%)" << std::endl;

    std::cout << "\nChunk Details:" << std::endl;
    std::cout << "  " << std::left << std::setw(8) << "Chunk#"
              << std::setw(12) << "Offset"
              << std::setw(14) << "UncompSize"
              << std::setw(14) << "CompSize"
              << std::setw(10) << "Records"
              << std::setw(10) << "Dropped"
              << std::setw(10) << "CRC32" << std::endl;
    std::cout << "  ----------------------------------------------------------------------" << std::endl;

    for (size_t i = 0; i < df.chunks.size(); ++i) {
        const auto &c = df.chunks[i];
        std::cout << "  " << std::left << std::setw(8) << i
                  << std::setw(12) << c.file_offset
                  << std::setw(14) << c.header.uncompressed_size
                  << std::setw(14) << c.header.compressed_size
                  << std::setw(10) << c.header.record_count
                  << std::setw(10) << c.header.dropped_events_count
                  << (c.crc_valid ? "OK" : "CORRUPT") << std::endl;
    }
    std::cout << "============================================================" << std::endl;
}

void dump_events(const std::vector<DecodedFile> &files, std::ostream &os = std::cout) {
    for (const auto &df : files) {
        for (const auto &ev : df.events) {
            double sec = static_cast<double>(ev.timestamp_ns) / 1e9;
            os << "[" << std::fixed << std::setprecision(6) << sec << "s] "
               << "[Node " << ev.node_id << "] "
               << "[Rank " << ev.rank << "] "
               << "[Event " << ev.event_type << "] "
               << ev.payload << std::endl;
        }
    }
}

void merge_and_dump_events(const std::vector<DecodedFile> &files, std::ostream &os = std::cout) {
    struct Cursor {
        size_t file_idx;
        size_t event_idx;
        uint64_t timestamp_ns;

        bool operator>(const Cursor &other) const {
            return timestamp_ns > other.timestamp_ns;
        }
    };

    std::priority_queue<Cursor, std::vector<Cursor>, std::greater<Cursor>> pq;

    for (size_t f = 0; f < files.size(); ++f) {
        if (!files[f].events.empty()) {
            pq.push({f, 0, files[f].events[0].timestamp_ns});
        }
    }

    while (!pq.empty()) {
        Cursor cur = pq.top();
        pq.pop();

        const auto &ev = files[cur.file_idx].events[cur.event_idx];
        double sec = static_cast<double>(ev.timestamp_ns) / 1e9;
        os << "[" << std::fixed << std::setprecision(6) << sec << "s] "
           << "[Node " << ev.node_id << "] "
           << "[Rank " << ev.rank << "] "
           << "[Event " << ev.event_type << "] "
           << ev.payload << std::endl;

        if (cur.event_idx + 1 < files[cur.file_idx].events.size()) {
            pq.push({cur.file_idx, cur.event_idx + 1, files[cur.file_idx].events[cur.event_idx + 1].timestamp_ns});
        }
    }
}

void export_chrome_tracing(const std::vector<DecodedFile> &files, const std::string &output_json) {
    std::ofstream out(output_json);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot write to " << output_json << std::endl;
        return;
    }

    out << "{\n  \"traceEvents\": [\n";
    bool first = true;

    for (const auto &df : files) {
        for (const auto &ev : df.events) {
            if (!first) {
                out << ",\n";
            }
            first = false;

            double ts_us = static_cast<double>(ev.timestamp_ns) / 1000.0;
            std::string ev_name = ev.payload.empty() ? "trace_event" : ev.payload.substr(0, ev.payload.find('('));
            if (ev_name.empty()) ev_name = "event_" + std::to_string(ev.event_type);

            out << "    {"
                << "\"name\": \"" << escape_json_string(ev_name) << "\", "
                << "\"cat\": \"ubpftrace\", "
                << "\"ph\": \"i\", "
                << "\"ts\": " << std::fixed << std::setprecision(3) << ts_us << ", "
                << "\"pid\": " << ev.node_id << ", "
                << "\"tid\": " << ev.rank << ", "
                << "\"args\": {"
                << "\"event_type\": " << ev.event_type << ", "
                << "\"payload\": \"" << escape_json_string(ev.payload) << "\""
                << "}}";
        }
    }

    out << "\n  ],\n  \"displayTimeUnit\": \"ms\"\n}\n";
    std::cout << "Exported Chrome Trace Event format to: " << output_json << std::endl;
}

void print_help(const char *prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS] <file1.ubpf / dir> [file2.ubpf ...]\n"
              << "\nOptions:\n"
              << "  -i, --info, --summary    Display container file metadata and chunk stats\n"
              << "  -d, --dump               Print trace records in chronological order (default)\n"
              << "  -m, --merge              Merge multiple node container streams chronologically\n"
              << "  -o, --output <file.txt>  Direct decoded output to specified file\n"
              << "  -c, --chrome <file.json> Export trace records to Chrome Tracing JSON format\n"
              << "  -h, --help               Display this help message\n";
}

} // anonymous namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    bool opt_info = false;
    bool opt_merge = false;
    std::string chrome_out;
    std::string output_file;
    std::vector<std::string> raw_inputs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-i" || arg == "--info" || arg == "--summary") {
            opt_info = true;
        } else if (arg == "-d" || arg == "--dump") {
            // default
        } else if (arg == "-m" || arg == "--merge") {
            opt_merge = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        } else if (arg == "-c" || arg == "--chrome" || arg == "--format") {
            if (i + 1 < argc) {
                chrome_out = argv[++i];
                if (chrome_out == "chrome" && i + 1 < argc && argv[i + 1][0] != '-') {
                    chrome_out = argv[++i];
                }
                if (chrome_out == "chrome") {
                    chrome_out = "tracing.json";
                }
            } else {
                chrome_out = "tracing.json";
            }
        } else if (!arg.empty() && arg[0] != '-') {
            raw_inputs.push_back(arg);
        }
    }

    std::vector<std::string> input_files;
    for (const auto &p : raw_inputs) {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            for (const auto &entry : std::filesystem::directory_iterator(p, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".ubpf") {
                    input_files.push_back(entry.path().string());
                }
            }
        } else {
            input_files.push_back(p);
        }
    }

    // Sort filenames for deterministic order
    std::sort(input_files.begin(), input_files.end());

    if (input_files.empty()) {
        std::cerr << "Error: No input .ubpf files found.\n";
        print_help(argv[0]);
        return 1;
    }

    std::vector<DecodedFile> decoded_files;
    for (const auto &f : input_files) {
        DecodedFile df;
        if (parse_ubpf_file(f, df)) {
            decoded_files.push_back(std::move(df));
        }
    }

    if (decoded_files.empty()) {
        std::cerr << "Error: No valid .ubpf files could be decoded.\n";
        return 1;
    }

    if (opt_info) {
        for (const auto &df : decoded_files) {
            print_file_info(df);
        }
        return 0;
    }

    if (!chrome_out.empty()) {
        export_chrome_tracing(decoded_files, chrome_out);
        return 0;
    }

    std::ofstream out_file_stream;
    std::ostream *target_stream = &std::cout;
    if (!output_file.empty()) {
        out_file_stream.open(output_file);
        if (!out_file_stream.is_open()) {
            std::cerr << "Error: Cannot open output file " << output_file << std::endl;
            return 1;
        }
        target_stream = &out_file_stream;
    }

    if (opt_merge || decoded_files.size() > 1) {
        merge_and_dump_events(decoded_files, *target_stream);
    } else {
        dump_events(decoded_files, *target_stream);
    }

    if (!output_file.empty()) {
        std::cout << "Successfully written merged trace to: " << output_file << std::endl;
    }

    return 0;
}
