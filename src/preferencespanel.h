// Copyright 2020 Allan Riordan Boll

#ifndef SRC_PREFERENCESPANEL_H_
#define SRC_PREFERENCESPANEL_H_

#include <wx/config.h>
#include <wx/preferences.h>
#include <wx/wx.h>

#include <string>

using std::string;

string guessTextEditor();

class PreferencesPageGeneralPanel : public wxPanel {
    wxConfigBase *config_;
    wxTextCtrl *editor_path_;
    wxChoice *size_units_;

public:
    PreferencesPageGeneralPanel(wxWindow *parent, wxConfigBase *config);

    virtual bool TransferDataToWindow();

    virtual bool TransferDataFromWindow();
};


class PreferencesPageGeneral : public wxStockPreferencesPage {
    wxConfigBase *config;

public:
    explicit PreferencesPageGeneral(wxConfigBase *config);

    virtual wxWindow *CreateWindow(wxWindow *parent);
};


#endif  // SRC_PREFERENCESPANEL_H_
