/*
 * RocketRPG multiplayer "extra mode" for EasyRPG Player. See rocket_extra.h.
 *
 * This file is part of a modified EasyRPG Player and is licensed under the GNU GPL v3 or later.
 */

#include "rocket_extra.h"
#include "rocket_bridge.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>

#include "bitmap.h"
#include "font.h"
#include "game_character.h"
#include "game_event.h"
#include "game_interpreter_map.h"
#include "game_map.h"
#include "game_message.h"
#include "game_player.h"
#include "main_data.h"
#include "player.h"
#include "scene.h"
#include "text.h"
#include "utils.h"
#include <lcf/rpg/eventpage.h>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kLeaseMs = 1500;   // 참가자 키 소식이 이만큼 없으면 키를 모두 뗀 것으로

/** 참가자 캐릭터: 게임 데이터(Main_Data)에 넣지 않는 캐릭터. 충돌은 플레이어와 같은 규칙. */
class Game_RocketGuest : public Game_CharacterDataStorage<lcf::rpg::SaveMapEventBase> {
public:
	Game_RocketGuest() : Game_CharacterDataStorage(Game_Character::Player) {}
	void UpdateNextMovementAction() override {}
	void Step() {
		SetProcessed(false);
		Update();
	}
	void Place(int x, int y) {
		SetX(x);
		SetY(y);
		SetRemainingStep(0);
	}
};

struct Guest {
	std::string name;
	Color color{255, 255, 255, 255};
	std::vector<int> order;   // 누른 방향 (나중에 누른 것이 끝)
	bool dash = false;
	Clock::time_point at = Clock::now();
	std::unique_ptr<Game_RocketGuest> ch;
	int map_id = -1;
	bool was_moving = false;
	BitmapRef label;
	std::string label_key;
};

struct State {
	bool on = false;
	std::map<std::string, Guest> guests;
	std::vector<Game_Character*> chars;
} ex;

int DirOfVk(int vk) {
	switch (vk) {
		case 0x28: case 0x62: return Game_Character::Down;
		case 0x25: case 0x64: return Game_Character::Left;
		case 0x27: case 0x66: return Game_Character::Right;
		case 0x26: case 0x68: return Game_Character::Up;
		default: return -1;
	}
}
bool IsOkVk(int vk) { return vk == 0x0D || vk == 0x20 || vk == 0x5A; }
bool IsDashVk(int vk) { return vk == 0x10 || vk == 0xA0 || vk == 0xA1; }

std::vector<std::string> Split(const std::string& s, char sep) {
	std::vector<std::string> out;
	size_t start = 0;
	for (;;) {
		size_t p = s.find(sep, start);
		out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
		if (p == std::string::npos) break;
		start = p + 1;
	}
	return out;
}

