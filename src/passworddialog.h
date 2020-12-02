// Copyright 2020 Allan Riordan Boll

#ifndef SRC_PASSWORDDIALOG_H_
#define SRC_PASSWORDDIALOG_H_

#include <wx/secretstore.h>
#include <wx/wx.h>

#include <string>

using std::string;

class PasswordDialog : public wxDialog {
    wxTextCtrl *password_txt_;
    wxCheckBox *save_passwd_chk_;

public:
    wxSecretValue value_;
    bool save_passwd_;

    explicit PasswordDialog(wxWindow *parent, string msg, bool allow_save);

    virtual bool TransferDataFromWindow();
};


#endif  // SRC_PASSWORDDIALOG_H_
