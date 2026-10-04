/*
 * RocketRPG multiplayer "extra mode" for EasyRPG Player (RPG Maker 2000/2003).
 *
 * Each participant gets a character that looks like the host's party leader. The launcher sends their keys
 * (xkey / xheld); the character walks on the map, cannot leave the host's camera (it is moved next to the host
 * when the camera leaves it) and can start events by talking (decision key), touching or stepping on them.
 * The characters are not part of the game data (Main_Data), so save files never contain them.
 * They only move while the host could move, are hidden while the host's character is hidden (title maps, cut-scenes),
 * walk at the host's speed, follow the host's teleports (also within a map) and can be summoned by the host.
 * Same rules as RocketRPG's MV/MZ (rocket_extra.js) and XP/VX/Ace (rocket_mkxp_agent.rb) versions.
 *
 * This file is part of a modified EasyRPG Player and is licensed under the GNU GPL v3 or later.
 */

#ifndef EP_ROCKET_EXTRA_H
#define EP_ROCKET_EXTRA_H

#include <functional>
#include <string>
#include <vector>

class Bitmap;
class Game_Character;
namespace RocketBridge { struct EspLabel; }

namespace RocketExtra {
	/** Launcher commands */
	void SetMode(bool on);
	void SetGuests(const std::string& list);   // id \x01 name \x01 #rrggbb, guests joined by \x02
	void Key(const std::string& id, int vk, bool down);
	void Held(const std::string& id, const std::vector<int>& vks);
	/** Host: move every participant character next to the host (next update). */
	void Summon();

	bool On();

	/** Once per logical frame after the scene update (moves the characters while the map scene is active). */
	void Update();

	/** Characters to draw on the map (Spriteset_Map keeps one sprite per character). Changes when guests join/leave. */
	const std::vector<Game_Character*>& Characters();

	/** Name labels for this frame (drawn by the display at window resolution, like the ESP labels). ox/oy: map render offset. */
	void AppendLabels(std::vector<RocketBridge::EspLabel>& out, int ox, int oy);

	/** Draws the name labels straight onto a game-resolution frame (the multiplayer stream). */
	void DrawLabels(Bitmap& surface, int ox, int oy);

	/** Autotest (RR_EXTRA_TEST=1): one step per logical frame. 0 = continue, 1 = press decision this frame, 2 = done. */
	int SelfTestStep(const std::function<void(const std::string&)>& log);
}

#endif