Color ParseColor(const std::string& hex) {
	unsigned v = 0xFFFFFF;
	try { v = static_cast<unsigned>(std::stoul(hex.substr(hex.find('#') == 0 ? 1 : 0), nullptr, 16)); } catch (...) {}
	return Color((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
}

void RebuildChars() {
	ex.chars.clear();
	if (!ex.on) return;
	for (auto& kv : ex.guests) if (kv.second.ch) ex.chars.push_back(kv.second.ch.get());
}

bool OnMap() {
	return Scene::instance && Scene::instance->type == Scene::Map && Main_Data::game_player;
}

bool CanAct() {
	if (!ex.on || !OnMap()) return false;
	if (Game_Map::GetInterpreter().IsRunning() || Game_Map::IsAnyEventStarting()) return false;
	if (Game_Message::IsMessageActive()) return false;
	if (Main_Data::game_player->IsPendingTeleport()) return false;
	return true;
}

// 방장 화면(카메라) 안의 칸인지 (이어지는 맵도 맞게)
bool InView(int x, int y) {
	int px = x * TILE_SIZE - Game_Map::GetDisplayX() / TILE_SIZE;
	int py = y * TILE_SIZE - Game_Map::GetDisplayY() / TILE_SIZE;
	if (Game_Map::LoopHorizontal()) px = Utils::PositiveModulo(px, Game_Map::GetTilesX() * TILE_SIZE);
	if (Game_Map::LoopVertical()) py = Utils::PositiveModulo(py, Game_Map::GetTilesY() * TILE_SIZE);
	return px >= 0 && py >= 0 && px <= Player::screen_width - TILE_SIZE && py <= Player::screen_height - TILE_SIZE;
}

// 그 자리 이벤트 시작 (방장과 같은 조건). same_layer: 캐릭터와 같은 층(앞에 있는 사람·물건) / 아니면 아래·위 층(밟는 것)
bool StartAt(Game_RocketGuest& g, int x, int y, std::initializer_list<lcf::rpg::EventPage::Trigger> triggers, bool same_layer, bool by_key) {
	if (Game_Map::GetInterpreter().IsRunning()) return false;
	x = Game_Map::RoundX(x);
	y = Game_Map::RoundY(y);
	bool started = false;
	for (auto& ev : Game_Map::GetEvents()) {
		if (!ev.IsActive() || ev.GetX() != x || ev.GetY() != y) continue;
		if ((ev.GetLayer() == lcf::rpg::EventPage::Layers_same) != same_layer) continue;
		const auto trigger = ev.GetTrigger();
		if (trigger < 0 || std::find(triggers.begin(), triggers.end(), trigger) == triggers.end()) continue;
		if (ev.ScheduleForegroundExecution(by_key, false)) {
			// 방장 대신 말을 건 참가자를 돌아봄
			if (!ev.IsFacingLocked() && !ev.IsSpinning()) ev.SetFacing(ev.GetDirectionToCharacter(g));
			started = true;
		}
	}
	return started;
}

void Action(Game_RocketGuest& g) {
	if (!CanAct() || !g.IsStopping()) return;
	const int d = g.GetDirection();
	int fx = Game_Map::XwithDirection(g.GetX(), d), fy = Game_Map::YwithDirection(g.GetY(), d);
	bool any = StartAt(g, fx, fy, {lcf::rpg::EventPage::Trigger_touched, lcf::rpg::EventPage::Trigger_collision}, true, true);
	any |= StartAt(g, g.GetX(), g.GetY(), {lcf::rpg::EventPage::Trigger_action}, false, true);
	bool got = StartAt(g, fx, fy, {lcf::rpg::EventPage::Trigger_action}, true, true);
	for (int i = 0; !got && i < 3 && Game_Map::IsCounter(fx, fy); ++i) {   // 카운터 너머 (RPG_RT처럼 3칸까지)
		fx = Game_Map::XwithDirection(fx, d);
		fy = Game_Map::YwithDirection(fy, d);
		got = StartAt(g, fx, fy, {lcf::rpg::EventPage::Trigger_action}, true, true);
	}
	(void)any;
}

void Look(Game_RocketGuest& g) {
	auto& p = *Main_Data::game_player;
	if (g.GetSpriteName() != p.GetSpriteName() || g.GetSpriteIndex() != p.GetSpriteIndex())
		g.SetSpriteGraphic(std::string(p.GetSpriteName()), p.GetSpriteIndex());
	g.SetTransparency(p.GetTransparency());
}

} // namespace

namespace RocketExtra {

bool On() { return ex.on; }

void SetMode(bool on) {
	ex.on = on;
	if (!on) ex.guests.clear();
	RebuildChars();
}

void SetGuests(const std::string& list) {
	std::map<std::string, std::pair<std::string, std::string>> want;
	if (!list.empty()) {
		for (const auto& row : Split(list, '\x02')) {
			auto f = Split(row, '\x01');
			if (!f.empty() && !f[0].empty()) want[f[0]] = {f.size() > 1 ? f[1] : "", f.size() > 2 ? f[2] : ""};
		}
	}
	for (auto it = ex.guests.begin(); it != ex.guests.end();) {
		if (!want.count(it->first)) it = ex.guests.erase(it); else ++it;
	}
	for (auto& kv : want) {
		auto& g = ex.guests[kv.first];
		g.name = kv.second.first;
		g.color = ParseColor(kv.second.second);
	}
	RebuildChars();
}

void Key(const std::string& id, int vk, bool down) {
	auto it = ex.guests.find(id);
	if (it == ex.guests.end()) return;
	auto& g = it->second;
	g.at = Clock::now();
	int d = DirOfVk(vk);
	if (d >= 0) {
		g.order.erase(std::remove(g.order.begin(), g.order.end(), d), g.order.end());
		if (down) g.order.push_back(d);
	}
	if (IsDashVk(vk)) g.dash = down;
	if (down && IsOkVk(vk) && g.ch) Action(*g.ch);
}

void Held(const std::string& id, const std::vector<int>& vks) {
	auto it = ex.guests.find(id);
	if (it == ex.guests.end()) return;
	auto& g = it->second;
	g.at = Clock::now();
	std::vector<int> dirs;
	bool dash = false;
	for (int vk : vks) {
		int d = DirOfVk(vk);
		if (d >= 0 && std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
		dash |= IsDashVk(vk);
	}
	g.order.erase(std::remove_if(g.order.begin(), g.order.end(), [&](int d) { return std::find(dirs.begin(), dirs.end(), d) == dirs.end(); }), g.order.end());
	for (int d : dirs) if (std::find(g.order.begin(), g.order.end(), d) == g.order.end()) g.order.push_back(d);
	g.dash = dash;
}

void Update() {
	if (!ex.on || ex.guests.empty() || !OnMap()) return;
	auto& p = *Main_Data::game_player;
	const bool act = CanAct();
	bool rebuilt = false;
	for (auto& kv : ex.guests) {
		auto& g = kv.second;
		if (!g.ch) {
			g.ch = std::make_unique<Game_RocketGuest>();
			rebuilt = true;
		}
		auto& c = *g.ch;
		Look(c);
		if (g.map_id != Game_Map::GetMapId()) {   // 맵이 바뀌면 방장 곁으로
			g.map_id = Game_Map::GetMapId();
			c.SetMapId(g.map_id);
			c.Place(p.GetX(), p.GetY());
			c.SetDirection(p.GetDirection());
			c.SetFacing(p.GetFacing());
		}
		if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.at).count() > kLeaseMs) {
			g.order.clear();
			g.dash = false;
		}
		if (c.IsStopping()) {
			// 걸어 들어간 자리의 아래·위 층 이벤트 (밟으면 시작)
			if (g.was_moving && act) StartAt(c, c.GetX(), c.GetY(), {lcf::rpg::EventPage::Trigger_touched, lcf::rpg::EventPage::Trigger_collision}, false, false);
			if (!InView(c.GetX(), c.GetY())) {
				c.Place(p.GetX(), p.GetY());   // 방장이 움직여 화면 밖으로 밀려남
			} else if (act && !g.order.empty()) {
				const int d = g.order.back();
				const int x2 = Game_Map::XwithDirection(c.GetX(), d), y2 = Game_Map::YwithDirection(c.GetY(), d);
				if (!InView(x2, y2)) {
					c.SetDirection(d);   // 화면 밖으로는 못 감
					c.SetFacing(d);
				} else if (!c.Move(d)) {
					c.SetDirection(d);
					c.SetFacing(d);
					// 막힌 앞자리의 사람·물건 (닿으면 시작)
					StartAt(c, x2, y2, {lcf::rpg::EventPage::Trigger_touched, lcf::rpg::EventPage::Trigger_collision}, true, false);
				}
			}
		}
		c.SetMoveSpeed(p.GetMoveSpeed());   // 2000/2003은 달리기가 없음: 방장 캐릭터와 같은 속도 (이벤트가 바꾼 속도도 따라감)
		c.Step();
		g.was_moving = !c.IsStopping();
	}
	if (rebuilt) RebuildChars();
}

const std::vector<Game_Character*>& Characters() {
	static const std::vector<Game_Character*> none;
	return ex.on ? ex.chars : none;
}

namespace {
template <typename F>
void ForEachLabel(F&& f) {
	if (!ex.on || !OnMap()) return;
	FontRef font;
	for (auto& kv : ex.guests) {
		auto& g = kv.second;
		if (!g.ch || g.ch->GetTransparency() >= 7) continue;
		std::string key = g.name + "/" + std::to_string(g.color.red) + "," + std::to_string(g.color.green) + "," + std::to_string(g.color.blue);
		if (!g.label || g.label_key != key) {
			if (!font) font = Font::DefaultBitmapFont();
			Rect sz = Text::GetSize(*font, g.name);
			g.label = Bitmap::Create(std::max(1, sz.width + 4), std::max(1, sz.height + 2), Color(0, 0, 0, 140));
			Text::Draw(*g.label, 2, 1, *font, g.color, g.name);
			g.label_key = key;
		}
		f(*g.ch, g.label);
	}
}
}

void AppendLabels(std::vector<RocketBridge::EspLabel>& out, int ox, int oy) {
	ForEachLabel([&](Game_Character& c, const BitmapRef& label) {
		out.push_back({c.GetScreenX() + ox, c.GetScreenY() - TILE_SIZE - 2 + oy, label});
	});
}

void DrawLabels(Bitmap& surface, int ox, int oy) {
	ForEachLabel([&](Game_Character& c, const BitmapRef& label) {
		int x = c.GetScreenX() + ox - label->width() / 2;
		int y = c.GetScreenY() - TILE_SIZE - 2 + oy - label->height();
		surface.Blit(x, y, *label, label->GetRect(), Opacity::Opaque());
	});
}

} // namespace RocketExtra

