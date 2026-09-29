#include "mtfind_core.h"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <thread>

// ---------- findAnchor ----------

TEST(FindAnchor, PicksFirstNonWildcard)
{
    AnchorInfo anchor = findAnchor("??a?c");
    EXPECT_FALSE(anchor.isAllWildcards);
    EXPECT_EQ(anchor.anchorPos, 2u);
    EXPECT_EQ(anchor.anchorChar, 'a');
}

TEST(FindAnchor, AllWildcardsHasNoAnchor)
{
    AnchorInfo anchor = findAnchor("?????");
    EXPECT_TRUE(anchor.isAllWildcards);
}

TEST(FindAnchor, SingleFixedCharAtStart)
{
    AnchorInfo anchor = findAnchor("a??");
    EXPECT_FALSE(anchor.isAllWildcards);
    EXPECT_EQ(anchor.anchorPos, 0u);
    EXPECT_EQ(anchor.anchorChar, 'a');
}

// ---------- calcPlannedChunkCount ----------

TEST(CalcPlannedChunkCount, NeverLessThanThreadCount)
{
    // Маленький файл, много потоков, чанков не меньше, чем потоков.
    auto count = calcPlannedChunkCount(1000, 8);
    EXPECT_GE(count, 8u);
}

TEST(CalcPlannedChunkCount, CappedAtThreadCountTimes64)
{
    // Огромный файл, верхняя граница не должна быть превышена.
    auto count = calcPlannedChunkCount(1'000'000'000'000ull, 4, 1); // маленький размер чанка, чтобы форсировать верхний предел
    EXPECT_LE(count, 4u * 64u);
}

TEST(CalcPlannedChunkCount, AtLeastOne)
{
    auto count = calcPlannedChunkCount(0, 0);
    EXPECT_GE(count, 1u);
}

// ---------- buildChunkBoundaries ----------

TEST(BuildChunkBoundaries, BoundariesAlwaysAfterNewline)
{
    std::string text = "aaa\nbbbb\nccccc\nd\n";
    auto boundaries = buildChunkBoundaries(text.data(), text.size(), 3);

    ASSERT_GE(boundaries.size(), 2u);
    EXPECT_EQ(boundaries.front(), 0u);
    EXPECT_EQ(boundaries.back(), text.size());

    // Каждая внутренняя граница (кроме первой и последней) должна стоять сразу после '\n'.
    for (std::size_t k = 1; k + 1 < boundaries.size(); ++k)
    {
        std::uint64_t b = boundaries[k];
        ASSERT_GT(b, 0u);
        EXPECT_EQ(text[b - 1], '\n');
    }
}

TEST(BuildChunkBoundaries, NoTrailingNewlineStillCoversWholeFile)
{
    std::string text = "onebigline_without_any_newline_at_all";
    auto boundaries = buildChunkBoundaries(text.data(), text.size(), 4);

    EXPECT_EQ(boundaries.front(), 0u);
    EXPECT_EQ(boundaries.back(), text.size());
    // Без единого \n все внутренние границы схлопнутся в конец файла, после unique останется 2 элемента.
    EXPECT_EQ(boundaries.size(), 2u);
}

// ---------- searchInChunk ----------

TEST(SearchInChunk, FindsSimpleMatch)
{
    std::string text = "aaa\nabc\nxyz\n";
    AnchorInfo anchor = findAnchor("a?c");

    auto result = searchInChunk(text.data(), 0, text.size(), "a?c", anchor);

    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].relLine, 2u);
    EXPECT_EQ(result.matches[0].column, 1u);
    EXPECT_EQ(result.matches[0].offset, 4u); // "abc" начинается сразу после первого '\n'
}

TEST(SearchInChunk, RejectsMatchCrossingNewline)
{
    // "a" в конце одной строки, "c" в начале следующей, не должно считаться совпадением.
    std::string text = "a\nc";
    AnchorInfo anchor = findAnchor("a?c");

    auto result = searchInChunk(text.data(), 0, text.size(), "a?c", anchor);

    EXPECT_TRUE(result.matches.empty());
}

TEST(SearchInChunk, NonOverlappingMatches)
{
    // "aaaa" при маске "aa" должно дать ровно 2 непересекающихся совпадения, а не 3.
    std::string text = "aaaa\n";
    AnchorInfo anchor = findAnchor("aa");

    auto result = searchInChunk(text.data(), 0, text.size(), "aa", anchor);

    ASSERT_EQ(result.matches.size(), 2u);
    EXPECT_EQ(result.matches[0].offset, 0u);
    EXPECT_EQ(result.matches[1].offset, 2u);
}

TEST(SearchInChunk, AllWildcardMaskSplitsWithoutOverlap)
{
    std::string text = "abcdef\n";
    AnchorInfo anchor = findAnchor("???"); // isAllWildcards == true

    auto result = searchInChunk(text.data(), 0, text.size(), "???", anchor);

    ASSERT_EQ(result.matches.size(), 2u); // "abc" и "def"
    EXPECT_EQ(result.matches[0].offset, 0u);
    EXPECT_EQ(result.matches[1].offset, 3u);
}

