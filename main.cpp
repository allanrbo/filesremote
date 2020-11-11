// Copyright 2020 Allan Riordan Boll

#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <filesystem>
#include <sstream>

#ifdef __WXMSW__

#include <winsock2.h>

#else

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#ifdef __WXGTK__

#include "icon/icon_48x48.xpm"

#endif


#include <stdio.h>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/cmdline.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/preferences.h>
#include <wx/dataview.h>
#include <wx/aboutdlg.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "./licensestrings.h"

using std::string;
using std::to_string;
using std::runtime_error;
using std::vector;
using std::unordered_set;
using std::copy;
using std::unique_ptr;
using std::stringstream;
using std::make_unique;
using std::filesystem::file_time_type;
using std::filesystem::create_directories;
using std::filesystem::last_write_time;
using std::filesystem::remove;
using std::exception;

#define BUFLEN 4096
#define ID_SET_DIR 10
#define ID_PARENT_DIR 30
#define ID_SHOW_LICENSES 40
#define ID_OPEN_SELECTED 50


void showException() {
    wxString error;
    try {
        throw;  // Rethrow the current exception in order to pattern match it here.
    } catch (const std::exception &e) {
        error = e.what();
    } catch (...) {
        error = "unknown error";
    }

    wxLogError("%s", error);
}


string normalize_path(string path) {
    // TODO(allan): support UTF-8 paths...
    replace(path.begin(), path.end(), '\\', '/');

    stringstream s(path);
    string segment;
    vector<string> parts;
    while (getline(s, segment, '/')) {
        if (segment.empty() || segment == ".") {
            continue;
        } else if (segment == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            } else {
                continue;
            }
        } else {
            parts.push_back(segment);
        }
    }

    string r = "";
    for (int i = 0; i < parts.size(); ++i) {
        if (i == 0 && parts[0].length() == 2 && parts[0][1] == ':') {
            // Windows drive letter part.
        } else {
            r += "/";
        }
        r += parts[i];
    }

    if (r.empty()) {
        return "/";
    }

    return r;
}


class DirEntry {
public:
    string name_;
    uint64_t size_ = 0;
    uint64_t modified_ = 0;
    uint64_t mode_;
    string mode_str_;
    string owner_;
    string group_;
    bool isDir_;

    string ModifiedFormatted() {
        auto t = wxDateTime((time_t) this->modified_);
        t.MakeUTC();
        return t.FormatISOCombined(' ').ToStdString();
    }
};


class UploadOpenFailed : public exception {
};


typedef std::function<string()> PasswordPromptCb;

class SftpConnection {
private:
    LIBSSH2_SESSION *session_ = NULL;
    LIBSSH2_SFTP *sftp_session_ = NULL;
    LIBSSH2_SFTP_HANDLE *sftp_opendir_handle_ = NULL;
    LIBSSH2_SFTP_HANDLE *sftp_openfile_handle_ = NULL;
    FILE *local_file_handle_ = 0;
    int sock_ = 0;

public:
    string home_dir_ = "";
    string username_;
    string host_;
    int port_;

    SftpConnection(string username, string host, int port, PasswordPromptCb passwordPromptCb) {
        this->username_ = username;
        this->host_ = host;
        this->port_ = port;

        int rc;

#ifdef __WXMSW__
        WSADATA wsadata;
        rc = WSAStartup(MAKEWORD(2, 0), &wsadata);
        if (rc != 0) {
            throw runtime_error("WSAStartup failed (" + to_string(rc) + ")");
        }
#endif

        rc = libssh2_init(0);
        if (rc != 0) {
            throw runtime_error("libssh2_init failed (" + to_string(rc) + ")");
        }

        this->sock_ = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        sin.sin_addr.s_addr = inet_addr(host.c_str());
        if (sin.sin_addr.s_addr == INADDR_NONE) {
            struct hostent *remote_host = gethostbyname(host.c_str());
            if (!remote_host) {
                throw runtime_error("gethostbyname failed");
            }

            sin.sin_addr.s_addr = *reinterpret_cast<u_long *>(remote_host->h_addr_list[0]);
        }

        if (connect(this->sock_, (struct sockaddr *) (&sin), sizeof(struct sockaddr_in)) != 0) {
            throw runtime_error("connect failed");
        }

        this->session_ = libssh2_session_init();
        if (!this->session_) {
            throw runtime_error("libssh2_session_init failed");
        }

        libssh2_session_set_blocking(this->session_, 1);

        rc = libssh2_session_handshake(this->session_, this->sock_);
        if (rc) {
            throw runtime_error("libssh2_session_handshake failed (" + to_string(rc) + ")");
        }

        // TODO(allan): verify fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
        //// char *userauthlist = libssh2_userauth_list(this->session, username.c_str(), username.size());


        if (!this->AgentAuth()) {
            string passwd = passwordPromptCb();
            if (libssh2_userauth_password(this->session_, this->username_.c_str(), passwd.c_str())) {
                throw runtime_error("libssh2_userauth_password failed");
            }
        }


        this->sftp_session_ = libssh2_sftp_init(this->session_);
        if (!this->sftp_session_) {
            throw runtime_error("libssh2_sftp_init failed");
        }

        char buf[BUFLEN];
        rc = libssh2_sftp_realpath(this->sftp_session_, ".", buf, BUFLEN);
        if (rc < 0) {
            throw runtime_error("libssh2_sftp_realpath failed (" + to_string(rc) + ")");
        }
        this->home_dir_ = string(buf);
    }

