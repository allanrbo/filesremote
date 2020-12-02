// Copyright 2020 Allan Riordan Boll

#ifndef SRC_ARTPROVIDER_H_
#define SRC_ARTPROVIDER_H_

#include <wx/artprov.h>
#include <wx/wx.h>

#include <map>
#include <string>
#include <tuple>

#include "src/artprovider.h"

using std::map;
using std::string;
using std::tuple;

class ArtProvider : public wxArtProvider {
    map<string, tuple<const unsigned char *, int>> art;

public:
    ArtProvider();

    virtual wxBitmap CreateBitmap(const wxArtID &id, const wxArtClient &client, const wxSize &size);

    static void CleanUpProviders();

    static const char **GetAppIcon();
};

#endif  // SRC_ARTPROVIDER_H_
