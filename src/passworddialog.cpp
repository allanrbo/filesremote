// Copyright 2020 Allan Riordan Boll

#include "src/passworddialog.h"

#include <wx/secretstore.h>
#include <wx/wx.h>

#include <string>

using std::string;

// Layout inspired by wxTextEntryDialog, but with the addition of a "remember" checkbox.
PasswordDialog::PasswordDialog(wxWindow *parent, string msg, bool allow_save) : wxDialog(
        parent, wxID_ANY, "Enter password") {
    this->top_sizer_ = new wxBoxSizer(wxVERTICAL);

    this->top_sizer_->Add(CreateTextSizer(msg), wxSizerFlags().DoubleBorder());

    this->password_txt_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         wxSize(300, wxDefaultCoord), wxTE_PASSWORD);
    this->top_sizer_->Add(this->password_txt_, wxSizerFlags(0).Expand().TripleBorder(wxLEFT | wxRIGHT));

    if (allow_save) {
#ifdef __WXOSX__
        string s = "Remember this password in my keychain";
#else
        string s = "Remember this password";
#endif
        this->save_passwd_chk_ = new wxCheckBox(this, wxID_ANY, s);
        this->top_sizer_->Add(this->save_passwd_chk_, wxSizerFlags().DoubleBorder(wxTOP | wxLEFT | wxRIGHT));
    }

    this->show_passwd_chk_ = new wxCheckBox(this, wxID_ANY, "Show password");
    this->top_sizer_->Add(this->show_passwd_chk_, wxSizerFlags().DoubleBorder(wxTOP | wxLEFT | wxRIGHT));
    this->show_passwd_chk_->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent &) {
        long style = 0;  // NOLINT Legacy wxWidgets type.
        if (!this->show_passwd_chk_->IsChecked()) {
            style = wxTE_PASSWORD;
        }


        auto old = this->password_txt_;
        this->password_txt_ = new wxTextCtrl(this, wxID_ANY, old->GetValue(), wxDefaultPosition,
                                             wxSize(300, wxDefaultCoord), style);
        this->top_sizer_->Replace(old, this->password_txt_);
        old->Destroy();
        this->top_sizer_->Layout();
    });

    auto button_sizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    this->top_sizer_->Add(button_sizer, wxSizerFlags().DoubleBorder().Expand());

    this->SetAutoLayout(true);
    this->SetSizer(this->top_sizer_);

    this->top_sizer_->SetSizeHints(this);
    this->top_sizer_->Fit(this);

    this->Center(wxBOTH);

    this->password_txt_->SetFocus();
}

bool PasswordDialog::TransferDataFromWindow() {
    this->value_ = wxSecretValue(this->password_txt_->GetValue());
    this->save_passwd_ = this->save_passwd_chk_ && this->save_passwd_chk_->IsChecked();
    return true;
}
