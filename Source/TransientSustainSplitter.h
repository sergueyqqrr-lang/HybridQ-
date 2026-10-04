#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

//==============================================================================
/**
    TransientSustainSplitter -- Fase 3, sección 3.1 Opción A del brief
    (envelope-domain splitting, recomendada como MVP).

    Produce `transientOut` y `sustainOut` a partir de `input` tales que
    `transientOut + sustainOut == input` EXACTAMENTE, sample a sample (split
    aditivo puro, sin pérdida de energía ni fase extra introducida por el
    split en sí). El método:

      1. Dos envelope followers por canal sobre la señal rectificada:
         uno "rápido" (ataque ~0.5 ms, release ~15 ms) que seguiría los
         transientes, y uno "lento" (ataque ~15 ms, release ~150 ms) que
         sigue el cuerpo/sustain.
      2. TDF = fastEnv - slowEnv (Transient Detection Function), >= 0.
      3. Esa TDF se normaliza y suaviza en una máscara g[n] en [0,1] que
         representa "cuánto de transiente hay en esta muestra".
      4. transient[n] = input[n] * g[n]; sustain[n] = input[n] * (1-g[n]).

    SIMPLIFICACIÓN CONSCIENTE DE ESTA FASE: el brief (sección 3.1) recomienda
    hacer la detección en 3-4 sub-bandas (vía crossovers Linkwitz-Riley)
    para no confundir, p. ej., un golpe de bombo grave con un transiente de
    hi-hat agudo. Esta primera versión detecta en banda completa (un único
    par de envelope followers por canal). Es más barata y más simple de
    verificar, pero en material polifónico denso (mezclas completas) la
    separación será menos precisa que la versión multibanda. Convertir esto
    en multibanda es el candidato más claro de pulido para after-MVP de esta
    fase, una vez que se confirme que el comportamiento base suena natural.

    Los coeficientes de attack/release y la sensibilidad están expuestos
    como constantes con valores de partida razonables (sección 3.1), pero
    NO han sido afinados escuchando material real -- eso solo se puede
    hacer con el plugin compilado y sonando, no desde este entorno de texto.
*/
class TransientSustainSplitter
{
public:
    TransientSustainSplitter() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** channel 0 = izquierdo, 1 = derecho (detección independiente por
        canal; no hay linking L/R en esta versión). `input` puede ser el
        mismo buffer que transientOut o sustainOut si se desea procesar
        in-place para uno de los dos -- pero NO ambos a la vez, ya que cada
        sample de salida depende solo del sample de entrada correspondiente,
        así que el aliasing de puntero es seguro mientras no se lea
        `input[n]` después de haber sido sobreescrito por esa misma llamada.
        Para evitar cualquier ambigüedad, el uso recomendado es con tres
        buffers distintos. */
    void split (const juce::AudioBuffer<float>& input,
                juce::AudioBuffer<float>& transientOut,
                juce::AudioBuffer<float>& sustainOut);

private:
    struct ChannelState
    {
        float fastEnv = 0.0f;
        float slowEnv = 0.0f;
        float maskSmoothed = 0.0f;
    };

    static constexpr int numChannelsSupported = 2;
    std::array<ChannelState, numChannelsSupported> channelStates;

    // Coeficientes de los one-pole followers, recalculados en prepare()
    // según la sample rate real (ver fórmula estándar de envelope follower
    // de pico: coeff = 1 - exp(-1 / (tau_segundos * sampleRate))).
    float fastAttackCoeff = 0.0f, fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
    float maskSmoothCoeff = 0.0f;

    // Sensibilidad: multiplica la TDF antes de saturarla a [0,1]. Un valor
    // más alto hace que la detección de transiente dispare con golpes más
    // sutiles; valor de partida razonable, pendiente de afinar por oído.
    static constexpr float sensitivity = 6.0f;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransientSustainSplitter)
};
