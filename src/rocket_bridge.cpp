/*
 * RocketRPG bridge for EasyRPG Player (see rocket_bridge.h).
 *
 * This file is part of a modified EasyRPG Player and is licensed under the GNU GPL v3 or later.
 */

#include "rocket_bridge.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "bitmap.h"
#include "filefinder.h"
#include "font.h"
#include "game_enemyparty.h"
#include "game_enemy.h"
#include "game_event.h"
#include "game_map.h"
#include "game_player.h"
#include "game_switches.h"
#include "game_variables.h"
#include "main_data.h"
#include "map_data.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "scene.h"
#include "scene_battle.h"
#include "scene_map.h"
#include "scene_save.h"
#include "spriteset_map.h"
#include "game_interpreter_map.h"
#include "game_message.h"
#include "input.h"
#include "scene_title.h"
#include "transition.h"
#include "teleport_target.h"
#include "text.h"
#include "utils.h"
#include <array>
#include <cstdio>
#include <map>
#include <lcf/data.h>
#include <lcf/rpg/eventcommand.h>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {

constexpr char SEP_CMD = '\x1E';
constexpr char SEP_F = '\x1F';
constexpr char NL = '\x1D';

using Clock = std::chrono::steady_clock;

struct State {
	bool enabled = false;
	std::string pipe_name;
#ifdef _WIN32
	HANDLE pipe = INVALID_HANDLE_VALUE;
#endif
	std::string rbuf;
	long long frame = 0;
	long long next_connect = 0;

	float speed = 1.0f;
	bool paused = false;
	bool noclip = false;
	bool auto_msg = false;
	float auto_speed = 1.0f;
	bool skip = false;
	bool force_advance = false;
	bool esp = false;
	bool has_mouse = false;
	int mouse_x = 0, mouse_y = 0;
	float bright = 1.0f;
	bool crt = false;
	BitmapRef white;
	std::map<int, BitmapRef> esp_labels; // 이벤트 id -> 이름표 (맵이 바뀌면 비움)
	int esp_map = -1;

	bool msg_busy = false;
	std::string msg_text;
	Clock::time_point wait_since{};
	bool waiting = false;
	std::string last_telemetry;
} st;

bool Init() {
	static bool inited = false;
	if (!inited) {
		inited = true;
		const char* p = std::getenv("RR_BRIDGE_PIPE");
		if (p && *p) {
			st.enabled = true;
			st.pipe_name = p;
		}
	}
	return st.enabled;
}

std::string Escape(std::string s) {
	s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
	std::replace(s.begin(), s.end(), '\n', NL);
	return s;
}

std::string JsonStr(const std::string& s) {
	std::string o;
	o.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break;
			case '\r': break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) o += ' '; else o += c;
		}
	}
	return o;
}

#ifdef _WIN32
void ClosePipe() {
	if (st.pipe != INVALID_HANDLE_VALUE) CloseHandle(st.pipe);
	st.pipe = INVALID_HANDLE_VALUE;
	st.rbuf.clear();
	st.paused = false;
	st.next_connect = st.frame + 120;
}

void Connect() {
	if (st.pipe != INVALID_HANDLE_VALUE || st.frame < st.next_connect) return;
	std::wstring name = L"\\\\.\\pipe\\";
	name.append(st.pipe_name.begin(), st.pipe_name.end());
	HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		st.next_connect = st.frame + 120;
		return;
	}
	st.pipe = h;
}

bool ReadLine(std::string& out) {
	for (;;) {
		auto pos = st.rbuf.find('\n');
		if (pos != std::string::npos) {
			out = st.rbuf.substr(0, pos);
			st.rbuf.erase(0, pos + 1);
			return true;
		}
		char buf[4096];
		DWORD rd = 0;
		if (!ReadFile(st.pipe, buf, sizeof(buf), &rd, nullptr) || rd == 0) return false;
		st.rbuf.append(buf, rd);
	}
}
#endif

void Handle(const std::string& cmd);