TEST(SearchInChunk, CaseSensitive)
{
    std::string text = "ABC\n";
    AnchorInfo anchor = findAnchor("abc");

    auto result = searchInChunk(text.data(), 0, text.size(), "abc", anchor);

    EXPECT_TRUE(result.matches.empty());
}

TEST(SearchInChunk, ColumnAndLineAcrossMultipleLines)
{
    std::string text = "xxxx\nxxxx\nxa?cx\n";
    AnchorInfo anchor = findAnchor("a?c");

    auto result = searchInChunk(text.data(), 0, text.size(), "a?c", anchor);

    ASSERT_EQ(result.matches.size(), 1u);
    EXPECT_EQ(result.matches[0].relLine, 3u);
    EXPECT_EQ(result.matches[0].column, 2u); // "xa?cx" -> 'a' на позиции 2 (с 1)
}

TEST(SearchInChunk, NewlineCountCoversWholeChunkIncludingTail)
{
    // Совпадений нет, но '\n' в чанке 3 штуки, должны быть учтены даже без единого найденного кандидата.
    std::string text = "xxx\nyyy\nzzz\n";
    AnchorInfo anchor = findAnchor("QQQ"); // такого символа в тексте нет

    auto result = searchInChunk(text.data(), 0, text.size(), "QQQ", anchor);

    EXPECT_TRUE(result.matches.empty());
    EXPECT_EQ(result.newlineCount, 3u);
}

TEST(SearchInChunk, EmptyRangeGivesNoMatches)
{
    std::string text = "abc";
    AnchorInfo anchor = findAnchor("abc");

    auto result = searchInChunk(text.data(), 0, 0, "abc", anchor);

    EXPECT_TRUE(result.matches.empty());
    EXPECT_EQ(result.newlineCount, 0u);
}


// ---------- Full pipeline test (as in main.cpp) ----------

namespace
{

// Повторяет логику main.cpp: разбиение на чанки, многопоточный поиск,
// префикс-сумма строк, сборка всех совпадений.
std::vector<MatchRecord> runFullPipeline(
    const char* data,
    std::uint64_t fileSize,
    const std::string& mask,
    unsigned threadCount)
{
    const std::size_t maskSize = mask.size();

    if (fileSize == 0 || maskSize > fileSize)
    {
        return {};
    }

    const std::uint64_t plannedChunkCount = calcPlannedChunkCount(fileSize, threadCount);
    const std::vector<std::uint64_t> chunkBoundaries =
        buildChunkBoundaries(data, fileSize, plannedChunkCount);
    const std::uint64_t chunkCount = chunkBoundaries.size() - 1;

    const AnchorInfo anchor = findAnchor(mask);

    std::vector<ChunkSearchResult> results(chunkCount);
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
                results[idx] = searchInChunk(
                    data, chunkBoundaries[idx], chunkBoundaries[idx + 1], mask, anchor);
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

    std::vector<std::uint64_t> linePrefix(chunkCount + 1, 0);
    for (std::uint64_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
    {
        linePrefix[chunkIdx + 1] = linePrefix[chunkIdx] + results[chunkIdx].newlineCount;
    }

    std::vector<MatchRecord> allMatches;
    for (std::uint64_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
    {
        for (const auto& match : results[chunkIdx].matches)
        {
            allMatches.push_back(
                { linePrefix[chunkIdx] + match.relLine, match.column, match.offset });
        }
    }

    return allMatches;
}

// Формирует вывод в том же формате, что и main.cpp.
std::string formatOutput(const char* data, const std::string& mask,
                         const std::vector<MatchRecord>& matches)
{
    std::ostringstream out;
    out << matches.size() << '\n';
    for (const auto& match : matches)
    {
        out << match.relLine << ' ' << match.column << ' '
            << std::string_view(data + match.offset, mask.size()) << '\n';
    }
    return out.str();
}

} // namespace

TEST(FullPipeline, AssignmentExampleSingleThread)
{
    std::string text =
        "I've paid my dues\n"
        "Time after time.\n"
        "I've done my sentence\n"
        "But committed no crime.\n"
        "And bad mistakes ?\n"
        "I've made a few.\n"
        "I've had my share of sand kicked in my face\n"
        "But I've come through.\n";

    auto matches = runFullPipeline(text.data(), text.size(), "?ad", 1);

    const std::string expected =
        "3\n"
        "5 5 bad\n"
        "6 6 mad\n"
        "7 6 had\n";

    EXPECT_EQ(formatOutput(text.data(), "?ad", matches), expected);
}

TEST(FullPipeline, AssignmentExampleMultiThread)
{
    std::string text =
        "I've paid my dues\n"
        "Time after time.\n"
        "I've done my sentence\n"
        "But committed no crime.\n"
        "And bad mistakes ?\n"
        "I've made a few.\n"
        "I've had my share of sand kicked in my face\n"
        "But I've come through.\n";

    // Прогоняем с разным числом потоков — результат должен быть идентичен.
    for (unsigned threads : { 1u, 2u, 4u, 8u })
    {
        auto matches = runFullPipeline(text.data(), text.size(), "?ad", threads);

        const std::string expected =
            "3\n"
            "5 5 bad\n"
            "6 6 mad\n"
            "7 6 had\n";

        EXPECT_EQ(formatOutput(text.data(), "?ad", matches), expected)
            << "failed with threadCount=" << threads;
    }
}