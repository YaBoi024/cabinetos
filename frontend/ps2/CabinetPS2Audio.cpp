// The sound PCSX2 makes, handed to CabinetOS instead of to a sound card.
//
// **WHY THIS FILE EXISTS AT ALL.** PCSX2 has two perfectly good audio backends
// on Linux, cubeb and SDL, and either would make a PlayStation 2 game audible
// in about one line. Neither may be used here. The console owns audio for
// every libretro core — one device, one volume, one latency, and an
// in-game overlay that can duck it — and a second device open on the same
// hardware is none of those things. It is also the exact shape of fault this
// project already fixed for input: a second path onto hardware the frontend
// thought it owned.
//
// So PCSX2's SPU2 is given a stream that goes nowhere, and the frontend pulls
// out of it whenever its own mixer wants more. `AudioStream` is built for
// this: it buffers, it stretches when the emulator runs slow, and `ReadFrames`
// is the pull. Nothing here re-implements any of that.
//
// THE ONE PATCH THIS PROJECT MAKES TO PCSX2 IS WHAT ROUTES SPU2 HERE, and it
// is three lines in `AudioStream::CreateStream`. See cores/build-pcsx2.sh.
// Cabinet makes the same edit on the Mac for the same reason.

#include "CabinetPS2Host.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "common/Error.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/Host/AudioStream.h"

namespace
{
	class CabinetAudioStream;
	CabinetAudioStream* s_stream = nullptr;
	std::mutex s_stream_lock;

	// Kept outside the stream because it outlives it. The stream is destroyed
	// when the game stops, and anything asking the rate afterwards — a frontend
	// draining the last of the buffer, a probe printing a summary — would
	// otherwise be told zero, which reads as "audio never worked".
	std::atomic<unsigned> s_sample_rate{0};

	class CabinetAudioStream final : public AudioStream
	{
	public:
		CabinetAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
			: AudioStream(sample_rate, parameters)
		{
			// Stereo, and no time stretching.
			//
			// **THE STRETCHER IS DELIBERATELY OFF AND THAT IS NOT AN
			// OVERSIGHT.** It exists to hide an emulator running slightly slow
			// by resampling, which is right when PCSX2 owns the speaker and
			// wrong here: the frontend already measures emulated speed off the
			// audio it receives — it is the only output whose rate the
			// emulated machine decides — and a stretcher upstream of that
			// measurement makes the machine look like it is keeping up when it
			// is not.
			BaseInitialize(&StereoSampleReaderImpl, false);

			std::lock_guard<std::mutex> lock(s_stream_lock);
			s_stream = this;
			s_sample_rate.store(sample_rate);
		}

		~CabinetAudioStream() override
		{
			std::lock_guard<std::mutex> lock(s_stream_lock);
			if (s_stream == this)
				s_stream = nullptr;
		}

		void SetPaused(bool paused) override
		{
			// Nothing to start or stop — there is no device. The base class
			// still wants to know, because a paused stream stops being filled.
			AudioStream::SetPaused(paused);
		}

		/// Pulls up to `max_frames` stereo frames and converts them to 16-bit.
		void Pull(std::vector<int16_t>* out, u32 max_frames)
		{
			// **ONLY WHAT IS ACTUALLY THERE, AND THIS IS THE WHOLE BUG.**
			//
			// The first version read `max_frames` every call regardless.
			// ReadFrames does not refuse — it pads with silence — so at the
			// frontend's frame rate, which on the A9 is around a thousand a
			// second, this asked for about a hundred and seventy seconds of
			// audio per second of wall clock. The buffer was drained empty on
			// the first call and every call after it returned silence.
			//
			// MMagTech, 2026-09-21, playing it: "audio wasn't coming through
			// in game even though after exit i heard weird things". That is
			// exactly this — silence while playing, and the tail of the buffer
			// arriving at once when the stream was torn down.
			const u32 frames = std::min(max_frames, GetBufferedFramesRelaxed());
			if (frames == 0)
				return;

			m_scratch.resize(static_cast<size_t>(frames) * NUM_INPUT_CHANNELS);
			ReadFrames(m_scratch.data(), frames);

			const size_t at = out->size();
			out->resize(at + m_scratch.size());
			for (size_t i = 0; i < m_scratch.size(); i++)
			{
				// PCSX2 works in floats and the frontend's mixer, like every
				// libretro core, works in 16-bit. Clamped rather than wrapped:
				// a sample over 1.0 that wraps is a loud click, and SPU2 does
				// produce them.
				const float v = std::clamp(m_scratch[i], -1.0f, 1.0f);
				(*out)[at + i] = static_cast<int16_t>(v * 32767.0f);
			}
		}

	private:
		std::vector<SampleType> m_scratch;
	};
} // namespace

// Named by the patch in cores/build-pcsx2.sh, which points SPU2's SDL backend
// at this instead. Declared there rather than in a header of ours, because
// PCSX2's own AudioStream.h is where the other two factories are declared and
// this one has to sit beside them to be visible from AudioStream.cpp.
std::unique_ptr<AudioStream> CabinetCreateAudioStream(
	u32 sample_rate, const AudioStreamParameters& parameters, bool stretch_enabled, Error* error)
{
	return std::make_unique<CabinetAudioStream>(sample_rate, parameters);
}

void CabinetPS2::DrainAudio(std::vector<int16_t>* out, unsigned frames)
{
	std::lock_guard<std::mutex> lock(s_stream_lock);
	if (s_stream)
		s_stream->Pull(out, frames);
}

unsigned CabinetPS2::AudioSampleRate()
{
	return s_sample_rate.load();
}