void SendLine(const std::string& kind, const std::string& payload = {}) {
#ifdef _WIN32
	if (st.pipe == INVALID_HANDLE_VALUE) return;
	std::string line = kind;
	line += SEP_F;
	line += Escape(payload);
	line += '\n';
	DWORD wr = 0;
	if (!WriteFile(st.pipe, line.data(), static_cast<DWORD>(line.size()), &wr, nullptr)) {
		ClosePipe();
		return;
	}
	// 카메라("C")는 매 프레임 보내므로 답을 기다리지 않습니다 (RocketRPG도 답하지 않음) — 게임 루프가 파이프 왕복을 기다리지 않게.
	if (kind == "C") return;
	std::string reply;
	if (!ReadLine(reply)) {
		ClosePipe();
		return;
	}
	size_t start = 0;
	while (start < reply.size()) {
		size_t end = reply.find(SEP_CMD, start);
		if (end == std::string::npos) end = reply.size();
		if (end > start) Handle(reply.substr(start, end - start));
		start = end + 1;
	}
#else
	(void)kind; (void)payload;
#endif
}

std::vector<std::string> Split(const std::string& s, char sep) {
	std::vector<std::string> v;
	size_t start = 0;
	for (;;) {
		size_t end = s.find(sep, start);
		v.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return v;
}

bool OnMap() {
	return Scene::instance && Scene::instance->type == Scene::Map && Main_Data::game_player;
}

void RenderOffset(int& ox, int& oy) {
	ox = oy = 0;
	if (!OnMap()) return;
	auto* map = static_cast<Scene_Map*>(Scene::instance.get());
	if (map->spriteset) {
		ox = map->spriteset->GetRenderOx();
		oy = map->spriteset->GetRenderOy();
	}
}

void Notice(const std::string& msg) {
	SendLine("N", msg);
}

void QuickSave() {
	if (!OnMap()) {
		Notice("맵 화면에서만 퀵 세이브할 수 있습니다");
		return;
	}
	auto fs = FileFinder::Save();
	if (Scene_Save::Save(fs, 1)) Notice("1번 슬롯에 퀵 세이브 완료");
	else Notice("퀵 세이브 실패");
}

void QuickLoad() {
	auto fs = FileFinder::Save();
	std::string name = fs.FindFile("Save01.lsd");
	if (name.empty()) {
		Notice("1번 슬롯에 저장 데이터가 없습니다");
		return;
	}
	Player::LoadSavegame(name, 1);
	Notice("1번 슬롯 퀵 로드 완료");
}

void DumpData(int max_sw, int max_va) {
	std::string sw, va;
	if (Main_Data::game_switches) {
		int n = std::min(max_sw, Main_Data::game_switches->GetSizeWithLimit());
		for (int i = 1; i <= n; ++i) sw += Main_Data::game_switches->Get(i) ? '1' : '0';
	}
	if (Main_Data::game_variables) {
		int n = std::min(max_va, Main_Data::game_variables->GetSizeWithLimit());
		for (int i = 1; i <= n; ++i) {
			if (i > 1) va += ';';
			va += std::to_string(Main_Data::game_variables->Get(i));
		}
	}
	SendLine("D", sw + "|" + va);
}

void Handle(const std::string& cmd) {
	auto f = Split(cmd, SEP_F);
	const std::string& k = f[0];
	auto arg = [&](size_t i) -> std::string { return i < f.size() ? f[i] : std::string(); };
	auto argf = [&](size_t i, float def) { try { return std::stof(arg(i)); } catch (...) { return def; } };
	auto argi = [&](size_t i, int def) { try { return std::stoi(arg(i)); } catch (...) { return def; } };

	if (k == "pause") st.paused = arg(1) == "1";
	else if (k == "speed") st.speed = std::max(0.25f, std::min(argf(1, 1.0f), 16.0f));
	else if (k == "noclip") st.noclip = arg(1) == "1";
	else if (k == "auto") { st.auto_msg = arg(1) == "1"; st.auto_speed = std::max(0.2f, argf(2, 1.0f)); }
	else if (k == "skip") st.skip = arg(1) == "1";
	else if (k == "advance") st.force_advance = true;
	else if (k == "esp") st.esp = arg(1) == "1";
	else if (k == "bright") st.bright = argf(1, 1.0f);
	else if (k == "crt") st.crt = arg(1) == "1";
	else if (k == "font") {
		// RocketRPG 인게임 글꼴을 재시작 없이 바꿉니다 (경로가 비면 게임 기본 글꼴).
		auto& cfg = Player::player_config;
		const std::string path = arg(1);
		const int size = std::max(6, std::min(16, argi(2, 12)));
		cfg.font1.Set(path);
		cfg.font2.Set(path);
		cfg.font1_size.Set(size);
		cfg.font2_size.Set(size);
		if (path.empty()) Player::LoadFonts();
		else Font::ResetDefault();
	}
	else if (k == "mouse") {
		st.has_mouse = !arg(1).empty();
		st.mouse_x = static_cast<int>(argf(1, 0));
		st.mouse_y = static_cast<int>(argf(2, 0));
	}
	else if (k == "qsave") QuickSave();
	else if (k == "qload") QuickLoad();
	else if (k == "dump") DumpData(argi(1, 500), argi(2, 200));
	else if (k == "setsw" && Main_Data::game_switches) {
		Main_Data::game_switches->Set(argi(1, 0), arg(2) == "1");
		Game_Map::SetNeedRefresh(true);
	}
	else if (k == "setvar" && Main_Data::game_variables) {
		Main_Data::game_variables->Set(argi(1, 0), argi(2, 0));
		Game_Map::SetNeedRefresh(true);
	}
	else if (k == "warp" && OnMap()) {
		Main_Data::game_player->ReserveTeleport(argi(1, 1), argi(2, 0), argi(3, 0), -1, TeleportTarget::eParallelTeleport);
	}
}

std::string Telemetry() {
	std::string scene = Scene::instance ? Scene::scene_names[Scene::instance->type] : "";
	if (scene == "Map") scene = "Scene_Map";
	int mid = 0, px = 0, py = 0, dx = 0, dy = 0;
	bool through = st.noclip;
	if (Main_Data::game_player) {
		mid = Game_Map::GetMapId();
		px = Main_Data::game_player->GetX();
		py = Main_Data::game_player->GetY();
		dx = Game_Map::GetDisplayX();
		dy = Game_Map::GetDisplayY();
		through = through || Main_Data::game_player->GetThrough();
	}
	std::ostringstream o;
	o << "{\"Scene\":\"" << JsonStr(scene) << "\",\"MapId\":" << mid << ",\"PlayerX\":" << px << ",\"PlayerY\":" << py
	  << ",\"DisplayX\":" << dx << ",\"DisplayY\":" << dy << ",\"Noclip\":" << (through ? "true" : "false")
	  << ",\"ScreenW\":" << Player::screen_width << ",\"ScreenH\":" << Player::screen_height << "}";
	return o.str();
}

void SendTile() {
	if (!OnMap() || !st.has_mouse) return;
	int ox, oy;
	RenderOffset(ox, oy);
	int mx = Game_Map::RoundX((Game_Map::GetDisplayX() + (st.mouse_x - ox) * TILE_SIZE) / SCREEN_TILE_SIZE);
	int my = Game_Map::RoundY((Game_Map::GetDisplayY() + (st.mouse_y - oy) * TILE_SIZE) / SCREEN_TILE_SIZE);
	bool valid = Game_Map::IsValid(mx, my);
	bool pass = false;
	std::string tiles, evs;
	if (valid) {
		pass = Game_Map::IsPassableTile(nullptr, Passable::Down | Passable::Left | Passable::Right | Passable::Up, mx, my, false, true);
		tiles = std::to_string(Game_Map::GetTileIdAt(mx, my, 0)) + "," + std::to_string(Game_Map::GetTileIdAt(mx, my, 1));
		for (auto& ev : Game_Map::GetEvents()) {
			if (ev.GetX() == mx && ev.GetY() == my && ev.IsActive()) {
				if (!evs.empty()) evs += ", ";
				std::string n(ev.GetName());
				evs += n.empty() ? "EV" + std::to_string(ev.GetId()) : n;
			}
		}
	}
	std::ostringstream o;
	o << "{\"mapX\":" << mx << ",\"mapY\":" << my << ",\"passable\":" << (pass ? "true" : "false")
	  << ",\"tileIds\":[" << tiles << "],\"events\":\"" << JsonStr(evs.empty() ? "없음" : evs)
	  << "\",\"screenX\":" << st.mouse_x << ",\"screenY\":" << st.mouse_y << "}";
	SendLine("I", o.str());
}

std::string CleanMessage(std::string t) {
	try {
		static const std::regex vars(R"(\\[Vv]\[(\d+)\])");
		std::string out;
		std::sregex_iterator it(t.begin(), t.end(), vars), end;
		size_t last = 0;
		for (; it != end; ++it) {
			out += t.substr(last, it->position() - last);
			int id = std::stoi((*it)[1]);
			if (Main_Data::game_variables) out += std::to_string(Main_Data::game_variables->Get(id));
			last = it->position() + it->length();
		}
		out += t.substr(last);
		t = out;
		static const std::regex codes(R"(\\[A-Za-z]\[[^\]]*\]|\\[.|!><^$_{}])");
		t = std::regex_replace(t, codes, "");
	} catch (...) {
	}
	return t;
}

} // namespace

