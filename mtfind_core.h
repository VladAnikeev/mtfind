#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MatchRecord
{
    std::uint64_t relLine; // номер строки внутри чанка (с 1)
    std::uint64_t column;  // позиция в строке (с 1)
    std::uint64_t offset;  // абсолютное смещение в файле
};

struct AnchorInfo
{
    std::size_t anchorPos;   // позиция первого не-'?' символа в маске
    char anchorChar;         // сам этот символ (не используется, если isAllWildcards)
    bool isAllWildcards;     // true, если вся маска состоит из '?'
};

struct ChunkSearchResult
{
    std::vector<MatchRecord> matches; // найденные совпадения, в порядке следования в чанке
    std::uint64_t newlineCount;       // сколько '\n' встретилось во всём чанке [begin, end)
};

// Находит первый не-'?' символ маски, используется для ускорения поиска.
AnchorInfo findAnchor(const std::string& mask);

// Считает "желаемое" число чанков: не меньше числа потоков и не больше threadCount*64,
// но не меньше 1 (защита от вырожденных случаев).
std::uint64_t calcPlannedChunkCount(
    std::uint64_t fileSize,
    unsigned threadCount,
    std::uint64_t desiredChunkBytes = 8ull * 1024 * 1024);

// Строит границы чанков по данным [0, fileSize), выровненные на конец строки ('\n').
// Итоговое число чанков может быть меньше plannedChunkCount (дубликаты схлопываются).
std::vector<std::uint64_t> buildChunkBoundaries(
    const char* data,
    std::uint64_t fileSize,
    std::uint64_t plannedChunkCount);

// Ищет все непересекающиеся вхождения маски в диапазоне [begin, end) данных data.
// Вхождение не может пересекать '\n'. relLine в результате, номер строки ВНУТРИ
// диапазона (начиная с 1 от begin), не глобальный, пересчёт в main.
ChunkSearchResult searchInChunk(
    const char* data,
    std::uint64_t begin,
    std::uint64_t end,
    const std::string& mask,
    const AnchorInfo& anchor);
