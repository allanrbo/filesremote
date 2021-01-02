// Copyright 2020 Allan Riordan Boll

#include <dirent.h>

#include <exception>
#include <regex>  // NOLINT
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
#include <wx/process.h>
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
using std::regex;
using std::regex_match;
using std::runtime_error;
using std::string;
using std::smatch;
using std::to_string;
using std::vector;

#ifndef __WXOSX__
using std::filesystem::create_directories;
using std::filesystem::remove_all;
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

static void cleanUpOrphanedTmpDirs(string local_tmp) {
    DIR *d = opendir(local_tmp.c_str());
    if (!d) {
        throw runtime_error("failed to open temp dir");
    }
    vector<string> to_delete;
    struct dirent *dir;
    regex r("filesremote_(\\d+)");
    while ((dir = readdir(d)) != NULL) {
        smatch sm;
        string s = string(dir->d_name);
        if (regex_match(s.cbegin(), s.cend(), sm, r)) {
            int pid = stoi(string(sm[1].str()));
            if (!wxProcess::Exists(pid)) {
                to_delete.push_back(normalize_path(local_tmp + "/" + dir->d_name));
            }
        }
    }
    closedir(d);
    for (int i = 0; i < to_delete.size(); ++i) {
        try {
            remove_all(localPathUnicode(to_delete[i]));
        } catch (...) {
            // Let it be a best effort. We may not have perm. Text editors, etc., could be locking these dirs.
        }
    }
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
            cleanUpOrphanedTmpDirs(local_tmp);
            local_tmp = normalize_path(local_tmp + "/filesremote_" + to_string(wxGetProcessId()));
            create_directories(localPathUnicode(local_tmp));

            auto es = wxEmptyString;
            wxFileConfig *config = new wxFileConfig("filesremote", es, es, es, wxCONFIG_USE_LOCAL_FILE);
            config->EnableAutoSave();
            config->SetRecordDefaults();
            wxConfigBase::Set(config);

            // Note: wxWdigets takes care of deleting this at shutdown.
            auto frame = new FileManagerFrame(config);
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

            frame->Connect(this->host_desc_, local_tmp);
        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    int OnExit() {
        // Clean up our tmp directory.
        auto local_tmp = string(wxStandardPaths::Get().GetTempDir());
        local_tmp = normalize_path(local_tmp + "/filesremote_" + to_string(wxGetProcessId()));
        try {
            remove_all(localPathUnicode(local_tmp));
        } catch (...) {
            // Let it be a best effort. Text editors, etc., could be locking these dirs.
        }

        return 0;
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
