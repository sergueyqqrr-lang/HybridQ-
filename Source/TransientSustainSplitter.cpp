#include "TransientSustainSplitter.h"

namespace
{
    constexpr float fastAttackSeconds  = 0.0005f; // 0.5 ms
    constexpr float fastReleaseSeconds = 0.015f;  // 15 ms
    constexpr float slowAttackSeconds  = 0.015f;  // 15 ms
    constexpr float slowReleaseSeconds = 0.150f;  // 150 ms
    constexpr float maskSmoothSeconds  = 0.001f;  // 1 ms -- evita clicks en la máscara

    inline float onePoleCoeff (float seconds, double sampleRate)
    {
        // coeff tal que env += coeff * (target - env) converge con
        // constante de tiempo `seconds`. Fórmula estándar de envelope
        // follower de pico (ver p.ej. Zölzer, "DAFX", cap. de dinámicas).
        return 1.0f - std::exp (-1.0f / (seconds * (float) sampleRate));
    }
}

//==============================================================================
void TransientSustainSplitter::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    fastAttackCoeff  = onePoleCoeff (fastAttackSeconds,  sampleRate);
    fastReleaseCoeff = onePoleCoeff (fastReleaseSeconds, sampleRate);
    slowAttackCoeff  = onePoleCoeff (slowAttackSeconds,  sampleRate);
    slowReleaseCoeff = onePoleCoeff (slowReleaseSeconds, sampleRate);
    maskSmoothCoeff  = onePoleCoeff (maskSmoothSeconds,  sampleRate);

    reset();
}

void TransientSustainSplitter::reset()
{
    for (auto& state : channelStates)
    {
        state.fastEnv = 0.0f;
        state.slowEnv = 0.0f;
        state.maskSmoothed = 0.0f;
    }
}

//==============================================================================
void TransientSustainSplitter::split (const juce::AudioBuffer<float>& input,
                                       juce::AudioBuffer<float>& transientOut,
                                       juce::AudioBuffer<float>& sustainOut)
{
    const auto numChannels = juce::jmin (input.getNumChannels(), numChannelsSupported);
    const auto numSamples  = input.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& state = channelStates[(size_t) channel];
        const auto* in   = input.getReadPointer (channel);
        auto* transientW = transientOut.getWritePointer (channel);
        auto* sustainW   = sustainOut.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto rectified = std::abs (in[i]);

            // Envelope follower rápido (ataque/release distintos según la
            // señal esté subiendo o bajando -- topología clásica de
            // "peak follower").
            const auto fastCoeff = (rectified > state.fastEnv) ? fastAttackCoeff : fastReleaseCoeff;
            state.fastEnv += fastCoeff * (rectified - state.fastEnv);

            const auto slowCoeff = (rectified > state.slowEnv) ? slowAttackCoeff : slowReleaseCoeff;
            state.slowEnv += slowCoeff * (rectified - state.slowEnv);

            // Transient Detection Function: positiva cuando el envelope
            // rápido va por delante del lento (ataque de una nota nueva).
            const auto tdf = juce::jmax (0.0f, state.fastEnv - state.slowEnv);

            // Normalización + saturación a [0,1]. Dividir por (slowEnv +
            // epsilon) hace la detección relativa al nivel de la señal
            // (para que un pasaje suave y uno fuerte disparen de forma
            // comparable), no solo absoluta.
            const auto normalizedTdf = tdf / (state.slowEnv + 1.0e-4f);
            const auto targetMask = juce::jlimit (0.0f, 1.0f, normalizedTdf * sensitivity);

            // Suavizado final de la máscara -- sin esto, cambios bruscos de
            // g[n] entre samples consecutivos sonarían como clicks/zipper
            // al multiplicar la señal por ella.
            state.maskSmoothed += maskSmoothCoeff * (targetMask - state.maskSmoothed);

            const auto g = state.maskSmoothed;
            transientW[i] = in[i] * g;
            sustainW[i]   = in[i] * (1.0f - g);
        }
    }

    // Si el buffer de entrada tiene más canales de los que soportamos (no
    // debería pasar en este plugin, que es estrictamente estéreo), copiamos
    // tal cual al sustain y silenciamos el transient para no dejar basura.
    for (int channel = numChannelsSupported; channel < input.getNumChannels(); ++channel)
    {
        sustainOut.copyFrom (channel, 0, input, channel, 0, numSamples);
        transientOut.clear (channel, 0, numSamples);
    }
}
