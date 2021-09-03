// Copyright 2020 Allan Riordan Boll

#ifndef SRC_CONNECTDIALOG_H_
#define SRC_CONNECTDIALOG_H_

#include <wx/config.h>
#include <wx/wx.h>

#include <string>

#include "src/hostdesc.h"

class ConnectDialog : public wxDialog {
    wxConfigBase *config_;
    wxTextCtrl *host_txt_;
    wxListBox *favorites_;
    wxButton *connect_btn_;

public:
    bool connect_ = false;
    HostDesc host_desc_;
    string identity_file_;

    explicit ConnectDialog(wxWindow *parent, wxConfigBase *config, string identity_file);

private:
    bool Connect();
};

#endif  // SRC_CONNECTDIALOG_H_
