#include "alo_export.h"

#include "scene_walker.h"

#include "alamo_format/ala_anim.h"
#include "alamo_format/alo_build.h"
#include "alamo_format/chunk_tree.h"
#include "alamo_format/export_scene.h"

#include <Max.h>
#include <maxapi.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace max2alamo {

namespace {

// Convert a Max TSTR (wide on Unicode builds) to UTF-8 -- used here for
// error messages we hand back to Max.
std::string to_utf8(const TCHAR* s)
{
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
}

bool write_all(const TCHAR* path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

}  // namespace

int AloExport::ExtCount()
{
    return 1;
}

const TCHAR* AloExport::Ext(int n)
{
    switch (n) {
        case 0:  return _T("ALO");
        default: return _T("");
    }
}

const TCHAR* AloExport::LongDesc()
{
    return _T("Alamo Object File (Empire at War / Forces of Corruption)");
}

const TCHAR* AloExport::ShortDesc()
{
    return _T("Alamo Object");
}

const TCHAR* AloExport::AuthorName()
{
    return _T("max2alamo-2026 contributors");
}

const TCHAR* AloExport::CopyrightMessage()
{
    return _T("MIT License -- see LICENSE in https://github.com/DrKnickers/max2alamo-2026");
}

const TCHAR* AloExport::OtherMessage1()  { return _T(""); }
const TCHAR* AloExport::OtherMessage2()  { return _T(""); }

unsigned int AloExport::Version()
{
    return 1;  // version * 100 in some Max conventions; stub for now.
}

void AloExport::ShowAbout(HWND hWnd)
{
    MessageBox(
        hWnd,
        _T("max2alamo-2026 (Phase 3 scaffold)\n\n")
        _T("Exports 3ds Max scenes to Petroglyph .alo / .ala\n")
        _T("for Star Wars: Empire at War and Forces of Corruption.\n\n")
        _T("This is a pre-release scaffold. Export functionality\n")
        _T("lands in subsequent phases."),
        _T("About max2alamo"),
        MB_OK | MB_ICONINFORMATION);
}

int AloExport::DoExport(const TCHAR*  name,
                        ExpInterface* /*ei*/,
                        Interface*    i,
                        BOOL          suppress_prompts,
                        DWORD         /*options*/)
{
    HWND parent_hwnd = i ? i->GetMAXHWnd() : nullptr;
    auto report = [&](const TCHAR* msg, UINT icon) {
        if (!suppress_prompts) MessageBox(parent_hwnd, msg, _T("max2alamo"), MB_OK | icon);
    };

    // 1. Walk the scene.
    alamo_format::ExportScene scene;
    std::vector<ClipAnimation> clips;
    std::string walker_err;
    if (!walk_scene(i, scene, clips, walker_err)) {
        std::wstring wmsg(walker_err.begin(), walker_err.end());
        report((std::wstring(L"Scene walk failed:\n\n") + wmsg).c_str(), MB_ICONERROR);
        return IMPEXP_FAIL;
    }
    if (scene.meshes.empty() && scene.lights.empty() && scene.proxies.empty()) {
        report(_T("Nothing to export: no exportable mesh, light, or proxy nodes "
                  "were found in the scene."),
               MB_ICONWARNING);
        return IMPEXP_FAIL;
    }

    // 2. Validate Phase 4 constraints up-front -- 16-bit face indices.
    for (const auto& m : scene.meshes) {
        for (const auto& s : m.submeshes) {
            if (s.vertices.size() > 0xFFFFu) {
                report(_T("A submesh has more than 65,535 vertices, which exceeds the ")
                       _T("16-bit face-index limit for the current vertex chunk format. ")
                       _T("Split the mesh or wait for vertex welding (planned for a later phase)."),
                       MB_ICONERROR);
                return IMPEXP_FAIL;
            }
        }
    }

    // 3. Serialize.
    std::vector<std::uint8_t> bytes;
    try {
        auto tree = alamo_format::build_alo(scene);
        bytes = alamo_format::write_chunk_tree(tree);
    } catch (const std::exception& e) {
        std::string what = e.what();
        std::wstring wwhat(what.begin(), what.end());
        report((std::wstring(L"Failed to build .alo bytes:\n\n") + wwhat).c_str(),
               MB_ICONERROR);
        return IMPEXP_FAIL;
    }

    // 4. Write to disk.
    if (!write_all(name, bytes)) {
        report(_T("Failed to write the output file. Check the filename and that the ")
               _T("destination is writable."), MB_ICONERROR);
        return IMPEXP_FAIL;
    }

    // 4.25 Phase 8b / Phase 11b: write one sibling .ala per authored
    // clip. Filename pattern:
    //   - Multi-clip path (`clip.name` non-empty): <basename>_<NAME>.ala
    //   - Single-clip back-compat (`clip.name` empty):       <basename>.ala
    // Best-effort -- if the typed pipeline throws on one clip, log it
    // and continue with the rest rather than failing the .alo export
    // the user already paid for (partial-export policy, 11a wrap).
    struct AlaEmission {
        std::string  clip_name;        // empty for single-clip back-compat
        std::wstring path;
        std::size_t  bytes_written  = 0;
        bool         wrote_ok       = false;
        bool         has_anim_track = false;
        const alamo_format::AlaAnimation* anim = nullptr;
    };
    std::vector<AlaEmission> emissions;
    emissions.reserve(clips.size());

    // Compute the basename prefix once: strip the trailing ".alo"/".ALO"
    // (case-insensitive) so multi-clip siblings can append "_<NAME>.ala".
    std::wstring basename_w = name;
    if (basename_w.size() >= 4) {
        std::wstring tail = basename_w.substr(basename_w.size() - 4);
        for (auto& c : tail) c = static_cast<wchar_t>(towlower(c));
        if (tail == L".alo") basename_w.resize(basename_w.size() - 4);
    }

    for (const auto& clip : clips) {
        AlaEmission em;
        em.clip_name = clip.name;
        em.anim      = &clip.anim;

        // Phase 8d: a clip with only animated visibility still gets a
        // sibling .ala (track_leaves carries 0x1007 chunks).
        for (const auto& b : clip.anim.bones) {
            if (b.idx_rotation >= 0 || b.idx_translation >= 0 ||
                !b.track_leaves.empty()) {
                em.has_anim_track = true;
                break;
            }
        }
        if (!em.has_anim_track) {
            emissions.push_back(std::move(em));
            continue;
        }

        em.path = basename_w;
        if (!clip.name.empty()) {
            em.path.push_back(L'_');
            // Clip names are ASCII per the authoring spec; widen each
            // byte. Non-ASCII bytes (if any leaked past the walker)
            // become the same wchar_t value -- still a valid filename
            // path on Windows.
            for (char c : clip.name) em.path.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        em.path += L".ala";

        try {
            auto ala_tree  = alamo_format::build_ala(clip.anim);
            auto ala_bytes = alamo_format::write_chunk_tree(ala_tree);
            if (write_all(em.path.c_str(), ala_bytes)) {
                em.wrote_ok      = true;
                em.bytes_written = ala_bytes.size();
            }
        } catch (const std::exception&) {
            // swallowed; .ala emission is best-effort per-clip
        }
        emissions.push_back(std::move(em));
    }

    const std::size_t n_clips_written = std::count_if(
        emissions.begin(), emissions.end(),
        [](const AlaEmission& e) { return e.wrote_ok; });

    // 4.5 Drop a sibling .export.log explaining what we saw on the
    // material side, for debugging "why did THIS texture get exported"
    // questions. Best-effort -- failure to write the log doesn't fail
    // the export.
    {
        std::string log;
        log_material_diagnostics(i, log);
        log_scene_summary(scene, log);

        // Phase 12: per-mesh walker diagnostics (shadow-volume closed-
        // manifold violations are the first user). Each warning is one
        // pre-formatted line; just terminate with a newline.
        for (const auto& m : scene.meshes) {
            for (const auto& w : m.warnings) {
                log += w;
                log += '\n';
            }
        }
        if (!emissions.empty()) {
            char header[128];
            std::snprintf(header, sizeof(header),
                "\nAnimation: %zu clip(s) declared, %zu written\n",
                emissions.size(), n_clips_written);
            log += header;
            for (const auto& em : emissions) {
                std::size_t n_vis_tracks = 0;
                if (em.anim) {
                    for (const auto& b : em.anim->bones) {
                        for (const auto& leaf : b.track_leaves) {
                            if (leaf.id == 0x1007) { ++n_vis_tracks; break; }
                        }
                    }
                }
                const char* status =
                    em.wrote_ok        ? "WROTE"
                  : em.has_anim_track  ? "FAILED"
                                       : "SKIPPED (no animation tracks)";
                std::string clip_label = em.clip_name.empty() ? "<single>" : em.clip_name;
                char clip_line[320];
                if (em.anim) {
                    std::snprintf(clip_line, sizeof(clip_line),
                        "  [%s] %s: %u frames @ %.2f fps, %zu bone(s), "
                        "%u rotation word(s), %u translation word(s), "
                        "%zu visibility track(s); %zu bytes\n",
                        status, clip_label.c_str(),
                        em.anim->n_frames, static_cast<double>(em.anim->fps),
                        em.anim->bones.size(),
                        em.anim->n_rotation_words, em.anim->n_translation_words,
                        n_vis_tracks, em.bytes_written);
                } else {
                    std::snprintf(clip_line, sizeof(clip_line),
                        "  [%s] %s: (no animation data)\n",
                        status, clip_label.c_str());
                }
                log += clip_line;
            }
        }
        std::wstring log_path = std::wstring(name) + L".export.log";
        std::ofstream lf(log_path, std::ios::binary | std::ios::trunc);
        if (lf) lf.write(log.data(), static_cast<std::streamsize>(log.size()));
    }

    // 5. Success message.
    if (!suppress_prompts) {
        TCHAR msg[512];
        _sntprintf_s(msg, _TRUNCATE,
            _T("Export complete.\n\n")
            _T("File: %s\n")
            _T("Bones: %zu\n")
            _T("Meshes: %zu\n")
            _T("Bytes written: %zu"),
            name, scene.bones.size(), scene.meshes.size(), bytes.size());
        report(msg, MB_ICONINFORMATION);
    }
    return IMPEXP_SUCCESS;
}

}  // namespace max2alamo
