// Copyright 2020 Allan Riordan Boll

#include "src/passworddialog.h"

#include <wx/secretstore.h>
#include <wx/wx.h>

#include <string>

using std::string;

// Layout inspired by wxTextEntryDialog, but with the addition of a "remember" checkbox.
PasswordDialog::PasswordDialog(wxWindow *parent, string msg, bool allow_save) : wxDialog(
        parent, wxID_ANY, "Enter password") {
    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    top_sizer->Add(CreateTextSizer(msg), wxSizerFlags().DoubleBorder());

    this->password_txt_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         wxSize(300, wxDefaultCoord), wxTE_PASSWORD);
    top_sizer->Add(this->password_txt_, wxSizerFlags(0).Expand().TripleBorder(wxLEFT | wxRIGHT));

    if (allow_save) {
#ifdef __WXOSX__
        string s = "Remember this password in my keychain";
#else
        string s = "Remember this password";
#endif
        this->save_passwd_chk_ = new wxCheckBox(this, wxID_ANY, s);
        top_sizer->Add(this->save_passwd_chk_, wxSizerFlags().DoubleBorder(wxTOP | wxLEFT | wxRIGHT));
    }

    this->show_passwd_chk_ = new wxCheckBox(this, wxID_ANY, "Show password");
    top_sizer->Add(this->show_passwd_chk_, wxSizerFlags().DoubleBorder(wxTOP | wxLEFT | wxRIGHT));
    this->show_passwd_chk_->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent &) {
        if (this->show_passwd_chk_->IsChecked()) {
            this->password_txt_->SetWindowStyleFlag(0 );
        } else {
            this->password_txt_->SetWindowStyleFlag(wxTE_PASSWORD);
        }
    });

    auto button_sizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    top_sizer->Add(button_sizer, wxSizerFlags().DoubleBorder().Expand());

    this->SetAutoLayout(true);
    this->SetSizer(top_sizer);

    top_sizer->SetSizeHints(this);
    top_sizer->Fit(this);

    this->Center(wxBOTH);

    this->password_txt_->SetFocus();
}

bool PasswordDialog::TransferDataFromWindow() {
    this->value_ = wxSecretValue(this->password_txt_->GetValue());
    this->save_passwd_ = this->save_passwd_chk_ && this->save_passwd_chk_->IsChecked();
    return true;
}
