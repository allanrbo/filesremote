// Copyright 2020 Allan Riordan Boll

#include "src/filemanagerframe.h"

#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/preferences.h>
#include <wx/stdpaths.h>
#include <wx/wx.h>

#include <algorithm>
#include <chrono>  // NOLINT
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

#include "./version.h"
#include "src/artprovider.h"
#include "src/channel.h"
#include "src/direntry.h"
#include "src/dirlistctrl.h"
#include "src/hostdesc.h"
#include "src/ids.h"
#include "src/licensestrings.h"
#include "src/passworddialog.h"
#include "src/paths.h"
#include "src/preferencespanel.h"
#include "src/sftpthread.h"
#include "src/string.h"

using std::chrono::seconds;
using std::future;
using std::launch;
using std::make_shared;
using std::make_unique;
using std::map;
using std::regex;
using std::regex_search;
using std::shared_ptr;
using std::stack;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::unordered_set;

#ifdef __WXOSX__
#include "src/filesystem.osx.polyfills.h"
#else
using std::filesystem::create_directories;
using std::filesystem::exists;
using std::filesystem::file_time_type;
using std::filesystem::last_write_time;
using std::filesystem::remove;
using std::filesystem::remove_all;
#endif

// Drag and drop for uploading.
class DnDFile : public wxFileDropTarget {
    function<bool(const wxArrayString &filenames)> on_drop_files_cb_;

public:
    explicit DnDFile(function<bool(const wxArrayString &filenames)> cb) {
        on_drop_files_cb_ = cb;
    }

    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames) wxOVERRIDE {
        return this->on_drop_files_cb_(filenames);
    }
};

FileManagerFrame::FileManagerFrame(HostDesc host_desc, wxConfigBase *config, string local_tmp) : wxFrame(
        NULL,
        wxID_ANY,
        wxEmptyString,
        wxPoint(-1, -1),
        wxSize(800, 600)
) {
    this->host_desc_ = host_desc;
    this->config_ = config;

    // Create sub tmp directory for this connection.
    local_tmp = normalize_path(local_tmp + "/" + this->host_desc_.ToStringNoCol());
    this->local_tmp_ = local_tmp;
    for (int i = 2; exists(localPathUnicode(this->local_tmp_)); i++) {
        // If another instance is already open and using this path, then choose a different path.
        this->local_tmp_ = local_tmp + "_" + to_string(i);
    }
    create_directories(localPathUnicode(this->local_tmp_));

#ifdef __WXMSW__
    this->SetIcon(wxIcon("aaaa"));
#else
    this->SetIcon(wxIcon(ArtProvider::GetAppIcon()));
#endif
    this->RefreshTitle();
    this->CreateStatusBar();

    // Create menus.
    auto *menuBar = new wxMenuBar();
    this->SetMenuBar(menuBar);

    auto *file_menu = new wxMenu();
    menuBar->Append(file_menu, "&File");

    file_menu->Append(wxID_OPEN, "&Open in editor",
                      "Open selected file in local editor and re-upload when modified");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->OnItemActivated();
    }, wxID_OPEN);

    file_menu->Append(ID_DOWNLOAD, "&Download\tCtrl+S", "Download selected file");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        int item = this->dir_list_ctrl_->GetHighlighted();
        auto entry = this->current_dir_list_[item];
        if (entry.is_dir_) {
            return;
        }

        auto local_dir = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads);
        local_dir = this->config_->Read("/last_dir", local_dir);

        wxFileDialog dialog(this,
                            "Download file",
                            local_dir,
                            wxString::FromUTF8(entry.name_),
                            wxFileSelectorDefaultWildcardStr,
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        local_dir = normalize_path(dialog.GetPath().ToStdString(wxMBConvUTF8()) + "/..");
        this->config_->Write("/last_dir", wxString::FromUTF8(local_dir));

        auto remote_path = normalize_path(this->current_dir_ + "/" + entry.name_);
        this->DownloadFile(remote_path, dialog.GetPath().ToStdString(wxMBConvUTF8()));
    }, ID_DOWNLOAD);

    file_menu->Append(ID_UPLOAD, "&Upload\tCtrl+U", "Upload a file to current directory");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        auto local_dir = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Desktop);
        local_dir = this->config_->Read("/last_dir", local_dir);

        wxFileDialog dialog(
                this,
                "Upload file",
                local_dir,
                wxEmptyString,
                wxFileSelectorDefaultWildcardStr,
                wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_SHOW_HIDDEN);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        string local_path = dialog.GetPath().ToStdString(wxMBConvUTF8());

        local_dir = normalize_path(local_path + "/..");
        this->config_->Write("/last_dir", wxString::FromUTF8(local_dir));

        this->UploadFile(local_path);
    }, ID_UPLOAD);

    file_menu->Append(ID_CANCEL, "&Cancel current transfer\tESC", "Cancel the current upload or download");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->cancellation_channel_->Put(true);
    }, ID_CANCEL);

    file_menu->Append(ID_RENAME, "&Rename\tF2", "Rename currently selected file or directory");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        int item = this->dir_list_ctrl_->GetHighlighted();
        auto entry = this->current_dir_list_[item];

        wxTextEntryDialog dialog(
                this,
                "Rename " + wxString::FromUTF8(entry.name_),
                "Enter new name:",
                wxString::FromUTF8(entry.name_),
                wxOK | wxCANCEL);

        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        string new_name = dialog.GetValue().ToStdString(wxMBConvUTF8());
        if (!this->ValidateFilename(new_name)) {
            return;
        }

        auto remote_old_path = normalize_path(this->current_dir_ + "/" + entry.name_);
        auto remote_new_path = normalize_path(this->current_dir_ + "/" + new_name);
        this->sftp_thread_channel_->Put(SftpThreadCmdRename{remote_old_path, remote_new_path});
        this->SetStatusText(wxString::FromUTF8("Renaming " + entry.name_ + " to " + new_name + " ..."));
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, ID_RENAME);

#ifdef __WXOSX__
    file_menu->Append(wxID_DELETE, "&Delete\tCtrl+Backspace", "Delete currently selected file or directory");
#else
    file_menu->Append(wxID_DELETE, "&Delete\tDelete", "Delete currently selected file or directory");
