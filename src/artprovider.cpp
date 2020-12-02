// Copyright 2020 Allan Riordan Boll

#include "src/artprovider.h"

#include <wx/artprov.h>
#include <wx/wx.h>

#include <map>
#include <string>
#include <tuple>

#include "graphics/appicon/icon_64x64.xpm"
#include "graphics/ui/uiicons.data.h"

using std::get;
using std::make_tuple;
using std::map;
using std::string;
using std::to_string;
using std::tuple;

ArtProvider::ArtProvider() {
#define ADD_IMG(ART_ID, NAME) \
    this->art[#ART_ID "_16_light_png"] = make_tuple(NAME##_16_light_png, WXSIZEOF(NAME##_16_light_png)); \
    this->art[#ART_ID "_16_dark_png"] = make_tuple(NAME##_16_dark_png, WXSIZEOF(NAME##_16_dark_png)); \
    this->art[#ART_ID "_24_light_png"] = make_tuple(NAME##_24_light_png, WXSIZEOF(NAME##_24_light_png)); \
    this->art[#ART_ID "_24_dark_png"] = make_tuple(NAME##_24_dark_png, WXSIZEOF(NAME##_24_dark_png)); \
    this->art[#ART_ID "_32_light_png"] = make_tuple(NAME##_32_light_png, WXSIZEOF(NAME##_32_light_png)); \
    this->art[#ART_ID "_32_dark_png"] = make_tuple(NAME##_32_dark_png, WXSIZEOF(NAME##_32_dark_png)); \
    this->art[#ART_ID "_48_light_png"] = make_tuple(NAME##_48_light_png, WXSIZEOF(NAME##_48_light_png)); \
    this->art[#ART_ID "_48_dark_png"] = make_tuple(NAME##_48_dark_png, WXSIZEOF(NAME##_48_dark_png)); \
    this->art[#ART_ID "_64_light_png"] = make_tuple(NAME##_64_light_png, WXSIZEOF(NAME##_64_light_png)); \
    this->art[#ART_ID "_64_dark_png"] = make_tuple(NAME##_64_dark_png, WXSIZEOF(NAME##_64_dark_png));

    ADD_IMG(_parent_dir, _parent_dir)
    ADD_IMG(_nav_back, _nav_back)
    ADD_IMG(_nav_fwd, _nav_fwd)
    ADD_IMG(_refresh, _refresh)
    ADD_IMG(_open_file, _open_file)
    ADD_IMG(_new_file, _new_file)
    ADD_IMG(_new_dir, _new_dir)
    ADD_IMG(_rename, _rename)
    ADD_IMG(_delete, _delete)
    ADD_IMG(_upload, _upload)
    ADD_IMG(_download, _download)
    ADD_IMG(wxART_FOLDER, _directory)
    ADD_IMG(wxART_NORMAL_FILE, _file)
    ADD_IMG(wxART_EXECUTABLE_FILE, _file_exec)
    ADD_IMG(_symlink, _symlink)
    ADD_IMG(_file_picture, _file_picture)
    ADD_IMG(_package, _package)
}

wxBitmap ArtProvider::CreateBitmap(const wxArtID &id, const wxArtClient &client, const wxSize &size) {
    string color = "light";
    if (wxSystemSettings::GetAppearance().IsDark()) {
        color = "dark";
    }

    auto id2 = string(id);
    replace(id2.begin(), id2.end(), '-', '_');

    int width = size.GetWidth();
    if (width > 64) {
        width = 64;
    }
    if (width < 16) {
        width = 16;
    }
    string key;
    while (1) {
        key = id2 + "_" + to_string(width) + "_" + color + "_png";
        if (this->art.find(key) != this->art.end()) {
            break;  // We found a match.
        }

        width--;  // Try increasing decreasing find a smaller image if we didn't find an exact match.
        if (width < 16) {
            return wxNullBitmap;
        }
    }

    auto t = this->art[key];
    return wxBitmap::NewFromPNGData(get<0>(t), get<1>(t));
}

void ArtProvider::CleanUpProviders() {
    wxArtProvider::CleanUpProviders();
}

const char **ArtProvider::GetAppIcon() {
    return icon_64x64;
}