// ── 자동 시험 (RR_AUTOTEST + RR_EXTRA_TEST=1): 첫 맵에서 가짜 참가자로 생성·이동·화면 제한·말 걸기·끄기를 확인 ──
namespace RocketExtra {
namespace {
struct SelfTestState {
	int et = 0, wait = 0, wait2 = 0, viol = 0, off_x = -1, start_x = 0, start_y = 0, moved = -1;
	Game_Event* target = nullptr;
} tst;
}

int SelfTestStep(const std::function<void(const std::string&)>& log) {
	const int et = tst.et++;
	auto& p = *Main_Data::game_player;
	Guest* g = ex.guests.count("t1") ? &ex.guests["t1"] : nullptr;
	Game_RocketGuest* c = g && g->ch ? g->ch.get() : nullptr;
	if (et == 0) {
		SetMode(true);
		SetGuests(std::string("t1\x01test guest\x01#ffd34d"));
		return 0;
	}
	if (et > 3 && c && c->IsStopping() && !InView(c->GetX(), c->GetY()) && et != 201) tst.viol++;
	if (et == 10 && !CanAct() && ++tst.wait < 2400) { tst.et = 10; return 1; }   // 이벤트가 끝날 때까지 (확인 키로 넘김)
	if (et == 10) {
		if (tst.wait) log("EXTRA waited " + std::to_string(tst.wait) + " frames for the event to end");
		bool ok = c && Characters().size() == 1 && c->GetX() == p.GetX() && c->GetY() == p.GetY();
		log(std::string("EXTRA spawn ") + (ok ? "ok" : "FAIL") + " at " + (c ? std::to_string(c->GetX()) + "," + std::to_string(c->GetY()) : "-") +
			" player " + std::to_string(p.GetX()) + "," + std::to_string(p.GetY()) + " image " + (c ? std::string(c->GetSpriteName()) : "-"));
		tst.start_x = c ? c->GetX() : 0;
		tst.start_y = c ? c->GetY() : 0;
	}
	if (et >= 10 && et < 170 && c) {
		static const int vks[4] = {0x28, 0x25, 0x27, 0x26};
		int i = (et - 10) / 40;
		if (et % 5 == 0) Held("t1", tst.moved >= 0 ? std::vector<int>{} : std::vector<int>{vks[i]});
		if (tst.moved < 0 && (c->GetX() != tst.start_x || c->GetY() != tst.start_y)) tst.moved = i;
	}
	if (et == 170) {
		Held("t1", {});
		log(std::string("EXTRA move ") + (tst.moved >= 0 ? "ok" : "FAIL") + " " + std::to_string(tst.start_x) + "," + std::to_string(tst.start_y) +
			" -> " + (c ? std::to_string(c->GetX()) + "," + std::to_string(c->GetY()) : "-") + " canAct " + (CanAct() ? "true" : "false"));
	}
	if (et == 200 && c) {
		for (int x = 0; x < Game_Map::GetTilesX(); ++x) if (!InView(x, p.GetY())) { tst.off_x = x; break; }
		if (tst.off_x >= 0) c->Place(tst.off_x, p.GetY()); else log("EXTRA off-screen skipped (map fits the screen)");
	}
	if (et == 203 && c && tst.off_x >= 0)
		log(std::string("EXTRA off-screen ") + std::to_string(tst.off_x) + " -> " + (c->GetX() == p.GetX() && c->GetY() == p.GetY() ? "back to host ok" : "FAIL"));
	if (et == 210 && c && !CanAct() && ++tst.wait2 < 1200) { tst.et = 210; return 1; }
	if (et == 210 && c) {
		// 말 걸기: 화면 안의 '결정 키' 이벤트 아래 칸에 세우고 위를 보게
		for (auto& ev : Game_Map::GetEvents()) {
			if (!ev.IsActive() || ev.GetTrigger() != lcf::rpg::EventPage::Trigger_action || ev.GetLayer() != lcf::rpg::EventPage::Layers_same || ev.GetList().empty()) continue;
			int x = ev.GetX(), y = ev.GetY() + 1;
			if (!Game_Map::IsValid(x, y) || !InView(x, y)) continue;
			bool busy = false;
			for (auto& o : Game_Map::GetEvents()) if (o.IsActive() && o.GetX() == x && o.GetY() == y) busy = true;
			if (busy) continue;
			tst.target = &ev;
			c->Place(x, y);
			c->SetDirection(Game_Character::Up);
			c->SetFacing(Game_Character::Up);
			break;
		}
		if (!tst.target) log("EXTRA talk skipped (no talk event on screen)");
	}
	if (et == 213 && c && tst.target) {
		Action(*c);
		bool ok = tst.target->IsWaitingForegroundExecution() || Game_Map::GetInterpreter().IsRunning();
		log(std::string("EXTRA talk ") + (ok ? "ok" : "FAIL") + " event " + std::to_string(tst.target->GetId()));
	}
	if (et == 230) {
		SetMode(false);
		log(std::string("EXTRA off ") + (Characters().empty() ? "characters removed" : "FAIL"));
		log("EXTRA DONE violations=" + std::to_string(tst.viol));
		return 2;
	}
	return 0;
}
} // namespace RocketExtra
