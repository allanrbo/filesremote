// Copyright 2020 Allan Riordan Boll

#ifndef SRC_FILEMANAGERFRAME_H_
#define SRC_FILEMANAGERFRAME_H_

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/preferences.h>
#include <wx/stdpaths.h>
#include <wx/wx.h>

#include <future>  // NOLINT
#include <map>
#include <memory>
#include <regex>  // NOLINT
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef __WXOSX__

#include <filesystem>

#endif

#include "src/channel.h"
#include "src/direntry.h"
#include "src/dirlistctrl.h"
#include "src/hostdesc.h"
#include "src/sftpthread.h"

using std::future;
using std::make_shared;
using std::map;
using std::regex;
using std::regex_search;
using std::shared_ptr;
using std::stack;
using std::string;
using std::unique_ptr;
using std::unordered_set;

#ifdef __WXOSX__
#include "src/filesystem.osx.polyfills.h"
#else
using std::filesystem::file_time_type;
#endif

struct OpenedFile {
    string local_path;
    string remote_path;
    file_time_type modified;
    bool upload_requested = false;
};


class FileManagerFrame : public wxFrame {
    HostDesc host_desc_;
    string identity_file_;
    wxSecretValue passwd_param_;
    string local_tmp_;
    wxConfigBase *config_;
    wxToolBarBase *tool_bar_;
    wxToolBarToolBase *sudo_btn_;
    DirListCtrl *dir_list_ctrl_;
    wxTextCtrl *path_text_ctrl_;
    wxTimer file_watcher_timer_;
    string home_dir_;
    string current_dir_;
    stack<string> prev_dirs_;
    stack<string> fwd_dirs_;
    vector<DirEntry> current_dir_list_;
    int sort_column_ = 0;
    bool sort_desc_ = false;
    map<string, OpenedFile> opened_files_local_;
    string stored_highlighted_ = "";
    unordered_set<string> stored_selected_;
    unique_ptr<future<void>> sftp_thread_;
    shared_ptr<Channel<threadFuncVariant>> sftp_thread_channel_ = make_shared<Channel<threadFuncVariant>>();
    shared_ptr<Channel<bool>> cancellation_channel_ = make_shared<Channel<bool>>();
    wxTimer reconnect_timer_;
    int reconnect_timer_countdown_;
    string reconnect_timer_error_ = "";
    string latest_interesting_status_ = "";
    unique_ptr<wxBusyCursor> busy_cursor_;
    bool sudo_ = false;

public:
    explicit FileManagerFrame(wxConfigBase *config);

    void Connect(HostDesc host_desc, string identity_file, wxSecretValue passwd_param, string local_tmp);

private:
    void SetupSftpThreadCallbacks();

    void OnItemActivated();

    void ChangeDir(string path);

    void SetIdleStatusText();

    void RefreshTitle();

    void UploadWatchedFile(string remote_path);

    void UploadFile(string local_path);

    void OnFileWatcherTimer(const wxTimerEvent &event);

    void RememberSelected();

    void RecallSelected();

    void RefreshDir(string remote_path, bool preserve_selection);

    void SortAndPopulateDir();

    void DownloadFileForEdit(string remote_path);

    void DownloadFile(string remote_path, string local_path);

    bool ValidateFilename(string filename);

    wxSecretValue PasswordPrompt(string msg, bool try_saved);

    wxBitmap GetBitmap(const wxArtID &id, const wxArtClient &client, const wxSize &size);
};

#endif  // SRC_FILEMANAGERFRAME_H_
