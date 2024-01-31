// Copyright 2020 Allan Riordan Boll

#include "src/preferencespanel.h"

#include <string>

#ifndef __WXOSX__

#include <filesystem>

#endif

#include <wx/config.h>
#include <wx/preferences.h>
#include <wx/wx.h>

using std::string;

#ifdef __WXOSX__
#include "src/filesystem.osx.polyfills.h"
#else
using std::filesystem::exists;
#endif


string guessTextEditor() {
#ifdef __WXMSW__
    if (exists("C:\\Program Files\\Sublime Text 3\\sublime_text.exe")) {
        return "C:\\Program Files\\Sublime Text 3\\sublime_text.exe";
    }
    if (exists("C:\\Program Files (x86)\\Sublime Text 3\\sublime_text.exe")) {
        return "C:\\Program Files (x86)\\Sublime Text 3\\sublime_text.exe";
    }
    if (exists("C:\\Program Files\\Sublime Text\\sublime_text.exe")) {
        return "C:\\Program Files\\Sublime Text\\sublime_text.exe";
    }
    if (exists("C:\\Program Files (x86)\\Sublime Text\\")) {
        return "C:\\Program Files (x86)\\Sublime Text\\sublime_text.exe";
    }
    if (exists("C:\\Program Files\\Microsoft VS Code\\Code.exe")) {
        return "C:\\Program Files\\Microsoft VS Code\\Code.exe";
    }
    string user =  wxGetUserId().ToStdString(wxMBConvUTF8());
    if (exists("C:\\Users\\" + user + "\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe")) {
        return "C:\\Users\\" + user + "\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe";
    }
    if (exists("C:\\Program Files\\Notepad++\\notepad++.exe")) {
        return "C:\\Program Files\\Notepad++\\notepad++.exe";
    }
    if (exists("C:\\Program Files (x86)\\Notepad++\\notepad++.exe")) {
        return "C:\\Program Files (x86)\\Notepad++\\notepad++.exe";
    }

    return "notepad";
#elif __WXOSX__
    if (exists("/Applications/Sublime Text.app/Contents/SharedSupport/bin/subl")) {
        return "/Applications/Sublime\\ Text.app/Contents/SharedSupport/bin/subl";
    }
    if (exists("/Applications/Visual Studio Code.app")) {
        return "open -a \"Visual Studio Code\"";
    }
    return "open -a \"TextEdit\"";
#else
    if (exists("/usr/bin/subl")) {
        return "/usr/bin/subl";
    }
    if (exists("/usr/bin/code")) {
        return "/usr/bin/code";
    }
    if (exists("/snap/bin/code")) {
        return "/snap/bin/code";
    }
    if (exists("/usr/bin/gedit")) {
        return "/usr/bin/gedit";
    }
    return "";
#endif
}


string guessVideoViewer() {
#ifdef __WXMSW__
    if (exists("C:\\Program Files\\Windows Media Player\\mplayer.exe")) {
        return "C:\\Program Files\\Windows Media Player\\mplayer.exe";
    }
    if (exists("C:\\Program Files (x86)\\Windows Media Player\\mplayer.exe")) {
        return "C:\\Program Files (x86)\\Windows Media Player\\mplayer.exe";
    }
    return "";
#elif __WXOSX__
    if (exists("/System/Applications/QuickTime\\ Player.app")) {
        return "open -a \"QuickTime\\ Player.app\"";
    }
    return "";
#else
    return "";
#endif
}


string guessImageViewer() {
#ifdef __WXMSW__
    if (exists("C:\\Program Files\\Windows Media Player\\mplayer.exe")) {
        return "C:\\Program Files\\Windows Media Player\\mplayer.exe";
    }
    if (exists("C:\\Program Files (x86)\\Windows Media Player\\mplayer.exe")) {
        return "C:\\Program Files (x86)\\Windows Media Player\\mplayer.exe";
    }
    return "";
#elif __WXOSX__
    if (exists("/System/Applications/Preview.app")) {
        return "open -a \"Preview.app\"";
    }
    return "";
#else
    return "";
#endif
}


