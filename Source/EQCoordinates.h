#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
/** Mapeo logarítmico estándar de frecuencia <-> posición X, y lineal de
    ganancia <-> posición Y, usados tanto por SpectrumDisplayComponent como
    por BandHandleComponent para que ambos coincidan exactamente. */
namespace EQCoordinates
{
    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    constexpr float minDb   = -24.0f;
    constexpr float maxDb   = 24.0f;

    inline float freqToX (float freqHz, float width)
    {
        const auto logMin = std::log10 (minFreq);
        const auto logMax = std::log10 (maxFreq);
        const auto logF   = std::log10 (juce::jlimit (minFreq, maxFreq, freqHz));
        return width * (logF - logMin) / (logMax - logMin);
    }

    inline float xToFreq (float x, float width)
    {
        const auto logMin = std::log10 (minFreq);
        const auto logMax = std::log10 (maxFreq);
        const auto proportion = juce::jlimit (0.0f, 1.0f, x / width);
        return std::pow (10.0f, logMin + proportion * (logMax - logMin));
    }

    inline float dbToY (float gainDb, float height)
    {
        const auto proportion = (gainDb - minDb) / (maxDb - minDb);
        return height * (1.0f - juce::jlimit (0.0f, 1.0f, proportion));
    }

    inline float yToDb (float y, float height)
    {
        const auto proportion = 1.0f - juce::jlimit (0.0f, 1.0f, y / height);
        return minDb + proportion * (maxDb - minDb);
    }
}