    vector<DirEntry> GetDir(string path) {
        int rc;

        this->sftp_opendir_handle_ = libssh2_sftp_opendir(this->sftp_session_, path.c_str());
        if (!this->sftp_opendir_handle_) {
            throw runtime_error("libssh2_sftp_opendir failed");
        }

        auto files = vector<DirEntry>();
        while (1) {
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            char name[BUFLEN];
            char line[BUFLEN];
            memset(name, 0, BUFLEN);
            memset(line, 0, BUFLEN);

            rc = libssh2_sftp_readdir_ex(this->sftp_opendir_handle_, name, sizeof(name), line, sizeof(line), &attrs);

            if (rc == LIBSSH2_ERROR_EAGAIN) {
                continue;
            }
            if (rc == 0) {
                break;
            }
            if (rc < 0) {
                throw runtime_error("libssh2_sftp_readdir_ex failed (" + to_string(rc) + ")");
            }

            auto d = DirEntry();

            d.name_ = string(name);
            if (d.name_ == "." || d.name_ == "..") {
                continue;
            }

            if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
                d.size_ = attrs.filesize;
            }
            if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
                d.modified_ = attrs.mtime;
            }
            if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                d.mode_ = attrs.permissions;
                d.isDir_ = attrs.permissions & S_IFDIR;
            }

            // Extract user, group and mode string from the free text line.
            stringstream s(line);
            string segment;
            vector<string> parts;
            int field_num = 0;
            while (getline(s, segment, ' ')) {
                if (segment.empty()) {
                    continue;
                }

                if (field_num == 0) {
                    if (segment.length() != 10) {
                        // Free text line was in an unexpected format.
                        break;
                    }
                    d.mode_str_ = string(segment);
                }

                if (field_num == 2) {
                    d.owner_ = string(segment);
                }

                if (field_num == 3) {
                    d.group_ = string(segment);
                }

                field_num++;
            }

            files.push_back(d);
        }

        if (this->sftp_opendir_handle_) {
            libssh2_sftp_closedir(this->sftp_opendir_handle_);
            this->sftp_opendir_handle_ = NULL;
        }

        return files;
    }

    void DownloadFile(string remoteSrcPath, string localDstPath) {
        this->sftp_openfile_handle_ = libssh2_sftp_open(
                this->sftp_session_,
                remoteSrcPath.c_str(),
                LIBSSH2_FXF_READ,
                0);
        if (!this->sftp_openfile_handle_) {
            throw runtime_error("libssh2_sftp_open failed");
        }

        this->local_file_handle_ = fopen(localDstPath.c_str(), "wb");

        char buf[BUFLEN];
        while (1) {
            int rc = libssh2_sftp_read(this->sftp_openfile_handle_, buf, BUFLEN);
            if (rc > 0) {
                fwrite(buf, 1, rc, this->local_file_handle_);
                // TODO(allan): error handling for fwrite.
            } else if (rc == 0) {
                break;
            } else {
                throw runtime_error("libssh2_sftp_read failed");
            }
        }

        libssh2_sftp_close(this->sftp_openfile_handle_);

        fclose(this->local_file_handle_);
        this->local_file_handle_ = 0;
    }

    void UploadFile(string localSrcPath, string remoteDstPath) {
        this->sftp_openfile_handle_ = libssh2_sftp_open(
                this->sftp_session_,
                remoteDstPath.c_str(),
                LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC,
                0);
        if (!this->sftp_openfile_handle_) {
            throw UploadOpenFailed();
        }

        this->local_file_handle_ = fopen(localSrcPath.c_str(), "rb");

        char buf[BUFLEN];
        while (1) {
            int rc = fread(buf, 1, BUFLEN, this->local_file_handle_);
            if (rc > 0) {
                int nread = rc;
                char *p = buf;
                while (nread) {
                    rc = libssh2_sftp_write(this->sftp_openfile_handle_, buf, rc);
                    if (rc < 0) {
                        throw runtime_error("libssh2_sftp_write failed");
                    }
                    p += rc;
                    nread -= rc;
                }
            } else {
                // TODO(allan): error handling for fread.
                break;
            }
        }

        libssh2_sftp_close(this->sftp_openfile_handle_);

        fclose(this->local_file_handle_);
        this->local_file_handle_ = 0;
    }

    ~SftpConnection() {
        if (this->local_file_handle_) {
            fclose(this->local_file_handle_);
        }

        if (this->sftp_openfile_handle_) {
            libssh2_sftp_close(this->sftp_openfile_handle_);
        }

        if (this->sftp_opendir_handle_) {
            libssh2_sftp_closedir(this->sftp_opendir_handle_);
        }

        if (this->sftp_session_) {
            libssh2_sftp_shutdown(this->sftp_session_);
        }

        if (this->session_) {
            libssh2_session_disconnect(this->session_, "normal shutdown");
            libssh2_session_free(this->session_);
        }

        if (this->sock_) {
#ifdef __WXMSW__
            closesocket(this->sock_);
#else
            close(this->sock_);
#endif
        }

        libssh2_exit();
    }