#endif
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        int item = this->dir_list_ctrl_->GetHighlighted();
        auto entry = this->current_dir_list_[item];

        auto s = wxString::FromUTF8("Permanently delete " + entry.name_ + "?");
        wxMessageDialog dialog(this, s, "Confirm deletion", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Delete", "Cancel");
        if (dialog.ShowModal() != wxID_YES) {
            return;
        }

        auto remote_path = normalize_path(this->current_dir_ + "/" + entry.name_);
        this->sftp_thread_channel_->Put(SftpThreadCmdDelete{remote_path});
        this->SetStatusText(wxString::FromUTF8("Deleting " + entry.name_ + " ..."));
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, wxID_DELETE);

    file_menu->Append(ID_MKDIR, "&New directory\tCtrl+Shift+N", "Create new sub-directory here.");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        wxTextEntryDialog dialog(
                this,
                "Create new directory",
                "Enter new directory name:",
                wxEmptyString,
                wxOK | wxCANCEL);

        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        string new_name = dialog.GetValue().ToStdString(wxMBConvUTF8());
        if (!this->ValidateFilename(new_name)) {
            return;
        }

        auto remote_new_path = normalize_path(this->current_dir_ + "/" + new_name);
        this->sftp_thread_channel_->Put(SftpThreadCmdMkdir{remote_new_path});
        this->SetStatusText(wxString::FromUTF8("Creating directory " + new_name + " ..."));
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, ID_MKDIR);

    file_menu->Append(wxID_NEW, "&New empty file\tCtrl+N", "Create new empty file here.");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }

        wxTextEntryDialog dialog(
                this,
                "Create new empty file",
                "Enter new file name:",
                wxEmptyString,
                wxOK | wxCANCEL);

        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        string new_name = dialog.GetValue().ToStdString(wxMBConvUTF8());
        if (!this->ValidateFilename(new_name)) {
            return;
        }

        auto remote_new_path = normalize_path(this->current_dir_ + "/" + new_name);
        this->sftp_thread_channel_->Put(SftpThreadCmdMkfile{remote_new_path});
        this->SetStatusText(wxString::FromUTF8("Creating file " + new_name + " ..."));
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, wxID_NEW);

    file_menu->Append(ID_SUDO, "&Sudo\tCtrl+E", "Elevate to root via sudo");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_) {
            return;
        }
        this->busy_cursor_ = make_unique<wxBusyCursor>();

        if (this->sudo_) {
            this->sftp_thread_channel_->Put(SftpThreadCmdSudoExit{});
            return;
        }

        this->sftp_thread_channel_->Put(SftpThreadCmdSudo{});
        this->SetStatusText(wxString::FromUTF8("Elevating to root via sudo ..."));
    }, ID_SUDO);

    file_menu->AppendSeparator();

    file_menu->Append(wxID_PREFERENCES);
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        auto prefs_editor = new wxPreferencesEditor();
        prefs_editor->AddPage(new PreferencesPageGeneral(this->config_));
        prefs_editor->Show(this);
    }, wxID_PREFERENCES);

    file_menu->Append(wxID_EXIT, "E&xit", "Quit this program");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->Close(true);
    }, wxID_EXIT);


    auto *go_menu = new wxMenu();
    menuBar->Append(go_menu, "&Go");

    // Adding refresh to the menu twice with two different hotkeys, instead of using SetAcceleratorTable.
    // It's wonky, but MacOS has trouble with non-menu accelerators when the wxDataViewListCtrl has focus.
    go_menu->Append(wxID_REFRESH, "Refresh\tF5");
    go_menu->Append(wxID_REFRESH, "Refresh\tCtrl+R");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->latest_interesting_status_ = "";
        this->RefreshDir(this->current_dir_, true);
    }, wxID_REFRESH);

    go_menu->Append(ID_SET_DIR, "Change directory\tCtrl+L");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &) {
        this->path_text_ctrl_->SetFocus();
        this->path_text_ctrl_->SelectAll();
    }, ID_SET_DIR);

#ifdef __WXOSX__
    go_menu->Append(ID_PARENT_DIR, "Parent directory\tCtrl+Up", wxEmptyString, wxITEM_NORMAL);
#else
    go_menu->Append(ID_PARENT_DIR, "Parent directory\tAlt+Up", wxEmptyString, wxITEM_NORMAL);
#endif
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->ChangeDir(normalize_path(this->current_dir_ + "/.."));
    }, ID_PARENT_DIR);

#ifdef __WXOSX__
    go_menu->Append(wxID_BACKWARD, "Back\tCtrl+[", wxEmptyString, wxITEM_NORMAL);
#else
    go_menu->Append(wxID_BACKWARD, "Back\tAlt+Left", wxEmptyString, wxITEM_NORMAL);
#endif
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        if (this->busy_cursor_ || this->prev_dirs_.empty()) {
            return;
        }

        string dir = this->prev_dirs_.top();
        this->prev_dirs_.pop();
        this->fwd_dirs_.push(this->current_dir_);
        this->RefreshDir(dir, false);
        this->tool_bar_->EnableTool(wxID_BACKWARD, this->prev_dirs_.size() > 0);
        this->tool_bar_->EnableTool(wxID_FORWARD, this->fwd_dirs_.size() > 0);
    }, wxID_BACKWARD);

#ifdef __WXOSX__
    go_menu->Append(wxID_FORWARD, "Forward\tCtrl+]", wxEmptyString, wxITEM_NORMAL);
#else
    go_menu->Append(wxID_FORWARD, "Forward\tAlt+Right", wxEmptyString, wxITEM_NORMAL);
#endif
    this->Bind(wxEVT_TOOL, [&](wxCommandEvent &event) {
        if (this->busy_cursor_ || this->fwd_dirs_.empty()) {
            return;
        }

        string dir = this->fwd_dirs_.top();
        this->fwd_dirs_.pop();
        this->prev_dirs_.push(this->current_dir_);
        this->RefreshDir(dir, false);
        this->tool_bar_->EnableTool(wxID_BACKWARD, this->prev_dirs_.size() > 0);
        this->tool_bar_->EnableTool(wxID_FORWARD, this->fwd_dirs_.size() > 0);
    }, wxID_FORWARD);

#ifdef __WXOSX__
    go_menu->Append(ID_OPEN_SELECTED, "Open selected item\tCtrl+Down", wxEmptyString, wxITEM_NORMAL);
