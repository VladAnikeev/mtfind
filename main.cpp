#include "mtfind_core.h"

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/program_options.hpp>

#include <atomic>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace po = boost::program_options;

inline constexpr int MAX_SIZE_MASK = 100'000;

int main(int argc, char* argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show this help message")
        ("file,f", po::value<std::string>(), "input file")
        ("mask,m", po::value<std::string>(), "search mask, max length 100'000");

    po::variables_map vm;

    try
    {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    }
    catch (const po::error& e)
    {
        std::cerr << "Error: options " << e.what() << '\n' << desc << '\n';
        return 1;
    }

    if (vm.count("help"))
    {
        std::cout << desc << '\n';
        return 0;
    }
    if (!vm.count("file") || !vm.count("mask"))
    {
        std::cerr << "Error: too few options\n" << desc << '\n';
        return 1;
    }

    const std::string filename = vm["file"].as<std::string>();
    const std::string mask = vm["mask"].as<std::string>();
    const std::size_t maskSize = mask.size();

    if (mask.empty() || maskSize > MAX_SIZE_MASK)
    {
        std::cerr << "Error: the mask length must be between 1 and 100000 characters.\n";
        return 1;
    }
    if (mask.find('\n') != std::string::npos)
    {
        std::cerr << "Error: mask must not contain a newline character.\n";
        return 1;
    }

    boost::iostreams::mapped_file_source mmapFile;
    try
    {
        mmapFile.open(filename);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: failed to open file. " << e.what() << '\n';
        return 1;
    }

    if (!mmapFile.is_open())
    {
        std::cerr << "Error: failed to open file.\n";
        return 1;
    }

    const char* data = mmapFile.data();
    const std::uint64_t fileSize = mmapFile.size();

    if (fileSize == 0 || maskSize > fileSize)
    {
        std::cout << 0 << '\n';
        return 0;
    }

    unsigned threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0)
    {
        threadCount = 2;
    }

    const std::uint64_t plannedChunkCount = calcPlannedChunkCount(fileSize, threadCount);
    const std::vector<std::uint64_t> chunkBoundaries = buildChunkBoundaries(data, fileSize, plannedChunkCount);
    const std::uint64_t chunkCount = chunkBoundaries.size() - 1;

    const AnchorInfo anchor = findAnchor(mask);

    // Поток пишет в свой чанк в векторе, по этому нет гонки данных 
    std::vector<ChunkSearchResult> results(chunkCount);

    //! Атомарная переменная не вызывает гонку данных
    //! https://ru.cppreference.net/cpp/language/multithread.html
    std::atomic<std::uint64_t> nextChunk{ 0 };

    auto worker = [&]()
        {
            while (true)
            {
                std::uint64_t idx = nextChunk.fetch_add(1);
                if (idx >= chunkCount)
                {
                    break;
                }

                results[idx] = searchInChunk(data, chunkBoundaries[idx], chunkBoundaries[idx + 1], mask, anchor);
            }
        };

    std::vector<std::thread> threads;
    unsigned numWorkers = std::min<unsigned>(threadCount, static_cast<unsigned>(chunkCount));
    for (unsigned t = 0; t < numWorkers; ++t)
    {
        threads.emplace_back(worker);
    }
    for (auto& th : threads)
    {
        th.join();
    }

    // Абсолютная нумерация строк: prefix sum по количеству строк в каждом чанке.
    std::vector<std::uint64_t> linePrefix(chunkCount + 1, 0);
    for (std::uint64_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
    {
        linePrefix[chunkIdx + 1] = linePrefix[chunkIdx] + results[chunkIdx].newlineCount;
    }

    std::uint64_t totalMatches = 0;
    for (const auto& chunkResult : results)
    {
        totalMatches += chunkResult.matches.size();
    }

    std::vector<MatchRecord> allMatches;
    allMatches.reserve(totalMatches);

    // Уже нумерация всех совпадений 
    for (std::uint64_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
    {
        for (const auto& match : results[chunkIdx].matches)
        {
            allMatches.push_back({ linePrefix[chunkIdx] + match.relLine, match.column, match.offset });
        }
    }

    std::cout << allMatches.size() << '\n';
    for (const auto& match : allMatches)
    {
        std::cout << match.relLine << ' ' << match.column << ' '
                   << std::string_view(data + match.offset, maskSize) << '\n';
    }

    return 0;
}