private:
    bool AgentAuth() {
        LIBSSH2_AGENT *agent = libssh2_agent_init(this->session_);
        if (!agent) {
            return false;
        }

        if (libssh2_agent_connect(agent)) {
            return false;
        }

        if (libssh2_agent_list_identities(agent)) {
            return false;
        }

        struct libssh2_agent_publickey *identity, *prev_identity = NULL;
        while (1) {
            int rc = libssh2_agent_get_identity(agent, &identity, prev_identity);
            if (rc != 0) {
                return false;
            }

            if (libssh2_agent_userauth(agent, username_.c_str(), identity) == 0) {
                return true;
            }

            prev_identity = identity;
        }

        return false;
    }
};


typedef std::function<void(int)> OnItemActivatedCb;
typedef std::function<void(int)> OnColumnHeaderClickCb;

// A base class, because wxDataViewListCtrl looks best on MacOS, and wxListCtrl looks best on GTK and Windows.
class DirListCtrl {
protected:
    OnItemActivatedCb on_item_activated_cb_;
    OnColumnHeaderClickCb on_column_header_click_cb_;
    wxImageList *icons_image_list_;

    int IconIdx(DirEntry entry) {
        int r = 0;
        if (entry.isDir_) {
            r = 1;
        }
        return r;
    }

public:
    DirListCtrl() {
        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);
        this->icons_image_list_ = new wxImageList(size.GetWidth(), size.GetHeight(), false, 1);
        this->icons_image_list_->Add(ap.GetBitmap(wxART_NORMAL_FILE, wxART_LIST, size));
        this->icons_image_list_->Add(ap.GetBitmap(wxART_FOLDER, wxART_LIST, size));
    }

    virtual void Refresh(vector<DirEntry> entries) = 0;

    virtual wxControl *GetCtrl() = 0;

    virtual void SetFocus() = 0;

    virtual void ActivateCurrent() = 0;

    virtual vector<int> GetSelected() = 0;

    virtual void SetSelected(vector<int>) = 0;

    virtual int GetHighlighted() = 0;

    virtual void SetHighlighted(int) = 0;

    void BindOnItemActivated(OnItemActivatedCb cb) {
        this->on_item_activated_cb_ = cb;
    }

    void BindOnColumnHeaderClickCb(OnColumnHeaderClickCb cb) {
        this->on_column_header_click_cb_ = cb;
    }
};


class DvlcDirList : public DirListCtrl {
    wxDataViewListCtrl *dvlc_;

public:
    explicit DvlcDirList(wxWindow *parent) : DirListCtrl() {
        this->dvlc_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                             wxDV_MULTIPLE | wxDV_ROW_LINES);

        // TODO(allan): wxDATAVIEW_CELL_EDITABLE?
        this->dvlc_->AppendIconTextColumn("Name", wxDATAVIEW_CELL_INERT, 300);
        this->dvlc_->AppendTextColumn("Size", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc_->AppendTextColumn("Modified", wxDATAVIEW_CELL_INERT, 150);
        this->dvlc_->AppendTextColumn("Mode", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc_->AppendTextColumn("Owner", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc_->AppendTextColumn("Group", wxDATAVIEW_CELL_INERT, 100);

        this->dvlc_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [&](wxDataViewEvent &evt) {
            if (!evt.GetItem()) {
                return;
            }
            int i = this->dvlc_->GetItemData(evt.GetItem());
            this->on_item_activated_cb_(i);
        });

        this->dvlc_->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, [&](wxDataViewEvent &evt) {
            this->on_column_header_click_cb_(evt.GetColumn());
        });
    }

