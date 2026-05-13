#pragma once

// Alamo_Proxy helper plugin class.
//
// Registers a non-rendering helper object in 3ds Max's Create panel
// (Create > Helpers > Standard > Alamo Proxy). Faithful clone of the
// legacy Petroglyph max2alamo plugin's helper class:
//
//   - Class name "Alamo_Proxy" (with underscore -- matches the name
//     Mike Lankamp's alamo2max.ms importer references at line 1344:
//     `local maxproxy = Alamo_Proxy name:proxy.name`)
//   - Displays as a small 3D cross at its origin
//   - Single-click placement in the viewport
//   - No parameters beyond the standard INode name (the node name is
//     the proxy's reference name, e.g. "p_engine_glow")
//
// Walker side (Phase 7c.2) detects these nodes by Class_ID match and
// emits a 0x603 proxy chunk for each.

#include <Max.h>
#include <iparamb2.h>

namespace max2alamo {

// Stable Class_ID. Must never change after first ship -- Max persists
// it in .max scenes that have ever placed an Alamo Proxy. Generated
// with genclassid; second part chosen distinct from AloExport
// (0x6ed3a4f1, 0x2b9c7d05) and AlamoUtility (0x6ed3a4f1, 0x4f51ab63).
inline constexpr Class_ID kAlamoProxyClassID(0x6ed3a4f1, 0x8a721d04);

// Singleton accessor for the helper's ClassDesc -- consumed by
// LibClassDesc in plugin_entry.cpp.
ClassDesc* GetAlamoProxyHelperClassDesc();

}  // namespace max2alamo