namespace RocketBridge {

// ---------------------------------------------------------------------------------------------------------------
// Automated compatibility test (RR_AUTOTEST=<log file>), same idea as RocketRPG's mkxp-z autotest:
// confirm through the title, start a new game, then visit every map the game itself teleports to (arriving at the
// command's coordinates), walk around and press confirm so events run, and wait for events to finish before the
// next teleport. Choices take the default first; only when the same choice comes back (a "wrong answer" loop) is
// the next option tried. Independent of the launcher pipe.
// ---------------------------------------------------------------------------------------------------------------
namespace {
struct AutoTest {
	bool checked = false, enabled = false, done = false, forced = false, title_fixed = false;
	int battle_frames = 0, name_step = 0, name_frames = 0;
	bool name_finish = false, battle_won = false;
	std::string path;
	int max_maps = 60, per_map = 150;
	long long frame = 0;
	int phase = 0; // 0 = boot, 1 = maps
	std::vector<int> maps;
	std::map<int, std::array<int, 3>> dests;
	int index = -1, map_frames = 0, off_map = 0, target = 0, last_scene = -1, scene_logs = 0;
	std::map<std::string, int> choice_seen;
	bool choice = false;
	int downs_needed = 0, downs_done = 0;
} at;

void AtLog(const std::string& msg) {
	std::string line = std::to_string(at.frame) + "\t" + msg + "\n";
#ifdef _WIN32
	FILE* f = _wfopen(Utils::ToWideString(at.path).c_str(), L"ab");
#else
	FILE* f = std::fopen(at.path.c_str(), "ab");
#endif
	if (!f) return;
	std::fwrite(line.data(), 1, line.size(), f);
	std::fclose(f);
}

int SceneType() {
	return Scene::instance ? static_cast<int>(Scene::instance->type) : static_cast<int>(Scene::Null);
}

bool Idle() {
	return SceneType() == Scene::Map && !Game_Map::GetInterpreter().IsRunning() && !Game_Message::IsMessageActive();
}

void BuildMapList() {
	auto scan = [](const std::vector<lcf::rpg::EventCommand>& list) {
		for (const auto& c : list) {
			if (static_cast<int>(c.code) != static_cast<int>(lcf::rpg::EventCommand::Code::Teleport) || c.parameters.size() < 3) continue;
			int dir = c.parameters.size() > 3 ? c.parameters[3] - 1 : -1;
			at.dests.emplace(c.parameters[0], std::array<int, 3>{ c.parameters[1], c.parameters[2], dir });
		}
	};
	std::vector<int> all;
	for (const auto& info : lcf::Data::treemap.maps) {
		if (info.type != lcf::rpg::TreeMap::MapType_map || info.ID <= 0) continue;
		all.push_back(info.ID);
		auto m = Game_Map::LoadMapFile(info.ID);
		if (!m) continue;
		for (const auto& ev : m->events) for (const auto& pg : ev.pages) scan(pg.event_commands);
	}
	for (const auto& ce : lcf::Data::commonevents) scan(ce.event_commands);
	std::vector<int> ids{ lcf::Data::treemap.start.party_map_id };
	for (const auto& d : at.dests) ids.push_back(d.first);
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id) { return std::find(all.begin(), all.end(), id) == all.end(); }), ids.end());
	if (ids.size() <= 1) ids = all;
	if (static_cast<int>(ids.size()) > at.max_maps) ids.resize(at.max_maps);
	at.maps = ids;
}