#endif
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        this->dir_list_ctrl_->ActivateCurrent();
    }, ID_OPEN_SELECTED);

    auto *help_menu = new wxMenu;
    menuBar->Append(help_menu, "&Help");

    help_menu->Append(ID_SHOW_LICENSES, "Licenses");
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        wxDialog *licenses_frame = new wxDialog(this,
                                                wxID_ANY,
                                                "Licenses",
                                                wxDefaultPosition,
                                                wxSize(600, 600),
                                                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
        wxTextCtrl *licenses_text_ctrl = new wxTextCtrl(
                licenses_frame,
                wxID_ANY,
                wxString::FromUTF8(licenses),
                wxDefaultPosition,
                wxDefaultSize,
                wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        sizer->Add(licenses_text_ctrl, 1, wxEXPAND | wxALL);
        licenses_frame->SetSizer(sizer);
        licenses_frame->Show();
    }, ID_SHOW_LICENSES);

    help_menu->Append(wxID_ABOUT);
    this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
        wxAboutDialogInfo info;
        info.SetName("FilesRemote");
        auto icon = this->GetIcon();
        if (!icon.IsOk()) {
            icon = wxIcon(ArtProvider::GetAppIcon());
        }
        info.SetIcon(icon);
        info.SetVersion(PROJECT_VERSION);
        info.SetDescription("An SSH file manager");
        info.SetCopyright("(C) 2020 Allan Riordan Boll");
        wxAboutBox(info, this);
    }, wxID_ABOUT);

    // Most keyboard accelerators for menu items are automatically bound via the string in its title. However, some
    // seem to only work via SetAcceleratorTable, so setting them again here.
    // MacOS seems to ignores this table when the focus is on wxDataViewListCtrl, so we rely on the accelerators in
    // the menu item titles on MacOS.
#ifndef __WXOSX__
    vector<wxAcceleratorEntry> entries{
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, wxID_REFRESH),
            wxAcceleratorEntry(wxACCEL_CTRL, 'R', wxID_REFRESH),
            wxAcceleratorEntry(wxACCEL_CTRL, 'L', ID_SET_DIR),
            wxAcceleratorEntry(wxACCEL_ALT, WXK_UP, ID_PARENT_DIR),
            wxAcceleratorEntry(wxACCEL_ALT, WXK_LEFT, wxID_BACKWARD),
            wxAcceleratorEntry(wxACCEL_ALT, WXK_RIGHT, wxID_FORWARD),
            wxAcceleratorEntry(wxACCEL_CTRL, 'U', ID_UPLOAD),
            wxAcceleratorEntry(wxACCEL_CTRL, 'S', ID_DOWNLOAD),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_ESCAPE, ID_CANCEL),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F2, ID_RENAME),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_DELETE, wxID_DELETE),
            wxAcceleratorEntry(wxACCEL_CTRL | wxACCEL_SHIFT, 'N', ID_MKDIR),
    };
    wxAcceleratorTable accel(entries.size(), &entries[0]);
    this->SetAcceleratorTable(accel);
