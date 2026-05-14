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

// Phase 10d: the LEGACY Petroglyph max2alamo plugin (Max 9 era, shipped
// with Empire at War SDK) registered its helper class with a different
// Class_ID. Files saved with that legacy plugin reference this ID; in
// Max 2026 with only the modern Class_ID registered, Max substitutes
// them as `Missing_Helper` placeholders on load and the proxies lose
// their class identity.
//
// Identified via Phase 10a's .max OLE-storage parse
// (re/scripts/dump_max_class_table.py against ThrREv Ascendancy
// fixtures -- MC80_D + IFTX_D reference this exact ID for their
// Missing_Helper-substituted nodes, with class table description
// "Exporter and tools for the Alamo engine" naming the legacy plugin).
//
// We register a second ClassDesc claiming this ID so legacy files load
// with real `Alamo_Proxy` instances. The legacy ClassDesc is hidden
// (`IsPublic() == FALSE`) so it doesn't appear in Create > Helpers >
// Standard alongside the modern Alamo_Proxy -- modern authoring still
// uses kAlamoProxyClassID. Since `AlamoProxyHelper::ClassID()` always
// returns the MODERN Class_ID, files opened via the legacy ClassDesc
// auto-migrate to the modern Class_ID on save -- one open+save cycle
// upgrades a file to modern conventions, and subsequent opens find
// the modern ID directly without going through the shim.
inline constexpr Class_ID kLegacyAlamoProxyClassID(0x52263841, 0x08194485);

// Singleton accessors -- consumed by LibClassDesc in plugin_entry.cpp.
ClassDesc* GetAlamoProxyHelperClassDesc();
ClassDesc* GetLegacyAlamoProxyHelperClassDesc();

}  // namespace max2alamo