void Finish() {
	at.done = true;
	AtLog("DONE");
	Player::exit_flag = true;
}

void NextMap() {
	if (at.target) {
		int now = Game_Map::GetMapId();
		AtLog("MAP " + std::to_string(at.target) + (now == at.target ? std::string(" ok") : " not reached (now " + std::to_string(now) + ")") +
			" scene=" + std::to_string(SceneType()));
	}
	at.index++;
	at.map_frames = 0;
	if (at.index >= static_cast<int>(at.maps.size())) {
		Finish();
		return;
	}
	int id = at.maps[at.index];
	int x = 0, y = 0, dir = -1;
	auto it = at.dests.find(id);
	if (it != at.dests.end()) {
		x = it->second[0]; y = it->second[1]; dir = it->second[2];
	} else {
		auto m = Game_Map::LoadMapFile(id);
		if (m) { x = m->width / 2; y = m->height / 2; }
	}
	AtLog("GO " + std::to_string(id));
	Main_Data::game_player->ReserveTeleport(id, x, y, dir, TeleportTarget::eForegroundTeleport);
	at.target = id;
}

void AutotestTick() {
	if (!at.checked) {
		at.checked = true;
#ifdef _WIN32
		// The environment is in the ANSI codepage; read it as UTF-16 so paths with Korean names work.
		const wchar_t* wp = _wgetenv(L"RR_AUTOTEST");
		at.enabled = wp && *wp;
		if (!at.enabled) return;
		at.path = Utils::FromWideString(wp);
#else
		const char* p = std::getenv("RR_AUTOTEST");
		at.enabled = p && *p;
		if (!at.enabled) return;
		at.path = p;
#endif
		if (const char* m = std::getenv("RR_AUTOTEST_MAX")) at.max_maps = std::max(1, std::atoi(m));
		if (const char* f = std::getenv("RR_AUTOTEST_FRAMES")) at.per_map = std::max(30, std::atoi(f));
		AtLog("BOOT easyrpg");
	}
	if (!at.enabled || at.done) return;
	at.frame++;
	int scene = SceneType();
	if (scene != at.last_scene) {
		at.last_scene = scene;
		if (++at.scene_logs <= 200) AtLog("SCENE " + std::to_string(scene));
	}

	// Confirm key; while a repeated choice still needs DOWN presses, press those first.
	// Title: the cursor starts on Continue when saves exist; move it up to New Game once per title visit.
	if (at.phase == 0 && SceneType() == Scene::Title) {
		if (!at.title_fixed && at.frame % 12 == 6) {
			at.title_fixed = true;
			if (FileFinder::HasSavegame()) Input::SimulateButtonPress(Input::UP);
		}
	} else {
		at.title_fixed = false;
	}
	bool need_down = at.choice && at.downs_done < at.downs_needed;
	if (need_down && at.frame % 12 == 6) {
		Input::SimulateButtonPress(Input::DOWN);
		at.downs_done++;
	} else if (!need_down && at.frame % 12 == 0) {
		// After the map's time is up, confirm only closes an open message; pressing it otherwise would restart the
		// conversation with the event in front forever. On the name screen, wait until the cursor is on <Done>.
		bool map_done = at.phase == 1 && SceneType() == Scene::Map && at.map_frames >= at.per_map && !Game_Message::IsMessageActive();
		if (!map_done && SceneType() != Scene::Name) Input::SimulateButtonPress(Input::DECISION);
	}

	// The test only presses OK, so fights can drag on forever (or be lost again and again). After ~10 s of battle,
	// defeat the enemies so the game's normal victory flow still runs.
	if (scene == Scene::Battle) {
		// Only at a safe point (option/command menu): killing enemies while a target window is open crashed.
		if (++at.battle_frames >= 600 && !at.battle_won && Main_Data::game_enemyparty) {
			if (static_cast<Scene_Battle*>(Scene::instance.get())->RocketForceVictory()) {
				at.battle_won = true;
				AtLog("BATTLE too long -> victory");
			}
		}
	} else {
		at.battle_frames = 0;
		at.battle_won = false;
	}

	// Name entry: Cancel only deletes letters. Move the cursor to <Done> (bottom-right: Up, Left wrap around);
	// the regular confirm presses then finish it (an empty name becomes the actor's default name first).
	if (scene == Scene::Name) {
		// Name entry: keep the actor's current name and leave, the same way the scene closes itself (done in PostUpdate).
		if (++at.name_frames > 60) at.name_finish = true;
	} else {
		at.name_step = 0;
		at.name_frames = 0;
	}

	if (at.phase == 0) {
		if (scene == Scene::Map && Game_Map::GetMapId() > 0 && ++at.off_map > 60) {
			at.off_map = 0;
			BuildMapList();
			AtLog("START map=" + std::to_string(Game_Map::GetMapId()) + " maps=" + std::to_string(at.maps.size()));
			at.phase = 1;
			NextMap();
		} else if (at.frame > 3600) {
			AtLog("STUCK before map scene=" + std::to_string(scene));
			Finish();
		} else if (at.frame >= 900 && !at.forced && scene == Scene::Title && !Transition::instance().IsActive() && !Scene::IsAsyncPending()) {
			at.forced = true;
			static_cast<Scene_Title*>(Scene::instance.get())->CommandNewGame();
			AtLog("FORCED new game");
		} else if ((scene == Scene::Save || scene == Scene::Load) && at.frame % 12 == 3) {
			// Leave like a player would (popping scenes from here leaves dangling drawables).
			Input::SimulateButtonPress(Input::CANCEL);
		}
		return;
	}

	if (scene == Scene::Map) {
		at.off_map = 0;
		// Walk like a player (not while a message is open).
		if (!Game_Message::IsMessageActive()) {
			int w = at.map_frames / 20;
			static const Input::InputButton dirs[4] = { Input::DOWN, Input::LEFT, Input::RIGHT, Input::UP };
			if (w % 4 != 3) Input::SimulateButtonPress(dirs[(w * 7 + at.index) % 4]);
		}
	} else if (scene != Scene::Battle && scene != Scene::Name && ++at.off_map > 180) {
		// Menus / shops / save screens opened by events: back out with Cancel like a player.
		if (at.off_map == 181) AtLog("LEFT MAP scene=" + std::to_string(scene) + " -> cancel");
		if (at.frame % 12 == 3) Input::SimulateButtonPress(Input::CANCEL);
	}
	at.map_frames++;
	// Never teleport out of a battle or menu: only from the map, once events finished (or after a long wait on the map).
	if (scene == Scene::Map && at.map_frames >= at.per_map && (Idle() || (at.map_frames >= at.per_map + 600 && !Game_Message::IsMessageActive()) || at.map_frames >= at.per_map + 2400)) NextMap();
}

