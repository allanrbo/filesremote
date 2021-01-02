// Copyright 2020 Allan Riordan Boll

#include <exception>
#include <sstream>
#include <string>

#ifndef __WXOSX__

#include <filesystem>

#endif

#include <stdio.h>

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/artprov.h>
#include <wx/cmdline.h>
#include <wx/fileconf.h>
#include <wx/preferences.h>
#include <wx/secretstore.h>
#include <wx/snglinst.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/wx.h>

#include "src/artprovider.h"
#include "src/connectdialog.h"
#include "src/filemanagerframe.h"
#include "src/hostdesc.h"
#include "src/paths.h"
#include "src/string.h"

using std::exception;
using std::invalid_argument;
using std::string;
using std::vector;

#ifndef __WXOSX__
using std::filesystem::create_directories;
#else
#include "src/filesystem.osx.polyfills.h"
#endif

static void showException() {
    wxString error;
    try {
        throw;  // Rethrow the current exception in order to pattern match it here.
    } catch (const exception &e) {
        error = e.what();
    } catch (...) {
        error = "unknown error";
    }

    wxLogError("%s", error);
}

class FilesRemoteApp : public wxApp {
    HostDesc host_desc_;

public:
    bool OnInit() {
        try {
            if (!wxApp::OnInit())
                return false;

            wxInitAllImageHandlers();
#ifdef __WXOSX__
            // The built-in art providers on wxMac don't have enough scaled versions and are therefore ugly...
            ArtProvider::CleanUpProviders();
#endif
            wxArtProvider::PushBack(new ArtProvider);

            // Create our tmp directory.
            auto local_tmp = string(wxStandardPaths::Get().GetTempDir());
            local_tmp = normalize_path(local_tmp + "/filesremote_" + wxGetUserId().ToStdString(wxMBConvUTF8()));
            create_directories(localPathUnicode(local_tmp));

            auto es = wxEmptyString;
            wxFileConfig *config = new wxFileConfig("filesremote", es, es, es, wxCONFIG_USE_LOCAL_FILE);
            config->EnableAutoSave();
            config->SetRecordDefaults();
            wxConfigBase::Set(config);


            // Note: wxWdigets takes care of deleting this at shutdown.
            auto frame = new FileManagerFrame(config, local_tmp);
            frame->Show();

            if (this->host_desc_.host_.empty()) {
                auto connect_dialog = new ConnectDialog(frame, config);

                connect_dialog->ShowModal();
                connect_dialog->Destroy();
                if (!connect_dialog->connect_) {
                    return true;  // Exit button or ESC was pressed. Not an error.
                }
                this->host_desc_ = connect_dialog->host_desc_;
            }

            frame->Connect(this->host_desc_);
        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    virtual void OnInitCmdLine(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[username@]host[:port]", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
    }

    virtual bool OnCmdLineParsed(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        if (parser.GetParamCount() > 0) {
            try {
                this->host_desc_ = HostDesc(string(parser.GetParam(0)));
            } catch (invalid_argument &e) {
                wxLogFatalError(wxString::FromUTF8(e.what()));
                return false;
            }
        }

        return true;
    }

    virtual bool OnExceptionInMainLoop() {
        showException();
        return false;
    }
};

IMPLEMENT_APP(FilesRemoteApp)