#endif

    // Set up a timer that will watch for changes in local files.
    this->file_watcher_timer_.Bind(wxEVT_TIMER, &FileManagerFrame::OnFileWatcherTimer, this);
    this->file_watcher_timer_.Start(1000);

    // Main layout.
    auto *panel = new wxPanel(this);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *sizer_inner_top = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(sizer_inner_top, 0, wxEXPAND | wxALL, 1);

    // Create toolbar
    this->tool_bar_ = this->CreateToolBar(wxTB_DEFAULT_STYLE);
    auto tool_bar_bmp_size = wxArtProvider::GetNativeSizeHint(wxART_TOOLBAR) * (1 / this->GetContentScaleFactor());
    this->tool_bar_->SetToolBitmapSize(tool_bar_bmp_size);
    auto size = this->tool_bar_->GetToolBitmapSize();
    size = size * this->GetContentScaleFactor();
    this->tool_bar_->AddTool(
            ID_PARENT_DIR,
            "Parent directory",
            this->GetBitmap("_parent_dir", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Parent directory",
            "Go to parent directory");
    this->tool_bar_->AddTool(
            wxID_BACKWARD,
            "Back",
            this->GetBitmap("_nav_back", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Back",
            "Go back a directory");
    this->tool_bar_->EnableTool(wxID_BACKWARD, false);
    this->tool_bar_->AddTool(
            wxID_FORWARD,
            "Forward",
            this->GetBitmap("_nav_fwd", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Forward",
            "Go forward a directory");
    this->tool_bar_->EnableTool(wxID_FORWARD, false);
    this->tool_bar_->AddTool(
            wxID_REFRESH,
            "Refresh",
            this->GetBitmap("_refresh", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Refresh",
            "Refresh the current directory list");
    this->tool_bar_->AddTool(
            wxID_OPEN,
            "Open",
            this->GetBitmap("_open_file", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Open",
            "Open the selected file");
    this->tool_bar_->AddTool(
            ID_DOWNLOAD,
            "Download",
            this->GetBitmap("_download", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Download",
            "Download the selected file to local system");
    this->tool_bar_->AddTool(
            ID_UPLOAD,
            "Upload",
            this->GetBitmap("_upload", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Upload",
            "Upload a local file to the current directory");
    this->tool_bar_->AddTool(
            wxID_NEW,
            "New file",
            this->GetBitmap("_new_file", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "New file",
            "Create an empty file");
    this->tool_bar_->AddTool(
            ID_MKDIR,
            "New directory",
            this->GetBitmap("_new_dir", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "New directory",
            "Create a new directory");
    this->tool_bar_->AddTool(
            ID_RENAME,
            "Rename",
            this->GetBitmap("_rename", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Rename",
            "Rename currently selected file or directory");
    this->tool_bar_->AddTool(
            wxID_DELETE,
            "Delete",
            this->GetBitmap("_delete", wxART_TOOLBAR, size),
            wxNullBitmap,
            wxITEM_NORMAL,
            "Delete",
            "Delete currently selected file or directory");
    this->sudo_btn_ = this->tool_bar_->AddCheckTool(
            ID_SUDO,
            "Sudo",
            this->GetBitmap("_sudo", wxART_TOOLBAR, size),
            wxNullBitmap,
            "Sudo",
            "Elevate to root via sudo");
    this->tool_bar_->Realize();

    // Create remote path text field.
    this->path_text_ctrl_ = new wxTextCtrl(
            panel,
            wxID_ANY,
            wxString::FromUTF8(this->current_dir_),
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_PROCESS_ENTER);
    sizer_inner_top->Add(this->path_text_ctrl_, 1, wxEXPAND | wxALL, 4);

    // Handle when pressing enter while focused on the address bar text field.
    this->path_text_ctrl_->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent &event) {
        this->sftp_thread_channel_->Put(SftpThreadCmdGoTo{
                this->path_text_ctrl_->GetValue().ToStdString(wxMBConvUTF8())});
    });

    // Handle when pressing ESC while focused on the address bar text field.
    this->path_text_ctrl_->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent &evt) {
        if (evt.GetModifiers() == 0 && evt.GetKeyCode() == WXK_ESCAPE && this->path_text_ctrl_->HasFocus()) {
            this->path_text_ctrl_->SetValue(wxString::FromUTF8(this->current_dir_));
            this->path_text_ctrl_->SelectNone();
            this->dir_list_ctrl_->SetFocus();
            return;
        }

        evt.Skip();
    });

    auto icon_size = this->FromDIP(wxSize(16, 16));
    auto icons_image_list = new wxImageList(icon_size.GetWidth(), icon_size.GetHeight(), false, 1);
    icons_image_list->Add(this->GetBitmap(wxART_NORMAL_FILE, wxART_LIST, icon_size));
    icons_image_list->Add(this->GetBitmap(wxART_FOLDER, wxART_LIST, icon_size));
    icons_image_list->Add(this->GetBitmap(wxART_EXECUTABLE_FILE, wxART_LIST, icon_size));
    icons_image_list->Add(this->GetBitmap("_symlink", wxART_LIST, icon_size));
    icons_image_list->Add(this->GetBitmap("_file_picture", wxART_LIST, icon_size));
    icons_image_list->Add(this->GetBitmap("_package", wxART_LIST, icon_size));

#ifdef __WXOSX__
    // On MacOS wxDataViewListCtrl looks best.
    this->dir_list_ctrl_ = new DvlcDirList(panel, this->config_, icons_image_list);
#else
    // On GTK and Windows wxListCtrl looks best.
    this->dir_list_ctrl_ = new LcDirList(panel, this->config_, icons_image_list);
#endif
    sizer->Add(this->dir_list_ctrl_->GetCtrl(), 1, wxEXPAND | wxALL, 0);
    this->dir_list_ctrl_->SetFocus();

    this->dir_list_ctrl_->BindOnItemActivated([&](void) {
        this->OnItemActivated();
    });

    this->dir_list_ctrl_->BindOnColumnHeaderClickCb([&](int col) {
        if (this->sort_column_ == col) {
            this->sort_desc_ = !this->sort_desc_;
        } else {
            this->sort_desc_ = false;
            this->sort_column_ = col;
        }

        this->RememberSelected();
        this->SortAndPopulateDir();
        this->RecallSelected();
        this->dir_list_ctrl_->SetFocus();
    });

    panel->SetSizerAndFit(sizer);

    // Restore window size and pos. This needs to happen after all controls are added.
    int x = this->config_->Read("/window_x", -1);
    int y = this->config_->Read("/window_y", -1);
    int w = this->config_->Read("/window_w", 800);
    int h = this->config_->Read("/window_h", 600);
    this->Move(x, y);
    this->SetClientSize(w, h);

    this->Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent &event) {
        this->SetStatusText("Disconnecting...");
        this->busy_cursor_ = make_unique<wxBusyCursor>();

        if (this->sftp_thread_channel_) {
            this->sftp_thread_channel_->Put(SftpThreadCmdShutdown{});

            // Unless we never even connected, wait up to 2 seconds.
            if (!this->home_dir_.empty()) {
                this->sftp_thread_->wait_for(seconds(2));
                this->sftp_thread_.release();
            }
        }

        // Clean up all files and directories we put there.
        for (auto o : this->opened_files_local_) {
            remove(localPathUnicode(o.second.local_path));
        }
        try {
            remove_all(localPathUnicode(this->local_tmp_));
        } catch (...) {
            // Let it be a best effort. Text editors, etc., could be locking these dirs.
        }

        // Save frame position.
        int x, y, w, h;
        this->GetClientSize(&w, &h);
        this->GetPosition(&x, &y);
        this->config_->Write("/window_x", x);
        this->config_->Write("/window_y", y);
        this->config_->Write("/window_w", w);
        this->config_->Write("/window_h", h);
        this->config_->Flush();

        event.Skip();  // Default event handler calls Destroy().
    });

    // Timer used to count down till reconnecting when reconnect was requested.
    this->reconnect_timer_.Bind(wxEVT_TIMER, [&](wxTimerEvent &event) {
        if (this->reconnect_timer_countdown_ > 0) {
            auto s = to_string(this->reconnect_timer_countdown_);
            this->SetStatusText(
                    wxString::FromUTF8(this->reconnect_timer_error_ + " Reconnecting in " + s + " seconds..."));
            this->reconnect_timer_countdown_ -= 1;
            return;
        }
        this->reconnect_timer_.Stop();
        this->sftp_thread_channel_->Put(SftpThreadCmdConnect{this->host_desc_});
        this->SetStatusText(wxString::FromUTF8(this->reconnect_timer_error_ + " Reconnecting..."));
    });

    // Drag and drop for uploading.
    this->SetDropTarget(new DnDFile([&](const wxArrayString &filenames) {
        if (filenames.size() > 1) {
            wxMessageDialog dialog(
                    this,
                    "Only one upload at a time is currently supported.",
                    "Error",
                    wxOK | wxICON_ERROR | wxCENTER);
            dialog.ShowModal();
            return false;
        }

        string path = filenames[0].ToStdString(wxMBConvUTF8());

        struct stat attr;
        stat(path.c_str(), &attr);
        if (LIBSSH2_SFTP_S_ISDIR(attr.st_mode)) {
            wxMessageDialog dialog(
                    this,
                    "Directory upload is currently not supported.",
                    "Error",
                    wxOK | wxICON_ERROR | wxCENTER);
            dialog.ShowModal();
            return false;
        }

        this->UploadFile(path);
        return true;
    }));

    // Start the sftp thread. We will be communicating with it only through message passing.
    this->SetupSftpThreadCallbacks();
    this->sftp_thread_ = make_unique<future<void>>(
            async(
                    launch::async,
                    sftpThreadFunc,
                    this,
                    this->sftp_thread_channel_,
                    this->cancellation_channel_));
    this->sftp_thread_channel_->Put(SftpThreadCmdConnect{this->host_desc_});
    this->busy_cursor_ = make_unique<wxBusyCursor>();
    this->SetStatusText("Connecting...");
}

void FileManagerFrame::SetupSftpThreadCallbacks() {
    // Sftp thread will trigger this callback after successfully connecting.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseConnected>();
        this->home_dir_ = r.home_dir;
        if (this->current_dir_ == "") {
            this->current_dir_ = r.home_dir;
        }

        // In case this was a reconnect after a dropped connection, reset all upload_requested-flags.
        for (auto o : this->opened_files_local_) {
            this->opened_files_local_[o.first].upload_requested = false;
        }


        this->SetStatusText("Connected. Getting directory list...");
        this->RefreshDir(this->current_dir_, false);
        this->sudo_ = false;
        this->tool_bar_->ToggleTool(this->sudo_btn_->GetId(), this->sudo_);
        this->RefreshTitle();
    }, ID_SFTP_THREAD_RESPONSE_CONNECTED);

    // Sftp thread will trigger this callback when it requires approval of the server fingerprint while connecting.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        auto r = event.GetPayload<SftpThreadResponseNeedFingerprintApproval>();
        this->busy_cursor_ = nullptr;
        this->RequestUserAttention(wxUSER_ATTENTION_INFO);

        int resp = 0;
        string key = "/known_host_fingerprints/" + this->host_desc_.ToStringNoUserNoCol();
        string prev_fingerprint = this->config_->Read(key, "").ToStdString(wxMBConvUTF8());
        if (prev_fingerprint.empty()) {
            string s = "First time connecting to " + this->host_desc_.ToStringNoUser() + ".\n";
            s += "Fingerprint: " + r.fingerprint;
            wxMessageDialog dialog(this, s, "Server fingerprint",
                                   wxOK | wxOK_DEFAULT | wxCANCEL | wxICON_QUESTION | wxCENTER);
            resp = dialog.ShowModal();
        } else if (prev_fingerprint != r.fingerprint) {
            string s = "REMOTE HOST IDENTIFICATION HAS CHANGED!\n";
            s += "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n";
            s += "It is also possible that a host key has just been changed.\n";
            s += "Host: " + this->host_desc_.ToStringNoUser() + "\n";
            s += "Old fingerprint: " + prev_fingerprint + ".\n";
            s += "New Fingerprint: " + r.fingerprint + "\n";
            s += "Fingerprint will be replaced with new one in local database if you continue.";
            wxMessageDialog dialog(this, s, "Server fingerprint",
                                   wxOK | wxCANCEL | wxCANCEL_DEFAULT | wxICON_WARNING | wxCENTER);
            dialog.SetOKLabel("Accept risk and continue");
            resp = dialog.ShowModal();
        } else {
            resp = wxID_OK;
        }

        if (resp == wxID_OK) {
            if (prev_fingerprint != r.fingerprint) {
                this->config_->Write(key, wxString::FromUTF8(r.fingerprint));
                this->config_->Flush();
            }
            this->sftp_thread_channel_->Put(SftpThreadCmdFingerprintApproved{});
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            this->Close();
        }
    }, ID_SFTP_THREAD_RESPONSE_NEED_FINGERPRINT_APPROVAL);

    // Sftp thread will trigger this callback if it requires a password for the connection.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto passwd = this->PasswordPrompt("Enter password for " + this->host_desc_.ToString(), true);
        if (!passwd.IsOk()) {
            this->Close();
            return;
        }
        this->sftp_thread_channel_->Put(SftpThreadCmdPassword{passwd});
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, ID_SFTP_THREAD_RESPONSE_NEED_PASSWD);

    // Sftp thread will trigger this callback after successfully getting a directory list.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseGetDir>();
        this->current_dir_list_ = r.dir_list;
        this->current_dir_ = r.dir;
        this->path_text_ctrl_->SetValue(wxString::FromUTF8(r.dir));
        this->SortAndPopulateDir();
        this->RecallSelected();
        if (this->latest_interesting_status_.empty()) {
            auto d = wxDateTime::Now().FormatISOCombined(' ');
            this->latest_interesting_status_ = "Refreshed dir list at " + d + ".";
        }
        this->SetIdleStatusText();
    }, ID_SFTP_THREAD_RESPONSE_GET_DIR);

    // Sftp thread will trigger this callback after successfully downloading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseDownload>();

        string d = string(wxDateTime::Now().FormatISOCombined(' '));
        this->latest_interesting_status_ = "Downloaded " + r.remote_path + " at " + d + ".";
        this->RefreshDir(this->current_dir_, true);

        if (!r.open_in_editor) {
            return;
        }

        // Is remote_path already a key in opened_files_local_?
        if (this->opened_files_local_.find(r.remote_path) != this->opened_files_local_.end()) {
            // Previously downloaded, so just update the modified time.

            this->opened_files_local_[r.remote_path].modified = last_write_time(localPathUnicode(r.local_path));
        } else {
            OpenedFile f;
            f.local_path = r.local_path;
            f.remote_path = r.remote_path;
            f.modified = last_write_time(localPathUnicode(r.local_path));
            this->opened_files_local_[r.remote_path] = f;
        }

        string editor = string(this->config_->Read("/editor", ""));
        if (editor.empty()) {
            string msg = "No text editor configured. Set one in Preferences.";
            editor = guessTextEditor();
            if (!editor.empty()) {
                msg += "\nDefaulting to \"" + editor + "\".";
            }
            wxMessageBox(wxString::FromUTF8(msg), "Text editor configuration", wxOK | wxICON_INFORMATION, this);
            if (editor.empty()) {
                return;
            }
        }
        string path = regex_replace(r.local_path, regex("\""), "\\\"");
        wxExecute(wxString::FromUTF8(editor + " \"" + path + "\""), wxEXEC_ASYNC);
    }, ID_SFTP_THREAD_RESPONSE_DOWNLOAD);

    // Sftp thread will trigger this callback after successfully uploading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseUpload>();

        string d = string(wxDateTime::Now().FormatISOCombined(' '));
        this->latest_interesting_status_ = "Uploaded " + r.remote_path + " at " + d + ".";
        this->RefreshDir(this->current_dir_, true);

        if (this->opened_files_local_.find(r.remote_path) != this->opened_files_local_.end()) {
            // TODO(allan): catch if a file gets written again after the upload starts but before it completes
            auto local_path = this->opened_files_local_[r.remote_path].local_path;
            this->opened_files_local_[r.remote_path].modified = last_write_time(localPathUnicode(local_path));
            this->opened_files_local_[r.remote_path].upload_requested = false;
        }
    }, ID_SFTP_THREAD_RESPONSE_UPLOAD);

    // Sftp thread will trigger this callback when a transfer was successfully cancelled by the user.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->latest_interesting_status_ = "Cancelled transfer.";
        this->SetIdleStatusText();
        this->RefreshDir(this->current_dir_, true);
    }, ID_SFTP_THREAD_RESPONSE_CANCELLED);

    // Sftp thread will trigger this callback when we need to follow a directory symlink.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        auto r = event.GetPayload<SftpThreadResponseFollowSymlinkDir>();
        this->busy_cursor_ = nullptr;
        this->latest_interesting_status_ = "Followed directory symlink: " + r.symlink_path;
        this->SetIdleStatusText();
        this->ChangeDir(r.real_path);
    }, ID_SFTP_THREAD_RESPONSE_FOLLOW_SYMLINK_DIR);

    // Sftp thread will trigger this callback when it was asked to go to a path (file or directory).
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        auto r = event.GetPayload<SftpThreadResponseGoTo>();
        this->busy_cursor_ = nullptr;

        if (r.is_dir) {
            this->ChangeDir(r.remote_path);
        } else {
            this->DownloadFileForEdit(r.remote_path);
        }
    }, ID_SFTP_THREAD_RESPONSE_GO_TO);

    // Sftp thread will trigger this callback on general errors while downloading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();
        auto s = wxString::FromUTF8("Failed to download " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Retry", "Ignore");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(r.cmd);
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            // User requested to ignore this download failure.
            this->SetStatusText(s);
        }
    }, ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED);

    // Sftp thread will trigger this callback on permission errors while downloading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();
        auto s = wxString::FromUTF8("Permission denied when downloading " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Retry", "Ignore");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(r.cmd);
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            // User requested to ignore this download failure.
            this->SetStatusText(s);
        }
    }, ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED_PERMISSION);

    // Sftp thread will trigger this callback on general errors while uploading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();
        auto s = wxString::FromUTF8("Failed to upload " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Retry", "Ignore");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(r.cmd);
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            // User requested to ignore this upload failure.
            if (this->opened_files_local_.find(r.remote_path) != this->opened_files_local_.end()) {
                auto local_path = this->opened_files_local_[r.remote_path].local_path;
                this->opened_files_local_[r.remote_path].modified = last_write_time(localPathUnicode(local_path));
                this->opened_files_local_[r.remote_path].upload_requested = false;
                this->SetStatusText(s);
            }
        }
    }, ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED);

    // Sftp thread will trigger this callback on permission errors on a remote file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();
        auto s = wxString::FromUTF8("Permission denied on " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Retry", "Ignore");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(r.cmd);
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            // User requested to ignore this upload failure.
            if (this->opened_files_local_.find(r.remote_path) != this->opened_files_local_.end()) {
                auto local_path = this->opened_files_local_[r.remote_path].local_path;
                this->opened_files_local_[r.remote_path].modified = last_write_time(localPathUnicode(local_path));
                this->opened_files_local_[r.remote_path].upload_requested = false;
                this->SetStatusText(s);
            }
        }
    }, ID_SFTP_THREAD_RESPONSE_PERMISSION);

    // Sftp thread will trigger this callback on disk space errors while uploading a file.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();
        auto s = wxString::FromUTF8("Insufficient disk space failure while uploading " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_ERROR | wxCENTER);
        dialog.SetYesNoLabels("Retry", "Ignore");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(r.cmd);
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        } else {
            // User requested to ignore this upload failure.
            if (this->opened_files_local_.find(r.remote_path) != this->opened_files_local_.end()) {
                auto local_path = this->opened_files_local_[r.remote_path].local_path;
                this->opened_files_local_[r.remote_path].modified = last_write_time(localPathUnicode(local_path));
                this->opened_files_local_[r.remote_path].upload_requested = false;
                this->SetStatusText(s);
            }
        }
    }, ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED_SPACE);

    // Sftp thread will trigger this callback when confirmation for overwriting a file is needed.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseConfirmOverwrite>();
        auto s = wxString::FromUTF8("Remote file already exists: " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxYES_NO | wxICON_QUESTION | wxCENTER);
        dialog.SetYesNoLabels("Replace", "Cancel");
        if (dialog.ShowModal() == wxID_YES) {
            this->sftp_thread_channel_->Put(SftpThreadCmdUploadOverwrite{r.local_path, r.remote_path});
            this->busy_cursor_ = make_unique<wxBusyCursor>();
        }
    }, ID_SFTP_THREAD_RESPONSE_CONFIRM_OVERWRITE);

    // Sftp thread will trigger this callback on disk space errors while listing a directory.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();

        // Make a dummy parent dir entry to make it easy to get back to the parent dir.
        if (this->current_dir_list_.size() == 0) {
            DirEntry parent_dir_entry;
            parent_dir_entry.name_ = "..";
            parent_dir_entry.is_dir_ = true;
            this->dir_list_ctrl_->Refresh(vector<DirEntry>{parent_dir_entry});
        }

        auto s = wxString::FromUTF8("Permission denied while listing directory " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        this->SetStatusText(s);
    }, ID_SFTP_THREAD_RESPONSE_DIR_LIST_FAILED);

    // Sftp thread will trigger this callback when deletion succeeded.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->latest_interesting_status_ = "";
        this->SetIdleStatusText();

        // Set highligted to be either after or before the deleted item.
        int highlighted = this->dir_list_ctrl_->GetHighlighted();
        if (highlighted + 1 < this->current_dir_list_.size()) {
            this->dir_list_ctrl_->SetHighlighted(highlighted + 1);
        } else {
            this->dir_list_ctrl_->SetHighlighted(highlighted - 1);
        }

        this->RefreshDir(this->current_dir_, true);
    }, ID_SFTP_THREAD_RESPONSE_DELETE_SUCCEEDED);

    // Sftp thread will trigger this callback when deletion failed.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseDeleteError>();

        auto s = wxString::FromUTF8("Failed to delete " + r.remote_path + ":\n" + r.err);
        wxMessageDialog dialog(this, s, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        this->SetStatusText(s);
    }, ID_SFTP_THREAD_RESPONSE_DELETE_FAILED);

    // Sftp thread will trigger this callback when a file or directory was not found.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseFileError>();

        // Make a dummy parent dir entry to make it easy to get back to the parent dir.
        if (this->current_dir_list_.size() == 0) {
            DirEntry parent_dir_entry;
            parent_dir_entry.name_ = "..";
            parent_dir_entry.is_dir_ = true;
            this->dir_list_ctrl_->Refresh(vector<DirEntry>{parent_dir_entry});
        }

        auto s = wxString::FromUTF8("File or directory not found: " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        this->SetStatusText(s);
    }, ID_SFTP_THREAD_RESPONSE_FILE_NOT_FOUND);

    // Sftp thread will trigger this callback when a directory with the requested name already exists.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto r = event.GetPayload<SftpThreadResponseDirectoryAlreadyExists>();

        auto s = wxString::FromUTF8("Directory already exists: " + r.remote_path);
        wxMessageDialog dialog(this, s, "Error", wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        this->SetStatusText(s);
    }, ID_SFTP_THREAD_RESPONSE_DIR_ALREADY_EXISTS);

    // Sftp thread will trigger this callback when sudo required a password.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto msg = "Sudo requires a password for root elevation.\n\nEnter password for " + this->host_desc_.username_;
        auto passwd = this->PasswordPrompt(msg, true);
        if (!passwd.IsOk()) {
            this->sudo_ = false;
            this->tool_bar_->ToggleTool(this->sudo_btn_->GetId(), this->sudo_);
            this->RefreshTitle();
            this->SetIdleStatusText();
            return;
        }
        this->sftp_thread_channel_->Put(SftpThreadCmdSudo{passwd});
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }, ID_SFTP_THREAD_RESPONSE_SUDO_NEEDS_PASSWD);

    // Sftp thread will trigger this callback when sudo elevation succeeds.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->sudo_ = true;
        this->tool_bar_->ToggleTool(this->sudo_btn_->GetId(), this->sudo_);
        this->RefreshTitle();
        this->SetIdleStatusText();
    }, ID_SFTP_THREAD_RESPONSE_SUDO_SUCCEEDED);

    // Sftp thread will trigger this callback when sudo elevation fails.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->sudo_ = false;
        this->tool_bar_->ToggleTool(this->sudo_btn_->GetId(), this->sudo_);
        this->RefreshTitle();
        auto r = event.GetPayload<SftpThreadResponseError>();
        wxMessageDialog dialog(
                this,
                "Sudo elevation failed.\n\n" + r.error,
                "Error",
                wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        this->SetIdleStatusText();
    }, ID_SFTP_THREAD_RESPONSE_SUDO_FAILED);

    // Sftp thread will trigger this callback when sudo exit succeeds.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->sudo_ = false;
        this->tool_bar_->ToggleTool(this->sudo_btn_->GetId(), this->sudo_);
        this->RefreshTitle();
        this->SetIdleStatusText();
    }, ID_SFTP_THREAD_RESPONSE_SUDO_EXIT_SUCCEEDED);

    // Sftp thread will trigger this callback when a general command was successful (for example rename).
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        this->latest_interesting_status_ = "";
        this->SetIdleStatusText();
        this->RefreshDir(this->current_dir_, true);
    }, ID_SFTP_THREAD_RESPONSE_SUCCESS);

    // Sftp thread will trigger this callback on an error that requires us to reconnect.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = make_unique<wxBusyCursor>();
        this->RequestUserAttention(wxUSER_ATTENTION_ERROR);
        auto r = event.GetPayload<SftpThreadResponseError>();
        auto error = PrettifySentence(r.error);

        this->reconnect_timer_error_ = error;
        this->SetStatusText(wxString::FromUTF8(error + " Reconnecting in 5 seconds..."));
        this->reconnect_timer_countdown_ = 5 - 1;
        this->reconnect_timer_.Start(1000);
    }, ID_SFTP_THREAD_RESPONSE_ERROR_CONNECTION);

    // Sftp thread will trigger this callback on auth errors.
    this->Bind(wxEVT_THREAD, [&](wxThreadEvent &event) {
        this->busy_cursor_ = nullptr;
        auto passwd = this->PasswordPrompt(
                "Failed to authenticate.\n\nEnter password for " + this->host_desc_.ToString(), false);
        if (!passwd.IsOk()) {
            this->Close();
            return;
        }
        this->sftp_thread_channel_->Put(SftpThreadCmdPassword{passwd});
    }, ID_SFTP_THREAD_RESPONSE_ERROR_AUTH);
}