    void Refresh(vector<DirEntry> entries) {
        this->dvlc_->DeleteAllItems();

        for (int i = 0; i < entries.size(); i++) {
            wxIcon icon = this->icons_image_list_->GetIcon(this->IconIdx(entries[i]));

            wxVector<wxVariant> data;
            data.push_back(wxVariant(wxDataViewIconText(wxString::FromUTF8(entries[i].name_), icon)));
            data.push_back(wxVariant(to_string(entries[i].size_)));
            data.push_back(wxVariant(entries[i].ModifiedFormatted()));
            data.push_back(wxVariant(entries[i].mode_str_));
            data.push_back(wxVariant(entries[i].owner_));
            data.push_back(wxVariant(entries[i].group_));
            this->dvlc_->AppendItem(data, i);
        }
    }

    wxControl *GetCtrl() {
        return this->dvlc_;
    }

    void SetFocus() {
        this->dvlc_->SetFocus();
    }

    void ActivateCurrent() {
        if (this->dvlc_->GetCurrentItem()) {
            int i = this->dvlc_->GetItemData(this->dvlc_->GetCurrentItem());
            this->on_item_activated_cb_(i);
        }
    }

    vector<int> GetSelected() {
        vector<int> r;
        for (int i = 0; i < this->dvlc_->GetItemCount(); ++i) {
            if (this->dvlc_->IsRowSelected(i)) {
                r.push_back(i);
            }
        }
        return r;
    }

    void SetSelected(vector<int> selected) {
        wxDataViewItemArray a;
        for (int i = 0; i < selected.size(); ++i) {
            a.push_back(this->dvlc_->RowToItem(selected[i]));
        }
        this->dvlc_->SetSelections(a);
    }

    int GetHighlighted() {
        int i = this->dvlc_->ItemToRow(this->dvlc_->GetCurrentItem());
        if (i < 0) {
            return 0;
        }
        return i;
    }

    void SetHighlighted(int row) {
        auto item = this->dvlc_->RowToItem(row);
        if (item.IsOk()) {
            this->dvlc_->SetCurrentItem(item);
            this->dvlc_->EnsureVisible(item);
        }
    }
};


class LcDirList : public DirListCtrl {
    wxListCtrl *list_ctrl_;

public:
    explicit LcDirList(wxWindow *parent) : DirListCtrl() {
        this->list_ctrl_ = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);

        this->list_ctrl_->AssignImageList(this->icons_image_list_, wxIMAGE_LIST_SMALL);

