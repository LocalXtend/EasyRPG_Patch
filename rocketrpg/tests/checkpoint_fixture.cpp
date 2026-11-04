// Only for owned test copies: retain event contents but require player action instead of autorun.
#include <lcf/lmu/reader.h>
#include <lcf/lmt/reader.h>
#include <lcf/rpg/eventpage.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::string root = argv[1];
    std::ifstream tree_input(root + "/RPG_RT.lmt", std::ios::binary);
    auto tree = lcf::LMT_Reader::Load(tree_input, argv[2]);
    if (!tree) return 5;
    std::ostringstream name;
    name << "Map" << std::setfill('0') << std::setw(4) << tree->start.party_map_id << ".lmu";
    std::string map_path = root + "/" + name.str();
    std::ifstream input(map_path, std::ios::binary);
    auto map = lcf::LMU_Reader::Load(input, argv[2]);
    if (!map) return 3;
    input.close();
    int changed = 0;
    for (auto& event : map->events) for (auto& page : event.pages)
        if (page.trigger == 3 || page.trigger == 4) { page.trigger = 0; ++changed; }
    std::ofstream output(map_path + ".checkpoint", std::ios::binary);
    bool saved = lcf::LMU_Reader::Save(output, *map, std::string(argv[3]) == "2003" ? lcf::EngineVersion::e2k3 : lcf::EngineVersion::e2k, argv[2]);
    if (!saved) return 4;
    std::cout << name.str() << '\n' << changed << " autorun pages changed\n";
}
