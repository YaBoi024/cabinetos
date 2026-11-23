// The controller's half of CabinetOS's host layer for PCSX2: the pads the
// frontend hands over (CabinetPS2::SetPad, on the frontend's thread), and
// their translation into a DualShock 2's inputs, or a guitar's in a port that
// holds one (ApplyPads, on PCSX2's CPU thread, first thing in
// Host::PumpMessagesOnCPUThread). The pads are not reset between games, on
// purpose. Moved out of CabinetPS2Host.cpp in the
// restructure (#398).

#include "CabinetPS2Pad.h"

#include <atomic>
#include <mutex>

#include "CabinetPS2Host.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/SIO/Pad/PadGuitar.h"

namespace
{
	// The controller. The frontend writes whenever it likes; PCSX2's CPU
	// thread reads it between frames, which is the only place pad state may be
	// touched.
	std::mutex s_pad_lock;
	CabinetPS2::Pad s_pads[2];
	std::atomic<bool> s_pads_dirty{false};

	// RetroPad id -> DualShock 2 input, and the whole translation is this table.
	//
	// The face buttons are the part worth reading twice. RetroPad is laid out
	// like a Super Nintendo pad and the PlayStation's is rotated against it, so
	// this maps by POSITION rather than by letter: B is the bottom button and
	// becomes Cross, A is the right and becomes Circle. Mapping B to a button
	// called B would put Cross where Square belongs on every game.
	constexpr int kRetroToPS2[16] = {
		PadDualshock2::Inputs::PAD_CROSS,    // 0  B      bottom
		PadDualshock2::Inputs::PAD_SQUARE,   // 1  Y      left
		PadDualshock2::Inputs::PAD_SELECT,   // 2
		PadDualshock2::Inputs::PAD_START,    // 3
		PadDualshock2::Inputs::PAD_UP,       // 4
		PadDualshock2::Inputs::PAD_DOWN,     // 5
		PadDualshock2::Inputs::PAD_LEFT,     // 6
		PadDualshock2::Inputs::PAD_RIGHT,    // 7
		PadDualshock2::Inputs::PAD_CIRCLE,   // 8  A      right
		PadDualshock2::Inputs::PAD_TRIANGLE, // 9  X      top
		PadDualshock2::Inputs::PAD_L1,       // 10 L
		PadDualshock2::Inputs::PAD_R1,       // 11 R
		PadDualshock2::Inputs::PAD_L2,       // 12 L2     see triggers
		PadDualshock2::Inputs::PAD_R2,       // 13 R2     see triggers
		PadDualshock2::Inputs::PAD_L3,       // 14
		PadDualshock2::Inputs::PAD_R3,       // 15
	};

	// RetroPad id -> PCSX2's guitar's input (SIO/Pad/PadGuitar.h), or -1, for
	// a port that holds a guitar. The frontend hands such a port the guitar's
	// state (frontend/src/guitar.h, ps2State): the frets by position as any
	// pad's buttons, so green is the bottom button and blue the left; the
	// whammy as the left trigger, 0 at rest; tilt as the L2 bit.
	constexpr int kRetroToGuitar[16] = {
		PadGuitar::Inputs::GREEN,      // 0  B      bottom
		PadGuitar::Inputs::BLUE,       // 1  Y      left
		PadGuitar::Inputs::SELECT,     // 2         star power
		PadGuitar::Inputs::START,      // 3
		PadGuitar::Inputs::STRUM_UP,   // 4
		PadGuitar::Inputs::STRUM_DOWN, // 5
		-1,                            // 6  Left
		-1,                            // 7  Right
		PadGuitar::Inputs::RED,        // 8  A      right
		PadGuitar::Inputs::YELLOW,     // 9  X      top
		PadGuitar::Inputs::ORANGE,     // 10 L
		-1,                            // 11 R
		PadGuitar::Inputs::TILT,       // 12 L2
		-1,                            // 13 R2
		-1,                            // 14 L3
		-1,                            // 15 R3
	};