        this->list_ctrl_->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        this->list_ctrl_->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl_->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 150);
        this->list_ctrl_->InsertColumn(3, "Mode", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl_->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl_->InsertColumn(5, "Owner", wxLIST_FORMAT_LEFT, 100);

        this->list_ctrl_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent &evt) {
            int i = this->list_ctrl_->GetItemData(evt.GetItem());
            this->on_item_activated_cb_(i);
        });

        this->list_ctrl_->Bind(wxEVT_LIST_COL_CLICK, [&](wxListEvent &evt) {
            this->on_column_header_click_cb_(evt.GetColumn());
        });
    }

    wxControl *GetCtrl() {
        return this->list_ctrl_;
    }

    void Refresh(vector<DirEntry> entries) {
        this->list_ctrl_->DeleteAllItems();

        for (int i = 0; i < entries.size(); i++) {
            this->list_ctrl_->InsertItem(i, entries[i].name_, this->IconIdx(entries[i]));
            this->list_ctrl_->SetItemData(i, i);
            this->list_ctrl_->SetItem(i, 0, wxString::FromUTF8(entries[i].name_));
            this->list_ctrl_->SetItem(i, 1, to_string(entries[i].size_));
            this->list_ctrl_->SetItem(i, 2, entries[i].ModifiedFormatted());
            this->list_ctrl_->SetItem(i, 3, entries[i].mode_str_);
            this->list_ctrl_->SetItem(i, 4, entries[i].owner_);
            this->list_ctrl_->SetItem(i, 5, entries[i].group_);
        }
    }

    void SetFocus() {
        this->list_ctrl_->SetFocus();
    }

    void ActivateCurrent() {
        if (this->list_ctrl_->GetSelectedItemCount() > 0) {
            int i = this->list_ctrl_->GetItemData(
                    this->list_ctrl_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED));
            this->on_item_activated_cb_(i);
        }
    }

    void SetSelected(vector<int> selected) {
        for (int i = 0; i < selected.size(); ++i) {
            this->list_ctrl_->SetItemState(selected[i], wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }
    }

    vector<int> GetSelected() {
        vector<int> r;
        int64_t cur = -1;
        while (1) {
            cur = this->list_ctrl_->GetNextItem(cur, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
            if (cur == -1) {
                break;
            }

            r.push_back(cur);
        }
        return r;
    }

    int GetHighlighted() {
        if (this->list_ctrl_->GetSelectedItemCount() > 0) {
            auto i = this->list_ctrl_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
            return this->list_ctrl_->GetItemData(i);
        }
        return 0;
    }

    void SetHighlighted(int row) {
        this->list_ctrl_->SetItemState(row, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
        if (row != 0) {
            this->list_ctrl_->EnsureVisible(row);
        }
    }
};


class PreferencesPageGeneralPanel : public wxPanel {
    wxConfigBase *config_;
    wxTextCtrl *text_editor_;

public:
    PreferencesPageGeneralPanel(wxWindow *parent, wxConfigBase *config) : wxPanel(parent) {
        this->config_ = config;

        auto *sizer = new wxBoxSizer(wxVERTICAL);

        auto *item_sizer_editor = new wxBoxSizer(wxHORIZONTAL);
        auto *label_editor = new wxStaticText(this, wxID_ANY, "Editor path:");
        item_sizer_editor->Add(label_editor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        item_sizer_editor->Add(5, 5, 1, wxALL, 0);
        this->text_editor_ = new wxTextCtrl(this, 100, wxEmptyString, wxDefaultPosition, wxSize(300, -1));
        this->Bind(wxEVT_TEXT, [&](wxCommandEvent &) {
            if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
                this->TransferDataFromWindow();
            }
        });
        item_sizer_editor->Add(this->text_editor_, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        sizer->Add(item_sizer_editor, 0, wxGROW | wxALL, 5);

        this->SetSizerAndFit(sizer);
    }

    virtual bool TransferDataToWindow() {
        this->text_editor_->SetValue(this->config_->Read("/editor", ""));
        return true;
    }

    virtual bool TransferDataFromWindow() {
        this->config_->Write("/editor", this->text_editor_->GetValue());
        this->config_->Flush();
        return true;
    }
};

class PreferencesPageGeneral : public wxStockPreferencesPage {
    wxConfigBase *config;

public:
    explicit PreferencesPageGeneral(wxConfigBase *config) : wxStockPreferencesPage(Kind_General) {
        this->config = config;
    }

    virtual wxWindow *CreateWindow(wxWindow *parent) {
        return new PreferencesPageGeneralPanel(parent, this->config);
    }
};


class OpenedFile {
public:
    string local_path_;
    string remote_path_;
    file_time_type modified_;
};


class SftpguiFrame : public wxFrame {
    unique_ptr<SftpConnection> sftp_connection_;
    wxConfigBase *config_;
    DirListCtrl *dir_list_ctrl_;
    wxTextCtrl *path_text_ctrl_;
    wxTimer file_watcher_timer_;
    string current_dir_ = "/";
    vector<DirEntry> current_dir_list_;
    int sort_column_ = 0;
    bool sort_desc_ = false;
    vector<OpenedFile> opened_files_local_;
    string stored_highlighted_ = "";
    unordered_set<string> stored_selected_;

public:
    SftpguiFrame(unique_ptr<SftpConnection> sftp_connection, wxConfigBase *config) : wxFrame(
            NULL,
            wxID_ANY,
            wxT("Sftpgui"),
            wxPoint(-1, -1),
            wxSize(800, 600)
    ) {
        this->sftp_connection_ = move(sftp_connection);
        this->current_dir_ = this->sftp_connection_->home_dir_;
        this->config_ = config;

#ifdef __WXMSW__
        this->SetIcon(wxIcon("aaaa"));
#elif __WXGTK__
        this->SetIcon(wxIcon(icon_48x48));
#endif

        this->SetTitle("Sftpgui - " + this->sftp_connection_->username_ + "@" + this->sftp_connection_->host_ +
                       ":" + to_string(this->sftp_connection_->port_));
        this->CreateStatusBar();

        // Restore window size and pos.
        int x = this->config_->Read("/window_x", -1);
        int y = this->config_->Read("/window_y", -1);
        int w = this->config_->Read("/window_w", 800);
        int h = this->config_->Read("/window_h", 600);
        this->Move(x, y);
        this->SetClientSize(w, h);

        // Create menus.
        auto *menuBar = new wxMenuBar();
        this->SetMenuBar(menuBar);

        auto *file_menu = new wxMenu();
        menuBar->Append(file_menu, "&File");

        // Adding refresh to the menu twice with two different hotkeys, instead of using SetAcceleratorTable.
        // It's wonky, but MacOS has trouble with non-menu accelerators when the wxDataViewListCtrl has focus.
        file_menu->Append(wxID_REFRESH, "Refresh\tF5");
        file_menu->Append(wxID_REFRESH, "Refresh\tCtrl+R");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->RefreshDir(true);
        }, wxID_REFRESH);

        file_menu->Append(ID_SET_DIR, "Change directory\tCtrl+L");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &) {
            this->path_text_ctrl_->SetFocus();
            this->path_text_ctrl_->SelectAll();
        }, ID_SET_DIR);

#ifdef __WXOSX__
        file_menu->Append(ID_PARENT_DIR, "Parent directory\tCtrl+Up", wxEmptyString, wxITEM_NORMAL);
#else
        file_menu->Append(ID_PARENT_DIR, "Parent directory\tAlt+Up", wxEmptyString, wxITEM_NORMAL);
#endif
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->current_dir_ = normalize_path(this->current_dir_ + "/..");
            this->path_text_ctrl_->SetValue(this->current_dir_);
            this->RefreshDir(false);
        }, ID_PARENT_DIR);