void FileManagerFrame::OnItemActivated() {
    if (this->busy_cursor_) {
        return;
    }

    int item = this->dir_list_ctrl_->GetHighlighted();
    auto entry = this->current_dir_list_[item];
    auto path = normalize_path(this->current_dir_ + "/" + entry.name_);
    if (entry.is_dir_) {
        this->ChangeDir(path);
    } else {
        this->DownloadFileForEdit(path);
    }
}

void FileManagerFrame::ChangeDir(string path) {
    // Add current directory to history.
    this->prev_dirs_.push(this->current_dir_);
    while (!this->fwd_dirs_.empty()) {
        this->fwd_dirs_.pop();
    }
    this->tool_bar_->EnableTool(wxID_BACKWARD, this->prev_dirs_.size() > 0);
    this->tool_bar_->EnableTool(wxID_FORWARD, this->fwd_dirs_.size() > 0);

    this->current_dir_ = path;
    this->path_text_ctrl_->SetValue(wxString::FromUTF8(path));
    this->current_dir_list_.clear();
    this->dir_list_ctrl_->Refresh(vector<DirEntry>{});
    this->RefreshDir(path, false);
}

void FileManagerFrame::SetIdleStatusText() {
    string s = to_string(this->current_dir_list_.size()) + " items";
    if (!this->latest_interesting_status_.empty()) {
        s += ". " + this->latest_interesting_status_;
    }
    this->SetStatusText(wxString::FromUTF8(s));
}