void AutotestMessage(const std::vector<std::string>& lines, int choices) {
	if (!at.enabled) return;
	at.choice = choices > 0;
	if (!at.choice) return;
	std::string key;
	for (auto& l : lines) key += l + "\n";
	int seen = ++at.choice_seen[key];
	at.downs_needed = (seen - 1) % std::max(choices, 1);
	at.downs_done = 0;
}
} // namespace

void LogicTick() {
	AutotestTick();
}

void PostUpdate() {
	if (!at.enabled || !at.name_finish) return;
	at.name_finish = false;
	if (SceneType() == Scene::Name) {
		AtLog("NAME entry -> keep default");
		Scene::Pop();
	}
}

void Tick() {
	if (!Init()) return;
	++st.frame;
#ifdef _WIN32
	Connect();
	if (st.pipe == INVALID_HANDLE_VALUE) return;
#endif
	// 파이프 왕복은 게임 루프를 잠깐 멈추므로 최소로: 6프레임마다 텔레메트리(바뀐 경우) 또는 하트비트 한 번.
	// 답에 실려 오는 명령은 이 왕복으로 받습니다. (ESP는 게임 화면에 직접 그리므로 보내지 않습니다)
	if (st.frame % 3 == 0) {
		bool sent = false;
		if (st.has_mouse && st.frame % 6 == 3) { SendTile(); sent = true; }
		if (st.frame % 6 == 0) {
			auto t = Telemetry();
			if (t != st.last_telemetry) {
				st.last_telemetry = t;
				SendLine("T", t);
				sent = true;
			}
			if (!sent) SendLine("H");
		}
	}
}

