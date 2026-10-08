#include "game/phz/config.hpp"

#include <sstream>

namespace pandora {
namespace phz {

std::string ConfigSnapshotJson(const PhzConfig& c) {
  std::ostringstream os;
  os << "{"
     << "\"min_hu_xi\":" << c.min_hu_xi << ","
     << "\"dian_pao\":" << (c.dian_pao ? "true" : "false") << ","
     << "\"force_wei\":" << (c.force_wei ? "true" : "false") << ","
     << "\"force_pao\":" << (c.force_pao ? "true" : "false") << ","
     << "\"force_ti\":" << (c.force_ti ? "true" : "false") << ","
     << "\"zimo_mode\":\"" << c.zimo_mode << "\","
     << "\"san_ti_wu_kan\":" << (c.san_ti_wu_kan ? "true" : "false")
     << "}";
  return os.str();
}

}  // namespace phz
}  // namespace pandora