void FileManagerFrame::RefreshTitle() {
    if (this->sudo_) {
        this->SetTitle("FilesRemote - " + this->host_desc_.ToString() + " (sudo)");
    } else {
        this->SetTitle("FilesRemote - " + this->host_desc_.ToString());
    }
}

void FileManagerFrame::UploadWatchedFile(string remote_path) {
    OpenedFile f = this->opened_files_local_[remote_path];
    this->sftp_thread_channel_->Put(SftpThreadCmdUploadOverwrite{f.local_path, f.remote_path});
    this->opened_files_local_[f.remote_path].upload_requested = true;
    this->SetStatusText(wxString::FromUTF8("Uploading " + f.remote_path + " ... Press Esc to cancel."));
    this->busy_cursor_ = make_unique<wxBusyCursor>();
}

void FileManagerFrame::UploadFile(string local_path) {
    string name = basename(local_path);
    string remote_path = normalize_path(this->current_dir_ + "/" + name);
    this->sftp_thread_channel_->Put(SftpThreadCmdUpload{local_path, remote_path});
    this->SetStatusText(wxString::FromUTF8("Uploading " + remote_path) + " ... Press Esc to cancel.");
    this->busy_cursor_ = make_unique<wxBusyCursor>();
}

void FileManagerFrame::OnFileWatcherTimer(const wxTimerEvent &event) {
    if (this->busy_cursor_) {
        return;
    }

    for (auto o : this->opened_files_local_) {
        OpenedFile f = o.second;
        auto local_path = f.local_path;
        if (!f.upload_requested && last_write_time(localPathUnicode(local_path)) > f.modified) {
            this->UploadWatchedFile(f.remote_path);
        }
    }
}