	// A guitar's port. THE WHAMMY GOES IN AT HALF: PadGuitar::Set takes the
	// half axis 0 to 1 as 127 less 255 times it, held to 0 to 127 (127 at
	// rest, 0 pushed), so a whole push would reach the end at its middle.
	void ApplyGuitar(u32 port, const CabinetPS2::Pad& pad)
	{
		for (int i = 0; i < 16; i++)
		{
			if (kRetroToGuitar[i] < 0)
				continue;
			const bool down = ((pad.buttons >> i) & 1u) != 0;
			::Pad::SetControllerState(port, static_cast<u32>(kRetroToGuitar[i]), down ? 1.0f : 0.0f);
		}
		::Pad::SetControllerState(port, PadGuitar::Inputs::WHAMMY, pad.leftTrigger * 0.5f);
	}
} // namespace

namespace CabinetPS2
{
	void ApplyPads()
	{
		if (!s_pads_dirty.exchange(false))
			return;

		CabinetPS2::Pad pads[2];
		{
			std::lock_guard<std::mutex> lock(s_pad_lock);
			pads[0] = s_pads[0];
			pads[1] = s_pads[1];
		}

		for (u32 port = 0; port < 2; port++)
		{
			const CabinetPS2::Pad& pad = pads[port];

			if (const PadBase* held = ::Pad::GetPad(static_cast<u8>(port));
				held && held->GetType() == ::Pad::ControllerType::Guitar)
			{
				ApplyGuitar(port, pad);
				continue;
			}

			for (int i = 0; i < 16; i++)
			{
				// L2 and R2 come from the analogue channel instead, below. A
				// digital press still registers, because the frontend sets the
				// trigger to 1.0 when the button is down — `PadState` says so.
				if (i == 12 || i == 13)
					continue;
				const bool down = ((pad.buttons >> i) & 1u) != 0;
				::Pad::SetControllerState(port, static_cast<u32>(kRetroToPS2[i]), down ? 1.0f : 0.0f);
			}

			::Pad::SetControllerState(port, PadDualshock2::Inputs::PAD_L2, pad.leftTrigger);
			::Pad::SetControllerState(port, PadDualshock2::Inputs::PAD_R2, pad.rightTrigger);

			// PCSX2 splits every stick axis into two half-axes, each 0..1,
			// rather than one signed value. So each of the four numbers the
			// frontend sends becomes a pair, and exactly one of each pair is
			// non-zero.
			static constexpr int kAxis[4][2] = {
				{PadDualshock2::Inputs::PAD_L_LEFT, PadDualshock2::Inputs::PAD_L_RIGHT},
				{PadDualshock2::Inputs::PAD_L_UP, PadDualshock2::Inputs::PAD_L_DOWN},
				{PadDualshock2::Inputs::PAD_R_LEFT, PadDualshock2::Inputs::PAD_R_RIGHT},
				{PadDualshock2::Inputs::PAD_R_UP, PadDualshock2::Inputs::PAD_R_DOWN},
			};
			const float sticks[4] = {pad.leftX, pad.leftY, pad.rightX, pad.rightY};
			for (int a = 0; a < 4; a++)
			{
				const float v = sticks[a];
				::Pad::SetControllerState(port, static_cast<u32>(kAxis[a][0]), v < 0.0f ? -v : 0.0f);
				::Pad::SetControllerState(port, static_cast<u32>(kAxis[a][1]), v > 0.0f ? v : 0.0f);
			}
		}
	}
} // namespace CabinetPS2

void CabinetPS2::SetPad(unsigned port, const Pad& pad)
{
	if (port >= 2)
		return;
	{
		std::lock_guard<std::mutex> lock(s_pad_lock);
		s_pads[port] = pad;
	}
	s_pads_dirty.store(true);
}