#ifdef __WXOSX__
        file_menu->Append(ID_OPEN_SELECTED, "Open selected item\tCtrl+Down", wxEmptyString, wxITEM_NORMAL);
#endif
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->dir_list_ctrl_->ActivateCurrent();
        }, ID_OPEN_SELECTED);

        file_menu->AppendSeparator();

        file_menu->Append(wxID_PREFERENCES);
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            auto prefs_editor = new wxPreferencesEditor();
            prefs_editor->AddPage(new PreferencesPageGeneral(this->config_));
            prefs_editor->Show(this);
        }, wxID_PREFERENCES);

        file_menu->AppendSeparator();

        file_menu->Append(wxID_EXIT, "E&xit", "Quit this program");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->Close(true);
        }, wxID_EXIT);

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
                    wxString::FromAscii(licenses),
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
            info.SetName("Sftpgui");
            info.SetVersion("0.1");
            info.SetDescription("A no-nonsense SFTP file browser");
            info.SetCopyright("(C) 2020 Allan Riordan Boll");
            wxAboutBox(info, this);
        }, wxID_ABOUT);

        // Most keyboard accelerators for menu items are automatically bound via the string in its title. However, some
        // seem to only work via SetAcceleratorTable, so setting them again here.
        // MacOS seems to ignores this table when the focus is on wxDataViewListCtrl, so we rely on the accelerators in
        // the menu item titles on MacOS.
        wxAcceleratorEntry entries[]{
                wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, wxID_REFRESH),
                wxAcceleratorEntry(wxACCEL_CTRL, 'R', wxID_REFRESH),
                wxAcceleratorEntry(wxACCEL_CTRL, 'L', ID_SET_DIR),
                wxAcceleratorEntry(wxACCEL_ALT, WXK_UP, ID_PARENT_DIR),
        };
        wxAcceleratorTable accel(sizeof(entries), entries);
        this->SetAcceleratorTable(accel);

        // Set up a timer that will watch for changes in local files.
        this->file_watcher_timer_.Bind(wxEVT_TIMER, &SftpguiFrame::OnFileWatcherTimer, this);
        this->file_watcher_timer_.Start(1000);

        // Main layout.
        auto *panel = new wxPanel(this);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        auto *sizer_inner_top = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(sizer_inner_top, 0, wxEXPAND | wxALL, 1);

        // Create remote path text field.
        this->path_text_ctrl_ = new wxTextCtrl(panel, wxID_ANY, this->current_dir_, wxDefaultPosition,
                                               wxDefaultSize, wxTE_PROCESS_ENTER);
#ifdef __WXOSX__
        sizer_inner_top->Add(this->path_text_ctrl_, 1, wxEXPAND | wxALL, 0);
#else
        sizer_inner_top->Add(this->path_text_ctrl_, 1, wxEXPAND | wxALL, 4);
#endif
        this->path_text_ctrl_->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent &event) {
            this->current_dir_ = this->path_text_ctrl_->GetValue();
            this->RefreshDir(false);
        });
        this->path_text_ctrl_->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent &evt) {
            if (evt.GetModifiers() == 0 && evt.GetKeyCode() == WXK_ESCAPE && this->path_text_ctrl_->HasFocus()) {
                this->path_text_ctrl_->SetValue(this->current_dir_);
                this->path_text_ctrl_->SelectNone();
                this->dir_list_ctrl_->SetFocus();
                return;
            }

            evt.Skip();
        });

#ifdef __WXOSX__
        // On MacOS wxDataViewListCtrl looks best.
        this->dir_list_ctrl_ = new DvlcDirList(panel);
#else
        // On GTK and Windows wxListCtrl looks best.
        this->dir_list_ctrl_ = new LcDirList(panel);
