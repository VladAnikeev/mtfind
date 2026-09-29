#include "mtfind_core.h"

#include <algorithm>
#include <string_view>

AnchorInfo findAnchor(const std::string& mask)
{
    for (std::size_t i = 0; i < mask.size(); ++i)
    {
        if (mask[i] != '?')
        {
            return AnchorInfo{ i, mask[i], false };
        }
    }
    return AnchorInfo{ std::string::npos, 0, true };
}

std::uint64_t calcPlannedChunkCount(
    std::uint64_t fileSize,
    unsigned threadCount,
    std::uint64_t desiredChunkBytes)
{
    std::uint64_t plannedChunkCount = fileSize / desiredChunkBytes;

    plannedChunkCount = std::max<std::uint64_t>(plannedChunkCount, threadCount);
    plannedChunkCount = std::min<std::uint64_t>(plannedChunkCount, static_cast<std::uint64_t>(threadCount) * 64);
    plannedChunkCount = std::max<std::uint64_t>(plannedChunkCount, 1);

    return plannedChunkCount;
}

std::vector<std::uint64_t> buildChunkBoundaries(
    const char* data,
    std::uint64_t fileSize,
    std::uint64_t plannedChunkCount)
{
    std::vector<std::uint64_t> chunkBoundaries(plannedChunkCount + 1);
    chunkBoundaries[0] = 0;
    chunkBoundaries[plannedChunkCount] = fileSize;

    std::uint64_t k = 1;
    for (; k < plannedChunkCount; ++k)
    {
        std::uint64_t nominal = (fileSize * k) / plannedChunkCount;
        if (nominal >= fileSize)
        {
            break;
        }

        std::string_view rest(data + nominal, fileSize - nominal);
        std::size_t pos = rest.find('\n');

        if (pos == std::string_view::npos)
        {
            break;
        }
        chunkBoundaries[k] = nominal + pos + 1;
    }

    std::fill(chunkBoundaries.begin() + static_cast<std::ptrdiff_t>(k), chunkBoundaries.end(), fileSize);

    chunkBoundaries.erase(std::unique(chunkBoundaries.begin(), chunkBoundaries.end()), chunkBoundaries.end());
    return chunkBoundaries;
}

ChunkSearchResult searchInChunk(
    const char* data,
    std::uint64_t begin,
    std::uint64_t end,
    const std::string& mask,
    const AnchorInfo& anchor)
{
    const std::size_t maskSize = mask.size();

    std::uint64_t currentLineNumber = 1;   // номер строки ВНУТРИ этого чанка (с 1)
    std::uint64_t currentLineOffset = begin; // абсолютное смещение начала текущей строки
    std::uint64_t newlineCount = 0;        // сколько '\n' встретилось во всём диапазоне [begin, end)
    std::uint64_t searchPos = begin;       // откуда продолжать поиск следующего кандидата

    std::vector<MatchRecord> matches;

    while (searchPos + maskSize <= end)
    {
        std::uint64_t matchStart = searchPos;

        // Ищем якорь (первый не-'?' символ маски), чтобы не проверять каждую позицию посимвольно.
        if (!anchor.isAllWildcards)
        {
            std::uint64_t anchorSearchFrom = searchPos + anchor.anchorPos;
            if (anchorSearchFrom >= end)
            {
                break;
            }

            std::string_view window(data + anchorSearchFrom, end - anchorSearchFrom);
            std::size_t anchorRelativePos = window.find(anchor.anchorChar);

            if (anchorRelativePos == std::string_view::npos)
            {
                break;
            }

            std::uint64_t anchorOffset = anchorSearchFrom + anchorRelativePos;
            matchStart = anchorOffset - anchor.anchorPos;
            if (matchStart + maskSize > end)
            {
                break;
            }
        }

        // Подтягиваем номер строки и смещение начала строки до matchStart.
        while (currentLineOffset < matchStart)
        {
            std::string_view window(data + currentLineOffset, matchStart - currentLineOffset);
            std::size_t newlineRelativePos = window.find('\n');

            if (newlineRelativePos == std::string_view::npos)
            {
                break;
            }

            currentLineOffset = currentLineOffset + newlineRelativePos + 1;
            ++currentLineNumber;
            ++newlineCount;
        }

        // Посимвольная проверка совпадения с маской.
        bool isMatch = true;
        for (std::size_t maskIndex = 0; maskIndex < maskSize; ++maskIndex)
        {
            char textChar = data[matchStart + maskIndex];
            if (textChar == '\n' || (mask[maskIndex] != '?' && mask[maskIndex] != textChar))
            {
                isMatch = false;
                break;
            }
        }

        if (isMatch)
        {
            matches.push_back({ currentLineNumber, matchStart - currentLineOffset + 1, matchStart });
            searchPos = matchStart + maskSize; // без пересечений
        }
        else
        {
            searchPos = matchStart + 1;
        }
    }

    // Досчитываем '\n' до конца чанка, нужно для корректного prefix sum между чанками.
    while (currentLineOffset < end)
    {
        std::string_view window(data + currentLineOffset, end - currentLineOffset);
        std::size_t newlineRelativePos = window.find('\n');

        if (newlineRelativePos == std::string_view::npos)
        {
            break;
        }
        currentLineOffset = currentLineOffset + newlineRelativePos + 1;
        ++newlineCount;
    }

    return ChunkSearchResult{ std::move(matches), newlineCount };
}