float SpeedFactor() {
	if (at.enabled && !at.done) return 4.0f; // autotest runs fast
	return st.enabled ? st.speed : 1.0f;
}

bool Paused() {
	return st.enabled && st.paused;
}

bool Noclip() {
	return st.enabled && st.noclip;
}

bool Skip() {
	return st.enabled && st.skip && st.msg_busy;
}

bool WantAdvance() {
	if (!st.enabled || !st.msg_busy) return false;
	if (st.force_advance) {
		st.force_advance = false;
		st.waiting = false;
		return true;
	}
	if (st.skip) return true;
	if (!st.auto_msg) {
		st.waiting = false;
		return false;
	}
	auto now = Clock::now();
	if (!st.waiting) {
		st.waiting = true;
		st.wait_since = now;
	}
	int len = static_cast<int>(st.msg_text.size() / 3); // UTF-8 한글 기준 대략적인 글자 수
	double wait = std::min(4.5, std::max(0.75, 1.1 + len * 0.03)) / st.auto_speed;
	if (std::chrono::duration<double>(now - st.wait_since).count() >= wait) {
		st.waiting = false;
		return true;
	}
	return false;
}

void OnMessageStart(const std::vector<std::string>& lines, int choices) {
	AutotestMessage(lines, choices);
	if (!Init()) return;
	std::string text;
	for (auto& l : lines) {
		if (!text.empty()) text += '\n';
		text += l;
	}
	st.msg_busy = true;
	st.msg_text = CleanMessage(text);
	st.waiting = false;
	SendLine("M", std::string("1") + SEP_F + SEP_F + st.msg_text);
}