#endif
        sizer->Add(this->dir_list_ctrl_->GetCtrl(), 1, wxEXPAND | wxALL, 0);
        this->dir_list_ctrl_->SetFocus();

        this->dir_list_ctrl_->BindOnItemActivated([&](int n) {
            string status = "";
            try {
                auto entry = this->current_dir_list_[n];
                if (entry.isDir_) {
                    this->current_dir_ = normalize_path(this->current_dir_ + "/" + entry.name_);
                    this->path_text_ctrl_->SetValue(this->current_dir_);
                    this->RefreshDir(false);
                } else {
                    this->DownloadFile(entry.name_);
                }
            } catch (...) {
                showException();
            }
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
        this->RefreshDir(false);
    }

    ~SftpguiFrame() {
        for (int i = 0; i < this->opened_files_local_.size(); ++i) {
            remove(this->opened_files_local_[i].local_path_);
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
    }

private:
    void SetIdleStatusText(string additional = "") {
        string s = "Ready";
        if (!additional.empty()) {
            s += ". " + additional;
        }
        this->SetStatusText(s);
    }

    void OnFileWatcherTimer(const wxTimerEvent &event) {
        string last_upload = "";
        for (int i = 0; i < this->opened_files_local_.size(); ++i) {
            OpenedFile f = this->opened_files_local_[i];
            if (last_write_time(f.local_path_) > f.modified_) {
                try {
                    this->sftp_connection_->UploadFile(f.local_path_, f.remote_path_);
                } catch (UploadOpenFailed) {
                    wxLogError("Failed to write remote file. Possibly a permissions issue.");
                    this->RequestUserAttention(wxUSER_ATTENTION_ERROR);
                }
                this->opened_files_local_[i].modified_ = last_write_time(f.local_path_);
                last_upload = f.remote_path_;
            }
        }

        if (!last_upload.empty()) {
            this->RefreshDir(true);
            string d = string(wxDateTime::Now().FormatISOCombined());
            this->SetIdleStatusText("Uploaded " + last_upload + " at " + d + ".");
        }
    }

    void RememberSelected() {
        this->stored_highlighted_ = this->current_dir_list_[this->dir_list_ctrl_->GetHighlighted()].name_;
        this->stored_selected_.clear();
        auto r = this->dir_list_ctrl_->GetSelected();
        for (int i = 0; i < r.size(); ++i) {
            this->stored_selected_.insert(this->current_dir_list_[r[i]].name_);
        }
    }

    void RecallSelected() {
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

    void RefreshDir(bool preserve_selection) {
        if (preserve_selection) {
            this->RememberSelected();
        } else {
            this->stored_selected_.clear();
            this->stored_highlighted_ = "";
        }
        this->current_dir_list_ = this->sftp_connection_->GetDir(this->current_dir_);
        this->SortAndPopulateDir();
        this->SetIdleStatusText(string("Retrieved dir list at " + wxDateTime::Now().FormatISOCombined() + "."));
        this->RecallSelected();
    }

    void SortAndPopulateDir() {
        auto cmp = [&](const DirEntry &a, const DirEntry &b) {
            if (a.name_ == "..") { return true; }
            if (b.name_ == "..") { return false; }
            if (a.isDir_ && !b.isDir_) { return true; }
            if (!a.isDir_ && b.isDir_) { return false; }

            string a_val, b_val;
            if (this->sort_column_ == 1) {
                if (this->sort_desc_) {
                    return a.size_ < b.size_;
                }
                return a.size_ > b.size_;
            } else if (this->sort_column_ == 2) {
                if (this->sort_desc_) {
                    return a.modified_ < b.modified_;
                }
                return a.modified_ > b.modified_;
            } else if (this->sort_column_ == 3) {
                if (this->sort_desc_) {
                    return a.mode_str_ < b.mode_str_;
                }
                return a.mode_str_ > b.mode_str_;
            } else if (this->sort_column_ == 4) {
                if (this->sort_desc_) {
                    return a.owner_ < b.owner_;
                }
                return a.owner_ > b.owner_;
            } else if (this->sort_column_ == 5) {
                if (this->sort_desc_) {
                    return a.group_ < b.group_;
                }
                return a.group_ > b.group_;
            }

            // Assume sort_column == 0.
            if (a.name_.length() > 0 && b.name_.length() > 0 && a.name_[0] == '.' && b.name_[0] != '.') { return true; }
            if (a.name_.length() > 0 && b.name_.length() > 0 && a.name_[0] != '.' &&
                b.name_[0] == '.') { return false; }
            if (this->sort_desc_) {
                return a.name_ < b.name_;
            }
            return a.name_ > b.name_;
        };
        sort(this->current_dir_list_.begin(), this->current_dir_list_.end(), cmp);

        this->dir_list_ctrl_->Refresh(this->current_dir_list_);
    }

    void DownloadFile(string name) {
        string editor = string(this->config_->Read("/editor", ""));
        if (editor.empty()) {
            string msg = "No text editor configured. Set one in Preferences.";
            wxMessageBox(msg, "Notice", wxOK | wxICON_INFORMATION, this);
            return;
        }

        string remote_path = normalize_path(this->current_dir_ + "/" + name);
        string local_tmp = string(wxStandardPaths::Get().GetTempDir());
        string local_dir = normalize_path(local_tmp + "/sftpgui/" +
                                          this->sftp_connection_->username_ + "@" + this->sftp_connection_->host_ +
                                          "_" +
                                          to_string(this->sftp_connection_->port_) + "/" + this->current_dir_);
        string local_path = normalize_path(local_dir + "/" + name);

        // TODO(allan): restrict permissions
        create_directories(local_dir);

        this->sftp_connection_->DownloadFile(remote_path, local_path);

        bool previously_downloaded = false;
        for (int i = 0; i < this->opened_files_local_.size(); ++i) {
            if (this->opened_files_local_[i].remote_path_ == remote_path) {
                this->opened_files_local_[i].modified_ = last_write_time(local_path);
                previously_downloaded = true;
                break;
            }
        }
        if (!previously_downloaded) {
            OpenedFile f;
            f.local_path_ = local_path;
            f.remote_path_ = remote_path;
            f.modified_ = last_write_time(local_path);
            this->opened_files_local_.push_back(f);
        }

        string d = string(wxDateTime::Now().FormatISOCombined());
        this->SetIdleStatusText("Downloaded " + remote_path + " at " + d + ".");

        wxExecute(wxString::FromUTF8(editor + " \"" + local_path + "\""), wxEXEC_ASYNC);
    }
};


class SftpguiApp : public wxApp {
    string host_;
    string username_;
    int port_ = 22;

public:
    bool OnInit() {
        try {
            if (!wxApp::OnInit())
                return false;

            auto es = wxEmptyString;
            wxFileConfig *config = new wxFileConfig("sftpgui", es, es, es, wxCONFIG_USE_LOCAL_FILE);
            config->EnableAutoSave();
            config->SetRecordDefaults();
            wxConfigBase::Set(config);

            if (host_.empty()) {
                wxTextEntryDialog dialog(0,
                                         "Enter remote host.\n"
                                         "Format: [username@]host:port\n"
                                         "Defaults to current local username and port 22 if not specified.",
                                         "Sftpgui");
                if (dialog.ShowModal() == wxID_CANCEL) {
                    return false;
                }
                if (!this->ParseHost(string(dialog.GetValue()))) {
                    return false;
                }
            }

            auto passwdCb = [&](void) {
                auto s = "Enter password for " + this->username_ + "@" + this->host_ + ":" + to_string(this->port_);
                auto passwd = wxGetPasswordFromUser(s, "Sftpgui", wxEmptyString, 0);
                return passwd.ToStdString();
            };

            auto sftpConnection = make_unique<SftpConnection>(username_, host_, port_, passwdCb);

            wxFrame *frame = new SftpguiFrame(move(sftpConnection), config);
            frame->Show();
        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    virtual void OnInitCmdLine(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[user@]host:port", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
    }

    virtual bool OnCmdLineParsed(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        if (parser.GetParamCount() > 0) {
            if (!this->ParseHost(string(parser.GetParam(0)))) {
                return false;
            }
        }

        return true;
    }

    virtual bool OnExceptionInMainLoop() {
        showException();
        return false;
    }

private:
    bool ParseHost(string host) {
        this->username_ = wxGetUserId();
        this->host_ = host;
        this->port_ = 22;

#ifdef __WXMSW__
        transform(this->username_.begin(), this->username_.end(), this->username_.begin(), ::tolower);
#endif

        if (this->host_.find("@") != string::npos) {
            int i = this->host_.find("@");
            this->username_ = this->host_.substr(0, i);
            this->host_ = this->host_.substr(i + 1);
        }

        if (this->host_.find(":") != string::npos) {
            int i = this->host_.find(":");

            string ps = string(this->host_.substr(i + 1));
            if (!std::all_of(ps.begin(), ps.end(), ::isdigit)) {
                wxLogFatalError("non-digit port number");
                return false;
            }
            this->port_ = stoi(string(ps));
            if (!(0 < this->port_ && this->port_ < 65536)) {
                wxLogFatalError("invalid port number");
                return false;
            }

            this->host_ = this->host_.substr(0, i);
        }

        return true;
    }
};

IMPLEMENT_APP(SftpguiApp)
