/*
 * RocketRPG bridge for EasyRPG Player.
 *
 * Connects to the RocketRPG launcher over the named pipe given in RR_BRIDGE_PIPE and speaks the same
 * line protocol as RocketRPG's mkxp-z agent (telemetry, dialogue state, ESP, tile info, data dump;
 * commands for speed, pause, noclip, auto/skip messages, brightness, quick save/load, warp, switches, variables).
 * When RR_BRIDGE_PIPE is not set every hook is a no-op, so the Player behaves exactly like upstream.
 *
 * This file is part of a modified EasyRPG Player and is licensed under the GNU GPL v3 or later.
 */

#ifndef EP_ROCKET_BRIDGE_H
#define EP_ROCKET_BRIDGE_H

#include <memory>
#include <string>
#include <vector>

class Bitmap;

namespace RocketBridge {
	/** Called once per displayed frame from Player::MainLoop. */
	void Tick();

	/** Called once per logical frame, right before the scene update (input simulation for the autotest). */
	void LogicTick();

	/** Called right after the scene update of a logical frame (a scene may be popped safely here). */
	void PostUpdate();

	/** Game speed multiplier requested by the launcher (1.0 = normal). */
	float SpeedFactor();

	/** Whether the launcher paused the game (scene updates are skipped, drawing continues). */
	bool Paused();

	/** Walk-through-walls requested by the launcher. */
	bool Noclip();

	/** Message text is shown instantly and advanced every frame. */
	bool Skip();

	/** Window_Message is waiting for DECISION: returns true when auto/skip/advance should confirm it. */
	bool WantAdvance();

	/** Dialogue started / finished (for the launcher's Ren'Py quick menu and dialogue log). */
	void OnMessageStart(const std::vector<std::string>& lines, int choices);
	void OnMessageEnd();

	/** Applies the launcher brightness/CRT and draws the ESP boxes onto the final frame. */
	void ApplyBrightness(Bitmap& surface);

	/** ESP name label for the current frame: x = box centre, y = box top, in game-screen pixels. */
	struct EspLabel {
		int x = 0, y = 0;
		std::shared_ptr<Bitmap> bitmap;
	};

	/**
	 * Labels are drawn by the display (window resolution, not game resolution) so they stay small and sharp.
	 * Generation changes whenever label bitmaps were recreated (map change): cached textures must be dropped.
	 */
	const std::vector<EspLabel>& EspLabels();
	int EspLabelGeneration();
	int EspTileSize();
}

#endif
