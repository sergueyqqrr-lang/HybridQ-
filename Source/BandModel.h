#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/** Formas de filtro disponibles por banda (sección 2.4 del brief).
    TiltShelf se implementa como low-shelf + high-shelf complementarios
    (ver BandProcessor) en vez de como una única sección biquad.
*/
enum class FilterType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    numTypes
};

inline juce::StringArray getFilterTypeChoices()
{
    return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
             "Notch", "Band Pass", "Tilt" };
}

//==============================================================================
/** A qué "lane" de señal afecta la banda. Stereo procesa L y R con la misma
    forma de filtro pero instancias independientes; Mid/Side requieren
    encode/decode M-S alrededor de la banda; Left/Right afectan solo un canal.
*/
enum class ChannelMode
{
    Stereo = 0,
    Mid,
    Side,
    Left,
    Right,
    numModes
};

inline juce::StringArray getChannelModeChoices()
{
    return { "Stereo", "Mid", "Side", "Left", "Right" };
}

//==============================================================================
/** Número máximo de bandas soportadas (sección 2.4: "hasta 24 bandas").
    Todas los parámetros de las 24 bandas se crean desde el arranque
    (patrón estándar en JUCE: el número de parámetros no puede cambiar en
    caliente), y el estado "active" por banda decide si participa o no
    en el procesamiento y en el display.
*/
constexpr int maxBands = 24;

//==============================================================================
/** Centraliza la construcción de IDs de parámetro por banda, p.ej.
    bandParamID (3, "freq") -> "band3_freq". Evita errores de tipeo al
    repetir estos strings por todo el código de UI y DSP.
*/
inline juce::String bandParamID (int bandIndex, const juce::String& suffix)
{
    return "band" + juce::String (bandIndex) + "_" + suffix;
}

namespace BandParamSuffix
{
    static constexpr const char* active      = "active";
    static constexpr const char* freq        = "freq";
    static constexpr const char* gain        = "gain";
    static constexpr const char* q           = "q";
    static constexpr const char* type        = "type";
    static constexpr const char* channelMode = "channelMode";
}