PreferencesPageGeneralPanel::PreferencesPageGeneralPanel(wxWindow *parent, wxConfigBase *config) : wxPanel(parent) {
    this->config_ = config;

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto item_sizer_editor = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(item_sizer_editor, 0, wxGROW | wxALL, 5);
    auto label_editor = new wxStaticText(this, wxID_ANY, "Editor path:");
    item_sizer_editor->Add(label_editor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    item_sizer_editor->Add(5, 5, 1, wxALL, 0);
    this->editor_path_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(220, -1));
    item_sizer_editor->Add(this->editor_path_, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    auto editor_default_btn = new wxButton(this, wxID_ANY, "Default", wxDefaultPosition, wxSize(70, -1));
    item_sizer_editor->Add(editor_default_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    editor_default_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        this->editor_path_->SetValue(wxString::FromUTF8(guessTextEditor()));
    });

    auto item_sizer_image_viewer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(item_sizer_image_viewer, 0, wxGROW | wxALL, 5);
    auto label_image_viewer = new wxStaticText(this, wxID_ANY, "Image Viewer path:");
    item_sizer_image_viewer->Add(label_image_viewer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    item_sizer_image_viewer->Add(5, 5, 1, wxALL, 0);
    this->image_viewer_path_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(220, -1));
    item_sizer_image_viewer->Add(this->image_viewer_path_, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    auto image_viewer_default_btn = new wxButton(this, wxID_ANY, "Default", wxDefaultPosition, wxSize(70, -1));
    item_sizer_image_viewer->Add(image_viewer_default_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    image_viewer_default_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        this->image_viewer_path_->SetValue(wxString::FromUTF8(guessImageViewer()));
    });

    auto item_sizer_video_viewer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(item_sizer_video_viewer, 0, wxGROW | wxALL, 5);
    auto label_video_viewer = new wxStaticText(this, wxID_ANY, "Video Viewer path:");
    item_sizer_video_viewer->Add(label_video_viewer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    item_sizer_video_viewer->Add(5, 5, 1, wxALL, 0);
    this->video_viewer_path_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(220, -1));
    item_sizer_video_viewer->Add(this->video_viewer_path_, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    auto video_viewer_default_btn = new wxButton(this, wxID_ANY, "Default", wxDefaultPosition, wxSize(70, -1));
    item_sizer_video_viewer->Add(video_viewer_default_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    video_viewer_default_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        this->video_viewer_path_->SetValue(wxString::FromUTF8(guessVideoViewer()));
    });

    auto item_sizer_size_unit = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(item_sizer_size_unit, 0, wxGROW | wxALL, 5);
    auto label_size_unit = new wxStaticText(this, wxID_ANY, "File size units:");
    item_sizer_size_unit->Add(label_size_unit, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    item_sizer_size_unit->Add(5, 5, 1, wxALL, 0);
    this->size_units_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(300, -1));
    this->size_units_->Append("Automatic");
    this->size_units_->Append("Bytes");
    item_sizer_size_unit->Add(this->size_units_, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    this->SetSizerAndFit(sizer);
}

bool PreferencesPageGeneralPanel::TransferDataToWindow() {
    this->editor_path_->SetValue(this->config_->Read("/editor", ""));
    this->image_viewer_path_->SetValue(this->config_->Read("/image_viewer", ""));
    this->video_viewer_path_->SetValue(this->config_->Read("/video_viewer", ""));

    auto size_units = this->config_->Read("/size_units", "1");
    if (size_units == "2") {
        this->size_units_->SetSelection(1);
    } else {
        this->size_units_->SetSelection(0);
    }

    // Setting up the on-change binds here, so we only start monitoring for change after values have been loaded.
    this->editor_path_->Bind(wxEVT_TEXT, [&](wxCommandEvent &) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
    });
    this->image_viewer_path_->Bind(wxEVT_TEXT, [&](wxCommandEvent &) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
    });
    this->video_viewer_path_->Bind(wxEVT_TEXT, [&](wxCommandEvent &) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
    }); 
    this->size_units_->Bind(wxEVT_CHOICE, [&](wxCommandEvent &) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
    });

    return true;
}

bool PreferencesPageGeneralPanel::TransferDataFromWindow() {
    this->config_->Write("/editor", this->editor_path_->GetValue());
    this->config_->Write("/image_viewer", this->image_viewer_path_->GetValue());
    this->config_->Write("/video_viewer", this->video_viewer_path_->GetValue());

    if (this->size_units_->GetSelection() == 1) {
        this->config_->Write("/size_units", "2");
    } else {
        this->config_->Write("/size_units", "1");
    }

    this->config_->Flush();
    return true;
}

PreferencesPageGeneral::PreferencesPageGeneral(wxConfigBase *config) : wxStockPreferencesPage(Kind_General) {
    this->config = config;
}

wxWindow *PreferencesPageGeneral::CreateWindow(wxWindow *parent) {
    return new PreferencesPageGeneralPanel(parent, this->config);
}