void OnMessageEnd() {
	at.choice = false;
	if (!Init() || !st.msg_busy) return;
	st.msg_busy = false;
	st.waiting = false;
	SendLine("M", std::string("0") + SEP_F + SEP_F);
}

namespace {
int TriggerClass(int trigger) {
	switch (trigger) {
	case 0: return 0;           // 결정키
	case 1: case 2: return 1;   // 접촉
	case 3: return 2;           // 자동실행
	case 4: return 3;           // 병렬처리
	default: return 4;
	}
}

// ESP: RocketRPG 창 위 투명 창에 그리면 매 프레임 창 전체를 다시 합성해야 해서 걸을 때 크게 버벅였습니다.
// 게임 화면에 직접 그리면 캐릭터와 정확히 같은 프레임에 움직이고 비용도 거의 없습니다.
void DrawEsp(Bitmap& surface) {
	if (!st.esp || !OnMap()) return;
	if (Game_Map::GetMapId() != st.esp_map) {
		st.esp_labels.clear();
		st.esp_map = Game_Map::GetMapId();
	}
	static const Color pal[5] = {
		Color(0, 153, 255, 255), Color(0, 204, 68, 255), Color(255, 136, 0, 255), Color(187, 51, 255, 255), Color(0, 255, 255, 255)
	};
	int ox, oy;
	RenderOffset(ox, oy);
	const int sw = surface.GetWidth(), sh = surface.GetHeight();
	FontRef font;
	// 게임 해상도(320x240)에서는 12px 이름표도 커서, 겹치는 이름표는 먼저 그린 것만 보여 줍니다 (상자는 모두 그림).
	std::vector<Rect> placed;
	for (auto& ev : Game_Map::GetEvents()) {
		if (!ev.IsActive()) continue;
		const int x = ev.GetScreenX() - TILE_SIZE / 2 + ox;
		const int y = ev.GetScreenY() - TILE_SIZE + oy;
		if (x <= -TILE_SIZE || y <= -TILE_SIZE || x >= sw || y >= sh + TILE_SIZE) continue;
		const Color& c = pal[TriggerClass(static_cast<int>(ev.GetTrigger()))];
		surface.FillRect(Rect(x, y, TILE_SIZE, TILE_SIZE), Color(c.red, c.green, c.blue, 60));
		surface.FillRect(Rect(x, y, TILE_SIZE, 1), c);
		surface.FillRect(Rect(x, y + TILE_SIZE - 1, TILE_SIZE, 1), c);
		surface.FillRect(Rect(x, y + 1, 1, TILE_SIZE - 2), c);
		surface.FillRect(Rect(x + TILE_SIZE - 1, y + 1, 1, TILE_SIZE - 2), c);

		auto& label = st.esp_labels[ev.GetId()];
		if (!label) {
			if (!font) font = Font::DefaultBitmapFont();
			std::string name(ev.GetName());
			if (name.empty()) name = "EV" + std::to_string(ev.GetId());
			// 긴 이름은 3칸 너비로 자릅니다
			const int max_w = TILE_SIZE * 3;
			if (Text::GetSize(*font, name).width > max_w) {
				auto u = Utils::DecodeUTF32(name);
				while (u.size() > 1 && Text::GetSize(*font, Utils::EncodeUTF(u) + "..").width > max_w) u.pop_back();
				name = Utils::EncodeUTF(u) + "..";
			}
			Rect sz = Text::GetSize(*font, name);
			label = Bitmap::Create(std::max(1, sz.width + 2), std::max(1, sz.height), Color(0, 0, 0, 170));
			Text::Draw(*label, 1, 0, *font, Color(255, 255, 255, 255), name);
		}
		Rect lr(x + (TILE_SIZE - label->GetWidth()) / 2, y - label->GetHeight(), label->GetWidth(), label->GetHeight());
		bool overlap = false;
		for (auto& p : placed) {
			if (lr.x < p.x + p.width && p.x < lr.x + lr.width && lr.y < p.y + p.height && p.y < lr.y + lr.height) { overlap = true; break; }
		}
		if (overlap) continue;
		placed.push_back(lr);
		surface.Blit(lr.x, lr.y, *label, label->GetRect(), Opacity::Opaque());
	}
}

void ApplyScreenEffects(Bitmap& surface) {
	Rect r(0, 0, surface.GetWidth(), surface.GetHeight());
	if (st.crt) {
		// CRT 주사선: 게임 해상도 기준 홀수 줄을 어둡게 (확대 시 브라운관 느낌)
		for (int y = 1; y < r.height; y += 2)
			surface.FillRect(Rect(0, y, r.width, 1), Color(0, 0, 0, 72));
	}
	float b = st.bright;
	if (b > 0.99f && b < 1.01f) return;
	if (b < 1.0f) {
		int a = std::max(0, std::min(240, static_cast<int>((1.0f - b) * 255)));
		surface.FillRect(r, Color(0, 0, 0, a));
	} else {
		if (!st.white || st.white->GetWidth() != r.width || st.white->GetHeight() != r.height) {
			st.white = Bitmap::Create(r.width, r.height, Color(255, 255, 255, 255));
		}
		int a = std::max(0, std::min(200, static_cast<int>((b - 1.0f) * 70)));
		surface.Blit(0, 0, *st.white, r, Opacity(a), Bitmap::BlendMode::Additive);
	}
}
} // namespace

void ApplyBrightness(Bitmap& surface) {
	if (!st.enabled) return;
	ApplyScreenEffects(surface);
	DrawEsp(surface); // 밝기와 상관없이 잘 보이도록 마지막에
}

} // namespace RocketBridge
