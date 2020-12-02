// Copyright 2020 Allan Riordan Boll

#include "src/connectdialog.h"

#include <wx/config.h>
#include <wx/secretstore.h>
#include <wx/wx.h>

#include <exception>
#include <string>

#include "src/artprovider.h"
#include "src/string.h"
#include "src/hostdesc.h"

using std::invalid_argument;

ConnectDialog::ConnectDialog(wxConfigBase *config) : wxDialog(
        NULL,
        wxID_ANY,
        "Connect to SSH/SFTP server",
        wxDefaultPosition,
        wxSize(400, 400),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    this->config_ = config;

#ifdef __WXMSW__
    this->SetIcon(wxIcon("aaaa"));
#else
    this->SetIcon(wxIcon(ArtProvider::GetAppIcon()));
#endif

    auto outer_sizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(outer_sizer);
    auto sizer = new wxBoxSizer(wxVERTICAL);

#ifdef __WXOSX__
    outer_sizer->Add(sizer, 1, wxEXPAND | wxALL, 10);
#else
    outer_sizer->Add(sizer, 1, wxEXPAND | wxALL, 5);
#endif
    auto host_label = new wxStaticText(
            this,
            wxID_ANY,
            "Host address. E.g., example.com or user1@example.com:22");
    sizer->Add(host_label, 0, wxALL, 5);

    this->host_txt_ = new wxTextCtrl(this, wxID_ANY);
    sizer->Add(this->host_txt_, 0, wxEXPAND | wxALL, 5);

    auto favorite_label = new wxStaticText(this, wxID_ANY, "Favorite hosts:");
    sizer->Add(favorite_label, 0, wxALL, 5);

    this->favorites_ = new wxListBox(
            this,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            0,
            NULL,
            wxLB_SORT | wxLB_SINGLE);
    sizer->Add(this->favorites_, 1, wxEXPAND | wxALL, 5);
    this->favorites_->Bind(wxEVT_LISTBOX, [&](wxCommandEvent &evt) {
        this->host_txt_->SetValue(evt.GetString());
    });
    this->favorites_->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent &evt) {
        this->host_txt_->SetValue(evt.GetString());
        if (this->Connect()) {
            this->connect_ = true;
            this->Close();
        }
    });

    this->config_->SetPath("/saved_connections/");
    wxString group_name;
    long i;  // NOLINT wxWidgets legacy. Not used anyway.
    bool cont = this->config_->GetFirstGroup(group_name, i);
    while (cont) {
        auto val = this->config_->Read("/saved_connections/" + group_name + "/host", "");
        if (!val.empty()) {
            this->favorites_->Append(val);
        }

        cont = this->config_->GetNextGroup(group_name, i);
    }
    this->config_->SetPath("/");

    auto bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(bottom_sizer, 0, wxEXPAND | wxALL, 0);


    auto add_btn = new wxButton(this, wxID_ANY, "Add");
    bottom_sizer->Add(add_btn, 0, wxALL, 5);
    add_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent &event) {
        string host = this->host_txt_->GetValue().ToStdString(wxMBConvUTF8());
        HostDesc host_desc;
        try {
            host_desc = HostDesc(host);
        } catch (invalid_argument &e) {
            string err = PrettifySentence(e.what());
            wxMessageDialog dialog(this, err, "Error", wxOK | wxICON_ERROR | wxCENTER);
            dialog.ShowModal();
            return;
        }

        if (host_desc.host_.empty()) {
            return;
        }

        string host_nocol = host;
        replace(host_nocol.begin(), host_nocol.end(), ':', '_');
        auto key = wxString::FromUTF8("/saved_connections/" + host_nocol);
        this->config_->Write(key + "/host", wxString::FromUTF8(host));
        this->config_->Flush();

        bool already_exists = false;
        for (int j = 0; j < this->favorites_->GetCount(); ++j) {
            if (this->favorites_->GetString(j) == this->host_txt_->GetValue()) {
                already_exists = true;
                break;
            }
        }
        if (!already_exists) {
            this->favorites_->Append(this->host_txt_->GetValue());
        }
    });


    auto remove_btn = new wxButton(this, wxID_ANY, "Remove");
    bottom_sizer->Add(remove_btn, 0, wxTOP | wxBOTTOM | wxRIGHT, 5);
    remove_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent &evt) {
        int selection = this->favorites_->GetSelection();
        if (selection < 0) {
            return;
        }

        string host = this->favorites_->GetStringSelection().ToStdString(wxMBConvUTF8());
        string host_nocol = host;
        replace(host_nocol.begin(), host_nocol.end(), ':', '_');
        this->config_->DeleteGroup(wxString::FromUTF8("/saved_connections/" + host_nocol));
        this->config_->Flush();

        auto secret_store = wxSecretStore::GetDefault();
        if (secret_store.IsOk(NULL)) {
            secret_store.Delete("filesremote/" + host_nocol);
        }

        this->favorites_->Delete(selection);
    });

    bottom_sizer->AddStretchSpacer();

    auto exit_btn = new wxButton(this, wxID_CANCEL, "Exit");
    bottom_sizer->Add(exit_btn, 0, wxALL, 5);

    this->connect_btn_ = new wxButton(this, wxID_OK, "Connect");
    this->connect_btn_->SetDefault();
    this->SetDefaultItem(this->connect_btn_);
    bottom_sizer->Add(this->connect_btn_, 0, wxTOP | wxBOTTOM | wxRIGHT, 5);
    this->connect_btn_->Bind(wxEVT_BUTTON, [&](wxCommandEvent &evt) {
        if (this->Connect()) {
            this->connect_ = true;
            evt.Skip();
        }
    });
}

bool ConnectDialog::Connect() {
    if (this->host_txt_->GetValue().empty()) {
        string s = "No host name given.";
        wxMessageDialog dialog(this, s, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        return false;
    }

    try {
        this->host_desc_ = HostDesc(this->host_txt_->GetValue().ToStdString(wxMBConvUTF8()));
    } catch (invalid_argument &e) {
        string err = PrettifySentence(e.what());
        wxMessageDialog dialog(this, err, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        return false;
    }

    return true;
}