void FileManagerFrame::RememberSelected() {
    this->stored_highlighted_ = this->current_dir_list_[this->dir_list_ctrl_->GetHighlighted()].name_;
    this->stored_selected_.clear();
    auto r = this->dir_list_ctrl_->GetSelected();
    for (int i = 0; i < r.size(); ++i) {
        this->stored_selected_.insert(this->current_dir_list_[r[i]].name_);
    }
}

void FileManagerFrame::RecallSelected() {
    int highlighted = 0;
    vector<int> selected;
    for (int i = 0; i < this->current_dir_list_.size(); ++i) {
        if (this->stored_selected_.find(this->current_dir_list_[i].name_) != this->stored_selected_.end()) {
            selected.push_back(i);
        }
        if (this->current_dir_list_[i].name_ == this->stored_highlighted_) {
            highlighted = i;
        }
    }
    this->dir_list_ctrl_->SetHighlighted(highlighted);
    this->dir_list_ctrl_->SetSelected(selected);
}

void FileManagerFrame::RefreshDir(string remote_path, bool preserve_selection) {
    if (this->busy_cursor_) {
        return;
    } else {
        this->busy_cursor_ = make_unique<wxBusyCursor>();
    }

    this->SetStatusText("Retrieving directory list...");

    if (preserve_selection) {
        this->RememberSelected();
    } else {
        this->stored_selected_.clear();
        this->stored_highlighted_ = "";
    }

    this->sftp_thread_channel_->Put(SftpThreadCmdGetDir{remote_path});
}

