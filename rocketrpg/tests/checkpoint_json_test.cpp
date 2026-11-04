#include "../../src/rocket_checkpoint_packet.h"
#include <cassert>
#include <iostream>

int main() {
    lcf::rpg::Save save;
    save.system.variables = {0, 731, -123};
    save.system.switches = {false, true, false};
    save.party_location.map_id = 17;
    save.party_location.position_x = 12;
    save.party_location.easyrpg_runtime_flags.reserved_1 = true;
    save.map_info.events.resize(1);
    save.map_info.events[0].ID = 7;
    save.map_info.events[0].position_y = 21;
    save.map_info.events[0].move_route.move_commands.resize(1);
    save.map_info.events[0].move_route.move_commands[0].parameter_c = 315;
    save.pictures.resize(1);
    save.pictures[0].ID = 12;
    save.pictures[0].flags.erase_on_battle_end = true;
    save.actors.resize(1);
    save.actors[0].ID = 1;
    save.common_events.resize(1);
    save.common_events[0].ID = 2;
    save.common_events[0].parallel_event_execstate.stack.resize(1);
    save.common_events[0].parallel_event_execstate.stack[0].commands.resize(1);
    save.common_events[0].parallel_event_execstate.stack[0].commands[0].code = 10610;
    save.targets.resize(1);
    save.targets[0].ID = 3;
    save.easyrpg_data.windows.resize(1);
    save.easyrpg_data.windows[0].ID = 14;
    nlohmann::json data = save;
    auto copy = nlohmann::json::parse(data.dump()).get<lcf::rpg::Save>();
    assert(copy == save);
    assert(copy.map_info.events[0].ID == 7);
    assert(copy.map_info.events[0].position_y == 21);
    assert(copy.pictures[0].ID == 12);
    assert(copy.actors[0].ID == 1);
    assert(copy.common_events[0].ID == 2);
    assert(copy.targets[0].ID == 3);
    assert(copy.easyrpg_data.windows[0].ID == 14);
    assert(nlohmann::json(copy) == data);
    assert(RocketCheckpoint::ReadState(RocketCheckpoint::Parse(data.dump())) == save);
    auto rejects = [](const auto& action) {
        bool failed = false;
        try { action(); } catch (const std::exception&) { failed = true; }
        assert(failed);
    };
    rejects([] { RocketCheckpoint::Parse("null"); });
    rejects([] { RocketCheckpoint::Parse("{\"a\":1,\"a\":2}"); });
    rejects([] { RocketCheckpoint::Parse("[1e400]"); });
    rejects([] {
        std::string values = "[";
        for (int i = 0; i < 200001; ++i) values += "0,";
        values.back() = ']';
        RocketCheckpoint::Parse(values);
    });
    rejects([] { RocketCheckpoint::Parse(std::string(101, '[') + "0" + std::string(101, ']')); });
    rejects([] { RocketCheckpoint::Parse(std::string(RocketCheckpoint::MaxBytes + 1, ' ')); });
    rejects([&] { auto invalid = data; invalid["party_location"]["direction"] = 1000000000000LL; RocketCheckpoint::ReadState(invalid); });
    rejects([&] { auto invalid = data; invalid["foreground_event_execstate"]["stack"].push_back(lcf::rpg::SaveEventExecFrame{}); RocketCheckpoint::ReadState(invalid); });
    data.erase("party_location");
    bool rejected = false;
    try { data.get<lcf::rpg::Save>(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
    std::cout << "CHECKPOINT_JSON_OK\n";
}