void FileManagerFrame::SortAndPopulateDir() {
    auto cmp = [&](const DirEntry &a, const DirEntry &b) {
        if (a.name_ == "..") { return true; }
        if (b.name_ == "..") { return false; }
        if (a.is_dir_ && !b.is_dir_) { return true; }
        if (!a.is_dir_ && b.is_dir_) { return false; }

        string a_val, b_val;
        if (this->sort_column_ == 1) {
            if (this->sort_desc_) {
                return a.size_ > b.size_;
            }
            return a.size_ < b.size_;
        } else if (this->sort_column_ == 2) {
            if (this->sort_desc_) {
                return a.modified_ > b.modified_;
            }
            return a.modified_ < b.modified_;
        } else if (this->sort_column_ == 3) {
            if (this->sort_desc_) {
                return a.mode_str_ > b.mode_str_;
            }
            return a.mode_str_ < b.mode_str_;
        } else if (this->sort_column_ == 4) {
            if (this->sort_desc_) {
                return a.owner_ > b.owner_;
            }
            return a.owner_ < b.owner_;
        } else if (this->sort_column_ == 5) {
            if (this->sort_desc_) {
                return a.group_ > b.group_;
            }
            return a.group_ < b.group_;
        }

        // Assume sort_column == 0.
        if (a.name_.length() > 0 && b.name_.length() > 0 && a.name_[0] == '.' &&
            b.name_[0] != '.') { return true; }
        if (a.name_.length() > 0 && b.name_.length() > 0 && a.name_[0] != '.' &&
            b.name_[0] == '.') { return false; }
        if (this->sort_desc_) {
            return a.name_ > b.name_;
        }
        return a.name_ < b.name_;
    };
    sort(this->current_dir_list_.begin(), this->current_dir_list_.end(), cmp);

    this->dir_list_ctrl_->Refresh(this->current_dir_list_);
}

void FileManagerFrame::DownloadFileForEdit(string remote_path) {
    remote_path = normalize_path(remote_path);
    string local_path = normalize_path(this->local_tmp_ + "/" + remote_path);
#ifdef __WXMSW__
    // Fallback filename for chars Windows cannot handle.
    if (regex_search(remote_path, regex("[\\\\:*?\"<>|]"))) {
        local_path = normalize_path(this->local_tmp_ + "/" + sha256(remote_path));
    }
#endif
    string local_dir = normalize_path(local_path + "/..");
    // TODO(allan): restrict permissions
    // TODO(allan): handle local file creation error seperately from a connection errors
    create_directories(localPathUnicode(local_dir));

    this->sftp_thread_channel_->Put(SftpThreadCmdDownload{local_path, remote_path, true});
    this->SetStatusText(wxString::FromUTF8("Downloading " + remote_path) + " ... Press Esc to cancel.");
    this->busy_cursor_ = make_unique<wxBusyCursor>();
}

void FileManagerFrame::DownloadFile(string remote_path, string local_path) {
    remote_path = normalize_path(remote_path);
    this->sftp_thread_channel_->Put(SftpThreadCmdDownload{local_path, remote_path, false});
    this->SetStatusText(wxString::FromUTF8("Downloading " + remote_path) + " ... Press Esc to cancel.");
    this->busy_cursor_ = make_unique<wxBusyCursor>();
}

bool FileManagerFrame::ValidateFilename(string filename) {
    if (regex_search(filename, regex("[/]")) || regex_match(filename, regex("\\s*"))) {
        wxMessageDialog dialog(
                this,
                "Invalid name: " + wxString::FromUTF8(filename),
                "Error",
                wxOK | wxICON_ERROR | wxCENTER);
        dialog.ShowModal();
        return false;
    }

    return true;
}

wxSecretValue FileManagerFrame::PasswordPrompt(string msg, bool try_saved) {
    string host_nocol = this->host_desc_.entered_;
    replace(host_nocol.begin(), host_nocol.end(), ':', '_');
    auto secret_store = wxSecretStore::GetDefault();
    bool secret_store_ok = secret_store.IsOk(NULL);
    auto key = "filesremote/" + host_nocol;

    wxSecretValue passwd;

    if (try_saved && secret_store_ok) {
        wxString user;
        secret_store.Load(key, user, passwd);
    }

    if (!passwd.IsOk()) {
        this->RequestUserAttention(wxUSER_ATTENTION_INFO);

        PasswordDialog password_dialog(this, msg, secret_store_ok);
        if (password_dialog.ShowModal() != wxID_OK) {
            return wxSecretValue();
        }

        passwd = password_dialog.value_;
        if (secret_store_ok) {
            if (password_dialog.save_passwd_) {
                secret_store.Save(key, this->host_desc_.username_, passwd);
            } else {
                secret_store.Delete(key);
            }
        }
    }

    return passwd;
}

// Wraps wxArtProvider::GetBitmap and sets the scale factor of the wxBitmap.
wxBitmap FileManagerFrame::GetBitmap(const wxArtID &id, const wxArtClient &client, const wxSize &size) {
    double scale_factor = this->GetContentScaleFactor();
    auto bmp = wxArtProvider::GetBitmap(id, client, size * scale_factor);

    // Scale factor on wxBitmaps doesn't seem to do anything on Win and MacOS, but it's needed on GTK.
    return wxBitmap(bmp.ConvertToImage(), -1, scale_factor);
}
